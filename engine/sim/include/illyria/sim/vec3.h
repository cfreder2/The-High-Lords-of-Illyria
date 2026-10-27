#pragma once

#include "illyria/sim/fixed.h"

namespace illyria::sim {

// World-space vector. Convention: z is up, 1.0 == 1 metre.
struct Vec3 {
    Fixed x, y, z;

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(Fixed s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(Fixed s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(const Vec3& o) { return *this = *this + o; }
    constexpr Vec3& operator-=(const Vec3& o) { return *this = *this - o; }
    constexpr bool operator==(const Vec3&) const = default;

    constexpr Fixed dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Fixed lengthSq() const { return dot(*this); }
    Fixed length() const { return sqrt(lengthSq()); }
    // Horizontal (ground-plane) projection.
    constexpr Vec3 flat() const { return {x, y, Fixed::zero()}; }
};

}  // namespace illyria::sim
