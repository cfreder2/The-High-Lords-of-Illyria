#pragma once

// The campaign's soldiers on the land (docs/ONE_WORLD.md, "Armies on the
// land"). A stack is drawn as the men it holds: every army's soldiers placed in
// a column of squads at the point the stack has reached along its route. All of
// it is worked out from the campaign's state (the stack, its route, the time on
// its leg) and never from the camera, so every machine sees the same men in the
// same places; nothing here is written back into the simulation.
//
// The renderer draws these as points far out and as the battle's sprites near
// the camera (Renderer::setMapSoldiers); the page's minimap reads the same list.

#include <cstdint>
#include <map>
#include <vector>

#include "illyria/sim/campaign.h"

namespace illyria::game {

struct MapSoldier {
    float x = 0, y = 0;  // continent metres, on the ground
    float yaw = 0;       // the way he faces (radians, as atan2(y, x))
    float speed = 0;     // metres a second as the stack marches; 0 at rest
    uint8_t owner = 0;   // the side (Campaign::kNeutral for a town's own garrison)
    uint8_t type = 0;    // sim::UnitType
    bool aloft = false;  // flying along (a stack of fliers alone, on the march)
    bool selected = false;
    uint32_t stack = 0;
    uint32_t id = 0;  // stable between frames, unlike his place in the list
};

// Where a stack stands as a whole, for picking it: its middle, its heading, the
// box that holds all its men (half its length along the heading, half its width
// across it) and the radius of the circle that does.
struct StackShape {
    uint32_t id = 0;
    uint8_t owner = 0;
    float x = 0, y = 0, yaw = 0, radius = 0;
    float halfLength = 0, halfWidth = 0;
    float speed = 0;
    int soldiers = 0;
};

// Presentation memory between frames: each stack's heading, which eases round
// at a bend of its route and is kept when it stops.
struct MapFacing {
    std::map<uint32_t, float> yaw;
};

// The point a stack has reached, in continent metres, and the way its leg runs.
// `ahead` is the share of a tick the clock has run since the last (hundredths of
// a tick, like Stack::progress), so a stack never steps. A stack in a town stands
// at the town; a leg that begins or ends in one begins or ends there.
struct StackPlace {
    float x = 0, y = 0;
    float dirX = 0, dirY = 0;  // unit vector along the leg (0, 0 at rest)
    float speed = 0;           // metres a second
};
StackPlace stackPlace(const sim::Campaign& c, const sim::Campaign::Stack& s, double ahead);

// Every soldier of every stack on the land (stacks afloat are aboard their boats,
// which are drawn on their own), in the stacks' order, and the stacks' shapes.
// `dt`: seconds since the last call, for the heading to ease by.
void layoutMapSoldiers(const sim::Campaign& c, double ahead, float dt, uint32_t selected, MapFacing& facing,
                       std::vector<MapSoldier>& soldiers, std::vector<StackShape>& shapes);

// How many metres apart a squad's men stand, by what they are (the biggest of
// them: elephants and giants take more room than infantry).
float soldierSpacing(sim::UnitType t);

}  // namespace illyria::game
