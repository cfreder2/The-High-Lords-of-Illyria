// Shooting with a blast (docs/GAME_DESIGN.md, "Friendly fire"). A bowman looses
// down the line at his foe and does not look who stands in it; a friend there
// takes the arrow in the back. Those who throw a blast (the catapult's stone,
// a wizard's fireball, a grenadier's Molotov) can aim, and try to spare their
// own: each volley they weigh a few places to drop it, by the damage it does
// the foe less FRIENDLY_WEIGHT times the damage it does their own side, and
// take the best. If even that would badly hurt a friend they hold their fire a
// few ticks for the melee to shift, up to a cap; then they let it go where it
// hurts their own least. A man with a foe on top of him does not wait.
#include <algorithm>
#include <vector>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// A friend counts this much against a foe: the same wound to a man of his own is
// worth twice the one to the enemy, so a stone is shifted for one friend saved
// at the price of up to two foes, but a heavy pile of foes still draws it.
constexpr int32_t kFriendlyWeight = 2;
// A friend is badly hurt by a blast that takes more than this share of his health.
constexpr int32_t kBadShareDenominator = 10;
// Held, he looks again every few ticks (a fifth of a second: a melee moves a
// metre or two in that), and lets it go after this long in all: a stone takes
// fourteen seconds to wind, and five more in a stalled fight is as long as a
// line can wait on one friend before the delay costs more men than he saves.
constexpr int32_t kHoldRetryTicks = kTicksPerSecond / 5;
constexpr int32_t kHoldCapTicks = 5 * kTicksPerSecond;
// A hold is forgotten (the count starts over) once he has not held for this long.
constexpr uint32_t kHoldForgetTicks = 2 * kTicksPerSecond;
// A foe this near is on top of him: he looses at the best place and does not wait.
constexpr Fixed kOverrunReach = Fixed::fromInt(6);
// The places tried round the mark, in shares of the blast's radius: along the line
// to him (nearer and farther) and across it.
constexpr int32_t kStepsEighths[] = {-8, -4, 4, 8};

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// A soldier in reach of the volley, and what a blast would do to the two sides: the expected damage to the foes and to
// his own (himself included), by the sum over the men the blast reaches of centre damage x (1 - d/radius), as
// World::explode deals it, averaged over where the dispersion may drop it.
struct AoeBody {
    Vec3 centre;
    bool friendly;
    int32_t maxHp;
};
struct AoeTally {
    Fixed foe, friends;
    bool badlyHurtFriend = false;  // a friend would lose more than a tenth of his health on the average (any, for a bomb)
};
// The stone lands anywhere in a circle of radius `spread` about the mark: the mark itself and four points on the
// circle at 4/5 of it (their mean squared distance is the circle's) stand for it.
constexpr int32_t kSamples = 5;

AoeTally tallyAt(const std::vector<AoeBody>& bodies, const Vec3* bursts, const Explosive& ex, ProjectileKind kind) {
    AoeTally t;
    thread_local std::vector<Fixed> hurtFriend;
    hurtFriend.assign(bodies.size(), Fixed::zero());
    for (int32_t k = 0; k < kSamples; ++k) {
        for (size_t i = 0; i < bodies.size(); ++i) {
            const Fixed d = (bodies[i].centre - bursts[k]).length();
            if (d >= ex.radius) continue;
            const Fixed hurt = (Fixed::one() - d / ex.radius) * ex.damage;
            if (bodies[i].friendly) {
                hurtFriend[i] += hurt;
                t.friends += hurt;
            } else {
                t.foe += hurt;
            }
        }
    }
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].friendly || hurtFriend[i] == Fixed::zero()) continue;
        if (kind == ProjectileKind::Bomb || hurtFriend[i] * kBadShareDenominator > Fixed::fromInt(bodies[i].maxHp) * kSamples) t.badlyHurtFriend = true;
    }
    return t;
}

}  // namespace

bool World::overrun(const Unit& u) const {
    bool found = false;
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, kOverrunReach, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (found || !o.alive() || o.owner == u.owner || o.inAir()) return;
        found = (o.pos - u.pos).flat().lengthSq() < kOverrunReach * kOverrunReach;
    });
    return found;
}

Vec3 World::burstPoint(const Unit& shooter, const Vec3& origin, const Vec3& aim) const {
    // The flat shots strike the first man in their way, as updateProjectiles has it.
    const Vec3 seg = (aim - origin).flat();
    const Fixed len = seg.length();
    if (len == Fixed::zero()) return aim;
    const Vec3 dir = seg / len;
    Fixed best = len;  // distance along the line to the first body
    nearby(origin.x + seg.x / 2, origin.y + seg.y / 2, len / 2 + Fixed::fromInt(6), [&](uint32_t k) {
        const Unit& o = units_[k];
        if (!o.alive() || o.id == shooter.id) return;
        const Vec3 rel = (o.pos - origin).flat();
        const Fixed along = rel.dot(dir);
        if (along < Fixed::zero() || along > best) return;
        const Fixed r = o.stats.radius;
        const Fixed across2 = rel.lengthSq() - along * along;
        if (across2 > r * r) return;
        best = max(Fixed::zero(), along - sqrt(max(Fixed::zero(), r * r - across2)));
    });
    if (best >= len) return aim;
    Vec3 hit = origin + dir * best;
    hit.z = origin.z + (aim.z - origin.z) * (best / len);
    return hit;
}

bool World::fireAt(Unit& u, const Vec3& target, bool atWalk) {
    const RangedWeapon& w = u.stats.ranged;
    const auto loose = [&](const Vec3& at) {
        if (!fire(u, at, atWalk)) return false;
        u.reloadRemaining = attackTicks(u, w.reloadTicks);
        u.aoeHeld = 0;
        return true;
    };
    // A flat shot at a man, or at nothing that bursts: as ever.
    if (w.kind == ProjectileKind::Arrow || w.explosive.radius <= Fixed::zero()) return loose(target);

    const Vec3 origin = u.pos + up(u.stats.height * Fixed::fromRatio(9, 10));
    const bool flat = w.kind == ProjectileKind::Fireball || w.kind == ProjectileKind::Molotov;
    const Vec3 toMark = (target - u.pos).flat();
    const Fixed dist = toMark.length();
    const Vec3 along = dist > Fixed::zero() ? toMark / dist : u.facing;
    const Vec3 across{-along.y, along.x, Fixed::zero()};
    const Explosive& ex = w.explosive;
    // Where it may fall about the mark (dispersionAt100m is for 100 m; a veteran's tighter aim is not counted).
    const Fixed spread = w.dispersionAt100m * dist / 100;
    const Fixed rho = spread * 4 / 5;

    // The men it could reach from any of the places tried.
    thread_local std::vector<AoeBody> bodies;
    bodies.clear();
    const Fixed reachAround = ex.radius * 2 + spread + Fixed::one();
    const auto gather = [&](uint32_t k) {
        const Unit& o = units_[k];
        if (!o.alive()) return;
        bodies.push_back({o.pos + up(o.stats.height / 2), o.owner == u.owner, o.stats.maxHp});
    };
    if (flat) nearby(origin.x + toMark.x / 2, origin.y + toMark.y / 2, dist / 2 + reachAround, gather);
    else nearby(target.x, target.y, reachAround, gather);

    const auto tally = [&](const Vec3& mark) {
        Vec3 falls[kSamples];
        const Vec3 spots[kSamples] = {mark, mark + along * rho, mark - along * rho, mark + across * rho, mark - across * rho};
        for (int32_t k = 0; k < kSamples; ++k) falls[k] = flat ? burstPoint(u, origin, spots[k]) : spots[k];
        return tallyAt(bodies, falls, ex, w.kind);
    };
    const AoeTally base = tally(target);
    // No friend in the blast, or no men at the mark (a wall, a gate): nothing to weigh.
    if (base.friends <= Fixed::zero() || base.foe <= Fixed::zero()) return loose(target);

    // The mark, and places round it within the blast's reach and the weapon's.
    const Fixed reach = shotRange(u, w.maxRange, target);
    Vec3 bestAim = target, safeAim = target;
    AoeTally best = base, safest = base;
    Fixed bestScore = base.foe - base.friends * kFriendlyWeight;
    const auto consider = [&](const Vec3& mark) {
        const Fixed d = (mark - u.pos).flat().length();
        if (d > reach || d < w.minRange) return;
        const AoeTally t = tally(mark);
        const Fixed score = t.foe - t.friends * kFriendlyWeight;
        if (score > bestScore) {
            bestScore = score;
            bestAim = mark;
            best = t;
        }
        // (For the last resort: the foe still hurt, the friends least.)
        if (t.foe > Fixed::zero() && t.friends < safest.friends) {
            safeAim = mark;
            safest = t;
        }
    };
    for (const int32_t s : kStepsEighths) {
        const Fixed off = ex.radius * s / 8;
        consider(target + along * off);
        consider(target + across * off);
    }

    if (!best.badlyHurtFriend) return loose(bestAim);

    // A friend would be badly hurt wherever it falls: wait for the melee to shift, within the cap.
    if (u.aoeHeldTick + kHoldForgetTicks < tick_) u.aoeHeld = 0;
    if (u.aoeHeld < kHoldCapTicks && !overrun(u)) {
        u.aoeHeld += kHoldRetryTicks;
        u.aoeHeldTick = tick_;
        u.reloadRemaining = kHoldRetryTicks;
        if (dist > Fixed::zero()) u.facing = along;
        return true;
    }
    // Past the cap, or with a foe on him: where it spares his own side most, or the best place if he is overrun.
    return loose(u.aoeHeld >= kHoldCapTicks ? safeAim : bestAim) || loose(target);
}

}  // namespace illyria::sim
