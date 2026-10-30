// Match rules on the soldiers (world.h): mending between Horde's rounds, a
// hero's level for surviving one, and a side put out of a capture-the-flag
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
