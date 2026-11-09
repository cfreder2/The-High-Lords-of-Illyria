#pragma once

// The paths soldiers will walk, drawn on the ground (docs/manual/12-orders.md): one line for each group of men
// going the same way, however many of them. Pure presentation: the sim's routes in (World::routeOf), lines out.

#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/world.h"

namespace illyria::game {

// One man's remaining route: where he stands, then his waypoints, the last his destination.
struct RouteIn {
    sim::EntityId unit = sim::kNoEntity;
    sim::World::Stance stance = sim::World::Stance::Idle;
    sim::EntityId quarry = sim::kNoEntity;  // Hunt: who he is after (men after one quarry go as one)
    std::vector<V3> points;
    float opacity = 1;
    int squad = -1;  // the squad he stands in (-1: none): its men bending a few metres apart are still one line
    // A leg of the queue behind his current order (Command::queue): its number in the queue, from 1 (0: the order in hand),
    // and whether it is only planned (held in the planning mode, not yet given): drawn in a look of its own.
    int number = 0;
    bool pending = false;
};

// A line to draw: the men of one group, as the middle of them going to the middle of where they end, by the route of
// one of them.
struct RouteLine {
    sim::World::Stance stance = sim::World::Stance::Idle;
    sim::EntityId quarry = sim::kNoEntity;
    std::vector<V3> points;
    float opacity = 1;  // the strongest of the group
    int men = 1;
    int number = 0;  // as RouteIn::number: a queued leg is marked with its number where it ends
    bool pending = false;
};

constexpr size_t kMaxRouteLines = 48;

// Men of one stance and quarry whose routes end within `tolerance` of one another (and that are halfway along within
// twice that, four times for men of one squad: one that goes round a wall is not one that goes straight) are one line. A tolerance below zero merges
// none. At most kMaxRouteLines come out; the rest join the line whose end is nearest. In input order.
std::vector<RouteLine> mergeRoutes(const std::vector<RouteIn>& routes, float tolerance);

// The point `fraction` (0..1) of the way along a polyline's length.
V3 along(const std::vector<V3>& points, float fraction);
// The point `metres` along it (clamped to its ends), and the way it runs there (a yaw, radians) if asked.
V3 alongMetres(const std::vector<V3>& points, float metres, float* yaw = nullptr);
float routeLength(const std::vector<V3>& points);

}  // namespace illyria::game
