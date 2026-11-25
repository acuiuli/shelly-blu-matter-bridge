// SPDX-License-Identifier: Apache-2.0
#include "ble_diagnostics.h"
#include "bridge/shelly_discovery.hpp"
#include "trv_gatt_probe.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_console.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>

namespace {
std::atomic<bool> running{false};
std::atomic<unsigned> reports{0};
std::atomic<int> minimum_rssi{127};
std::atomic<int> maximum_rssi{-128};
std::atomic<int> completion_reason{0};
std::atomic<bool> discovering{false};
std::atomic<unsigned> malformed_reports{0};
std::atomic<unsigned> unsupported_reports{0};
struct Candidate {
    bool used = false;
    ble_addr_t address{}; // private, RAM only; never printed
    std::optional<uint16_t> flags;
    int rssi = 127;
    unsigned reports = 0;
    uint64_t seen_ms = 0;
};
constexpr size_t candidate_capacity = 16;
std::array<Candidate, candidate_capacity> candidates;
portMUX_TYPE candidate_lock = portMUX_INITIALIZER_UNLOCKED;
unsigned scan_session = 0;
unsigned dropped_candidates = 0;

void observe_trv(const ble_gap_disc_desc &advertisement) {
    auto parsed = bridge::decode_shelly_advertisement(advertisement.data, advertisement.length_data);
    if (parsed.status == bridge::DiscoveryParse::malformed)
        malformed_reports.fetch_add(1);
    else if (parsed.status == bridge::DiscoveryParse::unsupported_block)
        unsupported_reports.fetch_add(1);
    if (!parsed.is_trv())
        return;
    portENTER_CRITICAL(&candidate_lock);
    size_t target = candidate_capacity;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].used && candidates[i].address.type == advertisement.addr.type &&
            std::memcmp(candidates[i].address.val, advertisement.addr.val, 6) == 0) {
            target = i;
            break;
        }
        if (!candidates[i].used && target == candidate_capacity)
            target = i;
    }
    if (target == candidate_capacity)
        ++dropped_candidates;
    else {
        auto &entry = candidates[target];
        entry.used = true;
        entry.address = advertisement.addr;
        if (parsed.flags)
            entry.flags = parsed.flags;
        entry.rssi = advertisement.rssi;
        ++entry.reports;
        entry.seen_ms = esp_timer_get_time() / 1000;
    }
    portEXIT_CRITICAL(&candidate_lock);
}

void discovery_status() {
    std::array<Candidate, candidate_capacity> snapshot;
    unsigned session, dropped;
    portENTER_CRITICAL(&candidate_lock);
    snapshot = candidates;
    session = scan_session;
    dropped = dropped_candidates;
    portEXIT_CRITICAL(&candidate_lock);
    unsigned count = 0;
    for (const auto &entry : snapshot)
        count += entry.used;
    std::printf("TRV discovery session=%u running=%u candidates=%u dropped=%u malformed=%u unsupported=%u\n",
                session, unsigned(discovering.load() && running.load()), count, dropped,
                malformed_reports.load(), unsupported_reports.load());
    for (size_t i = 0; i < snapshot.size(); ++i) {
        const auto &entry = snapshot[i];
        if (!entry.used)
            continue;
        std::printf("TRV candidate=%u:%u model=SBTR-001AEU", session, unsigned(i));
        if (entry.flags)
            std::printf(" pairing=%u auth=%u rpc=%u", unsigned((*entry.flags >> 4) & 1),
                        unsigned((*entry.flags >> 1) & 1), unsigned((*entry.flags >> 2) & 1));
        else
            std::printf(" pairing=unknown auth=unknown rpc=unknown");
        if (entry.rssi != 127)
            std::printf(" rssi=%d", entry.rssi);
        std::printf(" reports=%u\n", entry.reports);
    }
}

int scan_event(ble_gap_event *event, void *) {
    // The NimBLE host owns these events. Never replace Matter's host callbacks,
    // initialize another host, connect to devices, or print advertisement contents.
    if (event->type == BLE_GAP_EVENT_DISC) {
        reports.fetch_add(1);
        const int rssi = event->disc.rssi;
        if (rssi != 127) {
            minimum_rssi.store(std::min(minimum_rssi.load(), rssi));
            maximum_rssi.store(std::max(maximum_rssi.load(), rssi));
        }
        if (discovering.load())
            observe_trv(event->disc);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        completion_reason.store(event->disc_complete.reason);
        running.store(false);
    }
    return 0;
}

int scan_command_impl(int argc, char **argv, bool identify_trvs) {
    if (argc != 2) {
        std::printf("Usage: %s SECONDS(1..30)|status\n", identify_trvs ? "trv_discover" : "ble_scan");
        return 1;
    }
    if (std::strcmp(argv[1], "status") == 0) {
        if (identify_trvs) {
            discovery_status();
            return 0;
        }
        std::printf("BLE synced=%u scan_running=%u reports=%u completion_reason=%d",
                    unsigned(ble_hs_synced()), unsigned(running.load()), reports.load(),
                    completion_reason.load());
        if (minimum_rssi.load() != 127)
            std::printf(" rssi_min=%d rssi_max=%d", minimum_rssi.load(), maximum_rssi.load());
        std::printf("\n");
        return 0;
    }
    char *end;
    const long seconds = std::strtol(argv[1], &end, 10);
    if (!argv[1][0] || *end || seconds < 1 || seconds > 30)
        return 1;
    if (!ble_hs_synced()) {
        std::printf("BLE host is not synchronized\n");
        return 1;
    }
    if (trv_gatt_probe_active()) {
        std::printf("TRV BLE operation is in progress\n");
        return 1;
    }
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        std::printf("Diagnostic scan already running\n");
        return 1;
    }
    if (ble_gap_disc_active()) {
        running.store(false);
        std::printf("BLE discovery is already in use\n");
        return 1;
    }
    discovering.store(identify_trvs);
    if (identify_trvs) {
        portENTER_CRITICAL(&candidate_lock);
        candidates = {};
        ++scan_session;
        dropped_candidates = 0;
        portEXIT_CRITICAL(&candidate_lock);
        malformed_reports.store(0);
        unsupported_reports.store(0);
    }
    uint8_t own_address_type;
    int result = ble_hs_id_infer_auto(0, &own_address_type);
    if (result == 0) {
        reports.store(0);
        minimum_rssi.store(127);
        maximum_rssi.store(-128);
        completion_reason.store(0);
        ble_gap_disc_params parameters{};
        // Active scanning requests scan responses containing Shelly model data.
        // It does not connect, bond, or change a valve's configuration.
        parameters.passive = identify_trvs ? 0 : 1;
        parameters.itvl = 160; // 100 ms interval, 30 ms scan window
        parameters.window = 48;
        parameters.filter_duplicates = 1;
        result = ble_gap_disc(own_address_type, int32_t(seconds * 1000), &parameters, scan_event, nullptr);
    }
    if (result != 0) {
        running.store(false);
        std::printf("BLE scan start failed: %d\n", result);
        return 1;
    }
    if (identify_trvs)
        std::printf("Read-only TRV discovery started for %ld seconds; use trv_discover status\n", seconds);
    else
        std::printf("Passive BLE scan started for %ld seconds; use ble_scan status for counts\n", seconds);
    return 0;
}
int scan_command(int argc, char **argv) { return scan_command_impl(argc, argv, false); }
int discover_command(int argc, char **argv) { return scan_command_impl(argc, argv, true); }
} // namespace

bool ble_diagnostic_scan_active() { return running.load(); }
bool find_discovered_trv(const uint8_t address[6], ble_addr_t &peer, bool pairing_required) {
    auto now = uint64_t(esp_timer_get_time() / 1000);
    unsigned matches = 0;
    portENTER_CRITICAL(&candidate_lock);
    for (const auto &entry : candidates)
        if (entry.used && now - entry.seen_ms <= 120000 && std::memcmp(entry.address.val, address, 6) == 0) {
            // Initial enrollment supports unencrypted/Just Works devices only.
            // Do not downgrade security or guess a PIN for an encrypted valve.
            if (pairing_required &&
                (!entry.flags || !(*entry.flags & (1 << 4)) || (*entry.flags & (1 << 1)))) {
                matches += 2; // reject ambiguous/stale security advertisements
            } else {
                peer = entry.address;
                ++matches;
            }
        }
    portEXIT_CRITICAL(&candidate_lock);
    return matches == 1;
}
bool find_discovered_trv_candidate(unsigned session, unsigned index, ble_addr_t &peer) {
    const auto now = uint64_t(esp_timer_get_time() / 1000);
    bool found = false;
    portENTER_CRITICAL(&candidate_lock);
    if (session == scan_session && index < candidates.size()) {
        const auto &entry = candidates[index];
        found = entry.used && now - entry.seen_ms <= 120000 && entry.flags && (*entry.flags & (1 << 4)) &&
                !(*entry.flags & (1 << 1));
        if (found)
            peer = entry.address;
    }
    portEXIT_CRITICAL(&candidate_lock);
    return found;
}

void register_ble_diagnostics() {
    esp_console_cmd_t command{};
    command.command = "ble_scan";
    command.help = "Passive BLE diagnostic: ble_scan SECONDS(1..30)|status; no addresses or payloads logged";
    command.func = scan_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&command));
    esp_console_cmd_t discovery{};
    discovery.command = "trv_discover";
    discovery.help =
        "Read-only Shelly BLU TRV discovery: trv_discover SECONDS(1..30)|status; no addresses logged";
    discovery.func = discover_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&discovery));
    register_trv_gatt_probe();
}
