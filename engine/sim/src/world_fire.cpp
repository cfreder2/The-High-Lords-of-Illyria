// Fire on the ground: what a Molotov does (the owner's rule: "Molotovs should not explode, that is a satchel's job; they
// should burn the ground and the wood of a catapult"). A bottle of oil has no blast. It shatters into a patch of burning
// ground (its weapon's Explosive: `radius` wide, `damage` hit points a second to a man in it, burning `fuseTicks`) that
// hurts and frightens whoever stands in it or walks through it, and slows the walk (World::walk). Timber it lands on or
// beside catches: an engine burns on for a while whether or not it rolls out of the patch (it can burn to nothing), and a
// gate in the patch burns slowly while the patch does. Its fire sets off a satchel it reaches, as before: that is how a
// dwarf sets off his line. In water it goes out (World::explode). A weapon any unit may carry with a count: nothing here
// knows who threw it.
#include <algorithm>

#include "gate_geometry.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Timber alight (an engine): how long it burns once lit, and its hurt a second.
constexpr int32_t kTimberBurnTicks = 15 * kTicksPerSecond;
constexpr int32_t kTimberBurnPerSec = 20;
// A gate in a burning patch: hit points a second (a town gate has 2000: oak burns slowly; satchels and stones break it).
constexpr int32_t kGateBurnPerSec = 25;
// A man in the fire: his nerve a second, and how much slower he walks through it.
constexpr int32_t kFirePanic = 60;
constexpr int32_t kFireWalkPct = 50;
// Timber beside the patch catches too: this much beyond its edge.
constexpr Fixed kCatchBeside = Fixed::fromInt(1);

bool timber(const Unit& u) { return u.stats.gait == Gait::Wheeled; }
constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }
Vec3 flatDirection(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

}  // namespace

void World::kindle(const Vec3& at, const Explosive& ex, PlayerId owner) {
    impacts_.push_back({ImpactEvent::Kind::Burst, at, up(Fixed::one()), kNoEntity, kNoEntity, false, ex.radius, -1, DamageType::Fire});
    fires_.push_back({at, ex.radius, ex.damage, std::max<int32_t>(1, ex.fuseTicks), owner});
    // Timber on or beside it catches at once.
    const Fixed reach = ex.radius + kCatchBeside;
    for (Unit& u : units_)
        if (u.alive() && timber(u) && (u.pos - at).flat().length() - u.stats.radius < reach) u.burning = kTimberBurnTicks;
    (void)owner;
}

bool World::inFire(const Vec3& p) const {
    for (const GroundFire& f : fires_)
        if ((p - f.at).flat().lengthSq() < f.radius * f.radius) return true;
    return false;
}

int32_t World::fireWalkPct(const Vec3& p) const { return fires_.empty() || !inFire(p) ? 100 : kFireWalkPct; }

void World::updateFires() {
    const bool second = tick_ % kTicksPerSecond == 0;
    for (GroundFire& f : fires_) {
        const Fixed r2 = f.radius * f.radius;
        // Satchels it reaches (laid, or rolled into it) go up a moment later: the chain's spark.
        for (Projectile& p : projectiles_) {
            if (p.kind != ProjectileKind::Bomb || p.fuse != 0 || (p.resting && douses(p.pos))) continue;
            if ((p.pos - f.at).flat().lengthSq() >= r2 || p.pos.z > f.at.z + Fixed::fromInt(2)) continue;
            p.fuse = 4 + static_cast<int32_t>(rng_.nextBelow(8));
        }
        // ... and one in a man's hands goes up in them (the satchel run's end: a Molotov at the carrier).
        for (Unit& u : units_) {
            if (!u.alive() || !u.scav.satchel || u.inAir() || (u.scav.lit && u.scav.fuse <= 2) || (u.pos - f.at).flat().lengthSq() >= r2) continue;
            u.scav.fuse = 2;
            u.scav.lit = true;
        }
        if (second) {
            // Those in it, on the ground: burned and frightened; timber in it catches.
            for (Unit& u : units_) {
                if (!u.alive() || u.inAir() || (u.pos - f.at).flat().lengthSq() >= r2) continue;
                if (timber(u)) u.burning = kTimberBurnTicks;
                const Vec3 dir = flatDirection(f.at, u.pos, u.facing);
                damage(u, f.burn, nullptr, {DamageType::Fire, dir, u.pos + up(u.stats.height / 3), false, false});
                if (u.alive()) shakeMorale(u, kFirePanic);
            }
            // A gate in it burns.
            if (config_.region) {
                const auto& gs = config_.region->gates();
                for (size_t i = 0; i < gs.size(); ++i) {
                    if (gates_[i].broken()) continue;
                    const Vec3 q = nearestOnGate(gs[i], f.at);
                    const Fixed d = (q - f.at).flat().length() - gs[i].halfThick;
                    if (d < f.radius) damageGate(static_cast<int>(i), kGateBurnPerSec, q, (q - f.at).flat());
                }
            }
        }
        --f.ticks;
    }
    std::erase_if(fires_, [](const GroundFire& f) { return f.ticks <= 0; });
    // Timber alight burns on (the crew do not beat it out).
    for (Unit& u : units_) {
        if (u.burning <= 0) continue;
        if (!u.alive() || douses(u.pos)) {
            u.burning = 0;
            continue;
        }
        --u.burning;
        if ((tick_ + u.id) % kTicksPerSecond == 0) damage(u, kTimberBurnPerSec, nullptr, {DamageType::Fire, u.facing, u.pos + up(u.stats.height / 2), false, false});
    }
}

bool World::burnGate(Unit& u) {
    // A thrower of fire with no foe worth a bottle: a foe's gate within his throw, not burning already, takes one.
    if (u.stats.ranged.kind != ProjectileKind::Molotov || !armed(u) || u.holdFire || !config_.region) return false;
    if (u.stats.satchels.count > 0 && u.ammo <= 1) return false;  // (the last bottle is for his satchels)
    const auto& gs = config_.region->gates();
    const Fixed reach = autoReach(u);
    int best = -1;
    Fixed bestD;
    Vec3 mark;
    for (size_t i = 0; i < gs.size(); ++i) {
        const GateState& st = gates_[i];
        if (st.broken() || st.holder == GateState::kNoHolder || st.holder == u.owner) continue;
        const Vec3 q = nearestOnGate(gs[i], u.pos);
        const Fixed d = (q - u.pos).flat().length();
        if (d > reach || inFire(q) || douses(q)) continue;
        if (best < 0 || d < bestD) best = static_cast<int>(i), bestD = d, mark = q;
    }
    if (best < 0) return false;
    u.facing = flatDirection(u.pos, mark, u.facing);
    if (readyToLoose(u)) fireAt(u, mark, false);
    return true;
}

}  // namespace illyria::sim
