// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/trv_readback.hpp"
#include <array>
#include <cstdint>

namespace bridge {
// A position request is consumed immediately before submission on the BLE host task.
// Only complete, decoded fields from the current encrypted connection are used.
enum class PositionDecision { reject, already_at_target, write };
enum class PositionPrecondition { match_expected, match_expected_or_target };
struct TrvPositionWrite {
    int expected = -1;
    int target = -1;
    uint64_t expires_ms = 0;
    PositionPrecondition precondition = PositionPrecondition::match_expected;

    PositionDecision take(const std::array<std::optional<int>, size_t(TrvReadField::count)> &values,
                          uint64_t now_ms) {
        const uint64_t deadline = expires_ms;
        expires_ms = 0;
        const auto position = values[size_t(TrvReadField::position)];
        const auto functions = values[size_t(TrvReadField::functions)];
        if (now_ms >= deadline || expected < 0 || expected > 100 || target < 0 || target > 100 || !position ||
            !functions || (*functions & 1) || values[size_t(TrvReadField::calibration)] != 1 ||
            values[size_t(TrvReadField::errors)] != 0)
            return PositionDecision::reject;
        // An absolute Matter command may confirm a target already reached by the valve.
        // A differing live value still needs to match the expected start before any write.
        if (position != expected &&
            !(precondition == PositionPrecondition::match_expected_or_target && position == target))
            return PositionDecision::reject;
        return position == target ? PositionDecision::already_at_target : PositionDecision::write;
    }
};
} // namespace bridge
