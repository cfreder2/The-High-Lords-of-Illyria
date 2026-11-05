#pragma once

// The campaign's soldiers on the land (docs/ONE_WORLD.md, "Armies on the
// land"). Every soldier of every squad is a man with a place on the ground, and
// the simulation says where (Campaign::soldiers): asleep, in his slot of his
// squad's formation; in a fight, where the campaign's world has him. This is
// that list made ready to draw, to pick from and to show on the minimap. It is
// worked out from the campaign's state alone, never from the camera, so every
// machine sees the same men in the same places; nothing here is written back.
//
// The renderer draws them from the whole island down to one man: a coloured
// dot each from far off, the battle's sprites near, one continuous level of
// detail (Renderer::setMapSoldiers); the page's minimap reads the same list.

#include <cstdint>
#include <vector>

#include "illyria/sim/campaign.h"

namespace illyria::game {

struct MapSoldier {
    float x = 0, y = 0;  // continent metres, on the ground
    float yaw = 0;       // the way he faces (radians, as atan2(y, x))
    float speed = 0;     // metres a second as he walks; 0 standing
    uint8_t owner = 0;   // the side (Campaign::kNeutral for a town's own garrison)
    uint8_t type = 0;    // sim::UnitType
    bool aloft = false;  // flying along
    bool alive = true;   // false: a body lying where it fell, in a fight still going on
    bool awake = false;  // in a fight (stepped in the campaign's world)
    bool selected = false;
    uint32_t stack = 0;
    uint32_t id = 0;     // his own for ever, unlike his place in the list
    // Awake: when (campaign ticks) he last struck, was hurt, or fought at all.
    uint32_t struckTick = 0, hurtTick = 0, fightTick = 0;
};

// Every soldier of every stack on the land (stacks afloat are aboard their boats,
// which are drawn on their own), in the stacks' order. `ahead`: the share of a
// tick the clock has run since the last step (hundredths of a tick, like
// Stack::progress), so a soldier never steps. `selected`: the stack to mark.
void collectMapSoldiers(const sim::Campaign& c, double ahead, uint32_t selected, std::vector<MapSoldier>& out);

}  // namespace illyria::game
