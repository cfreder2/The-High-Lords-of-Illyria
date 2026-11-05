// Conquest is Skirmish on the island (docs/ARCHITECTURE_RULES.md, docs/PARITY.md): a march across a walled town goes
// round its walls through its gates, and archers ordered onto the wall-walk climb the ramps, exactly as in a skirmish,
// because the men are the same units of the same World with the same navigation. These tests run the scenario in a
// Conquest and, where there is a skirmish equivalent, in a skirmish, and compare.
#include <cstdio>
#include <memory>
#include <span>

#include "illyria/sim/campaign.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "campaign_orders.h"
#include "test.h"

using namespace illyria::sim;

namespace {

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// The island's ground and navigation grid.
struct Island {
    Campaign c;
    std::unique_ptr<NavGrid> nav;
    explicit Island(uint64_t seed) : c(continent(), seed) {
        for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);  // (no computer lords: only what the test does moves)
        c.prepareWorld();
        nav = std::make_unique<NavGrid>(*c.world()->region());
    }
    Vec3 local(const Vec3& p) const { return {p.x - c.world()->region()->origin().x, p.y - c.world()->region()->origin().y, p.z}; }
};

}  // namespace

TEST(a_march_across_a_walled_town_goes_round_its_walls) {
    // From the capital's courtyard to a place on the far side of its walls, in each of the eight directions: no man of
    // the march is ever inside a wall, a tower, a house or a shut gate (the ground the World's navigation keeps men out of).
    Island is(5);
    Campaign& c = is.c;
    const Campaign::CityState& cap = c.cities().front();
    const Continent::City& town = continent().cities().front();
    int tested = 0, inside = 0;
    for (int d = 0; d < 8; ++d) {
        static const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const int32_t x = cap.cx + dx[d] * 4, y = cap.cy + dy[d] * 4;
        if (x < 0 || y < 0 || x >= c.width() || y >= c.height() || c.cityAt(x, y) >= 0) continue;
        const uint32_t id = c.raise(0, cap.cx, cap.cy, {UnitType::LightInfantry, UnitType::Archers});
        if (!CampaignOrders::move(c, id, x, y)) continue;
        ++tested;
        for (int t = 0; t < 4000; ++t) {
            c.step();
            if (t % 5) continue;
            const Campaign::Stack* s = c.findStack(id);
            if (!s) break;
            std::vector<Campaign::Soldier> men;
            c.soldiers(0, men);
            for (const Campaign::Soldier& m : men) {
                if (m.stack != id) continue;
                const Vec3 p = m.unit ? m.unit->pos
                                      : Vec3{Fixed::fromRatio(static_cast<int64_t>(m.x * 100), 100) - c.world()->region()->origin().x,
                                             Fixed::fromRatio(static_cast<int64_t>(m.y * 100), 100) - c.world()->region()->origin().y, town.pos.z};
                const bool onWalk = m.unit && m.unit->pos.z > town.pos.z + Fixed::fromInt(3);
                if (!onWalk && is.nav->blocked(p)) ++inside;
            }
            if (s->path.empty() && t > 60) break;
        }
    }
    CHECK(tested >= 3);
    std::printf("  %d marches, %d samples of a man inside a wall\n", tested, inside);
    CHECK(inside == 0);
}

TEST(archers_ordered_onto_the_wall_walk_climb_the_ramps_as_in_a_skirmish) {
    Island is(5);
    Campaign& c = is.c;
    const Campaign::CityState& cap = c.cities().front();
    const Continent::City& town = continent().cities().front();
    const Region& r = *c.world()->region();
    // The embrasure farthest round from the courtyard, on the town's outer ring.
    const Vec3 yard = is.local(town.castle.courtyard);
    const Rampart* ring = nullptr;
    for (const Rampart& rp : r.ramparts())
        if (!rp.embrasures.empty() && !rp.ramps.empty() && (rp.centre - is.local(town.pos)).flat().length() < Fixed::fromInt(2))
            if (!ring || rp.corners.size() >= ring->corners.size()) ring = &rp;
    CHECK(ring != nullptr);
    if (!ring) return;
    Vec3 post = ring->embrasures.front().pos;
    for (const Rampart::Embrasure& e : ring->embrasures)
        if ((e.pos - yard).flat().length() > (post - yard).flat().length()) post = e.pos;
    const uint32_t id = c.raise(0, cap.cx, cap.cy, {UnitType::Archers});
    c.step();
    const Campaign::Stack* st = c.findStack(id);
    CHECK(st != nullptr);
    if (!st) return;
    std::vector<uint32_t> units;
    for (const Army& a : st->armies)
        for (const Man& m : a.men)
            if (m.unit) units.push_back(m.unit);
    CHECK(!units.empty());  // (a man of a stack is a unit of the world from the moment he is raised)
    if (units.empty()) return;
    // The Conquest: the order is the skirmish's own Command, carried by an Order.
    for (uint32_t u : units) {
        Campaign::Order o;
        o.kind = Campaign::Order::Kind::Command;
        o.side = 0;
        o.command = Command{0, u, Command::Type::Move, post};
        c.submit(o);
    }
    int conquestTicks = -1;
    for (int t = 0; t < 240 * kTicksPerSecond && conquestTicks < 0; ++t) {
        c.step();
        int up = 0;
        for (uint32_t u : units)
            if (const Unit* w = c.world()->findUnit(u); w && w->pos.z > post.z - Fixed::fromRatio(1, 2) && (w->pos - post).flat().length() < Fixed::fromInt(12)) ++up;
        if (up == static_cast<int>(units.size())) conquestTicks = t;
    }
    CHECK(conquestTicks >= 0);
    // The skirmish: the same order to a man in the same ground, on the town's own window.
    const auto window = std::make_shared<const Region>(Region::fromContinent(continent(), town.pos, 256));
    WorldConfig cfg;
    cfg.region = window;
    World w(cfg);
    const Vec3 wyard = {town.castle.courtyard.x - window->origin().x, town.castle.courtyard.y - window->origin().y, town.castle.courtyard.z};
    const Vec3 wpost = {post.x + r.origin().x - window->origin().x, post.y + r.origin().y - window->origin().y, post.z};
    const EntityId a = w.spawnUnit(0, wyard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::Archers));
    const Command go{0, a, Command::Type::Move, wpost};
    w.step(std::span(&go, 1));
    int skirmishTicks = -1;
    for (int t = 0; t < 240 * kTicksPerSecond && skirmishTicks < 0; ++t) {
        w.step({});
        const Unit* u = w.findUnit(a);
        if (u->pos.z > wpost.z - Fixed::fromRatio(1, 2) && (u->pos - wpost).flat().length() < Fixed::fromInt(12)) skirmishTicks = t;
    }
    CHECK(skirmishTicks >= 0);
    std::printf("  to the embrasure: conquest %d ticks, skirmish %d\n", conquestTicks, skirmishTicks);
    CHECK(conquestTicks <= skirmishTicks * 2 + 120);
}
