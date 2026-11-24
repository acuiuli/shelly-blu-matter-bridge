// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bridge {
enum class TrvReadField : uint8_t { target, position, calibration, functions, errors, count };
// Target temperature is returned in centidegrees Celsius, position in percent,
// and status bitmasks unchanged. Confirming a pending command additionally needs
// transport freshness and intent correlation in the reconciliation layer.
std::optional<int> decode_trv_readback(TrvReadField field, const uint8_t *data, size_t size);
} // namespace bridge
