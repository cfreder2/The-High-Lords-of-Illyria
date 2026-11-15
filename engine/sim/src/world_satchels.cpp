// Satchel charges (docs/GAME_DESIGN.md §4.2.1, the grenadiers): carried
// into battle, five to a dwarf, and laid on the ground, where a satchel lies
// still and never goes off by itself. A blast or a Molotov's fire reaching it
// sets it off a moment later (World::explode), and its blast sets off the
// next: chains. A grenadier left to himself lays one in the path of the enemy
// coming at him and steps back out of its blast, then throws his Molotovs at
// the satchels lying among the enemy before he throws at any one man. Killed
// by fire or a blast with satchels still on him, he goes up with all of them
// at once; any other death spills them where he falls.
//
// The trap is the player's to spring (Command::Detonate): one missile, at once,
// at the satchel shown, from the nearest man who can spark it, with no waiting
// on friends in the blast. And nothing that bursts is carried without end
// (RangedWeapon::ammo): a man with a store is thrifty with it on his own.
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
constexpr Fixed kLaySpacing = Fixed::fromInt(4);
constexpr int32_t kLayTicks = kTicksPerSecond / 2;
constexpr Fixed kStepBackMargin = Fixed::fromInt(3);
// He sets off a satchel with his Molotov only from beyond its blast and this much more.
constexpr Fixed kShootMargin = Fixed::fromInt(2);
// Where a satchel is tossed when his own men crowd his feet.
constexpr Fixed kTossShort = Fixed::fromInt(5), kTossFar = Fixed::fromInt(9);
// The spark waits on a friend in the blast this long at most (as a thrown stone does: world_aoe.cpp).
constexpr int32_t kSparkWaitTicks = 5 * kTicksPerSecond;
constexpr uint32_t kSparkForgetTicks = 2 * kTicksPerSecond;
// Thrift: a missile from a store goes at this many men in its blast (a body of kThriftGreatHp or more is as many),
// or at a foe within kThriftNear of him; and at a laid satchel with kSparkFoes or more on it.
constexpr int32_t kThriftCluster = 3;
// A stone's lane: this many men in it, this far short of the mark and beyond it, this far either side of its line.
constexpr int32_t kStoneLane = 4;
constexpr Fixed kStoneShort = Fixed::fromInt(2);
constexpr Fixed kStoneRoll = Fixed::fromInt(14);
constexpr Fixed kStoneHalf = Fixed::fromRatio(3, 2);
constexpr int32_t kThriftGreatHp = 200;
constexpr Fixed kThriftNear = Fixed::fromInt(6);
constexpr int32_t kSparkFoes = 2;
// A dwarf or a wizard gives ground before a foe this near, to behind a fighting friend within kShyCover (kShyBehind
// beyond him); a foe within kShyCaught has him, and he fights.
constexpr Fixed kShyReach = Fixed::fromInt(10);
constexpr Fixed kShyCover = Fixed::fromInt(30);
constexpr Fixed kShyBehind = Fixed::fromInt(6);
constexpr Fixed kShyCaught = Fixed::fromRatio(3, 2);
// What a satchel's blast takes from the morale of those left standing within twice its reach.
constexpr int32_t kBlastShock = 150;

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
    if (douses(u.pos + dir * reach)) return;  // not in water: it would be dead
    placeSatchel(u.owner, u.pos + dir * reach, charge, 0);
    --u.satchels;
    u.layCooldown = u.stats.satchels.layTicks;
    u.reloadRemaining = std::max(u.reloadRemaining, kLayTicks);  // a stoop (the game plays his stroke)
    // With more to do (a line of them to lay, a place to go): on to it, at once.
    if (!u.queue.empty()) return;
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
        bool clear = !douses(spot);
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
        // His own side's charges: where they lie is what his side knows. (A foe's are small and easy to miss; an order
        // may be given at one he has found, and any blast that reaches it sets it off.)
        if (!laid(p) || p.owner != u.owner || douses(p.pos)) continue;
        const Fixed d = (p.pos - u.pos).flat().length();
        if (d > shotRange(u, w.autoRange, p.pos) || d < p.explosive.radius + kShootMargin) continue;
        const Fixed reach2 = p.explosive.radius * p.explosive.radius;
        int32_t foes = 0, friends = 0;
        for (const Unit& o : units_) {
            if (!o.alive() || o.inAir() || (o.pos - p.pos).flat().lengthSq() >= reach2) continue;
            if (o.owner == u.owner) ++friends;
            else if (!o.routed) foes += o.stats.maxHp >= kThriftGreatHp ? kThriftCluster : 1;
        }
        if (foes < kSparkFoes && !(foes > 0 && overrun(u))) continue;  // (one man is not worth the charge)
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

bool World::canPrepare(const Unit& u, const Vec3& at) const {
    if ((at - u.pos).flat().lengthSq() > kPrepareReach * kPrepareReach || !standable(at)) return false;
    bool clear = true;
    nearbyNotOf(u.owner, at.x, at.y, kPrepareClear, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (o.alive() && o.owner != u.owner && (o.pos - at).flat().lengthSq() < kPrepareClear * kPrepareClear) clear = false;
    });
    return clear;
}

void World::startDetonate(Unit& u, const Vec3& at) {
    // The laid satchel nearest the spot shown (the first of equals), or the ground there.
    Vec3 mark{at.x, at.y, groundHeight(at.x, at.y)};
    Fixed best = kDetonatePick * kDetonatePick;
    for (const Projectile& p : projectiles_) {
        if (!laid(p)) continue;
        const Fixed d = (p.pos - at).flat().lengthSq();
        if (d >= best) continue;
        best = d;
        mark = p.pos;
    }
    u.hasAttackTarget = true;
    u.hasMoveTarget = false;
    u.attackGate = -1;
    u.attackTarget = mark;
    u.detonate = true;
    u.path.clear();
    u.pathIndex = 0;
}

bool World::detonateStep(Unit& u) {
    if (!armed(u)) return true;  // nothing left to throw: the order is over
    const RangedWeapon& w = u.stats.ranged;
    u.facing = flatDir(u.pos, u.attackTarget, u.facing);
    const Fixed range = (u.attackTarget - u.pos).flat().length();
    if (range > shotRange(u, w.maxRange, u.attackTarget)) {
        walk(u, u.attackTarget);
        return false;
    }
    // Standing in the blast of the charge he is to light, he steps back out of it first (the others of a line are the
    // player's to mind, as his friends are).
    for (const Projectile& p : projectiles_) {
        if (!laid(p) || (p.pos - u.attackTarget).flat().lengthSq() > Fixed::one()) continue;
        const Fixed clear = p.explosive.radius + kShootMargin;
        if (range >= clear) break;
        const Vec3 to = u.pos + flatDir(u.attackTarget, u.pos, -u.facing) * (clear - range + Fixed::one());
        if (!standable(to)) break;
        walk(u, to);
        return false;
    }
    if (u.reloadRemaining > 0) return false;
    // Direct: no weighing of friends, no waiting (the player's business). A shot that cannot be made from here
    // (a wall in the way) is given up.
    // Lobbed, to come down on the charge over the heads of the men standing before it; where he cannot lob (boughs over
    // him, a wall), the flat throw if it would burst at the charge; else he goes nearer until he can do one or the other,
    // and from as near as he may go throws flat whatever is in the way.
    const Vec3 origin = u.pos + up(u.stats.height * Fixed::fromRatio(9, 10));
    const Fixed near = w.explosive.radius / 2;
    Placed how = Placed::Lobbed;
    if (!canLob(u, origin, u.attackTarget)) {
        how = Placed::Flat;
        const bool atMark = (burstPoint(u, origin, u.attackTarget) - u.attackTarget).flat().lengthSq() <= near * near;
        if (!atMark && range > w.explosive.radius + kShootMargin + Fixed::fromInt(4)) {
            walk(u, u.attackTarget);
            return false;
        }
    }
    if (fire(u, u.attackTarget, false, how)) u.reloadRemaining = attackTicks(u, w.reloadTicks);
    return true;
}

bool World::worthAShot(const Unit& u, const Unit& target) const {
    if (target.routed) return false;
    const RangedWeapon& w = u.stats.ranged;
    // The last bottle is for his satchels.
    if (u.stats.satchels.count > 0 && sparks(u.stats) && u.ammo <= 1) return false;
    // A stone on the ground is worth its throw at a column end-on, or a deep block: kStoneLane men (a great body is as
    // many) in the lane it rolls down, from just short of the mark to the end of its roll. A lone man, or a thin line
    // across its path, loses one man to it at most: the stones wait for a gate, a column, or an order. (Men on a
    // wall-walk: no roll up there, so the blast's count below.)
    if (w.kind == ProjectileKind::Stone) {
        if (WalkSpot spot; !walkSpot(target.pos, spot)) {
            const Vec3 dir = flatDir(u.pos, target.pos, u.facing);
            int32_t lane = 0;
            nearbyNotOf(u.owner, target.pos.x, target.pos.y, kStoneRoll, [&](uint32_t i) {
                const Unit& o = units_[i];
                if (!o.alive() || o.owner == u.owner || o.routed || o.inAir()) return;
                const Vec3 d = (o.pos - target.pos).flat();
                const Fixed along = d.x * dir.x + d.y * dir.y, across = abs(d.x * dir.y - d.y * dir.x);
                if (along < -kStoneShort || along > kStoneRoll || across > kStoneHalf) return;
                lane += o.stats.maxHp >= kThriftGreatHp ? kStoneLane : 1;
            });
            return lane >= kStoneLane;
        }
    }
    // Water: oil goes out in it, and a fireball is a third of itself (World::explode).
    const bool wet = douses(target.pos);
    if (wet && w.kind == ProjectileKind::Molotov) return false;
    // A Molotov (world_fire.cpp) goes at an engine not alight yet, or at kThriftCluster men standing still in its patch
    // (a great body is as many) where none burns already: men on the move walk out of it.
    if (w.kind == ProjectileKind::Molotov) {
        if (target.stats.gait == Gait::Wheeled) return target.burning == 0;
        if (inFire(target.pos) || (target.pos - u.pos).flat().lengthSq() < w.explosive.radius * w.explosive.radius) return false;
        int32_t still = 0;
        const Fixed r2 = w.explosive.radius * w.explosive.radius;
        nearbyNotOf(u.owner, target.pos.x, target.pos.y, w.explosive.radius, [&](uint32_t i) {
            const Unit& o = units_[i];
            if (!o.alive() || o.owner == u.owner || o.routed || o.inAir() || o.hasMoveTarget || (o.pos - target.pos).flat().lengthSq() >= r2) return;
            still += o.stats.maxHp >= kThriftGreatHp ? kThriftCluster : 1;
        });
        return still >= kThriftCluster;
    }
    const Fixed radius = wet && w.kind == ProjectileKind::Fireball ? w.explosive.radius / 3 : w.explosive.radius;
    // Not at a mark so near that the blast is his own death too (he gives ground, or fights with his hands).
    const Fixed near2 = (target.pos - u.pos).flat().lengthSq();
    if (near2 < radius * radius) return false;
    if (near2 < kThriftNear * kThriftNear) return true;
    const Fixed reach2 = radius * radius;
    int32_t worth = 0;
    nearbyNotOf(u.owner, target.pos.x, target.pos.y, radius, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.owner == u.owner || o.routed || (o.pos - target.pos).flat().lengthSq() >= reach2) return;
        worth += o.stats.maxHp >= kThriftGreatHp ? kThriftCluster : 1;
    });
    return worth >= kThriftCluster;
}

bool World::shyAway(Unit& u) {
    u.menacedBy = kNoEntity;
    if (u.hold || u.stationed || u.inAir()) return false;
    // The nearest foe on the ground coming at him (ascending place, the first of equals).
    const Unit* foe = nullptr;
    Fixed best = kShyReach * kShyReach;
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, kShyReach, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.owner == u.owner || o.inAir() || o.routed || o.stats.melee.damage <= 0) return;
        const Fixed d = (o.pos - u.pos).flat().lengthSq();
        if (d < best || (d == best && foe != nullptr && o.id < foe->id)) {
            best = d;
            foe = &o;
        }
    });
    if (foe == nullptr) return false;
    u.menacedBy = foe->id;
    const Fixed gap = sqrt(best) - u.stats.radius - foe->stats.radius;
    if (gap <= kShyCaught) return false;  // caught: he fights
    // Behind his guard, if he has one near; else the nearest of his side who fights hand to hand; none near: straight away.
    const Unit* cover = nullptr;
    Fixed nearest = kShyCover * kShyCover;
    bool guards = false;
    nearby(u.pos.x, u.pos.y, kShyCover, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.owner != u.owner || o.id == u.id || o.routed || o.inAir() || precious(o.stats)) return;
        if (o.stats.melee.damage <= 0 || o.stats.ranged.muzzleSpeed > Fixed::zero()) return;
        const bool mine = o.guard != kNoEntity && (o.guard == u.id || (u.squad != 0 && o.guardSquad == u.squad));
        if (guards && !mine) return;
        const Fixed d = (o.pos - u.pos).flat().lengthSq();
        if ((mine && !guards) || d < nearest || (d == nearest && cover != nullptr && o.id < cover->id)) {
            nearest = d;
            cover = &o;
            guards = guards || mine;
        }
    });
    const Vec3 away = flatDir(foe->pos, u.pos, -u.facing);
    // (Guarded, he keeps inside the ring of his guard: where he stands, with them about him. world_guard.cpp)
    if (guards) return false;
    Vec3 to = cover != nullptr ? cover->pos + flatDir(foe->pos, cover->pos, away) * kShyBehind : u.pos + away * kShyBehind;
    if (!standable(to) || douses(to)) to = u.pos + away * kShyBehind;
    if (!standable(to) || (to - u.pos).flat().lengthSq() < Fixed::one()) return false;
    walk(u, to);
    return true;
}

void World::blastShock(const Vec3& at, const Explosive& ex) {
    const Fixed reach = ex.radius * 2;
    nearby(at.x, at.y, reach, [&](uint32_t i) {
        Unit& o = units_[i];
        if (o.alive() && (o.pos - at).flat().lengthSq() < reach * reach) shakeMorale(o, kBlastShock);
    });
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
