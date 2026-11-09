// Being under water (game/include/illyria/game/underwater.h, underwater_audio.h): the one question the picture and the
// sound both ask of the camera's eye, the depth-to-sound mapping, the hysteresis, and the filter's effect on a signal.
// Presentation only: none of this touches the simulation.
#include <cmath>
#include <cstdio>
#include <vector>

#include "illyria/game/camera.h"
#include "illyria/game/underwater.h"
#include "illyria/game/underwater_audio.h"
#include "illyria/sim/continent.h"
#include "test.h"

using namespace illyria::game;
namespace sim = illyria::sim;

namespace {
const sim::Continent& continent() {
    static const sim::Continent c(20261027);
    return c;
}
sim::Fixed fx(double v) { return sim::Fixed::fromInt(static_cast<int64_t>(std::floor(v))); }

struct Place {
    bool found = false;
    float x = 0, y = 0, surface = 0, bed = 0;
};
// A place in the sea (at least `deep` m of water), in a lake, in a river: the continent's own.
Place find(int kind, float deep) {
    const sim::Continent& c = continent();
    Place p;
    for (int j = -200; j < sim::Continent::kHeight + 200 && !p.found; j += 7)
        for (int i = -200; i < sim::Continent::kWidth + 200; i += 7) {
            const sim::Continent::Sample s = c.sample(fx(i), fx(j));
            sim::Fixed sf;
            if (!c.waterSurfaceOf(s, sf)) continue;
            const float surface = static_cast<float>(sf.toDouble()), bed = static_cast<float>(s.height.toDouble());
            const bool sea = surface < 0.05f, lake = s.inLake && s.height < s.lakeSurface;
            const bool river = !lake && !sea && s.riverDist < s.riverHalfWidth && s.height < s.riverSurface;
            if ((kind == 0 && !sea) || (kind == 1 && !lake) || (kind == 2 && !river)) continue;
            if (surface - bed < deep) continue;
            if (kind != 0 && surface < 3.0f) continue;  // (a river at its mouth is the sea's)
            // Wet all about it, so the camera's eye a few metres off is over water too.
            bool all = true;
            for (int k = 0; k < 4; ++k) {
                sim::Fixed w;
                all = all && c.waterSurfaceOf(c.sample(fx(i + (k < 2 ? (k ? 6 : -6) : 0)), fx(j + (k >= 2 ? (k == 3 ? 6 : -6) : 0))), w);
            }
            if (!all) continue;
            p = {true, static_cast<float>(i), static_cast<float>(j), surface, bed};
            break;
        }
    return p;
}
WaterQuery query() {
    return [](float x, float y) { return continentWater(continent(), 0, 0, x, y); };
}
}  // namespace

TEST(underwater_depth_of_the_eye_in_sea_lake_and_river) {
    const WaterQuery q = query();
    for (int kind = 0; kind < 3; ++kind) {
        const Place p = find(kind, 1.5f);
        CHECK(p.found);
        if (!p.found) continue;
        // The water level the sim gives, then the eye above, at and below it.
        const WaterAt w = q(p.x, p.y);
        CHECK(w.wet);
        CHECK(std::abs(w.surface - p.surface) < 1e-3f);
        if (kind == 0) CHECK(std::abs(w.surface) < 1e-3f);  // the sea is at level 0
        CHECK(underwaterDepth({p.x, p.y, p.surface + 2.0f}, q) == 0.0f);
        const Underwater above = underwaterAt({p.x, p.y, p.surface + 2.0f}, q);
        CHECK(above.blend == 0.0f);
        const Underwater at = underwaterAt({p.x, p.y, p.surface}, q);
        CHECK(std::abs(at.blend - 0.5f) < 1e-3f);
        CHECK(at.depth == 0.0f);
        const Underwater in = underwaterAt({p.x, p.y, p.surface - 1.0f}, q);
        CHECK(std::abs(in.depth - 1.0f) < 1e-3f);
        CHECK(in.blend == 1.0f);
        // Murkier inland than at sea, and the most in a river.
        CHECK(kind == 0 ? in.murk < 0.05f : kind == 1 ? (in.murk > 0.3f && in.murk < 0.7f) : in.murk > 0.9f);
        // The look fades in over the band either side of the surface, monotonically, with no jump.
        float last = -1;
        for (float z = p.surface + 0.5f; z >= p.surface - 0.5f; z -= 0.01f) {
            const float b = underwaterAt({p.x, p.y, z}, q).blend;
            CHECK(b >= last - 1e-6f);
            CHECK(b - last < 0.03f || last < 0);
            last = b;
        }
    }
    // Dry land: no water, no depth.
    CHECK(underwaterDepth({0, 0, -50}, [](float, float) { return WaterAt{}; }) == 0.0f);
}

TEST(underwater_visibility_shortens_with_depth_and_murk) {
    CHECK(waterVisibility(0, 0) > 25 && waterVisibility(0, 0) <= 40);
    CHECK(waterVisibility(20, 0) < waterVisibility(0, 0));
    CHECK(waterVisibility(20, 0) >= 3);
    CHECK(waterVisibility(2, 0.5f) < waterVisibility(2, 0));
    CHECK(waterVisibility(2, 1) < waterVisibility(2, 0.5f));
    CHECK(waterVisibility(20, 1) < 4 && waterVisibility(20, 1) > 1);
    float last = 1e9f;
    for (float d = 0; d <= 30; d += 0.5f) {
        const float v = waterVisibility(d, 0.3f);
        CHECK(v <= last + 1e-4f);
        last = v;
    }
}

TEST(underwater_the_real_camera_eye_may_go_below_the_surface_above_the_bed) {
    // Camera::eye() is what both the picture and the sound use. Over a lake's bed the eye is held 0.4 m off the bed
    // and nowhere else: not lifted out of the water.
    const sim::Continent& c = continent();
    const Place p = find(1, 2.5f);
    CHECK(p.found);
    if (!p.found) return;
    Camera cam;
    cam.setBounds(0, 0, sim::Continent::kWidth, sim::Continent::kHeight);
    cam.setHeightFn([&c](float x, float y) { return static_cast<float>(c.heightAt(fx(x), fx(y)).toDouble()); });
    cam.lookAt(p.x, p.y, 2.0f);
    for (int i = 0; i < 90; ++i) cam.advance(1.0f / 60);
    const V3 eye = cam.eye();
    const float ground = static_cast<float>(c.heightAt(fx(eye.x), fx(eye.y)).toDouble());
    CHECK(eye.z >= ground + 0.39f);
    const Underwater u = underwaterAt(eye, query());
    CHECK(u.depth > 0.5f);          // inside the lake, not floated to its surface
    CHECK(u.depth < p.surface - p.bed);  // and above its bed
    CHECK(eye.z < p.surface - 0.3f);
}

TEST(underwater_sound_latch_has_hysteresis) {
    SoundLatch l;
    CHECK(!l.update(-1));
    CHECK(!l.update(0.1f));   // inside the dead band: still out
    CHECK(l.update(0.2f));    // below by more than kSoundEnter
    CHECK(l.update(0.0f));    // bobbing at the surface: stays under
    CHECK(l.update(-0.1f));
    CHECK(!l.update(-0.2f));  // clear above kSoundLeave
    CHECK(!l.update(0.05f));
    // A 1 Hz, 0.2 m bob about the surface (eye amplitude inside the band) never chatters.
    int flips = 0;
    bool prev = l.in();
    for (int i = 0; i < 600; ++i) {
        const float below = 0.12f * std::sin(i * 0.1f);
        const bool now = l.update(below);
        flips += now != prev;
        prev = now;
    }
    CHECK(flips == 0);
}

TEST(underwater_sound_mapping_by_depth) {
    const UnderwaterSound shallow = underwaterSoundTarget(0.5f), deep = underwaterSoundTarget(20.0f);
    CHECK(shallow.cutoffHz <= 800.0f && shallow.cutoffHz >= 700.0f);
    CHECK(deep.cutoffHz >= 395.0f && deep.cutoffHz <= 430.0f);
    float last = 1e9f;
    for (float d = 0; d <= 40; d += 1) {
        const UnderwaterSound s = underwaterSoundTarget(d);
        CHECK(s.cutoffHz <= last + 1e-3f);
        last = s.cutoffHz;
        const float db = 20 * std::log10(s.gain);
        CHECK(db <= -5.9f && db >= -10.1f);
        CHECK(s.cutoffHz >= 399.0f && s.cutoffHz <= 800.0f);
    }
    CHECK(deep.gain < shallow.gain);
    CHECK(deep.surfaceScale < shallow.surfaceScale);
    // Crossfade: ~95% across in 200 ms, and back, and the effective values are the air's at amount 0.
    UnderwaterSound s;
    for (int i = 0; i < 12; ++i) s = glideUnderwaterSound(s, true, 3, 1.0f / 60);  // 0.2 s
    CHECK(s.amount > 0.9f && s.amount < 1.0f);
    const UnderwaterSound mid = effectiveUnderwaterSound(s);
    CHECK(mid.cutoffHz < 20000 && mid.cutoffHz > 400);
    for (int i = 0; i < 60; ++i) s = glideUnderwaterSound(s, false, 3, 1.0f / 60);
    CHECK(s.amount == 0.0f);
    const UnderwaterSound air = effectiveUnderwaterSound(s);
    CHECK(std::abs(air.cutoffHz - 20000) < 1 && std::abs(air.gain - 1) < 1e-6f && air.ambience == 0 && air.surfaceScale == 1);
}

namespace {
double rms(const std::vector<float>& x, size_t from = 0) {
    double s = 0;
    for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / std::max<size_t>(1, x.size() - from));
}
// Energy of a stereo signal's left channel in a band, by a straight DFT at a few frequencies (Goertzel).
double band(const std::vector<float>& x, int rate, double f) {
    const size_t n = x.size() / 2;
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        const double a = 2 * 3.14159265358979 * f * static_cast<double>(i) / rate;
        re += x[i * 2] * std::cos(a);
        im += x[i * 2] * std::sin(a);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(n);
}
}  // namespace

TEST(underwater_filter_darkens_and_quietens_a_signal) {
    constexpr int kRate = 44100;
    // A fixed pseudo-random noise, white to the ear: what the world sounds like, all bands.
    std::vector<float> noise(kRate * 2 * 2);
    uint32_t s = 12345;
    for (float& v : noise) {
        s = s * 1664525u + 1013904223u;
        v = (static_cast<float>(s >> 8) / 16777216.0f * 2 - 1) * 0.3f;
    }
    const double inRms = rms(noise);
    const double lo0 = band(noise, kRate, 200) + band(noise, kRate, 300), hi0 = band(noise, kRate, 3000) + band(noise, kRate, 5000) + band(noise, kRate, 8000);
    std::printf("underwater filter: input rms %.4f  low(200,300 Hz) %.5f  high(3,5,8 kHz) %.5f\n", inRms, lo0, hi0);
    for (float depth : {0.5f, 3.0f, 15.0f}) {
        UnderwaterSound u = underwaterSoundTarget(depth);
        u.amount = 1;
        UnderwaterBus bus(kRate);
        bus.set(effectiveUnderwaterSound(u));
        std::vector<float> out = noise;
        bus.process(out.data(), static_cast<int>(out.size() / 2));
        const double r = rms(out, kRate), lo = band(out, kRate, 200) + band(out, kRate, 300), hi = band(out, kRate, 3000) + band(out, kRate, 5000) + band(out, kRate, 8000);
        std::printf("  depth %4.1f m: cutoff %5.0f Hz  rms %.4f (%.1f dB)  low %.5f (%.1f dB)  high %.5f (%.1f dB)\n", depth, u.cutoffHz, r, 20 * std::log10(r / inRms), lo,
                    20 * std::log10(lo / lo0), hi, 20 * std::log10(hi / hi0));
        CHECK(hi < hi0 * 0.03);         // the highs gone: at least 30 dB down at 3 kHz and above
        CHECK(lo > lo0 * 0.25);         // the lows stay (within the gain and the filter's shoulder)
        CHECK(r < inRms * 0.5);         // and the whole is quieter (broadband: the lost highs and the -6 to -10 dB)
        CHECK(std::isfinite(r));
    }
    // Amount 0: the signal passes untouched.
    UnderwaterBus off(kRate);
    off.set(effectiveUnderwaterSound(UnderwaterSound{}));
    std::vector<float> same = noise;
    off.process(same.data(), static_cast<int>(same.size() / 2));
    CHECK(same == noise);
}

TEST(underwater_synthesised_sounds_are_sane_and_deterministic) {
    constexpr int kRate = 44100;
    const std::vector<float> amb = synthUnderwaterAmbience(kRate), amb2 = synthUnderwaterAmbience(kRate);
    CHECK(amb == amb2);
    CHECK(amb.size() == static_cast<size_t>(8 * kRate) * 2);
    // Seamless: the jump across the loop point is no bigger than the signal's own steps.
    double maxStep = 0, wrap = std::fabs(amb[0] - amb[amb.size() - 2]);
    for (size_t i = 1; i < amb.size() / 2; ++i) maxStep = std::max(maxStep, static_cast<double>(std::fabs(amb[i * 2] - amb[i * 2 - 2])));
    CHECK(wrap <= maxStep * 1.5 + 1e-3);
    const double r = rms(amb);
    std::printf("underwater ambience: rms %.3f, low(60 Hz) %.4f  high(4 kHz) %.5f\n", r, band(amb, kRate, 60), band(amb, kRate, 4000));
    CHECK(r > 0.02 && r < 0.6);
    CHECK(band(amb, kRate, 60) > band(amb, kRate, 4000) * 10);  // a low rumble, not hiss
    for (const std::vector<float>& cue : {synthPlunge(kRate), synthSurfacing(kRate)}) {
        double peak = 0;
        for (float v : cue) peak = std::max(peak, static_cast<double>(std::fabs(v)));
        CHECK(peak > 0.5 && peak <= 0.9);
        CHECK(cue.size() > static_cast<size_t>(kRate));
    }
}
