// Castle gates in battle: shut timber doors that block the way and the
// missiles until they are hacked, blasted or battered down, and then lie
// open for good (docs/GAME_DESIGN.md, castles).
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Swords and axes against oak and iron: a blow does a quarter of its damage.
constexpr int32_t kGateBlowDivisor = 4;
// How much more than to a man blasts and stones do to a gate, in percent: a
// stone flung at the doors, a satchel against them, a fireball (that scorches
// more than it breaks).
constexpr int32_t kStoneGatePct = 500;
constexpr int32_t kBombGatePct = 300;
constexpr int32_t kFireGatePct = 150;
// Where those coming up to a shut gate gather, in front of its face.
constexpr Fixed kGateApproach = Fixed::fromInt(4);
// A little more than a blow's reach: soldiers pressed against the doors.
constexpr Fixed kHackSlack = Fixed::fromRatio(3, 10);
// Those who cannot get through a shut gate stop this near its doors, or
// against their own men stopped before it, this near.
constexpr Fixed kHaltReach = Fixed::fromInt(6);
constexpr Fixed kHaltQueue = Fixed::fromInt(16);
constexpr Fixed kHaltTouch = Fixed::fromRatio(1, 2);

// A gate far off, by a box round one end of its doors (a continent's ground has
// eighty-eight; a soldier is only ever near one): the quick way to pass it by.
bool farFrom(const Gate& g, const Vec3& p) { return abs(p.x - g.a.x) > Region::kGateReach || abs(p.y - g.a.y) > Region::kGateReach; }

// The point of gate g's line nearest p (flat).
Vec3 nearestOnGate(const Gate& g, const Vec3& p) {
    const Vec3 ab = (g.b - g.a).flat();
    const Fixed len2 = ab.lengthSq();
    const Fixed t = len2 > Fixed::zero() ? clamp((p - g.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
    return g.a + ab * t;
}

// A place an order sends someone to is within a ring of walls if it lies
// inside them or up on their walk (whose outer half lies beyond the line of
// its corners): either way the way there is from inside, by the ramps.
bool withinRing(const Rampart& ring, const Vec3& p) {
    if (Gate::inside(ring, p)) return true;
    const Fixed reach = ring.halfThick + Fixed::fromRatio(1, 2);
    const size_t n = ring.corners.size();
    for (size_t e = 0; e < n; ++e) {
        const Vec3 a = ring.corners[e], ab = (ring.corners[(e + 1) % n] - a).flat();
        const Fixed len2 = ab.lengthSq();
        const Fixed t = len2 > Fixed::zero() ? clamp((p - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
        if ((p - (a + ab * t)).flat().lengthSq() <= reach * reach) return true;
    }
    return false;
}

// Whether someone standing at p is on the town's side of gate g: by the
// doors themselves near the gate (in the passage, behind the doors, he is
// still outside the line of the walk), by the ring's walls farther off.
bool townSide(const Gate& g, const Rampart& ring, const Vec3& p) {
    const Fixed near = (g.b - g.a).flat().length() / 2 + ring.halfThick * 2 + Fixed::fromInt(2);
    if ((p - (g.a + g.b) * Fixed::fromRatio(1, 2)).flat().lengthSq() <= near * near) return (p - g.a).flat().dot(g.out) < Fixed::zero();
    return Gate::inside(ring, p);
}

}  // namespace

void World::updateGates() {
    if (!config_.region || !nav_) return;
    const auto& gs = config_.region->gates();
    for (size_t i = 0; i < gs.size() && i < gates_.size(); ++i) {
        GateState& st = gates_[i];
        if (st.broken() || st.holder == GateState::kNoHolder) continue;
        const Gate& g = gs[i];
        const Rampart& ring = config_.region->ramparts()[static_cast<size_t>(g.ring)];
        const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
        const Fixed reach = (g.b - g.a).flat().length() / 2 + kHaltQueue;
        bool bound = false, inPassage = false;
        unitGrid_.query(mid.x, mid.y, reach, [&](uint32_t j) {
            const Unit& u = units_[j];
            if (!u.alive() || u.inAir() || u.flight != Unit::Flight::Grounded || u.pos.z >= g.top) return;
            if ((u.pos - nearestOnGate(g, u.pos)).flat().lengthSq() <= (g.halfThick + u.stats.radius + Fixed::fromInt(2)) * (g.halfThick + u.stats.radius + Fixed::fromInt(2))) inPassage = true;
            if (u.owner != st.holder || !u.hasMoveTarget) return;
            if (withinRing(ring, u.moveTarget) != townSide(g, ring, u.pos)) bound = true;
        });
        if (bound) {
            st.openUntil = tick_ + 3 * kTicksPerSecond;
            if (!st.opened) {
                st.opened = true;
                nav_->openGate(i, false);
            }
        } else if (st.opened && tick_ >= st.openUntil && !inPassage) {
            st.opened = false;
            nav_->shutGate(i);
        }
    }
}

void World::setGateHolder(size_t gate, PlayerId holder) {
    if (gate < gates_.size()) gates_[gate].holder = holder;
}

void World::setGateHp(size_t gate, int32_t hp) {
    if (gate >= gates_.size() || gates_[gate].broken()) return;
    if (hp <= 0) {
        const Gate& g = config_.region->gates()[gate];
        damageGate(static_cast<int>(gate), gates_[gate].hp, (g.a + g.b) * Fixed::fromRatio(1, 2), g.out);
        return;
    }
    gates_[gate].hp = hp;
}

Vec3 World::gateAimPoint(int gate, const Vec3& from) const {
    const Gate& g = config_.region->gates()[static_cast<size_t>(gate)];
    const bool outside = (from - g.a).flat().dot(g.out) >= Fixed::zero();
    Vec3 p = (g.a + g.b) * Fixed::fromRatio(1, 2) + g.out * (outside ? g.halfThick : -g.halfThick);
    p.z = g.a.z + (g.top - g.a.z) / 2;
    return p;
}

Fixed World::gateHit(const Vec3& a, const Vec3& b, Fixed limitT) const {
    if (!config_.region || gates_.empty()) return -Fixed::one();
    Fixed best = limitT;
    bool hit = false;
    const auto& gs = config_.region->gates();
    for (size_t i = 0; i < gs.size(); ++i) {
        if (gates_[i].passable()) continue;
        const Gate& g = gs[i];
        // Signed distances of the ends from the doors' middle plane.
        const Fixed sa = (a - g.a).flat().dot(g.out), sb = (b - g.a).flat().dot(g.out);
        if ((sa > g.halfThick && sb > g.halfThick) || (sa < -g.halfThick && sb < -g.halfThick)) continue;
        Fixed t;
        if (abs(sa) > g.halfThick) t = (sa - (sa > Fixed::zero() ? g.halfThick : -g.halfThick)) / (sa - sb);
        if (t >= best) continue;
        const Vec3 p = a + (b - a) * t;
        if (p.z < g.a.z || p.z > g.top) continue;
        const Vec3 ab = (g.b - g.a).flat();
        const Fixed along = (p - g.a).flat().dot(ab);
        if (along < Fixed::zero() || along > ab.lengthSq()) continue;
        best = t;
        hit = true;
    }
    return hit ? best : -Fixed::one();
}

void World::keepOutOfGates(Unit& u) {
    if (!config_.region) return;
    const auto& gs = config_.region->gates();
    // Only the gates near him are looked at, in order; a push moves him, so the near ones are asked again after it.
    uint32_t next = 0;
    for (bool pushed = true; pushed;) {
        pushed = false;
        const auto near = config_.region->gatesNear(u.pos.x, u.pos.y);
        for (auto it = std::lower_bound(near.begin(), near.end(), next); it != near.end() && !pushed; ++it) {
            const size_t i = *it;
            next = *it + 1;
            const Gate& g = gs[i];
            if (gates_[i].passable() || u.pos.z >= g.top || farFrom(g, u.pos)) continue;
            const Fixed reach = g.halfThick + u.stats.radius;
            const Vec3 p = nearestOnGate(g, u.pos);
            const Vec3 d = (u.pos - p).flat();
            const Fixed distSq = d.lengthSq();
            if (distSq >= reach * reach) continue;
            const Fixed dist = sqrt(distSq);
            const Vec3 n = dist > Fixed::zero() ? d / dist : g.out;
            u.pos += n * (reach - dist);
            pushed = true;
        }
    }
}

int World::gateToHack(Unit& u) {
    if (!config_.region || gates_.empty() || u.stats.melee.damage == 0) return -1;
    const Region& rg = *config_.region;
    const auto& gs = rg.gates();
    for (const size_t i : rg.gatesNear(u.pos.x, u.pos.y)) {
        const Gate& g = gs[i];
        if (gates_[i].passable() || gates_[i].holder == u.owner || u.pos.z >= g.top || farFrom(g, u.pos)) continue;
        const Fixed reach = u.stats.radius + g.halfThick + u.stats.melee.reach + kHackSlack;
        if ((u.pos - nearestOnGate(g, u.pos)).flat().lengthSq() > reach * reach) continue;
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        // Ordered through it, or idle with no foe this side of it.
        if (u.hasMoveTarget) {
            if (withinRing(ring, u.moveTarget) != townSide(g, ring, u.pos)) return static_cast<int>(i);
            continue;
        }
        if (u.hasAttackTarget) continue;
        const bool in = Gate::inside(ring, u.pos);
        const Unit* foe = nearestEnemy(u, u.stats.aggroRange);
        if (foe == nullptr || Gate::inside(ring, foe->pos) != in) return static_cast<int>(i);
    }
    return -1;
}

bool World::placeAtGate(const Unit& u, Vec3& spot) const {
    if (!config_.region || gates_.empty() || u.stats.melee.damage == 0 || !u.hasMoveTarget) return false;
    const Region& rg = *config_.region;
    const auto& gs = rg.gates();
    constexpr Fixed kQueue = Fixed::fromInt(10);
    for (const size_t i : rg.gatesNear(u.pos.x, u.pos.y)) {
        const Gate& g = gs[i];
        if (gates_[i].passable() || gates_[i].holder == u.owner || u.pos.z >= g.top || farFrom(g, u.pos)) continue;
        const Vec3 at = nearestOnGate(g, u.pos);
        const Vec3 off = (u.pos - at).flat();
        if (off.lengthSq() > kQueue * kQueue) continue;
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        if (withinRing(ring, u.moveTarget) == townSide(g, ring, u.pos)) continue;
        // Its own place along the doors, by where it stands, on its side of them.
        const Vec3 along = (g.b - g.a).flat();
        const Fixed len = along.length();
        if (len <= Fixed::zero()) continue;
        const Vec3 dir = along / len;
        const Fixed t = clamp((u.pos - g.a).flat().dot(dir), u.stats.radius, max(u.stats.radius, len - u.stats.radius));
        const Vec3 side = off.dot(g.out) >= Fixed::zero() ? g.out : -g.out;
        spot = g.a + dir * t + side * (g.halfThick + u.stats.radius + u.stats.melee.reach / 2);
        spot.z = u.pos.z;
        return true;
    }
    return false;
}

bool World::haltAtGate(const Unit& u) const {
    // Nobody passes a shut gate: its own side never hacks at it (nor does one
    // with no blow to strike), so they come as near as they can, to the gate,
    // and stop. Fliers go over.
    if (!config_.region || gates_.empty() || !u.hasMoveTarget || u.stats.flyAltitude > Fixed::zero()) return false;
    const Region& rg = *config_.region;
    const auto& gs = rg.gates();
    for (const size_t i : rg.gatesNear(u.pos.x, u.pos.y)) {
        const Gate& g = gs[i];
        if (gates_[i].passable() || u.pos.z >= g.top || farFrom(g, u.pos)) continue;
        if (gates_[i].holder != u.owner && u.stats.melee.damage > 0) continue;  // he hacks his way through (gateToHack)
        const Vec3 at = nearestOnGate(g, u.pos);
        const Fixed distSq = (u.pos - at).flat().lengthSq();
        if (distSq > kHaltQueue * kHaltQueue) continue;
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        const bool in = townSide(g, ring, u.pos);
        if (withinRing(ring, u.moveTarget) == in) continue;
        if (distSq <= kHaltReach * kHaltReach) return true;
        // Behind his own men already stopped before it.
        bool against = false;
        unitGrid_.query(u.pos.x, u.pos.y, u.stats.radius + Fixed::fromInt(2), [&](uint32_t j) {
            const Unit& o = units_[j];
            if (against || o.id == u.id || o.owner != u.owner || !o.alive() || o.hasMoveTarget || o.inAir() || o.flight != Unit::Flight::Grounded) return;
            const Fixed touch = u.stats.radius + o.stats.radius + kHaltTouch;
            if ((o.pos - u.pos).flat().lengthSq() > touch * touch || o.pos.z >= g.top || townSide(g, ring, o.pos) != in) return;
            against = (o.pos - nearestOnGate(g, o.pos)).flat().lengthSq() <= kHaltQueue * kHaltQueue;
        });
        if (against) return true;
    }
    return false;
}

void World::hackGate(Unit& u, int gate) {
    const Gate& g = config_.region->gates()[static_cast<size_t>(gate)];
    const Vec3 at = nearestOnGate(g, u.pos);
    const Vec3 d = (at - u.pos).flat();
    const Fixed len = d.length();
    if (len > Fixed::zero()) u.facing = d / len;
    u.engaged = true;
    if (u.meleeCooldown > 0) return;
    u.meleeCooldown = attackTicks(u, u.stats.melee.cooldownTicks);
    const int32_t amount = std::max<int32_t>(1, u.stats.melee.damage * prowessPct(u) / 100 / kGateBlowDivisor);
    damageGate(gate, amount, at + Vec3{Fixed::zero(), Fixed::zero(), u.stats.height * 3 / 5}, u.facing);
}

void World::blastGates(const Vec3& at, const Explosive& ex, ProjectileKind kind) {
    if (!config_.region || ex.radius <= Fixed::zero()) return;
    const int32_t pct = kind == ProjectileKind::Stone ? kStoneGatePct : kind == ProjectileKind::Bomb ? kBombGatePct : kFireGatePct;
    const auto& gs = config_.region->gates();
    for (size_t i = 0; i < gs.size(); ++i) {
        if (gates_[i].broken()) continue;
        const Gate& g = gs[i];
        // Nearest point of the doors: along their line, up their height, out of their thickness.
        Vec3 q = nearestOnGate(g, at);
        q.z = clamp(at.z, g.a.z, g.top);
        const Fixed d = max(Fixed::zero(), (at - q).length() - g.halfThick);
        if (d >= ex.radius) continue;
        const Fixed f = Fixed::one() - d / ex.radius;
        const int32_t amount = static_cast<int32_t>((f * ex.damage * pct / 100).floorToInt());
        if (amount > 0) damageGate(static_cast<int>(i), amount, q, (q - at).flat());
    }
}

void World::damageGate(int gate, int32_t amount, const Vec3& at, const Vec3& dir) {
    GateState& gs = gates_[static_cast<size_t>(gate)];
    if (gs.broken()) return;
    gs.hp = std::max(0, gs.hp - amount);
    ImpactEvent e{ImpactEvent::Kind::Gate, at, dir, kNoEntity, kNoEntity, gs.broken(), {}};
    e.gate = gate;
    impacts_.push_back(e);
    if (!gs.broken()) return;
    // Broken open for good: the way in is clear, and nobody shoots at it any more.
    gs.brokenTick = tick_;
    if (nav_) nav_->openGate(static_cast<size_t>(gate));
    dropGateArrows(gate);
    for (Unit& u : units_) {
        if (u.attackGate == gate) {
            u.attackGate = -1;
            u.hasAttackTarget = false;
        }
        // Everyone on the march finds the new way.
        if (u.alive() && u.hasMoveTarget && u.flight == Unit::Flight::Grounded && !u.airborne) planPath(u, u.moveTarget);
    }
}

bool World::routeToGate(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait) {
    // Through the shut gate of the ring between here and there: up to its face,
    // then on against the doors (which those who can will hack at).
    if (!config_.region || !nav_) return false;
    const Region& rg = *config_.region;
    const auto& gs = rg.gates();
    std::vector<std::pair<Fixed, size_t>> choices;
    for (size_t i = 0; i < gs.size(); ++i) {
        if (gates_[i].passable()) continue;
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(gs[i].ring)];
        if (townSide(gs[i], ring, from) == withinRing(ring, to)) continue;
        const Vec3 c = (gs[i].a + gs[i].b) * Fixed::fromRatio(1, 2);
        choices.push_back({(c - from).flat().length() + (to - c).flat().length(), i});
    }
    std::stable_sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& choice : choices) {
        const Gate& g = gs[choice.second];
        const bool outside = !townSide(g, rg.ramparts()[static_cast<size_t>(g.ring)], from);
        const Vec3 c = (g.a + g.b) * Fixed::fromRatio(1, 2);
        const Vec3 front = c + g.out * (outside ? kGateApproach : -kGateApproach);
        std::vector<Vec3> route;
        if (!nav_->findPath(from, front, route, gait)) continue;
        out = std::move(route);
        out.push_back(c);
        out.push_back(to);
        return true;
    }
    return false;
}

}  // namespace illyria::sim
