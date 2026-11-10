// SPDX-License-Identifier: Apache-2.0
#include "bridge/bridge.hpp"
#include <iostream>

int main() {
    bridge::Engine engine;
    bridge::Simulator simulator;
    simulator.faults[0].hold_position = true;
    simulator.faults[1].available = false;
    simulator.faults[2].failures = 1;
    for (size_t d = 0; d < bridge::device_count; ++d)
        engine.request(d, bridge::Field::position, 60, 0);
    for (bridge::Time now = 0; now <= 16000; now += 100)
        simulator.step(engine, now);
    std::cout << "slot reachable requested reported outcome (enum in bridge.hpp)\n";
    for (size_t d = 0; d < bridge::device_count; ++d) {
        const auto &s = engine.state(d);
        const auto &c = s.commands[static_cast<size_t>(bridge::Field::position)];
        std::cout << d << ' ' << s.reachable << ' ' << c.value << ' ';
        if (s.reported)
            std::cout << s.reported->position;
        else
            std::cout << "unknown";
        std::cout << ' ' << static_cast<int>(c.outcome) << '\n';
    }
}
