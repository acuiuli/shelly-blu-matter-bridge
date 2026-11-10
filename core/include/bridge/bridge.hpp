// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bridge {
constexpr size_t device_count = 4;
using Time = uint64_t; // monotonic milliseconds, never persisted
enum class Field : uint8_t { setpoint, position, mode, count };
enum class Outcome : uint8_t { idle, queued, sent, confirmed, expired, failed, superseded };
struct Command {
    uint64_t token = 0;
    Field field = Field::setpoint;
    int value = 0;
    Time expires = 0;
    Time next_attempt = 0;
    unsigned attempts = 0;
    Outcome outcome = Outcome::idle;
};
struct Telemetry {
    int temperature = 2000; // Celsius * 100, no floating point conversion
    int setpoint = 2000;
    int position = 0;
    int mode = 4;      // Matter Heat (4), Off (0); direct valve mode is separate
    int battery = 100; // percent, Matter Power Source uses half-percent units
};
struct State {
    std::optional<Telemetry> reported;
    Time last_seen = 0;
    bool reachable = false;
    std::array<Command, static_cast<size_t>(Field::count)> commands{};
};

// A future BLE implementation supplies transport observations; it must not mutate
// the Matter data model or persist commands. All methods run on one owner task.
class Engine {
  public:
    static constexpr Time stale_after = 30000;
    static constexpr Time command_lifetime = 15000;
    static constexpr unsigned max_attempts = 3;
    bool request(size_t device, Field field, int value, Time now);
    std::optional<Command> take(size_t device, Time now);
    bool current(size_t device, uint64_t token, Time now) const;
    void failure(size_t device, uint64_t token, Time now);
    bool observe(size_t device, const Telemetry &value, Time now,
                 std::optional<Field> manual_change = std::nullopt);
    void tick(Time now);
    const State &state(size_t device) const { return devices_.at(device); }

  private:
    std::array<State, device_count> devices_{};
    uint64_t next_token_ = 1;
};

struct Faults {
    Time delay = 500;
    unsigned failures = 0;
    bool available = true;
    bool hold_position = false; // accept request without confirming physical motion
};
class Simulator {
  public:
    std::array<Faults, device_count> faults{};
    void step(Engine &engine, Time now);
    bool manual(Engine &engine, size_t device, Field field, int value, Time now);

  private:
    struct Flight {
        Command command;
        Time due;
    };
    std::array<Telemetry, device_count> values_{};
    std::array<std::optional<Flight>, device_count> flights_{};
};

// Persist only a schema and stable gateway id. Fixed slots reserve endpoint ids
// 2/3, 4/5, 6/7, 8/9. Enrollment will need an explicit, versioned slot binding.
struct Identity {
    uint32_t schema = 1;
    std::array<uint8_t, 16> gateway{};
    bool valid() const;
    uint16_t thermostat_endpoint(size_t slot) const;
    uint16_t valve_endpoint(size_t slot) const;
    std::array<char, 33> unique_id(size_t slot) const;
};
} // namespace bridge
