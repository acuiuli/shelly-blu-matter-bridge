// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bridge {
enum class DiscoveryParse { not_shelly, valid, malformed, unsupported_block };
struct ShellyAdvertisement {
    DiscoveryParse status = DiscoveryParse::not_shelly;
    std::optional<uint16_t> model;
    std::optional<uint16_t> flags;
    bool is_trv() const { return status == DiscoveryParse::valid && model == 0x0008; }
};

// Parse advertising/scan-response AD structures, not device names or BTHome
// measurements. Manufacturer block definitions come from Shelly's public API.
// No address, key or payload is exposed through this result.
ShellyAdvertisement decode_shelly_advertisement(const uint8_t *data, size_t size);
} // namespace bridge
