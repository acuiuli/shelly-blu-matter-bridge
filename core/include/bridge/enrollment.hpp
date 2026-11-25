// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bridge {
// Private identity bindings, separate from both Matter fabrics and BLE keys.
// Pending reservations survive failed pairing; slots are never silently reused.
enum class EnrollmentState : uint8_t { empty, pending, verified };
struct TrvBinding {
    EnrollmentState state = EnrollmentState::empty;
    uint8_t address_type = 0;
    std::array<uint8_t, 6> address{};   // BLE little-endian public/static identity
    std::array<uint8_t, 16> identity{}; // unique to this physical enrollment
};
struct Enrollment {
    static constexpr size_t capacity = 4;
    static constexpr size_t encoded_size = 8 + capacity * 24;
    std::array<TrvBinding, capacity> slots{};
    bool valid() const;
    bool reserve(size_t slot, const TrvBinding &binding);
    bool begin_repair(size_t slot, uint8_t type, const std::array<uint8_t, 6> &selected);
    std::array<uint8_t, encoded_size> encode() const;
    static std::optional<Enrollment> decode(const uint8_t *data, size_t size);
};

// The Shelly flag is configuration, not a side effect of a position command.
// Preserve every other bit, including unknown future functions.
constexpr uint16_t valve_only_functions(uint16_t current) { return current & uint16_t(~uint16_t(1)); }
constexpr bool valve_only_enabled(uint16_t current) { return !(current & 1); }
} // namespace bridge
