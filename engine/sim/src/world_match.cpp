// Match rules on the soldiers (world.h): mending between Horde's rounds, a
// hero's level for surviving one, a fallen hero raised again, and a side put out of a capture-the-flag
// match. Called by the game modes from simulation state only, at the same
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

bool World::sideBroken(PlayerId owner) const {
    return std::binary_search(brokenSides_.begin(), brokenSides_.end(), owner);
}

}  // namespace illyria::sim
