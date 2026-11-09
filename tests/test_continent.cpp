#include <cstdio>
#include <memory>

#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
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
    CHECK(continent().heightAt(Fixed::fromInt(44), Fixed::fromInt(1778)) < Fixed::zero());
    Fixed highest = Fixed::zero();
    for (int y = 222; y < Continent::kHeight; y += 111)
        highest = std::max(highest, continent().heightAt(Fixed::fromInt(Continent::kWidth - 133), Fixed::fromInt(y)));
    CHECK(highest > Continent::peakHeight(150));
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
    // Every platform must generate exactly this world. (It holds a ruin: its
    // stones, floor, stair and stepped base come from the ruin's model, on levelled ground.)
    constexpr uint64_t kGolden = 0xfbfc83023e84c213;  // (was 0xcfc93f2b944829e5 before rocks and boulders stood in the water (Region::waterRockAt: in lakes, rivers and the sea, some breaking the surface and solid, some just under it); was was 0xa7d6a45fbe64f32b before the roads (classes, junctions, bridges that meet the ground) and the lakes (natural shores, level river surfaces) were merged, with the castles' houses no longer walling their ramps off; was 0xc99f5d588c1bbf5b before roads of classes by traffic (a lane to four wide, `Polyline::widths` read by the ground and the drawing), clean junctions (no spurs or loops), bends round boulders, bridges with ramps to the ground and no bridge under a castle's wall; was 0x3343cdcad97e5671 before the glade round ruins (nothing grows within 46 m); was 0x39dd9c7350e9e73c before roads laid to every castle's gate, the gates facing their roads, no town astride a road and a rule for towers)
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
    constexpr uint64_t kGolden = 0xe9778f49da3937dc;  // (was 0x8f6c6648f0c4c0e2 before rocks and boulders stood in the water (Region::waterRockAt: in lakes, rivers and the sea, some breaking the surface and solid, some just under it); was was 0xd50623761ef8d74b before the roads (classes, junctions, bridges that meet the ground) and the lakes (natural shores, level river surfaces) were merged, with the castles' houses no longer walling their ramps off; was 0xedd577b00cdc845c before roads of classes by traffic (a lane to four wide, `Polyline::widths` read by the ground and the drawing), clean junctions (no spurs or loops), bends round boulders, bridges with ramps to the ground and no bridge under a castle's wall; was 0x63757b655730c08f before roads laid to every castle's gate, the gates facing their roads, no town astride a road and a rule for towers; was 0x9aa2d45873d4c28f before every gate's fields were hashed always)
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
        // On dry land, and at least 30 m from another city's walls.
        Fixed surface;
        CHECK(!c.waterSurface(city.pos.x, city.pos.y, surface));
        CHECK(c.heightAt(city.pos.x, city.pos.y) == city.pos.z);  // the levelled pad
        for (const Continent::City& other : c.cities())
            if (&other != &city) CHECK((other.pos - city.pos).flat().length() - other.radius - city.radius >= Fixed::fromInt(30));
    }
    for (int f = 0; f < Continent::kFactionCount; ++f) CHECK(capitals[f] == 1);
    int kinds[3] = {};
    for (const Continent::Site& s : c.sites()) {
        ++kinds[static_cast<int>(s.kind)];
        Fixed surface;
        CHECK(!c.waterSurface(s.pos.x, s.pos.y, surface));
    }
    CHECK(kinds[0] == 34 && kinds[1] == 4 && kinds[2] == 2);  // ruins, temples, sages
    constexpr uint64_t kGolden = 0xb1d368e873a52b14;  // (was 0xe77402f751ea576e before the roads (classes, junctions, bridges that meet the ground) and the lakes (natural shores, level river surfaces) were merged, with the castles' houses no longer walling their ramps off; was 0x84c2eca1f008d047 before roads of classes by traffic (a lane to four wide, `Polyline::widths` read by the ground and the drawing), clean junctions (no spurs or loops), bends round boulders, bridges with ramps to the ground and no bridge under a castle's wall; was 0x898fe2f7beec6015 before roads laid to every castle's gate, the gates facing their roads, no town astride a road and a rule for towers)
    if (c.checksum() != kGolden) std::printf("  continent checksum: 0x%016llx\n", static_cast<unsigned long long>(c.checksum()));
    CHECK(c.checksum() == kGolden);
}

TEST(battlefield_is_varied_not_open_grass) {
    // The full 1 km battlefield: ponds, fallen logs, groves and rock outcrops, not empty grass.
    static const Region r = Region::fromContinent(continent(), continent().crossings().front().pos, 256);
    // (where the drainage leaves the ground dry and gentle enough to hold one,
    // and clear of the towns, which on a map a third the size stand all
    // round the ford's open ground)
    CHECK(r.ponds().size() >= 1);
    int logs = 0, trees = 0, boulders = 0;
    for (const Obstacle& o : r.obstacles()) {
        logs += o.kind == Obstacle::Kind::Log;
        trees += o.kind == Obstacle::Kind::Oak || o.kind == Obstacle::Kind::Pine;
        boulders += o.kind == Obstacle::Kind::Boulder;
        CHECK(o.kind != Obstacle::Kind::Log || !o.solid);  // stepped over
    }
    std::printf("  %zu ponds, %d logs, %d trees, %d boulders\n", r.ponds().size(), logs, trees, boulders);
    // (A dozen towns' walls and pads stand in it too, on a map a third the size.)
    CHECK(logs > 100);
    CHECK(trees > 1500);
    CHECK(boulders > 200);
    // Every pond holds water.
    for (const Vec3& p : r.ponds()) {
        const Ground g = r.groundAt(p.x, p.y);
        CHECK(g == Ground::Shallows || g == Ground::DeepWater);
    }
}

TEST(landmarks_are_shaped_and_can_be_walked) {
    // The set pieces made by hand, and the gorges found where there is room.
    const Continent& c = continent();
    const Continent::Landmark *church = nullptr, *narrows = nullptr, *terraces = nullptr;
    int gorges = 0;
    for (const Continent::Landmark& lm : c.landmarks()) {
        if (lm.name == "Church Hill") church = &lm;
        if (lm.name == "The Narrows") narrows = &lm;
        if (lm.name == "The Terraces") terraces = &lm;
        gorges += lm.kind == Continent::Landmark::Kind::Gorge;
    }
    CHECK(church && narrows && terraces);
    // (Gorges found by search need room the 80 towns leave none of on this
    // map, a third the size it was: the Narrows may be the only one.)
    CHECK(gorges >= 1);
    if (!church || !narrows || !terraces) return;
    const auto local = [](const Region& rg, const Vec3& p) { return Vec3{p.x - rg.origin().x, p.y - rg.origin().y, Fixed::zero()}; };
    std::vector<Vec3> path;

    // Church Hill: its road climbs no steeper than 1 in 10 (a walk, not a
    // scramble), its top stands 40 m over the land with the church and a pond
    // on it, and from the foot of the road a soldier can walk up to the church.
    for (size_t i = 0; i + 1 < church->line.size(); ++i) {
        const Fixed run = (church->line[i + 1] - church->line[i]).flat().length(), rise = church->line[i + 1].z - church->line[i].z;
        CHECK(rise >= Fixed::zero() && rise <= run / 10 + Fixed::fromRatio(1, 20));
    }
    CHECK(church->buildings.size() >= 2);
    CHECK(c.heightAt(church->line.back().x, church->line.back().y) > church->centre.z + Fixed::fromInt(35));
    bool pond = false;
    for (const Continent::Area& lake : c.lakes()) {
        Fixed surface;
        if (lake.name == "Church Hill Pond") pond = c.waterSurface(lake.centre.x, lake.centre.y, surface);
    }
    CHECK(pond);
    {
        const Region rg = Region::fromContinent(c, church->centre, 256);
        const NavGrid nav(rg);
        CHECK(nav.findPath(local(rg, church->line.front()), local(rg, church->line.back()), path));
        CHECK(!nav.blocked(local(rg, church->line[church->line.size() / 2])));  // the road itself is open
    }

    // The Narrows: its floor, the road, lies in a cleft 15 m and more below the
    // tableland either side, and stays open end to end.
    {
        const Region rg = Region::fromContinent(c, narrows->centre, 256);
        const NavGrid nav(rg);
        const Vec3 mid = local(rg, narrows->centre), side{-narrows->axis.y, narrows->axis.x, Fixed::zero()};
        const Fixed floor = rg.terrain().heightAt(mid.x, mid.y);
        for (int s = -1; s <= 1; s += 2) {
            const Vec3 rim = mid + side * (narrows->floor * 2 + Fixed::fromInt(30)) * Fixed::fromInt(s);
            CHECK(rg.terrain().heightAt(rim.x, rim.y) > floor + Fixed::fromInt(15));
        }
        for (const Vec3& p : narrows->line)
            if ((p - narrows->centre).flat().length() < narrows->halfLength) CHECK(!nav.blocked(local(rg, p)));
        CHECK(nav.findPath(local(rg, narrows->line.front()), local(rg, narrows->line.back()), path));
    }

    // The Terraces: their top can be walked up to from the fields below.
    {
        const Region rg = Region::fromContinent(c, terraces->centre, 256);
        const NavGrid nav(rg);
        const Vec3 top = local(rg, terraces->centre), below = top + Vec3{terraces->radius + Fixed::fromInt(40), Fixed::zero(), Fixed::zero()};
        CHECK(rg.terrain().heightAt(top.x, top.y) > terraces->centre.z + Fixed::fromInt(12));
        CHECK(nav.findPath(below, top, path));
    }
}
