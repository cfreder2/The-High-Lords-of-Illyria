// What the renderer reads off a battle for a soldier's clips (unit_deeds.h).

#include "unit_deeds.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "illyria/game/mode_ctf.h"
#include "illyria/game/mode_horde.h"

namespace illyria::game::deeds {

namespace {

uint32_t bit(sim::PlayerId p) { return p < 32 ? 1u << p : 0u; }
double f(sim::Fixed x) { return x.toDouble(); }
double flat(const sim::Vec3& a, const sim::Vec3& b) { return std::hypot(f(a.x) - f(b.x), f(a.y) - f(b.y)); }

// Missile reach from his height over the mark (World::shotRange: a metre
// farther for each metre down, within -1/4 and +1/2 of the bow's).
double shotRange(const sim::Unit& u, sim::Fixed base, const sim::Vec3& at) {
    const double b = f(base);
    return b + std::clamp(f(u.pos.z) - f(at.z), -b / 4, b / 2);
}

}  // namespace

uint32_t victors(const Battle& b) {
    const int outcome = b.outcome();
    if (outcome == 0) return 0;
    if (const BattleMode* m = b.mode()) {
        const std::string_view id = m->id();
        if (id == "ctf") {
            const sim::PlayerId w = static_cast<const CtfMode*>(m)->winner();
            return w == CtfMode::kNobody ? 0u : bit(w);
        }
        if (id == "horde") return bit(outcome == 1 ? HordeMode::kHolders : HordeMode::kHorde);
    }
    if (outcome == 1) return bit(Battle::kPlayer);
    if (b.capturePoint().taken()) return bit(b.capturePoint().attacker());
    uint32_t sides = 0;
    for (const sim::Unit& u : b.world().units())
        if (u.alive() && u.owner != Battle::kPlayer) sides |= bit(u.owner);
    return sides;
}

bool willLoose(const Battle& b, size_t i) {
    const sim::World& w = b.world();
    if (i >= w.units().size()) return false;
    const sim::Unit& u = w.units()[i];
    const sim::RangedWeapon& r = u.stats.ranged;
    if (!u.alive() || u.routed || u.airborne || u.flight != sim::Unit::Flight::Grounded || u.engaged) return false;
    if (r.muzzleSpeed <= sim::Fixed::zero() || u.stats.scavenge.sight > sim::Fixed::zero() || b.unitMoving(i)) return false;
    const double minRange = f(r.minRange);
    // Sent at a soldier: he keeps to a good bowshot of him (World::pursue).
    if (u.targetUnit != sim::kNoEntity) {
        const sim::Unit* t = w.findUnit(u.targetUnit);
        if (!t || !t->alive()) return false;
        const double d = flat(u.pos, t->pos);
        return d >= minRange && d <= shotRange(u, r.maxRange, t->pos) * 0.9;
    }
    if (u.hasMoveTarget) return false;
    if (u.hasAttackTarget) {
        const double d = flat(u.pos, u.attackTarget);
        return d >= minRange && d <= shotRange(u, r.maxRange, u.attackTarget);
    }
    if (r.autoRange <= sim::Fixed::zero()) return false;
    // Left to himself: the nearest foe (ties to the lowest id, as the sim).
    const double reach = f(r.autoRange) * 1.5;
    const sim::Unit* nearest = nullptr;
    double best = reach * reach;
    for (const sim::Unit& o : w.units()) {
        if (!o.alive() || o.owner == u.owner) continue;
        const double dx = f(o.pos.x) - f(u.pos.x), dy = f(o.pos.y) - f(u.pos.y), d2 = dx * dx + dy * dy;
        if (d2 <= best && (!nearest || d2 < best)) nearest = &o, best = d2;
    }
    if (!nearest) return false;
    const double d = std::sqrt(best);
    // So near he would close and fight it out instead (World::meleeTarget).
    if (u.stats.melee.damage > 0 && d <= f(u.stats.aggroRange)) return false;
    return d >= minRange && d <= shotRange(u, r.autoRange, nearest->pos);
}

}  // namespace illyria::game::deeds
