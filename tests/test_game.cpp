#include <cmath>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/game/gestures.h"
#include "test.h"

using namespace illyria::game;

namespace {
Camera flatCamera() {
    Camera c;
    c.setBounds(1024);
    c.setViewport(800, 600);
    c.setHeightFn([](float, float) { return 0.0f; });
    c.lookAt(512, 512, 200);
    return c;
}
}  // namespace

TEST(camera_projects_and_picks_consistently) {
    Camera c = flatCamera();
    float px, py;
    CHECK(c.project(c.target(), px, py));
    CHECK_NEAR(px, 400, 0.5);
    CHECK_NEAR(py, 300, 0.5);
    V3 hit;
    CHECK(c.pickGround(400, 300, hit));
    CHECK_NEAR(hit.x, 512, 0.5);
    CHECK_NEAR(hit.y, 512, 0.5);
    CHECK(c.pickGround(100, 450, hit));
    CHECK(c.project(hit, px, py));
    CHECK_NEAR(px, 100, 0.5);
    CHECK_NEAR(py, 450, 0.5);
}

TEST(camera_pitch_goes_from_horizon_to_top_down) {
    Camera c = flatCamera();
    c.lookAt(512, 512, Camera::kMinDistance);
    const float near = c.pitch();
    c.lookAt(512, 512, Camera::kMaxDistance);
    CHECK(near < 0.4f);
    CHECK(c.pitch() > 1.2f);
}

TEST(gestures_drag_pans_and_keeps_ground_under_finger) {
    Camera c = flatCamera();
    int taps = 0;
    Gestures g(c, [&](float, float) { ++taps; });
    V3 before;
    c.pickGround(300, 300, before);
    g.pointer(PointerEvent::Down, 1, 300, 300, 0, 0);
    g.pointer(PointerEvent::Move, 1, 350, 320, 0, 50);
    g.pointer(PointerEvent::Move, 1, 420, 340, 0, 100);
    g.pointer(PointerEvent::Up, 1, 420, 340, 0, 150);
    V3 after;
    c.pickGround(420, 340, after);
    CHECK_NEAR(after.x, before.x, 0.5);
    CHECK_NEAR(after.y, before.y, 0.5);
    CHECK(taps == 0);
}

TEST(gestures_tap_and_pinch) {
    Camera c = flatCamera();
    int taps = 0;
    Gestures g(c, [&](float, float) { ++taps; });
    g.pointer(PointerEvent::Down, 1, 200, 200, 0, 0);
    g.pointer(PointerEvent::Up, 1, 203, 201, 0, 120);
    CHECK(taps == 1);

    const float d0 = c.distance();
    g.pointer(PointerEvent::Down, 1, 300, 300, 0, 1000);
    g.pointer(PointerEvent::Down, 2, 500, 300, 0, 1000);
    g.pointer(PointerEvent::Move, 2, 600, 300, 0, 1050);  // fingers spread 200 -> 300 px
    g.pointer(PointerEvent::Up, 2, 600, 300, 0, 1100);
    g.pointer(PointerEvent::Up, 1, 300, 300, 0, 1100);
    CHECK_NEAR(c.distance(), d0 / 1.5f, 0.5);
    CHECK(taps == 1);  // ending a pinch is not a tap
}

TEST(battle_runs_and_player_can_command_a_squad) {
    Battle b(42);
    CHECK(b.outcome() == 0);
    Camera c;
    c.setBounds(Battle::kWorldSize);
    c.setViewport(800, 600);
    c.setHeightFn([&](float x, float y) { return b.heightAt(x, y); });
    // Select the Grey Watch by tapping its centre, then send it 40 m north.
    const Squad& watch = b.squads()[2];
    c.lookAt(b.squadCentre(watch).x, b.squadCentre(watch).y, 80);
    float px, py;
    CHECK(c.project(b.squadCentre(watch), px, py));
    b.tap(c, px, py, 1.0f);
    CHECK(b.selectedSquad() == 2);
    const V3 start = b.squadCentre(watch);
    V3 goal = start + V3{0, 40, 0};
    goal.z = b.heightAt(goal.x, goal.y);
    CHECK(c.project(goal, px, py));
    b.tap(c, px, py, 1.0f);
    for (int i = 0; i < 30 * 30; ++i) b.advance(1.0 / 30);
    CHECK((b.squadCentre(watch) - goal).length() < 6.0f);
    for (int i = 0; i < 240 * 30; ++i) b.advance(1.0 / 30);
    CHECK(!b.stuckArrows().empty());
    CHECK(!b.bloodStains().empty());
}
