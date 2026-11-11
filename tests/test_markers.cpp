// The decals under soldiers and the order markers (battle_orders.cpp,
// renderer_orders.cpp): which show, and at what height they lie. The decal
// shader drapes a mark over the battlefield's height field when its height is
// the ground's (Battle::heightAt, the same triangles as the terrain mesh) and
// lays it flat on a deck when it is higher; so the heights are checked here.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/sim/region.h"
#include "test.h"

using namespace illyria::sim;
using illyria::game::Battle;
using illyria::game::V3;

namespace {
int squadNamed(const Battle& b, const char* name) {
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].name == name) return static_cast<int>(i);
    return -1;
}
void run(Battle& b, double seconds) {
    for (double t = 0; t < seconds; t += 0.1) b.advance(0.1);
}
std::vector<Battle::OrderMark> slots(const Battle& b) {
    std::vector<Battle::OrderMark> out;
    for (const Battle::OrderMark& m : b.orderMarks())
        if (m.kind == Battle::OrderMark::Kind::Slot) out.push_back(m);
    return out;
}
// The shader's test (kDecalVs): flat on a deck when this far above the ground.
constexpr float kDeckLift = 0.12f;
// The besieged town's own walls, among its neighbours' in the window.
const Rampart& siegeWalls(const Battle& b) {
    const V3 o = b.origin(), town = illyria::game::toV3(illyria::game::siegeTown(b.continent()).pos) - V3{o.x, o.y, 0};
    const Rampart* best = &b.region().ramparts().front();
    float bestD = 1e30f;
    for (const Rampart& rp : b.region().ramparts()) {
        const V3 c = illyria::game::toV3(rp.centre);
        const float d = (c.x - town.x) * (c.x - town.x) + (c.y - town.y) * (c.y - town.y);
        if (d < bestD) {  // the outer ring comes first, and keeps ties
            bestD = d;
            best = &rp;
        }
    }
    return *best;
}
}  // namespace

TEST(order_markers_lie_on_the_ground_the_terrain_draws) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(watch)], false);
    // Over open ground, slopes and the road the way west: every marker is at
    // the drawn ground's height at its centre, so the shader drapes it.
    int checked = 0;
    for (const V3 off : {V3{-30, 10, 0}, V3{-60, -20, 0}, V3{25, 40, 0}, V3{-90, 35, 0}}) {
        V3 to = c + off;
        to.z = b.heightAt(to.x, to.y);
        b.orderMove(to);
        b.advance(0.05);
        for (const Battle::OrderMark& m : slots(b)) {
            CHECK_NEAR(m.pos.z, b.heightAt(m.pos.x, m.pos.y), 1e-3);
            CHECK(m.pos.z < b.heightAt(m.pos.x, m.pos.y) + kDeckLift);
            ++checked;
        }
    }
    CHECK(checked > 20);
    // And off the ground (a point high above it, as a flier aloft): down on it.
    const V3 high{c.x, c.y, b.heightAt(c.x, c.y) + 30};
    CHECK_NEAR(b.floorAt(high), b.heightAt(c.x, c.y), 1e-3);
}

TEST(markers_show_only_for_those_selected_and_their_current_orders) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    const std::vector<EntityId> squad = b.squads()[static_cast<size_t>(watch)].members;
    CHECK(squad.size() > 3);
    b.selectSquad(watch);
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(watch)], false);
    V3 to = c + V3{-40, 8, 0};
    to.z = b.heightAt(to.x, to.y);
    b.orderMove(to);
    b.advance(0.05);
    CHECK(slots(b).size() == squad.size());
    // One soldier picked out of the squad: once the order's moment has passed,
    // only his marker shows, not his squad-mates'.
    b.select({squad[0]});
    std::vector<Battle::OrderMark> now = slots(b);
    CHECK(now.size() == squad.size());  // just ordered: the others still fading
    for (const Battle::OrderMark& m : now) CHECK(m.unit == squad[0] ? m.opacity == 1.0f : m.opacity <= 1.0f);
    run(b, 2.0);
    for (const Battle::OrderMark& m : slots(b))
        if (m.unit != squad[0]) CHECK(m.opacity < 1.0f && m.opacity > 0.0f);
    run(b, 1.0);
    now = slots(b);
    CHECK(now.size() == 1);
    CHECK(!now.empty() && now.front().unit == squad[0] && now.front().opacity == 1.0f);
    // A new order replaces his old marker (one marker, at the new place).
    V3 again = to + V3{0, -20, 0};
    again.z = b.heightAt(again.x, again.y);
    b.orderMove(again);
    b.advance(0.05);
    now = slots(b);
    CHECK(now.size() == 1);
    CHECK(!now.empty() && std::hypot(now.front().pos.x - again.x, now.front().pos.y - again.y) < 3.0f);
    // There (up the bank, 1 in 6): none.
    run(b, 45);
    CHECK(slots(b).empty());
    // Nothing selected and nothing just ordered: no markers at all.
    b.clearSelection();
    CHECK(b.orderMarks().empty());
}

TEST(a_soldier_on_the_wall_walk_has_his_ring_on_the_walk) {
    Battle b(1, illyria::game::Scenario::Siege);
    run(b, 1);
    // The defenders up on the walls: their floor (where the shadow and the
    // selection ring are drawn) is the walk, well above the ground below it,
    // so the shader lays the ring flat on the walk.
    int onWalk = 0;
    for (size_t i = 0; i < b.world().units().size(); ++i) {
        const Unit& u = b.world().units()[i];
        if (!u.alive()) continue;
        const V3 p = b.unitPosition(i);
        const float ground = b.heightAt(p.x, p.y);
        const float floor = b.floorAt(p);
        if (p.z > ground + 3) {
            ++onWalk;
            CHECK_NEAR(floor, p.z, 0.02);
            CHECK(floor > ground + kDeckLift);
            // A flier hovering above him would have its ring on the walk too.
            CHECK_NEAR(b.floorAt({p.x, p.y, p.z + 15}), p.z, 0.02);
        } else {
            CHECK_NEAR(floor, p.z, 0.02);  // on the ground or a stair: where he stands
        }
    }
    CHECK(onWalk >= 8);
    // Just beside the wall, at the walk's height in the air: the ground.
    const Rampart& rp = siegeWalls(b);
    const V3 corner = illyria::game::toV3(rp.corners.front());
    const V3 out = V3{corner.x - illyria::game::toV3(rp.centre).x, corner.y - illyria::game::toV3(rp.centre).y, 0}.normalized();
    const V3 beyond = corner + out * 25.0f;
    CHECK_NEAR(b.floorAt({beyond.x, beyond.y, corner.z}), b.heightAt(beyond.x, beyond.y), 1e-3);
}

TEST(a_tap_on_a_wall_walk_its_face_or_a_ramp_lands_there) {
    // The order's spot (and its marker) is where the tap lands on the castle:
    // on the walk, on the walk above the face struck, partway up the ramp;
    // not on the ground beneath or behind.
    Battle b(1, illyria::game::Scenario::Siege);
    const Rampart& rp = siegeWalls(b);
    const V3 a = illyria::game::toV3(rp.corners[0]), c1 = illyria::game::toV3(rp.corners[1]);
    const V3 walk = (a + c1) * 0.5f;
    const V3 centre = illyria::game::toV3(rp.centre);
    const V3 out = V3{walk.x - centre.x, walk.y - centre.y, 0}.normalized();
    // Seen from outside the ring, looking in at the wall.
    illyria::game::Camera cam;
    cam.setBounds(Battle::kWorldSize);
    cam.setViewport(800, 600);
    cam.setHeightFn([&b](float x, float y) { return b.heightAt(x, y); });
    cam.lookAt(walk.x, walk.y, 45);
    float best = -1, bestYaw = 0;
    for (int k = 0; k < 16; ++k) {
        cam.rotate(0.3927f);
        const V3 e = cam.eye();
        const float d = (e.x - walk.x) * out.x + (e.y - walk.y) * out.y;
        if (d > best) best = d, bestYaw = cam.yaw();
    }
    cam.rotate(bestYaw - cam.yaw());
    const auto tap = [&](const V3& at, V3& hit) {
        float px, py;
        return cam.project(at, px, py) && b.pickFloor(cam, px, py, hit);
    };
    V3 hit;
    CHECK(tap(walk, hit));
    CHECK(std::hypot(hit.x - walk.x, hit.y - walk.y) < 1.5f);
    CHECK_NEAR(hit.z, walk.z, 0.3);
    CHECK(walk.z > b.heightAt(walk.x, walk.y) + 3);  // well above the ground there
    // The outer face, two metres under the walk: the spot goes up onto the walk above it.
    const V3 face = walk + out * static_cast<float>(rp.halfThick.toDouble()) + V3{0, 0, -2};
    CHECK(tap(face, hit));
    CHECK_NEAR(hit.z, walk.z, 0.3);
    // Halfway up a ramp (inside the ring, against the wall: seen from inside): on the ramp, at its height there.
    if (!rp.ramps.empty()) {
        const V3 foot = illyria::game::toV3(rp.ramps.front().first), head = illyria::game::toV3(rp.ramps.front().second);
        V3 mid = (foot + head) * 0.5f;
        // On its surface there (the head is the landing's end, so a steep
        // ramp's slope stands above the halfway height).
        Fixed surface;
        if (b.region().deckBelow(illyria::game::toFixed(mid.x), illyria::game::toFixed(mid.y), illyria::game::toFixed(mid.z + 2), surface))
            mid.z = static_cast<float>(surface.toDouble());
        const V3 in = V3{centre.x - mid.x, centre.y - mid.y, 0}.normalized();
        cam.lookAt(mid.x, mid.y, 30);
        best = -1e9f;
        for (int k = 0; k < 16; ++k) {
            cam.rotate(0.3927f);
            const V3 e = cam.eye();
            const float d = (e.x - mid.x) * in.x + (e.y - mid.y) * in.y;
            if (d > best) best = d, bestYaw = cam.yaw();
        }
        cam.rotate(bestYaw - cam.yaw());
        CHECK(tap(mid, hit));
        CHECK(std::hypot(hit.x - mid.x, hit.y - mid.y) < 1.5f);
        CHECK(std::abs(hit.z - mid.z) < 0.6f);
        CHECK(hit.z > b.heightAt(hit.x, hit.y) + 1);
    }
}

TEST(a_tapped_gate_is_shown_with_its_health) {
    Battle b(1, illyria::game::Scenario::Siege);
    CHECK(!b.region().gates().empty());
    const auto& g = b.region().gates().front();
    const V3 mid = (illyria::game::toV3(g.a) + illyria::game::toV3(g.b)) * 0.5f + V3{0, 0, 2};
    illyria::game::Camera cam;
    cam.setBounds(Battle::kWorldSize);
    cam.setViewport(800, 600);
    cam.setHeightFn([&b](float x, float y) { return b.heightAt(x, y); });
    cam.lookAt(mid.x, mid.y, 40);
    float px, py;
    CHECK(cam.project(mid, px, py));
    b.clearSelection();
    b.tap(cam, px, py, 1.0f);
    CHECK(b.inspectedGate() == 0);
    CHECK(b.notice().find("gate") != std::string::npos);
    CHECK(b.notice().find(std::to_string(g.maxHp)) != std::string::npos);
    // Anywhere else: let go.
    float qx, qy;
    const V3 away = mid + illyria::game::toV3(g.out) * 30.0f;
    CHECK(cam.project({away.x, away.y, b.heightAt(away.x, away.y)}, qx, qy));
    b.tap(cam, qx, qy, 1.0f);
    CHECK(b.inspectedGate() == -1);
}

TEST(a_flier_sent_onto_the_wall_walk_has_his_marker_on_the_walk) {
    Battle b(1, illyria::game::Scenario::Siege);
    const int griffins = squadNamed(b, "Eyrie Griffins");
    CHECK(griffins >= 0);
    b.select({b.squads()[static_cast<size_t>(griffins)].members.front()});  // one: his slot is the spot itself
    // A spot on the walk (behind an embrasure), from far enough off to fly there.
    // (One nobody of the garrison stands at: a place taken is not a place to land.)
    const Rampart::Embrasure* free = &siegeWalls(b).embrasures.front();
    for (const Rampart::Embrasure& c : siegeWalls(b).embrasures) {
        bool taken = false;
        for (const Unit& u : b.world().units()) taken = taken || (u.alive() && (u.pos - c.pos).flat().length() < Fixed::fromInt(3));
        if (!taken) {
            free = &c;
            break;
        }
    }
    const Rampart::Embrasure& e = *free;
    const V3 out = illyria::game::toV3(e.out);
    const V3 spot = illyria::game::toV3(e.pos) - out * 0.6f;
    V3 away = spot + out * 120.0f;
    away.z = b.heightAt(away.x, away.y);
    b.orderMove(away);
    run(b, 40);
    V3 onGround = spot;
    onGround.z = b.heightAt(spot.x, spot.y);  // the height a tap on the wall gives
    b.orderMove(onGround);
    b.advance(0.05);
    const std::vector<Battle::OrderMark> marks = slots(b);
    CHECK(!marks.empty());
    for (const Battle::OrderMark& m : marks) {
        // Where he will land: the walk if his place is on it, else the ground.
        Fixed deck;
        const float ground = b.heightAt(m.pos.x, m.pos.y);
        const bool decked = b.region().deckAt(illyria::game::toFixed(m.pos.x), illyria::game::toFixed(m.pos.y), deck);
        CHECK_NEAR(m.pos.z, decked ? std::max(static_cast<float>(deck.toDouble()), ground) : ground, 0.02);
    }
    CHECK(std::any_of(marks.begin(), marks.end(), [&](const Battle::OrderMark& m) { return m.pos.z > b.heightAt(m.pos.x, m.pos.y) + 3; }));
    // And there he lands, his ring with him.
    run(b, 40);
    const EntityId id = b.squads()[static_cast<size_t>(griffins)].members.front();
    const V3 p = b.unitPosition(id - 1);
    std::printf("  the griffin stands %.2f m above the ground, %.1f m from the spot\n", p.z - b.heightAt(p.x, p.y), std::hypot(p.x - spot.x, p.y - spot.y));
    CHECK(p.z > b.heightAt(p.x, p.y) + 3);
    CHECK_NEAR(b.floorAt(p), p.z, 0.02);
}

TEST(a_gate_of_ours_is_swung_by_a_tap_the_key_and_let_go) {
    Battle b(1, illyria::game::Scenario::Siege);
    const auto& g = b.region().gates().front();
    const V3 mid = (illyria::game::toV3(g.a) + illyria::game::toV3(g.b)) * 0.5f + V3{0, 0, 2};
    illyria::game::Camera cam;
    cam.setBounds(Battle::kWorldSize);
    cam.setViewport(800, 600);
    cam.setHeightFn([&b](float x, float y) { return b.heightAt(x, y); });
    cam.lookAt(mid.x, mid.y, 40);
    float px, py;
    CHECK(cam.project(mid, px, py));
    b.clearSelection();
    // Not ours: looked at, and nothing of how it stands is told.
    b.tap(cam, px, py, 1.0f);
    CHECK(b.inspectedGate() == 0);
    CHECK(!b.glance().ours);
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::AttackObject || b.intentAt(cam, px, py, 1.0f) == Battle::Intent::None);
    const size_t before = b.world().gates().size();
    CHECK(before > 0);
    // Ours: a tap holds it open, another holds it shut, the key lets go; the cursor says so.
    b.giveGates(Battle::kPlayer);
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::GateSwing);
    b.tap(cam, px, py, 1.0f);
    run(b, 1.0);
    CHECK(b.world().gates()[0].pin == illyria::sim::Command::GatePin::Open);
    CHECK(b.world().gates()[0].mode == illyria::sim::GateState::Mode::Opening);
    CHECK(b.glance().ours && b.glance().opens && b.glance().pin == 1);
    run(b, 4.0);
    CHECK(b.world().gates()[0].mode == illyria::sim::GateState::Mode::Open);
    CHECK(b.glance().swing == 1000);
    b.tap(cam, px, py, 1.0f);
    run(b, 5.0);
    CHECK(b.world().gates()[0].pin == illyria::sim::Command::GatePin::Shut);
    CHECK(b.world().gates()[0].mode == illyria::sim::GateState::Mode::Shut);
    b.swingGateByKey(true);
    run(b, 0.2);
    CHECK(b.world().gates()[0].pin == illyria::sim::Command::GatePin::Auto);
    // The key with nothing looked at: a gate of ours, the one nearest the men.
    b.clearSelection();
    b.tap(cam, px + 300, py, 1.0f);
    CHECK(b.inspectedGate() == -1);
    b.swingGateByKey(false);
    run(b, 0.2);
    int held = 0;
    for (const auto& gate : b.world().gates()) held += gate.pin == illyria::sim::Command::GatePin::Open;
    CHECK(held == 1);
}
