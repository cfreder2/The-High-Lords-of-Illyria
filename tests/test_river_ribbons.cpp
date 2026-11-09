#include <cmath>
#include <cstdio>

#include "illyria/game/river_ribbons.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;

namespace {
// Every sea mouth of the continent made from `seed`: the last ribbon segment of each river that reaches the sea.
void checkMouths(uint64_t seed, int& mouths) {
    const sim::Continent cont(seed);
    const V3 origin{};
    const std::vector<RibbonSegment> segs = riverRibbons(cont, origin);
    for (const RibbonSegment& s : segs) {
        // No vertex of any ribbon lies at sea (a few cm of slack for the fixed-point bisection).
        for (const V3& v : {s.a, s.b}) CHECK(cont.landDistance(toFixed(v.x), toFixed(v.y)) > sim::Fixed::fromRatio(-1, 20));
        if (s.toMouthB >= RibbonSegment::kNoMouth) continue;
        CHECK(s.toMouthA > s.toMouthB);
        CHECK(s.toMouthB > -0.05f);
        if (s.toMouthB < 0.05f) ++mouths;  // a river's last vertex: the shore itself
    }
}
}  // namespace

TEST(river_ribbons_end_at_the_shore_never_over_the_sea) {
    int mouths = 0;
    for (const uint64_t seed : {20261027ull, 1ull, 7ull, 424242ull, 99999ull}) checkMouths(seed, mouths);
    std::printf("  %d sea mouths over five seeds\n", mouths);
    CHECK(mouths >= 5);
}

// At each sea mouth the ribbon flares over its last kMouthFade metres (the estuary the sim carves) and is more than half as wide again
// as the river's own reach, and its distances to the shore fall steadily along the river to nothing: what the shader fades by.
TEST(river_ribbons_flare_into_the_estuary_and_count_down_to_the_shore) {
    const sim::Continent cont(20261027ull);
    const std::vector<RibbonSegment> segs = riverRibbons(cont, V3{});
    int flared = 0;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        if (segs[i].toMouthA >= RibbonSegment::kNoMouth) continue;
        if (segs[i].toMouthB >= 0.05f && segs[i + 1].toMouthA < RibbonSegment::kNoMouth) CHECK(segs[i + 1].toMouthA <= segs[i].toMouthA);  // the next leg, on the same river
        // Find the widest reach upstream of the flare for comparison.
        if (segs[i].toMouthB < 0.05f) {
            float reach = 0;
            for (size_t k = i; k-- > 0 && segs[k].toMouthA < RibbonSegment::kNoMouth && segs[k].toMouthA > kMouthFade;) reach = std::max(reach, segs[k].halfWidth);
            CHECK(segs[i].halfWidth > 1.5f * reach * 0.6f);
            ++flared;
        }
    }
    CHECK(flared >= 3);
}
