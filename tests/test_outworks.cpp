// Outworks (docs/MAP.md "Outworks", docs/GAME_DESIGN.md "Outworks and timber"): a fair share of the castles have a ditch,
// a bank with a palisade, abatis and watchtowers; the ditch bars horse and wheels and slows foot.
#include <cstdio>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
}  // namespace

TEST(outworks_are_fair_every_realm_its_seat_and_one_town) {
    const auto& cs = continent().cities();
    int seats[Continent::kFactionCount] = {}, towns[Continent::kFactionCount] = {}, moats = 0, all = 0;
    for (const Continent::City& c : cs) {
        if (!c.fortified()) continue;
        ++all;
        CHECK(c.walled);
        CHECK(c.realm < Continent::kFactionCount);
        if (c.realm >= Continent::kFactionCount) continue;
        (c.capital ? seats : towns)[c.realm] += 1;
        moats += c.ditch.wet;
        int pal = 0, aba = 0, tow = 0, eng = 0;
        for (const Work& w : c.works) {
            pal += w.kind == Work::Kind::Palisade;
            aba += w.kind == Work::Kind::Abatis;
            tow += w.kind == Work::Kind::Watchtower;
            eng += w.kind == Work::Kind::Engine;
        }
        std::printf("  %-14s realm %d %s ditch %.0f-%.0f m%s, %d causeways; palisade %d, abatis %d, towers %d, engines %d\n", c.name.c_str(), c.realm,
                    c.capital ? "seat" : "town", c.ditch.inner.toDouble(), c.ditch.outer.toDouble(), c.ditch.wet ? " (moat)" : "",
                    static_cast<int>(c.ditch.causeways.size()), pal, aba, tow, eng);
        CHECK(pal > 20);
        CHECK(aba > 6);
        CHECK(tow >= 2);
        CHECK(eng == (c.capital ? 2 : 1));
    }
    for (int f = 0; f < Continent::kFactionCount; ++f) {
        CHECK(seats[f] == 1);
        CHECK(towns[f] == 1);
    }
    std::printf("  %d fortified, %d with a moat\n", all, moats);
    // Every other place has a watchtower: the walled towns by the road before the gate, the villages at their lane's end.
    for (const Continent::City& c : cs) {
        if (c.fortified()) continue;
        int tow = 0;
        for (const Work& w : c.works) tow += w.kind == Work::Kind::Watchtower;
        CHECK(tow == 1);
    }
}

TEST(a_ditch_is_dug_and_its_causeways_carry_the_roads) {
    for (const Continent::City& c : continent().cities()) {
        if (!c.fortified()) continue;
        const Fixed mid = (c.ditch.inner + c.ditch.outer) / 2;
        int cut = 0, n = 0;
        for (int a = 0; a < 48; ++a) {
            const Vec3 dir{cos(kTwoPi * a / 48), sin(kTwoPi * a / 48), Fixed::zero()};
            const Vec3 p = c.pos + dir * mid;
            if (c.ditch.causeway(p.x, p.y)) continue;
            ++n;
            const Vec3 bank = c.pos + dir * (c.ditch.inner - Fixed::fromInt(2));
            const Fixed below = continent().heightAt(bank.x, bank.y) - continent().heightAt(p.x, p.y);
            cut += below > Fixed::fromInt(2);
        }
        // (Where a river runs by, it is the moat: no ditch is dug there.)
        CHECK(cut * 10 >= n * 6);
        // The road to the gate crosses on a causeway: the ground there is a road, not a ditch.
        const auto& ring = c.castle.rings.front();
        const Vec3 gm = (ring.corners[ring.gateEdge] + ring.corners[(ring.gateEdge + 1) % ring.corners.size()]) * Fixed::fromRatio(1, 2);
        const Vec3 out = (gm - c.pos).flat() / (gm - c.pos).flat().length();
        const Vec3 over = c.pos + out * mid;
        CHECK(c.ditch.causeway(over.x, over.y));
        CHECK(!c.ditch.holds(over.x, over.y));
    }
}
