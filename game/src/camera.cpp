#include "illyria/game/camera.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
constexpr float kPitchNear = 0.33f;  // ~19 degrees above the horizon at ground level
constexpr float kPitchNearDistance = 6.0f;  // at and below this the pitch stays at kPitchNear
constexpr float kZoomSmoothSeconds = 0.12f;  // the glide's time constant
constexpr float kEyeAboveGround = 0.4f;
// The view's centre height follows the ground: from kFocusFar metres of distance on with a time constant of
// kFocusSlowSeconds, close in with kFocusQuickSeconds (in between, in proportion), and exactly where the ground
// changes by less than kFocusStep (a pan over gentle land must not lag). A step (a shore, a cliff, a bank: the
// bed under water is metres below the shore) is always eased, so crossing it does not bump the view.
constexpr float kFocusNear = 80.0f, kFocusFar = 400.0f, kFocusSlowSeconds = 0.5f, kFocusQuickSeconds = 0.3f;
constexpr float kFocusStep = 0.5f;
constexpr float kPitchFar = 1.31f;   // ~75 degrees, near top-down at god view
}  // namespace

void Camera::lookAt(float x, float y, float distance) {
    tx_ = x;
    ty_ = y;
    distance_ = std::clamp(distance, kMinDistance, maxDistance_);
    goalLog_ = std::log(distance_);
    speedLog_ = 0;
    anchored_ = false;
    clampTarget();
    focusZ_ = groundAt(tx_, ty_);  // a jump to a place: centred on its ground at once
}

void Camera::followGround(float dtSeconds) {
    const float ground = groundAt(tx_, ty_);
    const float t = std::clamp((distance_ - kFocusNear) / (kFocusFar - kFocusNear), 0.0f, 1.0f);
    const float tau = kFocusQuickSeconds + t * (kFocusSlowSeconds - kFocusQuickSeconds);
    if (distance_ <= kFocusNear && std::abs(ground - focusZ_) < kFocusStep) focusZ_ = ground;
    else focusZ_ += (ground - focusZ_) * (1.0f - std::exp(-dtSeconds / tau));
}

float Camera::pitch() const {
    // Interpolate on a log scale so each pinch step feels the same; above
    // kTopDownDistance the view stays near top-down like the conquest map.
    constexpr float kTopDownDistance = 3000.0f;
    const float t = std::clamp(std::log(distance_ / kPitchNearDistance) / std::log(kTopDownDistance / kPitchNearDistance), 0.0f, 1.0f);
    const float s = t * t * (3 - 2 * t);
    return kPitchNear + (kPitchFar - kPitchNear) * s;
}

V3 Camera::eye() const {
    const float p = pitch();
    const V3 t = target();
    V3 e{t.x - std::cos(yaw_) * std::cos(p) * distance_, t.y - std::sin(yaw_) * std::cos(p) * distance_,
         t.z + std::sin(p) * distance_};
    // Never dip below the hills between the camera and its target.
    e.z = std::max(e.z, groundAt(e.x, e.y) + kEyeAboveGround);
    return e;
}

M4 Camera::view() const { return M4::lookAt(eye(), target(), {0, 0, 1}); }

M4 Camera::projection() const {
    const float zNear = std::max(0.1f, distance_ * 0.02f);
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
    anchored_ = false;
    const float planeZ = target().z;
    V3 a, b;
    if (!pickPlane(fromX, fromY, planeZ, a) || !pickPlane(toX, toY, planeZ, b)) return;
    tx_ += a.x - b.x;
    ty_ += a.y - b.y;
    clampTarget();
}

void Camera::pan(float right, float forward) {
    anchored_ = false;
    // Looking along yaw, the screen's right is a quarter turn clockwise of it.
    const float c = std::cos(yaw_), s = std::sin(yaw_);
    tx_ += c * forward + s * right;
    ty_ += s * forward - c * right;
    clampTarget();
}

void Camera::zoomAt(float factor, float px, float py) {
    if (!(factor > 0)) return;
    goalLog_ = std::clamp(goalLog_ - std::log(factor), std::log(kMinDistance), std::log(maxDistance_));
    // One anchor for a run of zooms about the same pixel, so the hills under the
    // eye as it moves never shift the plane the point is held on.
    if (anchored_ && std::abs(px - anchorPx_) < 2 && std::abs(py - anchorPy_) < 2) return;
    anchorPx_ = px;
    anchorPy_ = py;
    anchored_ = pickGround(px, py, anchor_) || pickPlane(px, py, target().z, anchor_);
}

void Camera::advance(float dtSeconds) {
    followGround(dtSeconds);
    const float x = std::log(distance_);
    if (std::abs(goalLog_ - x) < 1e-4f && std::abs(speedLog_) < 1e-4f) {
        distance_ = std::exp(goalLog_);
        speedLog_ = 0;
        anchored_ = false;
        return;
    }
    // Critically damped spring (Unity's SmoothDamp): stable for any step.
    const float omega = 2.0f / kZoomSmoothSeconds, h = omega * dtSeconds;
    const float decay = 1.0f / (1.0f + h + 0.48f * h * h + 0.235f * h * h * h);
    const float change = x - goalLog_;
    const float temp = (speedLog_ + omega * change) * dtSeconds;
    speedLog_ = (speedLog_ - omega * temp) * decay;
    distance_ = std::clamp(std::exp(goalLog_ + (change + temp) * decay), kMinDistance, maxDistance_);
    holdAnchor();
}

void Camera::holdAnchor() {
    if (!anchored_) return;
    for (int i = 0; i < 2; ++i) {  // twice: the target's height moves the eye as it slides
        V3 now;
        if (!pickPlane(anchorPx_, anchorPy_, anchor_.z, now)) return;
        tx_ += anchor_.x - now.x;
        ty_ += anchor_.y - now.y;
        clampTarget();
    }
}

void Camera::clampTarget() {
    tx_ = std::clamp(tx_, minX_, maxX_);
    ty_ = std::clamp(ty_, minY_, maxY_);
    if (radius_ > 0) {
        const float dx = tx_ - cx_, dy = ty_ - cy_, d = std::sqrt(dx * dx + dy * dy);
        if (d > radius_) {
            tx_ = cx_ + dx * radius_ / d;
            ty_ = cy_ + dy * radius_ / d;
        }
    }
    // Close in, the view is centred on the ground exactly, whatever moved it (a pan, a held drag).
    if (distance_ <= kFocusNear) {
        const float g = groundAt(tx_, ty_);
        if (std::abs(g - focusZ_) < kFocusStep) focusZ_ = g;
    }
}

}  // namespace illyria::game
