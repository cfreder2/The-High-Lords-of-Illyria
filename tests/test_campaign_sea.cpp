// Ports and boats in the campaign, after Warlords' navies: built in coastal
// cities, carrying up to seven armies over the water at their own speed.
#include <cstdio>
#include <set>

#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// The ports furthest north and furthest south.
std::pair<int32_t, int32_t> farPorts(const Campaign& c) {
    int32_t north = -1, south = -1;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& s = c.cities()[i];
        if (!s.port) continue;
        if (north < 0 || s.cy > c.cities()[static_cast<size_t>(north)].cy) north = static_cast<int32_t>(i);
        if (south < 0 || s.cy < c.cities()[static_cast<size_t>(south)].cy) south = static_cast<int32_t>(i);
    }
    return {north, south};
}

void runDays(Campaign& c, int days) {
    for (int t = 0; t < days * Campaign::kTicksPerDay; ++t) c.step();
}

// A boat for side 0 in this port, built the honest way.
const Campaign::Boat* buildOne(Campaign& c, int32_t port) {
    if (!c.buildBoat(port)) return nullptr;
    runDays(c, Campaign::kBoatDays + 1);
    return c.boatInPort(port);
}
}  // namespace

TEST(ports_are_found_on_the_coast) {
    const Campaign c(continent(), 1);
    int ports = 0, sea = 0;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& s = c.cities()[i];
        bool near = false;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = s.cx + dx, y = s.cy + dy;
                near = near || (x >= 0 && y >= 0 && x < c.width() && y < c.height() && c.navigable(x, y));
            }
        CHECK(s.port == near);
        if (!s.port) continue;
        ++ports;
        CHECK(c.navigable(s.hx, s.hy));
        const Continent::Sample smp = continent().sample(Fixed::fromInt(Campaign::centre(s.hx)), Fixed::fromInt(Campaign::centre(s.hy)));
        sea += smp.land < Fixed::zero();
    }
    std::printf("  %d ports, %d on the open sea\n", ports, sea);
    CHECK(ports >= 5);
    CHECK(sea >= 5);
    // Navigable water is water.
    for (int y = 0; y < c.height(); ++y)
        for (int x = 0; x < c.width(); ++x)
            if (c.navigable(x, y)) CHECK(c.terrainAt(x, y) == Lay::Water);
}

TEST(a_boat_carries_at_most_its_capacity) {
    Campaign c(continent(), 2);
    c.setHuman(0, true);  // nobody else gives side 0 orders
    const int32_t port = farPorts(c).first;
    CHECK(port >= 0);
    if (port < 0) return;
    const uint32_t id = c.grant(port, 0, UnitType::HeavyInfantry, 8);
    CHECK(!c.buildBoat(-1));
    const Campaign::Boat* boat = buildOne(c, port);
    CHECK(boat != nullptr);
    if (!boat) return;
    const uint32_t boatId = boat->id;
    CHECK(c.embark(id));
    const Campaign::Stack* aboard = c.findStack(id);
    CHECK(aboard && aboard->boat == boatId);
    if (!aboard) return;
    CHECK(static_cast<int32_t>(aboard->armies.size()) == Campaign::kBoatCapacity);
    // The eighth stays on the quay.
    int ashore = 0;
    const Campaign::CityState& city = c.cities()[static_cast<size_t>(port)];
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == 0 && s.cx == city.cx && s.cy == city.cy && !s.boat) ashore += static_cast<int>(s.armies.size());
    CHECK(ashore == 1);
    CHECK(c.voyages() == 1);
    // Out to the harbour, then ashore again: the boat ties up in port.
    runDays(c, 1);
    aboard = c.findStack(id);
    CHECK(aboard && aboard->cx == city.hx && aboard->cy == city.hy);
    CHECK(c.findBoat(boatId) && c.findBoat(boatId)->stack == id);
    CHECK(c.land(id));
    runDays(c, 1);
    aboard = c.findStack(id);
    CHECK(aboard && !aboard->boat);
    CHECK(c.findBoat(boatId) && c.findBoat(boatId)->port == port);
}

TEST(a_stack_sails_to_a_far_coast_faster_than_it_walks) {
    const std::pair<int32_t, int32_t> ends = [] {
        const Campaign c(continent(), 3);
        return farPorts(c);
    }();
    const int32_t from = ends.first, to = ends.second;
    CHECK(from >= 0 && to >= 0 && from != to);
    if (from < 0 || to < 0) return;
    // The same stack, the same order: once with a boat in port, once without.
    // Every side is left to the players, who give no orders: nobody gets in the way.
    const auto voyage = [&](bool boat, bool& sailed) {
        Campaign c(continent(), 3);
        for (size_t side = 0; side < c.sides().size(); ++side) c.setHuman(static_cast<uint8_t>(side), true);
        const uint32_t id = c.grant(from, 0, UnitType::LightInfantry, 4);
        c.grant(to, 0, UnitType::LightInfantry, 0);  // ours, and empty
        if (boat) CHECK(buildOne(c, from) != nullptr);
        const Campaign::CityState& dest = c.cities()[static_cast<size_t>(to)];
        const uint32_t start = c.day();
        if (!c.move(id, dest.cx, dest.cy)) return -1;
        sailed = false;
        for (int t = 0; t < 120 * Campaign::kTicksPerDay; ++t) {
            c.step();
            const Campaign::Stack* s = c.findStack(id);
            if (!s) return 3000;
            sailed = sailed || s->boat;
            if (s->cx == dest.cx && s->cy == dest.cy) return static_cast<int>(c.day() - start);
            if (s->path.empty() && !c.move(id, dest.cx, dest.cy)) return 1000;
        }
        return 2000;
    };
    bool sailed = false, walkedSailed = false;
    const int bySea = voyage(true, sailed), byLand = voyage(false, walkedSailed);
    std::printf("  %s to %s: %d days by sea, %d on foot\n", continent().cities()[static_cast<size_t>(from)].name.c_str(),
                continent().cities()[static_cast<size_t>(to)].name.c_str(), bySea, byLand);
    CHECK(sailed);
    CHECK(!walkedSailed);
    CHECK(bySea >= 0 && bySea < 1000);
    CHECK(byLand < 0 || bySea * 2 < byLand);
}

TEST(the_computer_takes_to_the_boats) {
    Campaign c(continent(), 7);
    runDays(c, 40);
    std::set<uint8_t> sailors;
    for (const Campaign::Boat& b : c.boats()) sailors.insert(b.owner);
    std::printf("  day 40: %zu boats, %d voyages, %zu sides afloat\n", c.boats().size(), c.voyages(), sailors.size());
    CHECK(!c.boats().empty());
    CHECK(c.voyages() > 0);
    // Every boat lies in a port, rides at anchor on navigable water, or carries a stack.
    for (const Campaign::Boat& b : c.boats()) {
        if (b.port >= 0) CHECK(c.cities()[static_cast<size_t>(b.port)].owner == b.owner);
        else if (!b.stack) CHECK(c.navigable(b.cx, b.cy));
        if (b.stack) {
            const Campaign::Stack* s = c.findStack(b.stack);
            CHECK(s && s->boat == b.id && static_cast<int32_t>(s->armies.size()) <= Campaign::kBoatCapacity);
        }
    }
}

TEST(boats_are_deterministic) {
    const auto run = [] {
        Campaign c(continent(), 11);
        c.setHuman(0, true);
        const int32_t port = farPorts(c).first;
        const uint32_t id = c.grant(port, 0, UnitType::HeavyCavalry, 6);
        buildOne(c, port);
        c.embark(id);
        const Campaign::CityState& to = c.cities()[static_cast<size_t>(farPorts(c).second)];
        c.move(id, to.cx, to.cy);
        runDays(c, 30);
        return c.checksum();
    };
    const uint64_t a = run();
    CHECK(a == run());
    constexpr uint64_t kGolden = 0xc14902684e41fc28;
    if (a != kGolden) std::printf("  boats checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
