// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/position_observation.hpp"
#include <cstddef>
bool queue_position_read(size_t slot);
bool queue_matter_position(size_t slot, const bridge::EnrollmentId &id, uint8_t expected, uint8_t target);
void register_trv_gatt_probe();
bool trv_gatt_probe_active();
