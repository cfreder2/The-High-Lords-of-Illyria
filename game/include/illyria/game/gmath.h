#pragma once

// Float math for presentation only (camera, rendering, input). Nothing here
// may feed back into the deterministic simulation except through a Command.

#include <cmath>

#include "illyria/sim/vec3.h"

namespace illyria::game {

struct V3 {
    float x = 0, y = 0, z = 0;

    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3& operator+=(const V3& o) { return *this = *this + o; }
    float dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
    V3 cross(const V3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    V3 normalized() const {
        const float l = length();
        return l > 0 ? *this * (1.0f / l) : V3{};
    }
};

inline V3 lerp(const V3& a, const V3& b, float t) { return a + (b - a) * t; }

inline V3 toV3(const sim::Vec3& v) {
    return {static_cast<float>(v.x.toDouble()), static_cast<float>(v.y.toDouble()),
            static_cast<float>(v.z.toDouble())};
}

// Player input is quantised to the simulation grid here, on the issuing
// client only; the resulting Command is what every peer receives.
inline sim::Fixed toFixed(float f) {
    return sim::Fixed::fromRaw(static_cast<int64_t>(std::llround(static_cast<double>(f) * sim::Fixed::kOneRaw)));
}
inline sim::Vec3 toSim(const V3& v) { return {toFixed(v.x), toFixed(v.y), toFixed(v.z)}; }

// Column-major 4x4 matrix, as OpenGL expects.
struct M4 {
    float m[16] = {};

    static M4 identity() {
        M4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    M4 operator*(const M4& o) const {
        M4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    static M4 perspective(float fovY, float aspect, float zNear, float zFar) {
        const float f = 1.0f / std::tan(fovY / 2);
        M4 r;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1;
        r.m[14] = 2 * zFar * zNear / (zNear - zFar);
        return r;
    }
    static M4 ortho(float l, float r, float b, float t, float n, float f) {
        M4 o = identity();
        o.m[0] = 2 / (r - l);
        o.m[5] = 2 / (t - b);
        o.m[10] = -2 / (f - n);
        o.m[12] = -(r + l) / (r - l);
        o.m[13] = -(t + b) / (t - b);
        o.m[14] = -(f + n) / (f - n);
        return o;
    }
    static M4 lookAt(const V3& eye, const V3& at, const V3& up) {
        const V3 f = (at - eye).normalized();
        const V3 s = f.cross(up).normalized();
        const V3 u = s.cross(f);
        M4 r = identity();
        r.m[0] = s.x, r.m[4] = s.y, r.m[8] = s.z;
        r.m[1] = u.x, r.m[5] = u.y, r.m[9] = u.z;
        r.m[2] = -f.x, r.m[6] = -f.y, r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }
};

}  // namespace illyria::game
