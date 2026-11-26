// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/position_observation.hpp"
#include <cstddef>

void initialize_position_bindings();
void publish_trv_positions(uint64_t now_ms);
void show_trv_positions(uint64_t now_ms);
void observe_trv_position(size_t slot, const bridge::EnrollmentId &source, uint8_t percent, uint64_t now_ms);
void expire_trv_position_sample(size_t slot, const bridge::EnrollmentId &id);
void invalidate_trv_position(size_t slot);
void finish_matter_position(size_t slot, const bridge::EnrollmentId &id, int result);
