// Oakhaven Ford (docs/GAME_DESIGN.md §4.1.4): fair ground either side of the Aldwater (sim::Continent::FairGround), the isle
// in the ford with its bare pole (CapturePoint::placeNeutral), deep water between the ford and the two bridges, and the
// computer's way of playing it (battle_isle.cpp).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/sim/nav.h"
#include "test.h"

using namespace illyria::game;
using illyria::sim::Fixed;
using illyria::sim::Ground;
using illyria::sim::UnitType;

namespace {

const Battle& ford() {
    static const std::unique_ptr<Battle> b = [] {
        SkirmishSetup setup;
        setup.budget = 300;
        return std::make_unique<Battle>(1, Scenario::Ford, setup);
    }();
    return *b;
}

Fixed m(int64_t v) { return Fixed::fromInt(v); }
bool wet(Ground g) { return g == Ground::Shallows || g == Ground::DeepWater; }

// The length of the way on foot between two places (battle metres), or -1 with none; `deep` counts its points in deep water.
float wayLength(const illyria::sim::NavGrid& nav, const illyria::sim::Region& rg, V3 from, V3 to, int* deep = nullptr) {
    std::vector<illyria::sim::Vec3> way;
    if (!nav.findPath(toSim(from), toSim(to), way)) return -1;
    float len = 0;
    V3 at = from;
    for (const illyria::sim::Vec3& p : way) {
        const V3 q = toV3(p);
        len += V3{q.x - at.x, q.y - at.y, 0}.length();
        // Every 2 m of the leg: no step of it in deep water.
        const int steps = std::max(1, static_cast<int>(V3{q.x - at.x, q.y - at.y, 0}.length() / 2));
        for (int k = 0; k <= steps && deep; ++k) {
            const V3 s = at + (q - at) * (static_cast<float>(k) / static_cast<float>(steps));
            Fixed deck;
            if (rg.groundAt(Fixed::fromRatio(static_cast<int64_t>(s.x * 16), 16), Fixed::fromRatio(static_cast<int64_t>(s.y * 16), 16)) == Ground::DeepWater &&
                !rg.deckAt(Fixed::fromRatio(static_cast<int64_t>(s.x * 16), 16), Fixed::fromRatio(static_cast<int64_t>(s.y * 16), 16), deck))
                ++*deep;
        }
        at = q;
    }
    return len;
}

}  // namespace

TEST(oakhaven_fords_east_bank_is_its_west_banks_mirror_image) {
    const illyria::sim::Region& rg = ford().region();
    const illyria::sim::Region::Mirror& mr = rg.mirror();
    CHECK(mr.on);
    const int32_t n = rg.terrain().cells() + 1;
    const Fixed cell = rg.terrain().cellSize();
    const int64_t axis = (mr.axis / cell).floorToInt();
    CHECK(cell * axis == mr.axis);  // a line of the grid
    int samples = 0, heights = 0, waters = 0, grounds = 0, cells = 0;
    for (int32_t iy = 0; iy < n; ++iy)
        for (int64_t ix = axis + 1; ix < n; ++ix) {
            if (!mr.holds(cell * ix, cell * iy)) continue;
            const size_t a = static_cast<size_t>(iy) * n + static_cast<size_t>(ix), b = static_cast<size_t>(iy) * n + static_cast<size_t>(axis * 2 - ix);
            ++samples;
            heights += rg.terrain().samples()[a] != rg.terrain().samples()[b];
            waters += rg.waterSurfaces()[a] != rg.waterSurfaces()[b];
        }
    for (int32_t iy = 0; iy + 1 < n; ++iy)
        for (int64_t ix = axis; ix + 1 < n; ++ix) {
            if (!mr.holds(cell * ix + cell / 2, cell * iy + cell / 2)) continue;
            ++cells;
            grounds += rg.cellGround(static_cast<int32_t>(ix), iy) != rg.cellGround(static_cast<int32_t>(axis * 2 - 1 - ix), iy);
        }
    // The trees, rocks and bushes: each has its twin over the water, the same kind and size.
    int things = 0, lone = 0;
    for (const illyria::sim::Obstacle& o : rg.obstacles()) {
        if (!mr.holds(o.pos.x, o.pos.y) || o.pos.x == mr.axis) continue;
        ++things;
        bool twin = false;
        for (const illyria::sim::Obstacle& t : rg.obstacles())
            twin = twin || (t.kind == o.kind && t.pos.y == o.pos.y && t.pos.x == mr.axis * 2 - o.pos.x && t.radius == o.radius && t.height == o.height && t.pos.z == o.pos.z);
        lone += !twin;
    }
    std::printf("  fair ground: %d samples (%d heights, %d waters unlike), %d cells (%d unlike), %d things (%d without a twin)\n", samples, heights, waters, cells, grounds, things, lone);
    CHECK(samples >= 4000 && cells >= 4000 && things > 200);  // (200 m by 320 m of the east bank, every 4 m)
    CHECK(heights == 0);
    CHECK(waters == 0);
    CHECK(grounds == 0);
    CHECK(lone == 0);
    // No ruin, creek or pool on it (one bank would have it and the other not).
    const illyria::sim::Continent& c = ford().continent();
    for (const illyria::sim::Continent::Site& s : c.sites()) CHECK(!c.fairGround().holds(s.pos.x, s.pos.y));
    for (const illyria::sim::Continent::River& st : c.streams())
        for (const illyria::sim::Vec3& p : st.points) CHECK(!c.fairGround().holds(p.x, p.y));
    for (const illyria::sim::Continent::City& t : c.cities()) CHECK(!c.fairGround().holds(t.pos.x, t.pos.y, t.radius));
}

TEST(oakhaven_fords_hosts_stand_alike_squad_for_squad) {
    const Battle& b = ford();
    Battle::Isle g;
    CHECK(b.isle(g));
    int mine = 0, matched = 0;
    for (const Squad& s : b.squads()) {
        if (s.owner != Battle::kPlayer) continue;
        ++mine;
        const V3 c = b.squadCentre(s, false);
        CHECK(std::fabs(g.pole.x - c.x) >= Battle::kIsleCampM - 30.0f);  // back from the water
        for (const Squad& t : b.squads()) {
            if (t.owner != Battle::kEnemy || t.type != s.type || t.members.size() != s.members.size()) continue;
            const V3 d = b.squadCentre(t, false);
            if (std::fabs((d.x - g.pole.x) + (c.x - g.pole.x)) < 0.6f && std::fabs(d.y - c.y) < 0.6f && std::fabs(b.heightAt(d.x, d.y) - b.heightAt(c.x, c.y)) < 0.05f) {
                ++matched;
                break;
            }
        }
    }
    int theirs = 0;
    for (const Squad& s : b.squads()) theirs += s.owner == Battle::kEnemy;
    std::printf("  %d squads a side, %d with their image over the river\n", mine, matched);
    CHECK(mine >= 6);
    CHECK(theirs == mine);
    CHECK(matched == mine);
}

TEST(oakhaven_fords_isle_is_waded_to_from_both_banks_and_the_deep_water_between_ford_and_bridges_is_not) {
    const Battle& b = ford();
    const illyria::sim::Region& rg = b.region();
    Battle::Isle g;
    CHECK(b.isle(g));
    const Fixed ax = rg.mirror().axis, py = Fixed::fromInt(static_cast<int64_t>(g.pole.y));
    // The isle stands dry, with shallows (no deep water) between it and each bank along the road.
    CHECK(!wet(rg.groundAt(ax, py)));
    CHECK(!wet(rg.groundAt(ax + m(6), py + m(8))) && !wet(rg.groundAt(ax - m(6), py - m(8))));
    int shallow = 0;
    for (int64_t dx = 1; dx <= 30; ++dx)
        for (const int64_t side : {-1, 1}) {
            const Ground gr = rg.groundAt(ax + m(dx * side), py);
            CHECK(gr != Ground::DeepWater);
            shallow += gr == Ground::Shallows;
        }
    CHECK(shallow >= 8);
    // Above and below the ford, as far as each bridge, the middle of the river is deep, and on a bridge it is a deck.
    for (const Battle::Isle::Bridge& br : g.bridges) {
        const int64_t off = static_cast<int64_t>(br.y - g.pole.y);
        CHECK(std::abs(off) >= 80);
        for (int64_t dy = 40; dy <= std::abs(off) - 8; dy += 4) CHECK(rg.groundAt(ax, py + m(off > 0 ? dy : -dy)) == Ground::DeepWater);
        Fixed deck;
        CHECK(rg.deckAt(ax, py + m(off), deck));
    }
    CHECK(std::fabs((g.bridges[0].y - g.pole.y) + (g.bridges[1].y - g.pole.y)) < 0.01f);  // the same distance up and down stream
    CHECK(g.bridges[0].reach == g.bridges[1].reach);
    // On foot: from each camp to the pole the same way, through the ford; and bank to bank between the ford and a bridge
    // there is no way but round by one of them (never through the deep water).
    const illyria::sim::NavGrid nav(rg);
    const V3 west{g.pole.x - Battle::kIsleCampM, g.pole.y, 0}, east{g.pole.x + Battle::kIsleCampM, g.pole.y, 0};
    int deep = 0;
    const float fromWest = wayLength(nav, rg, west, g.pole, &deep), fromEast = wayLength(nav, rg, east, g.pole, &deep);
    std::printf("  to the pole: %.1f m from the west camp, %.1f m from the east\n", fromWest, fromEast);
    CHECK(fromWest > 0 && fromWest < Battle::kIsleCampM + 15.0f);
    // (The ground is the same; the pathfinder breaks its ties one way round, and its way over the isle's last cells differs.)
    CHECK(std::fabs(fromWest - fromEast) < 8.0f);
    for (const Battle::Isle::Bridge& br : g.bridges) {
        const float mid = (br.y + g.pole.y) / 2;  // half way between the ford and the bridge
        const float across = wayLength(nav, rg, {g.pole.x - 40, mid, 0}, {g.pole.x + 40, mid, 0}, &deep);
        std::printf("  bank to bank half way to a bridge: %.1f m on foot (80 m as the crow flies)\n", across);
        CHECK(across > 150.0f);
        // Camp to the far end of each bridge: the same from either side.
        const float w = wayLength(nav, rg, west, {g.pole.x + br.reach + 8, br.y, 0}, &deep), e = wayLength(nav, rg, east, {g.pole.x - br.reach - 8, br.y, 0}, &deep);
        CHECK(w > 0 && std::fabs(w - e) < 8.0f);
    }
    CHECK(deep == 0);
}

TEST(a_bare_pole_takes_the_flag_of_whoever_stands_at_it_alone) {
    // Three small worlds over one pole: only the first host at it, both, only the second.
    using illyria::sim::Vec3;
    using illyria::sim::World;
    const Vec3 pole{m(100), m(100), Fixed::zero()};
    const auto world = [&](bool first, bool second) {
        illyria::sim::WorldConfig cfg;
        auto w = std::make_unique<World>(cfg);
        // (Far enough apart at the pole that the step under them is not a fight's.)
        if (first) w->spawnUnit(0, pole + Vec3{-m(9), Fixed::zero(), Fixed::zero()}, {Fixed::one(), Fixed::zero(), Fixed::zero()}, illyria::sim::unitStats(UnitType::Journeyman));
        if (second) w->spawnUnit(1, pole + Vec3{m(9), Fixed::zero(), Fixed::zero()}, {-Fixed::one(), Fixed::zero(), Fixed::zero()}, illyria::sim::unitStats(UnitType::Journeyman));
        w->spawnUnit(1, pole + Vec3{m(40), Fixed::zero(), Fixed::zero()}, {-Fixed::one(), Fixed::zero(), Fixed::zero()}, illyria::sim::unitStats(UnitType::Journeyman));  // (off the isle: counts for nothing)
        return w;
    };
    const auto mine = world(true, false), both = world(true, true), theirs = world(false, true), nobody = world(false, false);
    CapturePoint cap;
    cap.placeNeutral(pole, 0, 1);
    CHECK(cap.neutral() && cap.bare() && !cap.taken());
    // The first host alone: its flag climbs, a quarter of the way in a quarter of the time.
    for (int t = 0; t < CapturePoint::kNeutralSeconds * illyria::sim::kTicksPerSecond / 4; ++t) {
        mine->step({});
        cap.step(*mine);
    }
    CHECK(cap.attacker() == 0);
    CHECK(std::abs(cap.progress() - CapturePoint::kFull / 4) <= 8);
    CHECK(!cap.bare());
    // Both at the pole: it stays where it is. Nobody: likewise.
    const int32_t held = cap.progress();
    for (int t = 0; t < 60; ++t) cap.step(*both);
    CHECK(cap.progress() == held && cap.blocked());
    for (int t = 0; t < 60; ++t) cap.step(*nobody);
    CHECK(cap.progress() == held && cap.attacker() == 0);
    // The second host alone: the first's flag comes down three times as fast, and then its own goes up.
    int down = 0;
    while (cap.attacker() == 0 && down < 1000) {
        cap.step(*theirs);
        ++down;
    }
    CHECK(cap.attacker() == 1);
    CHECK(std::abs(down - held / CapturePoint::kHaulDown) <= 2);
    CHECK(cap.progress() == 0);
    int up = 0;
    while (!cap.taken() && up < 5000) {
        theirs->step({});
        cap.step(*theirs);
        ++up;
    }
    CHECK(cap.taken() && cap.attacker() == 1);
    CHECK(std::abs(up - CapturePoint::kNeutralSeconds * illyria::sim::kTicksPerSecond) <= 4 * illyria::sim::kTicksPerSecond);
    // Taken is for good.
    for (int t = 0; t < 100; ++t) cap.step(*mine);
    CHECK(cap.taken() && cap.attacker() == 1);
}

TEST(oakhaven_ford_has_a_bare_pole_on_its_isle) {
    const Battle& b = ford();
    Battle::Isle g;
    CHECK(b.isle(g));
    CHECK(b.capturePoint().active() && b.capturePoint().neutral() && b.capturePoint().bare());
    CHECK(std::fabs(g.pole.x - Battle::kWorldSize / 2) < 0.01f && std::fabs(g.pole.y - Battle::kWorldSize / 2) < 0.01f);
    CHECK(b.siegeLine() == "Raise your flag on the isle");
    CHECK(b.outcome() == 0);
    // No other field has one.
    SkirmishSetup setup;
    setup.budget = 100;
    const Battle field(1, Scenario::Field, setup);
    CHECK(!field.capturePoint().active() && !field.isle(g));
}

TEST(oakhaven_ford_the_computer_takes_the_isle_holds_a_bridge_with_its_pikes_keeps_out_of_the_deep_water_and_wins_by_the_flag) {
    SkirmishSetup setup;
    setup.budget = 300;
    Battle b(7, Scenario::Ford, setup);
    Battle::Isle g;
    CHECK(b.isle(g));
    // The player gives no order. Seventy seconds in: the computer's foot is on the isle and its flag is climbing, and its
    // pikes stand at their end of a bridge. Left alone its flag reaches the top and the battle is its, by the flag; and
    // nobody of its host was ever in deep water.
    int deep = 0, onIsle = 0, pikesAtBridge = 0, flagAt70 = 0, t = 0;
    for (; t < 240 * illyria::sim::kTicksPerSecond && b.outcome() == 0; ++t) {
        b.advance(1.0 / illyria::sim::kTicksPerSecond);
        if (t % 15 == 0)
            for (const illyria::sim::Unit& u : b.world().units()) {
                if (!u.alive() || u.owner != Battle::kEnemy || u.inAir()) continue;
                Fixed deck;
                deep += b.worldRegion().groundAt(u.pos.x, u.pos.y) == Ground::DeepWater && !b.worldRegion().deckAt(u.pos.x, u.pos.y, deck);
            }
        if (t != 70 * illyria::sim::kTicksPerSecond) continue;
        flagAt70 = b.capturePoint().attacker() == Battle::kEnemy ? b.capturePoint().progress() : 0;
        for (const Squad& s : b.squads()) {
            if (s.owner != Battle::kEnemy || b.aliveCount(s) == 0) continue;
            const V3 c = b.squadCentre(s, false);
            onIsle += V3{c.x - g.pole.x, c.y - g.pole.y, 0}.length() < 14.0f;
            if (s.type != UnitType::Pikemen) continue;
            for (const Battle::Isle::Bridge& br : g.bridges) pikesAtBridge += V3{c.x - (g.pole.x + br.reach + 8), c.y - br.y, 0}.length() < 12.0f;
        }
    }
    std::printf("  at 70 s: %d squads on the isle, flag %d%%, pikes at a bridge %d; over after %d s, outcome %d, \"%s\"; sightings in deep water %d\n", onIsle,
                flagAt70 * 100 / CapturePoint::kFull, pikesAtBridge, t / illyria::sim::kTicksPerSecond, b.outcome(), b.siegeResult().c_str(), deep);
    CHECK(onIsle >= 1);
    CHECK(flagAt70 > 0);
    CHECK(pikesAtBridge >= 1);
    CHECK(deep == 0);
    CHECK(b.capturePoint().taken() && b.capturePoint().attacker() == Battle::kEnemy);
    CHECK(b.outcome() == 2);
    CHECK(b.result().how == Battle::Result::How::Flag);
    CHECK(b.siegeResult() == "Their flag flies over the isle.");
}
