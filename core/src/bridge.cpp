// SPDX-License-Identifier: Apache-2.0
#include "bridge/bridge.hpp"
#include <algorithm>
#include <cstdio>

namespace bridge {
namespace {
bool valid(Field f, int v) {
    switch (f) {
    case Field::setpoint:
        return v >= 400 && v <= 3000 && v % 10 == 0;
    case Field::position:
        return v >= 0 && v <= 100;
    case Field::mode:
        return v == 0 || v == 4;
    default:
        return false;
    }
}
bool pending(const Command &c) { return c.outcome == Outcome::queued || c.outcome == Outcome::sent; }
int read(const Telemetry &t, Field f) {
    switch (f) {
    case Field::setpoint:
        return t.setpoint;
    case Field::position:
        return t.position;
    default:
        return t.mode;
    }
}
void write(Telemetry &t, Field f, int v) {
    switch (f) {
    case Field::setpoint:
        t.setpoint = v;
        break;
    case Field::position:
        t.position = v;
        break;
    default:
        t.mode = v;
        break;
    }
}
} // namespace
bool Engine::request(size_t d, Field f, int v, Time now) {
    if (d >= device_count || !valid(f, v))
        return false;
    auto &c = devices_[d].commands[static_cast<size_t>(f)];
    c = {next_token_++, f, v, now + command_lifetime, now, 0, Outcome::queued};
    return true;
}
std::optional<Command> Engine::take(size_t d, Time now) {
    if (d >= device_count)
        return std::nullopt;
    tick(now);
    auto &s = devices_[d];
    // One outstanding transport transaction per valve. Other valves are independent.
    for (const auto &c : s.commands)
        if (c.outcome == Outcome::sent)
            return std::nullopt;
    for (auto &c : s.commands) {
        if (c.outcome == Outcome::queued && now >= c.next_attempt) {
            c.outcome = Outcome::sent;
            ++c.attempts;
            return c;
        }
    }
    return std::nullopt;
}
bool Engine::current(size_t d, uint64_t token, Time now) const {
    if (d >= device_count)
        return false;
    for (const auto &c : devices_[d].commands)
        if (c.token == token && pending(c) && now < c.expires)
            return true;
    return false;
}
void Engine::failure(size_t d, uint64_t token, Time now) {
    if (d >= device_count)
        return;
    for (auto &c : devices_[d].commands) {
        if (c.token != token || c.outcome != Outcome::sent)
            continue;
        c.outcome = now >= c.expires             ? Outcome::expired
                    : c.attempts >= max_attempts ? Outcome::failed
                                                 : Outcome::queued;
        c.next_attempt = now + (500ULL << c.attempts);
    }
}
bool Engine::observe(size_t d, const Telemetry &t, Time now, std::optional<Field> manual) {
    if (d >= device_count || t.temperature < -4000 || t.temperature > 8500 ||
        !valid(Field::setpoint, t.setpoint) || !valid(Field::position, t.position) ||
        !valid(Field::mode, t.mode) || t.battery < 0 || t.battery > 100 ||
        (manual && *manual >= Field::count))
        return false;
    auto &s = devices_[d];
    if (s.reported && now < s.last_seen)
        return false;
    tick(now);
    s.reported = t;
    s.last_seen = now;
    s.reachable = true;
    for (auto &c : s.commands) {
        if (!pending(c))
            continue;
        if (manual && c.field == *manual)
            c.outcome = Outcome::superseded;
        else if (c.outcome == Outcome::sent && read(t, c.field) == c.value)
            c.outcome = Outcome::confirmed;
    }
    return true;
}
void Engine::tick(Time now) {
    for (auto &s : devices_) {
        if (s.reported && now >= s.last_seen && now - s.last_seen >= stale_after)
            s.reachable = false;
        for (auto &c : s.commands) {
            if (pending(c) && now >= c.expires)
                c.outcome = Outcome::expired;
        }
    }
}
void Simulator::step(Engine &e, Time now) {
    e.tick(now);
    for (size_t d = 0; d < device_count; ++d) {
        auto &flight = flights_[d];
        auto &f = faults[d];
        if (flight && !e.current(d, flight->command.token, now))
            flight.reset();
        if (flight && now >= flight->due) {
            auto c = flight->command;
            flight.reset();
            if (!f.available || f.failures > 0) {
                if (f.failures)
                    --f.failures;
                e.failure(d, c.token, now);
            } else if (!(f.hold_position && c.field == Field::position)) {
                write(values_[d], c.field, c.value);
            }
        }
        if (f.available)
            e.observe(d, values_[d], now);
        if (!flight) {
            if (auto c = e.take(d, now))
                flight = Flight{*c, now + f.delay};
        }
    }
}
bool Simulator::manual(Engine &e, size_t d, Field f, int v, Time now) {
    if (d >= device_count || !valid(f, v))
        return false;
    write(values_[d], f, v);
    return e.observe(d, values_[d], now, f);
}
bool Identity::valid() const {
    return schema == 1 && std::any_of(gateway.begin(), gateway.end(), [](auto b) { return b != 0; });
}
uint16_t Identity::thermostat_endpoint(size_t slot) const {
    return slot < device_count ? static_cast<uint16_t>(2 + slot * 2) : 0xFFFF;
}
uint16_t Identity::valve_endpoint(size_t slot) const {
    return slot < device_count ? static_cast<uint16_t>(3 + slot * 2) : 0xFFFF;
}
std::array<char, 33> Identity::unique_id(size_t slot) const {
    std::array<char, 33> out{};
    if (!valid() || slot >= device_count)
        return out;
    // Matter UniqueID is at most 32 characters: 120 gateway bits plus slot byte.
    for (size_t i = 0; i < gateway.size() - 1; ++i)
        std::snprintf(out.data() + i * 2, 3, "%02x", gateway[i]);
    std::snprintf(out.data() + 30, 3, "%02x", static_cast<unsigned>(slot));
    return out;
}
} // namespace bridge
