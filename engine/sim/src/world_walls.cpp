// Castle walls in battle: the stone that stops missiles (the slab, the
// parapet and its merlons, open at the embrasures), the walk soldiers stand
// and fall on, and archers leaning into the embrasures to shoot
// (docs/GAME_DESIGN.md, castles).
#include <algorithm>
#include <utility>

#include "illyria/sim/continent.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

using Castle = Continent::Castle;

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// The keep's walls (no walk) are solid to a little under their top.
constexpr Fixed kKeepBattlement = Fixed::fromRatio(3, 10);
// A missile coming down on the walk meets its paving (the ground) before the slab.
constexpr Fixed kPaving = Fixed::fromRatio(1, 20);
// A falling thing lands on a deck no more than this above it.
constexpr Fixed kFallReach = Fixed::fromRatio(1, 2);
// An archer within this much beyond an embrasure's side can lean into it,
// and looses from this far out past the wall's face (over the sill).
constexpr Fixed kLeanReach = Fixed::fromRatio(1, 5);
constexpr Fixed kLeanOut = Fixed::fromRatio(1, 10);
// A defender is shot at this high up his body (his chest, over the sill).
constexpr int32_t kChestPct = 60;
// Arrows stuck in a gate this far above the ground fall when it breaks.
constexpr Fixed kStuckAbove = Fixed::fromRatio(1, 5);

// Entry fraction of the segment p + d t (t in [0, 1]) into the box lo..hi,
// and where it leaves it; -1 if it misses, or starts inside (it is leaving).
Fixed boxEntry(const Fixed p[3], const Fixed d[3], const Fixed lo[3], const Fixed hi[3], Fixed& exit) {
    Fixed tin = Fixed::zero(), tout = Fixed::one();
    bool inside = true;
    for (int k = 0; k < 3; ++k) {
        inside = inside && p[k] > lo[k] && p[k] < hi[k];
        if (d[k] == Fixed::zero()) {
            if (p[k] < lo[k] || p[k] > hi[k]) return -Fixed::one();
            continue;
        }
        Fixed t1 = (lo[k] - p[k]) / d[k], t2 = (hi[k] - p[k]) / d[k];
        if (t1 > t2) std::swap(t1, t2);
        tin = max(tin, t1);
        tout = min(tout, t2);
        if (tin >= tout) return -Fixed::one();
    }
    if (inside) return -Fixed::one();
    exit = tout;
    return tin;
}

}  // namespace

Fixed World::wallHit(const Vec3& a, const Vec3& b, Fixed limitT) const {
    if (!config_.region) return -Fixed::one();
    Fixed best = limitT;
    bool hit = false;
    const Vec3 seg = b - a;
    for (const WallSeg& wl : config_.region->walls()) {
        const Fixed pad = wl.halfThick + Fixed::fromInt(2);
        if (max(a.x, b.x) < min(wl.a.x, wl.b.x) - pad || min(a.x, b.x) > max(wl.a.x, wl.b.x) + pad ||
            max(a.y, b.y) < min(wl.a.y, wl.b.y) - pad || min(a.y, b.y) > max(wl.a.y, wl.b.y) + pad)
            continue;
        const Vec3 ab = (wl.b - wl.a).flat();
        const Fixed len = ab.length();
        if (len == Fixed::zero()) continue;
        // In the wall's own frame: along it, out across it, up.
        const Vec3 dir = ab / len;
        const bool walked = wl.walk > Fixed::zero() && wl.out != Vec3{};
        const Vec3 out = walked ? wl.out : Vec3{-dir.y, dir.x, Fixed::zero()};
        const Vec3 rel = (a - wl.a).flat();
        const Fixed p[3] = {rel.dot(dir), rel.dot(out), a.z};
        const Fixed d[3] = {seg.flat().dot(dir), seg.flat().dot(out), seg.z};
        const Fixed ht = wl.halfThick;
        Fixed exit;
        const auto box = [&](Fixed v0, Fixed v1, Fixed z0, Fixed z1) {
            const Fixed lo[3] = {Fixed::zero(), v0, z0}, hi[3] = {len, v1, z1};
            return boxEntry(p, d, lo, hi, exit);
        };
        const auto take = [&](Fixed t) {
            if (t < Fixed::zero() || t >= best) return;
            best = t;
            hit = true;
        };
        if (!walked) {
            take(box(-ht, ht, wl.a.z, wl.top - kKeepBattlement));
            continue;
        }
        take(box(-ht, ht, wl.a.z, wl.walk - kPaving));  // the slab under the walk
        const Fixed v0 = ht - Castle::kParapetThick;
        take(box(v0, ht, wl.walk - kPaving, wl.walk + Castle::kParapetSill));  // the breastwork
        // Above the sill: merlons, but for the embrasures. A missile in one
        // strikes stone only if it leaves the embrasure sideways inside the parapet.
        const Fixed tin = box(v0, ht, wl.walk + Castle::kParapetSill, wl.top);
        if (tin < Fixed::zero() || tin >= best) continue;
        const Fixed s = p[0] + d[0] * tin;
        const Fixed half = Castle::kCrenelWidth / 2;
        Fixed at = tin;
        for (const Fixed c : wl.crenels) {
            if (s < c - half || s > c + half) continue;
            if (d[0] > Fixed::zero()) at = (c + half - p[0]) / d[0];
            else if (d[0] < Fixed::zero()) at = (c - half - p[0]) / d[0];
            else at = exit;
            break;
        }
        if (at < exit) take(at);
    }
    return hit ? best : -Fixed::one();
}

Fixed World::surfaceBelow(Fixed x, Fixed y, Fixed z) const {
    const Fixed ground = config_.terrain ? config_.terrain->heightAt(x, y) : Fixed::zero();
    Fixed deck;
    if (config_.region && !config_.region->decks().empty() && config_.region->deckBelow(x, y, z + kFallReach, deck)) return max(ground, deck);
    return ground;
}

bool World::walkSpot(const Vec3& p, WalkSpot& spot) const {
    if (!config_.region) return false;
    for (const WallSeg& wl : config_.region->walls()) {
        if (wl.walk <= Fixed::zero() || wl.out == Vec3{}) continue;
        if (p.z < wl.walk - kFallReach || p.z > wl.top) continue;
        const Fixed pad = wl.halfThick;
        if (p.x < min(wl.a.x, wl.b.x) - pad || p.x > max(wl.a.x, wl.b.x) + pad || p.y < min(wl.a.y, wl.b.y) - pad ||
            p.y > max(wl.a.y, wl.b.y) + pad)
            continue;
        const Vec3 ab = (wl.b - wl.a).flat();
        const Fixed len = ab.length();
        if (len == Fixed::zero()) continue;
        const Vec3 dir = ab / len;
        const Vec3 rel = (p - wl.a).flat();
        const Fixed along = rel.dot(dir), across = rel.dot(wl.out);
        if (along < Fixed::zero() || along > len || abs(across) > wl.halfThick) continue;
        spot = {&wl, dir, along, across, len};
        return true;
    }
    return false;
}

void World::keepOnWalk(Unit& u) {
    if (!config_.region || config_.region->ramparts().empty() || u.flight != Unit::Flight::Grounded) return;
    WalkSpot w;
    if (!walkSpot(u.pos, w)) return;
    const WallSeg& wl = *w.seg;
    const Fixed limit = wl.halfThick - Castle::kParapetThick - u.stats.radius;
    if (w.across > limit) {
        // Thrown high enough, a body goes over the parapet (and falls to the
        // wall's foot); otherwise it stops against it.
        if (u.airborne && u.pos.z > wl.walk + Castle::kParapetSill) return;
        u.pos -= wl.out * (w.across - limit);
        const Fixed outward = u.vel.flat().dot(wl.out);
        if (u.airborne && outward > Fixed::zero()) u.vel -= wl.out * outward;
    }
    // The dead lie along the walk, not across it into the stone.
    if (!u.alive() && !u.airborne) u.facing = u.facing.dot(w.dir) >= Fixed::zero() ? w.dir : -w.dir;
}

bool World::walkShot(const Unit& shooter, const Vec3& target, Vec3& origin) const {
    WalkSpot w;
    if (!walkSpot(shooter.pos, w)) return true;
    const WallSeg& wl = *w.seg;
    // Only shots out over the wall go through the embrasures.
    if ((target - wl.a).flat().dot(wl.out) <= wl.halfThick) return true;
    const Fixed half = Castle::kCrenelWidth / 2;
    for (const Fixed c : wl.crenels) {
        if (abs(w.along - c) > half + kLeanReach) continue;
        // Leaning into it: the bow out over the sill, within the opening.
        origin += w.dir * (clamp(w.along, c - half / 2, c + half / 2) - w.along);
        origin += wl.out * (wl.halfThick - w.across + kLeanOut);
        origin.z = max(origin.z, wl.walk + Castle::kParapetSill + Fixed::fromRatio(1, 5));
        return true;
    }
    return false;  // behind a merlon
}

bool World::aimHigh(const Unit& shooter, Vec3& aim, Fixed height) const {
    WalkSpot w;
    if (aim.z < shooter.pos.z + Fixed::one() || !walkSpot(aim, w)) return false;
    aim += up(height * kChestPct / 100);
    return true;
}

void World::dropGateArrows(int gate) {
    const Gate& g = config_.region->gates()[static_cast<size_t>(gate)];
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    const Vec3 ab = (g.b - g.a).flat();
    std::vector<uint32_t> near;
    remainGrid_.query(mid.x, mid.y, ab.length() / 2 + Fixed::fromInt(2), [&](uint32_t slot) { near.push_back(slot); });
    std::sort(near.begin(), near.end());  // grid buckets need not come in slot order
    for (const uint32_t slot : near) {
        const Remain& r = remains_[slot];
        if (r.kind != RemainKind::Arrow) continue;
        const Vec3 rel = (r.pos - g.a).flat();
        if (abs(rel.dot(g.out)) > g.halfThick + Fixed::one() || rel.dot(ab) < Fixed::zero() || rel.dot(ab) > ab.lengthSq()) continue;
        if (r.pos.z < surfaceBelow(r.pos.x, r.pos.y, r.pos.z) + kStuckAbove) continue;
        // Shaken loose: it drops with the timbers, a little inward.
        spawnDebris(RemainKind::Arrow, r.owner, r.pos, -g.out * Fixed::fromRatio(1, 2));
        clearRemain(slot);
    }
}

}  // namespace illyria::sim
