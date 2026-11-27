// SPDX-License-Identifier: Apache-2.0
#include "bridge/trv_sensor_sample.hpp"

namespace bridge {
std::optional<TrvSensorSample> decode_trv_sensor_sample(const uint8_t *data, size_t length) {
    if (!data || length < 2 || (data[0] != 0x40 && data[0] != 0x44))
        return std::nullopt;
    std::optional<uint8_t> packet_id, battery;
    std::optional<int16_t> measured;
    unsigned temperatures = 0;
    for (size_t pos = 1; pos < length;) {
        const auto id = data[pos++];
        if (pos == length)
            return std::nullopt;
        const auto value = data[pos++];
        switch (id) {
        case 0x00:
            if (packet_id)
                return std::nullopt;
            packet_id = value;
            break;
        case 0x01:
            if (battery || value > 100)
                return std::nullopt;
            battery = value;
            break;
        case 0x45: {
            if (pos == length || ++temperatures > 3)
                return std::nullopt;
            const int raw = value | (int(data[pos++]) << 8);
            // Shelly encodes target, measured, then optional external temperature.
            if (temperatures == 2) {
                const int centi = (raw < 0x8000 ? raw : raw - 0x10000) * 10;
                if (centi < -27315 || centi > INT16_MAX)
                    return std::nullopt;
                measured = static_cast<int16_t>(centi);
            }
            break;
        }
        case 0x3a: // Button event, one byte.
            break;
        case 0x54: // Status counter, encoded as a one-byte raw object.
            if (value != 1 || pos == length)
                return std::nullopt;
            ++pos;
            break;
        default:
            return std::nullopt; // Object lengths are not self-describing.
        }
    }
    if (!packet_id || !battery || !measured)
        return std::nullopt;
    return TrvSensorSample{*packet_id, *battery, *measured};
}
} // namespace bridge
