// SPDX-License-Identifier: Apache-2.0
#include "trv_gatt_probe.h"
#include "ble_diagnostics.h"
#include "bridge/trv_readback.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_console.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <nimble/nimble_port.h>
#include <os/os_mbuf.h>

namespace {
constexpr size_t field_count = size_t(bridge::TrvReadField::count);
const char *names[] = {"target_centi_c", "position", "calibration", "functions", "errors"};
const char *uuids[] = {"4936d0ba-ff05-472c-907b-6551898fc016", "c3020a1d-f05b-4430-b2e9-f97df761c33b",
                       "6db6f15e-43d6-425e-8669-5b449ab08250", "7ff16da2-6f68-4dc6-a97a-bee89d36e739",
                       "00a347f9-4a0a-4ad4-a2d5-1cc01f9b3fe8"};
enum class Phase { idle, queued, connecting, services, reading, disconnecting, done };
const char *phases[] = {"idle", "queued", "connecting", "services", "reading", "disconnecting", "done"};
struct Snapshot {
    Phase phase = Phase::idle;
    int result = 0;
    std::array<std::optional<int>, field_count> values;
    std::array<int, field_count> status;
    uint64_t elapsed_ms = 0;
};
Snapshot snapshot;
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> active{false};
ble_npl_event start_event;
ble_npl_callout timeout;
ble_addr_t peer;
ble_uuid_any_t service_uuid;
std::array<ble_uuid_any_t, field_count> characteristic_uuids;
std::array<uint16_t, field_count> handles{};
uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
uint16_t service_start = 0, service_end = 0;
size_t reading = 0;
unsigned successes = 0;
int final_result = 0;
bool bonding_overridden = false;
bool stopping = false;
uint8_t saved_bonding = 0;
uint64_t started_ms = 0;

void phase(Phase value) {
    portENTER_CRITICAL(&snapshot_lock);
    snapshot.phase = value;
    portEXIT_CRITICAL(&snapshot_lock);
}
void finish(int result) {
    ble_npl_callout_stop(&timeout);
    if (bonding_overridden) {
        ble_hs_cfg.sm_bonding = saved_bonding;
        bonding_overridden = false;
    }
    connection = BLE_HS_CONN_HANDLE_NONE;
    portENTER_CRITICAL(&snapshot_lock);
    snapshot.phase = Phase::done;
    snapshot.result = result;
    snapshot.elapsed_ms = uint64_t(esp_timer_get_time() / 1000) - started_ms;
    portEXIT_CRITICAL(&snapshot_lock);
    active.store(false);
}
void disconnect(int result) {
    if (stopping)
        return;
    stopping = true;
    final_result = result;
    phase(Phase::disconnecting);
    if (connection != BLE_HS_CONN_HANDLE_NONE) {
        int err = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        if (err == BLE_HS_ENOTCONN)
            finish(result);
    } else if (!ble_gap_conn_active()) {
        finish(result);
    } else {
        int err = ble_gap_conn_cancel();
        if (err != 0)
            finish(result);
    }
}
void next_read();
int read_value(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *attribute, void *) {
    if (!active.load() || stopping || conn != connection || reading >= field_count)
        return 0;
    std::optional<int> decoded;
    int status = error->status;
    if (!status && attribute && attribute->om) {
        uint8_t bytes[2];
        auto size = OS_MBUF_PKTLEN(attribute->om);
        if (size <= sizeof(bytes) && os_mbuf_copydata(attribute->om, 0, size, bytes) == 0)
            decoded = bridge::decode_trv_readback(bridge::TrvReadField(reading), bytes, size);
        if (!decoded)
            status = BLE_HS_EBADDATA;
    } else if (!status) {
        status = BLE_HS_EBADDATA;
    }
    portENTER_CRITICAL(&snapshot_lock);
    snapshot.values[reading] = decoded;
    snapshot.status[reading] = status;
    portEXIT_CRITICAL(&snapshot_lock);
    if (decoded)
        ++successes;
    ++reading;
    next_read();
    return 0;
}
void next_read() {
    if (!active.load() || stopping)
        return;
    while (reading < field_count) {
        int err = handles[reading] ? ble_gattc_read(connection, handles[reading], read_value, nullptr)
                                   : BLE_HS_ENOENT;
        if (err == 0)
            return;
        portENTER_CRITICAL(&snapshot_lock);
        snapshot.status[reading] = err;
        portEXIT_CRITICAL(&snapshot_lock);
        ++reading;
    }
    disconnect(successes ? 0 : BLE_HS_ENOENT);
}
int characteristic(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *chr, void *) {
    if (!active.load() || stopping || conn != connection)
        return 0;
    if (!error->status && chr) {
        for (size_t i = 0; i < field_count; ++i)
            if (ble_uuid_cmp(&chr->uuid.u, &characteristic_uuids[i].u) == 0)
                handles[i] = chr->val_handle;
    } else if (error->status == BLE_HS_EDONE) {
        phase(Phase::reading);
        reading = 0;
        next_read();
    } else {
        disconnect(error->status);
    }
    return 0;
}
int service(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *svc, void *) {
    if (!active.load() || stopping || conn != connection)
        return 0;
    if (!error->status && svc) {
        service_start = svc->start_handle;
        service_end = svc->end_handle;
    } else if (error->status == BLE_HS_EDONE) {
        int err = service_start ? ble_gattc_disc_all_chrs(connection, service_start, service_end,
                                                          characteristic, nullptr)
                                : BLE_HS_ENOENT;
        if (err)
            disconnect(err);
    } else {
        disconnect(error->status);
    }
    return 0;
}
int gap_event(ble_gap_event *event, void *) {
    if (!active.load())
        return 0;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) {
            finish(final_result ? final_result : event->connect.status);
            break;
        }
        connection = event->connect.conn_handle;
        if (stopping) {
            if (ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM) == BLE_HS_ENOTCONN)
                finish(final_result);
            break;
        }
        phase(Phase::services);
        if (int err = ble_gattc_disc_svc_by_uuid(connection, &service_uuid.u, service, nullptr))
            disconnect(err);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        finish(stopping ? final_result : BLE_HS_ENOTCONN);
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        disconnect(BLE_HS_EAUTHEN); // never request, print or inject a passkey in this probe
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE; // never delete an existing bond
    default:
        break;
    }
    return 0;
}
void timed_out(ble_npl_event *) { disconnect(BLE_HS_ETIMEOUT); }

bool existing_connection() {
    // Use public APIs only. HCI handles 0x0f00..0x0fff are reserved. This bounded
    // check runs once per explicit probe, never per advertisement or report.
    for (uint16_t handle = 0; handle < 0x0f00; ++handle)
        if (ble_gap_conn_find(handle, nullptr) == 0)
            return true;
    return false;
}
void start_probe(ble_npl_event *) {
    started_ms = esp_timer_get_time() / 1000;
    if (ble_diagnostic_scan_active() || ble_gap_disc_active() || ble_gap_adv_active() ||
        ble_gap_conn_active() || existing_connection()) {
        finish(BLE_HS_EBUSY);
        return;
    }
    uint8_t own_type;
    int err = ble_hs_id_infer_auto(0, &own_type);
    if (err) {
        finish(err);
        return;
    }
    handles = {};
    service_start = service_end = 0;
    successes = 0;
    final_result = 0;
    stopping = false;
    // Matter owns this single host and its callbacks. Change only the bonding
    // request bit, on the host task, with no other BLE link or advertisement.
    // Even if the valve requests transient link encryption, neither peer is
    // asked to create a persistent bond. Restore Matter's setting on disconnect.
    saved_bonding = ble_hs_cfg.sm_bonding;
    ble_hs_cfg.sm_bonding = 0;
    bonding_overridden = true;
    phase(Phase::connecting);
    if (ble_npl_callout_reset(&timeout, ble_npl_time_ms_to_ticks32(20000)) != BLE_NPL_OK) {
        finish(BLE_HS_ENOMEM);
        return;
    }
    err = ble_gap_connect(own_type, &peer, 10000, nullptr, gap_event, nullptr);
    if (err)
        finish(err);
}
void print_status() {
    Snapshot value;
    portENTER_CRITICAL(&snapshot_lock);
    value = snapshot;
    portEXIT_CRITICAL(&snapshot_lock);
    std::printf("TRV probe state=%s result=%d elapsed_ms=%llu host_bonding=%u\n", phases[size_t(value.phase)],
                value.result, static_cast<unsigned long long>(value.elapsed_ms),
                unsigned(ble_hs_cfg.sm_bonding));
    for (size_t i = 0; i < field_count; ++i) {
        std::printf("TRV read field=%s status=%d", names[i], value.status[i]);
        if (value.values[i])
            std::printf(" value=%d", *value.values[i]);
        std::printf("\n");
    }
}
int probe_command(int argc, char **argv) {
    if (argc != 2)
        return 1;
    if (std::strcmp(argv[1], "status") == 0) {
        print_status();
        return 0;
    }
    // This is an explicit private USB target, not a broadcast address dump.
    // Keep console history/captures private, as with the onboarding command.
    if (std::strlen(argv[1]) != 17 || ble_diagnostic_scan_active() || !ble_hs_synced())
        return 1;
    uint8_t address[6];
    for (size_t i = 0; i < 6; ++i) {
        if (i < 5 && argv[1][i * 3 + 2] != ':')
            return 1;
        unsigned octet = 0;
        for (size_t digit = 0; digit < 2; ++digit) {
            char c = argv[1][i * 3 + digit];
            int nibble = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : -1;
            if (nibble < 0)
                return 1;
            octet = (octet << 4) | unsigned(nibble);
        }
        address[5 - i] = uint8_t(octet);
    }
    ble_addr_t selected;
    if (!find_discovered_trv(address, selected)) {
        std::printf("Target must be an unambiguous TRV seen by trv_discover within 120 seconds\n");
        return 1;
    }
    bool expected = false;
    if (!active.compare_exchange_strong(expected, true))
        return 1;
    peer = selected;
    portENTER_CRITICAL(&snapshot_lock);
    snapshot = {};
    snapshot.status.fill(-1);
    snapshot.phase = Phase::queued;
    portEXIT_CRITICAL(&snapshot_lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &start_event);
    std::printf("Read-only TRV probe queued; no characteristic writes or persistent bond requested\n");
    return 0;
}
} // namespace

bool trv_gatt_probe_active() { return active.load(); }
void register_trv_gatt_probe() {
    ESP_ERROR_CHECK(ble_uuid_from_str(&service_uuid, "de8a5aac-a99b-c315-0c80-60d4cbb51225") == 0 ? ESP_OK
                                                                                                  : ESP_FAIL);
    for (size_t i = 0; i < field_count; ++i)
        ESP_ERROR_CHECK(ble_uuid_from_str(&characteristic_uuids[i], uuids[i]) == 0 ? ESP_OK : ESP_FAIL);
    ble_npl_event_init(&start_event, start_probe, nullptr);
    ESP_ERROR_CHECK(ble_npl_callout_init(&timeout, nimble_port_get_dflt_eventq(), timed_out, nullptr) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    esp_console_cmd_t command{};
    command.command = "trv_probe";
    command.help = "Read-only GATT probe: trv_probe PRIVATE_MAC|status; run trv_discover first";
    command.func = probe_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&command));
}
