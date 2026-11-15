#pragma once

// The desktop mixer's arithmetic, apart from SDL and the files so it can be tested (tests/test_audio_mix.cpp): the mix's
// numbers (the "mix" object of sfx/sounds.json, the same the page's player reads), what distance does to a cue, which
// strip gives way when all are playing, the voices' rule for cutting in, the shared reverb and the output limiter.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace illyria::native::audio::mixing {

struct MixSettings {
    int strips = 24, voiceStrips = 3;
    float fadeMs = 12;
    float lowpassNearHz = 18000, lowpassFarHz = 1400;
    float reverbNear = 0.04f, reverbFar = 0.34f;
    float loopGain = 4;
    float sfxVolume = 0.8f;
};

// dist: 0 beside the listener .. 1 at the edge of hearing.
inline float lowpassHz(const MixSettings& m, float dist) {
    const float d = std::clamp(dist, 0.0f, 1.0f);
    return m.lowpassFarHz + (m.lowpassNearHz - m.lowpassFarHz) * (1 - d) * (1 - d);
}
inline float reverbSend(const MixSettings& m, float dist) {
    const float d = std::clamp(dist, 0.0f, 1.0f);
    return m.reverbNear + (m.reverbFar - m.reverbNear) * d;
}

// An arrow passing: the playback rate it starts at (closing) and the one it slides down to (gone by).
struct Sweep {
    float from, to;
};
inline Sweep dopplerSweep(float rate, float doppler) {
    const float k = std::clamp(doppler / 120.0f, -0.35f, 0.35f);
    return {rate * (1 + k), rate * (1 - 0.8f * k)};
}

// A strip that is playing (one fading out after being stolen is not: its place is already given away).
struct Playing {
    int priority;
    float remaining;  // seconds until it ends
};
// With every strip playing, the one a cue of `priority` takes: the lowest priority, of those the one ending soonest, and only
// if the cue does not rank under it. -1: none (the cue is dropped).
inline int pickVictim(const Playing* strips, int count, int priority) {
    int best = -1;
    for (int i = 0; i < count; ++i)
        if (best < 0 || strips[i].priority < strips[best].priority ||
            (strips[i].priority == strips[best].priority && strips[i].remaining < strips[best].remaining))
            best = i;
    return best >= 0 && strips[best].priority <= priority ? best : -1;
}

// The voices (game/web/voice.js makeRoom): with all of them speaking, a cue cuts off the slightest one if it outranks it, or
// equals it and is an order or graver (the newest answer replaces the last).
constexpr int kVoiceOrder = 3, kVoiceGrave = 4;
constexpr float kVoiceGapSeconds = 0.3f;
inline bool voiceMayReplace(int slightest, int priority) { return slightest < priority || (slightest == priority && priority >= kVoiceOrder); }
inline bool voiceGapApplies(int priority) { return priority < kVoiceGrave; }

// A cue asked for while its files were still being decoded is played when they arrive, if no later than this.
constexpr float kHeldGrace = 0.4f;
inline bool heldInTime(float waited, float delay) { return waited <= std::max(0.0f, delay) + kHeldGrace; }

// A 12 dB per octave low-pass (RBJ, Butterworth Q), on two channels.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = {0, 0}, z2[2] = {0, 0};
    void lowpass(float hz, float rate) {
        const float w = 2 * 3.14159265f * std::clamp(hz, 20.0f, rate * 0.45f) / rate;
        const float c = std::cos(w), alpha = std::sin(w) / (2 * 0.70710678f), a0 = 1 + alpha;
        b0 = (1 - c) / 2 / a0;
        b1 = (1 - c) / a0;
        b2 = b0;
        a1 = -2 * c / a0;
        a2 = (1 - alpha) / a0;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    float run(int ch, float x) {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};

// One shared room for every effect: eight delay lines fed back through a Hadamard mix, each damped (the highs die first),
// decaying 60 dB in `seconds`. Stereo in (the sends), stereo out (the wet sound alone).
class Reverb {
public:
    explicit Reverb(int rate = 44100, float seconds = 1.6f) {
        static const int kLength[kLines] = {1123, 1361, 1657, 1931, 2311, 2707, 3181, 3571};  // at 44.1 kHz: 25 to 81 ms
        for (int i = 0; i < kLines; ++i) {
            const size_t n = static_cast<size_t>(static_cast<double>(kLength[i]) * rate / 44100.0);
            line_[i].assign(std::max<size_t>(n, 8), 0.0f);
            feedback_[i] = std::pow(10.0f, -3.0f * static_cast<float>(n) / (seconds * static_cast<float>(rate)));
        }
        damp_ = 1 - std::exp(-2 * 3.14159265f * 5200.0f / static_cast<float>(rate));
        quietAfter_ = static_cast<long>(seconds * 2 * static_cast<float>(rate));
        quiet_ = quietAfter_;
    }
    // `in` and `out`: interleaved stereo; out is overwritten.
    void process(const float* in, float* out, int frames) {
        for (int f = 0; f < frames; ++f) {
            const float il = in[f * 2], ir = in[f * 2 + 1];
            if (il != 0 || ir != 0) quiet_ = 0;
            else if (quiet_ < quietAfter_) ++quiet_;
            if (quiet_ >= quietAfter_) {  // (nothing sent for twice the decay: the room is silent, and costs nothing)
                out[f * 2] = out[f * 2 + 1] = 0;
                continue;
            }
            float x[kLines];
            for (int i = 0; i < kLines; ++i) {
                low_[i] += (line_[i][at_[i]] - low_[i]) * damp_;
                x[i] = low_[i] * feedback_[i];
            }
            out[f * 2] = (x[0] - x[2] + x[4] - x[6]) * kOut;
            out[f * 2 + 1] = (x[1] - x[3] + x[5] - x[7]) * kOut;
            // The Hadamard mix (a fast Walsh transform), scaled to keep the energy.
            for (int span = 1; span < kLines; span *= 2)
                for (int i = 0; i < kLines; i += span * 2)
                    for (int j = i; j < i + span; ++j) {
                        const float a = x[j], b = x[j + span];
                        x[j] = a + b;
                        x[j + span] = a - b;
                    }
            for (int i = 0; i < kLines; ++i) {
                line_[i][at_[i]] = x[i] * 0.35355339f + (i % 2 ? ir : il);
                if (++at_[i] >= line_[i].size()) at_[i] = 0;
            }
        }
    }

private:
    static constexpr int kLines = 8;
    static constexpr float kOut = 0.3f;
    std::vector<float> line_[kLines];
    size_t at_[kLines] = {};
    float feedback_[kLines] = {}, low_[kLines] = {};
    float damp_ = 1;
    long quiet_ = 0, quietAfter_ = 0;
};

// The output's limiter. Below the threshold (-3 dBFS) the signal is not touched at all; a peak above it is brought under the
// ceiling (-0.3 dBFS) along a soft knee, by turning the whole mix down at once (both channels alike, no look-ahead: the gain
// is right for the very sample that asked for it) and letting it back up over the release.
class Limiter {
public:
    static constexpr float kThreshold = 0.70794578f;  // -3 dBFS
    static constexpr float kCeiling = 0.96605088f;    // -0.3 dBFS
    explicit Limiter(int rate = 44100, float releaseMs = 150) : release_(1 - std::exp(-1000.0f / (releaseMs * static_cast<float>(rate)))) {}
    // What a peak of `peak` (>= 0) comes out as: itself up to the threshold, then a knee that never reaches the ceiling.
    static float curve(float peak) {
        if (peak <= kThreshold) return peak;
        const float room = kCeiling - kThreshold;
        return kThreshold + room * std::tanh((peak - kThreshold) / room);
    }
    void process(float* interleaved, int frames) {
        for (int f = 0; f < frames; ++f) {
            float& l = interleaved[f * 2];
            float& r = interleaved[f * 2 + 1];
            const float peak = std::max(std::fabs(l), std::fabs(r));
            if (peak <= kThreshold && gain_ >= 0.99999f) {
                gain_ = 1;
                continue;  // (the usual case: bit for bit what went in)
            }
            const float want = peak > kThreshold ? curve(peak) / peak : 1.0f;
            gain_ = std::min(want, gain_ + (1 - gain_) * release_);
            l *= gain_;
            r *= gain_;
        }
    }
    float gain() const { return gain_; }

private:
    float release_;
    float gain_ = 1;
};

}  // namespace illyria::native::audio::mixing
