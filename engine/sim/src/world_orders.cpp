// Orders beyond walking and shooting (docs/GAME_DESIGN.md §4.1.2): going
// after one soldier (of either side: a force attack on a friend only when
// ordered), attacking gates, walls and scenery, marching with initiative (on
// a leash from the route), the force march that ignores the enemy, and the
// choice a soldier makes when his target falls.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Marching with initiative. An enemy within this gap of a marching fighter is
// engaged (he takes the initiative); whoever struck at him in the last two
// seconds is answered within the wider reach. Neither is followed farther
// than the leash from the leg of the route he is on, and a foe that breaks
// and runs is let go once it is this far beyond his reach.
constexpr Fixed kInitiativeReach = Fixed::fromInt(5);
constexpr Fixed kRetaliateReach = Fixed::fromInt(10);
constexpr uint32_t kAttackedMemory = 2 * kTicksPerSecond;
constexpr Fixed kLeash = Fixed::fromInt(15);
constexpr Fixed kFleeChase = Fixed::fromInt(2);
// Ordered on out of a melee, he gets this long to break away before he
// answers blows again (a force march ignores them altogether).
constexpr int32_t kGraceTicks = 2 * kTicksPerSecond;
// Pursuit: within this gap (and a clear line) he closes straight in; farther,
// he follows a route, re-planned every half second once the target has moved
// a quarter of the way (at least 3 m) from its end.
constexpr Fixed kCloseIn = Fixed::fromInt(6);
constexpr uint32_t kReplanTicks = kTicksPerSecond / 2;
constexpr Fixed kReplanSlack = Fixed::fromInt(3);
// Missile troops after a soldier keep to 90% of their reach, and aim ahead
// of him by his pace times the missile's flight (distance over muzzle speed,
// a fifth more for the arc), at most 15 m.
constexpr int32_t kKeepRangePct = 90;
constexpr int32_t kLeadPct = 120;
constexpr Fixed kMaxLead = Fixed::fromInt(15);
// After the target falls: foes within this reach are a fighter's candidates
// (missile troops: their own reach), and the odds count the friends and foes
// within kSupportReach of the candidate (friends at half weight: they are
// busy). Going back is worth the troop's own odds there when it is fighting,
// kSafeBack when it is not; a marcher's mission is worth kMission.
constexpr Fixed kPressReach = Fixed::fromInt(15);
constexpr Fixed kSupportReach = Fixed::fromInt(10);
constexpr Fixed kTroopReach = Fixed::fromInt(12);
constexpr int32_t kSafeBack = 600;
constexpr int32_t kMission = 700;
// The dice: he sees his odds give or take a uniform error that narrows with
// veterancy: a recruit +-700 per mille, each step 150 less, never under 100.
constexpr int32_t kNoiseRecruit = 700, kNoisePerStep = 150, kNoiseMin = 100;
// His old place beside the friend he rejoins, at most this far from him.
constexpr Fixed kRallyOffsetMax = Fixed::fromInt(20);

Vec3 flatDir(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

Fixed gapBetween(const Unit& a, const Unit& b) { return (b.pos - a.pos).flat().length() - a.stats.radius - b.stats.radius; }

// Real fighters take the initiative; missile troops, wizards and healers only
// defend themselves.
bool takesInitiative(const UnitStats& s) {
    return s.melee.damage > 0 && s.ranged.muzzleSpeed == Fixed::zero() && s.healing.amount == 0;
}

// The flat distance from p to the segment a-b.
Fixed toSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = (b - a).flat();
    const Fixed len2 = ab.lengthSq();
    const Fixed t = len2 > Fixed::zero() ? clamp((p - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
    return (p - (a + ab * t)).flat().length();
}

}  // namespace

bool World::canBatter(const UnitStats& s) {
    if (s.ranged.muzzleSpeed == Fixed::zero()) return false;
    const ProjectileKind k = s.ranged.kind;
    return k == ProjectileKind::Stone || k == ProjectileKind::Bomb || k == ProjectileKind::Fireball;
}

int64_t World::fightingPower(const Unit& u) const {
    // Blows (or missiles) a second, in thousandths, times hit points and prowess.
    const auto rate = [](int32_t damage, int32_t ticks) -> int64_t {
        return ticks > 0 ? int64_t{damage} * 1000 * kTicksPerSecond / ticks : 0;
    };
    const RangedWeapon& r = u.stats.ranged;
    int64_t dps = rate(u.stats.melee.damage, u.stats.melee.cooldownTicks);
    if (r.muzzleSpeed > Fixed::zero()) dps = std::max(dps, rate(std::max(r.damage, r.explosive.damage), r.reloadTicks));
    const int64_t prowess = 100 + 10 * (u.rank + u.blessings);
    return std::max<int64_t>(1, dps * u.hp / 1000 * prowess / 100);
}

int32_t World::oddsAgainst(const Unit& u, const Unit& foe) const {
    int64_t mine = fightingPower(u), theirs = 0;
    const Fixed reachSq = kSupportReach * kSupportReach;
    for (const Unit& o : units_) {
        if (!o.alive() || o.routed || o.id == u.id) continue;
        if ((o.pos - foe.pos).flat().lengthSq() > reachSq) continue;
        if (o.owner == u.owner) mine += fightingPower(o) / 2;
        else if (o.owner == foe.owner) theirs += fightingPower(o);
    }
    return static_cast<int32_t>(mine * 1000 / std::max<int64_t>(1, mine + theirs));
}

void World::noteAttack(Unit& target, const Unit& by) {
    // Only a marcher needs it (an idle soldier fights whoever is nearest).
    if (!target.hasMoveTarget || target.forceMarch || by.owner == target.owner) return;
    target.lastAttacker = by.id;
    target.attackedTick = tick_;
}

void World::applyOrder(Unit& u, const Command& cmd) {
    // Every order replaces the last: no pursuit, no fight on the march.
    const bool inMelee = u.foe != kNoEntity || u.engaged;
    u.targetUnit = kNoEntity;
    u.targetChosen = false;
    u.rally = kNoEntity;
    u.rallyOffset = {};
    u.engageFoe = kNoEntity;
    u.lastAttacker = kNoEntity;
    u.attackedTick = 0;
    u.forceMarch = false;
    u.graceTicks = 0;
    switch (cmd.type) {
        case Command::Type::Move:
            u.forceMarch = cmd.force;
            u.routeFrom = u.pos;
            if (inMelee) u.graceTicks = kGraceTicks;
            break;
        case Command::Type::AttackUnit: {
            const Unit* t = findUnit(cmd.targetUnit);
            if (t == nullptr || !t->alive() || t->id == u.id) return;
            if (u.stats.melee.damage == 0 && u.stats.ranged.muzzleSpeed == Fixed::zero()) return;
            u.targetUnit = t->id;
            u.targetSeen = t->pos;
            u.hasMoveTarget = false;
            u.hasAttackTarget = false;
            u.attackGate = -1;
            u.path.clear();
            u.pathIndex = 0;
            if (const Unit* r = findUnit(cmd.rally); r != nullptr && r->alive() && r->owner == u.owner && r->id != u.id) {
                u.rally = r->id;
                Vec3 off = (u.pos - r->pos).flat();
                if (off.length() > kRallyOffsetMax) off = off / off.length() * kRallyOffsetMax;
                u.rallyOffset = off;
            }
            break;
        }
        case Command::Type::AttackObject: {
            if (!config_.region) return;
            const Region& rg = *config_.region;
            if (cmd.object == Command::Object::Gate) {
                if (cmd.objectIndex < 0 || static_cast<size_t>(cmd.objectIndex) >= rg.gates().size()) return;
                const size_t gi = static_cast<size_t>(cmd.objectIndex);
                if (gates_[gi].broken()) return;
                const Gate& g = rg.gates()[gi];
                if (u.stats.ranged.muzzleSpeed > Fixed::zero()) {
                    // Missile troops and engines shoot at the doors.
                    u.hasAttackTarget = true;
                    u.hasMoveTarget = false;
                    u.attackGate = cmd.objectIndex;
                    u.attackTarget = gateAimPoint(cmd.objectIndex, u.pos);
                } else if (u.stats.melee.damage > 0) {
                    // The rest go at it: ordered through, they hack at it until it gives.
                    const Vec3 centre = (g.a + g.b) * Fixed::fromRatio(1, 2);
                    const bool outside = (u.pos - centre).flat().dot(g.out) >= Fixed::zero();
                    u.hasMoveTarget = true;
                    u.hasAttackTarget = false;
                    u.attackGate = -1;
                    u.moveTarget = centre + g.out * Fixed::fromInt(outside ? -6 : 6);
                    u.moveTarget.z = groundHeight(u.moveTarget.x, u.moveTarget.y);
                    u.marchSpeed = Fixed::zero();
                    u.arriveFacing = {};
                    u.routeFrom = u.pos;
                    planPath(u, u.moveTarget);
                }
                return;
            }
            // Walls, houses and trees: only stones, satchels and fireballs harm
            // anything there (the men on the wall-walk, those sheltering behind
            // the house or under the trees). The masonry and timber stand.
            if (!canBatter(u.stats)) return;
            Vec3 aim = cmd.target;
            if (cmd.object == Command::Object::Wall) {
                if (cmd.objectIndex < 0 || static_cast<size_t>(cmd.objectIndex) >= rg.ramparts().size()) return;
                const Rampart& ring = rg.ramparts()[static_cast<size_t>(cmd.objectIndex)];
                // The point of the wall-walk nearest the spot shown.
                Fixed best = Fixed::fromInt(1000000);
                for (size_t e = 0; e < ring.corners.size(); ++e) {
                    const Vec3& a = ring.corners[e];
                    const Vec3& b = ring.corners[(e + 1) % ring.corners.size()];
                    const Vec3 ab = (b - a).flat();
                    const Fixed len2 = ab.lengthSq();
                    const Fixed t = len2 > Fixed::zero() ? clamp((cmd.target - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
                    const Vec3 p = a + ab * t;
                    const Fixed d = (p - cmd.target).flat().length();
                    if (d < best) {
                        best = d;
                        aim = {p.x, p.y, a.z};
                    }
                }
            } else {
                if (cmd.objectIndex < 0 || static_cast<size_t>(cmd.objectIndex) >= rg.obstacles().size()) return;
                const Obstacle& o = rg.obstacles()[static_cast<size_t>(cmd.objectIndex)];
                // Its near side, at the ground: a stone or satchel falls among those beside it.
                const Vec3 toward = flatDir(o.pos, u.pos, u.facing);
                const Fixed reach = o.kind == Obstacle::Kind::House ? max(o.halfX, o.halfY) : o.radius;
                aim = o.pos + toward * reach;
                aim.z = groundHeight(aim.x, aim.y);
            }
            u.hasAttackTarget = true;
            u.hasMoveTarget = false;
            u.attackGate = -1;
            u.attackTarget = aim;
            break;
        }
        case Command::Type::AttackGround:
        case Command::Type::AttackGate:
        case Command::Type::Stop:
            break;
    }
}

Fixed World::offRoute(const Unit& u, const Vec3& p) const {
    const Vec3 to = u.path.empty() ? u.moveTarget : u.path[std::min<size_t>(u.pathIndex, u.path.size() - 1)];
    return toSegment(p, u.routeFrom, to);
}

Unit* World::crosser(Unit& u, Fixed reach) {
    const bool groundBound = u.stats.ranged.muzzleSpeed == Fixed::zero();
    const Fixed reachUp = u.stats.height + u.stats.melee.reach;
    Unit* best = nullptr;
    Fixed bestGap;
    // Ascending id order, strict comparison: ties go to the lowest id.
    for (Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.routed || o.inAir()) continue;
        if (groundBound && o.pos.z - u.pos.z > reachUp) continue;
        const Fixed gap = gapBetween(u, o);
        if (gap > reach || (best != nullptr && gap >= bestGap)) continue;
        if (offRoute(u, o.pos) > kLeash) continue;
        best = &o;
        bestGap = gap;
    }
    return best;
}

bool World::marchInitiative(Unit& u) {
    if (u.forceMarch || u.stats.melee.damage == 0 || u.inAir() || u.flight != Unit::Flight::Grounded) return false;
    if (u.graceTicks > 0) {
        --u.graceTicks;
        return false;
    }
    Unit* foe = u.engageFoe != kNoEntity ? mutableUnit(u.engageFoe) : nullptr;
    if (foe != nullptr) {
        // Mission first: never beyond the leash, never after a foe that runs.
        const Fixed gap = gapBetween(u, *foe);
        bool keep = foe->alive() && !foe->inAir() && gap <= kRetaliateReach && offRoute(u, u.pos) <= kLeash;
        if (keep && foe->routed && gap > u.stats.melee.reach + kFleeChase) keep = false;
        if (!keep) {
            const bool fell = !foe->alive();
            u.engageFoe = kNoEntity;
            foe = nullptr;
            if (fell) {
                targetDown(u, true);
                if (u.engageFoe != kNoEntity) foe = mutableUnit(u.engageFoe);
            }
            if (foe == nullptr) {
                // Back to the march: the way to the route may now be round something.
                if (nav_ && !u.path.empty() && !nav_->clearLine(u.pos, u.path[std::min<size_t>(u.pathIndex, u.path.size() - 1)]))
                    planPath(u, u.moveTarget);
                return false;
            }
        }
    }
    if (foe == nullptr) {
        // Struck at: answer him. Else whoever crosses his path, if he is a fighter.
        if (u.lastAttacker != kNoEntity && tick_ - u.attackedTick <= kAttackedMemory) {
            Unit* a = mutableUnit(u.lastAttacker);
            if (a != nullptr && a->alive() && !a->routed && !a->inAir() && gapBetween(u, *a) <= kRetaliateReach &&
                offRoute(u, a->pos) <= kLeash)
                foe = a;
        }
        if (foe == nullptr && takesInitiative(u.stats)) foe = crosser(u, kInitiativeReach);
        if (foe == nullptr) return false;
        u.engageFoe = foe->id;
    }
    fight(u, *foe);
    return true;
}

void World::targetDown(Unit& u, bool marching) {
    const bool ranged = !marching && u.stats.ranged.muzzleSpeed > Fixed::zero();
    const Fixed reach = marching ? kRetaliateReach : ranged ? max(u.stats.ranged.autoRange, kPressReach) : kPressReach;
    // The nearest foe still standing (a fighter on the march: within his leash).
    Unit* cand = nullptr;
    Fixed bestGap;
    for (Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.routed || o.inAir()) continue;
        const Fixed gap = gapBetween(u, o);
        if (gap > reach || (cand != nullptr && gap >= bestGap)) continue;
        if (marching && offRoute(u, o.pos) > kLeash) continue;
        cand = &o;
        bestGap = gap;
    }
    // Going back: to the march, to the friend he left (his old place beside
    // him), or, with neither, standing his ground.
    const Unit* rally = !marching && u.rally != kNoEntity ? findUnit(u.rally) : nullptr;
    if (rally != nullptr && !rally->alive()) rally = nullptr;
    int32_t back = marching ? kMission : kSafeBack;
    Vec3 home;
    if (rally != nullptr) {
        home = rally->pos + u.rallyOffset;
        int64_t mine = fightingPower(u), theirs = 0;
        for (const Unit& o : units_) {
            if (!o.alive() || o.routed || o.id == u.id || (o.pos - home).flat().lengthSq() > kTroopReach * kTroopReach) continue;
            if (o.owner == u.owner) mine += fightingPower(o);
            else theirs += fightingPower(o);
        }
        // The troop is fighting: helping it is worth its odds with him.
        if (theirs > 0) back = static_cast<int32_t>(mine * 1000 / (mine + theirs));
    }
    OrderDecision d{u.id, u.owner, false, kNoEntity, 0};
    if (cand != nullptr) {
        d.odds = oddsAgainst(u, *cand);
        // The dice: what he makes of his chances.
        const int32_t noise = std::max(kNoiseMin, kNoiseRecruit - kNoisePerStep * veteranSteps(u));
        const int32_t seen = d.odds + static_cast<int32_t>(rng_.nextBelow(static_cast<uint32_t>(2 * noise + 1))) - noise;
        d.pressOn = seen > back;
        if (d.pressOn) d.target = cand->id;
    }
    if (!marching || cand != nullptr) decisions_.push_back(d);
    u.targetUnit = kNoEntity;
    u.targetChosen = false;
    if (d.pressOn) {
        if (marching) {
            u.engageFoe = cand->id;
        } else {
            u.targetUnit = cand->id;
            u.targetSeen = cand->pos;
            u.targetChosen = true;
            u.path.clear();
            u.pathIndex = 0;
        }
        return;
    }
    if (marching) return;  // marches on
    u.path.clear();
    u.pathIndex = 0;
    if (rally != nullptr) {
        // Back to his place beside the friend he left.
        u.hasMoveTarget = true;
        u.moveTarget = {home.x, home.y, groundHeight(home.x, home.y)};
        u.marchSpeed = Fixed::zero();
        u.arriveFacing = rally->facing;
        u.routeFrom = u.pos;
        planPath(u, u.moveTarget);
    }
    u.rally = kNoEntity;
}

void World::pursue(Unit& u) {
    Unit* t = mutableUnit(u.targetUnit);
    if (t == nullptr || !t->alive()) {
        targetDown(u, false);
        if (u.targetUnit == kNoEntity) return;
        t = mutableUnit(u.targetUnit);
    }
    // How he moved since last tick (thrown bodies aside), to shoot ahead of him.
    Vec3 stride = (t->pos - u.targetSeen).flat();
    if (t->inAir() || stride.length() > Fixed::one()) stride = {};
    u.targetSeen = t->pos;
    const Vec3 delta = (t->pos - u.pos).flat();
    const Fixed dist = delta.length();
    const Fixed gap = dist - u.stats.radius - t->stats.radius;
    // One he chose himself he lets go once it breaks and runs.
    if (u.targetChosen && t->routed && gap > u.stats.melee.reach + kFleeChase) {
        u.targetUnit = kNoEntity;
        u.targetChosen = false;
        return;
    }
    const Vec3 dir = dist > Fixed::zero() ? delta / dist : u.facing;
    const auto approach = [&] {
        const bool due = (tick_ + u.id) % kReplanTicks == 0;
        if (u.path.empty() || (due && (u.path.back() - t->pos).flat().length() > max(kReplanSlack, dist / 4))) {
            planPath(u, t->pos);
            u.routeFrom = u.pos;
        }
        if (followPath(u, Fixed::one())) u.path.clear();  // there, and he has moved on: plan afresh
    };
    if (u.stats.ranged.muzzleSpeed > Fixed::zero()) {
        // Missile troops keep to a good bowshot and loose at him.
        const Fixed range = shotRange(u, u.stats.ranged.maxRange, t->pos) * kKeepRangePct / 100;
        if (dist > range) {
            approach();
        } else if (dist < u.stats.ranged.minRange) {
            if (u.stats.melee.damage > 0) fight(u, *t);
            else walk(u, u.pos - dir * Fixed::fromInt(4));  // too near for the engine: back off
        } else {
            u.facing = dir;
            // Ahead of a walking man, by about the missile's time in flight.
            Vec3 lead = stride * (dist / u.stats.ranged.muzzleSpeed * kLeadPct / 100 * kTicksPerSecond);
            if (lead.length() > kMaxLead) lead = lead / lead.length() * kMaxLead;
            const Vec3 aim = t->pos + lead;
            if (u.reloadRemaining == 0) {
                if (fire(u, aim)) u.reloadRemaining = attackTicks(u, u.stats.ranged.reloadTicks);
                else approach();  // no clear shot from here (a wall, a house): closer
            }
        }
        return;
    }
    if (gap <= kCloseIn && (!nav_ || nav_->clearLine(u.pos, t->pos))) {
        u.path.clear();
        fight(u, *t);
    } else {
        approach();
    }
}

}  // namespace illyria::sim
