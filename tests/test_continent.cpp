#include <cstdio>
#include <memory>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/scenario.h"
#include "test.h"

using namespace illyria::sim;

namespace {
constexpr uint64_t kSeed = 20261027;

const Continent& continent() {
    static const Continent c(kSeed);
    return c;
}
// A small window at Oakhaven Ford keeps the test fast.
std::shared_ptr<const Region> fordWindow() {
    static const auto region =
        std::make_shared<const Region>(Region::fromContinent(continent(), continent().crossings().front().pos, 128));
    return region;
}
}  // namespace

TEST(continent_rivers_run_downhill_to_the_sea) {
    for (const Continent::River& r : continent().rivers()) {
        CHECK(r.points.size() >= 2);
        for (size_t i = 0; i + 1 < r.points.size(); ++i) CHECK(r.points[i + 1].z <= r.points[i].z);
    }
    // The western sea, the eastern mountains.
    CHECK(continent().heightAt(Fixed::fromInt(200), Fixed::fromInt(8000)) < Fixed::zero());
    Fixed highest = Fixed::zero();
    for (int y = 1000; y < Continent::kHeight; y += 500)
        highest = std::max(highest, continent().heightAt(Fixed::fromInt(Continent::kWidth - 600), Fixed::fromInt(y)));
    CHECK(highest > Fixed::fromInt(150));
}

TEST(continent_crossings_are_passable) {
    for (const Continent::Crossing& c : continent().crossings()) {
        const Ground g = continent().groundAt(c.pos.x, c.pos.y);
        if (g == Ground::DeepWater) std::printf("  %s (%d) is deep water\n", c.name.c_str(), static_cast<int>(c.kind));
        CHECK(g != Ground::DeepWater);
    }
    const Region& r = *fordWindow();
    // The window is centred on the ford, which is shallow enough to wade.
    const Fixed mid = Fixed::fromInt(128 * 4 / 2);
    CHECK(r.groundAt(mid, mid) != Ground::DeepWater);
    int wet = 0;
    for (Fixed w : r.waterSurfaces()) wet += w.raw != Region::kDryRaw;
    CHECK(wet > 100);
}

TEST(continent_window_is_deterministic_and_pinned) {
    const Region again = Region::fromContinent(continent(), continent().crossings().front().pos, 128);
    CHECK(again.checksum() == fordWindow()->checksum());
    // Every platform must generate exactly this world.
    constexpr uint64_t kGolden = 0x5822664e77864b03;
    if (fordWindow()->checksum() != kGolden)
        std::printf("  continent window checksum: 0x%016llx\n", static_cast<unsigned long long>(fordWindow()->checksum()));
    CHECK(fordWindow()->checksum() == kGolden);
}

TEST(oakhaven_battle_is_deterministic_and_pinned) {
    const auto run = [](uint64_t seed) {
        const Replay r = scenarios::Oakhaven::replay(seed);
        World w(r.config);
        scenarios::Oakhaven::setup(w);
        runReplay(w, r, scenarios::Oakhaven::kTicks);
        int dead = 0;
        for (const Unit& u : w.units()) dead += !u.alive();
        CHECK(dead > 5);  // they actually meet
        return w.checksum();
    };
    const uint64_t a = run(1234);
    CHECK(a == run(1234));
    constexpr uint64_t kGolden = 0xf3dca3755508bbd0;
    if (a != kGolden) std::printf("  Oakhaven checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}

TEST(continent_has_cities_castles_and_places_to_explore) {
    const Continent& c = continent();
    CHECK(c.cities().size() == 80);
    int capitals[Continent::kFactionCount] = {};
    for (const Continent::City& city : c.cities()) {
        if (city.capital) ++capitals[city.owner];
        else CHECK(city.owner == Continent::kNeutral);
        // On dry land, and never inside another city's walls.
        Fixed surface;
        CHECK(!c.waterSurface(city.pos.x, city.pos.y, surface));
        CHECK(c.heightAt(city.pos.x, city.pos.y) == city.pos.z);  // the levelled pad
        for (const Continent::City& other : c.cities())
            if (&other != &city) CHECK((other.pos - city.pos).flat().length() > Fixed::fromInt(500));
    }
    for (int f = 0; f < Continent::kFactionCount; ++f) CHECK(capitals[f] == 1);
    int kinds[3] = {};
    for (const Continent::Site& s : c.sites()) {
        ++kinds[static_cast<int>(s.kind)];
        Fixed surface;
        CHECK(!c.waterSurface(s.pos.x, s.pos.y, surface));
    }
    CHECK(kinds[0] == 34 && kinds[1] == 4 && kinds[2] == 2);  // ruins, temples, sages
    constexpr uint64_t kGolden = 0xafd7e11dfb465651;
    if (c.checksum() != kGolden) std::printf("  continent checksum: 0x%016llx\n", static_cast<unsigned long long>(c.checksum()));
    CHECK(c.checksum() == kGolden);
}

TEST(battlefield_is_varied_not_open_grass) {
    // The full 1 km battlefield: ponds, fallen logs, groves and rock outcrops, not empty grass.
    static const Region r = Region::fromContinent(continent(), continent().crossings().front().pos, 256);
    // (where the drainage leaves the ground dry and gentle enough to hold one)
    CHECK(r.ponds().size() >= 3);
    int logs = 0, trees = 0, boulders = 0;
    for (const Obstacle& o : r.obstacles()) {
        logs += o.kind == Obstacle::Kind::Log;
        trees += o.kind == Obstacle::Kind::Oak || o.kind == Obstacle::Kind::Pine;
        boulders += o.kind == Obstacle::Kind::Boulder;
        CHECK(o.kind != Obstacle::Kind::Log || !o.solid);  // stepped over
    }
    CHECK(logs > 200);
    CHECK(trees > 2000);
    CHECK(boulders > 200);
    // Every pond holds water.
    for (const Vec3& p : r.ponds()) {
        const Ground g = r.groundAt(p.x, p.y);
        CHECK(g == Ground::Shallows || g == Ground::DeepWater);
    }
}
