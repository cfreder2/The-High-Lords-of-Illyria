#pragma once

// Below the surface: what the camera's eye is in, for the picture (a tint, fog, light) and for the sound (a
// muffling filter, a hum). Presentation only: nothing here is read by the simulation, and the camera is not part of it.
//
// One query, `underwaterAt(eye, water)`, answers for every body of water the sim has (the sea, a lake, a river, a
// pool) from the water's surface over the eye's place; the renderer (uUw in shaders.h) and the sound (the platforms'
// sfxUnderwater) both read the one answer, so what is seen and what is heard always agree.

#include <functional>

#include "illyria/game/gmath.h"

namespace illyria::sim {
class Continent;
}

namespace illyria::game {

// The water over a place (battle-local x, y): its surface height, and how murky it is, 0 (the open sea) to 1 (a river).
struct WaterAt {
    bool wet = false;
    float surface = 0;
    float murk = 0;
};
using WaterQuery = std::function<WaterAt(float x, float y)>;

// The Continent's water at a battle-local point (origin: the battle window's corner on the continent): the sea at
// level 0, a lake's, a river's, a stream's or a basin's own surface.
WaterAt continentWater(const sim::Continent& c, float originX, float originY, float x, float y);

// How far the picture's underwater look fades in, in metres of the eye below the surface (half way through the surface
// it is half on): no pop as the camera crosses it.
constexpr float kSurfaceBand = 0.3f;
// How far below the surface the eye must go before the sound is under, and how far above to leave it: the eye bobbing
// about the surface (the camera's ground follow, a swell) must not make it chatter.
constexpr float kSoundEnter = 0.15f, kSoundLeave = -0.15f;

struct Underwater {
    float below = -1e9f;  // metres the eye is below the surface (negative: above it, or no water here)
    float depth = 0;      // max(below, 0)
    float blend = 0;      // the look: 0 above the surface .. 1 under it (over kSurfaceBand either side)
    float murk = 0;       // 0 sea .. 1 river
    float surface = 0;    // the water's surface height
};

// Metres of water over the eye (0 when it is above the surface or there is no water): the camera's real eye, Camera::eye().
float underwaterDepth(const V3& eye, const WaterQuery& water);
Underwater underwaterAt(const V3& eye, const WaterQuery& water);

// The visibility in the water, in metres (the distance at which a thing is 1/e of itself), by depth and murk: clear and
// far near the surface, shorter in deep water, shorter again in lakes, a few metres in a river.
float waterVisibility(float depth, float murk);

// Latches the sound's state with hysteresis (kSoundEnter, kSoundLeave).
class SoundLatch {
public:
    bool update(float below) {
        if (!in_ && below > kSoundEnter) in_ = true;
        else if (in_ && below < kSoundLeave) in_ = false;
        return in_;
    }
    bool in() const { return in_; }

private:
    bool in_ = false;
};

// What the underwater sound is like at a depth: handed to each platform's mixer as plain numbers.
struct UnderwaterSound {
    float amount = 0;       // 0 (in the air) .. 1 (under), already glided: crossfades over ~200 ms
    float cutoffHz = 20000; // the low-pass over the whole world mix
    float gain = 1;         // linear, the mix's loudness
    float wobble = 0;       // 0..1 depth of the slow pitch wobble / chorus
    float ambience = 0;     // the underwater loop's level
    float surfaceScale = 1; // what is left of the surface soundscape (wind, birds, battle clamour), before the filter
};
// The values to apply, scaled by `amount` (cutoff and gain by the ratio they take, the others linearly): a mixer applies
// these directly, and at amount 0 they are the air's (20 kHz, unity, nothing).
UnderwaterSound effectiveUnderwaterSound(const UnderwaterSound& s);
// The target for a depth in metres, at full submersion.
UnderwaterSound underwaterSoundTarget(float depth);
// Glides `now` toward the target (`engaged`: under, with the latch) over dt seconds (time constant kSoundGlideSeconds).
constexpr float kSoundGlideSeconds = 0.07f;  // ~95% in 200 ms
UnderwaterSound glideUnderwaterSound(UnderwaterSound now, bool engaged, float depth, float dt);

}  // namespace illyria::game
