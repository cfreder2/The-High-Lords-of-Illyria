#pragma once

#include <functional>

#include "illyria/game/gmath.h"

namespace illyria::game {

// Orbit camera that spans god view to ground level. Pitch follows zoom: high
// up it looks almost straight down at the map, close in it tilts toward the
// horizon like a battlefield camera.
class Camera {
public:
    using HeightFn = std::function<float(float x, float y)>;

    static constexpr float kMinDistance = 6.0f;
    static constexpr float kMaxDistance = 14700.0f;  // a little beyond the whole continent
    static constexpr float kFovY = 0.785398f;  // 45 degrees

    void setBounds(float size) { setBounds(0, 0, size, size); }
    void setBounds(float minX, float minY, float maxX, float maxY) {
        minX_ = minX;
        minY_ = minY;
        maxX_ = maxX;
        maxY_ = maxY;
    }
    void setHeightFn(HeightFn fn) { heightFn_ = std::move(fn); }
    void setViewport(float width, float height) {
        width_ = width;
        height_ = height;
    }

    void lookAt(float x, float y, float distance);
    // Moves the target so the ground point under `fromPx` ends up under `toPx`.
    void dragGround(float fromX, float fromY, float toX, float toY);
    // Multiplies distance by 1/factor, keeping the ground point under (px, py) fixed.
    void zoomAt(float factor, float px, float py);
    void rotate(float radians) { yaw_ += radians; }

    V3 eye() const;
    V3 target() const { return {tx_, ty_, groundAt(tx_, ty_)}; }
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

    HeightFn heightFn_;
    float minX_ = 0, minY_ = 0, maxX_ = 1024.0f, maxY_ = 1024.0f;
    float width_ = 1, height_ = 1;
    float tx_ = 0, ty_ = 0;
    float distance_ = 300.0f;
    float yaw_ = 0.0f;
};

}  // namespace illyria::game
