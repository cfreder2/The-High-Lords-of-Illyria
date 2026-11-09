#pragma once

// The sound of being under water, as pure DSP both platforms share: the filter the whole world mix goes through
// (the desktop's mixer runs it as it is; the browser has the same numbers in a BiquadFilter and a modulated delay),
// and the sounds made for it (the ambience loop, the plunge and the surfacing cue), synthesised, so there are no
// files to ship. Deterministic. Tested by tests/test_underwater.cpp.

#include <vector>

#include "illyria/game/underwater.h"

namespace illyria::game {

// A two-biquad (24 dB per octave) low-pass at the effective cutoff, the gain, and a slow wobble of pitch (a delay
// of a few milliseconds swung at 0.35 Hz, the two ears out of step), on interleaved stereo, in place. At amount 0 it
// passes the sound untouched.
class UnderwaterBus {
public:
    explicit UnderwaterBus(int rate = 44100);
    void set(const UnderwaterSound& effective) { s_ = effective; }
    void process(float* interleaved, int frames);

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] = {0, 0}, z2[2] = {0, 0};
        float run(int ch, float x) {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };
    int rate_;
    UnderwaterSound s_;
    Biquad lp_[2];
    std::vector<float> line_;  // the delay line, per channel interleaved
    size_t head_ = 0;
    double phase_ = 0;
};

// Stereo interleaved loops and cues at `rate`.
std::vector<float> synthUnderwaterAmbience(int rate);  // a seamless ~8 s loop: rumble, water motion, bubbles, a hollow whoomm
std::vector<float> synthPlunge(int rate);              // into the water: splash, a thump, bubbles streaming down
std::vector<float> synthSurfacing(int rate);           // out: bubbles rising, then the air opening up

}  // namespace illyria::game
