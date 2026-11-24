// SPDX-License-Identifier: Apache-2.0
#include "bridge/shelly_discovery.hpp"

namespace bridge {
ShellyAdvertisement decode_shelly_advertisement(const uint8_t *data, size_t size) {
    ShellyAdvertisement result;
    auto error = [](DiscoveryParse status) {
        ShellyAdvertisement rejected;
        rejected.status = status;
        return rejected;
    };
    if (!data && size)
        return error(DiscoveryParse::malformed);
    size_t offset = 0;
    while (offset < size) {
        size_t length = data[offset++];
        if (!length) {
            while (offset < size)
                if (data[offset++] != 0)
                    return error(DiscoveryParse::malformed);
            break;
        }
        if (length > size - offset)
            return error(DiscoveryParse::malformed);
        size_t end = offset + length;
        // Manufacturer data type 0xff; Shelly company id 0x0ba9, little endian.
        if (data[offset] != 0xff || length < 3 || data[offset + 1] != 0xa9 || data[offset + 2] != 0x0b) {
            offset = end;
            continue;
        }
        result.status = DiscoveryParse::valid;
        offset += 3;
        while (offset < end) {
            uint8_t block = data[offset++];
            size_t bytes;
            switch (block) {
            case 0x01: // feature flags
            case 0x0b: // model id
                bytes = 2;
                break;
            case 0x0a: // identity address: deliberately not returned or logged
                bytes = 6;
                break;
            default:
                // Blocks have no length prefix. Never guess how to skip one.
                return error(DiscoveryParse::unsupported_block);
            }
            if (bytes > end - offset)
                return error(DiscoveryParse::malformed);
            if (block == 0x01 || block == 0x0b) {
                auto &field = block == 0x01 ? result.flags : result.model;
                if (field)
                    return error(DiscoveryParse::malformed);
                field = uint16_t(data[offset]) | (uint16_t(data[offset + 1]) << 8);
            }
            offset += bytes;
        }
    }
    return result;
}
} // namespace bridge
