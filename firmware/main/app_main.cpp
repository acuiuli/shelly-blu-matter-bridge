// SPDX-License-Identifier: Apache-2.0
#include "ble_diagnostics.h"
#include "bridge/bridge.hpp"
#include "matter_trv_position.h"
#include <app/clusters/valve-configuration-and-control-server/CodegenIntegration.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_console.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <nvs_flash.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConnectivityManager.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if CONFIG_ENABLE_WIFI_STATION || CONFIG_ENABLE_WIFI_AP || CONFIG_OPENTHREAD_BORDER_ROUTER
#error "Gateway requires Thread without Wi-Fi or border routing"
#endif
#if !CONFIG_OPENTHREAD_FTD || !CONFIG_ENABLE_MATTER_OVER_THREAD
#error "Gateway requires Matter over Thread FTD"
#endif

using namespace esp_matter;
using namespace chip::app::Clusters;
static bridge::Engine engine;
static bridge::Simulator simulator;
static bridge::Identity identity;
static bool publishing = false;
static std::atomic<unsigned> publish_rejections{0};
static bridge::Time now_ms() { return esp_timer_get_time() / 1000; }

static void load_identity() {
    ESP_ERROR_CHECK(nvs_flash_init_partition("bridge_store"));
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open_from_partition("bridge_store", "identity", NVS_READWRITE, &h));
    size_t size = sizeof(identity);
    esp_err_t err = nvs_get_blob(h, "v1", &identity, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(identity.gateway.data(), identity.gateway.size());
        ESP_ERROR_CHECK(nvs_set_blob(h, "v1", &identity, sizeof(identity)));
        ESP_ERROR_CHECK(nvs_commit(h));
    } else {
        ESP_ERROR_CHECK(err);
        ESP_ERROR_CHECK(size == sizeof(identity) && identity.valid() ? ESP_OK : ESP_FAIL);
    }
    nvs_close(h);
}

static esp_err_t changed(attribute::callback_type_t type, uint16_t ep, uint32_t cluster_id, uint32_t attr,
                         esp_matter_attr_val_t *value, void *) {
    if (publishing || type != attribute::PRE_UPDATE)
        return ESP_OK;
    for (size_t d = 0; d < bridge::device_count; ++d) {
        if (ep != identity.thermostat_endpoint(d) || cluster_id != Thermostat::Id)
            continue;
        if (attr == Thermostat::Attributes::OccupiedHeatingSetpoint::Id)
            return engine.request(d, bridge::Field::setpoint, value->val.i16, now_ms()) ? ESP_OK
                                                                                        : ESP_ERR_INVALID_ARG;
        if (attr == Thermostat::Attributes::SystemMode::Id)
            return engine.request(d, bridge::Field::mode, value->val.u8, now_ms()) ? ESP_OK
                                                                                   : ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
static esp_err_t identify_cb(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void *) {
    return ESP_OK; // no LED driver in this prototype
}

// The native Matter server owns valve attributes and command validation. The
// delegate queues intent; only subsequent telemetry may confirm physical motion.
class ValveDelegate final : public ValveConfigurationAndControl::Delegate {
  public:
    size_t slot = 0;
    chip::app::DataModel::Nullable<chip::Percent>
    HandleOpenValve(chip::app::DataModel::Nullable<chip::Percent> level) override {
        if (!publishing &&
            (level.IsNull() || !engine.request(slot, bridge::Field::position, level.Value(), now_ms())))
            ESP_LOGE("bridge", "Rejected valve intent for slot %u", unsigned(slot));
        return {}; // Never turn command acceptance into a reported position.
    }
    CHIP_ERROR HandleCloseValve() override {
        return publishing || engine.request(slot, bridge::Field::position, 0, now_ms())
                   ? CHIP_NO_ERROR
                   : CHIP_ERROR_INVALID_ARGUMENT;
    }
    void HandleRemainingDurationTick(uint32_t) override {}
};
static std::array<ValveDelegate, bridge::device_count> valve_delegates;

static void update(uint16_t ep, uint32_t c, uint32_t a, esp_matter_attr_val_t v) {
    esp_err_t err = attribute::update(ep, c, a, &v);
    if (err != ESP_OK)
        ESP_LOGE("bridge", "Attribute update failed: endpoint=%u cluster=0x%lx attribute=0x%lx", unsigned(ep),
                 static_cast<unsigned long>(c), static_cast<unsigned long>(a));
    ESP_ERROR_CHECK(err);
}
static void publish_valve(size_t d, const bridge::State &s) {
    using namespace ValveConfigurationAndControl;
    static std::array<std::optional<bridge::Time>, bridge::device_count> last_observation;
    static std::array<uint64_t, bridge::device_count> reconciled_token{};
    if (!s.reachable || !s.reported || last_observation[d] == s.last_seen)
        return;
    last_observation[d] = s.last_seen;
    auto ep = identity.valve_endpoint(d);
    const auto &cmd = s.commands[1];
    bool active = cmd.outcome == bridge::Outcome::queued || cmd.outcome == bridge::Outcome::sent;
    auto level = static_cast<chip::Percent>(s.reported->position);
    // Clear an obsolete SDK target only against a NEW transport observation.
    // Suppress delegate enqueue while reconciling, and preserve timed-open state
    // on ordinary successful reports. Unreachable devices retain last-known data.
    if (!active && cmd.outcome != bridge::Outcome::confirmed && cmd.token != reconciled_token[d]) {
        auto err =
            level == 0 ? CloseValve(ep) : SetValveLevel(ep, chip::app::DataModel::MakeNullable(level), {});
        ESP_ERROR_CHECK(err == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);
        reconciled_token[d] = cmd.token;
    }
    ESP_ERROR_CHECK(UpdateCurrentLevel(ep, level) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);
    if (!active)
        ESP_ERROR_CHECK(UpdateCurrentState(ep, level ? ValveStateEnum::kOpen : ValveStateEnum::kClosed) ==
                                CHIP_NO_ERROR
                            ? ESP_OK
                            : ESP_FAIL);
}
static void confirm_boot_health() {
    static bool checked = false;
    if (checked || !ble_hs_synced())
        return;
    // A complete model publication must succeed before accepting an OTA image.
    // This still does not qualify network connectivity or physical valve health.
    esp_ota_img_states_t state;
    auto *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    checked = true;
}
static void publish(intptr_t) {
    simulator.step(engine, now_ms());
    publishing = true;
    for (size_t d = 0; d < bridge::device_count; ++d) {
        auto ep = identity.thermostat_endpoint(d);
        const auto &s = engine.state(d);
        update(ep, BridgedDeviceBasicInformation::Id,
               BridgedDeviceBasicInformation::Attributes::Reachable::Id, esp_matter_bool(s.reachable));
        if (!s.reported)
            continue;
        const auto &t = *s.reported;
        update(
            ep, Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id,
            esp_matter_nullable_int16(s.reachable ? nullable<int16_t>(t.temperature) : nullable<int16_t>()));
        const auto &sp = s.commands[0];
        if (sp.outcome != bridge::Outcome::sent && sp.outcome != bridge::Outcome::queued)
            update(ep, Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id,
                   esp_matter_int16(t.setpoint));
        const auto &mode = s.commands[2];
        if (mode.outcome != bridge::Outcome::sent && mode.outcome != bridge::Outcome::queued)
            update(ep, Thermostat::Id, Thermostat::Attributes::SystemMode::Id, esp_matter_enum8(t.mode));
        update(
            ep, PowerSource::Id, PowerSource::Attributes::BatPercentRemaining::Id,
            esp_matter_nullable_uint8(s.reachable ? nullable<uint8_t>(t.battery * 2) : nullable<uint8_t>()));
        publish_valve(d, s);
    }
    publish_trv_positions(now_ms());
    publishing = false;
    confirm_boot_health();
}
static void create_devices(node_t *node) {
    endpoint::aggregator::config_t aggregator_config;
    auto *aggregator = endpoint::aggregator::create(node, &aggregator_config, ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(aggregator && endpoint::get_id(aggregator) == 1 ? ESP_OK : ESP_FAIL);
    for (size_t d = 0; d < bridge::device_count; ++d) {
        endpoint::bridged_node::config_t bc;
        bc.bridged_device_basic_information.reachable = false;
        auto *ep = endpoint::bridged_node::create(node, &bc, ENDPOINT_FLAG_NONE, nullptr);
        ESP_ERROR_CHECK(ep && endpoint::get_id(ep) == identity.thermostat_endpoint(d) ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(endpoint::set_parent_endpoint(ep, aggregator));
        auto *basic = cluster::get(ep, BridgedDeviceBasicInformation::Id);
        auto uid = identity.unique_id(d);
        ESP_ERROR_CHECK(cluster::bridged_device_basic_information::attribute::create_unique_id(
                            basic, uid.data(), std::strlen(uid.data()))
                            ? ESP_OK
                            : ESP_FAIL);
        char label[24];
        std::snprintf(label, sizeof(label), "Simulated TRV %u", unsigned(d));
        cluster::bridged_device_basic_information::attribute::create_node_label(basic, label,
                                                                                std::strlen(label));
        endpoint::thermostat::config_t tc;
        tc.thermostat.control_sequence_of_operation = 2; // heating only
        tc.thermostat.system_mode = 4;
        tc.thermostat.feature_flags = cluster::thermostat::feature::heating::get_id();
        ESP_ERROR_CHECK(endpoint::thermostat::add(ep, &tc));
        cluster::power_source::config_t pc;
        pc.status = 1;
        pc.feature_flags = cluster::power_source::feature::battery::get_id();
        auto *power = cluster::power_source::create(ep, &pc, CLUSTER_FLAG_SERVER);
        ESP_ERROR_CHECK(power ? ESP_OK : ESP_FAIL);
        cluster::power_source::attribute::create_bat_percent_remaining(power, nullable<uint8_t>(), 0, 200);
        endpoint::water_valve::config_t vc;
        valve_delegates[d].slot = d;
        vc.valve_configuration_and_control.delegate = &valve_delegates[d];
        auto *vp = endpoint::water_valve::create(node, &vc, ENDPOINT_FLAG_NONE, nullptr);
        ESP_ERROR_CHECK(vp && endpoint::get_id(vp) == identity.valve_endpoint(d) ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(endpoint::set_parent_endpoint(vp, ep));
        auto *valve = cluster::get(vp, ValveConfigurationAndControl::Id);
        cluster::valve_configuration_and_control::feature::level::config_t lc;
        ESP_ERROR_CHECK(cluster::valve_configuration_and_control::feature::level::add(valve, &lc));
    }
}

// USB commands are executed on the Matter task so state has a single owner.
struct Injection {
    size_t slot;
    int operation;
    int value;
};
static void inject(intptr_t arg) {
    auto *p = reinterpret_cast<Injection *>(arg);
    auto &f = simulator.faults[p->slot];
    switch (p->operation) {
    case 0:
        f.available = p->value;
        break;
    case 1:
        f.delay = p->value;
        break;
    case 2:
        f.failures = p->value;
        break;
    case 3:
        f.hold_position = p->value;
        break;
    case 4:
        simulator.manual(engine, p->slot, bridge::Field::position, p->value, now_ms());
        break;
    }
    delete p;
}
static int sim_command(int argc, char **argv) {
    if (argc != 4)
        return 1;
    char *end;
    long d = std::strtol(argv[1], &end, 10);
    if (*end || d < 0 || d >= long(bridge::device_count))
        return 1;
    long value = std::strtol(argv[3], &end, 10);
    if (*end || value < 0 || value > 60000)
        return 1;
    const char *names[] = {"available", "delay", "failures", "hold", "manual"};
    int op = -1;
    for (int i = 0; i < 5; ++i)
        if (std::strcmp(names[i], argv[2]) == 0)
            op = i;
    if (op < 0 || ((op == 0 || op == 3) && value > 1) || (op == 4 && value > 100))
        return 1;
    auto *p = new Injection{size_t(d), op, int(value)};
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(inject, reinterpret_cast<intptr_t>(p)) !=
        CHIP_NO_ERROR) {
        delete p;
        return 1;
    }
    return 0;
}
static void invoke_simulated_valve(intptr_t arg) {
    using namespace ValveConfigurationAndControl;
    auto *p = reinterpret_cast<Injection *>(arg);
    uint8_t buffer[64];
    chip::TLV::TLVWriter writer;
    writer.Init(buffer);
    CHIP_ERROR err;
    if (p->value) {
        Commands::Open::Type args;
        args.targetLevel.SetValue(static_cast<uint8_t>(p->value));
        err = args.Encode(writer, chip::TLV::AnonymousTag());
    } else {
        Commands::Close::Type args;
        err = args.Encode(writer, chip::TLV::AnonymousTag());
    }
    if (err == CHIP_NO_ERROR)
        err = writer.Finalize();
    chip::TLV::TLVReader reader;
    reader.Init(buffer, writer.GetLengthWritten());
    if (err == CHIP_NO_ERROR)
        err = reader.Next();
    auto ep = identity.valve_endpoint(p->slot);
    auto *cluster = FindClusterOnEndpoint(ep);
    if (err == CHIP_NO_ERROR && cluster) {
        chip::Access::SubjectDescriptor subject;
        chip::app::DataModel::InvokeRequest request(
            {ep, Id, p->value ? Commands::Open::Id : Commands::Close::Id}, subject);
        // Local simulator diagnostics exercise native decoding and validation.
        // Network access control and commissioning still require controller tests.
        auto status = cluster->InvokeCommand(request, reader, nullptr);
        chip::app::DataModel::ActionReturnStatus::StringStorage storage;
        std::printf("Valve invoke slot=%u level=%d success=%u status=%s\n", unsigned(p->slot), p->value,
                    unsigned(status && status->IsSuccess()),
                    status ? status->c_str(storage) : "missing-response");
    } else {
        std::printf("Valve diagnostic encoding failed\n");
    }
    delete p;
}
static int sim_valve_command(int argc, char **argv) {
    if (argc != 3)
        return 1;
    char *end;
    long d = std::strtol(argv[1], &end, 10);
    if (!argv[1][0] || *end || d < 0 || d >= long(bridge::device_count))
        return 1;
    long level = std::strtol(argv[2], &end, 10);
    if (!argv[2][0] || *end || level < 0 || level > 255)
        return 1;
    auto *p = new Injection{size_t(d), 0, int(level)};
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(invoke_simulated_valve,
                                                      reinterpret_cast<intptr_t>(p)) != CHIP_NO_ERROR) {
        delete p;
        return 1;
    }
    return 0;
}
static void show_simulator_state(intptr_t) {
    auto now = now_ms();
    for (size_t d = 0; d < bridge::device_count; ++d) {
        const auto &s = engine.state(d);
        const auto &cmd = s.commands[1];
        std::printf("TRV slot=%u reachable=%u requested=%d reported=%d outcome=%u age_ms=%llu\n", unsigned(d),
                    unsigned(s.reachable), cmd.value, s.reported ? s.reported->position : -1,
                    unsigned(cmd.outcome), static_cast<unsigned long long>(now - s.last_seen));
    }
    show_trv_positions(now);
}
static int diagnostic_command(int, char **) {
    if (!esp_openthread_get_instance()) {
        std::printf("Thread stack is not initialized\n");
        return 1;
    }
    wifi_mode_t wifi_mode;
    bool wifi_uninitialized = esp_wifi_get_mode(&wifi_mode) == ESP_ERR_WIFI_NOT_INIT;
    esp_openthread_lock_acquire(portMAX_DELAY);
    auto *ot = esp_openthread_get_instance();
    std::printf("Thread role=%u router_eligible=%u heap=%u minimum_heap=%u wifi_uninitialized=%u "
                "publish_rejections=%u\n",
                unsigned(otThreadGetDeviceRole(ot)), unsigned(otThreadIsRouterEligible(ot)),
                unsigned(esp_get_free_heap_size()), unsigned(esp_get_minimum_free_heap_size()),
                unsigned(wifi_uninitialized), publish_rejections.load());
    esp_openthread_lock_release();
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(show_simulator_state, 0) == CHIP_NO_ERROR ? 0 : 1;
}
static void show_onboarding(intptr_t) {
    char buffer[64];
    chip::MutableCharSpan code(buffer);
    if (GetManualPairingCode(code, chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE)) !=
        CHIP_NO_ERROR) {
        std::printf("Cannot generate commissioning code\n");
        return;
    }
    // Deliberate local disclosure, never part of ordinary diagnostic logs.
    std::printf("PRIVATE development commissioning code: %.*s\n", int(code.size()), code.data());
}
static int onboarding_command(int, char **) {
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(show_onboarding, 0) == CHIP_NO_ERROR ? 0 : 1;
}
static void show_commissioning_status() {
    auto &server = chip::Server::GetInstance();
    auto &connectivity = chip::DeviceLayer::ConnectivityMgr();
    std::printf("Commissioning window_open=%u fabrics=%u ble_enabled=%u ble_advertising=%u "
                "ble_connections=%u uptime_ms=%llu\n",
                unsigned(server.GetCommissioningWindowManager().IsCommissioningWindowOpen()),
                unsigned(server.GetFabricTable().FabricCount()),
                unsigned(connectivity.IsBLEAdvertisingEnabled()), unsigned(ble_gap_adv_active()),
                unsigned(connectivity.NumBLEConnections()), static_cast<unsigned long long>(now_ms()));
}
static void commissioning_work(intptr_t open) {
    auto &manager = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (open) {
        // Do not interrupt an ongoing PASE exchange or replace a controller's
        // enhanced window. No fabrics, network keys or identity are erased.
        if (manager.GetPASESession().HasValue() || chip::DeviceLayer::ConnectivityMgr().NumBLEConnections())
            std::printf("Pairing already in progress; commissioning window unchanged\n");
        else if (manager.IsCommissioningWindowOpen())
            std::printf("Commissioning window is already open\n");
        else {
            auto err = manager.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(900));
            std::printf("Commissioning window open success=%u duration_seconds=900\n",
                        unsigned(err == CHIP_NO_ERROR));
        }
    }
    show_commissioning_status();
}
static int commissioning_command(int argc, char **argv) {
    if (argc != 2 || (std::strcmp(argv[1], "status") && std::strcmp(argv[1], "open")))
        return 1;
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(commissioning_work,
                                                         std::strcmp(argv[1], "open") == 0) == CHIP_NO_ERROR
               ? 0
               : 1;
}
extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init()); // fail visibly; never erase credentials automatically
    load_identity();
    initialize_position_bindings();
    esp_openthread_platform_config_t ot{};
    ot.radio_config.radio_mode = RADIO_MODE_UART_RCP;
    auto &uart = ot.radio_config.radio_uart_config;
    uart.port = UART_NUM_1;
    uart.uart_config.baud_rate = 460800;
    uart.uart_config.data_bits = UART_DATA_8_BITS;
    uart.uart_config.parity = UART_PARITY_DISABLE;
    uart.uart_config.stop_bits = UART_STOP_BITS_1;
    uart.uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart.uart_config.source_clk = UART_SCLK_DEFAULT;
    uart.rx_pin = GPIO_NUM_17;
    uart.tx_pin = GPIO_NUM_18;
    ot.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    ot.port_config.storage_partition_name = "nvs";
    ot.port_config.netif_queue_size = 10;
    ot.port_config.task_queue_size = 10;
    set_openthread_platform_config(&ot);
    node::config_t config;
    auto *node = node::create(&config, changed, identify_cb);
    ESP_ERROR_CHECK(node ? ESP_OK : ESP_FAIL);
    create_devices(node);
    install_position_command_gate();
    ESP_ERROR_CHECK(esp_matter::start(nullptr));
    ESP_ERROR_CHECK(esp_openthread_get_instance() ? ESP_OK : ESP_FAIL);
    wifi_mode_t wifi_mode;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode) == ESP_ERR_WIFI_NOT_INIT ? ESP_OK : ESP_FAIL);
    for (size_t d = 0; d < bridge::device_count; ++d) {
        ESP_ERROR_CHECK(ValveConfigurationAndControl::FindClusterOnEndpoint(identity.valve_endpoint(d))
                            ? ESP_OK
                            : ESP_FAIL);
        ValveConfigurationAndControl::SetDefaultDelegate(identity.valve_endpoint(d), &valve_delegates[d]);
    }
    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &repl_config, &repl));
    esp_console_cmd_t sim{};
    sim.command = "sim";
    sim.help = "sim SLOT available|delay|failures|hold|manual VALUE";
    sim.func = sim_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&sim));
    esp_console_cmd_t valve{};
    valve.command = "sim_valve";
    valve.help = "Native simulated valve command: sim_valve SLOT LEVEL (0 closes; 101..255 test rejection)";
    valve.func = sim_valve_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&valve));
    esp_console_cmd_t diag{};
    diag.command = "diag";
    diag.help = "Thread role, eligibility and memory; no credentials";
    diag.func = diagnostic_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&diag));
    esp_console_cmd_t onboarding{};
    onboarding.command = "onboarding";
    onboarding.help = "Explicitly display PRIVATE development commissioning code over USB";
    onboarding.func = onboarding_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&onboarding));
    esp_console_cmd_t commissioning{};
    commissioning.command = "commissioning";
    commissioning.help =
        "commissioning status|open: inspect pairing or open a 15-minute local pairing window";
    commissioning.func = commissioning_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&commissioning));
    register_ble_diagnostics();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    for (;;) {
        if (chip::DeviceLayer::PlatformMgr().ScheduleWork(publish, 0) != CHIP_NO_ERROR)
            publish_rejections.fetch_add(1);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
