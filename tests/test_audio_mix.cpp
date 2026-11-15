// The desktop mixer's arithmetic (game/native/audio_mix.h): pure, so it is tested here without a sound device.

#include <cmath>
#include <vector>

#include "../game/native/audio_mix.h"
#include "test.h"

using namespace illyria::native::audio::mixing;

TEST(audio_mix_distance_darkens_and_reverberates) {
    const MixSettings m;
    CHECK_NEAR(lowpassHz(m, 0), 18000.0f, 0.5f);
    CHECK_NEAR(lowpassHz(m, 1), 1400.0f, 0.5f);
    CHECK_NEAR(lowpassHz(m, 0.5f), 1400.0f + 16600.0f * 0.25f, 0.5f);
    CHECK_NEAR(lowpassHz(m, 7), 1400.0f, 0.5f);  // (clamped)
    CHECK_NEAR(reverbSend(m, 0), 0.04f, 1e-5f);
    CHECK_NEAR(reverbSend(m, 1), 0.34f, 1e-5f);
    CHECK_NEAR(reverbSend(m, 0.5f), 0.19f, 1e-5f);
}

TEST(audio_mix_doppler_is_a_slide_down) {
    const Sweep s = dopplerSweep(1.0f, 60);  // k = 0.35 (clamped from 0.5)
    CHECK_NEAR(s.from, 1.35f, 1e-5f);
    CHECK_NEAR(s.to, 1 - 0.8f * 0.35f, 1e-5f);
    const Sweep away = dopplerSweep(1.0f, -12);
    CHECK_NEAR(away.from, 0.9f, 1e-5f);
    CHECK_NEAR(away.to, 1.08f, 1e-5f);
}

TEST(audio_mix_victim_is_lowest_priority_then_soonest_to_end) {
    const Playing strips[] = {{2, 0.5f}, {1, 0.9f}, {1, 0.2f}, {3, 0.05f}};
    CHECK(pickVictim(strips, 4, 1) == 2);
    CHECK(pickVictim(strips, 4, 5) == 2);
    CHECK(pickVictim(strips, 4, 0) == -1);  // (it ranks under everything playing: dropped)
    CHECK(pickVictim(strips, 0, 9) == -1);
    const Playing grave[] = {{4, 1.0f}, {4, 2.0f}};
    CHECK(pickVictim(grave, 2, 4) == 0);
    CHECK(pickVictim(grave, 2, 3) == -1);
}

TEST(audio_mix_voice_rules) {
    CHECK(voiceMayReplace(1, 2));
    CHECK(!voiceMayReplace(2, 2));  // (a cry does not cut off a cry)
    CHECK(voiceMayReplace(3, 3));   // (the newest answer to an order replaces the last)
    CHECK(!voiceMayReplace(4, 3));
    CHECK(voiceGapApplies(3));
    CHECK(!voiceGapApplies(4));
    CHECK(heldInTime(0.39f, 0));
    CHECK(!heldInTime(0.41f, 0));
    CHECK(heldInTime(1.5f, 1.2f));
    CHECK(!heldInTime(1.7f, 1.2f));
}

TEST(audio_mix_limiter_leaves_quiet_sound_alone) {
    Limiter lim(44100);
    std::vector<float> in(2000), out;
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.7f * std::sin(static_cast<float>(i) * 0.05f);
    out = in;
    lim.process(out.data(), static_cast<int>(out.size() / 2));
    bool same = true;
    for (size_t i = 0; i < in.size(); ++i) same = same && in[i] == out[i];
    CHECK(same);  // bit for bit
    CHECK(Limiter::curve(0.5f) == 0.5f);
    CHECK(Limiter::curve(Limiter::kThreshold) == Limiter::kThreshold);
}

TEST(audio_mix_limiter_holds_the_ceiling_and_lets_go) {
    Limiter lim(44100);
    // The curve: rising, continuous at the threshold, never at the ceiling.
    float last = 0;
    for (float x = 0; x < 20; x += 0.01f) {
        const float y = Limiter::curve(x);
        CHECK(y >= last);
        CHECK(y < Limiter::kCeiling + 1e-6f);
        last = y;
    }
    CHECK_NEAR(Limiter::curve(Limiter::kThreshold + 0.001f), Limiter::kThreshold + 0.001f, 1e-4f);
    // A burst four times over full scale: no sample past the ceiling, the two channels turned down alike.
    std::vector<float> buf(44100 * 2);
    for (size_t f = 0; f < buf.size() / 2; ++f) {
        const float x = (f < 4410 ? 4.0f : 0.25f) * std::sin(static_cast<float>(f) * 0.3f);
        buf[f * 2] = x;
        buf[f * 2 + 1] = x * 0.5f;
    }
    const std::vector<float> in = buf;
    lim.process(buf.data(), static_cast<int>(buf.size() / 2));
    float peak = 0;
    bool linked = true;
    for (size_t f = 0; f < buf.size() / 2; ++f) {
        peak = std::max(peak, std::max(std::fabs(buf[f * 2]), std::fabs(buf[f * 2 + 1])));
        if (std::fabs(buf[f * 2]) > 1e-4f) linked = linked && std::fabs(buf[f * 2 + 1] / buf[f * 2] - 0.5f) < 1e-3f;
    }
    CHECK(peak <= Limiter::kCeiling);
    CHECK(peak > Limiter::kThreshold);
    CHECK(linked);
    // 150 ms release: 50 ms after the burst the gain is still down, most of a second after it is back.
    const size_t soon = 4410 + 2205, late = 44100 - 2;
    CHECK(std::fabs(buf[soon * 2]) < std::fabs(in[soon * 2]) * 0.9f || std::fabs(in[soon * 2]) < 1e-3f);
    CHECK(lim.gain() > 0.99f);
    CHECK_NEAR(buf[late * 2], in[late * 2], 0.01f);
}

TEST(audio_mix_lowpass_is_twelve_db_an_octave) {
    // A tone two octaves over the cutoff comes out about 24 dB down; one well under it, untouched.
    const auto level = [](float cutoff, float hz) {
        Biquad lp;
        lp.lowpass(cutoff, 44100);
        double sum = 0;
        for (int i = 0; i < 44100; ++i) {
            const float y = lp.run(0, std::sin(2 * 3.14159265f * hz * static_cast<float>(i) / 44100.0f));
            if (i >= 22050) sum += static_cast<double>(y) * y;
        }
        return 10 * std::log10(sum / 22050 * 2);
    };
    CHECK_NEAR(level(1400, 5600), -24.0, 1.5);
    CHECK_NEAR(level(1400, 200), 0.0, 0.2);
    CHECK_NEAR(level(1400, 1400), -3.0, 0.3);
}

TEST(audio_mix_reverb_decays_in_about_its_time) {
    Reverb room(44100, 1.6f);
    const int n = 44100 * 3;
    std::vector<float> in(static_cast<size_t>(n) * 2, 0.0f), out(in.size());
    in[0] = in[1] = 1;
    room.process(in.data(), out.data(), n);
    const auto energy = [&](double from, double to) {
        double sum = 0;
        for (int f = static_cast<int>(from * 44100); f < static_cast<int>(to * 44100); ++f)
            sum += static_cast<double>(out[static_cast<size_t>(f) * 2]) * out[static_cast<size_t>(f) * 2] +
                   static_cast<double>(out[static_cast<size_t>(f) * 2 + 1]) * out[static_cast<size_t>(f) * 2 + 1];
        return 10 * std::log10(sum / (to - from) + 1e-30);
    };
    const double early = energy(0.05, 0.25), late = energy(1.65, 1.85);
    CHECK(early - late > 45);  // (60 dB in 1.6 s is 37.5 dB a second: about 60 between these windows; damping takes more)
    CHECK(early - late < 90);
    CHECK(energy(2.8, 3.0) < late);  // still falling: stable
    float peak = 0;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    CHECK(peak < 1.0f);
}
