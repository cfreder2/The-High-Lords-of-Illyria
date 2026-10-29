#include "illyria/game/camera.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
constexpr float kPitchNear = 0.33f;  // ~19 degrees above the horizon at ground level
constexpr float kPitchFar = 1.31f;   // ~75 degrees, near top-down at god view
}  // namespace

void Camera::lookAt(float x, float y, float distance) {
    tx_ = x;
    ty_ = y;
    distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
    clampTarget();
}

float Camera::pitch() const {
    // Interpolate on a log scale so each pinch step feels the same; above
    // kTopDownDistance the view stays near top-down like the campaign map.
    constexpr float kTopDownDistance = 3000.0f;
    const float t = std::clamp(std::log(distance_ / kMinDistance) / std::log(kTopDownDistance / kMinDistance), 0.0f, 1.0f);
    const float s = t * t * (3 - 2 * t);
    return kPitchNear + (kPitchFar - kPitchNear) * s;
}

V3 Camera::eye() const {
    const float p = pitch();
    const V3 t = target();
    V3 e{t.x - std::cos(yaw_) * std::cos(p) * distance_, t.y - std::sin(yaw_) * std::cos(p) * distance_,
         t.z + std::sin(p) * distance_};
    // Never dip below the hills between the camera and its target.
    e.z = std::max(e.z, groundAt(e.x, e.y) + 2.0f);
    return e;
}

M4 Camera::view() const { return M4::lookAt(eye(), target(), {0, 0, 1}); }

M4 Camera::projection() const {
    const float zNear = std::max(0.2f, distance_ * 0.02f);
    const float zFar = distance_ * 6.0f + 2000.0f;
    return M4::perspective(kFovY, width_ / height_, zNear, zFar);
}

void Camera::ray(float px, float py, V3& origin, V3& dir) const {
    origin = eye();
    const V3 f = (target() - origin).normalized();
    const V3 r = f.cross({0, 0, 1}).normalized();
    const V3 u = r.cross(f);
    const float tanHalf = std::tan(kFovY / 2);
    const float nx = (2 * px / width_ - 1) * tanHalf * width_ / height_;
    const float ny = (1 - 2 * py / height_) * tanHalf;
    dir = (f + r * nx + u * ny).normalized();
}

bool Camera::pickPlane(float px, float py, float planeZ, V3& hit) const {
    V3 o, d;
    ray(px, py, o, d);
    if (d.z > -1e-4f) return false;
    hit = o + d * ((planeZ - o.z) / d.z);
    return true;
}

bool Camera::pickGround(float px, float py, V3& hit) const {
    V3 o, d;
    ray(px, py, o, d);
    const float maxT = distance_ * 6.0f + 2000.0f;
    const float step = std::max(0.5f, distance_ * 0.01f);
    float prevT = 0;
    for (float t = step; t < maxT; t += step) {
        const V3 p = o + d * t;
        if (p.z <= groundAt(p.x, p.y)) {
            // Refine between the last point above ground and this one.
            float lo = prevT, hi = t;
            for (int i = 0; i < 16; ++i) {
                const float mid = (lo + hi) / 2;
                const V3 q = o + d * mid;
                (q.z <= groundAt(q.x, q.y) ? hi : lo) = mid;
            }
            hit = o + d * hi;
            return true;
        }
        prevT = t;
    }
    return false;
}

bool Camera::project(const V3& p, float& px, float& py) const {
    const M4 vp = viewProjection();
    const float* m = vp.m;
    const float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (cw <= 0) return false;
    px = (cx / cw + 1) * 0.5f * width_;
    py = (1 - cy / cw) * 0.5f * height_;
    return true;
}

void Camera::dragGround(float fromX, float fromY, float toX, float toY) {
    const float planeZ = target().z;
    V3 a, b;
    if (!pickPlane(fromX, fromY, planeZ, a) || !pickPlane(toX, toY, planeZ, b)) return;
    tx_ += a.x - b.x;
    ty_ += a.y - b.y;
    clampTarget();
}

void Camera::zoomAt(float factor, float px, float py) {
    const float planeZ = target().z;
    V3 before;
    const bool anchored = pickPlane(px, py, planeZ, before);
    // Never closer than the sprites allow (setMinDistance), nor nearer than now if already inside that.
    distance_ = std::clamp(distance_ / factor, std::min(minDistance_, distance_), kMaxDistance);
    V3 after;
    if (anchored && pickPlane(px, py, planeZ, after)) {
        tx_ += before.x - after.x;
        ty_ += before.y - after.y;
    }
    clampTarget();
}

void Camera::clampTarget() {
    tx_ = std::clamp(tx_, minX_, maxX_);
    ty_ = std::clamp(ty_, minY_, maxY_);
}

}  // namespace illyria::game
