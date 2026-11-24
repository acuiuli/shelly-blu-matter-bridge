// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <nimble/ble.h>
void register_ble_diagnostics();
bool ble_diagnostic_scan_active();
bool find_discovered_trv(const uint8_t address[6], ble_addr_t &peer);
