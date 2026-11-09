// One flag to a pole (game/src/flag_poles.cpp): when the holder changes the old
// flag comes all the way down, and only then the new one goes up.
#include "illyria/game/flag_poles.h"

#include "test.h"

using namespace illyria::game;

namespace {
const V3 kRed{0.9f, 0.1f, 0.1f}, kBlue{0.1f, 0.2f, 0.9f}, kGold{0.9f, 0.8f, 0.1f};
bool is(const V3& a, const V3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
}  // namespace

TEST(pole_lowers_the_old_flag_then_raises_the_new) {
    PoleTween t;
    CHECK(t.advance(0.1f, {kRed, 1}).raised == 1);
    // The holder changes: the red comes down, never rising, never the blue until it is at the foot.
    float last = 1;
    bool bottomed = false, rose = false;
    for (int i = 0; i < 400; ++i) {
        const PoleShown s = t.advance(0.01f, {kBlue, 1});
        if (!bottomed) {
            if (is(s.colour, kRed)) {
                CHECK(s.raised <= last);  // lowering
                last = s.raised;
            } else {
                CHECK(is(s.colour, kBlue));
                bottomed = true;
                CHECK(last < 0.02f);  // the red was at the foot before the blue showed
                last = s.raised;
            }
        } else {
            CHECK(is(s.colour, kBlue));
            CHECK(s.raised >= last);  // raising, never back
            rose = rose || s.raised > 0.5f;
            last = s.raised;
        }
    }
    CHECK(bottomed && rose && last == 1);
}

TEST(pole_changeover_takes_seconds_not_a_frame) {
    PoleTween t;
    t.advance(0, {kRed, 1});
    const PoleShown halfway = t.advance(PoleTween::kLowerSeconds / 2, {kBlue, 1});
    CHECK(is(halfway.colour, kRed) && halfway.raised > 0.4f && halfway.raised < 0.6f);
    const PoleShown empty = t.advance(PoleTween::kLowerSeconds, {kBlue, 1});
    CHECK(!empty.visible());
    const PoleShown rising = t.advance(PoleTween::kRaiseSeconds / 2, {kBlue, 1});
    CHECK(is(rising.colour, kBlue) && rising.raised > 0.4f && rising.raised < 0.6f);
}

TEST(pole_height_follows_progress_for_the_same_holder) {
    PoleTween t;
    t.advance(0, {kRed, 0.2f});
    CHECK(t.advance(10, {kRed, 0.8f}).raised == 0.8f);
    CHECK(t.advance(10, {kRed, 0.3f}).raised == 0.3f);
}

TEST(contested_pole_is_one_flag_lowering_then_raising) {
    float last = 2;
    for (int i = 0; i <= 100; ++i) {
        const PoleTarget t = contestedPole(kRed, kBlue, static_cast<float>(i) / 100.0f);
        if (i < 50) {
            CHECK(is(t.colour, kRed) && t.level <= last);
        } else if (i > 50) {
            CHECK(is(t.colour, kBlue) && t.level >= last);
        }
        last = t.level;
    }
    CHECK(contestedPole(kRed, kBlue, 0).level == 1 && is(contestedPole(kRed, kBlue, 0).colour, kRed));
    CHECK(contestedPole(kRed, kBlue, 1).level == 1 && is(contestedPole(kRed, kBlue, 1).colour, kBlue));
    CHECK(contestedPole(kRed, kBlue, 0.5f).level == 0);
}

TEST(pole_set_forgets_poles_not_asked_for) {
    PoleSet set;
    set.advance(1, 0, {kRed, 1});
    set.advance(2, 0, {kGold, 1});
    set.endFrame();
    set.advance(1, 0, {kRed, 1});
    set.endFrame();
    // Pole 2 is new again: shown at once in its holder's colours, not tweened from the old state.
    CHECK(is(set.advance(2, 0.01f, {kBlue, 1}).colour, kBlue));
}
