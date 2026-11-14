#pragma once

#include <algorithm>
#include <cmath>
#include <functional>

#include "illyria/game/gmath.h"

namespace illyria::game {

// Orbit camera that spans god view to ground level. Pitch follows zoom: high
// up it looks almost straight down at the map, close in it tilts toward the
// horizon like a battlefield camera.
class Camera {
public:
    using HeightFn = std::function<float(float x, float y)>;

    static constexpr float kMinDistance = 2.0f;  // right down to a single man: he fills most of the screen
    static constexpr float kMaxDistance = 4900.0f;  // a little beyond the whole continent
    static constexpr float kFovY = 0.785398f;  // 45 degrees

    void setBounds(float size) { setBounds(0, 0, size, size); }
    void setBounds(float minX, float minY, float maxX, float maxY) {
        minX_ = minX;
        minY_ = minY;
        maxX_ = maxX;
        maxY_ = maxY;
    }
    // A match's field (Battle::zone): the view's centre kept within `radius`
    // of (cx, cy) and the camera no further out than `maxDistance`, so the
    // match never shows (or loads) the continent beyond it. Radius zero: no circle.
    void setCircle(float cx, float cy, float radius, float maxDistance) {
        cx_ = cx;
        cy_ = cy;
        radius_ = radius;
        maxDistance_ = radius > 0 ? std::clamp(maxDistance, kMinDistance, kMaxDistance) : kMaxDistance;
        distance_ = std::min(distance_, maxDistance_);
        goalLog_ = std::min(goalLog_, std::log(maxDistance_));
        clampTarget();
    }
    float maxDistance() const { return maxDistance_; }
    void setHeightFn(HeightFn fn) {
        heightFn_ = std::move(fn);
        focusZ_ = groundAt(tx_, ty_);
    }
    void setViewport(float width, float height) {
        width_ = width;
        height_ = height;
    }

    void lookAt(float x, float y, float distance);
    // Moves the target so the ground point under `fromPx` ends up under `toPx`.
    void dragGround(float fromX, float fromY, float toX, float toY);
    // Asks for the distance divided by `factor`, the ground point under (px, py)
    // staying under it as the view glides there (advance). The ask is clamped to
    // [kMinDistance, maxDistance], so scrolling past a limit stores nothing.
    // Every input (wheel, trackpad, pinch, keys) comes through here.
    void zoomAt(float factor, float px, float py);
    // Glides the distance toward the ask, critically damped on its logarithm.
    void advance(float dtSeconds);
    // Moves the target over the ground by metres along the view's right and its forward (up the screen).
    void pan(float right, float forward);
    void rotate(float radians) { yaw_ += radians; }
    V3 eye() const;
    // The point the view is centred on. Close to the ground its height is the ground's; from high above it follows
    // the ground slowly (a low-pass), so crossing mountains does not bob the view. The eye alone is lifted at once
    // where it would meet the ground (eye()).
    V3 target() const { return {tx_, ty_, focusZ_}; }
    float distance() const { return distance_; }
    float yaw() const { return yaw_; }
    float pitch() const;
    float width() const { return width_; }
    float height() const { return height_; }

    M4 view() const;
    M4 projection() const;
    M4 viewProjection() const { return projection() * view(); }

    // World-space ray through a pixel (origin, unit direction).
    void ray(float px, float py, V3& origin, V3& dir) const;
    // Where the ray through a pixel meets the terrain; false if it misses.
    bool pickGround(float px, float py, V3& hit) const;
    // Pixel position of a world point; false if behind the camera.
    bool project(const V3& p, float& px, float& py) const;

private:
    float groundAt(float x, float y) const { return heightFn_ ? heightFn_(x, y) : 0.0f; }
    // Intersects a pixel ray with the horizontal plane z = planeZ.
    bool pickPlane(float px, float py, float planeZ, V3& hit) const;
    void clampTarget();
    // Moves the target so the anchored ground point is back under its pixel.
    void holdAnchor();

    HeightFn heightFn_;
    float minX_ = 0, minY_ = 0, maxX_ = 1024.0f, maxY_ = 1024.0f;
    float width_ = 1, height_ = 1;
    float tx_ = 0, ty_ = 0;
    float distance_ = 300.0f;
    // The asked-for log distance, the rate the glide is moving at, and the
    // ground point (and the pixel) a zoom holds still while it glides.
    float goalLog_ = std::log(300.0f), speedLog_ = 0;
    bool anchored_ = false;
    V3 anchor_{};
    float anchorPx_ = 0, anchorPy_ = 0;
    float anchorLeft_ = 0;  // seconds the anchor may still be held after the glide ends
    float maxDistance_ = kMaxDistance;
    float cx_ = 0, cy_ = 0, radius_ = 0;
    float yaw_ = 0.0f;
    float focusZ_ = 0;  // the height the view is centred on (target().z)
    void followGround(float dtSeconds);
};

}  // namespace illyria::game
