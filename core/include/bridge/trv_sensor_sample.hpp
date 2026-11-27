// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bridge {
struct TrvSensorSample {
    uint8_t packet_id;
    uint8_t battery_percent;
    int16_t measured_centi_c;
};

// Shelly Sample BTHome Data characteristic: v2 device-info byte followed by objects.
std::optional<TrvSensorSample> decode_trv_sensor_sample(const uint8_t *data, size_t length);

struct TrvSensorObservation {
    std::optional<TrvSensorSample> sample;
    uint64_t observed_ms = 0;
    void observe(TrvSensorSample next, uint64_t now) {
        if (!sample || next.packet_id != sample->packet_id) {
            sample = next;
            observed_ms = now;
        }
    }
    bool fresh(uint64_t now, uint64_t lifetime) const {
        return sample && now >= observed_ms && now - observed_ms < lifetime;
    }
};
} // namespace bridge
