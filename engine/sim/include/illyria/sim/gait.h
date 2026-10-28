#pragma once

#include <cstdint>

#include "illyria/sim/fixed.h"

namespace illyria::sim {

enum class Ground : uint8_t;

// How a unit gets over the ground: each gait has its own speed on each kind
// of ground (after Warlords' terrain movement costs). The same table drives a
// walker's speed and the cost of its paths, so each kind takes its own fastest
// route: horsemen keep to roads and open ground, dwarves cut over the rocks,
// the dead wade through the swamp.
enum class Gait : uint8_t {
    Foot,      // men on foot; fliers when they walk
    Horse,     // cavalry: fast on roads and open ground, slow in woods and bog
    Wolf,      // wolf riders: fast in the open and in the woods, not in bog
    Dwarf,     // hill folk: rock and slopes slow them least
    Woodland,  // elves and spiders: at home among the trees
    Giant,     // giants, minotaurs, elephants, demons: long strides through mud and fords
    Undead,    // the dead do not tire in the swamp
    Wheeled,   // engines: roads, or a crawl
    Count
};
inline constexpr int kGaitCount = static_cast<int>(Gait::Count);
inline constexpr int kGroundCount = 10;

// Speed multiplier on this ground for this gait (zero: impassable on foot).
Fixed gaitSpeed(Gait gait, Ground ground);
// Percent of the usual slowdown when climbing.
int32_t gaitClimbPct(Gait gait);
const char* gaitName(Gait gait);

}  // namespace illyria::sim
