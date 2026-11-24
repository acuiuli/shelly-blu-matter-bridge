// SPDX-License-Identifier: Apache-2.0
#include "bridge/trv_readback.hpp"

namespace bridge {
std::optional<int> decode_trv_readback(TrvReadField field, const uint8_t *data, size_t size) {
    if (!data || field >= TrvReadField::count)
        return std::nullopt;
    size_t expected = field == TrvReadField::target || field == TrvReadField::functions ? 2 : 1;
    if (size != expected)
        return std::nullopt;
    int value = data[0];
    if (size == 2)
        value |= int(data[1]) << 8;
    switch (field) {
    case TrvReadField::target:
        if (value < 40 || value > 300)
            return std::nullopt;
        return value * 10; // vendor deci-Celsius to Matter centi-Celsius
    case TrvReadField::position:
        if (value > 100)
            return std::nullopt;
        break;
    case TrvReadField::calibration:
        if (value > 1)
            return std::nullopt;
        break;
    default:
        break; // Preserve status bits; mapping is a separate semantic decision.
    }
    return value;
}
} // namespace bridge
