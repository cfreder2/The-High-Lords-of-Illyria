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
// Myth's rule: no man is ringed by a crowd. A foe takes only so many men's blows at once (meleeCap); the rest take another
// foe within kFlankSlack of the nearest, or, with none, stand off this far from his reach and wait for a place.
constexpr Fixed kFlankSlack = Fixed::fromInt(3);
constexpr Fixed kStandOff = Fixed::fromInt(2);
// Closing on a foe, within this of his reach a man heads for a free place on his ring (ringSlot) rather than for his middle; a friend
// within kFlankCrowd of a place counts kCrowdCost metres against it.
constexpr Fixed kSlotRange = Fixed::fromInt(4), kFlankCrowd = Fixed::fromRatio(5, 2), kCrowdCost = Fixed::fromRatio(3, 4);
constexpr Fixed kRingShare = Fixed::fromRatio(3, 1);  // of the ring round a foe, a third is the front
constexpr Fixed kSlotBehind = Fixed::fromRatio(1, 4);  // (cos of about 105 degrees: how far round a foe a man looks for his place)
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
// (What a charge does to the nerve of the man ridden down, by where it lands: chargeShock, world_tactics.cpp.)
// Braced pikes: their blow on the horse that runs onto them, in percent, and what it does to the rider's nerve.
constexpr int32_t kBracedPct = 150, kBracedShock = 250;
// Any other foot standing formed (bracedFoot) and met head on: this share of the impact lands, and of the pike's shock on the rider.
constexpr int32_t kBracedFootImpactPct = 25;
// A blow from this much above (hundredths of grade: 0.06, about 1 in 16) is told as the high ground's.
constexpr int32_t kHighToldGrade = 6;
// A man wading, struck by a man on dry ground: what each such blow does to his nerve.
constexpr int32_t kFordShock = 40;
// Striking up a slope steeper than kHighToldGrade he is slower to strike again, by this share (the climb tires him).
constexpr int32_t kUphillSlowPct = 25;
// A rider who has charged home is out of order this long, and wheels away this long to come again (not one told to hold).
constexpr int32_t kChargeDisorder = 3 * kTicksPerSecond, kWheelTicks = 5 * kTicksPerSecond / 2;
// A charge down a slope is told when the slope added this much (percent).
constexpr int32_t kDownhillTold = 20;
// The run in: each unit of grade downhill adds 200% (a 1 in 3 slope: x1.67, at
// most x2); uphill takes 250% a unit (a 1 in 3 slope: x0.17, at least x0.25).
constexpr int32_t kChargeDownhillPctPerGrade = 200, kChargeDownhillPctMax = 100;
constexpr int32_t kChargeUphillPctPerGrade = 250, kChargeUphillPctMin = -75;
// Veterans, per step of veterancy (a soldier's rank, a hero's level in thirds,
// each blessing): back on their feet 15% sooner. Each blessing (the gods'
// favour, and the artifacts the conquest counts with them; not a rank, whose
// worth in a fight is a small edge: world_edges.cpp): blows and reloads 7%
// quicker and 6% less damage taken. At most five steps.
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

// A moving mark: the shooter leads a man by his pace times the missile's flight, misjudging it (kLeadLowPct to
// kLeadHighPct of the true lead, evenly), and aims within a circle wider by kMovingSpreadPct of the way the man goes
// while it flies; never more than kMaxLead ahead. A stone flies its steep arc, a third longer than a bowshot's time.
constexpr int32_t kLeadLowPct = 60, kLeadHighPct = 110, kMovingSpreadPct = 30;
constexpr Fixed kMaxLead = Fixed::fromInt(15);

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
    return std::max(1, ticks * (100 - kVetQuickPct * std::min(kVetMaxSteps, u.blessings)) / 100);
}

bool World::readyToLoose(Unit& u) {
    if (const int32_t draw = u.stats.ranged.drawTicks; draw > 0) {
        // Drawn on if he held this mark last tick, else drawn afresh: after a
        // march, a melee or a lull his reload may be long done, but not his draw.
        u.drawn = u.drawTick + 1 == tick_ ? std::min(u.drawn + 1, draw) : 1;
        u.drawTick = tick_;
        if (u.drawn < draw) return false;
    }
    return u.reloadRemaining == 0;
}

int32_t World::recoverTicks(const Unit& u) const { return kRecoverTicks * (100 - kVetRecoverPct * veteranSteps(u)) / 100; }

int32_t World::defendedDamage(const Unit& u, int32_t amount) const {
    if (amount <= 0) return amount;
    return std::max(1, amount * (100 - kVetDefencePct * std::min(kVetMaxSteps, u.blessings)) / 100);
}

int32_t World::sidePct(HitSide side, bool missile) {
    switch (side) {
        case HitSide::Front: return 100;
        case HitSide::Flank: return missile ? kFlankMissilePct : kFlankBlowPct;
        case HitSide::Rear: return missile ? kRearMissilePct : kRearBlowPct;
    }
    return 100;
}

Vec3 World::leadMark(const Unit& u, const Unit& t, Fixed& spread) {
    spread = Fixed::zero();
    const Vec3 vel = t.stride * Fixed::fromInt(kTicksPerSecond);
    const Fixed speed = vel.length(), muzzle = u.stats.ranged.muzzleSpeed;
    if (speed == Fixed::zero() || muzzle <= Fixed::zero()) return t.pos;
    const Fixed dist = (t.pos - u.pos).flat().length();
    const Fixed flight = dist / muzzle * (u.stats.ranged.kind == ProjectileKind::Stone ? Fixed::fromRatio(5, 3) : Fixed::fromRatio(11, 10));
    const int32_t judged = kLeadLowPct + static_cast<int32_t>(rng_.nextBelow(static_cast<uint32_t>(kLeadHighPct - kLeadLowPct + 1)));
    Vec3 lead = vel * (flight * judged / 100);
    if (lead.length() > kMaxLead) lead = lead / lead.length() * kMaxLead;
    spread = speed * flight * kMovingSpreadPct / 100;
    return t.pos + lead;
}

Vec3 World::chestOf(const Unit& u, const Unit& t, const Vec3& at) {
    return u.stats.ranged.kind == ProjectileKind::Arrow ? at + up(t.stats.height * 3 / 5) : at;
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
    // (A formed body wheels as slowly as horse: its men keep their places as they turn.)
    const bool heavy = u.stats.mounted || u.stats.mass >= kHeavyMass || turnsSlowly(u);
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

HitSide World::sideStruck(Unit& target, const Vec3& dir, EntityId by, bool& busy, bool& behind, bool charge) {
    HitSide side = hitSide(target.facing, dir);
    const bool closed = charge && formed(target) && formationRule(target.formation).tight;
    if (side != HitSide::Front && allRound(target)) {
        // A square has no flank.
        side = HitSide::Front;
        tell(TacticEvent::Kind::Square, target);
    } else if (side == HitSide::Flank && closed && target.facing.dot(dir) <= Fixed::zero()) {
        // A charge into closed ranks from anywhere ahead of the line meets its front: the men at his shoulder cover him.
        side = HitSide::Front;
    }
    behind = side == HitSide::Rear;  // truly at his back
    // Busy with someone else, he is off guard: a flank is as open as his back (a rider's charge excepted into closed ranks).
    busy = target.foe != kNoEntity && target.foe != by;
    if (side == HitSide::Flank && busy && !closed) side = HitSide::Rear;
    return side;
}

int32_t World::meleeCap(const Unit& u, const Unit& foe) const {
    // The men that stand shoulder to shoulder across a third of the ring at which `u` strikes `foe`: three on a man, a few more
    // on a giant, never fewer than two (a pike's long reach rings a wider circle but a rank still strikes past its friends).
    const Fixed ring = foe.stats.radius + u.stats.radius + u.stats.melee.reach;
    Fixed abreast = ring * kPi / (kRingShare * u.stats.radius);
    // A long weapon (a pike) is struck past the friend in front: the rank behind adds nothing, so fewer stand in the front.
    if (u.stats.melee.reach > Fixed::one()) abreast = abreast / u.stats.melee.reach;
    const int64_t slots = abreast.floorToInt();
    return static_cast<int32_t>(std::clamp<int64_t>(slots, 2, 6));
}

bool World::meleeFull(const Unit& u, const Unit& foe) const {
    // Men of the other side now striking him (each man's `foe` is his latest: this tick's for those already moved).
    const int32_t cap = meleeCap(u, foe);
    int32_t on = 0;
    const Fixed look = foe.stats.radius + u.stats.melee.reach + u.stats.radius + kFoeSlack + Fixed::fromInt(1);
    bool full = false;
    nearby(foe.pos.x, foe.pos.y, look, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (full || o.id == u.id || o.owner == foe.owner || !o.alive() || o.foe != foe.id) return;
        full = ++on >= cap;
    });
    return full;
}

bool World::ringSlot(const Unit& u, const Unit& foe, Vec3& slot) const {
    // Eight places round the foe at his reach (evenly by angle), the nearest to him that no friend holds or crowds, so that a line
    // closing on one man wraps him instead of piling at the point it met him.
    static constexpr Fixed kD = Fixed::fromRatio(7071, 10000);
    static const Vec3 dirs[8] = {{Fixed::one(), Fixed::zero(), Fixed::zero()}, {kD, kD, Fixed::zero()},
                                 {Fixed::zero(), Fixed::one(), Fixed::zero()}, {-kD, kD, Fixed::zero()},
                                 {-Fixed::one(), Fixed::zero(), Fixed::zero()}, {-kD, -kD, Fixed::zero()},
                                 {Fixed::zero(), -Fixed::one(), Fixed::zero()}, {kD, -kD, Fixed::zero()}};
    const Fixed radius = foe.stats.radius + u.stats.radius + u.stats.melee.reach - kCloseIn;
    // (A place a friend's body and his room are in is taken.)
    const Fixed width = u.stats.radius * 2 + Fixed::fromRatio(2, 5);
    // Only the places on his own side of the foe (within about 100 degrees of the way he comes from): the ends of a wider line
    // wrap a foe's flank, the ranks behind a front wait their turn behind it; nobody walks round a man to his back.
    const Vec3 from = (u.pos - foe.pos).flat();
    const Fixed fromLen = from.length();
    const Vec3 bearing = fromLen > Fixed::zero() ? from / fromLen : -foe.facing;
    Vec3 at[8];
    Fixed crowd[8] = {};
    bool taken[8] = {};
    for (int k = 0; k < 8; ++k) at[k] = foe.pos + dirs[k] * radius;
    nearby(foe.pos.x, foe.pos.y, radius + kFlankCrowd, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (o.id == u.id || o.owner != u.owner || !o.alive() || o.inAir()) return;
        for (int k = 0; k < 8; ++k) {
            const Fixed d = (o.pos - at[k]).flat().length();
            if (d < width) taken[k] = true;
            if (d < kFlankCrowd) crowd[k] += Fixed::one();
        }
    });
    int best = -1;
    Fixed bestScore;
    for (int k = 0; k < 8; ++k) {
        if (taken[k] || dirs[k].dot(bearing) < -kSlotBehind) continue;
        const Fixed score = (at[k] - u.pos).flat().length() + crowd[k] * kCrowdCost;
        if (best < 0 || score < bestScore) best = k, bestScore = score;
    }
    if (best < 0) return false;
    slot = at[best];
    return true;
}

Unit* World::meleeTarget(Unit& u, EntityId lastFoe) {
    if (lastFoe != kNoEntity) {
        Unit* f = mutableUnit(lastFoe);
        if (f != nullptr && f->alive() && !f->inAir() && f->owner != u.owner) {
            const Fixed gap = (f->pos - u.pos).flat().length() - u.stats.radius - f->stats.radius;
            if (gap <= u.stats.melee.reach + kFoeSlack) return f;
        }
    }
    Unit* near = nearestEnemy(u, u.stats.aggroRange);
    if (near == nullptr) return nullptr;
    const Fixed nearGap = (near->pos - u.pos).flat().length() - u.stats.radius - near->stats.radius;
    if (nearGap > u.stats.melee.reach + kFlankSlack || !meleeFull(u, *near)) return near;
    // His nearest is ringed already: the nearest of the rest that is not, within a few strides more.
    Unit* open = nullptr;
    Fixed openDist;
    uint32_t openIndex = 0;
    const Fixed range = std::min(u.stats.aggroRange, nearGap + u.stats.radius + near->stats.radius + kFlankSlack);
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, range, [&](uint32_t i) {
        Unit& o = units_[i];
        if (!o.alive() || o.inAir() || o.owner == u.owner || o.id == near->id) return;
        const Fixed d = (o.pos - u.pos).flat().length();
        if (open != nullptr && (d > openDist || (d == openDist && i > openIndex))) return;
        if (meleeFull(u, o)) return;
        open = &o;
        openDist = d;
        openIndex = i;
    });
    return open != nullptr ? open : near;  // none: he waits on the nearest, standing off (fight)
}

int32_t World::blowPct(const Unit& u, const Unit& enemy, Fixed dist, Fixed dz) const {
    // The weapon against riders, a hero's levels and blessings, and the
    // small edges (world_edges.cpp): the ground, the rank, a hero near, and
    // the high ground over this foe.
    // ... and position (world_tactics.cpp): wading, and the slope between them.
    int32_t pct = gracePct(u) + edgesOf(u, dist, dz).total() + positionPct(u, enemy, dist, dz);
    if (enemy.stats.mounted) pct += u.stats.melee.bonusVsMountedPct;
    return std::max(10, pct);
}

void World::fight(Unit& u, Unit& enemy) {
    const Vec3 delta = (enemy.pos - u.pos).flat();
    const Fixed dist = delta.length();
    const Vec3 dir = dist > Fixed::zero() ? delta / dist : u.facing;
    const MeleeWeapon& m = u.stats.melee;
    const Fixed gap = dist - u.stats.radius - enemy.stats.radius;
    if (gap > m.reach) {
        if (shutGateBetween(u, enemy.pos)) return;  // not at a foe the other side of a shut gate (he holds his place)
        // A rider at the gallop rides straight at his man: a charge is no place in the ring, and wheeling to one at the last
        // stride would lose it (he must be facing his man to strike: turnToward, below).
        const bool charging = chargeReady(u);
        // A foe ringed by his full share: no pushing in behind them. He closes to a stand-off and waits his turn, facing him.
        if (!charging && gap <= m.reach + kStandOff && meleeFull(u, enemy)) {
            turnToward(u, dir);
            return;
        }
        // Near him, to a free place on his ring: the line wraps him (and the foe of the next man is not the same man's front).
        if (!charging && gap <= m.reach + kSlotRange) {
            if (Vec3 slot; ringSlot(u, enemy, slot)) {
                walk(u, slot);
                return;
            }
        }
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
    // Up a slope: weaker (positionPct) and slower to strike again.
    if (dist > Fixed::zero() && dz * 100 >= dist * kHighToldGrade) {
        u.meleeCooldown = u.meleeCooldown * (100 + kUphillSlowPct) / 100;
        tell(TacticEvent::Kind::Uphill, u);
    }

    const Vec3 at = enemy.pos + up(enemy.stats.height * 3 / 5);
    bool busyAside = false, behind = false;
    const HitSide side = sideStruck(enemy, dir, u.id, busyAside, behind, false);
    // Position, told (world_tactics.cpp): a man caught wading or mired, a blow from well above.
    const Footing his = footing(enemy), mine = footing(u);
    if (his == Footing::Ford && mine != Footing::Ford) {
        tell(TacticEvent::Kind::Ford, enemy);
        shakeMorale(enemy, kFordShock);  // struck from the bank with the water round his knees
    } else if (his == Footing::Mire && mine == Footing::Firm) {
        tell(TacticEvent::Kind::Mire, enemy);
    } else if (dist > Fixed::zero() && -dz * 100 >= dist * kHighToldGrade) {
        tell(TacticEvent::Kind::HighGround, enemy);
    }
    ambush(u, enemy);
    // (A man in the water cannot keep his shield up.)
    if (shieldBlocks(enemy, dir, his == Footing::Ford ? kMeleeBlockScale / 2 : kMeleeBlockScale)) {
        impacts_.push_back({ImpactEvent::Kind::Blocked, at, dir, kNoEntity, enemy.id, false, {}, -1, m.damageType});
        return;
    }
    // ... and the formations (world_formation.cpp): his own where the blow lands, and the one it lands on.
    const int32_t amount = scaledDamage(m.damage, std::max(10, blowPct(u, enemy, dist, dz) + formBlowPct(u, side)),
                                        sidePct(side, false) * (100 + formStruckPct(enemy, side, false)) / 100);
    const bool killed = damage(enemy, amount, &u, {m.damageType, dir, at});
    if (side != HitSide::Front) enemy.flankTick = tick_ + 1;
    shakeBySide(enemy, side, false);
    // Struck in the flank or back while he fights another to his front: his whole squad feels it (world_tactics.cpp).
    if (side != HitSide::Front && busyAside) flankedInMelee(enemy, side);
    if (m.knockback > Fixed::zero()) {
        const Fixed downhill = dist > Fixed::zero() ? clamp(-dz / dist, Fixed::zero(), Fixed::fromRatio(1, 2)) : Fixed::zero();
        knock(enemy, dir, m.knockback * (Fixed::one() + downhill));
    }
    impacts_.push_back({ImpactEvent::Kind::Hit, at, dir, kNoEntity, enemy.id, killed, {}, -1, m.damageType});
}

bool World::bracedFoot(const Unit& u) const {
    // On foot, standing (not on the march), in closed ranks that hold (formed, a tight formation), on firm ground.
    return !u.stats.mounted && u.stats.melee.damage > 0 && formed(u) && formationRule(u.formation).tight && footing(u) == Footing::Firm &&
           (!u.hasMoveTarget || u.engageFoe != kNoEntity) && u.stride.lengthSq() * 4 < Fixed::fromRatio(1, 100);
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
    // (A square's front meets it all round, and its pikes are braced.)
    bool busy = false, behind = false;
    const HitSide side = sideStruck(enemy, dir, u.id, busy, behind, true);
    tell(TacticEvent::Kind::Charge, enemy, static_cast<int32_t>(side));
    if (slopePct >= 100 + kDownhillTold) tell(TacticEvent::Kind::Downhill, enemy);
    ambush(u, enemy);
    // Charged home: out of order. He does not stand in the melee to die: away to re-form and come again (unless told to
    // hold). Only a front that stood braced for him (below) holds him there, where he has made his bed.
    disorder(u, kChargeDisorder);
    const bool braced = side == HitSide::Front && bracedFoot(enemy);
    if (!u.hold && !braced) {
        u.wheel = kWheelTicks;
        u.wheelDir = -dir;
    }

    // Braced pikes, met head on: the charge runs onto their points.
    const MeleeWeapon& pike = enemy.stats.melee;
    // (Not pikes out of order, nor in water, mud or woods: no brace without firm ground and closed ranks.)
    if (side == HitSide::Front && pike.bonusVsMountedPct > 0 && pike.damage > 0 && !enemy.inAir() && enemy.disorder == 0 && footing(enemy) == Footing::Firm &&
        (!enemy.hasMoveTarget || enemy.engageFoe != kNoEntity)) {
        // The horse's own weight is what drives it on: the whole of the impact, and the pike's blow half again. The
        // charge is stopped cold (thrown back, his nerve shaken), and the pikeman takes nothing.
        const int32_t amount = scaledDamage(pike.damage, blowPct(enemy, u, dist, -dz), kBracedPct) + impact;
        const Vec3 at = u.pos + up(u.stats.height * 3 / 5);
        const bool killed = damage(u, amount, &enemy, {pike.damageType, -dir, at});
        knock(u, -dir, weight * kChargeKnock);
        shakeMorale(u, kBracedShock);
        tell(TacticEvent::Kind::Braced, enemy);
        impacts_.push_back({ImpactEvent::Kind::Hit, at, -dir, kNoEntity, u.id, killed, {}, -1, pike.damageType});
        u.wheel = 0;  // (stopped cold: nothing to wheel)
        return;
    }

    const Vec3 at = enemy.pos + up(enemy.stats.height * 3 / 5);
    if (braced) {
        // Foot standing formed to meet him: the charge breaks on the front rank. His blow lands, the weight behind it
        // half, and he is shaken and stuck in the press (a body that marched, or stood loose or broken, is ridden through).
        tell(TacticEvent::Kind::Braced, enemy);
        const bool caught = shieldBlocks(enemy, dir, kMeleeBlockScale);
        const int32_t amount = (caught ? 0 : scaledDamage(u.stats.melee.damage, blowPct(u, enemy, dist, dz))) + impact * kBracedFootImpactPct / 100;
        const bool killed = damage(enemy, amount, &u, {u.stats.melee.damageType, dir, at});
        chargeShock(enemy, side, false);
        knock(u, -dir, weight * kChargeKnock / 2);  // (thrown back off the shields: he does not ride through)
        shakeMorale(u, kBracedShock);
        impacts_.push_back({caught ? ImpactEvent::Kind::Blocked : ImpactEvent::Kind::Hit, at, dir, kNoEntity, enemy.id, killed, {}, -1, u.stats.melee.damageType});
        return;
    }
    // A shield turns the lance or sword but not the weight behind it.
    const bool blocked = shieldBlocks(enemy, dir, kMeleeBlockScale);
    const int32_t blow = blocked ? 0 : scaledDamage(u.stats.melee.damage, blowPct(u, enemy, dist, dz));
    // (A wedge strikes harder; a wedge struck in the flank takes more: world_formation.cpp. The rider's own disorder, just
    // set, is not counted against this blow.)
    const int32_t formPct = (formed(enemy) ? 100 + formStruckPct(enemy, side, false) : 100) * (100 + formationRule(u.formation).blowPct * (u.ranked >= 2 && u.formation != FormationKind::None)) / 100;
    const int32_t amount = (blow + (blocked ? impact / 2 : impact)) * sidePct(side, false) / 100 * formPct / 100;
    const bool killed = damage(enemy, amount, &u, {u.stats.melee.damageType, dir, at});
    chargeShock(enemy, side, busy && behind);
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
