// The campaign's soldiers on the land (game/src/campaign_soldiers.cpp,
// docs/ONE_WORLD.md): a man for every soldier of every stack, placed from the
// campaign's state alone, along the stack's route and in its town's courtyard,
// and none of it written back into the simulation.
#include <cmath>
#include <cstdio>

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

TEST(every_soldier_of_every_stack_on_land_is_laid_out_inside_its_stacks_box) {
    Campaign c = started(7, 3);
    MapFacing facing;
    std::vector<MapSoldier> men;
    std::vector<StackShape> shapes;
    layoutMapSoldiers(c, 0, 0.016f, 0, facing, men, shapes);
    size_t want = 0;
    for (const Campaign::Stack& s : c.stacks())
        if (!s.boat)
            for (const Army& a : s.armies) want += static_cast<size_t>(std::max(1, a.soldiers));
    CHECK(men.size() == want);
    CHECK(want > 1000);  // every town's garrison, and the lords' armies
    CHECK(shapes.size() > 80);
    for (const MapSoldier& m : men) {
        const StackShape* sh = nullptr;
        for (const StackShape& k : shapes)
            if (k.id == m.stack) sh = &k;
        CHECK(sh != nullptr);
        if (!sh) continue;
        const float dx = m.x - sh->x, dy = m.y - sh->y;
        const float along = dx * std::cos(sh->yaw) + dy * std::sin(sh->yaw), across = dx * std::sin(sh->yaw) - dy * std::cos(sh->yaw);
        CHECK(std::fabs(along) <= sh->halfLength + 0.01f && std::fabs(across) <= sh->halfWidth + 0.01f);
        CHECK(m.owner == c.findStack(m.stack)->owner);
    }
}

TEST(laying_out_the_soldiers_changes_nothing_in_the_campaign) {
    Campaign c = started(11, 2);
    const uint64_t before = c.checksum();
    MapFacing facing;
    std::vector<MapSoldier> men;
    std::vector<StackShape> shapes;
    for (int k = 0; k < 5; ++k) layoutMapSoldiers(c, 50.0 * k, 0.033f, 1, facing, men, shapes);
    CHECK(c.checksum() == before);
    // Nor does it depend on anything but the campaign: a stack's place is the same on every call.
    for (const Campaign::Stack& s : c.stacks()) {
        const StackPlace a = stackPlace(c, s, 25.0), b = stackPlace(c, s, 25.0);
        CHECK(a.x == b.x && a.y == b.y && a.speed == b.speed);
    }
}

TEST(a_stack_in_a_town_stands_in_its_courtyard_and_a_marching_one_between_its_ends) {
    Campaign c = started(13, 3);
    int garrisons = 0, marching = 0;
    for (const Campaign::Stack& s : c.stacks()) {
        if (s.armies.empty() || s.boat) continue;
        const StackPlace at = stackPlace(c, s, 0);
        const int32_t town = c.cityAt(s.cx, s.cy);
        if (s.path.empty() && town >= 0) {
            const Continent::Castle& k = c.continent().cities()[static_cast<size_t>(town)].castle;
            CHECK(std::hypot(at.x - static_cast<float>(k.courtyard.x.toDouble()), at.y - static_cast<float>(k.courtyard.y.toDouble())) < 0.5f);
            CHECK(at.speed == 0);
            ++garrisons;
        }
        if (!s.path.empty() && c.legTime(s) != Campaign::kClosed) {
            // On the line between where it set out from and the next cell, going at its leg's pace.
            Campaign::Stack first = s, last = s;
            first.progress = 0;
            last.progress = c.legTime(s);
            const StackPlace start = stackPlace(c, first, 0), end = stackPlace(c, last, 0);
            const float whole = std::hypot(end.x - start.x, end.y - start.y);
            CHECK_NEAR(std::hypot(at.x - start.x, at.y - start.y) + std::hypot(end.x - at.x, end.y - at.y), whole, 0.01);
            CHECK(at.speed > 0.3f && at.speed < 20.0f);  // a catapult's 0.8 m/s on plains to a wolf rider's 15 on a road
            // Between ticks it glides on: half a tick further is as far as that pace goes in that time.
            const StackPlace later = stackPlace(c, s, Campaign::kTimeScale / 2);
            if (s.progress + Campaign::kTimeScale / 2 < c.legTime(s))
                CHECK_NEAR(std::hypot(later.x - at.x, later.y - at.y), at.speed / (2.0 * Campaign::kTicksPerSecond), 0.01);
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
    MapFacing facing;
    std::vector<MapSoldier> men;
    std::vector<StackShape> shapes;
    layoutMapSoldiers(c, 0, 0.016f, 0, facing, men, shapes);
    const StackShape *sa = nullptr, *sb = nullptr;
    for (const StackShape& s : shapes) {
        if (s.id == a) sa = &s;
        if (s.id == b) sb = &s;
    }
    CHECK(sa && sb);
    if (sa && sb) CHECK(std::hypot(sa->x - sb->x, sa->y - sb->y) > sa->halfWidth + sb->halfWidth);
    // A squad of twelve is a block of four files and three ranks.
    int n = 0;
    for (const MapSoldier& m : men) n += m.stack == a;
    CHECK(n == 12);
}
