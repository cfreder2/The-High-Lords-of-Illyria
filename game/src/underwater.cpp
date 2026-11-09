#include "illyria/game/underwater.h"

#include <algorithm>
#include <cmath>

#include "illyria/sim/continent.h"

namespace illyria::game {

namespace {
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float mixf(float a, float b, float t) { return a + (b - a) * t; }
// shaders.h seaWeight: how much of the sea a surface level is (a river's surface falls to 0 at its mouth).
float seaWeight(float surface) { return 1 - smooth(0.1f, 2.0f, surface); }

// Visibility (m): shallow and deep, clear sea and the muddiest river.
constexpr float kVisShallow = 30.0f, kVisDeep = 8.0f;  // sea, at the surface and 20 m down
constexpr float kMurkLake = 0.5f;                       // a lake's murk; a river or stream's is 1
constexpr float kVisMurkScale = 0.7f;                   // the share of visibility murk 1 takes away
}  // namespace

WaterAt continentWater(const sim::Continent& c, float ox, float oy, float x, float y) {
    using sim::Fixed;
    const sim::Continent::Sample s = c.sample(Fixed::fromInt(static_cast<int64_t>(std::floor(x + ox))), Fixed::fromInt(static_cast<int64_t>(std::floor(y + oy))));
    Fixed sf;
    WaterAt w;
    if (!c.waterSurfaceOf(s, sf)) return w;
    w.wet = true;
    w.surface = static_cast<float>(sf.toDouble());
    const bool lake = s.inLake && s.height < s.lakeSurface;
    const bool flowing = !lake && ((s.riverDist < s.riverHalfWidth + Fixed::fromInt(6) && s.height < s.riverSurface) ||
                                   (s.streamDist < s.streamHalfWidth + Fixed::fromInt(2) && s.height < s.streamSurface));
    w.murk = flowing ? 1.0f : kMurkLake;
    w.murk *= 1 - seaWeight(w.surface);  // the sea at level 0 and a river's mouth: the sea's clear water
    return w;
}

Underwater underwaterAt(const V3& eye, const WaterQuery& water) {
    Underwater u;
    if (!water) return u;
    const WaterAt w = water(eye.x, eye.y);
    if (!w.wet) return u;
    u.surface = w.surface;
    u.murk = w.murk;
    u.below = w.surface - eye.z;
    u.depth = std::max(u.below, 0.0f);
    u.blend = smooth(-kSurfaceBand, kSurfaceBand, u.below);
    return u;
}

float underwaterDepth(const V3& eye, const WaterQuery& water) { return underwaterAt(eye, water).depth; }

float waterVisibility(float depth, float murk) {
    const float d = smooth(0.0f, 20.0f, depth);
    return mixf(kVisShallow, kVisDeep, d) * (1 - kVisMurkScale * std::clamp(murk, 0.0f, 1.0f));
}

UnderwaterSound underwaterSoundTarget(float depth) {
    // Dark and muffled at once, more so as it goes down: 800 Hz at the surface to 400 Hz at ~15 m.
    const float k = 1 - std::exp(-std::max(depth, 0.0f) / 6.0f);
    UnderwaterSound s;
    s.amount = 1;
    s.cutoffHz = 800.0f * std::pow(0.5f, k);
    s.gain = std::pow(10.0f, mixf(-6.0f, -10.0f, k) / 20.0f);
    s.wobble = mixf(0.6f, 1.0f, k);
    s.ambience = mixf(0.55f, 0.9f, k);
    s.surfaceScale = mixf(0.35f, 0.15f, k);
    return s;
}

UnderwaterSound effectiveUnderwaterSound(const UnderwaterSound& s) {
    UnderwaterSound e = s;
    const float a = std::clamp(s.amount, 0.0f, 1.0f);
    e.cutoffHz = 20000.0f * std::pow(std::max(s.cutoffHz, 20.0f) / 20000.0f, a);
    e.gain = std::pow(std::max(s.gain, 1e-4f), a);
    e.wobble = s.wobble * a;
    e.ambience = s.ambience * a;
    e.surfaceScale = mixf(1.0f, s.surfaceScale, a);
    return e;
}

UnderwaterSound glideUnderwaterSound(UnderwaterSound now, bool engaged, float depth, float dt) {
    const float a = 1 - std::exp(-std::max(dt, 0.0f) / kSoundGlideSeconds);
    const UnderwaterSound tgt = underwaterSoundTarget(depth);
    now.amount += ((engaged ? 1.0f : 0.0f) - now.amount) * a;
    if (now.amount < 1e-4f && !engaged) now.amount = 0;
    if (now.amount > 1 - 1e-4f && engaged) now.amount = 1;
    // The shape of the sound follows the depth while under; it only matters scaled by amount.
    now.cutoffHz = tgt.cutoffHz;
    now.gain = tgt.gain;
    now.wobble = tgt.wobble;
    now.ambience = tgt.ambience;
    now.surfaceScale = tgt.surfaceScale;
    return now;
}

}  // namespace illyria::game
