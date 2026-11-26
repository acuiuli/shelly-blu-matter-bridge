// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/position_observation.hpp"
#include <cstddef>

// Install the command gate before starting Matter. All endpoint state is owned
// by the Matter task. BLE sends bounded copies through the observation mailbox.
void install_position_command_gate();
void initialize_position_bindings();
void publish_trv_positions(uint64_t now_ms);
void show_trv_positions(uint64_t now_ms);
void observe_trv_position(size_t slot, const bridge::EnrollmentId &source, uint8_t percent, uint64_t now_ms);
void invalidate_trv_position(size_t slot);
