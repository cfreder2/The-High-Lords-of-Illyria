// What the water does with the dead (engine/sim/src/world_drift.cpp): bodies and parts float down a river, ground
// in the shallows, sink in armour, and it is all the simulation's own (fixed point, in the checksum).
#include <chrono>
#include <cstdio>
#include <memory>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};

// The Ford's ground: the river runs south from (552, 600) to the ford at (560, 512), 3.2 m deep but for the ford's
// 0.6 m, and on to the lake about (215, 155).
std::shared_ptr<const Region> ground() {
    static const auto region = std::make_shared<const Region>(Region::generateIllyria(20261027));
    return region;
}
const Vec3 kMidstream{m(555), m(570), m(0)};
const Vec3 kLake{m(170), m(120), m(0)};  // (out of the river's reach)

World world(uint64_t seed = 7) {
    WorldConfig cfg;
    cfg.seed = seed;
    cfg.region = ground();
    return World(cfg);
}

// A man put in the water there and struck dead where he stands.
EntityId drown(World& w, UnitType type, Vec3 at) {
    at.z = w.groundHeight(at.x, at.y);
    const EntityId id = w.spawnUnit(0, at, kEast, unitStats(type));
    Blow blow;
    blow.type = DamageType::Blunt;
    blow.dir = kEast;
    blow.at = at;
    // (Blow by blow: one far past death would tear him apart, and there would be no body.)
    while (w.findUnit(id)->alive()) {
        w.testBlow(id, 8, blow);
        w.step({});
    }
    return id;
}

void run(World& w, int seconds) {
    for (int i = 0; i < seconds * kTicksPerSecond; ++i) w.step({});
}

}  // namespace

TEST(a_body_in_a_river_floats_downstream) {
    World w = world();
    CHECK(w.waterDepth(kMidstream.x, kMidstream.y) > m(2));
    const Vec3 flow = w.flowAt(kMidstream.x, kMidstream.y);
    CHECK(flow.y < -Fixed::one());  // south, toward the ford, at a walking pace
    const EntityId id = drown(w, UnitType::Archers, kMidstream);
    const Unit& u = *w.findUnit(id);
    CHECK(!u.alive());
    CHECK(u.afloat);
    const Vec3 from = u.pos;
    run(w, 10);
    std::printf("  ten seconds: %.1f m downstream, %.1f m across\n", (from.y - u.pos.y).toDouble(), (u.pos.x - from.x).toDouble());
    CHECK(u.afloat);
    CHECK(from.y - u.pos.y > m(8));
    CHECK(from.y - u.pos.y < m(20));
    // He rides at the surface, over water all the way.
    CHECK(u.pos.z == ground()->waterSurfaceAt(u.pos.x, u.pos.y));
    CHECK(w.waterDepth(u.pos.x, u.pos.y) >= World::draughtOf(u.stats));
}

TEST(a_body_grounds_where_the_water_is_too_thin_for_it) {
    // The ford is 0.6 m deep in the middle and shelves to nothing: a man killed in its thin water lies where he fell.
    World w = world();
    const Fixed draught = World::draughtOf(unitStats(UnitType::Archers));
    const Vec3 ford = ground()->ford();
    Vec3 thin = ford;
    bool found = false;
    for (int dx = 0; dx < 20 && !found; ++dx) {
        thin = ford + Vec3{m(dx), m(0), m(0)};
        const Fixed d = w.waterDepth(thin.x, thin.y);
        found = d > Fixed::fromRatio(1, 10) && d < draught;
    }
    CHECK(found);
    const EntityId lying = drown(w, UnitType::Archers, thin);
    const Vec3 fell = w.findUnit(lying)->pos;
    run(w, 20);
    CHECK(!w.findUnit(lying)->afloat);
    CHECK(w.findUnit(lying)->pos == fell);
    // One who comes down the middle of the river drags over the ford's crest at a fraction of the stream's pace, and
    // either lodges on it or is let go below it: he is never carried over ground the water does not cover to his draught.
    World w2 = world();
    const EntityId man = drown(w2, UnitType::Archers, kMidstream);
    const Unit& a = *w2.findUnit(man);
    Fixed slowest = m(100), fastest;
    for (int s = 0; s < 150; ++s) {
        const Vec3 before = a.pos;
        run(w2, 1);
        if (!a.afloat) break;
        CHECK(w2.waterDepth(a.pos.x, a.pos.y) >= draught);
        const Fixed pace = (a.pos - before).flat().length();
        if ((a.pos - ford).flat().length() < m(8)) slowest = min(slowest, pace);
        fastest = max(fastest, pace);
    }
    std::printf("  the archer: %.2f m/s in the stream, %.2f m/s over the ford; after 150 s at (%.1f, %.1f), %s\n", fastest.toDouble(), slowest.toDouble(),
                a.pos.x.toDouble(), a.pos.y.toDouble(), a.afloat ? "afloat" : "at rest");
    CHECK(fastest > Fixed::one());
    CHECK(slowest < fastest * 3 / 5);
    CHECK(a.pos.y < kMidstream.y - m(40));  // he came down to the ford at least
    if (!a.afloat) CHECK(a.pos.z == ground()->terrain().heightAt(a.pos.x, a.pos.y));  // on the bed
}

TEST(an_armoured_man_sinks_in_deep_water_and_lies_on_the_bed) {
    World w = world();
    const EntityId id = drown(w, UnitType::HeavyInfantry, kMidstream);
    const Unit& u = *w.findUnit(id);
    CHECK(armoured(u.stats));
    CHECK(u.afloat);
    run(w, 5);
    CHECK(u.afloat);  // carried a while first
    run(w, 15);
    CHECK(!u.afloat);
    CHECK(w.waterDepth(u.pos.x, u.pos.y) > Fixed::one());
    CHECK(u.pos.z == ground()->terrain().heightAt(u.pos.x, u.pos.y));
    CHECK(u.pos.y < kMidstream.y - m(5));
    const Vec3 rest = u.pos;
    run(w, 10);
    CHECK(u.pos == rest);
}

TEST(still_water_bears_a_body_and_barely_moves_it) {
    World w = world();
    CHECK(w.flowAt(kLake.x, kLake.y) == Vec3{});
    const EntityId id = drown(w, UnitType::Archers, kLake);
    const Unit& u = *w.findUnit(id);
    CHECK(u.afloat);
    const Vec3 from = u.pos;
    run(w, 30);
    CHECK(u.afloat);
    const Fixed moved = (u.pos - from).flat().length();
    CHECK(moved > Fixed::zero());
    CHECK(moved < m(3));
}

TEST(a_man_killed_on_dry_land_lies_where_he_fell) {
    World w = world();
    const Vec3 dry{m(300), m(600), m(0)};
    const EntityId id = drown(w, UnitType::Archers, dry);
    const Unit& u = *w.findUnit(id);
    const Vec3 fell = u.pos;
    run(w, 5);
    CHECK(!u.afloat);
    CHECK(u.pos == fell);
}

TEST(parts_float_and_drift_and_arrows_and_blades_sink) {
    World w = world();
    const Fixed surface = ground()->waterSurfaceAt(kMidstream.x, kMidstream.y);
    const Vec3 over{kMidstream.x, kMidstream.y, surface + m(2)};
    w.launchPart(RemainKind::Head, 0, over, {});
    w.launchPart(RemainKind::Blade, 0, over + Vec3{m(1), m(0), m(0)}, {});
    w.launchPart(RemainKind::Bone, 0, over + Vec3{-m(1), m(0), m(0)}, {});
    int splashes = 0;
    for (int i = 0; i < 2 * kTicksPerSecond; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) {
            if (e.kind != ImpactEvent::Kind::Splash) continue;
            ++splashes;
            CHECK(e.pos.z == surface);
        }
    }
    CHECK(splashes == 3);  // each goes in with one
    CHECK(w.projectiles().empty());
    int heads = 0;
    Vec3 at;
    for (const Remain& r : w.remains()) {
        if (r.kind == RemainKind::None) continue;
        CHECK(r.kind == RemainKind::Head);  // the blade and the bone went down
        CHECK(r.afloat);
        at = r.pos;
        ++heads;
    }
    CHECK(heads == 1);
    run(w, 10);
    for (const Remain& r : w.remains()) {
        if (r.kind != RemainKind::Head) continue;
        CHECK(at.y - r.pos.y > m(8));
        CHECK(r.pos.z == ground()->waterSurfaceAt(r.pos.x, r.pos.y));
    }
}

TEST(the_drift_is_the_simulations_own) {
    // The same battle in the water twice: the same checksum every second; and the floating is in it.
    const auto fill = [](World& w) {
        drown(w, UnitType::Archers, kMidstream);
        drown(w, UnitType::HeavyInfantry, kMidstream + Vec3{m(3), m(8), m(0)});
        drown(w, UnitType::Giants, kMidstream + Vec3{-m(2), m(14), m(0)});
        drown(w, UnitType::LightCavalry, kLake);
        w.launchPart(RemainKind::Torso, 1, kMidstream + Vec3{m(1), m(20), m(6)}, {m(1), -m(2), m(3)});
        w.launchPart(RemainKind::Arm, 1, kMidstream + Vec3{-m(1), m(24), m(6)}, {-m(1), m(2), m(4)});
    };
    World a = world(), b = world();
    fill(a);
    fill(b);
    for (int s = 0; s < 60; ++s) {
        run(a, 1);
        run(b, 1);
        CHECK(a.checksum() == b.checksum());
    }
    // A body carried is not the body where it fell.
    World c = world();
    const EntityId id = drown(c, UnitType::Archers, kMidstream);
    const uint64_t fell = c.checksum();
    const Vec3 at = c.findUnit(id)->pos;
    run(c, 1);
    CHECK(c.findUnit(id)->pos != at);
    CHECK(c.checksum() != fell);
}

TEST(a_stone_bridge_stands_on_piers_a_body_lodges_against) {
    // The land about Oakhaven Ford: its road bridge is stone, a bay every eleven metres or so on piers 1.8 m thick
    // (game/render/meshes.cpp draws them; World::pierAt is where the simulation has them).
    static const Continent continent(20261027);
    WorldConfig cfg;
    cfg.region = std::make_shared<const Region>(Region::fromContinent(continent, continent.crossings().front().pos, 256));
    World w(cfg);
    int bridges = 0;
    for (const Deck& d : cfg.region->decks()) {
        if (d.foot || d.raised || d.ramp || d.arc || d.halfSpan < m(9)) continue;
        ++bridges;
        const Fixed step = Fixed::fromRatio(1, 8);
        int piers = 0;
        Fixed width, widest;
        bool in = false;
        for (Fixed s = -d.halfSpan; s <= d.halfSpan; s += step) {
            const bool pier = w.pierAt(d.centre.x + d.dir.x * s, d.centre.y + d.dir.y * s);
            if (pier && !in) ++piers, width = Fixed::zero();
            if (pier) width += step, widest = max(widest, width);
            in = pier;
        }
        const int64_t bays = ((d.halfSpan * 2 + Fixed::fromRatio(11, 2)) / 11).floorToInt();
        std::printf("  a stone bridge %.0f m long: %d piers, %.2f m thick\n", (d.halfSpan * 2).toDouble(), piers, widest.toDouble());
        CHECK(piers == bays - 1);
        CHECK(!in);  // it ends on a bay, not a pier
        if (piers > 0) CHECK(widest > Fixed::fromRatio(3, 2) && widest < Fixed::fromRatio(21, 10));
        // Beside the deck, beyond the cutwaters, the water is open.
        const Fixed out = d.halfWidth + m(2);
        for (Fixed s = -d.halfSpan; s <= d.halfSpan; s += Fixed::one())
            CHECK(!w.pierAt(d.centre.x + d.dir.x * s - d.dir.y * out, d.centre.y + d.dir.y * s + d.dir.x * out));
    }
    CHECK(bridges > 0);
}

TEST(carrying_the_dead_costs_little) {
    // Two hundred parts in the river at once (a great slaughter at a ford), stepped for ten seconds with nothing else
    // in the world: what the drift adds to a tick. (A fight is about 4 microseconds a man a tick.)
    World w = world();
    const Fixed surface = ground()->waterSurfaceAt(kMidstream.x, kMidstream.y);
    for (int i = 0; i < 200; ++i)
        w.launchPart(RemainKind::Arm, 0, {kMidstream.x + Fixed::fromRatio(i % 10 - 5, 1), kMidstream.y + m(i / 10) * 3, surface + Fixed::one()}, {});
    run(w, 1);
    int afloat = 0;
    for (const Remain& r : w.remains()) afloat += r.afloat;
    CHECK(afloat > 150);
    const auto t0 = std::chrono::steady_clock::now();
    run(w, 10);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (10.0 * kTicksPerSecond);
    std::printf("  %d afloat: %.1f microseconds a tick, %.2f each\n", afloat, us, us / afloat);
    CHECK(us / afloat < 25.0);  // (loose: the machine may be busy)
}
