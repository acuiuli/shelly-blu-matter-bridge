// SPDX-License-Identifier: Apache-2.0
#include "bridge/bridge.hpp"
#include <app/CommandHandler.h>
#include <app/data-model/Decode.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_console.h>
#include <esp_heap_caps.h>
#include <esp_matter.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <platform/CHIPDeviceLayer.h>
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
using Status = chip::Protocols::InteractionModel::Status;
static bridge::Engine engine;
static bridge::Simulator simulator;
static bridge::Identity identity;
static bool publishing = false;
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

// Intercept the standard server command: its default immediate-motion behavior
// must not turn a transport acknowledgment into confirmed physical position.
static esp_err_t valve_command(const chip::app::ConcreteCommandPath &path, chip::TLV::TLVReader &reader,
                               void *opaque) {
    using namespace ValveConfigurationAndControl;
    int level = 0;
    Status status = Status::Success;
    if (path.mCommandId == Commands::Open::Id) {
        Commands::Open::DecodableType args;
        if (chip::app::DataModel::Decode(reader, args) != CHIP_NO_ERROR)
            status = Status::InvalidCommand;
        else if (args.openDuration.HasValue() && !args.openDuration.Value().IsNull())
            status = Status::InvalidCommand; // timed operation requires a separate implementation
        else
            level = args.targetLevel.HasValue() ? args.targetLevel.Value() : 100;
        if (status == Status::Success && (level < 1 || level > 100))
            status = Status::ConstraintError;
    } else {
        Commands::Close::DecodableType args;
        if (chip::app::DataModel::Decode(reader, args) != CHIP_NO_ERROR)
            status = Status::InvalidCommand;
    }
    bool accepted = false;
    if (status == Status::Success) {
        for (size_t d = 0; d < bridge::device_count; ++d)
            if (path.mEndpointId == identity.valve_endpoint(d))
                accepted = engine.request(d, bridge::Field::position, level, now_ms());
        if (!accepted)
            status = Status::ConstraintError;
    }
    static_cast<chip::app::CommandHandler *>(opaque)->AddStatus(path, status);
    // ESP-Matter runs its built-in callback only when user_callback returns ESP_OK.
    return ESP_FAIL;
}
static void update(uint16_t ep, uint32_t c, uint32_t a, esp_matter_attr_val_t v) {
    ESP_ERROR_CHECK(attribute::update(ep, c, a, &v));
}
static void publish(intptr_t) {
    simulator.step(engine, now_ms());
    publishing = true;
    for (size_t d = 0; d < bridge::device_count; ++d) {
        auto ep = identity.thermostat_endpoint(d);
        auto vp = identity.valve_endpoint(d);
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
        update(vp, ValveConfigurationAndControl::Id,
               ValveConfigurationAndControl::Attributes::CurrentLevel::Id,
               esp_matter_nullable_uint8(s.reachable ? nullable<uint8_t>(t.position) : nullable<uint8_t>()));
        update(vp, ValveConfigurationAndControl::Id,
               ValveConfigurationAndControl::Attributes::CurrentState::Id,
               esp_matter_nullable_enum8(s.reachable ? nullable<uint8_t>(t.position ? 1 : 0)
                                                     : nullable<uint8_t>()));
        const auto &cmd = s.commands[1];
        bool active = cmd.outcome == bridge::Outcome::queued || cmd.outcome == bridge::Outcome::sent;
        update(vp, ValveConfigurationAndControl::Id,
               ValveConfigurationAndControl::Attributes::TargetLevel::Id,
               esp_matter_nullable_uint8(active ? cmd.value : t.position));
        update(vp, ValveConfigurationAndControl::Id,
               ValveConfigurationAndControl::Attributes::TargetState::Id,
               esp_matter_nullable_enum8((active ? cmd.value : t.position) ? 1 : 0));
    }
    publishing = false;
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
        auto *vp = endpoint::water_valve::create(node, &vc, ENDPOINT_FLAG_NONE, nullptr);
        ESP_ERROR_CHECK(vp && endpoint::get_id(vp) == identity.valve_endpoint(d) ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(endpoint::set_parent_endpoint(vp, ep));
        auto *valve = cluster::get(vp, ValveConfigurationAndControl::Id);
        cluster::valve_configuration_and_control::feature::level::config_t lc;
        ESP_ERROR_CHECK(cluster::valve_configuration_and_control::feature::level::add(valve, &lc));
        command::set_user_callback(
            command::get(valve, ValveConfigurationAndControl::Commands::Open::Id, COMMAND_FLAG_ACCEPTED),
            valve_command);
        command::set_user_callback(
            command::get(valve, ValveConfigurationAndControl::Commands::Close::Id, COMMAND_FLAG_ACCEPTED),
            valve_command);
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
static int diagnostic_command(int, char **) {
    wifi_mode_t wifi_mode;
    bool wifi_uninitialized = esp_wifi_get_mode(&wifi_mode) == ESP_ERR_WIFI_NOT_INIT;
    esp_openthread_lock_acquire(portMAX_DELAY);
    auto *ot = esp_openthread_get_instance();
    std::printf("Thread role=%u router_eligible=%u heap=%u minimum_heap=%u wifi_uninitialized=%u\n",
                unsigned(otThreadGetDeviceRole(ot)), unsigned(otThreadIsRouterEligible(ot)),
                unsigned(esp_get_free_heap_size()), unsigned(esp_get_minimum_free_heap_size()),
                unsigned(wifi_uninitialized));
    esp_openthread_lock_release();
    return 0;
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
extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init()); // fail visibly; never erase credentials automatically
    load_identity();
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
    ESP_ERROR_CHECK(esp_matter::start(nullptr));
    wifi_mode_t wifi_mode;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode) == ESP_ERR_WIFI_NOT_INIT ? ESP_OK : ESP_FAIL);
    // Mark the image healthy only after model and Thread stack initialization.
    // This is a prototype boot check; radio/controller health still needs hardware validation.
    esp_ota_img_states_t state;
    auto *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &repl_config, &repl));
    esp_console_cmd_t sim{};
    sim.command = "sim";
    sim.help = "sim SLOT available|delay|failures|hold|manual VALUE";
    sim.func = sim_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&sim));
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
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    for (;;) {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(publish, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
