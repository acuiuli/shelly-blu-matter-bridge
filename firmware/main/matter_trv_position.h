// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/position_observation.hpp"
#include "bridge/trv_firmware.hpp"
#include "bridge/trv_sensor_sample.hpp"
#include "bridge/trv_setpoint.hpp"
#include <cstddef>
#include <esp_err.h>

void initialize_position_bindings();
void publish_trv_positions();
void observe_trv_firmware(size_t slot, const bridge::EnrollmentId &id, const bridge::TrvFirmware &value);
void show_trv_positions(uint64_t now_ms);
void observe_trv_position(size_t slot, const bridge::EnrollmentId &source, uint8_t percent, uint64_t now_ms);
void record_trv_sensor_read(size_t slot, const bridge::EnrollmentId &source,
                            std::optional<bridge::TrvSensorSample> sample);
void expire_trv_position_sample(size_t slot, const bridge::EnrollmentId &id);
void invalidate_trv_position(size_t slot);
esp_err_t remove_trv_endpoint(size_t slot, const bridge::EnrollmentId &id);
void finish_matter_position(size_t slot, const bridge::EnrollmentId &id, int result);
void observe_trv_climate(size_t slot, const bridge::EnrollmentId &id,
                         std::optional<bridge::TrvClimate> state);
