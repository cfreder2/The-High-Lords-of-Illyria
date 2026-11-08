#include <cmath>
#include <vector>

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
    for (int i = 0; i < 120; ++i) c.advance(0.016f);  // the zoom glides
    CHECK_NEAR(c.distance(), d0 / 1.5f, 0.5);
    CHECK(taps == 1);  // ending a pinch is not a tap
}

TEST(battle_runs_and_player_can_command_a_squad) {
    Battle b(42);
    CHECK(b.outcome() == 0);
    // Every unit type is on the field (but the retired legion: units.h retired()).
    std::vector<bool> seen(illyria::sim::kUnitTypeCount, false);
    for (const Squad& s : b.squads()) seen[static_cast<int>(s.type)] = true;
    for (int t = 0; t < illyria::sim::kUnitTypeCount; ++t) CHECK(seen[static_cast<size_t>(t)] != illyria::sim::retired(static_cast<illyria::sim::UnitType>(t)));

    Camera c;
    c.setBounds(Battle::kWorldSize);
    c.setViewport(800, 600);
    c.setHeightFn([&](float x, float y) { return b.heightAt(x, y); });
    // Select the Grey Watch by tapping its centre, then send it 40 m north.
    const int watchIndex = 1;
    const Squad& watch = b.squads()[watchIndex];
    CHECK(watch.name == "The Grey Watch");
    c.lookAt(b.squadCentre(watch).x, b.squadCentre(watch).y, 80);
    float px, py;
    CHECK(c.project(b.squadCentre(watch), px, py));
    // One tap: that soldier alone; again: every heavy infantryman in view (the Watch).
    b.tap(c, px, py, 1.0f);
    CHECK(b.selectedAlive().size() == 1);
    CHECK(b.squadOf(b.selectedAlive().front()) == watchIndex);
    b.tap(c, px, py, 1.0f);
    CHECK(b.selectedSquad() == watchIndex);
    const V3 start = b.squadCentre(watch);
    V3 goal = start + V3{0, 40, 0};
    goal.z = b.heightAt(goal.x, goal.y);
    CHECK(c.project(goal, px, py));
    b.tap(c, px, py, 1.0f);
    for (int i = 0; i < 30 * 30; ++i) b.advance(1.0 / 30);
    CHECK((b.squadCentre(watch) - goal).length() < 6.0f);
    for (int i = 0; i < 120 * 30; ++i) b.advance(1.0 / 30);
    int remains = 0;
    for (const auto& r : b.world().remains()) remains += r.kind != illyria::sim::RemainKind::None;
    CHECK(remains > 0);
    CHECK(b.groundMarksAdded() > 0);
    CHECK(!b.takeDirtyRemains().empty() || remains > 0);
}

namespace {
void settle(Camera& c) {
    for (int i = 0; i < 300; ++i) c.advance(0.016f);
}
}  // namespace

TEST(camera_zoom_reaches_both_limits_and_banks_nothing_beyond_them) {
    Camera c = flatCamera();
    CHECK(Camera::kMinDistance <= 2.0f);
    for (int i = 0; i < 200; ++i) c.zoomAt(1.5f, 400, 300);
    settle(c);
    CHECK_NEAR(c.distance(), Camera::kMinDistance, 0.001);
    // 200 notches past the limit are not owed: one step out moves it at once.
    c.zoomAt(1.0f / 1.5f, 400, 300);
    settle(c);
    CHECK_NEAR(c.distance(), Camera::kMinDistance * 1.5f, 0.01);
    for (int i = 0; i < 200; ++i) c.zoomAt(1.0f / 1.5f, 400, 300);
    settle(c);
    CHECK_NEAR(c.distance(), c.maxDistance(), 0.5);
    c.zoomAt(1.5f, 400, 300);
    settle(c);
    CHECK_NEAR(c.distance(), c.maxDistance() / 1.5f, 0.5);
}

TEST(camera_wheel_steps_are_equal_in_log_distance_at_every_distance_and_input) {
    for (const float start : {10.0f, 100.0f, 1000.0f}) {
        Camera c = flatCamera();
        c.setBounds(-1e5f, -1e5f, 1e5f, 1e5f);
        c.lookAt(512, 512, start);
        Gestures g(c, [](float, float) {});
        g.wheel(-100, 400, 300);  // a mouse notch in
        settle(c);
        CHECK_NEAR(std::log(start / c.distance()), 0.15, 1e-3);
        // Many small trackpad-sized deltas that add up to a notch are the same step.
        c.lookAt(512, 512, start);
        for (int i = 0; i < 25; ++i) g.wheel(-4, 400, 300);
        settle(c);
        CHECK_NEAR(std::log(start / c.distance()), 0.15, 1e-3);
        // A browser pinch (ctrl-wheel) and a pinch's scale are one zoom.
        c.lookAt(512, 512, start);
        g.pinchWheel(-10, 400, 300);
        settle(c);
        const float viaWheel = c.distance();
        c.lookAt(512, 512, start);
        g.pinch(std::exp(0.1f), 0, 400, 300);
        settle(c);
        CHECK_NEAR(c.distance(), viaWheel, 1e-3 * start);
    }
}

TEST(camera_zoom_holds_the_point_under_the_cursor) {
    Camera c = flatCamera();
    c.setBounds(-1e5f, -1e5f, 1e5f, 1e5f);
    for (const float start : {400.0f, 40.0f, 8.0f}) {
        c.lookAt(512, 512, start);
        c.rotate(0.7f);
        V3 before, after;
        CHECK(c.pickGround(650, 420, before));
        for (int i = 0; i < 6; ++i) c.zoomAt(1.2f, 650, 420);  // wheel notches in a row
        for (int i = 0; i < 30; ++i) c.advance(0.016f);
        CHECK(c.distance() < start);
        CHECK(c.pickGround(650, 420, after));
        CHECK_NEAR(after.x, before.x, 0.02 * start / 40 + 0.02);
        CHECK_NEAR(after.y, before.y, 0.02 * start / 40 + 0.02);
        for (int i = 0; i < 6; ++i) c.zoomAt(1.0f / 1.2f, 650, 420);
        settle(c);
        CHECK(c.pickGround(650, 420, after));
        CHECK_NEAR(after.x, before.x, 0.02 * start / 40 + 0.02);
        CHECK_NEAR(after.y, before.y, 0.02 * start / 40 + 0.02);
    }
}

TEST(camera_never_goes_below_the_terrain_at_the_closest_zoom) {
    Camera c = flatCamera();
    const auto hills = [](float x, float y) { return 3.0f + 2.0f * std::sin(x * 0.4f) + 2.0f * std::cos(y * 0.5f); };
    c.setHeightFn(hills);
    for (float yaw = 0; yaw < 6.3f; yaw += 0.9f) {
        c.lookAt(512, 512, 200);
        c.rotate(yaw - c.yaw());
        for (int i = 0; i < 100; ++i) c.zoomAt(1.5f, 400, 300);
        settle(c);
        CHECK_NEAR(c.distance(), Camera::kMinDistance, 0.001);
        for (float step = 0; step < 20; step += 0.7f) {
            c.pan(0.7f, 0.7f);
            const V3 e = c.eye();
            CHECK(e.z >= hills(e.x, e.y) + 0.3f);
        }
    }
}
