// Where a man is put when the place he was given will not hold him (conquest_world.cpp's seating, world_keep.cpp's stations).
#pragma once

#include <algorithm>

#include "illyria/sim/fixed.h"
#include "illyria/sim/vec3.h"

namespace illyria::sim {

// The nearest place to p that fits, a ring at a time (round the compass in sixths of a turn per metre of radius), or p.
template <class Fits>
Vec3 nearestFit(const Vec3& p, const Fits& fits) {
    for (int ring = 0; ring <= 40 && !fits(p); ++ring) {
        for (int step = 0; step < std::max(1, 6 * ring); ++step) {
            static const int32_t kCos[24] = {1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259, 0, 259, 500, 707, 866, 966};
            static const int32_t kSin[24] = {0, 259, 500, 707, 866, 966, 1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259};
            const int32_t dir = step * 24 / std::max(1, 6 * ring);
            const Vec3 q{p.x + Fixed::fromRatio(kCos[dir] * ring, 1000), p.y + Fixed::fromRatio(kSin[dir] * ring, 1000), p.z};
            if (fits(q)) return q;
        }
    }
    return p;
}

}  // namespace illyria::sim
