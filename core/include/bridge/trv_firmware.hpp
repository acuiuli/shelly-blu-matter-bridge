// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace bridge {
// Matter SoftwareVersionString accepts 1..64 bytes. Keep the complete Shelly build ID.
using TrvFirmware = std::array<char, 65>;
struct TrvFirmwareRead {
    TrvFirmware value{};
    size_t length = 0;
    bool append(size_t offset, const uint8_t *bytes, size_t count) {
        if (offset != length || count > value.size() - 1 - length ||
            !std::all_of(bytes, bytes + count, [](uint8_t c) { return c >= 0x21 && c <= 0x7e; }))
            return false;
        std::copy_n(bytes, count, value.data() + length);
        length += count;
        return true;
    }
};
} // namespace bridge
