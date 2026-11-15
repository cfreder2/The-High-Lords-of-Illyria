// Match rules on the soldiers (world.h): mending between Horde's rounds, a
// hero's level for surviving one, a fallen hero raised again, a side put out of a capture-the-flag
// match, and the field's edge that wounds whoever stands beyond it. Called by the game modes from simulation state only, at the same
// tick on every peer.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

void World::heal(EntityId id, int32_t amount) {
    Unit* u = mutableUnit(id);
    if (!u || !u->alive() || amount <= 0) return;
    u->hp = std::min(u->stats.maxHp, u->hp + amount);
}

void World::levelUp(EntityId id) {
    Unit* u = mutableUnit(id);
    if (!u || !u->alive()) return;
    if (u->stats.type == static_cast<uint8_t>(UnitType::Hero)) {
        if (u->rank >= 8) return;
        ++u->rank;
        const int32_t gain = unitStats(UnitType::Hero).maxHp * kHeroLevelHpPct / 100;
        u->stats.maxHp += gain;
        u->hp += gain;
        return;
    }
    u->rank = std::min(3, u->rank + 1);
}

EntityId World::raiseFallen(EntityId fallen, const Vec3& pos, const Vec3& facing, int32_t hpPct) {
    const Unit* f = findUnit(fallen);
    if (!f || f->alive()) return kNoEntity;
    const Unit was = *f;  // spawning may move the units
    const EntityId id = spawnUnit(was.owner, pos, facing, was.stats);
    Unit* u = mutableUnit(id);
    u->rank = was.rank;
    u->kills = was.kills;
    u->blessings = was.blessings;
    u->hp = std::max<int32_t>(1, u->stats.maxHp * std::clamp(hpPct, 1, 100) / 100);
    return id;
}

void World::breakSide(PlayerId owner) {
    if (sideBroken(owner)) return;
    brokenSides_.insert(std::lower_bound(brokenSides_.begin(), brokenSides_.end(), owner), owner);
    for (Unit& u : units_) {
        if (u.owner != owner || !u.alive()) continue;
        // As a soldier breaking (world_morale.cpp): orders forgotten, the fight given up.
        u.morale = 0;
        u.routed = true;
        u.hasMoveTarget = false;
        u.hasAttackTarget = false;
        u.attackGate = -1;
        u.path.clear();
        u.foe = kNoEntity;
        u.targetUnit = kNoEntity;
    }
}

void World::quitField(PlayerId owner) {
    if (sideBroken(owner)) return;
    Vec3 mine, theirs;
    int32_t nMine = 0, nTheirs = 0;
    for (const Unit& u : units_) {
        if (!u.alive()) continue;
        if (u.owner == owner) mine += u.pos.flat(), ++nMine;
        else theirs += u.pos.flat(), ++nTheirs;
    }
    breakSide(owner);
    if (nMine == 0 || nTheirs == 0) return;
    mine = mine / Fixed::fromInt(nMine);
    theirs = theirs / Fixed::fromInt(nTheirs);
    Vec3 away = mine - theirs;
    const Fixed len = away.length();
    if (len == Fixed::zero()) return;
    away = away / len;
    // Along that way to the edge of the ground, a few metres short of it (a plain with no edge: 200 m off).
    const Terrain* ground = config_.region ? &config_.region->terrain() : config_.terrain.get();
    const Fixed lo = ground ? Fixed::fromInt(8) : Fixed::fromInt(-1000000), hi = ground ? ground->size() - Fixed::fromInt(8) : Fixed::fromInt(1000000);
    Fixed reach = Fixed::fromInt(200);
    const auto bound = [&](Fixed at, Fixed dir) {
        if (dir > Fixed::fromRatio(1, 1000)) reach = min(reach, (hi - at) / dir);
        else if (dir < -Fixed::fromRatio(1, 1000)) reach = min(reach, (lo - at) / dir);
    };
    if (ground) reach = Fixed::fromInt(100000);
    bound(mine.x, away.x);
    bound(mine.y, away.y);
    if (reach <= Fixed::zero()) return;  // (already at it)
    const Vec3 edge = mine + away * reach;
    for (Unit& u : units_) {
        if (u.owner != owner || !u.alive() || u.inAir()) continue;
        Command c;
        c.player = owner;
        c.unit = u.id;
        c.type = Command::Type::Move;
        const Vec3 to = edge + (u.pos.flat() - mine);  // each keeps his place beside the others
        c.target = Vec3{min(max(to.x, lo), hi), min(max(to.y, lo), hi), Fixed::zero()};
        c.force = true;
        c.march = true;
        c.marchFrom = mine;
        c.marchTo = edge;
        u.queue.clear();
        startOrder(u, c);
    }
}

bool World::sideBroken(PlayerId owner) const {
    return std::binary_search(brokenSides_.begin(), brokenSides_.end(), owner);
}

void World::retune(uint8_t type, const UnitStats& stats) {
    for (Unit& u : units_) {
        if (!u.alive() || u.stats.type != type) continue;
        const int32_t was = std::max<int32_t>(1, u.stats.maxHp);
        u.hp = std::max<int32_t>(1, static_cast<int32_t>(static_cast<int64_t>(u.hp) * stats.maxHp / was));
        u.stats = stats;
        u.stats.type = type;
    }
}

void World::setZone(const Vec3& centre, Fixed radius, int32_t pctPerSecond) {
    zoneCentre_ = {centre.x, centre.y, Fixed::zero()};
    zoneRadius_ = radius > Fixed::zero() ? radius : Fixed::zero();
    zonePct_ = std::max<int32_t>(0, pctPerSecond);
}

bool World::outsideZone(const Vec3& p) const {
    if (zoneRadius_ <= Fixed::zero()) return false;
    const Vec3 d{p.x - zoneCentre_.x, p.y - zoneCentre_.y, Fixed::zero()};
    return d.lengthSq() > zoneRadius_ * zoneRadius_;
}

// Once a second: everyone alive beyond the edge loses his share, a dull blow
// from no one, pushing him back toward the middle.
void World::applyZone() {
    if (zonePct_ <= 0 || zoneRadius_ <= Fixed::zero() || tick_ % kTicksPerSecond != 0) return;
    for (Unit& u : units_) {
        if (!u.alive() || !outsideZone(u.pos)) continue;
        Blow blow;
        blow.type = DamageType::Blunt;
        const Vec3 in{zoneCentre_.x - u.pos.x, zoneCentre_.y - u.pos.y, Fixed::zero()};
        const Fixed len = in.length();
        blow.dir = len > Fixed::zero() ? in / len : Vec3{};
        blow.at = u.pos;
        damage(u, std::max<int32_t>(1, u.stats.maxHp * zonePct_ / 100), nullptr, blow);
    }
}

}  // namespace illyria::sim
