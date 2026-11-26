// SPDX-License-Identifier: Apache-2.0
#include "matter_trv_position.h"
#include "trv_binding_store.h"
#include "trv_gatt_probe.h"
#include <app/clusters/valve-configuration-and-control-server/CodegenIntegration.h>
#include <app/util/MatterCallbacks.h>
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_matter.h>
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
};
std::array<Feed, capacity> feeds;
portMUX_TYPE feed_lock = portMUX_INITIALIZER_UNLOCKED;
struct Published {
    uint16_t ep = chip::kInvalidEndpointId;
    bridge::EnrollmentId identity{};
    bool reachable = false;
    std::optional<uint8_t> last_level;
    uint64_t next_poll = 0;
    uint64_t last_publish = 0;
    uint32_t blocked_commands = 0;
};
std::array<Published, capacity> devices;
class ReadOnlyPhysicalValves final : public chip::DataModelCallbacks {
  public:
    chip::DataModelCallbacks *previous = nullptr;
    CHIP_ERROR PreCommandReceived(const chip::app::ConcreteCommandPath &path,
                                  const chip::Access::SubjectDescriptor &subject) override {
        for (auto &device : devices) {
            if (device.ep != chip::kInvalidEndpointId && device.ep == path.mEndpointId &&
                path.mClusterId == ValveConfigurationAndControl::Id) {
                ++device.blocked_commands;
                return CHIP_ERROR_INCORRECT_STATE;
            }
        }
        return previous->PreCommandReceived(path, subject);
    }
    void PostCommandReceived(const chip::app::ConcreteCommandPath &path,
                             const chip::Access::SubjectDescriptor &subject) override {
        previous->PostCommandReceived(path, subject);
    }
    void AttributeOperation(OperationType op, OperationOrder order,
                            const chip::app::ConcreteAttributePath &path) override {
        previous->AttributeOperation(op, order, path);
    }
};
ReadOnlyPhysicalValves gate;
constexpr size_t record_size = 2 + bridge::EnrollmentId{}.size();
using EndpointRecord = std::array<uint8_t, record_size>;

esp_err_t create_endpoint(size_t slot, const bridge::EnrollmentId &id) {
    // Store the SDK-assigned endpoint id together with the physical enrollment
    // identity. A mismatched or damaged record must never alias another valve.
    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition("bridge_store", "matter_trv_v1", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    struct Close {
        nvs_handle_t h;
        ~Close() { nvs_close(h); }
    } close{nvs};
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
                 endpoint::water_valve::add(ep, &vc) == ESP_OK &&
                 cluster::valve_configuration_and_control::feature::level::add(
                     cluster::get(ep, ValveConfigurationAndControl::Id), &level) == ESP_OK &&
                 endpoint::set_parent_endpoint(ep, endpoint::get(1)) == ESP_OK;
    if (!ready) {
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
    err = endpoint::enable(ep);
    if (err != ESP_OK || !ValveConfigurationAndControl::FindClusterOnEndpoint(number)) {
        endpoint::destroy(node, ep);
        return ESP_FAIL;
    }
    devices[slot].identity = id;
    devices[slot].ep = number;
    return ESP_OK;
}
Feed get_feed(size_t slot) {
    portENTER_CRITICAL(&feed_lock);
    auto copy = feeds[slot];
    portEXIT_CRITICAL(&feed_lock);
    return copy;
}
} // namespace
void install_position_command_gate() { gate.previous = chip::DataModelCallbacks::SetInstance(&gate); }
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
    feeds[slot] = {id, {id, now_ms, percent}};
    portEXIT_CRITICAL(&feed_lock);
}
void invalidate_trv_position(size_t slot) {
    portENTER_CRITICAL(&feed_lock);
    feeds[slot] = {};
    portEXIT_CRITICAL(&feed_lock);
}
void publish_trv_positions(uint64_t now) {
    for (size_t i = 0; i < capacity; ++i) {
        auto &d = devices[i];
        const auto feed = get_feed(i);
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
        const bool bound = feed.binding && *feed.binding == d.identity;
        const auto level = bound ? feed.sample.current(d.identity, now, stale_ms) : std::nullopt;
        if (level && (!d.reachable || level != d.last_level)) {
            using namespace ValveConfigurationAndControl;
            if (UpdateCurrentLevel(d.ep, *level) != CHIP_NO_ERROR ||
                UpdateCurrentState(d.ep, *level ? ValveStateEnum::kOpen : ValveStateEnum::kClosed) !=
                    CHIP_NO_ERROR)
                continue;
            d.last_level = level;
        }
        if (level)
            d.last_publish = feed.sample.observed_ms;
        if (level.has_value() != d.reachable) {
            auto v = esp_matter_bool(level.has_value());
            if (attribute::update(d.ep, BridgedDeviceBasicInformation::Id,
                                  BridgedDeviceBasicInformation::Attributes::Reachable::Id, &v) == ESP_OK)
                d.reachable = level.has_value();
        }
        if (bound && now >= d.next_poll && (!level || now - feed.sample.observed_ms >= poll_ms)) {
            d.next_poll = now + (queue_position_read(i) ? 60000 : 5000);
        }
    }
}
void show_trv_positions(uint64_t now) {
    for (size_t i = 0; i < capacity; ++i) {
        const auto &d = devices[i];
        if (d.ep != chip::kInvalidEndpointId)
            std::printf("Physical TRV slot=%u endpoint=%u reachable=%u confirmed_position=%d age_ms=%lld "
                        "blocked_commands=%lu\n",
                        unsigned(i), unsigned(d.ep), unsigned(d.reachable),
                        d.reachable && d.last_level ? int(*d.last_level) : -1,
                        d.last_publish ? static_cast<long long>(now - d.last_publish) : -1LL,
                        static_cast<unsigned long>(d.blocked_commands));
    }
}
