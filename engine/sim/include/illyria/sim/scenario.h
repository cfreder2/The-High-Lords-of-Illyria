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

// Six archers volley a column of ten footmen advancing over hills through a
// crosswind, then fight them hand to hand. Used as the cross-platform
// determinism benchmark: terrain, ballistics, wind, auto-fire and melee.
struct Ford {
    static constexpr uint32_t kTicks = 60 * kTicksPerSecond;
    static void setup(World& world);
    static Replay replay(uint64_t seed);
};

// Every unit type on both sides, plus extra dwarven grenadiers, charging
// each other across a valley. The determinism benchmark for shields, fliers,
// healing, knockback, explosions and remains.
struct Melee {
    static constexpr uint32_t kTicks = 60 * kTicksPerSecond;
    static void setup(World& world);
    static Replay replay(uint64_t seed);
};

// On the Illyria region: an army fords the river under archer and grenadier
// fire while griffins fly over. The determinism benchmark for region
// generation, pathfinding, collisions, flight and kicked remains.
struct Crossing {
    static constexpr uint32_t kTicks = 100 * kTicksPerSecond;
    static void setup(World& world);
    static Replay replay(uint64_t seed);
};

}  // namespace scenarios
}  // namespace illyria::sim
