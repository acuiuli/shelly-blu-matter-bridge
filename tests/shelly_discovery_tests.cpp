// SPDX-License-Identifier: Apache-2.0
#include "bridge/shelly_discovery.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __LINE__ << ": " #expr "\n";                                                        \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
using namespace bridge;
int main() {
    // Synthetic frames, never captures or identities from an installed device.
    std::vector<uint8_t> trv{9, 0xff, 0xa9, 0x0b, 0x01, 0x16, 0x00, 0x0b, 0x08, 0x00};
    auto parsed = decode_shelly_advertisement(trv.data(), trv.size());
    CHECK(parsed.is_trv() && parsed.flags == 0x0016);
    CHECK(!decode_shelly_advertisement(nullptr, 0).is_trv());
    CHECK(decode_shelly_advertisement(nullptr, 1).status == DiscoveryParse::malformed);
    for (size_t length = 1; length < trv.size(); ++length) {
        parsed = decode_shelly_advertisement(trv.data(), length);
        CHECK(parsed.status == DiscoveryParse::malformed && !parsed.model && !parsed.flags);
    }
    auto other = trv;
    other[2] = 0xa8;
    CHECK(decode_shelly_advertisement(other.data(), other.size()).status == DiscoveryParse::not_shelly);
    other = trv;
    other[8] = 3;
    parsed = decode_shelly_advertisement(other.data(), other.size());
    CHECK(parsed.status == DiscoveryParse::valid && !parsed.is_trv());
    // Names containing TRV are not an identity proof.
    const uint8_t name[] = {4, 0x09, 'T', 'R', 'V'};
    CHECK(!decode_shelly_advertisement(name, sizeof(name)).is_trv());
    auto combined = std::vector<uint8_t>{2, 0x01, 0x06};
    combined.insert(combined.end(), trv.begin(), trv.end());
    CHECK(decode_shelly_advertisement(combined.data(), combined.size()).is_trv());
    combined.insert(combined.end(), {0, 0});
    CHECK(decode_shelly_advertisement(combined.data(), combined.size()).is_trv());
    combined.push_back(1);
    CHECK(decode_shelly_advertisement(combined.data(), combined.size()).status == DiscoveryParse::malformed);
    auto unknown = trv;
    unknown[0] += 2;
    unknown.insert(unknown.end(), {0x55, 0});
    parsed = decode_shelly_advertisement(unknown.data(), unknown.size());
    CHECK(parsed.status == DiscoveryParse::unsupported_block && !parsed.model && !parsed.flags);
    auto duplicate = trv;
    duplicate[0] += 3;
    duplicate.insert(duplicate.end(), {0x0b, 8, 0});
    CHECK(decode_shelly_advertisement(duplicate.data(), duplicate.size()).status ==
          DiscoveryParse::malformed);
    auto address = trv;
    address[0] += 7;
    address.insert(address.end(), {0x0a, 1, 2, 3, 4, 5, 6});
    CHECK(decode_shelly_advertisement(address.data(), address.size()).is_trv());
    address.pop_back();
    address[0]--;
    CHECK(decode_shelly_advertisement(address.data(), address.size()).status == DiscoveryParse::malformed);
    const uint8_t model_only[] = {6, 0xff, 0xa9, 0x0b, 0x0b, 8, 0};
    parsed = decode_shelly_advertisement(model_only, sizeof(model_only));
    CHECK(parsed.is_trv() && !parsed.flags); // absent flags are unknown, never false
    std::cout << "Shelly discovery identity, bounds, ambiguity and privacy checks passed\n";
}
