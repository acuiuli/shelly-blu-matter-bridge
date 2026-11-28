// SPDX-License-Identifier: Apache-2.0
#include "bridge/position_observation.hpp"
#include "bridge/trv_firmware.hpp"
#include "bridge/trv_position_write.hpp"
#include "bridge/trv_readback.hpp"
#include "bridge/trv_sensor_sample.hpp"
#include "bridge/trv_setpoint.hpp"
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
    TrvFirmwareRead fw;
    const uint8_t prefix[] = {'v', '1', '.'}, suffix[] = {'5', '.', '0'};
    CHECK(fw.append(0, prefix, 3));
    CHECK(!fw.append(0, prefix, 3) && !fw.append(4, suffix, 3));
    const uint8_t nul[] = {0};
    CHECK(!fw.append(3, nul, 1));
    CHECK(fw.append(3, suffix, 3) && std::string(fw.value.data()) == "v1.5.0");
    std::array<uint8_t, 59> tail{};
    tail.fill('a');
    CHECK(!fw.append(6, tail.data(), 59));
    CHECK(fw.append(6, tail.data(), 58) && fw.value.back() == 0 && fw.length == 64);
    const TrvClimate manual{2000, 160, 500, 2900};
    const TrvClimate automatic{2000, 161, 500, 2900};
    CHECK(manual.accepts(500) && manual.accepts(2900));
    CHECK(!manual.accepts(490) && !manual.accepts(2910) && !manual.accepts(2055));
    TrvSetpointWrite setpoint{2000, 2050, 160, 100};
    CHECK(setpoint.take(manual, 99) == SetpointDecision::write);
    CHECK(setpoint.take(manual, 99) == SetpointDecision::reject);
    CHECK(!setpoint.confirmed(manual));
    auto changed_target = manual;
    changed_target.target = 2050;
    CHECK(setpoint.confirmed(changed_target));
    changed_target.functions ^= 2;
    CHECK(!setpoint.confirmed(changed_target));
    for (const auto &request :
         {TrvSetpointWrite{1950, 2050, 160, 100}, TrvSetpointWrite{2000, 2050, 161, 100},
          TrvSetpointWrite{2000, 499, 160, 100}, TrvSetpointWrite{2000, 2050, 160, 99}}) {
        auto attempt = request;
        CHECK(attempt.take(manual, 99) == SetpointDecision::reject);
    }
    TrvSetpointWrite already{1950, 2000, 160, 100};
    CHECK(already.take(manual, 90) == SetpointDecision::verify);
    TrvSetpointWrite heat{2000, 2150, 161, 100};
    CHECK(heat.take(automatic, 90) == SetpointDecision::write);
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
    // HA last saw 20%. An independent change can put the valve at 0% before Close arrives.
    auto preflight = live;
    preflight[size_t(TrvReadField::position)] = 0;
    const TrvPositionWrite absolute_close{20, 0, 15000, PositionPrecondition::match_expected_or_target};
    auto once = absolute_close;
    CHECK(once.take(preflight, 7500) == Decision::already_at_target);
    CHECK(once.take(preflight, 7501) == Decision::reject);
    once = absolute_close;
    CHECK(once.take(live, 1) == Decision::reject); // 18% is neither the target nor the expected start
    preflight[size_t(TrvReadField::position)] = 20;
    TrvPositionWrite absolute_open{0, 20, 15000, PositionPrecondition::match_expected_or_target};
    CHECK(absolute_open.take(preflight, 1) == Decision::already_at_target);
    once = absolute_close;
    CHECK(once.take(preflight, 1) == Decision::write);
    preflight[size_t(TrvReadField::position)] = 0;
    TrvPositionWrite strict_close{20, 0, 15000};
    CHECK(strict_close.take(preflight, 1) == Decision::reject);
    once = absolute_close;
    CHECK(once.take(preflight, 15000) == Decision::reject);
    for (auto field :
         {TrvReadField::position, TrvReadField::functions, TrvReadField::calibration, TrvReadField::errors}) {
        auto unhealthy = preflight;
        unhealthy[size_t(field)].reset();
        once = absolute_close;
        CHECK(once.take(unhealthy, 1) == Decision::reject);
    }
    for (auto field : {TrvReadField::functions, TrvReadField::calibration, TrvReadField::errors}) {
        auto unhealthy = preflight;
        unhealthy[size_t(field)] = field == TrvReadField::calibration ? 0 : 1;
        once = absolute_close;
        CHECK(once.take(unhealthy, 1) == Decision::reject);
    }
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
    // Synthetic BTHome v2 GATT sample: 21 C target, -0.5 C measured, 17 C external.
    std::vector<uint8_t> frame{0x44, 0, 255, 1, 0, 0x45, 210, 0, 0x45, 0xfb, 0xff, 0x45, 170, 0, 0x54, 1, 7};
    auto decode = [](const auto &v) { return decode_trv_sensor_sample(v.data(), v.size()); };
    auto sensor = decode(frame);
    CHECK(sensor && sensor->packet_id == 255 && sensor->battery_percent == 0 &&
          sensor->measured_centi_c == -50);
    auto damaged = frame;
    damaged[0] |= 1; // encrypted BTHome body
    CHECK(!decode(damaged));
    damaged = frame;
    damaged[4] = 101;
    CHECK(!decode(damaged));
    damaged = frame;
    damaged[9] = 0xff;
    damaged[10] = 0x7f;
    CHECK(!decode(damaged));
    for (size_t short_size : {0, 1, 4, 7, 8, 10, 16})
        CHECK(!decode_trv_sensor_sample(frame.data(), short_size));
    for (uint8_t extra : {0x00, 0x01, 0x45, 0xaa}) {
        damaged = frame;
        damaged.insert(damaged.end(), {extra, 0, 0});
        CHECK(!decode(damaged));
    }
    damaged = frame;
    damaged[15] = 4;
    CHECK(!decode(damaged));
    frame.resize(11); // no optional external temperature or status counter
    frame[0] = 0x40;
    frame[2] = 0;
    frame[4] = 100;
    frame[9] = frame[10] = 0;
    auto zero = decode(frame);
    CHECK(zero && zero->measured_centi_c == 0 && zero->battery_percent == 100);
    TrvSensorObservation last;
    CHECK(!last.fresh(200, 10));
    last.observe(*sensor, 200);
    last.observe(*sensor, 208); // a repeated characteristic value cannot renew freshness
    CHECK(last.fresh(209, 10) && !last.fresh(210, 10));
    last.observe(*zero, 212); // packet ID wraps from 255 to zero
    CHECK(last.fresh(212, 10) && last.sample->battery_percent == 100);
    std::cout << "TRV readback, BTHome sensor samples, freshness and flag checks passed\n";
}
