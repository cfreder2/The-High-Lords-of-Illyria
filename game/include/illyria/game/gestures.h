#pragma once

#include <functional>
#include <vector>

#include "illyria/game/camera.h"

namespace illyria::game {

enum class PointerEvent { Down, Move, Up, Cancel };

// Turns raw input (touch, mouse, pen, trackpad, keys) into camera motion and taps.
//   one finger / left mouse drag   pan
//   two fingers                    pinch to zoom, drag to pan, twist to rotate
//   right or middle mouse drag     rotate
//   wheel                          zoom
//   trackpad: two-finger swipe     pan (scroll); pinch and twist: zoom and rotate (pinch)
//   short press without moving     tap (select / command)
//   right click without moving     secondary tap (an order, never a selection)
//   press held still, then drag    hold: a selection box or a facing line (if
//                                  the hold handler takes it; else it pans)
//   shift + drag (desktop)         hold at once (a box)
//   arrow keys held (desktop)      pan; with shift, left and right rotate (setKeys)
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
    // A right-button click that does not drag (a trackpad's two-finger tap too).
    void setSecondaryTap(TapFn fn) { onSecondaryTap_ = std::move(fn); }

    // `pixelScale` converts the platform's thresholds (CSS pixels) to event pixels.
    void setPixelScale(float s) { pixelScale_ = s; }
    // Shift held (desktop): a press with the primary button starts a hold at once.
    void setShift(bool down) { shift_ = down; }

    // `button`: 0 primary (touch or left mouse), 1 middle, 2 right. `timeMs` is monotonic.
    void pointer(PointerEvent type, int id, float x, float y, int button, double timeMs);
    void wheel(float deltaY, float x, float y);
    // A trackpad's two-finger swipe: pans as a drag of (-dx, -dy) pixels from (x, y) would.
    void scroll(float dx, float dy, float x, float y);
    // A trackpad's pinch: zoom by `factor` about (x, y), and turn by `radians` (a twist).
    void pinch(float factor, float radians, float x, float y);
    // Keys held (desktop), each -1, 0 or 1: pan right / forward (up the
    // screen), and turn (1: the view turns left). The camera eases to the
    // pace, which grows with the zoom distance.
    void setKeys(int right, int forward, int turn);
    // Turn the view to `yaw` over a moment (N: north up); any other turning stops it.
    void turnTo(float yaw);
    bool turning() const { return turning_; }
    // Called every frame: a press held still for kHoldMs becomes a hold; held keys move the camera.
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
    void moveByKeys(double timeMs);

    Camera& camera_;
    TapFn onTap_;
    TapFn onSecondaryTap_;
    HoldFn onHold_;
    HoldMoveFn onHoldMove_;
    HoldEndFn onHoldEnd_;
    bool shift_ = false;
    bool holding_ = false;
    bool holdTried_ = false;  // this press was offered as a hold already
    float pixelScale_ = 1.0f;
    std::vector<Pointer> pointers_;
    bool tapCandidate_ = false;  // the current gesture can still become a tap
    bool secondaryCandidate_ = false;  // ... or a right click
    // Keys: what is held, the eased pace (-1..1 each), and the last update.
    int keyRight_ = 0, keyForward_ = 0, keyTurn_ = 0;
    float paceRight_ = 0, paceForward_ = 0, paceTurn_ = 0;
    double lastUpdateMs_ = -1;
    bool turning_ = false;
    float turnToYaw_ = 0;
    // Two-finger baseline.
    float pinchDist_ = 0, pinchAngle_ = 0, pinchMidX_ = 0, pinchMidY_ = 0;
};

}  // namespace illyria::game
