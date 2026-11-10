// SPDX-License-Identifier: Apache-2.0
#include "bridge/bridge.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __LINE__ << ": " #expr "\n";                                                        \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
using namespace bridge;
const Command &position(const Engine &e, size_t d = 0) { return e.state(d).commands[1]; }
int main() {
    Engine e;
    CHECK(!e.request(4, Field::position, 10, 0));
    CHECK(!e.request(0, Field::count, 10, 0));
    for (int v : {-1, 101, 10000})
        CHECK(!e.request(0, Field::position, v, 0));
    CHECK(!e.request(0, Field::setpoint, 2001, 0));
    CHECK(!e.request(0, Field::mode, 1, 0));
    CHECK(e.request(0, Field::position, 80, 0));
    auto old = e.take(0, 0);
    CHECK(old && !e.state(0).reported);
    CHECK(e.request(0, Field::position, 20, 1));
    CHECK(!e.current(0, old->token, 1));
    e.failure(0, old->token, 2);
    CHECK(position(e).outcome == Outcome::queued);
    CHECK(e.take(0, 2)->value == 20);
    Telemetry t;
    t.position = 80;
    CHECK(e.observe(0, t, 3));
    CHECK(position(e).outcome == Outcome::sent);
    t.position = 20;
    CHECK(e.observe(0, t, 4));
    CHECK(position(e).outcome == Outcome::confirmed);
    CHECK(e.request(0, Field::position, 90, 5));
    e.take(0, 5);
    t.position = 25;
    CHECK(e.observe(0, t, 6, Field::position));
    CHECK(position(e).outcome == Outcome::superseded);
    CHECK(!e.take(0, 100));
    CHECK(!e.observe(0, t, 5)); // reject observations from before latest telemetry
    t.battery = 101;
    CHECK(!e.observe(0, t, 8));
    CHECK(e.state(0).reported->battery == 100);
    e.tick(30006);
    CHECK(!e.state(0).reachable && e.state(0).reported->position == 25);

    Engine retry;
    CHECK(retry.request(0, Field::position, 50, 0));
    auto c = retry.take(0, 0);
    retry.failure(0, c->token, 100);
    CHECK(!retry.take(0, 1099));
    c = retry.take(0, 1100);
    CHECK(c && c->attempts == 2);
    retry.failure(0, c->token, 1200);
    c = retry.take(0, 3200);
    CHECK(c && c->attempts == 3);
    retry.failure(0, c->token, 3300);
    CHECK(position(retry).outcome == Outcome::failed);
    CHECK(!retry.take(0, 10000));

    Engine expiring;
    CHECK(expiring.request(0, Field::position, 100, 0));
    CHECK(!expiring.take(0, 15000));
    CHECK(position(expiring).outcome == Outcome::expired);

    Engine four;
    Simulator sim;
    sim.faults[0].available = false;
    sim.faults[1].hold_position = true;
    sim.faults[2].failures = 1;
    sim.faults[3].delay = 1200;
    for (size_t d = 0; d < device_count; ++d)
        CHECK(four.request(d, Field::position, 70, 0));
    for (Time n = 0; n <= 16000; n += 100)
        sim.step(four, n);
    CHECK(!four.state(0).reachable);
    CHECK(four.state(1).reported->position == 0);
    CHECK(position(four, 1).outcome == Outcome::expired);
    CHECK(four.state(2).reported->position == 70);
    CHECK(four.state(3).reported->position == 70);
    sim.faults[0].available = true;
    sim.step(four, 17000);
    sim.step(four, 18000);
    CHECK(four.state(0).reported->position == 0); // reconnect must not replay failed command

    Engine delayed;
    Simulator slow;
    slow.faults[0].delay = 2000;
    delayed.request(0, Field::position, 90, 0);
    slow.step(delayed, 0);
    delayed.request(0, Field::position, 10, 100);
    slow.step(delayed, 100);
    slow.step(delayed, 2000);
    CHECK(delayed.state(0).reported->position == 0);
    slow.step(delayed, 2100);
    CHECK(delayed.state(0).reported->position == 10);
    delayed.request(0, Field::position, 80, 2200);
    slow.step(delayed, 2200);
    CHECK(slow.manual(delayed, 0, Field::position, 30, 2300));
    slow.step(delayed, 5000);
    CHECK(delayed.state(0).reported->position == 30);
    delayed.request(0, Field::mode, 0, 5000);
    slow.step(delayed, 5000);
    slow.step(delayed, 7000);
    CHECK(delayed.state(0).reported->position == 30); // Off never fabricates closure

    Identity id;
    CHECK(!id.valid());
    id.gateway[0] = 0xAB;
    CHECK(id.valid());
    auto restored = id;
    for (size_t d = 0; d < device_count; ++d) {
        CHECK(id.unique_id(d) == restored.unique_id(d));
        CHECK(std::strlen(id.unique_id(d).data()) == 32);
        CHECK(id.thermostat_endpoint(d) == 2 + 2 * d);
        CHECK(id.valve_endpoint(d) == 3 + 2 * d);
        if (d)
            CHECK(id.unique_id(d) != id.unique_id(d - 1));
    }
    CHECK(id.thermostat_endpoint(4) == 0xFFFF);
    id.schema = 2;
    CHECK(!id.valid());
    Engine restarted;
    CHECK(!restarted.take(0, 0) && !restarted.state(0).reported);
    std::cout << "All reconciliation, fault isolation, identity and input checks passed\n";
}
