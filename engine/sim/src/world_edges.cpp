// The small edges in the 3D battle (edges.h, docs/GAME_DESIGN.md §3.4): each a
// few percent more damage, added up to at most kEdgeCapPct, as in the
// campaign's dice. Every soldier reckons his own:
// - home land: his side holds the hex the battle is fought in (+3%);
// - familiar ground: his kind is at home on the ground under his feet (his
//   gait clearly quicker over it than a man on foot: elves and wolves in the
//   woods, dwarves on the rocks, giants in the mud and fords, the dead in the
//   swamp: +4%), or his type's liking for open ground or hills (UnitStats::
//   terrain: pikemen and light horse +3% in the open, knights +5%, dwarves +4%
//   in the hills); the better of the two, at most +6%;
// - veterans: +3% a rank (regular 3, veteran 6, elite 9); a hero's own levels
//   are his strength instead (World::gracePct);
// - a hero leading: the best living hero of his side within 30 m, by the
//   hero's strength (3 + level): 4-6 +2%, 7-8 +4%, 9 +6%;
// - the high ground: striking down at a foe below, +1% for each 0.05 of grade
//   between them, at most +4%.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {
constexpr Fixed kLeadReach = Fixed::fromInt(30);
// Open ground and hills, for the types' liking for them (as world_combat.cpp's melee terrain).
constexpr Fixed kOpenSlope = Fixed::fromRatio(3, 50);
constexpr Fixed kHillsSlope = Fixed::fromRatio(3, 20);
constexpr int32_t kHeightPctPerGrade = 20;
}  // namespace

Edges World::edgesOf(const Unit& u, Fixed dist, Fixed dz) const {
    Edges e;
    if (config_.homeSide >= 0 && u.owner == static_cast<PlayerId>(config_.homeSide)) e.home = kEdgeHomePct;
    int32_t ground = familiarGround(u.stats.gait, groundAt(u.pos.x, u.pos.y)) ? kEdgeGroundPct : 0;
    if (u.stats.terrain.openPct > 0 || u.stats.terrain.hillsPct > 0) {
        const Fixed slope = slopeAt(u.pos.x, u.pos.y);
        if (slope <= kOpenSlope) ground = std::max(ground, u.stats.terrain.openPct);
        if (slope >= kHillsSlope) ground = std::max(ground, u.stats.terrain.hillsPct);
    }
    e.ground = std::min(kEdgeGroundMaxPct, ground);
    const bool hero = u.stats.type == static_cast<uint8_t>(UnitType::Hero);
    if (!hero) e.seniority = kEdgeRankPct * std::clamp(u.rank, 0, 3);
    int32_t best = 0;
    for (const Unit& h : units_) {
        if (h.owner != u.owner || !h.alive() || h.stats.type != static_cast<uint8_t>(UnitType::Hero) || h.id == u.id) continue;
        if ((h.pos - u.pos).flat().lengthSq() > kLeadReach * kLeadReach) continue;
        best = std::max(best, std::min(9, 4 + h.rank));  // the hero's strength: level + 3
    }
    e.hero = heroLeadPct(best);
    if (dist > Fixed::zero() && dz < Fixed::zero())
        e.height = std::clamp(static_cast<int32_t>((-dz / dist * kHeightPctPerGrade).floorToInt()), 0, kEdgeHeightPct);
    return e;
}

int32_t World::scaledDamage(int32_t base, int32_t pctA, int32_t pctB) {
    const int64_t num = int64_t(base) * pctA * pctB;
    int64_t out = num / 10000;
    const int64_t rest = num % 10000;
    if (rest > 0 && static_cast<int64_t>(rng_.nextBelow(10000)) < rest) ++out;
    return static_cast<int32_t>(out);
}

}  // namespace illyria::sim
