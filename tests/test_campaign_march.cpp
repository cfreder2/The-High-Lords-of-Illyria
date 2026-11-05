// Marching on the campaign map at the battle's speeds, on the battle's clock
// (engine/sim/src/campaign_march.cpp, docs/ONE_WORLD.md): a step takes its
// distance over the slowest army's speed on that ground, roads double it,
// climbs slow it, and fliers alone go at their flight cycle's pace over
// anything.
#include <cstdio>
#include <cstdlib>

#include "illyria/sim/campaign.h"
#include "illyria/sim/nav.h"
#include "campaign_orders.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// A campaign where nobody moves but by our orders.
Campaign still(uint64_t seed) {
    Campaign c(continent(), seed);
    for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);
    return c;
}

// Two plain cells side by side at the same height, no river between them and
// nobody near, from which a man on foot can reach a town: (x, y) and (x + 1, y).
bool levelPlain(Campaign& c, int32_t& x, int32_t& y) {
    Campaign::Stack probe;
    probe.armies.push_back(Army{});
    for (y = 1; y < c.height() - 1; ++y)
        for (x = 1; x < c.width() - 2; ++x) {
            if (c.terrainAt(x, y) != Lay::Plain || c.terrainAt(x + 1, y) != Lay::Plain || c.cellHeight(x, y) != c.cellHeight(x + 1, y)) continue;
            if (c.stepTime(probe, x, y, x + 1, y) == Campaign::kClosed) continue;
            bool empty = true;
            for (const Campaign::Stack& s : c.stacks()) empty = empty && std::abs(s.cx - x) + std::abs(s.cy - y) > 3;
            if (!empty) continue;
            // Not a pocket walled in by water, peaks and rivers.
            const uint32_t id = c.raise(0, x, y, {UnitType::LightInfantry});
            bool out = false;
            for (const Campaign::CityState& t : c.cities()) out = out || CampaignOrders::move(c, id, t.cx, t.cy);
            CampaignOrders::disband(c, id, c.findStack(id)->armies.front().id);
            if (out) return true;
        }
    return false;
}

// Hundredths of a tick to cover mm at mm a second.
int64_t ticksFor(int64_t mm, int64_t speed) { return mm * Campaign::kTicksPerSecond * Campaign::kTimeScale / speed; }

// Ticks until the stack stands on (x, y), or -1.
int32_t ticksTo(Campaign& c, uint32_t id, int32_t x, int32_t y, int32_t most) {
    for (int32_t t = 1; t <= most; ++t) {
        c.step();
        const Campaign::Stack* s = c.findStack(id);
        if (!s) return -1;
        if (s->cx == x && s->cy == y && s->path.empty()) return t;
    }
    return -1;
}
}  // namespace

TEST(the_campaign_runs_on_the_battles_clock_with_two_minute_days) {
    CHECK(Campaign::kTicksPerSecond == kTicksPerSecond);
    CHECK(Campaign::kTicksPerDay == 120 * kTicksPerSecond);
    // What is reckoned in days keeps its days: the land ten times a day, a lone army's banner in about half of one.
    CHECK(Campaign::kTerritoryTicks * 10 == Campaign::kTicksPerDay);
    const int32_t lone = Campaign::kBannerFull / (Campaign::kBannerBase + 1);
    CHECK(lone * 2 > Campaign::kTicksPerDay && lone < Campaign::kTicksPerDay);
}

TEST(a_step_takes_its_distance_over_the_speed_and_a_road_halves_it) {
    // Level plains: the cell's width at the unit's own speed, as in battle.
    const int32_t plain = Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, 0, false);
    CHECK(plain == Campaign::kCell * 1000);
    CHECK(Campaign::groundSpeed(UnitType::HeavyInfantry) == 1900);  // UNITS.md: 1.9 m/s
    // A road doubles every gait's pace; half a step on road, half off, is in between.
    for (int g = 0; g < kGaitCount; ++g) {
        const Gait gait = static_cast<Gait>(g);
        const int32_t road = Campaign::effort(gait, Ground::Road, Ground::Road, 0, false);
        CHECK(std::abs(road * 2 - Campaign::effort(gait, Ground::Plains, Ground::Plains, 0, false)) <= 1);
        CHECK(std::abs(Campaign::effort(gait, Ground::Road, Ground::Plains, 0, false) - road * 3 / 2) <= 1);
    }
    // A diagonal step is the square root of two steps.
    CHECK(std::abs(Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, 0, true) - Campaign::kCell * 1414) <= Campaign::kCell);
    // A wood slows a man to 70%, a horse to half.
    CHECK(std::abs(Campaign::effort(Gait::Foot, Ground::Forest, Ground::Forest, 0, false) * 7 - plain * 10) <= 10);
    CHECK(std::abs(Campaign::effort(Gait::Horse, Ground::Forest, Ground::Forest, 0, false) - plain * 2) <= 2);
    // Water closes the way to every walker.
    for (int g = 0; g < kGaitCount; ++g) CHECK(Campaign::effort(static_cast<Gait>(g), Ground::Plains, Ground::DeepWater, 0, false) == Campaign::kClosed);

    // On the map: heavy infantry over one level cell of plain take 70 s (133 m at 1.9 m/s), to the tick.
    Campaign c = still(1);
    int32_t x = 0, y = 0;
    CHECK(levelPlain(c, x, y));
    const uint32_t id = c.raise(0, x, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry});
    const int64_t want = ticksFor(int64_t{Campaign::kCell} * 1000, 1900);
    CHECK(c.stepTime(*c.findStack(id), x, y, x + 1, y) == want);
    CHECK(CampaignOrders::move(c, id, x + 1, y));
    CHECK(c.timeLeft(*c.findStack(id)) == want);
    const int32_t ticks = ticksTo(c, id, x + 1, y, Campaign::kTicksPerDay);
    std::printf("  heavy infantry over %d m of level plain: %d ticks (%.1f s)\n", Campaign::kCell, ticks, ticks / double(Campaign::kTicksPerSecond));
    CHECK(ticks == (want + Campaign::kTimeScale - 1) / Campaign::kTimeScale);
}

TEST(climbing_slows_a_march_as_it_slows_a_soldier) {
    const int32_t level = Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, 0, false);
    // Up a one in ten: 1 + 0.1 x 2.5 times as long for a man (nav.h's kClimbCost).
    const int32_t rise = Campaign::kCell / 10;
    const Fixed grade = Fixed::fromInt(rise) / Fixed::fromInt(Campaign::kCell);
    const int32_t up = Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, rise, false);
    CHECK(std::abs(up - static_cast<int32_t>((Fixed::fromInt(level) * (Fixed::one() + grade * kClimbCost)).floorToInt())) <= 5);
    // Down is quicker, but never by more than the battle's 15%.
    const int32_t down = Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, -rise, false);
    CHECK(down < level && down * 115 >= level * 100 - 115);
    CHECK(Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, -Campaign::kCell / 3, false) * 115 >= level * 100 - 115);
    // Dwarves climb at half a man's cost; wheels at half again.
    CHECK(Campaign::effort(Gait::Dwarf, Ground::Plains, Ground::Plains, rise, false) < up);
    CHECK(Campaign::effort(Gait::Wheeled, Ground::Plains, Ground::Plains, rise, false) > up);
    // Steeper than the battle's steep slope: a further 20% slower, either way.
    const int32_t steep = Campaign::kCell * 2 / 5;
    CHECK(Campaign::effort(Gait::Foot, Ground::Plains, Ground::Plains, -steep, false) > down);
}

TEST(the_slowest_army_sets_the_pace) {
    Campaign c = still(2);
    int32_t x = 0, y = 0;
    CHECK(levelPlain(c, x, y));
    const uint32_t horse = c.raise(0, x, y, {UnitType::LightCavalry});
    const uint32_t foot = c.raise(0, x, y, {UnitType::HeavyInfantry});
    const uint32_t both = c.raise(0, x, y, {UnitType::LightCavalry, UnitType::HeavyInfantry, UnitType::LightCavalry});
    const uint32_t engine = c.raise(0, x, y, {UnitType::LightCavalry, UnitType::Catapult});
    // On every step anywhere, the mixed stack takes as long as the slower of the two on that ground.
    const Campaign::Stack &h = *c.findStack(horse), &f = *c.findStack(foot), &b = *c.findStack(both), &e = *c.findStack(engine);
    int32_t steps = 0, mismatched = 0;
    for (int32_t cy = 0; cy < c.height(); cy += 3)
        for (int32_t cx = 0; cx < c.width(); cx += 3)
            for (int32_t dy = -1; dy <= 1; ++dy)
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int32_t th = c.stepTime(h, cx, cy, cx + dx, cy + dy), tf = c.stepTime(f, cx, cy, cx + dx, cy + dy);
                    if (th == Campaign::kClosed || tf == Campaign::kClosed) continue;
                    ++steps;
                    mismatched += c.stepTime(b, cx, cy, cx + dx, cy + dy) != std::max(th, tf);
                    CHECK(th < tf);  // the horse is quicker anywhere a man is
                    CHECK(c.stepTime(e, cx, cy, cx + dx, cy + dy) > th);
                }
    std::printf("  %d steps: a stack of horse and foot takes the foot's time on each\n", steps);
    CHECK(steps > 1000);
    CHECK(mismatched == 0);
    // The catapult rolls on wheels: a wood holds it up far more than it does a man.
    CHECK(gaitOf(UnitType::Catapult) == Gait::Wheeled);
    // And sent the same way, horse and foot together arrive with the foot.
    // (To the nearest town they can reach.)
    int32_t tx = -1, ty = -1;
    for (int32_t r = 1; r < c.width() && tx < 0; ++r)
        for (const Campaign::CityState& t : c.cities())
            if (tx < 0 && std::max(std::abs(t.cx - x), std::abs(t.cy - y)) == r && CampaignOrders::move(c, foot, t.cx, t.cy)) tx = t.cx, ty = t.cy;
    CHECK(tx >= 0);
    CHECK(CampaignOrders::move(c, both, tx, ty) && CampaignOrders::move(c, horse, tx, ty));
    CHECK(c.findStack(both)->path == c.findStack(foot)->path);
    CHECK(c.timeLeft(*c.findStack(both)) == c.timeLeft(*c.findStack(foot)));
    CHECK(c.timeLeft(*c.findStack(horse)) * 2 < c.timeLeft(*c.findStack(foot)));
}

TEST(fliers_alone_go_at_their_flight_cycles_pace_over_anything) {
    // The long-run pace (UNITS.md): the take-off run, the flight as long as
    // their endurance at air speed, then twice as long resting.
    for (UnitType t : {UnitType::Griffins, UnitType::Pegasi, UnitType::Dragons, UnitType::Archons}) {
        const UnitStats s = unitStats(t);
        const double e = static_cast<double>(s.staminaTicks) / kTicksPerSecond;
        const double pace = (s.takeoffRun.toDouble() + s.flySpeed.toDouble() * e) /
                            (s.takeoffRun.toDouble() / (1.5 * s.moveSpeed.toDouble()) + e + 2 * e);
        std::printf("  %s: %.2f m/s on the wing in the long run (%.1f in the air, %.1f on foot)\n", unitTypeName(t), Campaign::flightPace(t) / 1000.0,
                    s.flySpeed.toDouble(), s.moveSpeed.toDouble());
        CHECK(std::abs(Campaign::flightPace(t) - pace * 1000) <= 2);
        CHECK(Campaign::flightPace(t) > Campaign::groundSpeed(t));
    }
    CHECK(Campaign::flightPace(UnitType::HeavyInfantry) == 0);
    Campaign c = still(3);
    // Griffins on every step take the same time, whatever is below: sea, mountains, roads, rivers.
    const uint32_t id = c.raise(0, c.width() / 2, c.height() / 2, {UnitType::Griffins, UnitType::Griffins});
    const Campaign::Stack& g = *c.findStack(id);
    CHECK(Campaign::airborne(g));
    const int64_t straight = ticksFor(int64_t{Campaign::kCell} * 1000, Campaign::flightPace(UnitType::Griffins));
    int32_t water = 0, mountains = 0;
    for (int32_t cy = 0; cy + 1 < c.height(); ++cy)
        for (int32_t cx = 0; cx + 1 < c.width(); ++cx) {
            CHECK(c.stepTime(g, cx, cy, cx + 1, cy) == straight);
            CHECK(c.stepTime(g, cx, cy, cx, cy + 1) == straight);
            water += c.terrainAt(cx + 1, cy) == Lay::Water;
            mountains += c.terrainAt(cx + 1, cy) == Lay::Mountain;
        }
    CHECK(water > 100 && mountains > 100);
    // Sent from the west coast out over the sea: a straight line, at that pace.
    int32_t sx = -1, sy = 0;
    for (int32_t row = 0; row < c.height(); ++row) {
        int32_t coast = 0;
        while (coast < c.width() && c.terrainAt(coast, row) == Lay::Water) ++coast;
        if (coast < c.width() && coast > sx) sx = coast, sy = row;
    }
    CHECK(sx >= 10);
    const uint32_t far = c.raise(0, sx, sy, {UnitType::Griffins});
    const int32_t out = 12;  // a kilometre and a half or so on today's map
    CHECK(CampaignOrders::move(c, far, sx - out, sy));
    CHECK(static_cast<int32_t>(c.findStack(far)->path.size()) == out);
    CHECK(c.timeLeft(*c.findStack(far)) == straight * out);
    const int32_t ticks = ticksTo(c, far, sx - out, sy, 10 * Campaign::kTicksPerDay);
    std::printf("  griffins out %d cells over the sea: %d ticks (%.1f min at 1x)\n", out, ticks, ticks / 60.0 / Campaign::kTicksPerSecond);
    CHECK(std::abs(ticks - straight * out / Campaign::kTimeScale) <= 1);
    // One walker among them and they all walk: never over the sea or the peaks.
    const uint32_t mixed = c.raise(0, sx, sy, {UnitType::Griffins, UnitType::Hero});
    CHECK(!Campaign::airborne(*c.findStack(mixed)));
    for (int32_t cx = 0; cx < c.width(); ++cx)
        if (c.terrainAt(cx, sy) == Lay::Water || c.terrainAt(cx, sy) == Lay::Mountain) CHECK(!c.passable(*c.findStack(mixed), cx, sy));
}

TEST(winged_sandals_make_the_heros_stack_a_third_quicker) {
    Campaign c = still(4);
    int32_t x = 0, y = 0;
    CHECK(levelPlain(c, x, y));
    const uint32_t plain = c.raise(0, x, y, {UnitType::Hero, UnitType::LightInfantry});
    int32_t sandals = -1;
    for (size_t k = 0; k < Campaign::artifacts().size(); ++k)
        if (Campaign::artifacts()[k].speed > 0) sandals = static_cast<int32_t>(k);
    CHECK(sandals >= 0);
    if (sandals < 0) return;
    Campaign::Stack shod = *c.findStack(plain);
    for (Army& a : shod.armies)
        if (a.hero()) a.artifacts.push_back(static_cast<uint8_t>(sandals));
    const int32_t without = c.stepTime(*c.findStack(plain), x, y, x + 1, y), with = c.stepTime(shod, x, y, x + 1, y);
    std::printf("  a hero and light infantry over a cell: %d, with the %s %d\n", without, Campaign::artifacts()[static_cast<size_t>(sandals)].name, with);
    CHECK(std::abs(with * (100 + Campaign::artifacts()[static_cast<size_t>(sandals)].speed) - without * 100) <= 133);
}
