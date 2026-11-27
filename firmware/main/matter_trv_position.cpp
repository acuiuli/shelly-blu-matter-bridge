// SPDX-License-Identifier: Apache-2.0
#include "matter_trv_position.h"
#include "trv_binding_store.h"
#include "trv_gatt_probe.h"
#include <app/clusters/valve-configuration-and-control-server/CodegenIntegration.h>
#include <clusters/ValveConfigurationAndControl/Commands.h>
#include <cstdio>
#include <cstring>
#include <data_model_provider/clusters/temperature_measurement/integration.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_timer.h>
#include <nvs.h>

namespace {
using namespace esp_matter;
using namespace chip::app::Clusters;

constexpr size_t capacity = bridge::Enrollment::capacity;
constexpr uint64_t poll_ms = 300000;
constexpr uint64_t stale_ms = poll_ms * 2 + 30000;
struct Feed {
    std::optional<bridge::EnrollmentId> binding;
    bridge::PositionObservation sample;
    bridge::TrvSensorObservation sensors;
    uint64_t sensor_attempt_ms = 0;
    uint32_t completions = 0;
    int last_result = -1;
};
std::array<Feed, capacity> feeds;
portMUX_TYPE feed_lock = portMUX_INITIALIZER_UNLOCKED;
struct Published {
    uint16_t ep = chip::kInvalidEndpointId;
    bridge::EnrollmentId identity{};
    bool reachable = false;
    std::optional<uint8_t> last_level;
    std::optional<int16_t> temperature;
    std::optional<uint8_t> battery;
    uint64_t next_poll = 0;
    uint64_t last_publish = 0;
    uint32_t blocked_commands = 0;
    uint32_t reachability_losses = 0;
    bool registered = false;
};
std::array<Published, capacity> devices;
Feed get_feed(size_t slot);
using namespace chip::app;
namespace Valve = ValveConfigurationAndControl;
using chip::Protocols::InteractionModel::Status;

// Each registered server belongs to one enrolled valve. The SDK implements the
// standard attribute metadata and confirmed state. Pending commands live in RAM.
class PhysicalValve final : public ValveConfigurationAndControlCluster {
  public:
    PhysicalValve(uint16_t ep, size_t slot)
        : ValveConfigurationAndControlCluster(ep, {chip::BitFlags<Valve::Feature>(Valve::Feature::kLevel),
                                                   OptionalAttributeSet(0),
                                                   {{}, 100, 1},
                                                   nullptr,
                                                   nullptr}),
          slot_(slot) {}

    std::optional<DataModel::ActionReturnStatus> InvokeCommand(const DataModel::InvokeRequest &request,
                                                               chip::TLV::TLVReader &input,
                                                               CommandHandler *) override {
        uint8_t target = 0;
        if (request.path.mCommandId == Valve::Commands::Open::Id) {
            Valve::Commands::Open::DecodableType args;
            ReturnErrorOnFailure(args.Decode(input));
            if (args.openDuration.HasValue() && !args.openDuration.Value().IsNull())
                return Status::ConstraintError;
            target = args.targetLevel.ValueOr(100);
            if (target == 0 || target > 100)
                return Status::ConstraintError;
        } else if (request.path.mCommandId == Valve::Commands::Close::Id) {
            Valve::Commands::Close::DecodableType args;
            ReturnErrorOnFailure(args.Decode(input));
        } else {
            return Status::UnsupportedCommand;
        }
        auto &d = devices[slot_];
        auto feed = get_feed(slot_);
        const auto level = feed.sample.current(d.identity, esp_timer_get_time() / 1000, stale_ms);
        if (!level || feed.binding != d.identity || !d.reachable) {
            ++d.blocked_commands;
            return Status::InvalidInState;
        }
        if (pending_ || !queue_matter_position(slot_, d.identity, *level, target)) {
            ++d.blocked_commands;
            return Status::Busy;
        }
        completion_at_start_ = feed.completions;
        set_pending(target);
        return Status::Success;
    }

    DataModel::ActionReturnStatus ReadAttribute(const DataModel::ReadAttributeRequest &request,
                                                AttributeValueEncoder &encoder) override {
        switch (request.path.mAttributeId) {
        case Valve::Attributes::TargetLevel::Id:
            return pending_ ? encoder.Encode(*pending_) : encoder.EncodeNull();
        case Valve::Attributes::TargetState::Id:
            return pending_ ? encoder.Encode(*pending_ ? Valve::ValveStateEnum::kOpen
                                                       : Valve::ValveStateEnum::kClosed)
                            : encoder.EncodeNull();
        case Valve::Attributes::CurrentState::Id:
            if (pending_)
                return encoder.Encode(Valve::ValveStateEnum::kTransitioning);
            [[fallthrough]];
        case Valve::Attributes::CurrentLevel::Id:
            if (!devices[slot_].last_level)
                return encoder.EncodeNull();
            break;
        case Valve::Attributes::DefaultOpenDuration::Id:
            return encoder.EncodeNull();
        }
        return ValveConfigurationAndControlCluster::ReadAttribute(request, encoder);
    }

    DataModel::ActionReturnStatus WriteAttribute(const DataModel::WriteAttributeRequest &request,
                                                 AttributeValueDecoder &decoder) override {
        if (request.path.mAttributeId == Valve::Attributes::DefaultOpenDuration::Id) {
            DataModel::Nullable<uint32_t> duration;
            ReturnErrorOnFailure(decoder.Decode(duration));
            return duration.IsNull() ? Status::Success : Status::ConstraintError;
        }
        return ValveConfigurationAndControlCluster::WriteAttribute(request, decoder);
    }

    void publish_level(std::optional<uint8_t> level) {
        auto &last = devices[slot_].last_level;
        if (last == level)
            return;
        const bool validity_changed = last.has_value() != level.has_value();
        last = level;
        if (level) {
            UpdateCurrentLevel(*level);
            UpdateCurrentState(*level ? Valve::ValveStateEnum::kOpen : Valve::ValveStateEnum::kClosed);
        }
        if (validity_changed) {
            NotifyAttributeChanged(Valve::Attributes::CurrentLevel::Id);
            NotifyAttributeChanged(Valve::Attributes::CurrentState::Id);
        }
    }
    void reconcile(const Feed &feed) {
        if (pending_ && (feed.binding != devices[slot_].identity || feed.completions != completion_at_start_))
            set_pending(std::nullopt);
    }

  private:
    size_t slot_;
    std::optional<uint8_t> pending_;
    uint32_t completion_at_start_ = 0;
    void set_pending(std::optional<uint8_t> value) {
        pending_ = value;
        NotifyAttributeChanged(Valve::Attributes::TargetLevel::Id);
        NotifyAttributeChanged(Valve::Attributes::TargetState::Id);
        NotifyAttributeChanged(Valve::Attributes::CurrentState::Id);
    }
};

std::array<LazyRegisteredServerCluster<PhysicalValve>, capacity> servers;
void start_valve(uint16_t ep) {
    for (size_t i = 0; i < capacity; ++i) {
        if (devices[i].ep != ep)
            continue;
        if (devices[i].registered)
            return;
        auto &s = servers[i];
        if (!s.IsConstructed())
            s.Create(ep, i);
        devices[i].registered =
            data_model::provider::get_instance().registry().Register(s.Registration()) == CHIP_NO_ERROR;
        if (!devices[i].registered)
            s.Destroy();
        return;
    }
}
void stop_valve(uint16_t ep, ClusterShutdownType reason) {
    for (size_t i = 0; i < capacity; ++i) {
        if (devices[i].ep != ep || !servers[i].IsConstructed())
            continue;
        if (devices[i].registered) {
            auto err =
                data_model::provider::get_instance().registry().Unregister(&servers[i].Cluster(), reason);
            if (err != CHIP_NO_ERROR) {
                ESP_LOGE("position", "Could not unregister valve endpoint %u", unsigned(ep));
                return;
            }
            devices[i].registered = false;
        }
        if (reason == ClusterShutdownType::kPermanentRemove)
            servers[i].Destroy();
        return;
    }
}
constexpr size_t record_size = 2 + bridge::EnrollmentId{}.size();
using EndpointRecord = std::array<uint8_t, record_size>;
struct NvsCloser {
    nvs_handle_t h;
    ~NvsCloser() { nvs_close(h); }
};

bool add_physical_sensors(endpoint_t *ep) {
    cluster::temperature_measurement::config_t temperature{};
    cluster::power_source::config_t battery{};
    battery.status = 1; // The enrolled TRV is battery powered.
    battery.feature_flags = cluster::power_source::feature::battery::get_id();
    auto *power = cluster::power_source::create(ep, &battery, CLUSTER_FLAG_SERVER);
    return power &&
           cluster::power_source::attribute::create_bat_percent_remaining(power, nullable<uint8_t>(), 0,
                                                                          200) &&
           cluster::temperature_measurement::create(ep, &temperature, CLUSTER_FLAG_SERVER);
}

esp_err_t create_endpoint(size_t slot, const bridge::EnrollmentId &id) {
    // Store the SDK-assigned endpoint id together with the physical enrollment
    // identity. A mismatched or damaged record must never alias another valve.
    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition("bridge_store", "matter_trv_v1", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    NvsCloser close{nvs};
    const char key[] = {'s', char('0' + slot), 0};
    EndpointRecord record{};
    size_t size = record.size();
    err = nvs_get_blob(nvs, key, record.data(), &size);
    const bool first = err == ESP_ERR_NVS_NOT_FOUND;
    if (!first && err != ESP_OK)
        return err;
    const uint16_t saved = uint16_t(record[0] | (uint16_t(record[1]) << 8));
    if (!first && (size != record.size() || std::memcmp(record.data() + 2, id.data(), id.size()) ||
                   saved < 10 || saved == chip::kInvalidEndpointId))
        return ESP_ERR_INVALID_STATE;
    auto *node = node::get();
    endpoint::bridged_node::config_t bc;
    bc.bridged_device_basic_information.reachable = false;
    auto *ep = first ? endpoint::bridged_node::create(node, &bc, ENDPOINT_FLAG_NONE, nullptr)
                     : endpoint::bridged_node::resume(node, &bc, ENDPOINT_FLAG_NONE, saved, nullptr);
    if (!ep)
        return ESP_FAIL;
    const auto number = endpoint::get_id(ep);
    endpoint::water_valve::config_t vc;
    cluster::valve_configuration_and_control::feature::level::config_t level;
    char label[24];
    std::snprintf(label, sizeof(label), "Shelly BLU TRV %u", unsigned(slot + 1));
    char uid[33];
    for (size_t i = 0; i < id.size(); ++i)
        std::snprintf(uid + 2 * i, 3, "%02x", unsigned(id[i]));
    auto *basic = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    bool ready = number >= 10 && basic &&
                 cluster::bridged_device_basic_information::attribute::create_unique_id(basic, uid, 32) &&
                 cluster::bridged_device_basic_information::attribute::create_node_label(
                     basic, label, std::strlen(label)) &&
                 endpoint::water_valve::add(ep, &vc) == ESP_OK && add_physical_sensors(ep) &&
                 cluster::valve_configuration_and_control::feature::level::add(
                     cluster::get(ep, ValveConfigurationAndControl::Id), &level) == ESP_OK &&
                 endpoint::set_parent_endpoint(ep, endpoint::get(1)) == ESP_OK;
    if (!ready || cluster::set_init_and_shutdown_callbacks(cluster::get(ep, Valve::Id), start_valve,
                                                           stop_valve) != ESP_OK) {
        endpoint::destroy(node, ep);
        return ESP_FAIL;
    }
    if (first) {
        record[0] = uint8_t(number);
        record[1] = uint8_t(number >> 8);
        std::memcpy(record.data() + 2, id.data(), id.size());
        err = nvs_set_blob(nvs, key, record.data(), record.size());
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        if (err != ESP_OK) {
            endpoint::destroy(node, ep);
            return err;
        }
    }
    // On the Matter task: command dispatch cannot interleave with creation.
    devices[slot].identity = id;
    devices[slot].ep = number;
    err = endpoint::enable(ep);
    if (err != ESP_OK || !devices[slot].registered) {
        endpoint::destroy(node, ep);
        devices[slot] = {};
        return ESP_FAIL;
    }
    return ESP_OK;
}
Feed get_feed(size_t slot) {
    portENTER_CRITICAL(&feed_lock);
    auto copy = feeds[slot];
    portEXIT_CRITICAL(&feed_lock);
    return copy;
}
} // namespace
void initialize_position_bindings() {
    bridge::Enrollment table;
    if (auto err = load_trv_bindings(table); err != ESP_OK) {
        ESP_LOGE("position", "Enrollment storage unavailable: %s", esp_err_to_name(err));
        return;
    }
    for (size_t i = 0; i < capacity; ++i)
        if (table.slots[i].state == bridge::EnrollmentState::verified)
            feeds[i].binding = table.slots[i].identity;
}
void observe_trv_position(size_t slot, const bridge::EnrollmentId &id, uint8_t percent, uint64_t now_ms) {
    if (slot >= capacity || percent > 100)
        return;
    portENTER_CRITICAL(&feed_lock);
    feeds[slot].binding = id;
    feeds[slot].sample = {id, now_ms, percent};
    portEXIT_CRITICAL(&feed_lock);
}
void record_trv_sensor_read(size_t slot, const bridge::EnrollmentId &id,
                            std::optional<bridge::TrvSensorSample> sample) {
    if (slot >= capacity)
        return;
    const uint64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&feed_lock);
    auto &feed = feeds[slot];
    if (feed.binding == id) {
        feed.sensor_attempt_ms = now;
        if (sample)
            feed.sensors.observe(*sample, now);
    }
    portEXIT_CRITICAL(&feed_lock);
}
void expire_trv_position_sample(size_t slot, const bridge::EnrollmentId &id) {
    portENTER_CRITICAL(&feed_lock);
    if (feeds[slot].binding == id)
        feeds[slot].sample = {};
    portEXIT_CRITICAL(&feed_lock);
}
void finish_matter_position(size_t slot, const bridge::EnrollmentId &id, int result) {
    portENTER_CRITICAL(&feed_lock);
    if (feeds[slot].binding == id) {
        ++feeds[slot].completions;
        feeds[slot].last_result = result;
    }
    portEXIT_CRITICAL(&feed_lock);
}
void invalidate_trv_position(size_t slot) {
    portENTER_CRITICAL(&feed_lock);
    feeds[slot] = {};
    portEXIT_CRITICAL(&feed_lock);
}
esp_err_t remove_trv_endpoint(size_t slot, const bridge::EnrollmentId &id) {
    if (slot >= capacity)
        return ESP_ERR_INVALID_ARG;
    // Called by the USB task with downstream operations excluded. Serialize
    // the endpoint record, the live server and PartsList updates with Matter.
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto &d = devices[slot];
    if (d.ep != chip::kInvalidEndpointId && d.identity != id)
        return ESP_ERR_INVALID_STATE;
    nvs_handle_t nvs;
    auto err = nvs_open_from_partition("bridge_store", "matter_trv_v1", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    NvsCloser close{nvs};
    const char key[] = {'s', char('0' + slot), 0};
    EndpointRecord record{};
    size_t size = record.size();
    err = nvs_get_blob(nvs, key, record.data(), &size);
    const bool has_record = err == ESP_OK;
    if (has_record) {
        const uint16_t saved = uint16_t(record[0] | (uint16_t(record[1]) << 8));
        if (size != record.size() || std::memcmp(record.data() + 2, id.data(), id.size()) || saved < 10 ||
            saved == chip::kInvalidEndpointId || (d.ep != saved && endpoint::get(saved)))
            return ESP_ERR_INVALID_STATE;
        if (d.ep != chip::kInvalidEndpointId && d.ep != saved)
            return ESP_ERR_INVALID_STATE;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    invalidate_trv_position(slot);
    if (d.ep != chip::kInvalidEndpointId) {
        if (auto *ep = endpoint::get(d.ep)) {
            if (auto rc = endpoint::destroy(node::get(), ep); rc != ESP_OK)
                return rc;
        }
        if (servers[slot].IsConstructed()) {
            stop_valve(d.ep, ClusterShutdownType::kPermanentRemove);
            if (servers[slot].IsConstructed())
                return ESP_ERR_INVALID_STATE;
        }
    }
    d = {};
    if (!has_record)
        return ESP_OK; // pending enrollment, or cleanup resumed after record deletion
    err = nvs_erase_key(nvs, key);
    return err == ESP_OK ? nvs_commit(nvs) : err;
}
namespace {
void publish_sensors(Published &d, const bridge::TrvSensorSample *sample) {
    const auto temperature = sample ? std::optional<int16_t>(sample->measured_centi_c) : std::nullopt;
    if (temperature != d.temperature) {
        DataModel::Nullable<int16_t> value;
        if (temperature)
            value.SetNonNull(*temperature);
        if (TemperatureMeasurement::SetMeasuredValue(d.ep, value) == CHIP_NO_ERROR)
            d.temperature = temperature;
    }
    const auto battery = sample ? std::optional<uint8_t>(sample->battery_percent) : std::nullopt;
    if (battery != d.battery) {
        auto value =
            esp_matter_nullable_uint8(battery ? nullable<uint8_t>(*battery * 2) : nullable<uint8_t>());
        if (attribute::update(d.ep, PowerSource::Id, PowerSource::Attributes::BatPercentRemaining::Id,
                              &value) == ESP_OK)
            d.battery = battery;
    }
}
} // namespace
void publish_trv_positions() {
    for (size_t i = 0; i < capacity; ++i) {
        auto &d = devices[i];
        const auto feed = get_feed(i);
        // BLE can publish a newer observation while the Matter task is running.
        const uint64_t now = esp_timer_get_time() / 1000;
        if (d.ep == chip::kInvalidEndpointId) {
            if (!feed.binding || now < d.next_poll)
                continue;
            if (auto err = create_endpoint(i, *feed.binding); err != ESP_OK) {
                ESP_LOGE("position", "slot=%u endpoint creation failed: %s", unsigned(i),
                         esp_err_to_name(err));
                d.next_poll = now + 60000;
                continue;
            }
        }
        if (!d.registered)
            continue;
        auto &server = servers[i].Cluster();
        server.reconcile(feed);
        const bool bound = feed.binding && *feed.binding == d.identity;
        const auto level = bound ? feed.sample.current(d.identity, now, stale_ms) : std::nullopt;
        server.publish_level(level);
        publish_sensors(d, level && feed.sensors.fresh(now, stale_ms) ? &*feed.sensors.sample : nullptr);
        if (level)
            d.last_publish = feed.sample.observed_ms;
        if (level.has_value() != d.reachable) {
            auto v = esp_matter_bool(level.has_value());
            if (attribute::update(d.ep, BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::Reachable::Id, &v) == ESP_OK) {
                d.reachable = level.has_value();
                if (!d.reachable)
                    ++d.reachability_losses;
            }
        }
        if (bound && now >= d.next_poll &&
            (!level || now - feed.sample.observed_ms >= poll_ms || !feed.sensor_attempt_ms ||
             now - feed.sensor_attempt_ms >= poll_ms)) {
            d.next_poll = now + (queue_position_read(i) ? 60000 : 5000);
        }
    }
}
void show_trv_positions(uint64_t now) {
    for (size_t i = 0; i < capacity; ++i) {
        const auto &d = devices[i];
        const auto feed = get_feed(i);
        if (d.ep != chip::kInvalidEndpointId) {
            std::printf("Physical TRV slot=%u endpoint=%u reachable=%u confirmed_position=%d age_ms=%lld "
                        "blocked_commands=%lu matter_completed=%lu last_result=%d reachability_losses=%lu\n",
                        unsigned(i), unsigned(d.ep), unsigned(d.reachable),
                        d.reachable && d.last_level ? int(*d.last_level) : -1,
                        d.last_publish ? static_cast<long long>(now - d.last_publish) : -1LL,
                        static_cast<unsigned long>(d.blocked_commands),
                        static_cast<unsigned long>(feed.completions), feed.last_result,
                        static_cast<unsigned long>(d.reachability_losses));
            std::printf("Physical sensors slot=%u endpoint=%u", unsigned(i), unsigned(d.ep));
            if (d.temperature)
                std::printf(" measured_centi_c=%d", int(*d.temperature));
            if (d.battery)
                std::printf(" battery_percent=%u", unsigned(*d.battery));
            std::printf("\n");
        }
    }
}
