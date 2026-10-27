#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "illyria/sim/world.h"

namespace illyria::sim {

// Recorded inputs for a match: everything needed to reproduce it exactly.
struct Replay {
    WorldConfig config;
    struct TimedCommand {
        uint32_t tick;
        Command command;
    };
    std::vector<TimedCommand> commands;  // sorted by tick
};

// Builds the starting world for a scenario.
using ScenarioSetup = std::function<void(World&)>;

// Runs `ticks` ticks of a replay. `onTick` (optional) is called after each step.
void runReplay(World& world, const Replay& replay, uint32_t ticks,
               const std::function<void(const World&)>& onTick = {});

namespace scenarios {

UnitStats archer();
UnitStats footman();

// Six archers on a hillside volley a column of ten footmen advancing through a
// crosswind. Used as the cross-platform determinism benchmark.
struct Ford {
    static constexpr uint32_t kTicks = 20 * kTicksPerSecond;
    static void setup(World& world);
    static Replay replay(uint64_t seed);
};

}  // namespace scenarios
}  // namespace illyria::sim
