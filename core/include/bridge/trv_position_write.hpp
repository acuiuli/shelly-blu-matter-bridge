// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/trv_readback.hpp"
#include <array>
#include <cstdint>

namespace bridge {
// A position request is consumed immediately before submission on the BLE host task.
// Only complete, decoded fields from the current encrypted connection are used.
enum class PositionDecision { reject, already_at_target, write };
struct TrvPositionWrite {
    int expected = -1;
    int target = -1;
    uint64_t expires_ms = 0;

    PositionDecision take(const std::array<std::optional<int>, size_t(TrvReadField::count)> &values,
                          uint64_t now_ms) {
        const uint64_t deadline = expires_ms;
        expires_ms = 0;
        auto functions = values[size_t(TrvReadField::functions)];
        if (now_ms >= deadline || expected < 0 || expected > 100 || target < 0 || target > 100 ||
            values[size_t(TrvReadField::position)] != expected || !functions || (*functions & 1) ||
            values[size_t(TrvReadField::calibration)] != 1 || values[size_t(TrvReadField::errors)] != 0)
            return PositionDecision::reject;
        return expected == target ? PositionDecision::already_at_target : PositionDecision::write;
    }
};
} // namespace bridge
