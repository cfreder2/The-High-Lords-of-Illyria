// What the gate code (world_gates.cpp) and the garrisons' stations (world_keep.cpp) both ask of a gate's ground.
#pragma once

#include "illyria/sim/region.h"

namespace illyria::sim {

// The point of gate g's line nearest p (flat).
inline Vec3 nearestOnGate(const Gate& g, const Vec3& p) {
    const Vec3 ab = (g.b - g.a).flat();
    const Fixed len2 = ab.lengthSq();
    const Fixed t = len2 > Fixed::zero() ? clamp((p - g.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
    return g.a + ab * t;
}

// A place an order sends someone to is within a ring of walls if it lies
// inside them or up on their walk (whose outer half lies beyond the line of
// its corners): either way the way there is from inside, by the ramps.
inline bool withinRing(const Rampart& ring, const Vec3& p) {
    if (Gate::inside(ring, p)) return true;
    const Fixed reach = ring.halfThick + Fixed::fromRatio(1, 2);
    const size_t n = ring.corners.size();
    for (size_t e = 0; e < n; ++e) {
        const Vec3 a = ring.corners[e], ab = (ring.corners[(e + 1) % n] - a).flat();
        const Fixed len2 = ab.lengthSq();
        const Fixed t = len2 > Fixed::zero() ? clamp((p - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
        if ((p - (a + ab * t)).flat().lengthSq() <= reach * reach) return true;
    }
    return false;
}

// Whether someone standing at p is on the town's side of gate g: by the
// doors themselves near the gate (in the passage, behind the doors, he is
// still outside the line of the walk), by the ring's walls farther off.
inline bool townSide(const Gate& g, const Rampart& ring, const Vec3& p) {
    const Fixed near = (g.b - g.a).flat().length() / 2 + ring.halfThick * 2 + Fixed::fromInt(2);
    if ((p - (g.a + g.b) * Fixed::fromRatio(1, 2)).flat().lengthSq() <= near * near) return (p - g.a).flat().dot(g.out) < Fixed::zero();
    return Gate::inside(ring, p);
}

}  // namespace illyria::sim
