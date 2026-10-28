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
//   press held still, then drag    hold: a selection box or a facing line (if
//                                  the hold handler takes it; else it pans)
//   shift + drag (desktop)         hold at once (a box)
class Gestures {
public:
    using TapFn = std::function<void(float x, float y)>;
    // A hold begins: return false to leave the press to the camera. `box`: shift was down.
    using HoldFn = std::function<bool(float x, float y, bool box)>;
    using HoldMoveFn = std::function<void(float x, float y)>;
    using HoldEndFn = std::function<void(float x, float y, bool cancelled)>;

    static constexpr double kHoldMs = 300.0;

    Gestures(Camera& camera, TapFn onTap) : camera_(camera), onTap_(std::move(onTap)) {}
    void setHold(HoldFn begin, HoldMoveFn move, HoldEndFn end) {
        onHold_ = std::move(begin);
        onHoldMove_ = std::move(move);
        onHoldEnd_ = std::move(end);
    }

    // `pixelScale` converts the platform's thresholds (CSS pixels) to event pixels.
    void setPixelScale(float s) { pixelScale_ = s; }
    // Shift held (desktop): a press with the primary button starts a hold at once.
    void setShift(bool down) { shift_ = down; }

    // `button`: 0 primary (touch or left mouse), 1 middle, 2 right. `timeMs` is monotonic.
    void pointer(PointerEvent type, int id, float x, float y, int button, double timeMs);
    void wheel(float deltaY, float x, float y);
    // Called every frame: a press held still for kHoldMs becomes a hold.
    void update(double timeMs);
    bool holding() const { return holding_; }

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

    void endHold(float x, float y, bool cancelled);

    Camera& camera_;
    TapFn onTap_;
    HoldFn onHold_;
    HoldMoveFn onHoldMove_;
    HoldEndFn onHoldEnd_;
    bool shift_ = false;
    bool holding_ = false;
    bool holdTried_ = false;  // this press was offered as a hold already
    float pixelScale_ = 1.0f;
    std::vector<Pointer> pointers_;
    bool tapCandidate_ = false;  // the current gesture can still become a tap
    // Two-finger baseline.
    float pinchDist_ = 0, pinchAngle_ = 0, pinchMidX_ = 0, pinchMidY_ = 0;
};

}  // namespace illyria::game
