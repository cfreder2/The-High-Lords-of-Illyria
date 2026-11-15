// A siege played as one: the engines batter a gate from their own reach, the men near them hold a line in front and meet
// the garrison's sorties (escort), the shooters answer the wall-walks first (suppression), and once the doors are down the
// attackers mass outside the breach before they go in together (rally), and those sent to hold the ground beyond it stay on it
// (hold). These are rules of the World, for every side, the
// player's and the computer's alike, a skirmish's and a Conquest's (docs/BALANCE.md, the siege). All derived from the men's
// own state each step: nothing here is saved, and nothing reads a float.
#include <algorithm>

#include "gate_geometry.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Escort: the men within this of a battering engine keep its line; the line stands this far ahead of the engines (foot), and
// the shooters and healers this far (their own sign: behind the foot, nearer the engines).
constexpr Fixed kEscortReach = Fixed::fromInt(80);
constexpr Fixed kEscortAheadFoot = Fixed::fromInt(14);
constexpr Fixed kEscortAheadShooter = Fixed::fromInt(5);
constexpr Fixed kEscortAheadHealer = Fixed::fromInt(-3);
constexpr Fixed kEscortSlack = Fixed::fromInt(4);
constexpr int32_t kEscortFile = 13;              // men to the width of a line, then they double up
constexpr Fixed kEscortGap = Fixed::fromInt(3);  // between men along the line
// A foe this near an engine is a sortie: the escort meets it.
constexpr Fixed kSortieReach = Fixed::fromInt(45);
// Suppression: a shooter prefers a man on a wall-walk unless a foe is nearer than this.
constexpr Fixed kCloseFoe = Fixed::fromInt(18);
// An engine does not shell a man on the wall-walk with one of his own side's within this of him (the stone's burst and scatter).
constexpr Fixed kFriendClear = Fixed::fromInt(10);
// ... and an engine creeps up to the walls' men this much beyond its reach, to be in it.
constexpr Fixed kCreepMore = Fixed::fromInt(40);
// Rally: a breach's garrison shoots 70 m and a little more from its walls, so the wave gathers beyond that: those within
// kRallyOuter of a breach (and farther than kRallyInner, outside it) wait for the wave; those coming in
// from within kRallyFar are waited for if their order is no older than kRallyWait; the wave goes when this share (percent) is
// there; a breach older than kRallyCap is no longer a place to wait.
constexpr Fixed kRallyInner = Fixed::fromInt(70);
constexpr Fixed kRallyOuter = Fixed::fromInt(95);
constexpr Fixed kRallyFar = Fixed::fromInt(220);
constexpr int32_t kRallyShare = 70;
constexpr uint32_t kRallyWait = 60 * kTicksPerSecond;
constexpr uint32_t kRallyCap = 240 * kTicksPerSecond;
// Hold: a man sent to hold ground who is idle farther than this from his place goes back to it.
constexpr Fixed kHoldSlack = Fixed::fromInt(6);

bool isEngine(const Unit& u) { return u.stats.ranged.kind == ProjectileKind::Stone; }

}  // namespace

Fixed World::autoReach(const Unit& u) const {
    return isEngine(u) ? u.stats.ranged.maxRange * 4 / 5 : u.stats.ranged.autoRange;
}

void World::updateSiege() {
    batteries_.clear();
    rallies_.clear();
    if (!config_.region || gates_.empty()) return;
    const Region& rg = *config_.region;
    // The engines at work on a gate still standing, by owner and gate, with the middle of them.
    struct Sum {
        PlayerId owner;
        int32_t gate;
        Vec3 total;
        int32_t n;
    };
    std::vector<Sum> sums;
    for (const Unit& u : units_) {
        if (!u.present() || !u.alive() || u.routed || !u.hasAttackTarget || u.attackGate < 0 || !isEngine(u)) continue;
        const size_t g = static_cast<size_t>(u.attackGate);
        if (g >= gates_.size() || gates_[g].broken()) continue;
        auto it = std::find_if(sums.begin(), sums.end(), [&](const Sum& s) { return s.owner == u.owner && s.gate == u.attackGate; });
        if (it == sums.end()) it = sums.insert(sums.end(), Sum{u.owner, u.attackGate, Vec3{}, 0});
        it->total = it->total + u.pos;
        ++it->n;
    }
    for (const Sum& s : sums) {
        const Gate& g = rg.gates()[static_cast<size_t>(s.gate)];
        const Vec3 centre = s.total / Fixed::fromInt(s.n);
        const Vec3 to = ((g.a + g.b) * Fixed::fromRatio(1, 2) - centre).flat();
        const Fixed len = to.length();
        if (len > Fixed::zero()) batteries_.push_back({s.owner, s.gate, centre, to / len});
    }
    // The breaches that are fresh: who has come, and who is still coming, of each side.
    for (size_t i = 0; i < gates_.size() && i < rg.gates().size(); ++i) {
        const GateState& st = gates_[i];
        if (!st.broken() || st.holder == GateState::kNoHolder || tick_ - st.brokenTick >= kRallyCap) continue;
        const Gate& g = rg.gates()[i];
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
        struct Count {
            PlayerId owner;
            int32_t arrived, inbound;
        };
        std::vector<Count> counts;
        unitGrid_.query(mid.x, mid.y, kRallyFar, [&](uint32_t j) {
            const Unit& u = units_[j];
            if (!u.alive() || u.routed || u.inAir() || u.flight != Unit::Flight::Grounded || isEngine(u) || u.owner == st.holder) return;
            const Fixed d2 = (u.pos - mid).flat().lengthSq();
            const bool arrived = d2 <= kRallyOuter * kRallyOuter;
            const bool coming = !arrived && u.hasMoveTarget && tick_ - u.orderTick < kRallyWait && withinRing(ring, u.moveTarget) &&
                                !townSide(g, ring, u.pos);
            if (!arrived && !coming) return;
            auto it = std::find_if(counts.begin(), counts.end(), [&](const Count& c) { return c.owner == u.owner; });
            if (it == counts.end()) it = counts.insert(counts.end(), Count{u.owner, 0, 0});
            (arrived ? it->arrived : it->inbound) += 1;
        });
        for (const Count& c : counts) rallies_.push_back({static_cast<int32_t>(i), c.owner, c.arrived * 100 >= (c.arrived + c.inbound) * kRallyShare});
    }
}

bool World::rallyHold(const Unit& u) const {
    if (rallies_.empty() || !u.hasMoveTarget || isEngine(u) || tick_ - u.orderTick >= kRallyWait) return false;
    const Region& rg = *config_.region;
    for (const Rally& r : rallies_) {
        if (r.owner != u.owner || r.go) continue;
        const Gate& g = rg.gates()[static_cast<size_t>(r.gate)];
        const Fixed d2 = (u.pos - (g.a + g.b) * Fixed::fromRatio(1, 2)).flat().lengthSq();
        if (d2 < kRallyInner * kRallyInner || d2 > kRallyOuter * kRallyOuter) continue;
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        if (!townSide(g, ring, u.pos) && withinRing(ring, u.moveTarget)) return true;
    }
    return false;
}

Unit* World::escortFoe(const Unit& u) {
    if (batteries_.empty() || isEngine(u) || u.stats.melee.damage == 0 || u.stats.ranged.muzzleSpeed > Fixed::zero() || u.inAir() || u.flight != Unit::Flight::Grounded || gateKeeper(u)) return nullptr;
    const Fixed reachUp = u.stats.height + u.stats.melee.reach;
    for (const Battery& b : batteries_) {
        if (b.owner != u.owner || (u.pos - b.centre).flat().lengthSq() > kEscortReach * kEscortReach) continue;
        Unit* best = nullptr;
        Fixed bestD2;
        uint32_t bestIndex = 0;
        nearbyNotOf(u.owner, b.centre.x, b.centre.y, kSortieReach, [&](uint32_t i) {
            Unit& o = units_[i];
            if (!o.alive() || o.owner == u.owner || o.fleeing() || o.inAir() || o.pos.z - u.pos.z > reachUp) return;
            if ((o.pos - b.centre).flat().lengthSq() > kSortieReach * kSortieReach) return;
            const Fixed d2 = (o.pos - u.pos).flat().lengthSq();
            if (best == nullptr || d2 < bestD2 || (d2 == bestD2 && i < bestIndex)) {
                best = &o;
                bestD2 = d2;
                bestIndex = i;
            }
        });
        if (best != nullptr) return best;
    }
    return nullptr;
}

bool World::escortSpot(Unit& u) {
    if (batteries_.empty() || isEngine(u) || u.inAir() || u.flight != Unit::Flight::Grounded || u.routed || gateKeeper(u)) return false;
    for (const Battery& b : batteries_) {
        if (b.owner != u.owner || (u.pos - b.centre).flat().lengthSq() > kEscortReach * kEscortReach) continue;
        const Fixed ahead = u.stats.healing.amount > 0                      ? kEscortAheadHealer
                            : u.stats.ranged.muzzleSpeed > Fixed::zero() ? kEscortAheadShooter
                                                                         : kEscortAheadFoot;
        // Each man his own place along the line, by his number, the line doubling up behind itself when it is wide.
        const int32_t slot = static_cast<int32_t>(u.id % static_cast<uint32_t>(kEscortFile)) - kEscortFile / 2;
        const int32_t rank = static_cast<int32_t>((u.id / static_cast<uint32_t>(kEscortFile)) % 2);
        const Vec3 side{-b.toward.y, b.toward.x, Fixed::zero()};
        Vec3 spot = b.centre + b.toward * (ahead - Fixed::fromInt(3 * rank)) + side * (kEscortGap * Fixed::fromInt(slot));
        spot.z = u.pos.z;
        if ((spot - u.pos).flat().lengthSq() > kEscortSlack * kEscortSlack) walk(u, spot);
        else turnToward(u, b.toward);
        return true;
    }
    return false;
}

bool World::holdGround(Unit& u) {
    // The storm that took a courtyard stays in it: after the fight, and after a rout and a rally, each man goes back to his place
    // there, until an order says otherwise (World::startOrder).
    if (!u.holding || u.inAir() || u.flight != Unit::Flight::Grounded || (u.holdAt - u.pos).flat().lengthSq() <= kHoldSlack * kHoldSlack) return false;
    const Vec3 face = u.arriveFacing;
    beginMove(u, u.holdAt, Fixed::zero(), face);
    planPath(u, u.holdAt);
    return true;
}

bool World::friendNear(const Unit& u, const Vec3& at, Fixed radius) const {
    bool found = false;
    nearby(at.x, at.y, radius, [&](uint32_t i) {
        const Unit& o = units_[i];
        found = found || (o.alive() && o.owner == u.owner && o.id != u.id && !o.inAir() && (o.pos - at).flat().lengthSq() <= radius * radius);
    });
    return found;
}

Unit* World::wallShooter(const Unit& u, Fixed reach) {
    if (!config_.region || config_.region->ramparts().empty() || u.stats.ranged.muzzleSpeed == Fixed::zero() || u.stats.ranged.autoRange <= Fixed::zero()) return nullptr;
    const Region& rg = *config_.region;
    if (rg.rampartAt(u.pos.x, u.pos.y, u.pos.z) >= 0) return nullptr;  // (a man on the walls shoots as he always did)
    const bool engine = isEngine(u);
    Unit* best = nullptr;
    Fixed bestD2;
    uint32_t bestIndex = 0;
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, reach, [&](uint32_t i) {
        Unit& o = units_[i];
        // The garrison's shooters on the walks (an engine, which brings the whole parapet down, takes any man up there).
        if (!o.alive() || o.inAir() || (!engine && o.stats.ranged.autoRange <= Fixed::zero())) return;
        const Fixed d2 = (o.pos - u.pos).flat().lengthSq();
        if (d2 > reach * reach || (best != nullptr && (d2 > bestD2 || (d2 == bestD2 && i > bestIndex)))) return;
        if (rg.rampartAt(o.pos.x, o.pos.y, o.pos.z) < 0) return;
        if (engine && friendNear(u, o.pos, kFriendClear)) return;  // (a stone does not fall among his own)
        best = &o;
        bestD2 = d2;
        bestIndex = i;
    });
    return best;
}

bool World::creepToWall(Unit& u) {
    // An engine with nothing in reach and the garrison's men on the wall-walks a little beyond it: closer, to its own reach and no
    // nearer (the walls' archers shoot 70 m, an engine's throw is twice that). The men behind the walls cannot be hit from outside.
    if (!isEngine(u) || u.hasMoveTarget || u.hasAttackTarget) return false;
    const Unit* mark = wallShooter(u, autoReach(u) + kCreepMore);
    if (mark == nullptr) return false;
    walk(u, mark->pos);
    return true;
}

Unit* World::autoTarget(const Unit& u) {
    if (u.stats.ranged.autoRange <= Fixed::zero()) return nullptr;
    Unit* near = nearestEnemy(u, autoReach(u) * 3 / 2);
    // The walls are cleared first, unless a foe is on him.
    if (Unit* wall = wallShooter(u, autoReach(u)); wall != nullptr && wall != near && (near == nullptr || (near->pos - u.pos).flat().lengthSq() > kCloseFoe * kCloseFoe)) return wall;
    return near;
}

}  // namespace illyria::sim
