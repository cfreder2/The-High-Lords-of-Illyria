// The ground markers of orders (Battle::orderMarks): a yellow ring where each
// soldier of a moving formation will stand, with a tick the way he will face;
// a red ring under the soldier they are after (it follows him) or on the
// spot or gate they shoot at, and a dotted red line from each attacker.

#include <algorithm>
#include <cmath>

#include "renderer.h"

namespace illyria::game {

namespace {
constexpr float kDecalRing = 2, kDecalDot = 7, kDecalArrow = 8;  // as renderer.cpp
constexpr float kYellow[3] = {1.0f, 0.86f, 0.18f};
constexpr float kRed[3] = {1.0f, 0.2f, 0.14f};
constexpr float kLineGap = 1.4f;  // metres between the dots of an attack line
constexpr int kMaxLineDots = 45;
}  // namespace

void pushOrderMarks(const Battle& battle, float seconds, std::vector<float>& decals) {
    const auto mark = [&](const V3& p, float yaw, float size, float type, const float* rgb) {
        decals.insert(decals.end(), {p.x, p.y, p.z, yaw, size, 0, type, rgb[0], rgb[1], rgb[2], 0});
    };
    // Each target once, however many are after it: a ring that breathes.
    const float pulse = 1.0f + 0.12f * std::sin(seconds * 6.0f);
    std::vector<V3> ringed;
    for (const Battle::OrderMark& m : battle.orderMarks()) {
        switch (m.kind) {
            case Battle::OrderMark::Kind::Slot:
                mark(m.pos, 0, m.size, kDecalRing, kYellow);
                mark(m.pos + m.face * (m.size * 0.95f), std::atan2(m.face.y, m.face.x), m.size * 0.42f, kDecalArrow, kYellow);
                break;
            case Battle::OrderMark::Kind::Target:
            case Battle::OrderMark::Kind::Spot: {
                const bool seen = std::any_of(ringed.begin(), ringed.end(), [&](const V3& r) {
                    return (r.x - m.pos.x) * (r.x - m.pos.x) + (r.y - m.pos.y) * (r.y - m.pos.y) < 0.04f;
                });
                if (!seen) {
                    ringed.push_back(m.pos);
                    mark(m.pos, 0, m.size * pulse, kDecalRing, kRed);
                    if (m.kind == Battle::OrderMark::Kind::Target) mark(m.pos, 0, m.size * 0.62f, kDecalRing, kRed);
                }
                // The line, from just outside the attacker to the ring's edge.
                const V3 d{m.pos.x - m.from.x, m.pos.y - m.from.y, 0};
                const float len = d.length();
                if (len < 2.0f) break;
                const V3 dir = d * (1.0f / len);
                const float end = len - m.size * 0.8f;
                const float gap = std::max(kLineGap, end / kMaxLineDots);
                // The dots creep toward the target.
                const float phase = std::fmod(seconds * 2.0f, 1.0f) * gap;
                for (float t = 0.8f + phase; t < end; t += gap) mark(m.from + dir * t, 0, 0.13f, kDecalDot, kRed);
                break;
            }
        }
    }
}

}  // namespace illyria::game
