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
    // Missile troops loose: nobody within 1.4 gaps of anyone (one blast, one man), the line's men one apart.
    CHECK(minGap(formationLayout(Formation::Loose, 12, spacing)) >= spacing * 1.4f);
    // The line: one rank, twice as wide as the double line's two.
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
    CHECK(ranks(formationLayout(Formation::Line, 12, spacing)) == 1);
    CHECK(ranks(formationLayout(Formation::DoubleLine, 12, spacing)) == 2);
    CHECK_NEAR(width(formationLayout(Formation::Line, 12, spacing)), 22.0, 0.01);
    CHECK_NEAR(width(formationLayout(Formation::DoubleLine, 12, spacing)), 10.0, 0.01);
    // The box: hollow, everyone on its edge (none inside), a man at each corner, all facing out.
    for (int n : {12, 14, 16}) {
        const std::vector<FormationSlot> s = formationLayout(Formation::Box, n, spacing);
        float half = 0;
        for (const FormationSlot& p : s) half = std::max(half, std::max(std::abs(p.pos.x), std::abs(p.pos.y)));
        int corners = 0;
        for (const FormationSlot& p : s) {
            const bool onX = std::abs(std::abs(p.pos.x) - half) < 0.6f, onY = std::abs(std::abs(p.pos.y) - half) < 0.6f;
            CHECK(onX || onY);
            CHECK(p.face.dot(V3{p.pos.x, p.pos.y, 0}) > 0);
            corners += onX && onY;
        }
        CHECK(corners >= 3);
    }
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
    for (Formation f : {Formation::Line, Formation::DoubleLine, Formation::Wedge, Formation::Loose, Formation::Vee})
        for (const FormationSlot& p : formationLayout(f, 9, spacing)) CHECK(p.face.y == 1.0f);
}

TEST(formation_spacing_respects_size_and_tight_formations_march_slower) {
    for (int t = 0; t < illyria::sim::kUnitTypeCount; ++t) {
        const UnitType type = static_cast<UnitType>(t);
        CHECK(formationSpacing(type) >= 2.0f * static_cast<float>(illyria::sim::unitStats(type).radius.toDouble()));
    }
    CHECK(formationSpacing(UnitType::Elephants) > formationSpacing(UnitType::LightInfantry));
    CHECK(formationPace(Formation::Box) < formationPace(Formation::Line));
    CHECK(formationPace(Formation::Line) == formationPace(Formation::DoubleLine));
    CHECK(formationPace(Formation::DoubleLine) < formationPace(Formation::Loose));

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
        if (role == Role::Ranged || role == Role::Bomber) CHECK(s.formation == Formation::Loose);
        if (s.type == UnitType::LightInfantry || s.type == UnitType::Undead || s.type == UnitType::HeavyInfantry) CHECK(s.formation == Formation::DoubleLine);
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
    b.setFormation(Formation::Line);
    b.assignGroup(7);
    CHECK(b.groupSelection(7) == 2);
    b.clearSelection();
    CHECK(b.recallGroup(1));
    CHECK(b.selectedSquad() == squadNamed(b, "Lord Aldric"));
    CHECK(b.recallGroup(7));
    CHECK(b.selectedSquad() == squadNamed(b, "Ferry Pikes"));
    CHECK(b.selectionFormation() == Formation::Line);
    CHECK(!b.recallGroup(4));
    b.clearGroup(7);
    CHECK(!b.recallGroup(7));
    b.selectHeroes();
    CHECK(b.selectedSquad() == squadNamed(b, "Lord Aldric"));
    // Ungroup takes only the groups wholly selected: the hero's, not the pikes'.
    b.selectSquad(squadNamed(b, "Ferry Pikes"));
    b.assignGroup(7);
    b.clearSelection();
    CHECK(b.ungroupSelection() == 0);
    b.selectHeroes();
    CHECK(b.ungroupSelection() == 1);
    CHECK(!b.recallGroup(1));
    CHECK(b.recallGroup(7));
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
    CHECK(std::hypot(c.x - to.x, c.y - to.y) < 4.0f);  // (the ground there rises a few metres)
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
    // Pressed where they are to go and dragged south: the ghost of them stands
    // there and turns to face south; before the drag, as they face now.
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    V3 at = b.squadCentre(b.squads()[static_cast<size_t>(watch)]) + V3{-30, 0, 0};
    at.z = b.heightAt(at.x, at.y);
    const Camera c = cameraOn(b, at, 120);
    float px, py, qx, qy;
    CHECK(c.project(at, px, py));
    V3 tip = at + V3{0, -15, 0};
    tip.z = b.heightAt(tip.x, tip.y);
    CHECK(c.project(tip, qx, qy));
    CHECK(b.pressBegin(c, px, py, 1.0f, false));
    CHECK(b.drag().mode == Battle::Drag::Mode::Facing);
    CHECK(b.drag().ghost.size() == b.selectedAlive().size());
    CHECK(b.drag().facing.dot(V3{1, 0, 0}) > 0.99f);
    b.pressMove(c, qx, qy);
    CHECK(b.drag().aimed);
    CHECK(b.drag().facing.dot(V3{0, -1, 0}) > 0.95f);
    V3 middle;
    for (const FormationSlot& s : b.drag().ghost) {
        CHECK(s.face.dot(V3{0, -1, 0}) > 0.95f);
        middle += s.pos;
    }
    middle = middle * (1.0f / static_cast<float>(b.drag().ghost.size()));
    CHECK(std::hypot(middle.x - at.x, middle.y - at.y) < 1.5f);
    b.pressEnd(c, qx, qy, false);
    run(b, 45);
    for (illyria::sim::EntityId id : b.selectedAlive()) CHECK(facingOf(b, id).dot(V3{0, -1, 0}) > 0.95f);
}

namespace {

// The Grey Watch (knights) and the Silverwood Archers some way behind them, selected together.
std::vector<illyria::sim::EntityId> knightsAndBows(const Battle& b) {
    std::vector<illyria::sim::EntityId> all = b.squads()[static_cast<size_t>(squadNamed(b, "The Grey Watch"))].members;
    const auto& bows = b.squads()[static_cast<size_t>(squadNamed(b, "Silverwood Archers"))].members;
    all.insert(all.end(), bows.begin(), bows.end());
    return all;
}

V3 middleOf(const Battle& b, const std::vector<illyria::sim::EntityId>& ids) {
    V3 sum;
    for (illyria::sim::EntityId id : ids) sum += toV3(b.world().units()[id - 1].pos);
    return sum * (1.0f / static_cast<float>(ids.size()));
}

// Where the archers stand from the knights.
V3 bowsFromKnights(const Battle& b) {
    const Squad& watch = b.squads()[static_cast<size_t>(squadNamed(b, "The Grey Watch"))];
    const Squad& bows = b.squads()[static_cast<size_t>(squadNamed(b, "Silverwood Archers"))];
    const V3 d = b.squadCentre(bows, false) - b.squadCentre(watch, false);
    return {d.x, d.y, 0};
}

// Every one of these places has a man within `slack` of it, the arrangement moved to `at`.
bool standsAs(const Battle& b, const std::vector<illyria::sim::EntityId>& ids, const std::vector<V3>& places, const V3& at, float slack) {
    for (const V3& p : places) {
        bool found = false;
        for (illyria::sim::EntityId id : ids) {
            const V3 q = toV3(b.world().units()[id - 1].pos) - at;
            found = found || V3{q.x - p.x, q.y - p.y, 0}.length() < slack;
        }
        if (!found) return false;
    }
    return true;
}

std::vector<V3> placesFrom(const Battle& b, const std::vector<illyria::sim::EntityId>& ids, const V3& middle) {
    std::vector<V3> out;
    for (illyria::sim::EntityId id : ids) out.push_back(toV3(b.world().units()[id - 1].pos) - middle);
    return out;
}

}  // namespace

TEST(pressed_on_the_selected_they_turn_where_they_stand) {
    // A hold on one of the selected and a drag south: not a box, but the
    // facing line from their middle; they turn there and stay put.
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    const std::vector<illyria::sim::EntityId> men = b.selectedAlive();
    const V3 middle = middleOf(b, men);
    V3 on = toV3(b.world().units()[men[3] - 1].pos) + V3{0, 0, 0.9f};
    const Camera c = cameraOn(b, middle, 60);
    float px, py, qx, qy;
    CHECK(c.project(on, px, py));
    V3 south = middle + V3{0, -12, 0};
    south.z = b.heightAt(south.x, south.y);
    CHECK(c.project(south, qx, qy));
    CHECK(b.pressBegin(c, px, py, 1.0f, false));
    CHECK(b.drag().mode == Battle::Drag::Mode::Facing);
    CHECK(std::hypot(b.drag().anchor.x - middle.x, b.drag().anchor.y - middle.y) < 0.2f);
    b.pressMove(c, qx, qy);
    CHECK(b.drag().aimed && b.drag().facing.dot(V3{0, -1, 0}) > 0.9f);
    b.pressEnd(c, qx, qy, false);
    run(b, 30);
    CHECK(std::hypot(middleOf(b, men).x - middle.x, middleOf(b, men).y - middle.y) < 2.0f);
    for (illyria::sim::EntityId id : men) CHECK(facingOf(b, id).dot(V3{0, -1, 0}) > 0.95f);
    // On one of ours not selected: a box, as ever.
    b.selectSquad(squadNamed(b, "Ferry Pikes"));
    CHECK(b.pressBegin(c, px, py, 1.0f, false));
    CHECK(b.drag().mode == Battle::Drag::Mode::Box);
    b.pressEnd(c, px, py, true);
}

TEST(a_selection_marches_as_it_stands) {
    // Tapped somewhere else, knights and archers keep their places and their
    // front; given a new front, the whole of it turns, the archers still behind.
    // (Facing is checked on the knights: archers turn to shoot whoever comes.)
    Battle b(42);
    const std::vector<illyria::sim::EntityId> all = knightsAndBows(b);
    const std::vector<illyria::sim::EntityId>& knights = b.squads()[static_cast<size_t>(squadNamed(b, "The Grey Watch"))].members;
    b.select(all);
    CHECK(b.keepingShape());
    const V3 gap = bowsFromKnights(b);
    CHECK(gap.x < -10);  // behind them, to the west
    const V3 to = middleOf(b, all) + V3{-30, 15, 0};
    b.orderMove(to);
    run(b, 45);
    CHECK(std::hypot(middleOf(b, all).x - to.x, middleOf(b, all).y - to.y) < 3.0f);  // (on rising ground)
    CHECK((bowsFromKnights(b) - gap).length() < 2.0f);
    for (illyria::sim::EntityId id : knights) CHECK(facingOf(b, id).dot(V3{1, 0, 0}) > 0.95f);
    // Turned to face north: the archers end up south of the knights.
    const V3 to2 = to + V3{0, -20, 0};
    b.orderMoveFacing(to2, V3{0, 1, 0});
    run(b, 45);
    const V3 turned{-gap.y, gap.x, 0};
    CHECK((bowsFromKnights(b) - turned).length() < 2.5f);
    for (illyria::sim::EntityId id : knights) CHECK(facingOf(b, id).dot(V3{0, 1, 0}) > 0.95f);
}

TEST(dragged_to_face_a_way_every_one_of_them_faces_it) {
    // Knights who have been facing every which way (each walked off on his
    // own, as men turn to whatever is near): pressed and dragged north, they
    // all end up facing north, not each turned from where he faced.
    Battle b(42);
    b.selectSquad(squadNamed(b, "The Grey Watch"));
    const std::vector<illyria::sim::EntityId> men = b.selectedAlive();
    const V3 start = middleOf(b, men);
    for (size_t i = 0; i < men.size(); ++i) {
        const float a = static_cast<float>(i) * 2.1f;
        b.testMove(men[i], toV3(b.world().units()[men[i] - 1].pos) + V3{std::cos(a) * 3.0f, std::sin(a) * 3.0f, 0});
    }
    run(b, 6);
    float spread = 0;
    for (illyria::sim::EntityId id : men) spread = std::max(spread, 1.0f - facingOf(b, id).dot(facingOf(b, men.front())));
    CHECK(spread > 0.5f);  // they did face different ways
    b.selectSquad(squadNamed(b, "The Grey Watch"));
    b.orderMoveFacing(start + V3{-25, 10, 0}, V3{0, 1, 0});
    run(b, 45);
    for (illyria::sim::EntityId id : men) CHECK(facingOf(b, id).dot(V3{0, 1, 0}) > 0.95f);
}

TEST(a_shape_is_where_they_were_sent_not_where_the_march_has_them) {
    // Sent off, then sent elsewhere before they got there: they form as they
    // stood, not strung out as the march had them.
    Battle b(42);
    b.selectSquad(squadNamed(b, "The Grey Watch"));
    const std::vector<illyria::sim::EntityId> men = b.selectedAlive();
    const V3 start = middleOf(b, men);
    const std::vector<V3> places = placesFrom(b, men, start);
    b.orderMove(start + V3{-60, 0, 0});
    run(b, 4);
    // Somewhere about 40 m off with no tree, bush or rock in the way of the
    // shape (any would push a file out of its place).
    V3 to = start + V3{-30, 25, 0};
    bool found = false;
    for (int dx = -30; dx >= -50 && !found; dx -= 10)
        for (int dy = 25; dy <= 45 && !found; dy += 5)
            for (int sgn = 1; sgn >= -1 && !found; sgn -= 2) {
                const V3 at = start + V3{static_cast<float>(dx), static_cast<float>(dy * sgn), 0};
                bool clear = true;
                for (const illyria::sim::Obstacle& o : b.region().obstacles())
                    clear = clear && std::hypot(static_cast<float>(o.pos.x.toDouble()) - at.x, static_cast<float>(o.pos.y.toDouble()) - at.y) > 7.0f;
                if (clear) {
                    to = at;
                    found = true;
                }
            }
    b.orderMove(to);
    run(b, 60);  // (up the bank, 1 in 6)
    // Each place has a man within a waypoint's reach of it (1.5 m: the
    // jostle on arrival can leave one a little off); strung out would be metres.
    CHECK(standsAs(b, men, places, to, 1.5f));
}

TEST(a_saved_group_marches_in_its_saved_shape) {
    // Saved with the archers behind the knights; the archers sent off on their
    // own; the group recalled marches as it was saved.
    Battle b(42);
    const std::vector<illyria::sim::EntityId> all = knightsAndBows(b);
    b.select(all);
    const V3 gap = bowsFromKnights(b);
    b.assignGroup(3);
    b.selectSquad(squadNamed(b, "Silverwood Archers"));
    b.orderMove(b.squadCentre(b.squads()[static_cast<size_t>(squadNamed(b, "Silverwood Archers"))], false) + V3{0, 30, 0});
    run(b, 30);
    CHECK((bowsFromKnights(b) - gap).length() > 20.0f);
    CHECK(b.recallGroup(3));
    CHECK(b.keepingShape());
    const V3 to = middleOf(b, all) + V3{-25, 0, 0};
    b.orderMove(to);
    run(b, 50);
    CHECK((bowsFromKnights(b) - gap).length() < 2.0f);
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
    b.selectSquad(squadNamed(b, "Iron Hill Sappers"));
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

TEST(a_right_click_gives_the_tap_s_order_and_never_selects) {
    // The gesture: a right click that does not drag, not a right drag (that turns the view).
    {
        Camera c;
        c.setBounds(1024);
        c.setViewport(800, 600);
        c.lookAt(512, 512, 200);
        int taps = 0, orders = 0;
        Gestures g(c, [&](float, float) { ++taps; });
        g.setSecondaryTap([&](float, float) { ++orders; });
        g.pointer(PointerEvent::Down, 1, 300, 300, 2, 0);
        g.pointer(PointerEvent::Up, 1, 302, 300, 2, 120);
        CHECK(orders == 1 && taps == 0);
        const float yaw = c.yaw();
        g.pointer(PointerEvent::Down, 1, 300, 300, 2, 1000);
        g.pointer(PointerEvent::Move, 1, 360, 300, 2, 1050);
        g.pointer(PointerEvent::Up, 1, 360, 300, 2, 1100);
        CHECK(orders == 1);
        CHECK(std::abs(c.yaw() - yaw) > 0.3f);
        g.pointer(PointerEvent::Down, 1, 300, 300, 1, 2000);  // the middle button turns, and never orders
        g.pointer(PointerEvent::Up, 1, 300, 300, 1, 2050);
        g.pointer(PointerEvent::Down, 1, 300, 300, 0, 3000);
        g.pointer(PointerEvent::Up, 1, 300, 300, 0, 3050);
        CHECK(orders == 1 && taps == 1);
    }
    // On the battlefield, wired as the page wires it.
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    const std::vector<illyria::sim::EntityId> chosen = b.selectedAlive();
    const V3 home = b.squadCentre(b.squads()[static_cast<size_t>(watch)]);
    Camera cam = cameraOn(b, home, 120);
    Gestures g(cam, [&](float x, float y) { b.tap(cam, x, y, 1.0f); });
    g.setSecondaryTap([&](float x, float y) { b.orderTap(cam, x, y, 1.0f); });
    double t = 0;
    const auto rightClick = [&](float x, float y) {
        g.pointer(PointerEvent::Down, 1, x, y, 2, t += 1000);
        g.pointer(PointerEvent::Up, 1, x, y, 2, t += 100);
        b.advance(0.05);
    };
    const auto meanTarget = [&] {
        V3 sum;
        for (illyria::sim::EntityId id : chosen) {
            CHECK(b.world().units()[id - 1].hasMoveTarget);
            sum += toV3(b.world().units()[id - 1].moveTarget);
        }
        return sum * (1.0f / static_cast<float>(chosen.size()));
    };
    const auto flatGap = [](const V3& a, const V3& c) { return V3{a.x - c.x, a.y - c.y, 0}.length(); };
    // The ground: they march there.
    V3 spot = home + V3{-30, 0, 0};
    spot.z = b.heightAt(spot.x, spot.y);
    float px, py;
    CHECK(cam.project(spot, px, py));
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::Move);
    rightClick(px, py);
    CHECK(b.selectedAlive() == chosen);
    CHECK(flatGap(meanTarget(), spot) < 3.0f);
    // One of ours not selected: a left tap would pick him; a right click marches them to him.
    const int pikes = squadNamed(b, "Ferry Pikes");
    const illyria::sim::EntityId pikeman = b.squads()[static_cast<size_t>(pikes)].members.front();
    const V3 pp = b.unitPosition(pikeman - 1);
    cam.lookAt(pp.x, pp.y, 60);
    CHECK(cam.project(pp + V3{0, 0, 0.9f}, px, py));
    CHECK(b.pickAt(cam, px, py, 1.0f).kind == Battle::Pick::Kind::Own);
    rightClick(px, py);
    CHECK(b.selectedAlive() == chosen);
    CHECK(!b.isSelected(pikeman));
    CHECK(flatGap(meanTarget(), pp) < 4.0f);
    // An enemy: they go after him, and still nobody else is selected.
    illyria::sim::EntityId foe = illyria::sim::kNoEntity;
    float best = 1e9f;
    for (const illyria::sim::Unit& u : b.world().units()) {
        if (u.owner != Battle::kEnemy || !u.alive()) continue;
        const float d = (toV3(u.pos) - home).length();
        if (d < best) best = d, foe = u.id;
    }
    const V3 fp = b.unitPosition(foe - 1);
    cam.lookAt(fp.x, fp.y, 60);
    CHECK(cam.project(fp + V3{0, 0, 0.9f}, px, py));
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::Attack);
    rightClick(px, py);
    CHECK(b.selectedAlive() == chosen);
    for (illyria::sim::EntityId id : chosen) CHECK(b.world().units()[id - 1].targetUnit == foe);
    // Nothing selected: a right click on one of ours selects nobody.
    b.clearSelection();
    cam.lookAt(pp.x, pp.y, 60);
    CHECK(cam.project(b.unitPosition(pikeman - 1) + V3{0, 0, 0.9f}, px, py));
    rightClick(px, py);
    CHECK(b.selectedAlive().empty());
}

TEST(trackpad_swipes_pan_pinches_zoom_and_keys_move_and_turn_the_view) {
    constexpr float kNorth = 1.5707963f;
    Camera c;
    c.setBounds(4096);
    c.setViewport(800, 600);
    c.lookAt(2048, 2048, 200);
    c.rotate(kNorth - c.yaw());
    Gestures g(c, [](float, float) {});
    // A two-finger swipe up (natural scrolling: dy > 0) carries the land up, as a drag would: the view goes south.
    const V3 at = c.target();
    g.scroll(0, 100, 400, 300);
    CHECK(c.target().y < at.y - 10);
    CHECK_NEAR(c.target().x, at.x, 0.01);
    Camera dragged = c;
    dragged.lookAt(at.x, at.y, 200);
    dragged.dragGround(400, 300, 400, 200);
    CHECK_NEAR(c.target().y, dragged.target().y, 0.01);
    g.scroll(100, 0, 400, 300);  // and to the left: the view goes east
    CHECK(c.target().x > at.x + 10);
    // A pinch zooms about the cursor; a twist turns.
    const float d = c.distance();
    g.pinch(2.0f, 0, 400, 300);
    CHECK_NEAR(c.distance(), d / 2, 0.01);
    g.pinch(1.0f, 0.25f, 400, 300);
    CHECK_NEAR(c.yaw(), kNorth + 0.25f, 1e-4);
    // Arrows: up the screen (north here), at a pace that grows with the distance; they stop when let go.
    c.rotate(kNorth - c.yaw());
    const auto hold4 = [&](int right, int forward, int turn, int zoom, double fromMs, double ms) {
        g.update(fromMs);
        g.setKeys(right, forward, turn, zoom);
        for (double t = fromMs + 16; t <= fromMs + ms; t += 16) g.update(t);
        g.setKeys(0, 0, 0, 0);
        for (double t = fromMs + ms + 16; t <= fromMs + ms + 400; t += 16) g.update(t);
    };
    const auto hold = [&](int right, int forward, int turn, double fromMs, double ms) { hold4(right, forward, turn, 0, fromMs, ms); };
    V3 before = c.target();
    hold(0, 1, 0, 0, 1000);
    const float first = c.target().y - before.y;
    CHECK(first > c.distance() * 0.7f && first < c.distance() * 1.0f);
    CHECK_NEAR(c.target().x, before.x, 0.01);
    c.lookAt(c.target().x, c.target().y, c.distance() * 2);
    before = c.target();
    hold(0, 1, 0, 2000, 1000);
    CHECK_NEAR((c.target().y - before.y) / first, 2.0, 0.05);  // twice as far out, twice the pace
    before = c.target();
    hold(1, 0, 0, 4000, 500);
    CHECK(c.target().x > before.x + 50);  // right: east, looking north
    // Shift + left (Q): the view turns left (anticlockwise).
    float yaw = c.yaw();
    hold(0, 0, 1, 6000, 500);
    CHECK(c.yaw() > yaw + 0.5f);
    // R held: nearer, about the middle of the screen (where it looks stays put); F: back out.
    const float far = c.distance();
    const V3 looking = c.target();
    hold4(0, 0, 0, 1, 7000, 580);
    CHECK(c.distance() < far * 0.6f && c.distance() > far * 0.35f);
    CHECK(std::hypot(c.target().x - looking.x, c.target().y - looking.y) < 0.05f * far);
    const float near = c.distance();
    hold4(0, 0, 0, -1, 8100, 580);
    CHECK(c.distance() > near * 1.6f);
    // N: back to north over a moment, the short way round; turning by hand stops it.
    c.rotate(2 * 3.14159265f * 3 + 0.4f);
    g.turnTo(kNorth);
    CHECK(g.turning());
    for (double t = 9200; t < 9800; t += 16) g.update(t);
    CHECK(!g.turning());
    CHECK_NEAR(std::remainder(c.yaw() - kNorth, 2 * 3.14159265f), 0.0, 0.003);
    c.rotate(1.0f);
    g.turnTo(kNorth);
    g.update(10000);
    g.update(10016);
    g.pointer(PointerEvent::Down, 1, 300, 300, 2, 10020);
    g.pointer(PointerEvent::Move, 1, 340, 300, 2, 10030);
    g.pointer(PointerEvent::Up, 1, 340, 300, 2, 10040);
    CHECK(!g.turning());
}
