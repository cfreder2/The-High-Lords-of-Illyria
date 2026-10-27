#pragma once

#include <functional>
#include <vector>

#include "illyria/game/camera.h"

namespace illyria::game {

enum class PointerEvent { Down, Move, Up, Cancel };

// Turns raw pointer input (touch, mouse or pen) into camera motion and taps.
//   one finger / left mouse drag   pan
//   two fingers                    pinch to zoom, drag to pan, twist to rotate
//   right or middle mouse drag     rotate
//   wheel                          zoom
//   short press without moving     tap (select / command)
class Gestures {
public:
    using TapFn = std::function<void(float x, float y)>;

    Gestures(Camera& camera, TapFn onTap) : camera_(camera), onTap_(std::move(onTap)) {}

    // `pixelScale` converts the platform's thresholds (CSS pixels) to event pixels.
    void setPixelScale(float s) { pixelScale_ = s; }

    // `button`: 0 primary (touch or left mouse), 1 middle, 2 right. `timeMs` is monotonic.
    void pointer(PointerEvent type, int id, float x, float y, int button, double timeMs);
    void wheel(float deltaY, float x, float y);

private:
    struct Pointer {
        int id;
        float x, y;
        float startX, startY;
        int button;
        double startMs;
    };

    Pointer* find(int id);
    void resetBaseline();

    Camera& camera_;
    TapFn onTap_;
    float pixelScale_ = 1.0f;
    std::vector<Pointer> pointers_;
    bool tapCandidate_ = false;  // the current gesture can still become a tap
    // Two-finger baseline.
    float pinchDist_ = 0, pinchAngle_ = 0, pinchMidX_ = 0, pinchMidY_ = 0;
};

}  // namespace illyria::game
