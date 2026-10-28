// The death matrix (docs/GAME_DESIGN.md §5.5): how a soldier dies decides
// what is left of him. The killing blow's damage type, its direction and the
// overkill (how far below zero health it went) choose between a body that
// falls whole, one that loses a limb or its head to a cut, one torn to pieces
// by a blast (or a blow far past death), one shot through with the arrow left
// in it, and one burned black.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// Only man-sized bodies (and riders) lose a limb to a cut: a giant's arm is
// not taken off by one sword stroke.
constexpr Fixed kSeverMaxMass = Fixed::fromInt(4);
// A limb cut off flies on along the blow at this speed, plus a little per point of overkill.
constexpr Fixed kSeverSpeed = Fixed::fromInt(2);
constexpr Fixed kSeverSpeedMax = Fixed::fromInt(5);
// A body hacked or crushed apart (not by a blast) bursts this hard.
constexpr Fixed kBlowGibStrength = Fixed::fromInt(6);
// Rolling down slopes: steeper than this grade a part cannot grip and rolls
// (heads and gobbets roll easily, limbs and trunks drag), damped as it goes.
constexpr Fixed kRollGrip = Fixed::fromRatio(1, 4);
constexpr Fixed kSlideGrip = Fixed::fromRatio(2, 5);
constexpr Fixed kRollDamp = Fixed::fromRatio(49, 50);

Vec3 unitFlat(const Vec3& v, const Vec3& fallback) {
    const Vec3 f = v.flat();
    const Fixed len = f.length();
    return len == Fixed::zero() ? fallback : f / len;
}

}  // namespace

const char* damageTypeName(DamageType t) {
    switch (t) {
        case DamageType::Slash: return "slash";
        case DamageType::Cleave: return "cleave";
        case DamageType::Blunt: return "blunt";
        case DamageType::Pierce: return "pierce";
        case DamageType::Explosive: return "explosive";
        case DamageType::Fire: return "fire";
    }
    return "?";
}

const char* deathKindName(DeathKind k) {
    switch (k) {
        case DeathKind::None: return "none";
        case DeathKind::Fell: return "fell";
        case DeathKind::Severed: return "severed";
        case DeathKind::Gibbed: return "gibbed";
        case DeathKind::Pierced: return "pierced";
        case DeathKind::Charred: return "charred";
    }
    return "?";
}

void World::resolveDeath(Unit& u, const Blow& blow, int32_t overkill, const Unit* killer) {
    Unit::Death& d = u.death;
    d.cause = blow.type;
    d.overkill = std::max(overkill, 0);
    d.tick = tick_;
    // Struck from nowhere in particular, he falls back.
    d.dir = unitFlat(blow.dir, -u.facing);

    const bool engine = u.stats.type == static_cast<uint8_t>(UnitType::Catapult);
    DeathKind kind = DeathKind::Fell;
    switch (blow.type) {
        case DamageType::Fire: kind = DeathKind::Charred; break;
        case DamageType::Pierce: kind = blow.missile ? DeathKind::Pierced : DeathKind::Fell; break;
        case DamageType::Explosive: kind = d.overkill >= kGibOverkillBlast ? DeathKind::Gibbed : DeathKind::Fell; break;
        case DamageType::Slash:
        case DamageType::Cleave: {
            const int32_t threshold = blow.type == DamageType::Cleave ? kSeverOverkillCleave : kSeverOverkillSlash;
            if (u.stats.mass < kSeverMaxMass && d.overkill >= threshold) kind = DeathKind::Severed;
            break;
        }
        case DamageType::Blunt: break;
    }
    // Far past death any blow but fire (and a spent arrow) tears the body apart.
    if (d.overkill >= kObliterateOverkill && blow.type != DamageType::Fire && !(blow.missile && blow.type == DamageType::Pierce))
        kind = DeathKind::Gibbed;
    // An engine breaks up into timber, or burns.
    if (engine && kind != DeathKind::Charred) kind = DeathKind::Fell;
    d.kind = kind;

    if (kind == DeathKind::Pierced) {
        // The arrow stays in him, where it went in.
        d.arrowPinned = true;
        const Fixed len = blow.dir.length();
        d.arrowDir = len == Fixed::zero() ? d.dir : blow.dir / len;
        d.arrowHeight = clamp(blow.at.z - u.pos.z, u.stats.height / 5, u.stats.height * 9 / 10);
    } else if (kind == DeathKind::Severed) {
        // Twice past the threshold the head goes; else an arm or a leg.
        const int32_t threshold = blow.type == DamageType::Cleave ? kSeverOverkillCleave : kSeverOverkillSlash;
        RemainKind part = RemainKind::Head;
        if (d.overkill < threshold * 2) part = rng_.nextBelow(2) == 0 ? RemainKind::Arm : RemainKind::Leg;
        d.severed = part;
        const Fixed height = part == RemainKind::Head ? u.stats.height * 9 / 10
                             : part == RemainKind::Arm ? u.stats.height * 13 / 20
                                                       : u.stats.height * 3 / 10;
        // One RNG draw per statement (operand order differs between compilers).
        const Fixed lift = Fixed::fromInt(2) + rng_.nextUnit();
        const Fixed side = rng_.nextSigned();
        const Fixed speed = min(kSeverSpeed + Fixed::fromInt(d.overkill) / 8, kSeverSpeedMax);
        const Vec3 vel = d.dir * speed + Vec3{-d.dir.y * side, d.dir.x * side, lift};
        spawnDebris(u.stats.bleeds ? part : RemainKind::Bone, u.owner, u.pos + up(height), vel);
        spawned_.back().bleeding = u.stats.bleeds;
        // A pool where he fell.
        if (u.stats.bleeds && !isWater(u.pos.x, u.pos.y))
            impacts_.push_back({ImpactEvent::Kind::Bleed, u.pos, d.dir, kNoEntity, u.id, false, Fixed::fromRatio(7, 10)});
    } else if (kind == DeathKind::Gibbed && !blow.blast) {
        // Hacked or crushed apart; a blast throws the pieces itself (explode).
        spawnGibs(u, u.pos - d.dir, kBlowGibStrength);
    }

    deathEvents_.push_back({u.id, u.owner, u.stats.type, kind, blow.type, u.pos, d.dir, d.overkill,
                            killer != nullptr ? killer->id : kNoEntity, d.severed});
}

bool World::rollDownhill(Projectile& p) {
    if (!config_.terrain) return false;
    const Fixed step = Fixed::one();
    const Fixed gx = (groundHeight(p.pos.x + step, p.pos.y) - groundHeight(p.pos.x - step, p.pos.y)) / 2;
    const Fixed gy = (groundHeight(p.pos.x, p.pos.y + step) - groundHeight(p.pos.x, p.pos.y - step)) / 2;
    const Fixed grade = sqrt(gx * gx + gy * gy);
    const Fixed grip = p.remain == RemainKind::Head || p.remain == RemainKind::Chunk ? kRollGrip : kSlideGrip;
    if (grade <= grip) return false;
    // Gravity along the slope less what the ground holds back.
    const Fixed a = config_.gravity * (grade - grip) / (Fixed::one() + grade) * kTickDt;
    p.vel.x = (p.vel.x - gx / grade * a) * kRollDamp;
    p.vel.y = (p.vel.y - gy / grade * a) * kRollDamp;
    return true;
}

}  // namespace illyria::sim
