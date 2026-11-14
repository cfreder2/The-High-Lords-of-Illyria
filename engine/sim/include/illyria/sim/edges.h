#pragma once

// The small edges of battle (docs/GAME_DESIGN.md §3.4): fighting on your own
// land, on ground your kind of soldier knows, as veterans, with a hero
// leading, from the high ground, holding a temple or a tower. Each is worth a
// few percent; they add up, to at most kEdgeCapPct. The same numbers serve
// the Warlords dice (a share more strength: conquest_rules.cpp) and the 3D
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
inline constexpr int32_t kEdgeRealmPct = 8;    // the kind of land your realm is at home on (realmLand)
inline constexpr int32_t kEdgeCapPct = 20;     // all of them together, at most
// A realm's men also march quicker over its own kind of land, by this many percent.
inline constexpr int32_t kRealmPacePct = 15;

// The kinds of land a realm can be at home on, a bit each: where it fights harder (kEdgeRealmPct) and marches
// quicker (kRealmPacePct), in the battle and on the conquest map alike. After Warlords' empires, each with its
// country: the knights and the horse lords in the open, the elves in the woods, giants and dwarves in the hills,
// the orcs and the dark lord in the fens and wastes, the sea-minded order at the water's edge.
enum Land : uint8_t {
    kLandOpen = 1,    // level plains and the roads over them
    kLandForest = 2,  // woods
    kLandHills = 4,   // steep ground and bare rock
    kLandMarsh = 8,   // swamp and mud
    kLandShore = 16,  // sand, shallows, fords and bridges
};
// What a spot of the battlefield counts as (hilly: steeper than about 1 in 7).
uint8_t landOfGround(Ground ground, bool hilly);
// The realm's own land, by the continent's order of factions (0 for anyone else).
uint8_t realmLand(uint8_t faction);
// "the open country", "the woods"...: a realm's land in words, for the player.
const char* landName(uint8_t land);

struct Edges {
    int32_t home = 0, ground = 0, seniority = 0, hero = 0, height = 0, holding = 0, realm = 0;
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
