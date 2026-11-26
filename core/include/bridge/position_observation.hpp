// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace bridge {
using EnrollmentId = std::array<uint8_t, 16>;
struct PositionObservation {
    EnrollmentId source{};
    uint64_t observed_ms = 0;
    std::optional<uint8_t> position;
    std::optional<uint8_t> current(const EnrollmentId &expected, uint64_t now, uint64_t lifetime) const {
        if (!position || *position > 100 || source != expected || now < observed_ms ||
            now - observed_ms >= lifetime)
            return std::nullopt;
        return position;
    }
};
} // namespace bridge
