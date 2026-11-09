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
#include "terrain_audit.h"

using namespace illyria::sim;

int main(int argc, char** argv) {
    uint64_t seed = 20261027;
    bool verbose = false;
    const char* map = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-v")) verbose = true;
        else if (!std::strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else seed = std::strtoull(argv[i], nullptr, 10);
    }
    const Continent c(seed);
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
    std::printf("\ntowers per castle:\n");
    for (size_t n = 0; n < towerHist.size(); ++n)
        if (towerHist[n]) std::printf("  %2zu towers: %d castles\n", n, towerHist[n]);
    return 0;
}
