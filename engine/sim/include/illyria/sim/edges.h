#pragma once

// The small edges of battle (docs/GAME_DESIGN.md §3.4): fighting on your own
// land, on ground your kind of soldier knows, as veterans, with a hero
// leading, from the high ground, holding a temple or a tower. Each is worth a
// few percent; they add up, to at most kEdgeCapPct. The same numbers serve
// the Warlords dice (a share more strength: campaign_rules.cpp) and the 3D
// battle (a share more damage: world_edges.cpp), so a stack that has the
// edge on the map has it on the battlefield too. None of them decides a
// fight alone: a weaker army with every edge is still the weaker army.

#include <cstdint>
#include <string>

#include "illyria/sim/gait.h"

namespace illyria::sim {

inline constexpr int32_t kEdgeHomePct = 3;     // the hex is your side's land
inline constexpr int32_t kEdgeGroundPct = 4;   // ground this kind of soldier is at home on
inline constexpr int32_t kEdgeGroundMaxPct = 6;
inline constexpr int32_t kEdgeRankPct = 3;     // per rank: regular 3, veteran 6, elite 9
inline constexpr int32_t kEdgeHeightPct = 4;   // the high ground
inline constexpr int32_t kEdgeHoldPct = 4;     // holding a temple, a ruin's stones or an outpost tower
inline constexpr int32_t kEdgeCapPct = 15;     // all of them together, at most

struct Edges {
    int32_t home = 0, ground = 0, seniority = 0, hero = 0, height = 0, holding = 0;
    // All of them, in percent, at most kEdgeCapPct.
    int32_t total() const;
    // For the player: "home land +3% · high ground +4%", or "" with none.
    std::string describe() const;
};

// A hero leads the armies beside him by his strength, as in Warlords'
// leadership points (4-6: one, 7-8: two, 9: three), each worth 2%.
int32_t heroLeadPct(int32_t heroStrength);
// Ground a gait is at home on: clearly quicker over it than a man on foot
// (elves and wolves in the woods, dwarves on the rocks, giants in the mud
// and fords, the dead in the swamp). Plains, roads and bridges are anyone's.
bool familiarGround(Gait gait, Ground ground);

}  // namespace illyria::sim
