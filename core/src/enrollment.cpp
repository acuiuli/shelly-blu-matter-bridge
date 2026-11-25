// SPDX-License-Identifier: Apache-2.0
#include "bridge/enrollment.hpp"
#include <algorithm>

namespace bridge {
namespace {
template <size_t N> bool zero(const std::array<uint8_t, N> &value) {
    return std::all_of(value.begin(), value.end(), [](uint8_t b) { return b == 0; });
}
bool same_peer(const TrvBinding &a, const TrvBinding &b) {
    return a.address_type == b.address_type && a.address == b.address;
}
} // namespace
bool Enrollment::valid() const {
    for (size_t i = 0; i < capacity; ++i) {
        const auto &b = slots[i];
        if (b.state == EnrollmentState::empty) {
            if (b.address_type || !zero(b.address) || !zero(b.identity))
                return false;
            continue;
        }
        if (b.state != EnrollmentState::pending && b.state != EnrollmentState::verified)
            return false;
        if (b.address_type > 1 || zero(b.address) || zero(b.identity) ||
            (b.address_type == 1 && (b.address[5] & 0xc0) != 0xc0))
            return false; // rotating private addresses cannot be durable identities
        for (size_t j = 0; j < i; ++j)
            if (slots[j].state != EnrollmentState::empty &&
                (same_peer(b, slots[j]) || b.identity == slots[j].identity))
                return false;
    }
    return true;
}
bool Enrollment::reserve(size_t slot, const TrvBinding &binding) {
    if (slot >= capacity || binding.state != EnrollmentState::pending || !valid())
        return false;
    auto candidate = *this;
    if (candidate.slots[slot].state != EnrollmentState::empty)
        return false;
    candidate.slots[slot] = binding;
    if (!candidate.valid())
        return false;
    *this = candidate;
    return true;
}
bool Enrollment::begin_repair(size_t slot, uint8_t type, const std::array<uint8_t, 6> &selected) {
    if (slot >= capacity || !valid())
        return false;
    auto &binding = slots[slot];
    if (binding.state == EnrollmentState::empty || binding.address_type != type ||
        binding.address != selected)
        return false;
    binding.state = EnrollmentState::pending;
    return true;
}
std::array<uint8_t, Enrollment::encoded_size> Enrollment::encode() const {
    std::array<uint8_t, encoded_size> out{};
    out[0] = 'T';
    out[1] = 'R';
    out[2] = 'V';
    out[3] = 'B';
    out[4] = 1;
    out[5] = capacity;
    for (size_t i = 0; i < capacity; ++i) {
        auto *p = out.data() + 8 + 24 * i;
        const auto &b = slots[i];
        p[0] = uint8_t(b.state);
        p[1] = b.address_type;
        std::copy(b.address.begin(), b.address.end(), p + 2);
        std::copy(b.identity.begin(), b.identity.end(), p + 8);
    }
    return out;
}
std::optional<Enrollment> Enrollment::decode(const uint8_t *data, size_t size) {
    if (!data || size != encoded_size || data[0] != 'T' || data[1] != 'R' || data[2] != 'V' ||
        data[3] != 'B' || data[4] != 1 || data[5] != capacity || data[6] || data[7])
        return std::nullopt;
    Enrollment out;
    for (size_t i = 0; i < capacity; ++i) {
        const auto *p = data + 8 + 24 * i;
        auto &b = out.slots[i];
        b.state = EnrollmentState(p[0]);
        b.address_type = p[1];
        std::copy(p + 2, p + 8, b.address.begin());
        std::copy(p + 8, p + 24, b.identity.begin());
    }
    return out.valid() ? std::optional<Enrollment>(out) : std::nullopt;
}
} // namespace bridge
