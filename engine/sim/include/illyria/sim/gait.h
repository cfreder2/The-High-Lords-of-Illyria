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
    Hyena,     // the hyena-folk's lope: rough ground, bog and woods slow it little
    Count
};
inline constexpr int kGaitCount = static_cast<int>(Gait::Count);
inline constexpr int kGroundCount = 10;

// How far a soldier keeps his middle from standing stone (a site's stones, a
// ruin's stepped base, pillars) so his picture does not sink into it: his
// drawn half-width, about a third more than his radius, and a hand more.
constexpr Fixed stoneStandoff(Fixed radius) { return radius * 4 / 3 + Fixed::fromRatio(1, 10); }
// How far routes on this gait keep from a site's stone: at least the standoff
// of the broadest who walk it (an elephant farther than a man), so a route
// never runs him into stone that would push him off it. In centimetres, in
// Gait's order: men 0.5 m round stand off 0.77, heavy horse (0.95) 1.37,
// wolves (0.8) 1.17, spiders (1.1) 1.57, elephants (1.8) 2.5, the catapult
// (1.6) 2.23; a little over. Fliers on foot go as men.
inline constexpr int32_t kGaitClearanceCm[kGaitCount] = {80, 140, 120, 80, 160, 250, 80, 230, 80};
constexpr Fixed gaitClearance(Gait gait) {
    const int gi = static_cast<int>(gait);
    return Fixed::fromRatio(gi >= 0 && gi < kGaitCount ? kGaitClearanceCm[gi] : 80, 100);
}

// Speed multiplier on this ground for this gait (zero: impassable on foot).
Fixed gaitSpeed(Gait gait, Ground ground);
// Percent of the usual slowdown when climbing.
int32_t gaitClimbPct(Gait gait);
const char* gaitName(Gait gait);

}  // namespace illyria::sim
