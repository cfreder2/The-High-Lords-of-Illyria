#include "illyria/game/underwater_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace illyria::game {

namespace {
constexpr float kPi = 3.14159265f;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    float next() {  // 0..1
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s >> 8) / 16777216.0f;
    }
    float signedNoise() { return next() * 2 - 1; }
    float range(float a, float b) { return a + (b - a) * next(); }
};

struct OnePole {
    float y = 0;
    float run(float x, float a) { return y += (x - y) * a; }
};
float alphaOf(float hz, int rate) { return 1 - std::exp(-2 * kPi * hz / static_cast<float>(rate)); }

// A bubble: a sine rising a little while it fades (Minnaert: small ones high), added to both channels with a pan.
void bubble(std::vector<float>& out, int rate, float at, float hz, float ms, float gain, float pan) {
    const int n = static_cast<int>(ms * 0.001f * rate * 5);
    const int i0 = static_cast<int>(at * rate);
    const float gl = std::cos((pan + 1) * kPi / 4), gr = std::sin((pan + 1) * kPi / 4);
    float ph = 0;
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i0 + i) * 2;
        if (k + 1 >= out.size()) break;
        const float t = static_cast<float>(i) / rate, tau = ms * 0.001f;
        ph += 2 * kPi * hz * (1 + 0.6f * std::min(t / tau, 1.5f)) / rate;
        const float e = std::exp(-t / tau) * std::min(t / 0.0015f, 1.0f);
        const float v = std::sin(ph) * e * gain;
        out[k] += v * gl;
        out[k + 1] += v * gr;
    }
}

void normalise(std::vector<float>& x, float peak) {
    float m = 1e-9f;
    for (float v : x) m = std::max(m, std::fabs(v));
    for (float& v : x) v *= peak / m;
}
}  // namespace

UnderwaterBus::UnderwaterBus(int rate) : rate_(rate), line_(4096 * 2, 0.0f) {}

void UnderwaterBus::process(float* io, int frames) {
    const size_t cap = line_.size() / 2;
    const bool active = s_.cutoffHz < 19000.0f || s_.wobble > 1e-3f || s_.gain < 0.999f;  // (the air: untouched)
    // The filter's coefficients (RBJ low-pass, Q = 0.7071), from the effective cutoff.
    Biquad& q = lp_[0];
    if (active) {
        const float w = 2 * kPi * std::min(s_.cutoffHz, rate_ * 0.45f) / rate_, c = std::cos(w), a = std::sin(w) / (2 * 0.7071f);
        const float a0 = 1 + a;
        q.b0 = (1 - c) / 2 / a0;
        q.b1 = (1 - c) / a0;
        q.b2 = (1 - c) / 2 / a0;
        q.a1 = -2 * c / a0;
        q.a2 = (1 - a) / a0;
        lp_[1].b0 = q.b0, lp_[1].b1 = q.b1, lp_[1].b2 = q.b2, lp_[1].a1 = q.a1, lp_[1].a2 = q.a2;
    }
    const double dPhase = 2 * 3.14159265358979 * 0.35 / rate_;
    for (int i = 0; i < frames; ++i) {
        float in[2] = {io[i * 2], io[i * 2 + 1]};
        line_[head_ * 2] = in[0];
        line_[head_ * 2 + 1] = in[1];
        if (active) {
            phase_ += dPhase;
            for (int ch = 0; ch < 2; ++ch) {
                // A few milliseconds back, swung by up to 2.5 ms at 0.35 Hz (the right ear a third of a turn on): about 10 cents.
                const float d = (0.006f + 0.0025f * s_.wobble * std::sin(static_cast<float>(phase_) + ch * 2.1f)) * rate_;
                const float back = static_cast<float>(head_) + cap - d;
                const size_t i0 = static_cast<size_t>(back) % cap, i1 = (i0 + 1) % cap;
                const float f = back - std::floor(back);
                float x = line_[i0 * 2 + static_cast<size_t>(ch)] * (1 - f) + line_[i1 * 2 + static_cast<size_t>(ch)] * f;
                x = lp_[1].run(ch, lp_[0].run(ch, x)) * s_.gain;
                io[i * 2 + ch] = x;
            }
        } else {
            for (int ch = 0; ch < 2; ++ch) lp_[0].z1[ch] = lp_[0].z2[ch] = lp_[1].z1[ch] = lp_[1].z2[ch] = 0;
        }
        head_ = (head_ + 1) % cap;
    }
}

std::vector<float> synthUnderwaterAmbience(int rate) {
    constexpr float kLoop = 8.0f, kOverlap = 0.6f;
    const int n = static_cast<int>(kLoop * rate), extra = static_cast<int>(kOverlap * rate);
    std::vector<float> x(static_cast<size_t>(n + extra) * 2, 0.0f);
    Rng rng(0x5eed1234);
    OnePole brown[2], body[2], hum[2];
    for (int i = 0; i < n + extra; ++i) {
        const float t = static_cast<float>(i) / rate;
        const float swell = 0.65f + 0.35f * std::sin(2 * kPi * t / 5.3f + 1.0f) * std::sin(2 * kPi * t / 8.0f * 2 + 0.3f);
        for (int ch = 0; ch < 2; ++ch) {
            // The low rumble of a body of water: noise rounded off under ~90 Hz, swelling; and its water motion above (~250 Hz).
            const float w = rng.signedNoise();
            const float r = brown[ch].run(w, alphaOf(90, rate)) * 6.0f;
            const float m = body[ch].run(rng.signedNoise(), alphaOf(260, rate)) * 1.2f * (0.5f + 0.5f * std::sin(2 * kPi * t * (0.13f + 0.04f * ch) + ch));
            const float h = hum[ch].run(w, alphaOf(40, rate)) * 8.0f;
            x[static_cast<size_t>(i) * 2 + static_cast<size_t>(ch)] = (r + m + h * 0.7f) * swell;
        }
    }
    // Bubbles, a few a second, at random places in the stereo field.
    for (float at = rng.range(0.1f, 0.5f); at < kLoop + kOverlap - 0.1f; at += rng.range(0.25f, 1.4f)) {
        const int count = 1 + static_cast<int>(rng.next() * 3);
        for (int k = 0; k < count; ++k) bubble(x, rate, at + k * rng.range(0.03f, 0.11f), rng.range(260, 900), rng.range(10, 36), rng.range(0.1f, 0.35f), rng.range(-0.8f, 0.8f));
    }
    // The hollow whoomm: a deep tone sagging and swelling, twice a loop.
    for (float at : {1.7f, 5.2f}) {
        const int i0 = static_cast<int>(at * rate), len = static_cast<int>(2.2f * rate);
        float ph = 0;
        for (int i = 0; i < len && i0 + i < n + extra; ++i) {
            const float t = static_cast<float>(i) / rate;
            ph += 2 * kPi * (52.0f - 14.0f * t / 2.2f) / rate;
            const float e = std::min(t / 0.5f, 1.0f) * std::exp(-t / 0.9f);
            const float v = (std::sin(ph) + 0.35f * std::sin(ph * 2.01f)) * e * 0.8f;
            x[static_cast<size_t>(i0 + i) * 2] += v;
            x[static_cast<size_t>(i0 + i) * 2 + 1] += v;
        }
    }
    // Seamless: the last kOverlap seconds fade out as the first fade in over them.
    std::vector<float> out(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch) {
            const size_t k = static_cast<size_t>(i) * 2 + static_cast<size_t>(ch);
            float v = x[k];
            if (i < extra) {
                const float f = static_cast<float>(i) / extra;
                v = x[k] * f + x[static_cast<size_t>(n + i) * 2 + static_cast<size_t>(ch)] * (1 - f);
            }
            out[k] = v;
        }
    normalise(out, 0.6f);
    return out;
}

std::vector<float> synthPlunge(int rate) {
    const int n = static_cast<int>(1.3f * rate);
    std::vector<float> x(static_cast<size_t>(n) * 2, 0.0f);
    Rng rng(0xb10b);
    OnePole lo[2], hi[2];
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / rate;
        for (int ch = 0; ch < 2; ++ch) {
            const float w = rng.signedNoise();
            // The splash: noise (~1 to 6 kHz) falling away in a quarter second, the water closing over.
            const float band = hi[ch].run(w, alphaOf(6000, rate)) - lo[ch].run(w, alphaOf(900, rate));
            x[static_cast<size_t>(i) * 2 + static_cast<size_t>(ch)] += band * std::exp(-t / 0.18f) * std::min(t / 0.004f, 1.0f) * 1.5f;
        }
        // The thump of the body, and a hollow after it.
        const float th = std::sin(2 * kPi * (85.0f - 40.0f * std::min(t / 0.4f, 1.0f)) * t) * std::exp(-t / 0.22f) * 0.9f;
        x[static_cast<size_t>(i) * 2] += th;
        x[static_cast<size_t>(i) * 2 + 1] += th;
    }
    for (float at = 0.03f; at < 1.0f; at += rng.range(0.012f, 0.05f) * (1 + at * 2))
        bubble(x, rate, at, rng.range(300, 1500), rng.range(6, 24), rng.range(0.1f, 0.5f) * (1 - at * 0.8f), rng.range(-0.8f, 0.8f));
    normalise(x, 0.85f);
    return x;
}

std::vector<float> synthSurfacing(int rate) {
    const int n = static_cast<int>(1.4f * rate);
    std::vector<float> x(static_cast<size_t>(n) * 2, 0.0f);
    Rng rng(0xa17);
    OnePole lp[2];
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / rate;
        // The air opening up: noise whose brightness opens from a few hundred hertz to the sky's, as the head clears.
        const float open = std::clamp((t - 0.3f) / 0.5f, 0.0f, 1.0f);
        const float cut = 400.0f * std::pow(22.0f, open);
        const float env = std::min(std::max(t - 0.25f, 0.0f) / 0.2f, 1.0f) * std::exp(-std::max(t - 0.55f, 0.0f) / 0.3f);
        for (int ch = 0; ch < 2; ++ch)
            x[static_cast<size_t>(i) * 2 + static_cast<size_t>(ch)] += lp[ch].run(rng.signedNoise(), alphaOf(cut, rate)) * env * 1.2f;
    }
    // Bubbles streaming up first: thick and rising in pitch, thinning as they reach the top.
    for (float at = 0.0f; at < 0.6f; at += rng.range(0.015f, 0.045f) * (1 + at * 3)) {
        const float f = 250.0f + 1400.0f * at;
        bubble(x, rate, at, rng.range(f * 0.8f, f * 1.4f), rng.range(7, 26), rng.range(0.15f, 0.5f), rng.range(-0.8f, 0.8f));
    }
    normalise(x, 0.8f);
    return x;
}

}  // namespace illyria::game
