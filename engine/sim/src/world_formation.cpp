// Formations and the ground under a formed body (docs/BALANCE.md, "Land, formations, calls").
// - Each formation is a choice with a cost (kRules): the double line holds its nerve, the square has no flank and is
//   slow, weak in attack and a fat mark for arrows, the wedge strikes hard and dies in the flank, loose order lets arrows
//   by and fights poorly, the crescent and the V strike best into a flank and stand a shock worst.
// - A formation is its men together and in order: a man with no squad-mates by him, or disordered (after a charge,
//   struck in the flank or back, in woods, mud or water), has none until he has re-formed.
// - Woods: arrows are stopped among the trees, and a man in them is unseen by a side with nobody near him; the first
//   blow out of hiding is an ambush.
// All of it from the men's own state, Fixed and integers only, the same for every side and for Skirmish and Conquest.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

//                                        blow aside shock flankShock struckAside struckFront shot spare allRound tight
constexpr World::FormationRule kRules[kFormationKinds] = {
    /* None       */ {0, 0, 0, 0, 0, 0, 0, 0, false, false},
    /* Line       */ {0, 0, 0, 50, 0, 0, 0, 0, false, true},
    /* DoubleLine */ {0, 0, -30, 0, 0, 0, 15, 0, false, true},
    /* Box        */ {-25, 0, -15, 0, 0, 0, 40, 0, true, true},
    /* Wedge      */ {25, 0, 0, 50, 30, 0, 0, 0, false, true},
    /* Loose      */ {-20, 0, 30, 0, 0, 0, 0, 40, false, false},
    /* Encircle   */ {0, 25, 30, 0, 0, 0, 0, 20, false, false},
    /* Vee        */ {0, 25, 0, 0, 0, 20, 0, 0, false, true},
};
// A man out of his formation: his blows this much weaker, shocks this much worse, for as long as it takes to re-form.
constexpr int32_t kDisorderBlowPct = -20, kDisorderShockPct = 30;
// Squad-mates within this of him make a formation (kRankMates of them at least).
constexpr Fixed kRankReach = Fixed::fromInt(6);
constexpr int32_t kRankMates = 2;
// Bad ground keeps a formation broken this long after he last stood on it.
constexpr int32_t kGroundDisorder = 2 * kTicksPerSecond;
// Among the trees: this share of the arrows meant for a man are stopped.
constexpr int32_t kWoodsSparePct = 40;
// An ambush: the shock of the first blow from men unseen; it counts for this long after they break cover.
constexpr int32_t kAmbushShock = 250;
constexpr uint32_t kAmbushTicks = 5 * kTicksPerSecond;
constexpr int32_t kCensusTicks = 10;  // (with the morale census: world_morale.cpp)

}  // namespace

const World::FormationRule& World::formationRule(FormationKind f) { return kRules[std::min<size_t>(static_cast<size_t>(f), kFormationKinds - 1)]; }

void World::setSquad(EntityId id, uint32_t squad, FormationKind formation) {
    Unit* u = mutableUnit(id);
    if (u == nullptr) return;
    u->squad = squad;
    u->formation = formation;
    u->ranked = kRankMates;  // raised in his place among them (the first census counts)
}

World::Footing World::footing(const Unit& u) const {
    if (!config_.region || !u.alive() || u.inAir()) return Footing::Firm;
    const Ground g = groundAt(u.pos.x, u.pos.y);
    if (g != Ground::Shallows && g != Ground::Swamp && g != Ground::Mud && g != Ground::Forest) return Footing::Firm;
    if (familiarGround(u.stats.gait, g)) return Footing::Firm;
    return g == Ground::Shallows ? Footing::Ford : g == Ground::Forest ? Footing::Woods : Footing::Mire;
}

bool World::inWoods(const Unit& u) const {
    return config_.region && u.alive() && !u.inAir() && groundAt(u.pos.x, u.pos.y) == Ground::Forest;
}

bool World::concealed(const Unit& u, PlayerId side) const {
    return side != u.owner && side < 8 && (u.seenBy & (1u << side)) == 0;
}

bool World::formed(const Unit& u) const {
    return u.formation != FormationKind::None && u.disorder == 0 && !u.routed && u.ranked >= kRankMates && u.alive() && !u.inAir();
}

int32_t World::formBlowPct(const Unit& u, HitSide side) const {
    if (u.disorder > 0) return footing(u) == Footing::Firm || footing(u) == Footing::Woods ? kDisorderBlowPct : 0;  // (water and mud take their own)
    if (!formed(u)) return 0;
    const FormationRule& r = formationRule(u.formation);
    return r.blowPct + (side == HitSide::Front ? 0 : r.asidePct);
}

int32_t World::formStruckPct(const Unit& target, HitSide side, bool missile) const {
    if (!formed(target)) return 0;
    const FormationRule& r = formationRule(target.formation);
    if (missile) return r.shotPct;
    return side == HitSide::Front ? r.struckFrontPct : r.struckAsidePct;
}

int32_t World::formShockPct(const Unit& u) const {
    if (u.disorder > 0) return kDisorderShockPct;
    return formed(u) ? formationRule(u.formation).shockPct : 0;
}

int32_t World::formFlankShockPct(const Unit& u) const { return formed(u) ? formationRule(u.formation).flankShockPct : 0; }

bool World::allRound(const Unit& u) const { return formed(u) && formationRule(u.formation).allRound; }

bool World::turnsSlowly(const Unit& u) const { return formed(u) && formationRule(u.formation).tight; }

bool World::arrowSpared(const Unit& u) {
    const int32_t pct = std::max(inWoods(u) ? kWoodsSparePct : 0, formed(u) ? formationRule(u.formation).sparePct : 0);
    return pct > 0 && static_cast<int32_t>(rng_.nextBelow(100)) < pct;
}

void World::disorder(Unit& u, int32_t ticks) {
    if (!u.alive() || ticks <= 0) return;
    // Told when a formed body loses its order (not of a man who had none, nor again while it lasts).
    if (u.disorder == 0 && u.formation != FormationKind::None && formationRule(u.formation).tight && u.ranked >= kRankMates && !u.routed)
        tell(TacticEvent::Kind::Disordered, u);
    u.disorder = std::max(u.disorder, ticks);
}

void World::ambush(Unit& striker, Unit& struck) {
    if (striker.hidTick == 0 || tick_ + 1 - striker.hidTick > kAmbushTicks || striker.owner == struck.owner) return;
    striker.hidTick = 0;  // the surprise is spent
    shakeMorale(struck, kAmbushShock);
    disorder(struck, kGroundDisorder);
    tell(TacticEvent::Kind::Ambush, struck);
}

void World::updateRanks() {
    for (Unit& u : units_) {
        if (!u.alive() || (tick_ + u.id) % kCensusTicks != 0) continue;  // (each man on his own tick, a few times a second)
        // Re-forming, unless the ground forbids: closed ranks and horse among trees, anyone in mud or water.
        const Footing f = footing(u);
        const bool broken = f == Footing::Ford || f == Footing::Mire ||
                            (f == Footing::Woods && (u.stats.mounted || (u.formation != FormationKind::None && formationRule(u.formation).tight)));
        if (broken) disorder(u, kGroundDisorder);
        else u.disorder = std::max(0, u.disorder - kCensusTicks);
        // Squad-mates by him.
        if (u.squad != 0) {
            int32_t mates = 0;
            nearby(u.pos.x, u.pos.y, kRankReach, [&](uint32_t i) {
                const Unit& o = units_[i];
                if (o.id != u.id && o.alive() && o.owner == u.owner && o.squad == u.squad && !o.routed && (o.pos - u.pos).flat().lengthSq() < kRankReach * kRankReach) ++mates;
            });
            u.ranked = mates;
        }
        // Who sees him among the trees.
        if (!inWoods(u)) {
            u.seenBy = 0xFF;
            continue;
        }
        uint8_t seen = static_cast<uint8_t>(u.owner < 8 ? 1u << u.owner : 0);
        nearby(u.pos.x, u.pos.y, kSpotReach, [&](uint32_t i) {
            const Unit& o = units_[i];
            if (o.alive() && o.owner != u.owner && o.owner < 8 && (o.pos - u.pos).flat().lengthSq() < kSpotReach * kSpotReach) seen = static_cast<uint8_t>(seen | (1u << o.owner));
        });
        u.seenBy = seen;
        if (seen == (u.owner < 8 ? 1u << u.owner : 0)) u.hidTick = tick_ + 1;  // (never 0: that is "never")
    }
}

}  // namespace illyria::sim
