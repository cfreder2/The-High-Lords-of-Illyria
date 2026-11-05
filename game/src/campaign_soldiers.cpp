#include "illyria/game/campaign_soldiers.h"

namespace illyria::game {

void collectMapSoldiers(const sim::Campaign& c, double ahead, uint32_t selected, std::vector<MapSoldier>& out, uint8_t viewer) {
    thread_local std::vector<sim::Campaign::Soldier> men;
    c.soldiers(static_cast<int32_t>(ahead), men);
    out.clear();
    out.reserve(men.size());
    for (const sim::Campaign::Soldier& s : men) {
        MapSoldier m;
        m.x = s.x;
        m.y = s.y;
        m.yaw = s.yaw;
        m.speed = s.speed;
        m.owner = s.owner;
        m.type = static_cast<uint8_t>(s.type);
        m.aloft = s.aloft;
        m.alive = s.alive;
        m.awake = s.awake;
        m.selected = s.stack == selected;
        m.inspected = m.selected && s.owner != viewer;
        m.stack = s.stack;
        m.id = s.id;
        m.struckTick = s.struckTick;
        m.hurtTick = s.hurtTick;
        m.fightTick = s.fightTick;
        m.unit = s.unit;
        m.unitId = s.unitId;
        out.push_back(m);
    }
}

}  // namespace illyria::game
