// Sheets of their own clips (game/render/sprite_clips.h): finding a clip by
// name and its stand-ins, walking or running, the bow drawn so the arrow
// leaves as the sim looses it, a scavenger's pickup and throw, the cheer of
// a won field; and the fixed layout's frames, exactly as they always were.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "figures.h"
#include "sprite_clips.h"
#include "sprite_layout.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;
using sprites::ClipMemory;
using sprites::Deeds;
using sprites::SheetClip;
using sprites::SheetClips;

namespace {

constexpr float kTps = 30.0f;  // sim::kTicksPerSecond

// The archers' list as the importer writes it (tools/sprites/layout.py EXAMPLE_CLIPS).
const char* const kArchers =
    "idle 0 8 2.6 1 0 -1\n"
    "walk 8 8 0 1 1.4 -1\n"
    "run 16 8 0 1 2.74 -1\n"
    "attack 24 12 2.4 0 0 1.45 0.0 0.25 0.5 0.75 1.0 1.15 1.3 1.45 1.6 1.8 2.0 2.2\n"
    "flinch 36 4 0.6 0 0 -1\n"
    "die_back 40 8 1.5 0 0 -1\n"
    "die_front 48 8 1.4 0 0 -1\n"
    "die_crumple 56 8 1.5 0 0 -1\n"
    "die_burn 64 8 2.2 0 0 -1\n"
    "celebrate 72 16 2.7 1 0 -1\n";

// A hyena-folk sheet: a slash for an attack, and a pickup and a throw.
const char* const kHyena =
    "idle 0 4 2.0 1 0 -1\n"
    "walk 4 8 0 1 1.3 -1\n"
    "attack 12 6 0.6 0 0 -1\n"
    "flinch 18 2 0.3 0 0 -1\n"
    "die_back 20 6 0.9 0 0 -1\n"
    "pickup 26 5 0.7 0 0 -1\n"
    "throw 31 8 1.0 0 0 0.6\n";

int firstOf(const SheetClips& s, const char* name) { return s.find(name) ? s.find(name)->first : -1; }
bool inClip(const SheetClips& s, const char* name, int frame) {
    const SheetClip* c = s.find(name);
    return c && frame >= c->first && frame < c->first + c->frames;
}

// The fixed layout's frame as renderer.cpp chose it at 21e6e07, word for word
// (what sprites::fixedLayoutFrame must still give).
int fixedLayoutFrameAsItWas(float struck, float strike, float hurt, bool moving, bool inAir, float walked, float stride,
                            bool prerenderedFlier, float gameSeconds, uint32_t id) {
    int frame;
    if (struck >= 0 && struck < strike) {
        frame = figures::kFirstFrame[2] + std::min(figures::kFrames[2] - 1, static_cast<int>(struck / strike * figures::kFrames[2]));
    } else if (hurt >= 0 && hurt < figures::kFlinchSeconds) {
        frame = figures::kFirstFrame[3] + (hurt < figures::kFlinchSeconds * 0.5f ? 0 : 1);
    } else if (moving || inAir) {
        const float cycle = inAir ? gameSeconds * 1.6f : walked / stride;
        frame = figures::kFirstFrame[1] + static_cast<int>((cycle - std::floor(cycle)) * figures::kFrames[1]) % figures::kFrames[1];
        if (prerenderedFlier) {
            const int half = figures::kFrames[1] / 2;
            const float c = inAir ? gameSeconds * 1.1f : walked / stride;
            frame = figures::kFirstFrame[1] + (inAir ? half : 0) + static_cast<int>((c - std::floor(c)) * half) % half;
        }
    } else {
        const float cycle = gameSeconds * 0.45f + static_cast<float>(id % 13) * 0.37f;
        frame = static_cast<int>((cycle - std::floor(cycle)) * figures::kFrames[0]) % figures::kFrames[0];
    }
    return frame;
}

}  // namespace

// A sheet's clips by name, and what plays a clip it lacks.
TEST(sprite_clips_by_name_and_their_stand_ins) {
    const SheetClips s = sprites::parseClips(kArchers);
    CHECK(s.clips.size() == 10 && s.framesPerView() == 88 && s.valid(88));
    CHECK(!s.valid(87));
    CHECK(s.find("run") && s.find("run")->stride > 2.7f && s.find("run")->loop);
    CHECK(s.find("attack")->release > 1.44f && s.find("attack")->times.size() == 12);
    CHECK(s.find("pickup") == nullptr && s.play("pickup") == s.find("attack"));
    CHECK(s.play("throw") == s.find("attack") && s.play("slash") == s.find("attack"));
    CHECK(s.play("nonesuch") == nullptr);
    // The stand-ins: run the walk, celebrate the idle, the falls die_back (burning: die_crumple first).
    CHECK(sprites::fallbackOf("run") == "walk" && sprites::fallbackOf("celebrate") == "idle");
    CHECK(sprites::fallbackOf("die_front") == "die_back" && sprites::fallbackOf("die_crumple") == "die_back");
    CHECK(sprites::fallbackOf("die_burn") == "die_crumple" && sprites::fallbackOf("idle").empty());
    // Every clip the game plays (the sandbox offers them all) comes down to one of the fixed layout's eight.
    for (const char* known : sprites::kKnownClips) {
        std::string_view n = known;
        for (int k = 0; k < 8 && std::none_of(std::begin(sprites::kClipNames), std::end(sprites::kClipNames), [&](const char* f) { return n == f; }); ++k)
            n = sprites::fallbackOf(n);
        CHECK(std::any_of(std::begin(sprites::kClipNames), std::end(sprites::kClipNames), [&](const char* f) { return n == f; }));
        CHECK(s.play(known) != nullptr);
    }
    const SheetClips h = sprites::parseClips(kHyena);
    CHECK(h.valid(39));
    CHECK(h.play("run") == h.find("walk") && h.play("celebrate") == h.find("idle"));
    CHECK(h.play("die_front") == h.find("die_back") && h.play("die_burn") == h.find("die_back"));
    CHECK(h.play("pickup") == h.find("pickup") && h.play("throw") == h.find("throw"));
    // Malformed lists are refused.
    CHECK(sprites::parseClips("idle 0 4 2.0 1 0").empty());           // too few words
    CHECK(sprites::parseClips("idle 0 4.5 2.0 1 0 -1").empty());      // half a frame
    CHECK(sprites::parseClips("idle 0 4 two 1 0 -1").empty());
    CHECK(!sprites::parseClips("idle 0 4 0 1 0 -1\nwalk 5 8 0 1 0 -1").valid(13));   // a gap
    CHECK(!sprites::parseClips("idle 0 4 0 1 0 -1\nidle 4 8 0 1 0 -1").valid(12));   // twice
    CHECK(!sprites::parseClips("idle 0 2 1 0 0 -1 0.5 0.2").valid(2));              // times falling
    CHECK(!sprites::parseClips("idle 0 2 1 0 0 -1 0.5").valid(2));                  // a time short
    CHECK(!sprites::parseClips("attack 0 4 1 0 0 1.5").valid(4));                   // released past its end
    CHECK(sprites::parseClips("").empty() && !SheetClips{}.valid(0));
    // Frames by time: evenly, or as `times` say; looped, or held on the last.
    const SheetClip& a = *s.find("attack");
    CHECK(a.frameAt(0, 2.4f, false) == 0 && a.frameAt(0.26f, 2.4f, false) == 1 && a.frameAt(1.449f, 2.4f, false) == 6);
    CHECK(a.frameAt(1.45f, 2.4f, false) == 7 && a.frameAt(2.3f, 2.4f, false) == 11 && a.frameAt(9.0f, 2.4f, false) == 11);
    const SheetClip& idle = *s.find("idle");
    CHECK(idle.frameAt(0.33f, 2.6f, true) == 1 && idle.frameAt(2.6f + 0.33f, 2.6f, true) == 1 && idle.frameAt(2.59f, 2.6f, false) == 7);
    CHECK(idle.frameAtPhase(0.5f) == 4 && idle.frameAtPhase(1.99f) == 7);
}

// Walking or running by the smoothed pace, either side of the geometric
// mean of the two clips' own paces, with a band so it does not flicker; the
// stride carried on by the ground covered, through a change of gait.
TEST(sprite_clips_walk_or_run_with_hysteresis) {
    const SheetClips s = sprites::parseClips(kArchers);
    const SheetClip& walk = *s.find("walk");
    const SheetClip& run = *s.find("run");
    CHECK_NEAR(sprites::clipSpeed(walk), 1.40, 1e-5);  // a stride a second where no seconds are given
    const float threshold = sprites::runThreshold(walk, run);
    CHECK_NEAR(threshold, std::sqrt(1.40 * 2.74), 1e-4);
    // Up past the band to run, down past it to walk; between, as he was.
    bool running = false;
    const float up[] = {1.2f, 1.9f, 2.0f, 2.15f, 2.2f, 2.0f, 1.8f, 1.75f, 1.7f, 2.1f};
    const bool expect[] = {false, false, false, false, true, true, true, true, false, false};
    for (int k = 0; k < 10; ++k) {
        running = sprites::runs(running, up[k], threshold);
        CHECK(running == expect[k]);
    }
    CHECK(!sprites::runs(true, 9.0f, 0.0f));  // no run clip: never
    // The pace eases toward what he does.
    CHECK_NEAR(sprites::easeSpeed(0, 3, sprites::kSpeedEase), 3 * (1 - std::exp(-1.0)), 1e-5);
    CHECK(sprites::easeSpeed(1.5f, 3, 0) == 1.5f);  // paused: as he was
    // In play: the walk at a walker's pace, half a stride on at half its length.
    ClipMemory m;
    Deeds d;
    d.moving = true;
    m.speed = 1.3f;
    d.stepMetres = 0.7f;
    sprites::Shown shown = sprites::chooseFrame(s, d, m);
    CHECK(shown.clip == &walk && !m.running && shown.frame == walk.first + 4);
    // Faster: the run, from the same point of the stride.
    m.speed = 2.6f;
    d.stepMetres = 0;
    shown = sprites::chooseFrame(s, d, m);
    CHECK(shown.clip == &run && m.running && shown.frame == run.first + 4);
    // The run's loop covers its own stride: a quarter of it on.
    d.stepMetres = 2.74f / 4;
    shown = sprites::chooseFrame(s, d, m);
    CHECK(shown.frame == run.first + 6);
    // Slowing within the band: still the run; below it: the walk.
    m.speed = 1.8f;
    d.stepMetres = 0;
    CHECK(sprites::chooseFrame(s, d, m).clip == &run);
    m.speed = 1.6f;
    CHECK(sprites::chooseFrame(s, d, m).clip == &walk);
    // A sheet with no run walks however fast he goes, by its own stride.
    const SheetClips h = sprites::parseClips(kHyena);
    ClipMemory hm;
    hm.speed = 6.0f;
    d.stepMetres = 1.3f * 0.19f;
    shown = sprites::chooseFrame(h, d, hm);
    CHECK(shown.clip == h.find("walk") && !hm.running && shown.frame == h.find("walk")->first + 1);
}

// The draw before the shot: in the last `release` seconds of his reload a
// missile soldier set to loose shows the frames before the release, so the
// release frame comes in the tick the sim looses (his attack tick), and the
// frames after it play from there. Given his reload left and attack tick.
TEST(sprite_clips_release_lines_up_with_the_attack_tick) {
    const SheetClips s = sprites::parseClips(kArchers);
    const SheetClip& a = *s.find("attack");
    const int releaseFrame = a.first + 7;  // times[7] == 1.45, its release
    // As a pure function of the time to the release and since the last.
    CHECK_NEAR(sprites::releaseTime(a, 2.4f, 1.45f, -1), 0.0, 1e-6);
    CHECK_NEAR(sprites::releaseTime(a, 2.4f, 0.0f, -1), 1.45, 1e-6);
    CHECK_NEAR(sprites::releaseTime(a, 2.4f, -1, 0.5f), 1.95, 1e-6);
    CHECK(sprites::releaseTime(a, 2.4f, -1, 0.95f) < 0);  // the follow-through over
    CHECK(sprites::releaseTime(a, 2.4f, 1.5f, -1) < 0);   // not yet drawing
    CHECK_NEAR(sprites::releaseTime(a, 2.4f, 1.0f, 0.4f), 0.45, 1e-6);  // a short reload: the draw cuts the follow-through
    CHECK(sprites::releaseTime(*s.find("idle"), 2.6f, 0.5f, 0.1f) < 0);  // no release, no draw
    CHECK(sprites::secondsToRelease(0, 0.3f, kTps) < 0 && sprites::secondsToRelease(3, 0.5f, kTps) == 2.5f / kTps);
    // Played through two shots as the sim counts them down: a 4 s reload
    // (120 ticks) loosed at tick 300 and again at 420.
    const int reload = 120;
    const uint32_t first = 300, next = first + reload;
    int last = -1, drawn = 0;
    for (uint32_t tick = first; tick < next + 40; ++tick)
        for (float alpha : {0.0f, 0.5f, 0.99f}) {
            const uint32_t attackTick = tick >= next ? next : first;
            const int reloadLeft = reload - static_cast<int>(tick - attackTick);  // after the step at `tick`
            Deeds d;
            d.now = (static_cast<float>(tick) + alpha) / kTps;
            d.sinceAttack = d.now - static_cast<float>(attackTick) / kTps;
            d.toRelease = sprites::secondsToRelease(reloadLeft, alpha, kTps);
            if (d.toRelease > a.release) d.toRelease = -1;  // not yet in his draw (the renderer asks only then)
            ClipMemory m;
            const int frame = sprites::chooseFrame(s, d, m).frame;
            if (tick == next && alpha == 0.0f) CHECK(frame == releaseFrame);  // loosed: the release frame, at once
            if (tick == next - 1 && alpha > 0.9f) CHECK(frame == releaseFrame - 1);  // a breath before
            if (tick == first && alpha == 0.0f) CHECK(frame == releaseFrame);
            if (tick > first + 29 && tick < next - 44) CHECK(inClip(s, "idle", frame));  // between: at ease
            if (tick >= next - 43 && tick < next) {
                // Drawing: from the clip's first frame, never back.
                CHECK(inClip(s, "attack", frame) && frame < releaseFrame && frame >= last);
                last = frame;
                ++drawn;
            }
            if (tick == next - 43 && alpha == 0.0f) CHECK(frame == a.first);
            if (tick >= next && tick < next + 28) CHECK(frame >= releaseFrame && frame < a.first + a.frames);  // the follow-through
            if (tick == next + 28 && alpha == 0.0f) CHECK(frame == a.first + 11);  // its last frame, 2.38 s in
            if (tick >= next + 29) CHECK(inClip(s, "idle", frame));
        }
    CHECK(drawn == 43 * 3);
    // Not set to loose (no mark in range): no draw, only the follow-through of a shot.
    Deeds idle;
    idle.now = 10;
    idle.sinceAttack = 3;
    ClipMemory m;
    CHECK(inClip(s, "idle", sprites::chooseFrame(s, idle, m).frame));
    // A sheet of the fixed layout's kind of attack (no release) plays the blow from its start.
    const SheetClips h = sprites::parseClips(kHyena);
    Deeds blow;
    blow.sinceAttack = 0.31f;
    blow.toRelease = 0.2f;  // nothing to draw
    CHECK(sprites::chooseFrame(h, blow, m).frame == firstOf(h, "attack") + 3);
}

// A scavenger's pickup when he takes something up, his throw drawn back so
// the thing leaves his hand as his wind-up runs out, and the slash of a
// plain blow; on a sheet without them, the attack as ever.
TEST(sprite_clips_pickup_and_throw) {
    const SheetClips h = sprites::parseClips(kHyena);
    ClipMemory m;
    const auto look = [&](bool holding, bool striking, bool retreating, int timer, uint32_t tick, uint32_t attackTick) {
        sprites::observeScavenger(m, holding, striking, retreating, timer, tick, attackTick, kTps);
    };
    const auto deeds = [&](uint32_t tick, float alpha, uint32_t attackTick, bool striking, int timer) {
        Deeds d;
        d.now = (static_cast<float>(tick) + alpha) / kTps;
        d.sinceAttack = attackTick == UINT32_MAX ? -1 : d.now - static_cast<float>(attackTick) / kTps;
        d.attackFrom = attackTick == m.claimed ? m.claimedBy : 0;
        d.sincePickup = m.pickupAt >= 0 ? d.now - m.pickupAt : -1;
        d.toThrow = striking ? sprites::secondsToRelease(timer, alpha, kTps) : -1;
        d.sinceThrow = m.throwAt >= 0 ? d.now - m.throwAt : -1;
        d.attackSeconds = 0.6f;
        return d;
    };
    look(false, false, false, 0, 100, UINT32_MAX);
    // He stoops and takes a head (the sim's stoop: his attack tick at 102).
    look(true, false, false, 0, 103, 102);
    CHECK_NEAR(m.pickupAt, 102 / kTps, 1e-6);
    CHECK(m.claimed == 102 && m.claimedBy == 1);
    Deeds d = deeds(103, 0, 102, false, 0);
    CHECK(sprites::chooseFrame(h, d, m).frame == firstOf(h, "pickup"));
    d = deeds(118, 0, 102, false, 0);  // half a second on: still stooping, not the attack
    CHECK(sprites::chooseFrame(h, d, m).frame == firstOf(h, "pickup") + 3);
    d = deeds(130, 0, 102, false, 0);  // done
    CHECK(inClip(h, "idle", sprites::chooseFrame(h, d, m).frame));
    // Drawn back to throw (his attack tick at 140, the wind-up 12 ticks): the frames before the release.
    look(true, true, false, 12, 140, 140);
    CHECK(m.releaseTick == 152 && m.claimed == 140 && m.claimedBy == 2);
    d = deeds(140, 0, 140, true, 12);
    CHECK_NEAR(d.toThrow, 0.4, 1e-6);
    CHECK(sprites::chooseFrame(h, d, m).frame == firstOf(h, "throw") + 1);  // 0.2 s into it
    look(true, true, false, 1, 151, 140);
    d = deeds(151, 0.5f, 140, true, 1);
    CHECK(sprites::chooseFrame(h, d, m).frame == firstOf(h, "throw") + 4);  // a breath before the release
    // Out of his hand (seen a tick late): the release frame from the tick the wind-up ran out.
    look(false, false, true, 0, 153, 140);
    CHECK_NEAR(m.throwAt, 152 / kTps, 1e-6);
    d = deeds(152, 0, 140, false, 0);
    CHECK(sprites::chooseFrame(h, d, m).frame == firstOf(h, "throw") + 4);
    d = deeds(160, 0, 140, false, 0);
    CHECK(sprites::chooseFrame(h, d, m).frame == firstOf(h, "throw") + 6);
    d = deeds(170, 0, 140, false, 0);  // the throw played out: not the attack his drawing back made
    CHECK(inClip(h, "idle", sprites::chooseFrame(h, d, m).frame));
    // A slash: a blow of his own (attack tick 180), the attack clip.
    look(false, false, false, 0, 181, 180);
    d = deeds(181, 0, 180, false, 0);
    CHECK(d.attackFrom == 0 && inClip(h, "attack", sprites::chooseFrame(h, d, m).frame));
    // On a sheet with no pickup or throw, his stoop and his drawing back play the attack, as ever.
    const SheetClips plain = sprites::parseClips("idle 0 4 2 1 0 -1\nwalk 4 8 0 1 1.3 -1\nattack 12 6 0.6 0 0 -1\nflinch 18 2 0.3 0 0 -1\ndie_back 20 6 0.9 0 0 -1");
    ClipMemory pm;
    sprites::observeScavenger(pm, false, false, false, 0, 100, UINT32_MAX, kTps);
    sprites::observeScavenger(pm, true, false, false, 0, 103, 102, kTps);
    Deeds pd;
    pd.now = 103 / kTps;
    pd.sinceAttack = 1 / kTps;
    pd.attackFrom = 1;
    pd.sincePickup = 1 / kTps;
    pd.attackSeconds = 0.6f;
    CHECK(sprites::chooseFrame(plain, pd, pm).frame == firstOf(plain, "attack"));
    // Snatched on the run (no stoop: no fresh attack tick): the pickup from now; and
    // drawn back and thrown between two looks: the throw from the look that saw it gone.
    ClipMemory r;
    sprites::observeScavenger(r, false, false, false, 0, 200, 102, kTps);
    sprites::observeScavenger(r, true, false, false, 0, 204, 102, kTps);
    CHECK_NEAR(r.pickupAt, 204 / kTps, 1e-6);
    CHECK(r.claimedBy == 0);
    sprites::observeScavenger(r, false, false, true, 0, 212, 210, kTps);
    CHECK_NEAR(r.throwAt, 212 / kTps, 1e-6);
    CHECK(r.claimed == 210 && r.claimedBy == 2);
    // The first look at a soldier already holding something is no pickup.
    ClipMemory late;
    sprites::observeScavenger(late, true, false, false, 0, 50, 40, kTps);
    CHECK(late.pickupAt < 0);
}

// A won field: the winners cheer (celebrate, looped), or stand at ease on a
// sheet without it; a deed (a blow, a step) comes first.
TEST(sprite_clips_the_winners_celebrate) {
    const SheetClips s = sprites::parseClips(kArchers);
    ClipMemory m;
    Deeds d;
    d.now = 100.0f;
    d.celebrating = true;
    CHECK(inClip(s, "celebrate", sprites::chooseFrame(s, d, m).frame));
    d.now += 2.7f * 0.5f;
    CHECK(inClip(s, "celebrate", sprites::chooseFrame(s, d, m).frame));
    d.sinceHit = 0.1f;
    CHECK(inClip(s, "flinch", sprites::chooseFrame(s, d, m).frame));
    d.sinceHit = -1;
    d.moving = true;
    CHECK(inClip(s, "walk", sprites::chooseFrame(s, d, m).frame));
    const SheetClips h = sprites::parseClips(kHyena);
    d.moving = false;
    CHECK(inClip(h, "idle", sprites::chooseFrame(h, d, m).frame));
    // And a rank at ease does not breathe as one: each soldier's idle is offset.
    Deeds a, b;
    a.now = b.now = 5.0f;
    a.idlePhase = 0.0f;
    b.idlePhase = 0.37f * 5;
    CHECK(sprites::chooseFrame(s, a, m).frame != sprites::chooseFrame(s, b, m).frame);
}

// Old sheets: every soldier's frame exactly as before, over a grid of what he
// can be doing (struck, hurt, walking, flying, at ease, for any type's stroke
// and stride, pre-rendered flier or not, at any time and for any id).
TEST(sprite_fixed_layout_frames_as_they_always_were) {
    int checked = 0, mismatched = 0;
    for (float struck : {1e9f, 0.0f, 0.05f, 0.3f, 0.449f, 0.55f, 0.79f, 0.8f, 0.899f, 0.9f, 2.0f})
        for (float strike : {0.45f, 0.56f, 0.8f, 0.9f, 1.0f})
            for (float hurt : {1e9f, 0.0f, 0.1f, 0.149f, 0.15f, 0.29f, 0.3f, 0.31f})
                for (int how = 0; how < 4; ++how)
                    for (float walked : {0.0f, 0.4f, 1.53f, 7.77f, 1234.5f})
                        for (float stride : {1.1f, 1.53f, 3.2f, 5.0f})
                            for (bool flier : {false, true})
                                for (float t : {0.0f, 0.37f, 12.8f, 999.97f})
                                    for (uint32_t id : {1u, 13u, 27u, 1001u}) {
                                        const bool moving = (how & 1) != 0, inAir = (how & 2) != 0;
                                        const int now = sprites::fixedLayoutFrame(struck, strike, hurt, moving, inAir, walked, stride, flier, t, id);
                                        const int was = fixedLayoutFrameAsItWas(struck, strike, hurt, moving, inAir, walked, stride, flier, t, id);
                                        mismatched += now != was;
                                        ++checked;
                                        CHECK(now >= 0 && now < sprites::kLegacyFramesPerView);
                                    }
    CHECK(mismatched == 0);
    CHECK(checked > 100000);
    // A few by hand: the stroke's frames, the flinch's two, a stride, a flier's wingbeat.
    CHECK(sprites::fixedLayoutFrame(0.45f, 0.9f, 1e9f, false, false, 0, 1.5f, false, 0, 1) == 15);
    CHECK(sprites::fixedLayoutFrame(1e9f, 0.9f, 0.2f, false, false, 0, 1.5f, false, 0, 1) == 19);
    CHECK(sprites::fixedLayoutFrame(1e9f, 0.9f, 1e9f, true, false, 0.75f, 1.5f, false, 0, 1) == 8);
    CHECK(sprites::fixedLayoutFrame(1e9f, 0.9f, 1e9f, false, true, 0, 1.5f, true, 0.5f, 1) == 4 + 4 + 2);
}
