// The ground markers of orders (Battle::orderMarks): a yellow ring where each
// soldier of a moving formation will stand, with a tick the way he will face;
// a red ring under the soldier they are after (it follows him) or on the
// spot or gate they shoot at, and a dotted red line from each attacker.
// pushPathLines draws Battle::pathLines the same way: where each group is going.
// They lie on the floor (Battle::floorAt): the ground, or the wall-walk,
// stair or bridge deck there; the decal shader drapes them over the ground.

#include <algorithm>
#include <cmath>

#include "renderer.h"

namespace illyria::game {

namespace {
using Stance = sim::World::Stance;
constexpr float kDecalRing = 2, kDecalDot = 7, kDecalArrow = 8;  // as renderer.cpp
constexpr float kYellow[3] = {1.0f, 0.86f, 0.18f};
constexpr float kRed[3] = {1.0f, 0.2f, 0.14f};
constexpr float kLineGap = 1.4f;  // metres between the dots of an attack line
constexpr int kMaxLineDots = 45;
}  // namespace

void pushOrderMarks(const Battle& battle, float seconds, std::vector<float>& decals) {
    float fade = 0;  // 1 - the mark's opacity (decal extra.w)
    const auto mark = [&](const V3& p, float yaw, float size, float type, const float* rgb) {
        decals.insert(decals.end(), {p.x, p.y, p.z, yaw, size, 0, type, rgb[0], rgb[1], rgb[2], fade});
    };
    // Each target once, however many are after it: a ring that breathes.
    const float pulse = 1.0f + 0.12f * std::sin(seconds * 6.0f);
    std::vector<V3> ringed;
    for (const Battle::OrderMark& m : battle.orderMarks()) {
        fade = 1.0f - m.opacity;
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
                for (float t = 0.8f + phase; t < end; t += gap) {
                    V3 p = m.from + dir * t;
                    p.z = battle.floorAt(p);  // off the wall-walk, down on the ground
                    mark(p, 0, 0.13f, kDecalDot, kRed);
                }
                break;
            }
        }
    }
}

// --- Paths --------------------------------------------------------------------------------------------------------
// Three line languages, apart by shape as well as colour (for the colour-blind): a patrol (he fights what he meets)
// is a dashed line with chevrons ending in an open ring; a hunt (he ignores all but his quarry) a solid line with
// arrowheads ending in a cross-hair on the quarry; a plain move (a forced march, deaf to the enemy) a dotted line
// ending in a solid dot. Their widths and spacing are in screen pixels, so they read at 2 m and at 4900 m.

namespace {
constexpr float kDecalDash = 9, kDecalCross = 10;
constexpr float kPatrol[3] = {0.35f, 0.95f, 0.72f};
constexpr float kHunt[3] = {1.0f, 0.3f, 0.1f};
constexpr float kMove[3] = {0.92f, 0.92f, 0.92f};
constexpr int kStepsPerLine = 600;     // however long, however near the eye
constexpr size_t kMaxPathDecals = 3000;  // a frame's whole share (each mark has a dark one beneath it)
constexpr float kDark[3] = {0.03f, 0.03f, 0.04f};
constexpr float kOutlinePx = 2.0f;      // how much wider the dark mark under each is, all round

const float* colourOf(sim::World::Stance s) {
    return s == sim::World::Stance::Hunt ? kHunt : s == sim::World::Stance::Move ? kMove : kPatrol;
}
}  // namespace

size_t pushPathLines(const Battle& battle, const Camera& camera, float seconds, std::vector<float>& decals) {
    const bool close = camera.distance() < 60.0f && battle.selectionSize() <= 12;
    const std::vector<RouteLine> lines = battle.pathLines(close, camera.distance() > 600.0f);
    if (lines.empty()) return 0;
    const V3 eye = camera.eye();
    const float perPixel = 2.0f * std::tan(Camera::kFovY * 0.5f) / std::max(1.0f, camera.height());
    const float w = camera.width(), h = camera.height();
    size_t pushed = 0;
    std::vector<float> bright;  // the coloured marks, laid after every dark one so that no outline cuts across a line
    for (const RouteLine& line : lines) {
        if (line.points.size() < 2) continue;
        const float* rgb = colourOf(line.stance);
        const float fade = 1.0f - line.opacity * (close ? 0.75f : 1.0f);
        const auto mpp = [&](const V3& p) { return std::max(1e-4f, (p - eye).length() * perPixel); };
        const auto put = [&](const V3& at, float yaw, float sizePx, float type) {
            if (pushed + 2 > kMaxPathDecals) return;
            V3 p = at;
            p.z = battle.floorAt(p);
            const float m = mpp(at);
            // A dark mark a little larger beneath, so the line reads on pale ground and snow.
            decals.insert(decals.end(), {p.x, p.y, p.z, yaw, (sizePx + kOutlinePx) * m, 0, type, kDark[0], kDark[1], kDark[2], fade});
            bright.insert(bright.end(), {p.x, p.y, p.z, yaw, sizePx * m, 0, type, rgb[0], rgb[1], rgb[2], fade});
            pushed += 2;
        };
        const auto visible = [&](const V3& p) {
            float px, py;
            return camera.project(p, px, py) && px > -60 && py > -60 && px < w + 60 && py < h + 60;
        };
        const Stance stance = line.stance;
        const bool hunt = stance == Stance::Hunt, patrol = stance == Stance::Patrol;
        // Pixels from one mark to the next, and the size (half the length of a dash, the radius of a dot).
        const float period = patrol ? 30.0f : hunt ? 12.0f : 10.0f;
        const float markPx = patrol ? 8.0f : hunt ? 7.0f : 3.0f;
        const float markType = patrol || hunt ? kDecalDash : kDecalDot;
        // The chevrons and arrowheads, every so many marks.
        const int every = patrol ? 3 : hunt ? 9 : 0;
        const float headPx = patrol ? 8.0f : 10.0f;
        size_t seg = 1;
        float segStart = 0;  // metres along the line where the segment begins
        float segLen = std::hypot(line.points[1].x - line.points[0].x, line.points[1].y - line.points[0].y);
        float s = std::fmod(seconds * 14.0f, period) * mpp(line.points[0]);  // the marks creep the way he goes
        int mark = 0;
        for (int step = 0; step < kStepsPerLine; ++step) {
            while (s - segStart > segLen && seg + 1 < line.points.size()) {
                segStart += segLen;
                ++seg;
                segLen = std::hypot(line.points[seg].x - line.points[seg - 1].x, line.points[seg].y - line.points[seg - 1].y);
            }
            if (s - segStart > segLen) break;
            const V3 a = line.points[seg - 1], b = line.points[seg];
            const float t = segLen > 0 ? (s - segStart) / segLen : 0.0f;
            const V3 p = lerp(a, b, t);
            const float yaw = std::atan2(b.y - a.y, b.x - a.x);
            const float step_m = period * mpp(p);
            if (!visible(p)) {
                s += step_m * 4.0f;  // (not drawn: out of the picture; cross it quickly)
                mark += 4;
                continue;
            }
            if (every > 0 && mark % every == every - 1) {
                // A chevron in the gap, an arrowhead on the solid.
                put(patrol ? p + V3{std::cos(yaw), std::sin(yaw), 0} * (step_m * 0.5f) : p, yaw, headPx, kDecalArrow);
                if (patrol) put(p, yaw, markPx, markType);
            } else {
                put(p, yaw, markPx, markType);
            }
            s += step_m;
            ++mark;
        }
        // The end.
        const V3 end = line.points.back();
        if (visible(end)) {
            if (hunt) put(end, 0, 14.0f, kDecalCross);
            else if (patrol) put(end, 0, 11.0f, kDecalRing);
            else put(end, 0, 6.0f, kDecalDot);
        }
    }
    decals.insert(decals.end(), bright.begin(), bright.end());
    return pushed;
}

}  // namespace illyria::game
