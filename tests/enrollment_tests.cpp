// SPDX-License-Identifier: Apache-2.0
#include "bridge/enrollment.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __LINE__ << ": " #expr "\n";                                                        \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
using namespace bridge;
int main() {
    Enrollment table;
    CHECK(table.valid());
    auto empty = table.encode();
    CHECK(Enrollment::decode(empty.data(), empty.size()));
    TrvBinding a{EnrollmentState::pending, 0, {1, 2, 3, 4, 5, 6}, {1}};
    CHECK(table.reserve(0, a));
    CHECK(!table.reserve(1, a)); // one physical valve cannot occupy two slots
    auto b = a;
    b.address[0] = 2;
    CHECK(!table.reserve(1, b)); // enrollment identity must also be unique
    b.identity[0] = 2;
    CHECK(!table.reserve(0, b)); // no implicit identity reuse
    CHECK(table.reserve(1, b));
    CHECK(!table.reserve(4, a));
    auto saved = table.encode();
    auto restored = Enrollment::decode(saved.data(), saved.size());
    CHECK(restored && restored->slots[0].state == EnrollmentState::pending);
    CHECK(restored->slots[0].identity == a.identity);
    restored->slots[0].state = EnrollmentState::verified;
    auto verified = restored->encode();
    CHECK(Enrollment::decode(verified.data(), verified.size())->slots[0].state == EnrollmentState::verified);
    CHECK(!Enrollment::decode(saved.data(), saved.size() - 1));
    CHECK(!Enrollment::decode(nullptr, saved.size()));
    for (auto offset : {0, 4, 5, 6, 7, 8}) {
        auto corrupt = saved;
        corrupt[size_t(offset)] = 255;
        CHECK(!Enrollment::decode(corrupt.data(), corrupt.size()));
    }
    auto private_address = a;
    private_address.address_type = 1;
    Enrollment rotating;
    CHECK(!rotating.reserve(0, private_address));
    private_address.address[5] = 0xc1;
    CHECK(rotating.reserve(0, private_address));
    for (unsigned flags = 0; flags <= 65535; ++flags) {
        auto result = valve_only_functions(uint16_t(flags));
        CHECK(valve_only_enabled(result));
        CHECK((result ^ uint16_t(flags)) == (flags & 1));
        CHECK(valve_only_functions(result) == result);
    }
    std::cout << "Enrollment identity, storage validation and valve-only bit preservation passed\n";
}
