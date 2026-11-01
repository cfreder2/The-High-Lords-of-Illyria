#include "illyria/game/gestures.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
constexpr float kTapSlopPx = 12.0f;
constexpr double kTapMaxMs = 450.0;
constexpr float kRotatePerPx = 0.006f;
constexpr float kPi = 3.14159265f;
// Keys: the view crosses about 0.9 of its own distance a second (about two
// thirds of the screen's width), turns a full circle in about 4.5 s, and
// eases to and from that pace in about a tenth of a second.
constexpr float kKeyPanPerSecond = 0.9f;
constexpr float kKeyTurnPerSecond = 1.4f;
constexpr float kKeyZoomPerSecond = 1.2f;  // e-fold of the distance a second: twice as near in 0.6 s
constexpr double kKeyEaseMs = 90.0;
// Turning to a heading (turnTo): nearly there in a third of a second.
constexpr double kTurnEaseMs = 90.0;
}  // namespace

Gestures::Pointer* Gestures::find(int id) {
    for (Pointer& p : pointers_)
        if (p.id == id) return &p;
    return nullptr;
}

void Gestures::resetBaseline() {
    if (pointers_.size() < 2) return;
    const Pointer& a = pointers_[0];
    const Pointer& b = pointers_[1];
    pinchDist_ = std::max(1.0f, std::hypot(b.x - a.x, b.y - a.y));
    pinchAngle_ = std::atan2(b.y - a.y, b.x - a.x);
    pinchMidX_ = (a.x + b.x) / 2;
    pinchMidY_ = (a.y + b.y) / 2;
}

void Gestures::pointer(PointerEvent type, int id, float x, float y, int button, double timeMs) {
    switch (type) {
        case PointerEvent::Down: {
            if (find(id) != nullptr) return;
            pointers_.push_back({id, x, y, x, y, button, timeMs});
            tapCandidate_ = pointers_.size() == 1 && button == 0;
            secondaryCandidate_ = pointers_.size() == 1 && button == 2;
            holdTried_ = false;
            // A second finger: a pinch, not a box or a line.
            if (holding_ && pointers_.size() > 1) endHold(x, y, true);
            if (tapCandidate_ && shift_ && onHold_ && onHold_(x, y, true)) {
                holding_ = true;
                tapCandidate_ = false;
            }
            resetBaseline();
            return;
        }
        case PointerEvent::Move: {
            Pointer* p = find(id);
            if (p == nullptr) return;
            const float prevX = p->x, prevY = p->y;
            p->x = x;
            p->y = y;
            if (holding_) {
                if (onHoldMove_) onHoldMove_(x, y);
                return;
            }
            update(timeMs);
            if (holding_) return;
            if (std::hypot(x - p->startX, y - p->startY) > kTapSlopPx * pixelScale_) tapCandidate_ = secondaryCandidate_ = false;

            if (pointers_.size() == 1) {
                if (p->button == 0) {
                    camera_.dragGround(prevX, prevY, x, y);
                } else {
                    camera_.rotate((x - prevX) * kRotatePerPx);
                    if (x != prevX) turning_ = false;
                }
            } else if (pointers_.size() >= 2 && (p == &pointers_[0] || p == &pointers_[1])) {
                const Pointer& a = pointers_[0];
                const Pointer& b = pointers_[1];
                const float dist = std::max(1.0f, std::hypot(b.x - a.x, b.y - a.y));
                const float angle = std::atan2(b.y - a.y, b.x - a.x);
                const float midX = (a.x + b.x) / 2, midY = (a.y + b.y) / 2;
                camera_.dragGround(pinchMidX_, pinchMidY_, midX, midY);
                camera_.zoomAt(dist / pinchDist_, midX, midY);
                float dAngle = angle - pinchAngle_;
                if (dAngle > kPi) dAngle -= 2 * kPi;
                if (dAngle < -kPi) dAngle += 2 * kPi;
                camera_.rotate(-dAngle);
                if (dAngle != 0) turning_ = false;
                pinchDist_ = dist;
                pinchAngle_ = angle;
                pinchMidX_ = midX;
                pinchMidY_ = midY;
            }
            return;
        }
        case PointerEvent::Up:
        case PointerEvent::Cancel: {
            Pointer* p = find(id);
            if (p == nullptr) return;
            const bool quick = type == PointerEvent::Up && pointers_.size() == 1 && timeMs - p->startMs <= kTapMaxMs;
            const bool tap = quick && tapCandidate_;
            const bool secondary = quick && secondaryCandidate_;
            pointers_.erase(pointers_.begin() + (p - pointers_.data()));
            if (holding_) endHold(x, y, type == PointerEvent::Cancel);
            if (tap && onTap_) onTap_(x, y);
            if (secondary && onSecondaryTap_) onSecondaryTap_(x, y);
            // Lifting one finger of a pinch must not turn the other into a tap.
            if (!pointers_.empty()) tapCandidate_ = secondaryCandidate_ = false;
            resetBaseline();
            return;
        }
    }
}

void Gestures::update(double timeMs) {
    moveByKeys(timeMs);
    // Held still long enough: the press is the hold handler's, if it wants it.
    if (holding_ || holdTried_ || !tapCandidate_ || pointers_.size() != 1 || !onHold_) return;
    const Pointer& p = pointers_.front();
    if (timeMs - p.startMs < kHoldMs) return;
    holdTried_ = true;
    holding_ = onHold_(p.x, p.y, false);
    if (holding_) tapCandidate_ = false;  // else a slow tap is still a tap
}

void Gestures::endHold(float x, float y, bool cancelled) {
    holding_ = false;
    if (onHoldEnd_) onHoldEnd_(x, y, cancelled);
}

void Gestures::wheel(float deltaY, float x, float y) {
    camera_.zoomAt(std::exp(-deltaY * 0.0015f), x, y);
}

void Gestures::scroll(float dx, float dy, float x, float y) {
    // Fingers swiped up scroll a page on (dy > 0): the land goes up with them, as under a drag.
    camera_.dragGround(x, y, x - dx, y - dy);
}

void Gestures::pinch(float factor, float radians, float x, float y) {
    if (factor > 0) camera_.zoomAt(factor, x, y);
    if (radians != 0) {
        camera_.rotate(radians);
        turning_ = false;
    }
}

void Gestures::setKeys(int right, int forward, int turn, int zoom) {
    keyRight_ = std::clamp(right, -1, 1);
    keyForward_ = std::clamp(forward, -1, 1);
    keyTurn_ = std::clamp(turn, -1, 1);
    keyZoom_ = std::clamp(zoom, -1, 1);
    if (keyTurn_ != 0) turning_ = false;
}

void Gestures::turnTo(float yaw) {
    turning_ = true;
    turnToYaw_ = yaw;
}

void Gestures::moveByKeys(double timeMs) {
    const double dtMs = lastUpdateMs_ < 0 ? 0.0 : std::clamp(timeMs - lastUpdateMs_, 0.0, 100.0);
    lastUpdateMs_ = std::max(lastUpdateMs_, timeMs);
    if (dtMs <= 0) return;
    const float dt = static_cast<float>(dtMs / 1000.0);
    // Ease toward the keys' pace: a tap of a key nudges, a hold glides, and the
    // view stops within a tenth of a second or so of the key's release.
    const float k = static_cast<float>(1.0 - std::exp(-dtMs / kKeyEaseMs));
    float* paces[] = {&paceRight_, &paceForward_, &paceTurn_, &paceZoom_};
    const int keys[] = {keyRight_, keyForward_, keyTurn_, keyZoom_};
    for (int i = 0; i < 4; ++i) {
        *paces[i] += (static_cast<float>(keys[i]) - *paces[i]) * k;
        if (std::abs(*paces[i]) < 0.01f) *paces[i] = 0;
    }
    if (paceRight_ != 0 || paceForward_ != 0) {
        const float metres = camera_.distance() * kKeyPanPerSecond * dt;
        camera_.pan(paceRight_ * metres, paceForward_ * metres);
    }
    if (paceTurn_ != 0) camera_.rotate(paceTurn_ * kKeyTurnPerSecond * dt);
    if (paceZoom_ != 0) camera_.zoomAt(std::exp(paceZoom_ * kKeyZoomPerSecond * dt), camera_.width() / 2, camera_.height() / 2);
    if (turning_) {
        const float left = std::remainder(turnToYaw_ - camera_.yaw(), 2 * kPi);
        if (std::abs(left) < 0.002f) {
            camera_.rotate(left);
            turning_ = false;
        } else {
            camera_.rotate(left * static_cast<float>(1.0 - std::exp(-dtMs / kTurnEaseMs)));
        }
    }
}

}  // namespace illyria::game
