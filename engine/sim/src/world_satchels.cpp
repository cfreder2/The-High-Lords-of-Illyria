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

Vec3 flatDir(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

// A satchel lying laid: on the ground, its fuse unlit, not in a scavenger's throw.
bool laid(const Projectile& p) { return p.kind == ProjectileKind::Bomb && !p.thrown && p.fuse == 0; }

}  // namespace

void World::laySatchel(Unit& u, const Vec3& toward) {
    if (u.satchels <= 0) return;
    const Vec3 dir = flatDir(u.pos, toward, u.facing);
    const Explosive& charge = u.stats.satchels.charge;
    placeSatchel(u.owner, u.pos + dir * (u.stats.radius + Fixed::fromRatio(1, 2)), charge, 0);
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
    laySatchel(u, foe->pos);
    return true;
}

bool World::shootSatchel(Unit& u) {
    const RangedWeapon& w = u.stats.ranged;
    // The laid satchel (his side's or theirs) with the most foes in its blast
    // and none of his friends; within his reach, and not so near he is in it.
    bool found = false;
    Vec3 at;
    int32_t most = 0;
    for (const Projectile& p : projectiles_) {
        if (!laid(p)) continue;
        const Fixed d = (p.pos - u.pos).flat().length();
        if (d > shotRange(u, w.autoRange, p.pos) || d < p.explosive.radius + kShootMargin) continue;
        const Fixed reach2 = p.explosive.radius * p.explosive.radius;
        int32_t foes = 0;
        bool friends = false;
        for (const Unit& o : units_) {
            if (!o.alive() || o.inAir() || (o.pos - p.pos).flat().lengthSq() >= reach2) continue;
            if (o.owner == u.owner) friends = true;
            else ++foes;
        }
        if (friends || foes <= most) continue;
        found = true;
        most = foes;
        at = p.pos;
    }
    if (!found) return false;
    u.facing = flatDir(u.pos, at, u.facing);
    if (u.reloadRemaining == 0 && fire(u, at)) u.reloadRemaining = attackTicks(u, w.reloadTicks);
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
