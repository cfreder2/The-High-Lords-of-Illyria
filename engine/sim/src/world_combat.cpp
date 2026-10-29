// Battle mechanics of the melee and the missile: facing (flanks and the rear),
// riders' charges, veterans' drill, and height for archers
// (docs/GAME_DESIGN.md, battle).
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// Where a blow lands: the front is the 120 degree cone the target faces (as
// its shield covers), the rear the 120 degrees behind it, flanks between.
constexpr Fixed kFrontCos = Fixed::fromRatio(1, 2);
// Extra damage by where a blow or missile lands, in percent. A blow in the
// back is a backstab; a missile in the back finds less armour, but less so.
constexpr int32_t kFlankBlowPct = 150, kRearBlowPct = 200;
constexpr int32_t kFlankMissilePct = 130, kRearMissilePct = 160;
// Turning on the spot, per tick at 30 Hz: a man 9 degrees (270 a second), a
// horse, an elephant or anything as heavy 4 (120 a second). cos and sin.
constexpr Fixed kQuickTurnCos = Fixed::fromRatio(98769, 100000), kQuickTurnSin = Fixed::fromRatio(15643, 100000);
constexpr Fixed kSlowTurnCos = Fixed::fromRatio(99756, 100000), kSlowTurnSin = Fixed::fromRatio(6976, 100000);
constexpr Fixed kHeavyMass = Fixed::fromInt(4);
// A blow can be struck only within 45 degrees of facing the foe.
constexpr Fixed kStrikeCos = Fixed::fromRatio(7071, 10000);
// Closing in, soldiers step this far inside their reach.
constexpr Fixed kCloseIn = Fixed::fromRatio(1, 10);
// A foe is kept while within this much more than reach (bodies jostle).
constexpr Fixed kFoeSlack = Fixed::fromRatio(1, 2);
// Melee terrain: Warlords' bonuses in the open and in the hills (rise over run).
constexpr Fixed kOpenSlope = Fixed::fromRatio(3, 50);
constexpr Fixed kHillsSlope = Fixed::fromRatio(3, 20);
// High ground: striking down the slope at a foe adds this much melee damage
// per unit of grade between them (a 1 in 3 slope: +20%), within limits; blows
// struck downhill also throw harder.
constexpr int32_t kHighGroundPctPerGrade = 60;
constexpr int32_t kHighGroundPctMax = 35;
constexpr int32_t kHighGroundPctMin = -25;
// Melee blows are blocked less often than arrows: the attacker can work round the shield.
constexpr Fixed kMeleeBlockScale = Fixed::fromRatio(7, 10);
// Charges. Momentum builds toward the rider's speed by this share a tick
// (most of the way in about a second at the gallop) and is lost below a walk.
constexpr Fixed kMomentumGain = Fixed::fromRatio(1, 16);
constexpr Fixed kMomentumStop = Fixed::one();
constexpr Fixed kMomentumShed = Fixed::fromInt(8);  // m/s lost per second when slowing
// A charge needs this much momentum, and at least this share of its own speed.
constexpr Fixed kChargeMinSpeed = Fixed::fromRatio(5, 2);
constexpr Fixed kChargeSpeedShare = Fixed::fromRatio(3, 5);
// Impact damage per m/s of momentum per unit of mass (heavy cavalry at the
// gallop on the flat: 5.5 x 4 x 1.2 = 26 on top of its blow), and the throw.
constexpr Fixed kChargeDamage = Fixed::fromRatio(6, 5);
constexpr Fixed kChargeKnock = Fixed::fromRatio(1, 3);
// Ridden down by a charge shakes the bravest (world_morale.cpp).
constexpr int32_t kChargedShock = 150;
// The run in: each unit of grade downhill adds 200% (a 1 in 3 slope: x1.67, at
// most x2); uphill takes 250% a unit (a 1 in 3 slope: x0.17, at least x0.25).
constexpr int32_t kChargeDownhillPctPerGrade = 200, kChargeDownhillPctMax = 100;
constexpr int32_t kChargeUphillPctPerGrade = 250, kChargeUphillPctMin = -75;
// Veterans, per step of veterancy (a soldier's rank, a hero's level in thirds,
// each blessing): blows and reloads 7% quicker, back on their feet 15% sooner,
// 6% less damage taken. At most five steps.
constexpr int32_t kVetQuickPct = 7, kVetRecoverPct = 15, kVetDefencePct = 6, kVetMaxSteps = 5;
// Ticks a thrown soldier needs to get back up (a recruit).
constexpr int32_t kRecoverTicks = kTicksPerSecond * 2 / 3;
// Missiles reach a metre farther for each metre the shooter stands above the
// target (up to half again), and fall short by as much shooting up (a quarter at most).
constexpr Fixed kHeightRangePerMetre = Fixed::one();
// A plunging arrow: +1% damage per 0.01 of the sine of its fall beyond 0.25
// (about 15 degrees, a long flat shot), at most +25%.
constexpr Fixed kPlungeSine = Fixed::fromRatio(1, 4);
constexpr int32_t kPlungePctMax = 25;

int32_t floorPct(Fixed v) { return static_cast<int32_t>(v.floorToInt()); }

}  // namespace

HitSide hitSide(const Vec3& targetFacing, const Vec3& travel) {
    const Vec3 t = travel.flat();
    const Fixed len = t.length();
    if (len == Fixed::zero()) return HitSide::Front;
    // Against the way the target faces is the front.
    const Fixed d = -(targetFacing.dot(t) / len);
    return d >= kFrontCos ? HitSide::Front : d <= -kFrontCos ? HitSide::Rear : HitSide::Flank;
}

int32_t veteranGrade(const Unit& u) {
    if (u.stats.type == static_cast<uint8_t>(UnitType::Hero)) return std::min(3, (u.rank + 2) / 3);
    return std::clamp(u.rank, 0, 3);
}

int32_t World::veteranSteps(const Unit& u) const { return std::min(kVetMaxSteps, veteranGrade(u) + u.blessings); }

int32_t World::attackTicks(const Unit& u, int32_t baseTicks) const {
    // A wavering soldier hangs back: a quarter slower.
    const int32_t ticks = u.wavering() ? baseTicks * 5 / 4 : baseTicks;
    return std::max(1, ticks * (100 - kVetQuickPct * veteranSteps(u)) / 100);
}

int32_t World::recoverTicks(const Unit& u) const { return kRecoverTicks * (100 - kVetRecoverPct * veteranSteps(u)) / 100; }

int32_t World::defendedDamage(const Unit& u, int32_t amount) const {
    if (amount <= 0) return amount;
    return std::max(1, amount * (100 - kVetDefencePct * veteranSteps(u)) / 100);
}

int32_t World::sidePct(HitSide side, bool missile) {
    switch (side) {
        case HitSide::Front: return 100;
        case HitSide::Flank: return missile ? kFlankMissilePct : kFlankBlowPct;
        case HitSide::Rear: return missile ? kRearMissilePct : kRearBlowPct;
    }
    return 100;
}

Fixed World::shotRange(const Unit& u, Fixed base, const Vec3& target) const {
    const Fixed above = u.pos.z - target.z;
    return base + clamp(above * kHeightRangePerMetre, -base / 4, base / 2);
}

int32_t World::plungePct(const Vec3& vel) {
    const Fixed speed = vel.length();
    if (speed == Fixed::zero()) return 100;
    const Fixed sine = -vel.z / speed;
    return 100 + std::clamp(floorPct((sine - kPlungeSine) * 100), 0, kPlungePctMax);
}

bool World::turnToward(Unit& u, const Vec3& dir) const {
    const bool heavy = u.stats.mounted || u.stats.mass >= kHeavyMass;
    const Fixed c = heavy ? kSlowTurnCos : kQuickTurnCos, s = heavy ? kSlowTurnSin : kQuickTurnSin;
    if (u.facing.dot(dir) >= c) {
        u.facing = dir;
        return true;
    }
    // Rotate a step the shorter way round (left when straight behind).
    const Fixed cross = u.facing.x * dir.y - u.facing.y * dir.x;
    const Fixed sn = cross >= Fixed::zero() ? s : -s;
    const Vec3 f = u.facing;
    Vec3 turned{f.x * c - f.y * sn, f.y * c + f.x * sn, Fixed::zero()};
    const Fixed len = turned.length();
    if (len > Fixed::zero()) turned = turned / len;
    u.facing = turned;
    return u.facing.dot(dir) >= kStrikeCos;
}

Unit* World::meleeTarget(Unit& u, EntityId lastFoe) {
    if (lastFoe != kNoEntity) {
        Unit* f = mutableUnit(lastFoe);
        if (f != nullptr && f->alive() && !f->inAir() && f->owner != u.owner) {
            const Fixed gap = (f->pos - u.pos).flat().length() - u.stats.radius - f->stats.radius;
            if (gap <= u.stats.melee.reach + kFoeSlack) return f;
        }
    }
    return nearestEnemy(u, u.stats.aggroRange);
}

int32_t World::blowPct(const Unit& u, const Unit& enemy, Fixed dist, Fixed dz) const {
    int32_t pct = 100;
    const Fixed slope = slopeAt(u.pos.x, u.pos.y);
    if (slope <= kOpenSlope) pct += u.stats.terrain.openPct;
    if (slope >= kHillsSlope) pct += u.stats.terrain.hillsPct;
    if (enemy.stats.mounted) pct += u.stats.melee.bonusVsMountedPct;
    // The high ground: striking down at a foe below is easier than reaching up.
    if (dist > Fixed::zero()) pct += std::clamp(floorPct(-dz / dist * kHighGroundPctPerGrade), kHighGroundPctMin, kHighGroundPctMax);
    return std::max(10, pct + prowessPct(u) - 100);
}

void World::fight(Unit& u, Unit& enemy) {
    const Vec3 delta = (enemy.pos - u.pos).flat();
    const Fixed dist = delta.length();
    const Vec3 dir = dist > Fixed::zero() ? delta / dist : u.facing;
    const MeleeWeapon& m = u.stats.melee;
    const Fixed gap = dist - u.stats.radius - enemy.stats.radius;
    if (gap > m.reach) {
        // A little inside reach: closing on the exact edge of it never quite arrives.
        walk(u, u.pos + dir * min(gap - m.reach + kCloseIn, u.stats.moveSpeed));
        return;
    }
    u.engaged = true;
    u.foe = enemy.id;
    noteAttack(enemy, u);
    // Out of reach vertically (a flier still diving, or one above a ground unit).
    const Fixed dz = enemy.pos.z - u.pos.z;
    if (dz > u.stats.height + m.reach || -dz > enemy.stats.height + m.reach) return;
    // Squaring up to a foe at one's side or back takes a moment: no blow meanwhile.
    if (!turnToward(u, dir)) return;
    if (chargeReady(u)) {
        charge(u, enemy, dir, dist, dz);
        return;
    }
    if (u.meleeCooldown > 0) return;
    u.meleeCooldown = attackTicks(u, m.cooldownTicks);

    const Vec3 at = enemy.pos + up(enemy.stats.height * 3 / 5);
    HitSide side = hitSide(enemy.facing, dir);
    // Busy with someone else, it is off guard: a flank is as open as its back.
    if (side == HitSide::Flank && enemy.foe != kNoEntity && enemy.foe != u.id) side = HitSide::Rear;
    if (shieldBlocks(enemy, dir, kMeleeBlockScale)) {
        impacts_.push_back({ImpactEvent::Kind::Blocked, at, dir, kNoEntity, enemy.id, false, {}, -1, m.damageType});
        return;
    }
    const int32_t amount = m.damage * blowPct(u, enemy, dist, dz) / 100 * sidePct(side, false) / 100;
    const bool killed = damage(enemy, amount, &u, {m.damageType, dir, at});
    shakeBySide(enemy, side, false);
    if (m.knockback > Fixed::zero()) {
        const Fixed downhill = dist > Fixed::zero() ? clamp(-dz / dist, Fixed::zero(), Fixed::fromRatio(1, 2)) : Fixed::zero();
        knock(enemy, dir, m.knockback * (Fixed::one() + downhill));
    }
    impacts_.push_back({ImpactEvent::Kind::Hit, at, dir, kNoEntity, enemy.id, killed, {}, -1, m.damageType});
}

bool World::chargeReady(const Unit& u) const {
    return u.stats.mounted && u.momentum >= max(kChargeMinSpeed, u.stats.moveSpeed * kChargeSpeedShare);
}

void World::charge(Unit& u, Unit& enemy, const Vec3& dir, Fixed dist, Fixed dz) {
    const Fixed speed = u.momentum;
    const Fixed grade = u.chargeGrade;
    u.momentum = Fixed::zero();
    u.chargeGrade = Fixed::zero();
    u.meleeCooldown = attackTicks(u, u.stats.melee.cooldownTicks);
    // Down a slope the weight of horse and rider multiplies the blow; up one it is sapped.
    const int32_t slopePct =
        100 + (grade < Fixed::zero() ? std::min(kChargeDownhillPctMax, floorPct(-grade * kChargeDownhillPctPerGrade))
                                     : std::max(kChargeUphillPctMin, -floorPct(grade * kChargeUphillPctPerGrade)));
    const Fixed weight = speed * u.stats.mass * slopePct / 100;
    const int32_t impact = floorPct(weight * kChargeDamage);
    HitSide side = hitSide(enemy.facing, dir);
    if (side == HitSide::Flank && enemy.foe != kNoEntity && enemy.foe != u.id) side = HitSide::Rear;

    // Braced pikes, met head on: the charge runs onto their points.
    const MeleeWeapon& pike = enemy.stats.melee;
    if (side == HitSide::Front && pike.bonusVsMountedPct > 0 && pike.damage > 0 && !enemy.inAir() &&
        (!enemy.hasMoveTarget || enemy.engageFoe != kNoEntity)) {
        const int32_t amount = pike.damage * blowPct(enemy, u, dist, -dz) / 100 + impact / 2;
        const Vec3 at = u.pos + up(u.stats.height * 3 / 5);
        const bool killed = damage(u, amount, &enemy, {pike.damageType, -dir, at});
        knock(u, -dir, weight * kChargeKnock);
        impacts_.push_back({ImpactEvent::Kind::Hit, at, -dir, kNoEntity, u.id, killed, {}, -1, pike.damageType});
        return;
    }

    const Vec3 at = enemy.pos + up(enemy.stats.height * 3 / 5);
    // A shield turns the lance or sword but not the weight behind it.
    const bool blocked = shieldBlocks(enemy, dir, kMeleeBlockScale);
    const int32_t blow = blocked ? 0 : u.stats.melee.damage * blowPct(u, enemy, dist, dz) / 100;
    const int32_t amount = (blow + (blocked ? impact / 2 : impact)) * sidePct(side, false) / 100;
    const bool killed = damage(enemy, amount, &u, {u.stats.melee.damageType, dir, at});
    shakeMorale(enemy, kChargedShock * sidePct(side, false) / 100);
    knock(enemy, dir, weight * kChargeKnock * (blocked ? Fixed::fromRatio(1, 2) : Fixed::one()) + u.stats.melee.knockback);
    impacts_.push_back({blocked ? ImpactEvent::Kind::Blocked : ImpactEvent::Kind::Hit, at, dir, kNoEntity, enemy.id, killed, {}, -1,
                        u.stats.melee.damageType});
}

void World::trackMomentum(Unit& u, const Vec3& before) {
    if (!u.stats.mounted) return;
    const Fixed moved = (u.pos - before).flat().length();
    const Fixed speed = moved * kTicksPerSecond;
    if (!u.alive() || u.inAir() || u.engaged || speed < kMomentumStop) {
        u.momentum = Fixed::zero();
        u.chargeGrade = Fixed::zero();
        return;
    }
    // Speeding up builds it over a second or so; reining in sheds it quickly.
    if (speed > u.momentum) u.momentum += (speed - u.momentum) * kMomentumGain;
    else u.momentum = max(speed, u.momentum - kMomentumShed * kTickDt);
    u.momentum = min(u.momentum, u.stats.moveSpeed * Fixed::fromRatio(6, 5));
    u.chargeGrade += ((u.pos.z - before.z) / moved - u.chargeGrade) * kMomentumGain;
}

}  // namespace illyria::sim
