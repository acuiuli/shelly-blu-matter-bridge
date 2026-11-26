// SPDX-License-Identifier: Apache-2.0
#include "trv_gatt_probe.h"
#include "ble_diagnostics.h"
#include "bridge/enrollment.hpp"
#include "bridge/trv_readback.hpp"
#include "matter_trv_position.h"
#include "trv_binding_store.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_console.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <host/ble_store.h>
#include <limits>
#include <nimble/nimble_port.h>
#include <os/os_mbuf.h>

namespace {
constexpr size_t field_count = size_t(bridge::TrvReadField::count);
const char *names[] = {"target_centi_c", "position", "calibration", "functions", "errors"};
const char *uuids[] = {"4936d0ba-ff05-472c-907b-6551898fc016", "c3020a1d-f05b-4430-b2e9-f97df761c33b",
                       "6db6f15e-43d6-425e-8669-5b449ab08250", "7ff16da2-6f68-4dc6-a97a-bee89d36e739",
                       "00a347f9-4a0a-4ad4-a2d5-1cc01f9b3fe8"};
enum class Phase { idle, queued, connecting, security, services, reading, configuring, disconnecting, done };
const char *phases[] = {"idle",    "queued",      "connecting",    "security", "services",
                        "reading", "configuring", "disconnecting", "done"};
enum class Operation { probe, enroll, reconnect, valve_only, repair, position };
const char *operations[] = {"probe", "enroll", "reconnect", "valve_only", "repair", "position"};
bool pairs_peer(Operation op) { return op == Operation::enroll || op == Operation::repair; }
struct Snapshot {
    Phase phase = Phase::idle;
    int result = 0;
    std::array<std::optional<int>, field_count> values;
    std::array<int, field_count> status;
    uint64_t elapsed_ms = 0;
    Operation operation = Operation::probe;
    int slot = -1;
    bool encrypted = false, bonded = false, authenticated = false;
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
constexpr size_t position_field = size_t(bridge::TrvReadField::position);
uint64_t position_observed_ms = 0;
unsigned successes = 0;
int final_result = 0;
bool bonding_overridden = false;
bool stopping = false;
uint8_t saved_bonding = 0;
uint64_t started_ms = 0;
Operation operation = Operation::probe;
size_t selected_slot = 0;
bridge::Enrollment bindings;
bool secured = false;
uint16_t expected_functions = 0;

bool peer_has_key(const ble_addr_t &address) {
    ble_store_key_sec key{};
    key.peer_addr = address;
    ble_store_value_sec value{};
    const int result = ble_store_read_peer_sec(&key, &value);
    bool found = result == 0 && value.ltk_present && value.key_size == 16;
    // Key bytes stay local to this lookup and are never returned or logged.
    volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(value); ++i)
        bytes[i] = 0;
    return found;
}
bool secure_peer() {
    ble_gap_conn_desc desc{};
    if (ble_gap_conn_find(connection, &desc) || !desc.sec_state.encrypted || !desc.sec_state.bonded ||
        desc.sec_state.key_size != 16 || (desc.peer_id_addr.type & 1) != peer.type ||
        std::memcmp(desc.peer_id_addr.val, peer.val, 6) || !peer_has_key(peer))
        return false;
    portENTER_CRITICAL(&snapshot_lock);
    snapshot.encrypted = desc.sec_state.encrypted;
    snapshot.bonded = desc.sec_state.bonded;
    snapshot.authenticated = desc.sec_state.authenticated;
    portEXIT_CRITICAL(&snapshot_lock);
    return true;
}

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
int confirm_functions(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *attribute, void *) {
    if (!active.load() || stopping || conn != connection)
        return 0;
    int status = error->status;
    std::optional<int> value;
    uint8_t bytes[2];
    if (!status && attribute && attribute->om && OS_MBUF_PKTLEN(attribute->om) == 2 &&
        os_mbuf_copydata(attribute->om, 0, 2, bytes) == 0)
        value = bridge::decode_trv_readback(bridge::TrvReadField::functions, bytes, 2);
    if (!status && (!value || *value != expected_functions || !secure_peer()))
        status = BLE_HS_EBADDATA;
    portENTER_CRITICAL(&snapshot_lock);
    snapshot.values[3] = value;
    snapshot.status[3] = status;
    portEXIT_CRITICAL(&snapshot_lock);
    disconnect(status);
    return 0;
}
int functions_written(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *) {
    if (!active.load() || stopping || conn != connection)
        return 0;
    int err = error->status;
    if (!err)
        err = ble_gattc_read(connection, handles[3], confirm_functions, nullptr);
    if (err)
        disconnect(err);
    return 0;
}
void reads_complete() {
    if (operation == Operation::probe) {
        disconnect(successes ? 0 : BLE_HS_ENOENT);
        return;
    }
    const bool poll = operation == Operation::position;
    if (successes != (poll ? 1U : field_count) || !secure_peer() ||
        (poll && bindings.slots[selected_slot].state != bridge::EnrollmentState::verified)) {
        disconnect(BLE_HS_EAUTHEN);
        return;
    }
    // A reservation becomes verified only after encrypted, bonded access and
    // every allowlisted read (including the protected error characteristic).
    if (bindings.slots[selected_slot].state == bridge::EnrollmentState::pending) {
        bindings.slots[selected_slot].state = bridge::EnrollmentState::verified;
        if (save_trv_bindings(bindings) != ESP_OK) {
            disconnect(BLE_HS_ESTORE_FAIL);
            return;
        }
    }
    observe_trv_position(selected_slot, bindings.slots[selected_slot].identity,
                         uint8_t(*snapshot.values[position_field]), position_observed_ms);
    if (operation != Operation::valve_only) {
        disconnect(0);
        return;
    }
    int functions;
    portENTER_CRITICAL(&snapshot_lock);
    functions = *snapshot.values[3];
    portEXIT_CRITICAL(&snapshot_lock);
    expected_functions = bridge::valve_only_functions(uint16_t(functions));
    if (expected_functions == functions) {
        disconnect(0); // already configured; no write
        return;
    }
    phase(Phase::configuring);
    const uint8_t bytes[] = {uint8_t(expected_functions), uint8_t(expected_functions >> 8)};
    // This is the only configuration write. Never write position, setpoint,
    // calibration, PIN, reset, or any other characteristic in this module.
    int err = ble_gattc_write_flat(connection, handles[3], bytes, sizeof(bytes), functions_written, nullptr);
    if (err)
        disconnect(err);
}
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
    if (decoded) {
        ++successes;
        if (reading == position_field)
            position_observed_ms = esp_timer_get_time() / 1000;
    }
    ++reading;
    next_read();
    return 0;
}
void next_read() {
    if (!active.load() || stopping)
        return;
    while (reading < field_count) {
        if (operation == Operation::position && reading != position_field) {
            ++reading;
            continue;
        }
        int err = handles[reading] ? ble_gattc_read(connection, handles[reading], read_value, nullptr)
                                   : BLE_HS_ENOENT;
        if (err == 0)
            return;
        portENTER_CRITICAL(&snapshot_lock);
        snapshot.status[reading] = err;
        portEXIT_CRITICAL(&snapshot_lock);
        ++reading;
    }
    reads_complete();
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
void discover_services() {
    phase(Phase::services);
    if (int err = ble_gattc_disc_svc_by_uuid(connection, &service_uuid.u, service, nullptr))
        disconnect(err);
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
        if (operation == Operation::probe) {
            discover_services();
        } else {
            phase(Phase::security);
            // Reconnect requires a pre-existing LTK. No automatic re-pairing,
            // bond deletion or fallback to plaintext on a missing/stale key.
            ble_gap_conn_desc desc{};
            if (ble_gap_conn_find(connection, &desc) || desc.our_id_addr.type != BLE_ADDR_PUBLIC ||
                desc.peer_id_addr.type != peer.type || std::memcmp(desc.peer_id_addr.val, peer.val, 6)) {
                disconnect(BLE_HS_EINVAL);
                break;
            }
            if (!pairs_peer(operation) && !peer_has_key(peer)) {
                disconnect(BLE_HS_EAUTHEN);
                break;
            }
            if (int err = ble_gap_security_initiate(connection))
                disconnect(err);
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (stopping || operation == Operation::probe || secured)
            break;
        if (event->enc_change.status || !secure_peer()) {
            disconnect(event->enc_change.status ? event->enc_change.status : BLE_HS_EAUTHEN);
        } else {
            secured = true;
            discover_services();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        finish(stopping ? final_result : BLE_HS_ENOTCONN);
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        disconnect(BLE_HS_EAUTHEN); // PIN-protected enrollment is not implemented; never guess or downgrade
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
    // Matter generates a random static commissioning address on boot. This SDK
    // prefers that address in infer_auto(), which invalidates downstream bonds
    // on reboot. Select the controller's public identity only for bonded GATT.
    uint8_t own_type = BLE_OWN_ADDR_PUBLIC;
    int err = operation == Operation::probe ? ble_hs_id_infer_auto(0, &own_type)
                                            : ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, nullptr, nullptr);
    if (err) {
        finish(err);
        return;
    }
    handles = {};
    service_start = service_end = 0;
    successes = 0;
    position_observed_ms = 0;
    final_result = 0;
    stopping = false;
    secured = false;
    if (operation != Operation::probe) {
        if (load_trv_bindings(bindings) != ESP_OK) {
            finish(BLE_HS_ESTORE_FAIL);
            return;
        }
        auto &binding = bindings.slots[selected_slot];
        if (pairs_peer(operation)) {
            const bool pending = binding.state == bridge::EnrollmentState::pending;
            if (operation == Operation::enroll &&
                (binding.state == bridge::EnrollmentState::verified ||
                 (pending &&
                  (binding.address_type != peer.type || std::memcmp(binding.address.data(), peer.val, 6))))) {
                finish(BLE_HS_EALREADY);
                return;
            }
            int count = 0, our_count = 0;
            if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count) ||
                ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &our_count) ||
                (!peer_has_key(peer) &&
                 (count >= CONFIG_BT_NIMBLE_MAX_BONDS || our_count >= CONFIG_BT_NIMBLE_MAX_BONDS))) {
                finish(BLE_HS_ESTORE_CAP); // no least-used bond eviction
                return;
            }
            if (operation == Operation::repair) {
                std::array<uint8_t, 6> selected;
                std::copy(peer.val, peer.val + selected.size(), selected.begin());
                if (!bindings.begin_repair(selected_slot, peer.type, selected)) {
                    finish(BLE_HS_EINVAL);
                    return;
                }
                // Commit before clearing this peer's local keys. Power loss
                // leaves the same identity reserved for a deliberate retry.
                if (save_trv_bindings(bindings) != ESP_OK) {
                    finish(BLE_HS_ESTORE_FAIL);
                    return;
                }
                invalidate_trv_position(selected_slot);
                int rc = ble_gap_unpair(&peer);
                if (rc && rc != BLE_HS_ENOENT) {
                    finish(rc);
                    return;
                }
            } else if (!pending) {
                bridge::TrvBinding reservation;
                reservation.state = bridge::EnrollmentState::pending;
                reservation.address_type = peer.type;
                std::copy(peer.val, peer.val + 6, reservation.address.begin());
                esp_fill_random(reservation.identity.data(), reservation.identity.size());
                if (!bindings.reserve(selected_slot, reservation) || save_trv_bindings(bindings) != ESP_OK) {
                    finish(BLE_HS_ESTORE_FAIL);
                    return;
                }
            }
        } else {
            if (binding.state == bridge::EnrollmentState::empty ||
                ((operation == Operation::valve_only || operation == Operation::position) &&
                 binding.state != bridge::EnrollmentState::verified)) {
                finish(BLE_HS_ENOENT);
                return;
            }
            peer.type = binding.address_type;
            std::copy(binding.address.begin(), binding.address.end(), peer.val);
            if (!peer_has_key(peer)) {
                finish(BLE_HS_EAUTHEN);
                return;
            }
        }
    }
    // Matter owns this single host and its callbacks. Change only the bonding
    // request bit, on the host task, with no other BLE link or advertisement.
    // Only enrollment or explicit repair requests a new bond. The probe never requests
    // bonding, and reconnect/configuration require existing keys.
    saved_bonding = ble_hs_cfg.sm_bonding;
    ble_hs_cfg.sm_bonding = operation == Operation::probe ? 0 : 1;
    bonding_overridden = true;
    phase(Phase::connecting);
    if (ble_npl_callout_reset(&timeout, ble_npl_time_ms_to_ticks32(
                                            operation == Operation::probe ? 20000 : 30000)) != BLE_NPL_OK) {
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
    std::printf("TRV operation=%s slot=%d encrypted=%u bonded=%u authenticated=%u\n",
                operations[size_t(value.operation)], value.slot, unsigned(value.encrypted),
                unsigned(value.bonded), unsigned(value.authenticated));
    for (size_t i = 0; i < field_count; ++i) {
        std::printf("TRV read field=%s status=%d", names[i], value.status[i]);
        if (value.values[i])
            std::printf(" value=%d", *value.values[i]);
        std::printf("\n");
    }
}
bool selected_address(const char *text, ble_addr_t &selected, bool pairing_required) {
    // Enrollment can use the anonymous discovery token. No vendor gateway/app,
    // known MAC, or address dump is needed for a factory-new valve.
    const char *separator = std::strchr(text, ':');
    if (pairing_required && separator && separator == std::strrchr(text, ':')) {
        char *end;
        errno = 0;
        if (text[0] < '0' || text[0] > '9' || separator[1] < '0' || separator[1] > '9')
            return false;
        auto session = std::strtoul(text, &end, 10);
        if (errno || end != separator || session > std::numeric_limits<unsigned>::max())
            return false;
        auto index = std::strtoul(separator + 1, &end, 10);
        if (errno || *end || index > std::numeric_limits<unsigned>::max() ||
            !find_discovered_trv_candidate(unsigned(session), unsigned(index), selected)) {
            std::printf("Candidate must belong to the latest discovery and be in pairing mode with PIN "
                        "protection off\n");
            return false;
        }
        return selected.type == BLE_ADDR_PUBLIC ||
               (selected.type == BLE_ADDR_RANDOM && (selected.val[5] & 0xc0) == 0xc0);
    }
    if (std::strlen(text) != 17)
        return false;
    uint8_t address[6];
    for (size_t i = 0; i < 6; ++i) {
        if (i < 5 && text[i * 3 + 2] != ':')
            return false;
        unsigned octet = 0;
        for (size_t digit = 0; digit < 2; ++digit) {
            char c = text[i * 3 + digit];
            int nibble = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : -1;
            if (nibble < 0)
                return false;
            octet = (octet << 4) | unsigned(nibble);
        }
        address[5 - i] = uint8_t(octet);
    }
    if (!find_discovered_trv(address, selected, pairing_required)) {
        std::printf("Target must be an unambiguous recently discovered TRV");
        if (pairing_required)
            std::printf(" in pairing mode with PIN protection off");
        std::printf("\n");
        return false;
    }
    return selected.type == BLE_ADDR_PUBLIC ||
           (selected.type == BLE_ADDR_RANDOM && (selected.val[5] & 0xc0) == 0xc0);
}
int queue_operation(Operation selected_operation, size_t slot, const ble_addr_t &target) {
    if (ble_diagnostic_scan_active() || !ble_hs_synced())
        return 1;
    bool expected = false;
    if (!active.compare_exchange_strong(expected, true))
        return 1;
    peer = target;
    operation = selected_operation;
    selected_slot = slot;
    portENTER_CRITICAL(&snapshot_lock);
    snapshot = {};
    snapshot.status.fill(-1);
    snapshot.phase = Phase::queued;
    snapshot.operation = selected_operation;
    snapshot.slot = selected_operation == Operation::probe ? -1 : int(slot);
    portEXIT_CRITICAL(&snapshot_lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &start_event);
    if (selected_operation != Operation::position)
        std::printf("TRV operation=%s queued\n", operations[size_t(selected_operation)]);
    return 0;
}
int probe_command(int argc, char **argv) {
    if (argc != 2)
        return 1;
    if (std::strcmp(argv[1], "status") == 0) {
        print_status();
        return 0;
    }
    // The address argument and all console history/captures are private.
    ble_addr_t target;
    return selected_address(argv[1], target, false) ? queue_operation(Operation::probe, 0, target) : 1;
}
void enrollment_status() {
    print_status();
    bridge::Enrollment table;
    auto err = load_trv_bindings(table);
    if (err != ESP_OK) {
        std::printf("TRV bindings storage_error=%d; no automatic erase\n", int(err));
        return;
    }
    const char *states[] = {"empty", "pending", "verified"};
    for (size_t i = 0; i < table.capacity; ++i)
        std::printf("TRV enrollment slot=%u state=%s\n", unsigned(i), states[size_t(table.slots[i].state)]);
}
int enrollment_command(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "status") == 0) {
        enrollment_status();
        return 0;
    }
    Operation op = std::strcmp(argv[0], "trv_enroll") == 0       ? Operation::enroll
                   : std::strcmp(argv[0], "trv_repair") == 0     ? Operation::repair
                   : std::strcmp(argv[0], "trv_valve_only") == 0 ? Operation::valve_only
                                                                 : Operation::reconnect;
    if (argc != (pairs_peer(op) ? 3 : 2) || std::strlen(argv[1]) != 1 || argv[1][0] < '0' ||
        argv[1][0] >= '0' + int(bridge::Enrollment::capacity))
        return 1;
    size_t slot = size_t(argv[1][0] - '0');
    ble_addr_t target{};
    if (pairs_peer(op) && !selected_address(argv[2], target, true))
        return 1;
    return queue_operation(op, slot, target);
}
} // namespace

bool queue_position_read(size_t slot) {
    return slot < bridge::Enrollment::capacity && queue_operation(Operation::position, slot, {}) == 0;
}

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
    for (const auto *name : {"trv_enroll", "trv_repair", "trv_reconnect", "trv_valve_only"}) {
        esp_console_cmd_t cmd{};
        cmd.command = name;
        cmd.help =
            std::strcmp(name, "trv_enroll") == 0
                ? "USB enrollment: trv_enroll SLOT(0..3) SESSION:INDEX|PRIVATE_MAC, or status; requires "
                  "pairing mode"
            : std::strcmp(name, "trv_repair") == 0 ? "Refresh the same valve's BLE keys: trv_repair SLOT "
                                                     "SESSION:INDEX|PRIVATE_MAC; requires pairing mode"
            : std::strcmp(name, "trv_reconnect") == 0
                ? "Encrypted read using stored bond: trv_reconnect SLOT(0..3)|status"
                : "Explicitly disable only automatic temperature control, with readback: trv_valve_only "
                  "SLOT(0..3)|status";
        cmd.func = enrollment_command;
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    }
}
