#pragma once

#include "illyria/sim/vec3.h"

namespace illyria::sim {

struct LaunchSolution {
    Vec3 velocity;
    // False when the target is beyond reach at this muzzle speed; the
    // velocity is then the 45-degree maximum-range shot toward it.
    bool inRange = false;
};

// Velocity needed to hit `target` from `origin` along the low (flat) arc,
// given muzzle speed and downward gravity (positive, along -z). Ignores wind:
// archers aim at where they see the target and the wind is part of the miss.
LaunchSolution solveLowArc(const Vec3& origin, const Vec3& target, Fixed speed, Fixed gravity);

// The same along the high (lobbed) arc, over a wall or a wood in the way.
LaunchSolution solveHighArc(const Vec3& origin, const Vec3& target, Fixed speed, Fixed gravity);

// Velocity needed to hit `target` from `origin` launching at a fixed
// elevation (its tangent; a catapult's arm always stops against the same
// crossbar) with whatever speed that takes, as an engineer winds the skein
// tighter or looser for the range. Beyond `maxSpeed` it throws as hard as it
// can at that elevation and inRange is false.
LaunchSolution solveFixedElevation(const Vec3& origin, const Vec3& target, Fixed tanElevation, Fixed maxSpeed, Fixed gravity);

}  // namespace illyria::sim
