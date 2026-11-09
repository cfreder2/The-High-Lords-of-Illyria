// Walks the generated continent and reports what is wrong with its castles
// and the roads to them:
//   illyria_terrainaudit [seed] [-v]
// Every castle's gate: does a road reach its mouth? Is the castle joined to
// the road network? How many towers, and how many embrasures can shoot down
// onto the ground in front of each gate. The measuring is terrain_audit.h,
// shared with tests/test_terrain_audit.cpp.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "illyria/sim/continent.h"
#include "bridge_audit.h"
#include "terrain_audit.h"
#include "shape_audit.h"
#include "water_audit.h"

using namespace illyria::sim;

int main(int argc, char** argv) {
    uint64_t seed = 20261027;
    bool verbose = false;
    const char* map = nullptr;
    bool bridges = false;  // --bridges [first count]: walk every bridge (slow)
    size_t firstBridge = 0, countBridges = SIZE_MAX;
    double nearX = -1, nearY = -1, nearR = 0;  // --roads-near X,Y,R: every road's points within R metres of (X, Y)
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-v")) verbose = true;
        else if (!std::strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else if (!std::strcmp(argv[i], "--bridges")) bridges = true;
        else if (!std::strcmp(argv[i], "--bridge-range") && i + 2 < argc) firstBridge = std::strtoull(argv[i + 1], nullptr, 10), countBridges = std::strtoull(argv[i + 2], nullptr, 10), i += 2;
        else if (!std::strcmp(argv[i], "--roads-near") && i + 1 < argc) std::sscanf(argv[++i], "%lf,%lf,%lf", &nearX, &nearY, &nearR);
        else seed = std::strtoull(argv[i], nullptr, 10);
    }
    const Continent c(seed);
    if (bridges) {
        int total = 0, ok = 0;
        for (size_t i = firstBridge; i < c.crossings().size() && i < firstBridge + countBridges; ++i) {
            if (!c.crossings()[i].bridged()) continue;
            const audit::BridgeReport b = audit::auditBridge(c, i, 128, countBridges == 1);
            ++total;
            ok += b.passable();
            std::printf("bridge %3zu (%6.0f,%6.0f) %s len %5.1f half width %.1f  deck above ground at ends %5.2f / %5.2f  road off axis %4.1f / %4.1f  foot %d%d rider %d%d engine %d%d castle %5.0f m  %s\n", i, b.pos.x.toDouble(),
                        b.pos.y.toDouble(), b.foot ? "footbridge" : "bridge    ", b.length, b.halfWidth, b.rise[0], b.rise[1], b.roadOff[0], b.roadOff[1], b.walks[0][0], b.walks[0][1],
                        b.walks[1][0], b.walks[1][1], b.walks[2][0], b.walks[2][1], b.nearCastle, b.passable() ? "ok" : "BLOCKED");
            std::fflush(stdout);
        }
        std::printf("passable %d of %d bridges\n", ok, total);
        int fords = 0, fordOk = 0, noRoad = 0;
        for (size_t i = firstBridge; i < c.crossings().size() && i < firstBridge + countBridges; ++i) {
            if (c.crossings()[i].bridged()) continue;
            const audit::FordReport f = audit::auditFord(c, i, 96, countBridges == 1);
            if (!f.onRoad) {
                ++noRoad;
                std::printf("ford  %3zu (%6.0f,%6.0f) '%s': no road crosses it\n", i, f.pos.x.toDouble(), f.pos.y.toDouble(), c.crossings()[i].name.c_str());
                continue;
            }
            ++fords;
            fordOk += f.passable();
            std::printf("ford  %3zu (%6.0f,%6.0f) '%s': foot %d%d rider %d%d engine %d%d  %s\n", i, f.pos.x.toDouble(), f.pos.y.toDouble(), c.crossings()[i].name.c_str(), f.walks[0][0], f.walks[0][1], f.walks[1][0], f.walks[1][1],
                        f.walks[2][0], f.walks[2][1], f.passable() ? "ok" : "BLOCKED");
            std::fflush(stdout);
        }
        std::printf("passable %d of %d fords on a road (%d more have no road)\n", fordOk, fords, noRoad);
        return 0;
    }
    if (nearR > 0) {
        for (size_t ri = 0; ri < c.roads().size(); ++ri) {
            const Polyline& rd = c.roads()[ri];
            std::printf("road %zu (%zu points): ", ri, rd.points.size());
            for (size_t i = 0; i < rd.points.size(); ++i) {
                const audit::P p = audit::pt(rd.points[i]);
                if (audit::len(p - audit::P{nearX, nearY}) < nearR) std::printf("[%zu](%.1f,%.1f w%.1f) ", i, p.x, p.y, rd.widths[i].toDouble());
            }
            std::printf("\n");
        }
        return 0;
    }
    if (map) {  // the whole continent, roads and castles marked (a PPM; one pixel is about 4.4 m, so a wall is a pixel wide)
        const int W = 1280, H = 1600;
        std::vector<unsigned char> img(static_cast<size_t>(W) * H * 3);
        const auto put = [&](int px, int py, int r, int g, int b) {
            if (px < 0 || py < 0 || px >= W || py >= H) return;
            unsigned char* o = &img[(static_cast<size_t>(py) * W + px) * 3];
            o[0] = static_cast<unsigned char>(r), o[1] = static_cast<unsigned char>(g), o[2] = static_cast<unsigned char>(b);
        };
        const double mpp = static_cast<double>(Continent::kWidth) / W;
        for (int py = 0; py < H; ++py)
            for (int px = 0; px < W; ++px) {
                const Fixed x = Fixed::fromRatio((2 * px + 1) * Continent::kWidth, 2 * W), y = Fixed::fromRatio((2 * (H - 1 - py) + 1) * Continent::kHeight, 2 * H);
                static const int col[10][3] = {{120, 158, 72}, {240, 60, 40}, {48, 96, 48}, {95, 110, 70}, {120, 175, 200}, {45, 95, 165}, {135, 125, 110}, {255, 140, 0}, {120, 158, 72}, {120, 158, 72}};
                const int g = static_cast<int>(c.groundAt(x, y));
                put(px, py, col[g][0], col[g][1], col[g][2]);
            }
        const auto at = [&](audit::P p) { return std::pair<int, int>{static_cast<int>(p.x / mpp), H - 1 - static_cast<int>(p.y / mpp)}; };
        for (const Continent::City& city : c.cities()) {
            const auto [cx, cy] = at(audit::pt(city.pos));
            for (int d = -2; d <= 2; ++d)
                for (int e = -2; e <= 2; ++e) put(cx + d, cy + e, 255, 255, 0);
            const audit::Gate gate = audit::gateOf(city.castle.rings[0]);
            const auto [gx, gy] = at(gate.mouth);
            for (int d = -1; d <= 1; ++d)
                for (int e = -1; e <= 1; ++e) put(gx + d, gy + e, 255, 0, 255);
        }
        std::FILE* f = std::fopen(map, "wb");
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
    }
    std::printf("seed %" PRIu64 ": %zu cities, %zu roads\n", seed, c.cities().size(), c.roads().size());
    int noRoad = 0, noLink = 0, wet = 0, inWall = 0, noView = 0, crowd = 0;
    std::vector<int> towerHist(40, 0);
    for (const Continent::City& city : c.cities()) {
        const audit::CastleReport r = audit::inspect(c, city);
        towerHist[std::min<size_t>(r.towers, 39)]++;
        const bool bad = !r.gateRoad || !r.linked || r.gateWet || r.roadThroughWall || r.overlook < 3;
        noRoad += !r.gateRoad;
        noLink += !r.linked;
        wet += r.gateWet;
        inWall += r.roadThroughWall;
        noView += r.overlook < 3;
        crowd += r.crowded > 0;
        if (verbose || bad)
            std::printf("%-3s %-18s (%6.0f,%6.0f) shape %u corners %zu towers %2zu  gate-road %-3s  spur %5.1f m  linked %-3s  wet %-3s road-through-wall %-3s  overlook %2d  perim %4.0f m  crowded %d (min gap %.1f m)  gentle corners %d  ramps %zu\n",
                        city.capital ? "CAP" : "", city.name.c_str(), city.pos.x.toDouble(), city.pos.y.toDouble(), city.castle.shape,
                        city.castle.rings[0].corners.size(), r.towers, r.gateRoad ? "yes" : "NO", r.spur, r.linked ? "yes" : "NO",
                        r.gateWet ? "YES" : "no", r.roadThroughWall ? "YES" : "no", r.overlook, r.perimeter, r.crowded, r.minGap, r.sharpCorners, city.castle.ramps.size());
    }
    std::printf("\ngate without a road to its mouth: %d\ncastle not joined to the road network: %d\ngate mouth in water: %d\n"
                "road through the walls: %d\ngate with fewer than 3 embrasures covering its approach: %d\ncastles with towers crowded within 2 m of each other: %d\n",
                noRoad, noLink, wet, inWall, noView, crowd);
    std::printf("\nroads through water that is no bridge or ford (more than half a metre deep):\n");
    for (const audit::WetRun& w : audit::wetRoads(c)) std::printf("  road %zu%s: %d points from (%.0f, %.0f)\n", w.road, w.road >= 7 ? " (a spur)" : "", w.points, w.first.x, w.first.y);
    {
        const audit::RoadFlaws f = audit::roadFlaws(c);
        std::printf("\nroad dead-end stubs under 100 m: %zu, loops under 150 m: %zu\n", f.stubs.size(), f.loops.size());
        for (const audit::RoadFlaw& s : f.stubs) std::printf("  stub %.0f m at (%.0f, %.0f)\n", s.length, s.at.x, s.at.y);
        for (const audit::RoadFlaw& l : f.loops) std::printf("  loop %.0f m at (%.0f, %.0f)\n", l.length, l.at.x, l.at.y);
    }
    {
        const audit::RoadClasses k = audit::roadClasses(c);
        std::printf("\nroad classes: single lane %.0f m (%.0f%%), main %.0f m (%.0f%%), trunk %.0f m (%.0f%%); %zu of %zu roads have a trunk stretch;\n"
                    "  half width %.2f to %.2f m, changes at most %.3f m a metre\n",
                    k.metres[0], 100 * k.metres[0] / k.total, k.metres[1], 100 * k.metres[1] / k.total, k.metres[2], 100 * k.metres[2] / k.total, k.trunkRoads, k.roads, k.narrowest, k.widest, k.steepest);
    }
    {
        int64_t most = 0;
        for (const auto& t : c.roadTraffic())
            for (int64_t f : t) most = std::max(most, f);
        std::printf("  the busiest road point carries %lld journeys\n", static_cast<long long>(most));
    }
    std::printf("\nrivers (sections 1 m apart across, a section every river vertex): walls = outermost water deeper than %.2f m; steep = falls faster than 1 in %.0f\n", wateraudit::kWallDepth, 1 / wateraudit::kMaxFall);
    for (const wateraudit::RiverReport& r : wateraudit::rivers(c))
        std::printf("  %-14s sections %5d  walls %4d (tallest %5.2f m)  steep %4d (steepest 1 in %5.1f, biggest step %5.2f m)  uphill %d\n", c.rivers()[r.river].name.c_str(), r.sections, r.walls, r.tallestWall,
                    r.steep, r.steepest > 0 ? 1 / r.steepest : 0.0, r.biggestStep, r.uphill);
    std::printf("\nlakes and islands (deviation from the ellipse of the same moments, 0 = a drawn oval; solidity = area / convex hull; lobes = radius spread):\n");
    for (const shapeaudit::Shape& sh : shapeaudit::bodies(c))
        std::printf("  %-18s %-4s area %9.0f m2  deviation %5.3f  solidity %5.3f  lobes %5.3f\n", sh.name.c_str(), sh.lake ? "lake" : "land", sh.area, sh.deviation, sh.solidity, sh.lobes);
    std::printf("\ntowers per castle:\n");
    for (size_t n = 0; n < towerHist.size(); ++n)
        if (towerHist[n]) std::printf("  %2zu towers: %d castles\n", n, towerHist[n]);
    return 0;
}
