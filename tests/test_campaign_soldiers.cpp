// The campaign's soldiers on the land (game/src/campaign_soldiers.cpp,
// engine/sim/src/campaign_world.cpp, docs/ONE_WORLD.md): a man for every soldier of
// every stack, placed from the campaign's state alone, along the stack's route and in
// its town's courtyard, and none of it written back into the simulation.
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "illyria/game/campaign_soldiers.h"
#include "test.h"

using namespace illyria::game;
using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// A campaign a few days old, the computer lords' stacks out on the march.
Campaign started(uint64_t seed, int days) {
    Campaign c(continent(), seed);
    c.setHuman(0, true);
    for (int t = 0; t < days * Campaign::kTicksPerDay; ++t) c.step();
    return c;
}
}  // namespace

TEST(every_living_soldier_of_every_stack_on_land_is_drawn_once_where_his_stack_is) {
    Campaign c = started(7, 3);
    std::vector<MapSoldier> men;
    collectMapSoldiers(c, 0, 0, men);
    size_t want = 0;
    for (const Campaign::Stack& s : c.stacks())
        if (!s.boat)
            for (const Army& a : s.armies) want += static_cast<size_t>(a.soldiers);
    size_t alive = 0;
    std::set<uint32_t> ids;
    for (const MapSoldier& m : men) {
        alive += m.alive;
        CHECK(ids.insert(m.id).second);  // each man once
    }
    CHECK(alive == want);
    CHECK(want > 1000);  // every town's garrison, and the lords' armies
    // Each stands near his stack's middle (a column of squads is a hundred metres at most).
    std::map<uint32_t, std::pair<double, double>> middle;
    std::map<uint32_t, int> count;
    for (const MapSoldier& m : men) {
        middle[m.stack].first += m.x, middle[m.stack].second += m.y, ++count[m.stack];
    }
    for (const MapSoldier& m : men) {
        const Campaign::Stack* st = c.findStack(m.stack);
        CHECK(st != nullptr);
        if (!st) continue;
        CHECK(m.owner == st->owner);
        const double dx = m.x - middle[m.stack].first / count[m.stack], dy = m.y - middle[m.stack].second / count[m.stack];
        CHECK(std::hypot(dx, dy) < 150.0);
    }
}

TEST(laying_out_the_soldiers_changes_nothing_in_the_campaign) {
    Campaign c = started(11, 2);
    const uint64_t before = c.checksum();
    std::vector<MapSoldier> men;
    for (int k = 0; k < 5; ++k) collectMapSoldiers(c, 50.0 * k, 1, men);
    CHECK(c.checksum() == before);
    // Nor does it depend on anything but the campaign: a man's place is the same on every call.
    std::vector<MapSoldier> again;
    collectMapSoldiers(c, 25.0, 1, men);
    collectMapSoldiers(c, 25.0, 1, again);
    CHECK(men.size() == again.size());
    for (size_t i = 0; i < men.size() && i < again.size(); ++i) CHECK(men[i].x == again[i].x && men[i].y == again[i].y && men[i].id == again[i].id);
}

TEST(a_stack_in_a_town_stands_in_its_courtyard_and_a_marching_one_between_its_ends) {
    Campaign c = started(13, 3);
    int garrisons = 0, marching = 0;
    std::vector<MapSoldier> men;
    collectMapSoldiers(c, 0, 0, men);
    for (const Campaign::Stack& s : c.stacks()) {
        if (s.armies.empty() || s.boat || s.awake) continue;
        double sx = 0, sy = 0;
        int n = 0;
        for (const MapSoldier& m : men)
            if (m.stack == s.id) sx += m.x, sy += m.y, ++n;
        if (n == 0) continue;
        sx /= n, sy /= n;
        const int32_t town = c.cityAt(s.cx, s.cy);
        if (s.path.empty() && town >= 0) {
            const Continent::Castle& k = c.continent().cities()[static_cast<size_t>(town)].castle;
            // (a block of squads round the courtyard's middle; several stacks stand side by side)
            CHECK(std::hypot(sx - k.courtyard.x.toDouble(), sy - k.courtyard.y.toDouble()) < 40.0);
            for (const MapSoldier& m : men)
                if (m.stack == s.id) CHECK(m.speed == 0);
            ++garrisons;
        }
        if (!s.path.empty() && c.legTime(s) != Campaign::kClosed) {
            // On the line between where it set out from and the next cell, going at its leg's pace.
            int32_t ax, ay, bx, by;
            Campaign::Stack first = s, last = s;
            first.progress = 0;
            last.progress = c.legTime(s);
            c.bodyOf(first, 0, ax, ay);
            c.bodyOf(last, 0, bx, by);
            int32_t px, py;
            c.bodyOf(s, 0, px, py);
            const double whole = std::hypot(double(bx - ax), double(by - ay)) / 1000.0;
            const double there = std::hypot(double(px - ax), double(py - ay)) / 1000.0 + std::hypot(double(bx - px), double(by - py)) / 1000.0;
            CHECK_NEAR(there, whole, 0.01);
            float speed = 0;
            for (const MapSoldier& m : men)
                if (m.stack == s.id) speed = m.speed;
            CHECK(speed > 0.3f && speed < 20.0f);  // a catapult's 0.8 m/s on plains to a wolf rider's 15 on a road
            ++marching;
        }
    }
    CHECK(garrisons > 60);
    CHECK(marching > 0);
}

TEST(stacks_standing_in_one_town_stand_side_by_side_not_on_each_other) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    const Campaign::CityState& home = c.cities()[0];
    const uint32_t a = c.raise(0, home.cx, home.cy, {UnitType::HeavyInfantry});
    const uint32_t b = c.raise(0, home.cx, home.cy, {UnitType::HeavyInfantry});
    std::vector<MapSoldier> men;
    collectMapSoldiers(c, 0, 0, men);
    double ax = 0, ay = 0, bx = 0, by = 0;
    int na = 0, nb = 0;
    for (const MapSoldier& m : men) {
        if (m.stack == a) ax += m.x, ay += m.y, ++na;
        if (m.stack == b) bx += m.x, by += m.y, ++nb;
    }
    // A squad of twelve is a block of four files and three ranks.
    CHECK(na == 12 && nb == 12);
    if (na && nb) CHECK(std::hypot(ax / na - bx / nb, ay / na - by / nb) > 6.0);
}
