#include "illyria/game/gestures.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
constexpr float kTapSlopPx = 12.0f;
constexpr double kTapMaxMs = 450.0;
constexpr float kRotatePerPx = 0.006f;
constexpr float kPi = 3.14159265f;
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
            resetBaseline();
            return;
        }
        case PointerEvent::Move: {
            Pointer* p = find(id);
            if (p == nullptr) return;
            const float prevX = p->x, prevY = p->y;
            p->x = x;
            p->y = y;
            if (std::hypot(x - p->startX, y - p->startY) > kTapSlopPx * pixelScale_) tapCandidate_ = false;

            if (pointers_.size() == 1) {
                if (p->button == 0) {
                    camera_.dragGround(prevX, prevY, x, y);
                } else {
                    camera_.rotate((x - prevX) * kRotatePerPx);
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
            const bool tap = type == PointerEvent::Up && tapCandidate_ && pointers_.size() == 1 &&
                             timeMs - p->startMs <= kTapMaxMs;
            pointers_.erase(pointers_.begin() + (p - pointers_.data()));
            if (tap && onTap_) onTap_(x, y);
            // Lifting one finger of a pinch must not turn the other into a tap.
            if (!pointers_.empty()) tapCandidate_ = false;
            resetBaseline();
            return;
        }
    }
}

void Gestures::wheel(float deltaY, float x, float y) {
    camera_.zoomAt(std::exp(-deltaY * 0.0015f), x, y);
}

}  // namespace illyria::game
