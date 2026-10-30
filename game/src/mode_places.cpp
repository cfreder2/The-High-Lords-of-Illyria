// Where to fight a match (mode.h): named places read from the continent, and
// on the battlefield cut there, the hill to hold and firm ground to stand on.
// Fixed point throughout: every peer picks the same field and the same hill.
#include <algorithm>

#include "illyria/game/mode.h"

namespace illyria::game {

using sim::Fixed;
using sim::Vec3;

namespace {

Fixed m(int64_t metres) { return Fixed::fromInt(metres); }

// How far the ground here stands above the land around it (at `reach`).
Fixed prominence(const sim::Continent& c, Fixed x, Fixed y, Fixed h, int64_t reach) {
    static constexpr int kDirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    Fixed sum;
    for (const auto& d : kDirs) {
        const int64_t k = d[0] != 0 && d[1] != 0 ? reach * 7 / 10 : reach;  // the diagonals as far out
        sum += c.sample(x + m(d[0] * k), y + m(d[1] * k)).height;
    }
    return h - sum / 8;
}

// Dry, open land a battle can be fought on.
bool fieldable(const sim::Continent& c, Fixed x, Fixed y, sim::Continent::Sample& s) {
    s = c.sample(x, y);
    Fixed surface;
    if (s.land <= Fixed::zero() || c.waterSurfaceOf(s, surface)) return false;
    const sim::Ground g = c.landGround(x, y, s.height, s.mountain, s.roadDist);
    return g != sim::Ground::Rock && g != sim::Ground::Swamp;
}

// Clear of the towns (their walls make another kind of battle).
bool clearOfTowns(const sim::Continent& c, const Vec3& p, int64_t gap) {
    for (const sim::Continent::City& city : c.cities())
        if ((city.pos - p).flat().length() < city.radius + m(gap)) return false;
    return true;
}

const sim::Continent::City* nearestCity(const sim::Continent& c, const Vec3& p) {
    const sim::Continent::City* best = nullptr;
    for (const sim::Continent::City& city : c.cities())
        if (!best || (city.pos - p).flat().lengthSq() < (best->pos - p).flat().lengthSq()) best = &city;
    return best;
}

// Inside the continent with a battlefield's width to spare.
bool roomFor(const Vec3& p) {
    const int64_t kMargin = 700;
    return p.x > m(kMargin) && p.y > m(kMargin) && p.x < m(sim::Continent::kWidth - kMargin) &&
           p.y < m(sim::Continent::kHeight - kMargin);
}

bool openGround(sim::Ground g) {
    return g == sim::Ground::Plains || g == sim::Ground::Road || g == sim::Ground::Sand || g == sim::Ground::Mud;
}

}  // namespace

const char* placeKindName(Place::Kind k) {
    switch (k) {
        case Place::Kind::Hill: return "hill";
        case Place::Kind::Ford: return "ford";
        case Place::Kind::Bridge: return "bridge";
        case Place::Kind::Pass: return "pass";
        case Place::Kind::Ruin: return "ruin";
        case Place::Kind::Temple: return "temple";
    }
    return "place";
}

std::vector<Place> battlePlaces(const sim::Continent& c) {
    std::vector<Place> out;
    sim::Continent::Sample s;
    // Hills: the most prominent dry high ground, well apart, away from the towns.
    {
        struct Rise {
            Vec3 pos;
            Fixed prom;
        };
        std::vector<Rise> rises;
        const int64_t kStep = 400;
        for (int64_t y = 800; y < sim::Continent::kHeight - 700; y += kStep)
            for (int64_t x = 800; x < sim::Continent::kWidth - 700; x += kStep) {
                // Not a mountain: a hill a horde can climb.
                if (!fieldable(c, m(x), m(y), s) || s.height < m(15) || s.height > m(165) || s.mountain > m(30)) continue;
                const Vec3 p{m(x), m(y), s.height};
                if (!clearOfTowns(c, p, 600)) continue;
                const Fixed prom = prominence(c, m(x), m(y), s.height, 320);
                if (prom >= m(6) && prom <= m(45)) rises.push_back({p, prom});
            }
        std::stable_sort(rises.begin(), rises.end(), [](const Rise& a, const Rise& b) { return a.prom > b.prom; });
        std::vector<Vec3> taken;
        for (const Rise& r : rises) {
            bool apart = true;
            for (const Vec3& t : taken) apart = apart && (t - r.pos).flat().length() > m(1500);
            if (!apart) continue;
            taken.push_back(r.pos);
            const sim::Continent::City* city = nearestCity(c, r.pos);
            Place pl;
            pl.kind = Place::Kind::Hill;
            pl.name = city ? "Hill above " + city->name : "The Hill";
            pl.pos = r.pos;
            pl.score = 100 + static_cast<int32_t>(r.prom.floorToInt());
            out.push_back(pl);
            if (taken.size() >= 8) break;
        }
    }
    // The river crossings: fords to hold, bridges to keep.
    for (const sim::Continent::Crossing& x : c.crossings()) {
        if (!roomFor(x.pos) || !clearOfTowns(c, x.pos, 300)) continue;
        Place pl;
        pl.kind = x.kind == sim::Continent::Crossing::Kind::Ford ? Place::Kind::Ford : Place::Kind::Bridge;
        pl.name = x.name;
        pl.pos = x.pos;
        pl.score = pl.kind == Place::Kind::Ford ? 80 : 60;
        out.push_back(pl);
    }
    // The passes through the mountains, named for their range.
    for (const Vec3& p : c.passes()) {
        const Vec3 at{p.x, p.y, Fixed::zero()};
        if (!roomFor(at)) continue;
        const sim::Continent::Ridge* ridge = nullptr;
        Fixed best = m(1000000);
        for (const sim::Continent::Ridge& r : c.ridges())
            for (const Vec3& q : r.line.points)
                if (const Fixed d = (q - at).flat().length(); d < best) {
                    best = d;
                    ridge = &r;
                }
        Place pl;
        pl.kind = Place::Kind::Pass;
        pl.name = ridge ? "Pass of the " + ridge->name : "The Pass";
        pl.pos = at;
        pl.score = 70;
        out.push_back(pl);
    }
    // Ruins and temples, the higher the better.
    for (const sim::Continent::Site& site : c.sites()) {
        if (site.kind == sim::Continent::Site::Kind::Sage || !roomFor(site.pos) || !clearOfTowns(c, site.pos, 300)) continue;
        if (!fieldable(c, site.pos.x, site.pos.y, s)) continue;
        Place pl;
        pl.kind = site.kind == sim::Continent::Site::Kind::Ruin ? Place::Kind::Ruin : Place::Kind::Temple;
        pl.name = site.name;
        pl.pos = site.pos;
        const Fixed prom = prominence(c, site.pos.x, site.pos.y, s.height, 300);
        pl.score = (pl.kind == Place::Kind::Ruin ? 72 : 55) + std::clamp<int32_t>(static_cast<int32_t>(prom.floorToInt()), -10, 20);
        out.push_back(pl);
    }
    // The recommended: the two best hills, the best ford, pass and ruin.
    std::stable_sort(out.begin(), out.end(), [](const Place& a, const Place& b) { return a.score > b.score; });
    int hills = 0;
    bool ford = false, pass = false, ruin = false;
    for (Place& pl : out) {
        if (pl.kind == Place::Kind::Hill && hills < 2) pl.recommended = ++hills > 0;
        else if (pl.kind == Place::Kind::Ford && !ford) pl.recommended = ford = true;
        else if (pl.kind == Place::Kind::Pass && !pass) pl.recommended = pass = true;
        else if (pl.kind == Place::Kind::Ruin && !ruin) pl.recommended = ruin = true;
    }
    std::stable_sort(out.begin(), out.end(), [](const Place& a, const Place& b) { return a.recommended > b.recommended; });
    return out;
}

sim::Vec3 highGround(const sim::Region& r, const sim::Vec3& near, int32_t searchM) {
    const sim::Terrain& t = r.terrain();
    const Fixed cell = t.cellSize();
    const int32_t n = t.cells();
    const int32_t reach = std::max(1, searchM / static_cast<int32_t>(cell.floorToInt()));
    const int32_t cx = static_cast<int32_t>((near.x / cell).floorToInt()), cy = static_cast<int32_t>((near.y / cell).floorToInt());
    struct Spot {
        Fixed score;
        int32_t ix, iy;
    };
    // Cells no one can fight on well: rock, water, or a face steeper than
    // about 25 degrees (2 m over a 4 m cell). The hilltop keeps clear of them
    // over the hill's whole radius, so it can be climbed from every side.
    const int32_t ring = 6;  // cells: about the hill rule's radius
    const auto rough = [&](int32_t ix, int32_t iy) {
        if (ix < 1 || iy < 1 || ix >= n || iy >= n) return true;
        const Fixed x = cell * ix, y = cell * iy;
        if (r.underWater(x, y) || r.groundAt(x, y) == sim::Ground::Rock) return true;
        return abs(t.sample(ix + 1, iy) - t.sample(ix, iy)) > m(2) || abs(t.sample(ix, iy + 1) - t.sample(ix, iy)) > m(2);
    };
    const int32_t x0 = std::max(1, cx - reach - ring), y0 = std::max(1, cy - reach - ring);
    const int32_t w = std::min(n - 1, cx + reach + ring) - x0 + 1, hgt = std::min(n - 1, cy + reach + ring) - y0 + 1;
    std::vector<uint8_t> bad(static_cast<size_t>(std::max(0, w * hgt)), 0);
    for (int32_t j = 0; j < hgt; ++j)
        for (int32_t i = 0; i < w; ++i) bad[static_cast<size_t>(j * w + i)] = rough(x0 + i, y0 + j);
    const auto gentle = [&](int32_t ix, int32_t iy) {
        for (int32_t dy = -ring; dy <= ring; ++dy)
            for (int32_t dx = -ring; dx <= ring; ++dx) {
                if (dx * dx + dy * dy > ring * ring) continue;
                const int32_t i = ix + dx - x0, j = iy + dy - y0;
                if (i < 0 || j < 0 || i >= w || j >= hgt || bad[static_cast<size_t>(j * w + i)]) return false;
            }
        return true;
    };
    std::vector<Spot> spots;
    for (int32_t iy = std::max(4, cy - reach); iy <= std::min(n - 4, cy + reach); ++iy)
        for (int32_t ix = std::max(4, cx - reach); ix <= std::min(n - 4, cx + reach); ++ix) {
            const int64_t dx = ix - cx, dy = iy - cy;
            if (dx * dx + dy * dy > static_cast<int64_t>(reach) * reach) continue;
            const Fixed x = cell * ix, y = cell * iy;
            if (r.underWater(x, y) || !openGround(r.groundAt(x, y))) continue;
            // Not a cliff: the samples a cell either side within 3 m.
            const Fixed h = t.sample(ix, iy);
            if (!gentle(ix, iy)) continue;
            // A metre of height is worth 40 m nearer the centre.
            const Fixed dist = Vec3{cell * dx, cell * dy, Fixed::zero()}.length();
            spots.push_back({h - dist / 40, ix, iy});
        }
    std::stable_sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.score > b.score; });
    // The best clear of trees, rocks and houses.
    for (size_t k = 0; k < spots.size() && k < 200; ++k) {
        const Vec3 p{cell * spots[k].ix, cell * spots[k].iy, Fixed::zero()};
        bool clear = true;
        for (const sim::Obstacle& o : r.obstacles())
            if (o.solid && (o.pos - p).flat().lengthSq() < m(6) * m(6)) {
                clear = false;
                break;
            }
        if (clear) return {p.x, p.y, t.heightAt(p.x, p.y)};
    }
    return firmGround(r, {near.x, near.y, Fixed::zero()}, searchM);
}

sim::Vec3 firmGround(const sim::Region& r, const sim::Vec3& p, int32_t searchM) {
    const sim::Terrain& t = r.terrain();
    const Fixed cell = t.cellSize();
    const int32_t n = t.cells();
    const int32_t cx = std::clamp(static_cast<int32_t>((p.x / cell).floorToInt()), 3, n - 3);
    const int32_t cy = std::clamp(static_cast<int32_t>((p.y / cell).floorToInt()), 3, n - 3);
    const auto good = [&](int32_t ix, int32_t iy) {
        if (ix < 3 || iy < 3 || ix > n - 3 || iy > n - 3) return false;
        const Fixed x = cell * ix, y = cell * iy;
        const sim::Ground g = r.groundAt(x, y);
        if (r.underWater(x, y) || g == sim::Ground::Rock || g == sim::Ground::DeepWater || sim::groundSpeed(g) == Fixed::zero()) return false;
        const Vec3 q{x, y, Fixed::zero()};
        for (const sim::Obstacle& o : r.obstacles())
            if (o.solid && (o.pos - q).flat().lengthSq() < m(4) * m(4)) return false;
        for (const sim::Rampart& rp : r.ramparts())
            if ((rp.centre - q).flat().lengthSq() < m(90) * m(90)) return false;  // not in a castle
        return true;
    };
    // Rings outward, each in one order: the first good cell found.
    const int32_t reach = std::max(1, searchM / static_cast<int32_t>(cell.floorToInt()));
    for (int32_t ring = 0; ring <= reach; ++ring)
        for (int32_t k = -ring; k <= ring; ++k) {
            const int32_t cand[4][2] = {{cx + k, cy - ring}, {cx + ring, cy + k}, {cx - k, cy + ring}, {cx - ring, cy - k}};
            for (const auto& c2 : cand)
                if (good(c2[0], c2[1])) {
                    const Fixed x = cell * c2[0], y = cell * c2[1];
                    return {x, y, t.heightAt(x, y)};
                }
        }
    return {cell * cx, cell * cy, t.heightAt(cell * cx, cell * cy)};
}

}  // namespace illyria::game
