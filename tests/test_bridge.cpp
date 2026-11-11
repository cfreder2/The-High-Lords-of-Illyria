// The footbridge at Oakhaven: men fight on its deck and stay on it, no one walks in under it, and anyone thrown
// into the river swims to the bank and goes on.
#include <cstdio>
#include <memory>
#include <vector>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }

std::shared_ptr<const Region> fordRegion() {
    static const auto region = [] {
        const Continent c(20261027);
        return std::make_shared<const Region>(Region::fromContinent(c, c.crossings().front().pos, 256));
    }();
    return region;
}

const Deck& footbridge() {
    for (const Deck& d : fordRegion()->decks())
        if (d.foot && !d.raised) return d;
    return fordRegion()->decks().front();
}

// Where a man is, by the bridge: on its deck, in the river (or under the deck), or neither.
struct Where {
    bool onDeck, river;
};
Where whereIs(const World& w, const Unit& u) {
    const Region& rg = *w.region();
    Fixed deck;
    const bool on = rg.deckBelow(u.pos.x, u.pos.y, u.pos.z + Fixed::fromRatio(9, 10), deck) && u.pos.z > deck - Fixed::one();
    return {on, !on && w.inRiver(u.pos.x, u.pos.y, u.pos.z)};
}
}  // namespace

TEST(a_fight_on_the_footbridge_leaves_no_one_in_the_river) {
    const Deck& d = footbridge();
    CHECK(d.foot);
    WorldConfig cfg;
    cfg.region = fordRegion();
    cfg.seed = 7;
    World w(cfg);
    const Vec3 side{-d.dir.y, d.dir.x, Fixed::zero()};
    const Vec3 a = d.centre - d.dir * (d.halfSpan + m(14)), b = d.centre + d.dir * (d.halfSpan + m(14));
    std::vector<EntityId> ids;
    std::vector<Command> orders;
    // Twelve a bank, each sent to the other's: they meet on a deck one man wide. Heavy horse on one side throws men about.
    for (int i = 0; i < 24; ++i) {
        const bool west = i < 12;
        const Vec3 p = (west ? a : b) + side * m(i % 4 * 2 - 3) + d.dir * m((west ? -1 : 1) * (i % 12 / 4) * 2);
        const EntityId u = w.spawnUnit(west ? 0 : 1, {p.x, p.y, m(0)}, west ? d.dir : -d.dir, unitStats(west ? UnitType::LightInfantry : UnitType::HeavyCavalry));
        ids.push_back(u);
        orders.push_back({static_cast<PlayerId>(west ? 0 : 1), u, Command::Type::Move, west ? b : a});
    }
    w.step(orders);
    int fought = 0, longestWet = 0;
    std::vector<int> wet(ids.size(), 0);
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) {
        w.step({});
        for (size_t i = 0; i < ids.size(); ++i) {
            const Unit* u = w.findUnit(ids[i]);
            if (!u || !u->alive() || u->inAir()) continue;
            const Where at = whereIs(w, *u);
            fought += at.onDeck ? 1 : 0;
            // Thrown in, he is out again within the minute (the river here is some thirty metres bank to bank).
            wet[i] = at.river ? wet[i] + 1 : 0;
            longestWet = std::max(longestWet, wet[i]);
        }
    }
    CHECK(fought > 200);  // (they did meet on it)
    std::printf("  longest in the river: %.1f s\n", longestWet / static_cast<double>(kTicksPerSecond));
    CHECK(longestWet < 60 * kTicksPerSecond);
    // Called home, every one left alive gets there: none is stood on the river's bed, under the deck or stuck beside it.
    orders.clear();
    for (const EntityId id : ids)
        if (const Unit* u = w.findUnit(id); u && u->alive()) orders.push_back({u->owner, id, Command::Type::Move, u->owner == 0 ? a : b});
    CHECK(orders.size() >= 4);
    w.step(orders);
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) w.step({});
    for (const Command& c : orders) {
        const Unit* u = w.findUnit(c.unit);
        if (!u || !u->alive()) continue;
        const Fixed left = (u->pos - c.target).flat().length();
        if (left >= m(8)) std::printf("  unit %u is %.1f m from home\n", static_cast<unsigned>(u->id), left.toDouble());
        CHECK(left < m(8));
        CHECK(!whereIs(w, *u).river);
    }
}

TEST(a_rider_sent_over_from_beside_the_footbridge_crosses_on_its_deck) {
    const Deck& d = footbridge();
    WorldConfig cfg;
    cfg.region = fordRegion();
    World w(cfg);
    const Vec3 side{-d.dir.y, d.dir.x, Fixed::zero()};
    // From three metres to the side of the way onto it: the straight line to the far bank runs in under the deck.
    const Vec3 from = d.centre - d.dir * (d.halfSpan + m(13)) - side * m(3), to = d.centre + d.dir * (d.halfSpan + m(14));
    const EntityId id = w.spawnUnit(0, {from.x, from.y, m(0)}, d.dir, unitStats(UnitType::LightCavalry));
    const Command go{0, id, Command::Type::Move, to};
    w.step(std::span(&go, 1));
    int onDeck = 0;
    for (int t = 0; t < 60 * kTicksPerSecond && w.findUnit(id)->hasMoveTarget; ++t) {
        w.step({});
        const Unit& u = *w.findUnit(id);
        const Where at = whereIs(w, u);
        CHECK(!at.river);
        onDeck += at.onDeck ? 1 : 0;
        // Over the water he is on the planks, not on the river's bed.
        if (w.region()->underWater(u.pos.x, u.pos.y)) CHECK(u.pos.z > d.centre.z - Fixed::one());
    }
    CHECK(onDeck > kTicksPerSecond);
    CHECK((w.findUnit(id)->pos - to).flat().length() < m(2));
}

TEST(a_man_in_deep_water_swims_to_the_bank_and_marches_on) {
    WorldConfig cfg;
    cfg.region = fordRegion();
    World w(cfg);
    const Region& rg = *w.region();
    // The deepest water within reach of the bridge, clear of its deck.
    const Deck& d = footbridge();
    Vec3 deep{};
    bool found = false;
    for (int y = -60; y <= 60 && !found; y += 2)
        for (int x = -60; x <= 60 && !found; x += 2) {
            const Vec3 p{d.centre.x + m(x), d.centre.y + m(y), Fixed::zero()};
            if (rg.groundAt(p.x, p.y) != Ground::DeepWater || rg.groundAt(p.x + m(3), p.y) != Ground::DeepWater || rg.groundAt(p.x - m(3), p.y) != Ground::DeepWater) continue;
            deep = p;
            found = true;
        }
    if (!found) {
        std::printf("  (no deep water by the footbridge: the river there is all shallows)\n");
        return;
    }
    const EntityId id = w.spawnUnit(0, deep, d.dir, unitStats(UnitType::LightInfantry));
    const Vec3 goal = d.centre - d.dir * (d.halfSpan + m(14));
    const Command go{0, id, Command::Type::Move, goal};
    w.step(std::span(&go, 1));
    Fixed surface;
    CHECK(rg.waterSurface(deep.x, deep.y, surface));
    // He swims: his head above the water, not walking the bed.
    const Unit& u = *w.findUnit(id);
    CHECK(w.inRiver(u.pos.x, u.pos.y, u.pos.z));
    CHECK(u.pos.z + u.stats.height > surface);
    int ticks = 0;
    for (; ticks < 90 * kTicksPerSecond && w.inRiver(u.pos.x, u.pos.y, u.pos.z); ++ticks) w.step({});
    CHECK(!w.inRiver(u.pos.x, u.pos.y, u.pos.z));
    std::printf("  ashore in %.1f s\n", ticks / static_cast<double>(kTicksPerSecond));
    for (int t = 0; t < 120 * kTicksPerSecond && u.hasMoveTarget; ++t) w.step({});
    CHECK((u.pos - goal).flat().length() < m(3));
}
