#include <cmath>
#include <cstdio>

#include "illyria/game/river_ribbons.h"
#include "shape_audit.h"
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

namespace {
// Is this point (battle-local, as the ribbons are) within 2 m of open lake water?
bool byLake(const sim::Continent& cont, const V3& v) {
    for (int k = 0; k < 8; ++k) {
        const float a = static_cast<float>(k) * 0.7853982f;
        const sim::Continent::Sample n = cont.sample(toFixed(v.x + 2.0f * std::cos(a)), toFixed(v.y + 2.0f * std::sin(a)));
        if (n.inLake && n.height < n.lakeSurface) return true;
    }
    return false;
}
}  // namespace

// Before: the Heartmere's three rivers (and the Emberflow's) were drawn right across it, dark ribbons in a fork over the lake's own water.
// A river now ends at the lake's shore (the sim's course goes on under the lake, where the water is the lake's).
TEST(river_ribbons_end_at_a_lakes_shore_and_never_cross_its_water) {
    int endings = 0;
    for (const uint64_t seed : {20261027ull, 1ull, 7ull, 424242ull, 99999ull}) {
        const sim::Continent cont(seed);
        const std::vector<RibbonSegment> segs = riverRibbons(cont, V3{});
        const double over = shapeaudit::ribbonMetresInLake(cont, segs, V3{});
        std::printf("  seed %llu: %zu ribbon pieces, %.1f m over lake water\n", static_cast<unsigned long long>(seed), segs.size(), over);
        CHECK(over < 1.0);
        for (const RibbonSegment& sg : segs)
            for (const V3& v : {sg.a, sg.b}) {
                const sim::Continent::Sample here = cont.sample(toFixed(v.x), toFixed(v.y));
                if (!(here.inLake && here.height < here.lakeSurface) && byLake(cont, v)) ++endings;
            }
    }
    std::printf("  %d ribbon ends at a lake's shore\n", endings);
    CHECK(endings >= 8);
}

// A river's mouth on a lake is wider than the river a little way up: a small delta, not a pipe's end.
TEST(a_river_opens_out_where_it_meets_a_lake) {
    const sim::Continent cont(20261027ull);
    const std::vector<RibbonSegment> segs = riverRibbons(cont, V3{});
    int opened = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        const RibbonSegment& sg = segs[i];
        if (sg.toMouthB < RibbonSegment::kNoMouth) continue;
        // A mouth at a piece's far end (the river runs into the lake) or at its near end (it leaves the lake): the piece a
        // little over 100 m along the river from the shore is narrower.
        float inland = 0;
        if (byLake(cont, sg.b)) {  // the river runs into the lake here: look back along it
            for (size_t k = i; k-- > 0 && inland == 0;)
                if (std::hypot(segs[k].a.x - sg.b.x, segs[k].a.y - sg.b.y) > 100.0f) inland = segs[k].halfWidth;
            if (inland > 0 && sg.halfWidth > 1.4f * inland) ++opened;
        }
        inland = 0;
        if (byLake(cont, sg.a)) {  // it leaves the lake here: look ahead
            for (size_t k = i + 1; k < segs.size() && inland == 0; ++k)
                if (std::hypot(segs[k].b.x - sg.a.x, segs[k].b.y - sg.a.y) > 100.0f) inland = segs[k].halfWidth;
            if (inland > 0 && sg.halfWidth > 1.4f * inland) ++opened;
        }
    }
    std::printf("  %d mouths on a lake opened out\n", opened);
    CHECK(opened >= 2);
}
