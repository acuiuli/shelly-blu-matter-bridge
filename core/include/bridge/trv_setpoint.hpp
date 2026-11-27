// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <optional>

namespace bridge {
// Matter temperatures are in hundredths of a degree. The valve accepts tenths.
struct TrvClimate {
    static constexpr int absolute_min = 400, absolute_max = 3000;
    int target;
    uint16_t functions;
    int minimum;
    int maximum;
    bool accepts(int value) const {
        return minimum >= absolute_min && maximum <= absolute_max && minimum < maximum && value >= minimum &&
               value <= maximum && value % 10 == 0;
    }
};

enum class SetpointDecision { reject, verify, write };
struct TrvSetpointWrite {
    int expected = -1;
    int target = -1;
    uint16_t functions = 0;
    uint64_t deadline = 0;
    SetpointDecision take(const TrvClimate &live, uint64_t now) {
        const auto expires = deadline;
        deadline = 0;
        if (now >= expires || functions != live.functions || !live.accepts(target))
            return SetpointDecision::reject;
        if (live.target == target)
            return SetpointDecision::verify;
        return live.target == expected ? SetpointDecision::write : SetpointDecision::reject;
    }
    bool confirmed(const TrvClimate &live) const {
        return live.target == target && live.functions == functions;
    }
};
} // namespace bridge
