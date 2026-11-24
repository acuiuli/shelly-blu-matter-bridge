// SPDX-License-Identifier: Apache-2.0
#include "ble_diagnostics.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_console.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>

namespace {
std::atomic<bool> running{false};
std::atomic<unsigned> reports{0};
std::atomic<int> minimum_rssi{127};
std::atomic<int> maximum_rssi{-128};
std::atomic<int> completion_reason{0};

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
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        completion_reason.store(event->disc_complete.reason);
        running.store(false);
    }
    return 0;
}

int scan_command(int argc, char **argv) {
    if (argc != 2) {
        std::printf("Usage: ble_scan SECONDS(1..30)|status\n");
        return 1;
    }
    if (std::strcmp(argv[1], "status") == 0) {
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
    uint8_t own_address_type;
    int result = ble_hs_id_infer_auto(0, &own_address_type);
    if (result == 0) {
        reports.store(0);
        minimum_rssi.store(127);
        maximum_rssi.store(-128);
        completion_reason.store(0);
        ble_gap_disc_params parameters{};
        parameters.passive = 1;
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
    std::printf("Passive BLE scan started for %ld seconds; use ble_scan status for counts\n", seconds);
    return 0;
}
} // namespace

void register_ble_diagnostics() {
    esp_console_cmd_t command{};
    command.command = "ble_scan";
    command.help = "Passive BLE diagnostic: ble_scan SECONDS(1..30)|status; no addresses or payloads logged";
    command.func = scan_command;
    ESP_ERROR_CHECK(esp_console_cmd_register(&command));
}
