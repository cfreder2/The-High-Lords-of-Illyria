// Commanding the battle: formations, selection, groups, moving with a
// facing, scattering, and that it all reaches the simulation as Commands.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/game/gestures.h"
#include "test.h"

using namespace illyria::game;
using illyria::sim::UnitType;

namespace {

float minGap(const std::vector<FormationSlot>& s) {
    float best = 1e9f;
    for (size_t i = 0; i < s.size(); ++i)
        for (size_t j = i + 1; j < s.size(); ++j) best = std::min(best, (s[i].pos - s[j].pos).length());
    return best;
}

int squadNamed(const Battle& b, const char* name) {
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].name == name) return static_cast<int>(i);
    return -1;
}

Camera cameraOn(const Battle& b, const V3& at, float distance) {
    Camera c;
    c.setBounds(Battle::kWorldSize);
    c.setViewport(800, 600);
    c.setHeightFn([&b](float x, float y) { return b.heightAt(x, y); });
    c.lookAt(at.x, at.y, distance);
    return c;
}

void run(Battle& b, double seconds) {
    for (double t = 0; t < seconds; t += 0.1) b.advance(0.1);
}

V3 facingOf(const Battle& b, illyria::sim::EntityId id) { return toV3(b.world().units()[id - 1].facing); }

}  // namespace

TEST(every_formation_has_a_place_for_each_and_keeps_its_gaps) {
    const float spacing = 2.0f;
    for (int k = 0; k < kFormationCount; ++k) {
        const Formation f = kFormationOrder[k];
        for (int n : {1, 2, 7, 12, 16}) {
            const std::vector<FormationSlot> s = formationLayout(f, n, spacing);
            CHECK(static_cast<int>(s.size()) == n);
            if (n > 1 && minGap(s) < spacing * 0.99f) std::printf("  %s x%d: gap %.2f\n", formationName(f), n, minGap(s));
            if (n > 1) CHECK(minGap(s) >= spacing * 0.99f);
            CHECK(formationSlots(f, n, spacing).size() == s.size());
        }
        CHECK(formationName(f)[0] != '?');
    }
    // Missile troops' checkerboard: nobody within 1.8 gaps of anyone (one blast, one man).
    CHECK(minGap(formationLayout(Formation::Staggered, 12, spacing)) >= spacing * 1.79f);
    {
        // Each man of the back rank stands in a gap of the front rank.
        const std::vector<FormationSlot> s = formationLayout(Formation::Staggered, 12, spacing);
        std::vector<float> front, back;
        for (const FormationSlot& p : s) (p.pos.y > 0 ? front : back).push_back(p.pos.x);
        CHECK(front.size() == 6 && back.size() == 6);
        for (float x : back) {
            float nearest = 1e9f;
            for (float fx : front) nearest = std::min(nearest, std::abs(fx - x));
            CHECK_NEAR(nearest, spacing, 0.01);  // half of the 2-gap interval
        }
    }
    // The short line: three ranks, narrower than the two-rank line.
    const auto width = [](const std::vector<FormationSlot>& s) {
        float lo = 1e9f, hi = -1e9f;
        for (const FormationSlot& p : s) lo = std::min(lo, p.pos.x), hi = std::max(hi, p.pos.x);
        return hi - lo;
    };
    const auto ranks = [](const std::vector<FormationSlot>& s) {
        std::vector<float> ys;
        for (const FormationSlot& p : s)
            if (std::none_of(ys.begin(), ys.end(), [&](float y) { return std::abs(y - p.pos.y) < 0.01f; })) ys.push_back(p.pos.y);
        return ys.size();
    };
    CHECK(ranks(formationLayout(Formation::ShortLine, 12, spacing)) == 3);
    CHECK(ranks(formationLayout(Formation::Line, 12, spacing)) == 2);
    CHECK(width(formationLayout(Formation::ShortLine, 12, spacing)) < width(formationLayout(Formation::Line, 12, spacing)));
    // Encirclement: a wide crescent around the point, every man facing out.
    {
        const std::vector<FormationSlot> s = formationLayout(Formation::Encircle, 16, spacing);
        float lo = 1e9f, hi = -1e9f;
        for (const FormationSlot& p : s) {
            const float r = p.pos.length();
            CHECK(r >= spacing * 2.5f - 0.01f);
            CHECK_NEAR(p.face.length(), 1.0, 1e-4);
            CHECK(p.pos.normalized().dot(p.face) > 0.999f);
            const float a = std::atan2(p.pos.x, p.pos.y);
            lo = std::min(lo, a), hi = std::max(hi, a);
        }
        CHECK(hi - lo > 3.5f);  // over 200 degrees of arc
        CHECK(hi - lo < 6.0f);  // but open behind: a crescent, not a ring
    }
    // Everyone else faces the front.
    for (const FormationSlot& p : formationLayout(Formation::Box, 9, spacing)) CHECK(p.face.y == 1.0f);
}

TEST(formation_spacing_respects_size_and_tight_formations_march_slower) {
    for (int t = 0; t < illyria::sim::kUnitTypeCount; ++t) {
        const UnitType type = static_cast<UnitType>(t);
        CHECK(formationSpacing(type) >= 2.0f * static_cast<float>(illyria::sim::unitStats(type).radius.toDouble()));
    }
    CHECK(formationSpacing(UnitType::Elephants) > formationSpacing(UnitType::LightInfantry));
    CHECK(formationPace(Formation::Box) < formationPace(Formation::Line));
    CHECK(formationPace(Formation::Line) < formationPace(Formation::Staggered));
    CHECK(formationPace(Formation::Staggered) < formationPace(Formation::Loose));

    // The same squad marching the same way: slower in a box than loose.
    const auto pace = [](Formation f) {
        Battle b(7);
        b.selectSquad(squadNamed(b, "The Grey Watch"));
        b.setFormation(f);
        run(b, 30);
        b.orderMove(b.squadCentre(b.squads()[static_cast<size_t>(b.selectedSquad())], false) + V3{-40, 0, 0});
        b.advance(0.05);
        float fastest = 0;
        for (illyria::sim::EntityId id : b.selectedAlive())
            fastest = std::max(fastest, static_cast<float>(b.world().units()[id - 1].marchSpeed.toDouble()));
        return fastest;
    };
    CHECK(pace(Formation::Box) < pace(Formation::Loose) * 0.9f);
}

TEST(the_computer_forms_sensibly) {
    const Battle b(3);
    for (const Squad& s : b.squads()) {
        const Role role = roleOf(s.type);
        if (role == Role::Ranged || role == Role::Bomber) CHECK(s.formation == Formation::Staggered);
        if (s.type == UnitType::LightInfantry || s.type == UnitType::Undead || s.type == UnitType::HeavyInfantry) CHECK(s.formation == Formation::Line);
        if (s.type == UnitType::WolfRiders || s.type == UnitType::LightCavalry) CHECK(s.formation == Formation::Wedge);
    }
}

TEST(tap_selects_one_soldier_then_every_one_of_his_type_in_view) {
    Battle b(42);
    const int bow1 = squadNamed(b, "1st Oakhaven Bowmen"), bow2 = squadNamed(b, "Silverwood Archers");
    CHECK(bow1 >= 0 && bow2 >= 0);
    const V3 mid = (b.squadCentre(b.squads()[static_cast<size_t>(bow1)]) + b.squadCentre(b.squads()[static_cast<size_t>(bow2)])) * 0.5f;
    const Camera c = cameraOn(b, mid, 120);
    // One archer.
    const illyria::sim::EntityId archer = b.squads()[static_cast<size_t>(bow1)].members[3];
    float px, py;
    CHECK(c.project(b.unitPosition(archer - 1) + V3{0, 0, 0.9f}, px, py));
    b.tap(c, px, py, 1.0f);
    CHECK(b.selectedAlive().size() == 1);
    CHECK(b.isSelected(b.selectedAlive().front()));
    const illyria::sim::EntityId picked = b.selectedAlive().front();
    CHECK(b.world().units()[picked - 1].stats.type == static_cast<uint8_t>(UnitType::Archers));
    // The same one again: both archer squads, and nobody else.
    {
        b.tap(c, px, py, 1.0f);
        CHECK(b.selectedAlive().size() == b.squads()[static_cast<size_t>(bow1)].members.size() + b.squads()[static_cast<size_t>(bow2)].members.size());
        CHECK(b.squadSelection(b.squads()[static_cast<size_t>(bow1)]) == 2);
        CHECK(b.squadSelection(b.squads()[static_cast<size_t>(bow2)]) == 2);
        CHECK(b.selectedSquad() == -1);  // two squads
    }
    // At the logic level: of a type, only those in view.
    b.selectVisibleOfType(c, UnitType::Archers);
    CHECK(b.selectedAlive().size() == 24);
    const Camera away = cameraOn(b, mid + V3{0, 400, 0}, 60);
    b.selectVisibleOfType(away, UnitType::Archers);
    CHECK(b.selectedAlive().empty());
    // Shift-tap adds one, again takes him away.
    b.selectSquad(bow2);
    const size_t n = b.selectedAlive().size();
    b.tap(c, px, py, 1.0f, true);
    CHECK(b.selectedAlive().size() == n + 1);
    b.tap(c, px, py, 1.0f, true);
    CHECK(b.selectedAlive().size() == n);
}

TEST(box_select_takes_ours_in_the_box_and_no_enemies) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    const Camera c = cameraOn(b, b.squadCentre(b.squads()[static_cast<size_t>(watch)]), 300);
    b.selectInRect(c, 0, 0, 800, 600);
    CHECK(!b.selectedAlive().empty());
    for (illyria::sim::EntityId id : b.selectedAlive()) CHECK(b.world().units()[id - 1].owner == Battle::kPlayer);
    CHECK(b.squadSelection(b.squads()[static_cast<size_t>(watch)]) == 2);
    // A box around the Watch alone, by press and drag.
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (illyria::sim::EntityId id : b.squads()[static_cast<size_t>(watch)].members) {
        float px, py;
        c.project(b.unitPosition(id - 1) + V3{0, 0, 0.9f}, px, py);
        x0 = std::min(x0, px), y0 = std::min(y0, py), x1 = std::max(x1, px), y1 = std::max(y1, py);
    }
    b.clearSelection();
    CHECK(b.pressBegin(c, x0 - 2, y0 - 2, 1.0f, false));  // nothing selected: a box
    CHECK(b.drag().mode == Battle::Drag::Mode::Box);
    b.pressMove(c, x1 + 2, y1 + 2);
    b.pressEnd(c, x1 + 2, y1 + 2, false);
    CHECK(b.squadSelection(b.squads()[static_cast<size_t>(watch)]) == 2);
    CHECK(b.drag().mode == Battle::Drag::Mode::None);
}

TEST(groups_are_saved_and_recalled) {
    Battle b(42);
    b.selectSquad(squadNamed(b, "Lord Aldric"));
    CHECK(b.saveGroup() == 1);  // the first key along the keyboard
    b.selectSquad(squadNamed(b, "Ferry Pikes"));
    b.setFormation(Formation::ShortLine);
    b.assignGroup(7);
    CHECK(b.groupSelection(7) == 2);
    b.clearSelection();
    CHECK(b.recallGroup(1));
    CHECK(b.selectedSquad() == squadNamed(b, "Lord Aldric"));
    CHECK(b.recallGroup(7));
    CHECK(b.selectedSquad() == squadNamed(b, "Ferry Pikes"));
    CHECK(b.selectionFormation() == Formation::ShortLine);
    CHECK(!b.recallGroup(4));
    b.clearGroup(7);
    CHECK(!b.recallGroup(7));
    b.selectHeroes();
    CHECK(b.selectedSquad() == squadNamed(b, "Lord Aldric"));
}

TEST(a_move_with_facing_arrives_facing_that_way_laid_across_the_line) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    run(b, 1);
    // March west, away from the Red Hand, but face north on arrival.
    const V3 to = b.squadCentre(b.squads()[static_cast<size_t>(watch)], false) + V3{-25, 0, 0};
    const V3 north{0, 1, 0};
    b.orderMoveFacing(to, north);
    run(b, 45);
    float alongNorth = 0, alongEast = 0;
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(watch)], false);
    CHECK((c - to).length() < 4.0f);
    for (illyria::sim::EntityId id : b.selectedAlive()) {
        CHECK(facingOf(b, id).dot(north) > 0.98f);
        const V3 p = toV3(b.world().units()[id - 1].pos) - c;
        alongNorth = std::max(alongNorth, std::abs(p.y));
        alongEast = std::max(alongEast, std::abs(p.x));
    }
    // The line runs across the facing: wide east to west, shallow north to south.
    CHECK(alongEast > alongNorth * 2);
}

TEST(press_and_drag_draws_the_facing_line_with_its_ghost) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    const V3 at = b.squadCentre(b.squads()[static_cast<size_t>(watch)]) + V3{-30, 0, 0};
    const Camera c = cameraOn(b, at, 120);
    float px, py, qx, qy;
    CHECK(c.project(at, px, py));
    V3 tip = at + V3{0, -15, 0};
    tip.z = b.heightAt(tip.x, tip.y);
    CHECK(c.project(tip, qx, qy));
    CHECK(b.pressBegin(c, px, py, 1.0f, false));
    CHECK(b.drag().mode == Battle::Drag::Mode::Facing);
    CHECK(b.drag().ghost.size() == b.selectedAlive().size());
    b.pressMove(c, qx, qy);
    CHECK(b.drag().aimed);
    CHECK(b.drag().facing.dot(V3{0, -1, 0}) > 0.95f);
    for (const FormationSlot& s : b.drag().ghost) CHECK(s.face.dot(V3{0, -1, 0}) > 0.99f);
    b.pressEnd(c, qx, qy, false);
    run(b, 45);
    for (illyria::sim::EntityId id : b.selectedAlive()) CHECK(facingOf(b, id).dot(V3{0, -1, 0}) > 0.98f);
}

TEST(encircled_soldiers_face_outward_from_the_point) {
    Battle b(42);
    b.selectSquad(squadNamed(b, "Ferry Pikes"));
    b.setFormation(Formation::Encircle);
    const V3 point = b.squadCentre(b.squads()[static_cast<size_t>(b.selectedSquad())], false) + V3{-20, 0, 0};
    b.orderMoveFacing(point, V3{-1, 0, 0});
    run(b, 45);
    for (illyria::sim::EntityId id : b.selectedAlive()) {
        const V3 out = (toV3(b.world().units()[id - 1].pos) - point);
        CHECK(out.length() > 3.0f);
        CHECK(facingOf(b, id).dot(V3{out.x, out.y, 0}.normalized()) > 0.8f);
    }
}

TEST(scatter_spreads_them_outward) {
    Battle b(42);
    b.selectSquad(squadNamed(b, "Iron Hill Legion"));
    const std::vector<illyria::sim::EntityId> men = b.selectedAlive();
    const auto spread = [&] {
        V3 c;
        for (illyria::sim::EntityId id : men) c += toV3(b.world().units()[id - 1].pos);
        c = c * (1.0f / static_cast<float>(men.size()));
        float sum = 0;
        for (illyria::sim::EntityId id : men) sum += (toV3(b.world().units()[id - 1].pos) - c).length();
        return sum / static_cast<float>(men.size());
    };
    const float before = spread();
    b.scatter();
    run(b, 6);
    CHECK(spread() > before + 4.0f);
}

TEST(orders_are_lockstep_commands_and_replay_identically) {
    const auto play = [](uint64_t seed) {
        Battle b(seed);
        b.selectSquad(squadNamed(b, "The Grey Watch"));
        b.setFormation(Formation::Encircle);
        run(b, 2);
        b.orderMoveFacing(b.squadCentre(b.squads()[1], false) + V3{-20, 10, 0}, V3{0, 1, 0});
        run(b, 5);
        b.selectSquad(squadNamed(b, "Silverwood Archers"));
        b.scatter();
        run(b, 5);
        return b.world().checksum();
    };
    CHECK(play(5) == play(5));
    // And the sim itself honours a facing order, with it in its state.
    illyria::sim::WorldConfig cfg;
    cfg.seed = 1;
    illyria::sim::World w(cfg), plain(cfg);
    illyria::sim::EntityId id = 0;
    for (illyria::sim::World* x : {&w, &plain})
        id = x->spawnUnit(0, {illyria::sim::Fixed::fromInt(10), illyria::sim::Fixed::fromInt(10), {}},
                          {illyria::sim::Fixed::one(), {}, {}}, illyria::sim::unitStats(UnitType::LightInfantry));
    illyria::sim::Command go{0, id, illyria::sim::Command::Type::Move, {illyria::sim::Fixed::fromInt(20), illyria::sim::Fixed::fromInt(10), {}}};
    plain.step(std::vector<illyria::sim::Command>{go});
    go.face = {{}, -illyria::sim::Fixed::one(), {}};
    w.step(std::vector<illyria::sim::Command>{go});
    CHECK(w.checksum() != plain.checksum());
    for (int t = 0; t < 30 * illyria::sim::kTicksPerSecond; ++t) w.step({}), plain.step({});
    CHECK(w.units()[0].facing == (illyria::sim::Vec3{{}, -illyria::sim::Fixed::one(), {}}));
    CHECK(plain.units()[0].facing.x > illyria::sim::Fixed::zero());
}

TEST(gestures_hold_is_offered_and_a_refused_hold_still_taps) {
    Camera c;
    c.setBounds(1024);
    c.setViewport(800, 600);
    c.setHeightFn([](float, float) { return 0.0f; });
    c.lookAt(512, 512, 200);
    int taps = 0, begins = 0, moves = 0, ends = 0;
    bool take = true;
    Gestures g(c, [&](float, float) { ++taps; });
    g.setHold([&](float, float, bool) { ++begins; return take; }, [&](float, float) { ++moves; }, [&](float, float, bool) { ++ends; });
    const V3 target = c.target();
    g.pointer(PointerEvent::Down, 1, 300, 300, 0, 0);
    g.update(100);
    CHECK(begins == 0);
    g.update(Gestures::kHoldMs + 10);
    CHECK(begins == 1 && g.holding());
    g.pointer(PointerEvent::Move, 1, 360, 330, 0, 400);
    g.pointer(PointerEvent::Up, 1, 360, 330, 0, 450);
    CHECK(moves == 1 && ends == 1 && taps == 0);
    CHECK_NEAR(c.target().x, target.x, 1e-3);  // a hold does not pan
    // Not taken (the campaign map): a slow tap is still a tap.
    take = false;
    g.pointer(PointerEvent::Down, 1, 300, 300, 0, 1000);
    g.update(1000 + Gestures::kHoldMs + 10);
    g.pointer(PointerEvent::Up, 1, 301, 300, 0, 1400);
    CHECK(begins == 2 && taps == 1 && ends == 1);
    // Shift (desktop): a box at once.
    take = true;
    g.setShift(true);
    g.pointer(PointerEvent::Down, 1, 300, 300, 0, 2000);
    CHECK(begins == 3 && g.holding());
    g.pointer(PointerEvent::Down, 2, 400, 300, 0, 2010);  // a second finger cancels it
    CHECK(!g.holding() && ends == 2);
}
