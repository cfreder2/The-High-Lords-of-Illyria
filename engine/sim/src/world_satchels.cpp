// Satchel charges (docs/GAME_DESIGN.md §4.2.1, the grenadiers): carried
// into battle, five to a dwarf, and laid on the ground, where a satchel lies
// still and never goes off by itself. A blast or a Molotov's fire reaching it
// sets it off a moment later (World::explode), and its blast sets off the
// next: chains. A grenadier left to himself lays one in the path of the enemy
// coming at him and steps back out of its blast, then throws his Molotovs at
// the satchels lying among the enemy before he throws at any one man. Killed
// by fire or a blast with satchels still on him, he goes up with all of them
// at once; any other death spills them where he falls.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// He lays one when the nearest foe on the ground is within kLayReach but not
// yet within kLayNear (then it is too late: he fights or throws), and none
// lies within kLaySpacing of him already. Laying takes kLayTicks (a stoop);
// then he steps back out of its blast, kStepBackMargin beyond it.
constexpr Fixed kLayReach = Fixed::fromInt(25);
constexpr Fixed kLayNear = Fixed::fromInt(6);
constexpr Fixed kLaySpacing = Fixed::fromInt(5);
constexpr int32_t kLayTicks = kTicksPerSecond / 2;
constexpr Fixed kStepBackMargin = Fixed::fromInt(3);
// He sets off a satchel with his Molotov only from beyond its blast and this much more.
constexpr Fixed kShootMargin = Fixed::fromInt(2);
// Where a satchel is tossed when his own men crowd his feet.
constexpr Fixed kTossShort = Fixed::fromInt(5), kTossFar = Fixed::fromInt(9);
// The spark waits on a friend in the blast this long at most (as a thrown stone does: world_aoe.cpp).
constexpr int32_t kSparkWaitTicks = 5 * kTicksPerSecond;
constexpr uint32_t kSparkForgetTicks = 2 * kTicksPerSecond;

Vec3 flatDir(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

// A satchel lying laid: on the ground, its fuse unlit, not in a scavenger's throw.
bool laid(const Projectile& p) { return p.kind == ProjectileKind::Bomb && !p.thrown && p.fuse == 0; }

}  // namespace

void World::laySatchel(Unit& u, const Vec3& toward, Fixed reach) {
    if (u.satchels <= 0) return;
    const Vec3 dir = flatDir(u.pos, toward, u.facing);
    const Explosive& charge = u.stats.satchels.charge;
    if (reach <= Fixed::zero()) reach = u.stats.radius + Fixed::fromRatio(1, 2);  // at his feet, as he stoops
    placeSatchel(u.owner, u.pos + dir * reach, charge, 0);
    --u.satchels;
    u.layCooldown = u.stats.satchels.layTicks;
    u.reloadRemaining = std::max(u.reloadRemaining, kLayTicks);  // a stoop (the game plays his stroke)
    // Then back out of its blast, facing the way he laid it.
    Vec3 back = u.pos - dir * (charge.radius + kStepBackMargin);
    if (!standable(back)) back = u.pos;
    u.hasMoveTarget = true;
    u.moveTarget = back;
    u.marchSpeed = Fixed::zero();
    u.arriveFacing = dir;
    u.routeFrom = u.pos;
    planPath(u, back);
}

bool World::layInPath(Unit& u) {
    if (u.satchels <= 0 || u.layCooldown > 0 || u.inAir()) return false;
    // The nearest foe on the ground (ascending id, the first of equals).
    const Unit* foe = nullptr;
    Fixed best = kLayReach * kLayReach;
    for (const Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.inAir() || o.routed) continue;
        const Fixed d = (o.pos - u.pos).flat().lengthSq();
        if (d < best) {
            best = d;
            foe = &o;
        }
    }
    if (foe == nullptr || best < kLayNear * kLayNear) return false;
    for (const Projectile& p : projectiles_)
        if (laid(p) && (p.pos - u.pos).flat().lengthSq() < kLaySpacing * kLaySpacing) return false;
    // Where it lies keeps clear of his own men (the blast of it would take them, and any spark that finds it):
    // at his feet, or tossed out ahead of him, short of the foe. None clear: none laid.
    const Vec3 dir = flatDir(u.pos, foe->pos, u.facing);
    const Fixed foeAt = sqrt(best);
    const Explosive& charge = u.stats.satchels.charge;
    for (const Fixed reach : {Fixed::zero(), kTossShort, kTossFar}) {
        if (reach > Fixed::zero() && reach + Fixed::one() > foeAt) break;
        const Vec3 spot = u.pos + dir * (reach > Fixed::zero() ? reach : u.stats.radius + Fixed::fromRatio(1, 2));
        bool clear = true;
        nearby(spot.x, spot.y, charge.radius, [&](uint32_t k) {
            const Unit& o = units_[k];
            if (!clear || o.id == u.id || !o.alive() || o.owner != u.owner) return;
            clear = (o.pos - spot).flat().lengthSq() >= charge.radius * charge.radius;
        });
        if (!clear) continue;
        laySatchel(u, foe->pos, reach);
        return true;
    }
    return false;
}

bool World::shootSatchel(Unit& u) {
    const RangedWeapon& w = u.stats.ranged;
    // The laid satchel (his side's or theirs) with the most foes in its blast
    // and none of his friends; within his reach, and not so near he is in it.
    bool found = false;
    Vec3 at;
    int32_t most = 0;
    // And, failing that, the one with foes in it and the fewest friends, to spark at the last.
    bool blocked = false;
    Vec3 blockedAt;
    int32_t fewestFriends = 0, blockedFoes = 0;
    for (const Projectile& p : projectiles_) {
        if (!laid(p)) continue;
        const Fixed d = (p.pos - u.pos).flat().length();
        if (d > shotRange(u, w.autoRange, p.pos) || d < p.explosive.radius + kShootMargin) continue;
        const Fixed reach2 = p.explosive.radius * p.explosive.radius;
        int32_t foes = 0, friends = 0;
        for (const Unit& o : units_) {
            if (!o.alive() || o.inAir() || (o.pos - p.pos).flat().lengthSq() >= reach2) continue;
            if (o.owner == u.owner) ++friends;
            else ++foes;
        }
        if (friends > 0) {
            if (foes > 0 && (!blocked || friends < fewestFriends || (friends == fewestFriends && foes > blockedFoes))) {
                blocked = true;
                blockedAt = p.pos;
                fewestFriends = friends;
                blockedFoes = foes;
            }
            continue;
        }
        if (foes <= most) continue;
        found = true;
        most = foes;
        at = p.pos;
    }
    if (!found) {
        if (!blocked) return false;
        // The spark waits while his own men stand in the blast, up to a cap (then it goes where the fewest are; a
        // blast on his own men is no answer to a foe at his back). He does what else he can meanwhile.
        if (u.sparkHeldTick + kSparkForgetTicks < tick_) u.sparkHeld = 0;
        u.sparkHeldTick = tick_;
        if (u.sparkHeld < kSparkWaitTicks) {
            ++u.sparkHeld;
            return false;
        }
        at = blockedAt;
    }
    u.facing = flatDir(u.pos, at, u.facing);
    if (u.reloadRemaining == 0 && fire(u, at)) {
        u.reloadRemaining = attackTicks(u, w.reloadTicks);
        u.sparkHeld = 0;
    }
    return true;
}

void World::packBlast(Unit& u, bool setOff) {
    const int32_t n = u.satchels;
    u.satchels = 0;
    if (n <= 0) return;
    if (setOff) {
        // One blast for them all: a quarter wider and half again as hard for
        // each beyond the first (five: twice as wide, three times as hard).
        Explosive big = u.stats.satchels.charge;
        big.radius = big.radius * (3 + n) / 4;
        big.damage = big.damage * (1 + n) / 2;
        big.impulse = big.impulse * (3 + n) / 4;
        pendingExplosions_.push_back({u.pos + up(u.stats.height / 2), big, u.owner, ProjectileKind::Bomb, DamageType::Explosive});
        return;
    }
    // Spilled about his body, unlit: a prize for a scavenger, and a chain
    // waiting for a spark. (Into spawned_: a death may come while the
    // missiles are being moved.)
    for (int32_t k = 0; k < n; ++k) {
        const Fixed a = Fixed::fromRatio(k * 6283, n * 1000);
        Projectile p;
        p.id = nextId_++;
        p.owner = u.owner;
        p.kind = ProjectileKind::Bomb;
        p.pos = u.pos + Vec3{cos(a), sin(a), Fixed::zero()} * Fixed::fromRatio(3, 5);
        p.pos.z = surfaceBelow(p.pos.x, p.pos.y, u.pos.z + Fixed::one());
        p.resting = true;
        p.explosive = u.stats.satchels.charge;
        p.cause = DamageType::Explosive;
        spawned_.push_back(p);
    }
}

}  // namespace illyria::sim
