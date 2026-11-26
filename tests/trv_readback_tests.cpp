// SPDX-License-Identifier: Apache-2.0
#include "bridge/position_observation.hpp"
#include "bridge/trv_position_write.hpp"
#include "bridge/trv_readback.hpp"
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
    using Decision = PositionDecision;
    const std::array<std::optional<int>, size_t(TrvReadField::count)> live{400, 18, 1, 160, 0};
    TrvPositionWrite close{18, 0, 15000};
    CHECK(close.take(live, 14999) == Decision::write);
    CHECK(close.take(live, 14999) == Decision::reject);
    const TrvPositionWrite request{18, 20, 15000};
    auto decide = [request](auto fields, uint64_t when = 1) {
        auto once = request;
        return once.take(fields, when);
    };
    CHECK(decide(live, 15000) == Decision::reject);
    CHECK(decide(live, 0) == Decision::write);
    TrvPositionWrite noop{18, 18, 15000};
    CHECK(noop.take(live, 1) == Decision::already_at_target);
    CHECK(noop.take(live, 1) == Decision::reject);
    for (const int invalid_target : {-1, 101}) {
        TrvPositionWrite malformed{18, invalid_target, 15000};
        CHECK(malformed.take(live, 1) == Decision::reject);
    }
    for (auto field :
         {TrvReadField::position, TrvReadField::functions, TrvReadField::calibration, TrvReadField::errors}) {
        auto fields = live;
        fields[size_t(field)].reset();
        CHECK(decide(fields) == Decision::reject);
        if (field == TrvReadField::functions)
            fields[size_t(field)] = 161;
        else if (field == TrvReadField::errors)
            fields[size_t(field)] = 1;
        else
            fields[size_t(field)] = 0;
        CHECK(decide(fields) == Decision::reject);
    }
    EnrollmentId valve{1}, other{2};
    PositionObservation reading{};
    CHECK(!reading.current(valve, 20, 100));
    reading = {valve, 20, 0};
    CHECK(reading.current(valve, 20, 100) == 0);
    CHECK(!reading.current(other, 20, 100));
    CHECK(!reading.current(valve, 19, 100));
    CHECK(reading.current(valve, 119, 100) == 0);
    CHECK(!reading.current(valve, 120, 100));
    reading = {valve, 120, 50};
    CHECK(reading.current(valve, 125, 100) == 50);
    reading.position = 101;
    CHECK(!reading.current(valve, 125, 100));
    const uint8_t minimum[] = {40, 0}, normal[] = {210, 0}, maximum[] = {44, 1};
    CHECK(decode_trv_readback(TrvReadField::target, minimum, 2) == 400);
    CHECK(decode_trv_readback(TrvReadField::target, normal, 2) == 2100);
    CHECK(decode_trv_readback(TrvReadField::target, maximum, 2) == 3000);
    const uint8_t wrong_units[] = {0x34, 0x08}, below[] = {39, 0}, negative[] = {0xff, 0xff};
    CHECK(!decode_trv_readback(TrvReadField::target, wrong_units, 2));
    CHECK(!decode_trv_readback(TrvReadField::target, below, 2));
    CHECK(!decode_trv_readback(TrvReadField::target, negative, 2));
    CHECK(!decode_trv_readback(TrvReadField::target, normal, 1));
    const uint8_t closed[] = {0}, open[] = {100}, invalid[] = {101}, calibration[] = {1};
    CHECK(decode_trv_readback(TrvReadField::position, closed, 1) == 0);
    CHECK(decode_trv_readback(TrvReadField::position, open, 1) == 100);
    CHECK(!decode_trv_readback(TrvReadField::position, invalid, 1));
    CHECK(!decode_trv_readback(TrvReadField::position, open, 2));
    CHECK(decode_trv_readback(TrvReadField::calibration, calibration, 1) == 1);
    CHECK(!decode_trv_readback(TrvReadField::calibration, invalid, 1));
    const uint8_t flags[] = {0xa4, 0x91};
    CHECK(decode_trv_readback(TrvReadField::functions, flags, 2) == 0x91a4);
    CHECK(decode_trv_readback(TrvReadField::errors, invalid, 1) == 101);
    CHECK(!decode_trv_readback(TrvReadField::errors, nullptr, 1));
    CHECK(!decode_trv_readback(TrvReadField::count, closed, 1));
    std::cout << "TRV readback length, units, range and flag preservation checks passed\n";
}
