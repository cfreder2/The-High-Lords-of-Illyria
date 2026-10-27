#include "illyria/sim/region.h"

#include <algorithm>

#include "illyria/sim/hash.h"
#include "illyria/sim/noise.h"
#include "illyria/sim/rng.h"

namespace illyria::sim {

namespace {

constexpr Fixed m(int64_t v) { return Fixed::fromInt(v); }
constexpr Fixed r(int64_t num, int64_t den) { return Fixed::fromRatio(num, den); }
constexpr int32_t kCells = 256;
constexpr int64_t kCellSize = 4;
constexpr Fixed kWater = m(8);

Vec3 P(int64_t x, int64_t y) { return {m(x), m(y), Fixed::zero()}; }

// Squared flat distance from (px, py) to segment ab.
Fixed segmentDistSq(Fixed px, Fixed py, const Vec3& a, const Vec3& b) {
    const Fixed dx = b.x - a.x, dy = b.y - a.y;
    const Fixed len2 = dx * dx + dy * dy;
    Fixed t;
    if (len2 > Fixed::zero()) t = clamp(((px - a.x) * dx + (py - a.y) * dy) / len2, Fixed::zero(), Fixed::one());
    const Fixed cx = a.x + dx * t - px, cy = a.y + dy * t - py;
    return cx * cx + cy * cy;
}

// Flat distance to a set of polylines, or `limit` if all are at least that far.
Fixed distanceTo(const std::vector<Polyline>& lines, Fixed px, Fixed py, Fixed limit) {
    Fixed best = limit * limit;
    for (const Polyline& l : lines) {
        for (size_t i = 0; i + 1 < l.points.size(); ++i) {
            const Vec3& a = l.points[i];
            const Vec3& b = l.points[i + 1];
            // Cheap bounding-box rejection before the exact test.
            if (px < min(a.x, b.x) - limit || px > max(a.x, b.x) + limit || py < min(a.y, b.y) - limit ||
                py > max(a.y, b.y) + limit) {
                continue;
            }
            best = min(best, segmentDistSq(px, py, a, b));
        }
    }
    return best >= limit * limit ? limit : sqrt(best);
}

// Smooth bump: 1 at t = 0, 0 at |t| >= 1.
Fixed bump(Fixed t) {
    const Fixed a = abs(t);
    if (a >= Fixed::one()) return Fixed::zero();
    const Fixed s = Fixed::one() - a * a;
    return s * s;
}

Fixed flatDist(Fixed x, Fixed y, const Vec3& c) {
    const Fixed dx = x - c.x, dy = y - c.y;
    return sqrt(dx * dx + dy * dy);
}

}  // namespace

Fixed groundSpeed(Ground g) {
    switch (g) {
        case Ground::Plains: return Fixed::one();
        case Ground::Road: return r(5, 4);
        case Ground::Forest: return r(7, 10);
        case Ground::Swamp: return r(1, 2);
        case Ground::Shallows: return r(2, 5);
        case Ground::DeepWater: return Fixed::zero();
        case Ground::Rock: return r(1, 2);
    }
    return Fixed::one();
}

Ground Region::groundAt(Fixed x, Fixed y) const {
    const int32_t n = terrain_.cells();
    const int32_t ix = static_cast<int32_t>(std::clamp<int64_t>((x / terrain_.cellSize()).floorToInt(), 0, n - 1));
    const int32_t iy = static_cast<int32_t>(std::clamp<int64_t>((y / terrain_.cellSize()).floorToInt(), 0, n - 1));
    return cellGround(ix, iy);
}

Region Region::generateIllyria(uint64_t seed) {
    Region rg;
    rg.waterLevel_ = kWater;

    // The river runs south from the mountains, through the ford, into the lake.
    rg.rivers_.push_back({{P(650, 1030), P(615, 930), P(585, 820), P(560, 700), P(552, 600), P(560, 512),
                           P(548, 430), P(505, 340), P(430, 265), P(330, 205), P(250, 172)},
                          m(11)});
    const Vec3 lake = P(215, 155);
    const Fixed lakeRadius = m(85);
    rg.ford_ = P(560, 512);
    // The old east-west road crosses at the ford; another runs north past the
    // village into the mountains; a track climbs to the temple.
    rg.roads_.push_back({{P(-10, 522), P(150, 508), P(300, 526), P(440, 515), P(560, 512), P(660, 506),
                          P(790, 536), P(1034, 528)},
                         r(7, 2)});
    rg.roads_.push_back({{P(345, -10), P(360, 150), P(342, 300), P(318, 420), P(300, 526), P(305, 640),
                          P(290, 760), P(262, 900), P(250, 1034)},
                         r(7, 2)});
    rg.roads_.push_back({{P(790, 536), P(828, 610), P(858, 690)}, m(3)});
    // An earthwork above the ford.
    rg.berms_.push_back({{P(455, 440), P(462, 590)}, r(7, 2)});
    const Vec3 temple = P(870, 705);

    const int32_t n = kCells + 1;
    const Fixed cell = m(kCellSize);
    std::vector<Fixed> h(static_cast<size_t>(n) * n);
    const uint64_t sBase = noise::nextSeed(seed), sRoll = noise::nextSeed(sBase), sPeak = noise::nextSeed(sRoll),
                   sForest = noise::nextSeed(sPeak), sWet = noise::nextSeed(sForest);

    // Pass 1: hills, mountains, the temple hill, and the river valley and lake.
    for (int32_t iy = 0; iy < n; ++iy) {
        for (int32_t ix = 0; ix < n; ++ix) {
            const Fixed x = cell * ix, y = cell * iy;
            Fixed z = m(11) + noise::fbm(sBase, x, y, 260) * 20 + (noise::fbm(sRoll, x, y, 60) - r(1, 2)) * 3;
            const Fixed mountains = max(noise::smoothstep(m(690), m(900), y),
                                        noise::smoothstep(m(820), m(1000), x) * noise::smoothstep(m(600), m(780), y));
            if (mountains > Fixed::zero()) {
                z += mountains * (m(15) + noise::ridged(sPeak, x, y, 180) * 110);
            }
            z += m(16) * bump(flatDist(x, y, temple) / 70);

            const Fixed dRiver = distanceTo(rg.rivers_, x, y, m(400));
            const bool atFord = flatDist(x, y, rg.ford_) < m(26);
            // The river narrows and widens along its course.
            const Fixed wobble = (noise::value(sWet, x, y, 90) - r(1, 2)) * 8;
            const Fixed hw = atFord ? m(14) : rg.rivers_[0].halfWidth + wobble;
            const Fixed bed = atFord ? kWater - r(3, 5) : kWater - r(16, 5);
            const Fixed t = dRiver / hw;
            const Fixed riverProfile = dRiver < hw ? bed + (kWater + r(3, 10) - bed) * t * t
                                                   : kWater + r(3, 10) + (dRiver - hw) * r(8, 25);
            z = min(z, riverProfile);

            const Fixed dLake = flatDist(x, y, lake) - lakeRadius - (noise::fbm(sRoll, x, y, 70) - r(1, 2)) * 50;
            const Fixed lakeProfile = dLake < Fixed::zero()
                                          ? kWater - Fixed::one() - noise::smoothstep(Fixed::zero(), m(60), -dLake) * 4
                                          : kWater + r(3, 10) + dLake / 4;
            z = min(z, lakeProfile);
            h[static_cast<size_t>(iy) * n + ix] = z;
        }
    }

    // Pass 2: roads settle into smoothed, slightly sunken beds; the berm rises.
    std::vector<Fixed> shaped = h;
    for (int32_t iy = 0; iy < n; ++iy) {
        for (int32_t ix = 0; ix < n; ++ix) {
            const Fixed x = cell * ix, y = cell * iy;
            Fixed& z = shaped[static_cast<size_t>(iy) * n + ix];
            const Fixed dRoad = distanceTo(rg.roads_, x, y, m(7));
            if (dRoad < m(7) && z >= kWater) {
                Fixed sum;
                int32_t count = 0;
                for (int32_t dy = -2; dy <= 2; ++dy) {
                    for (int32_t dx = -2; dx <= 2; ++dx) {
                        const int32_t sx = std::clamp(ix + dx, 0, n - 1), sy = std::clamp(iy + dy, 0, n - 1);
                        sum += h[static_cast<size_t>(sy) * n + sx];
                        ++count;
                    }
                }
                const Fixed smooth = sum / count;
                const Fixed w = noise::smoothstep(m(2), m(7), dRoad);
                z = smooth + (z - smooth) * w - (Fixed::one() - w) * r(3, 10);
                z = max(z, kWater + r(1, 5));
            }
            const Fixed dBerm = distanceTo(rg.berms_, x, y, m(5));
            if (dBerm < m(5)) z += r(9, 5) * bump(dBerm / 5);
        }
    }
    rg.terrain_ = Terrain::fromSamples(kCells, cell, shaped);

    // Ground types per cell, from the cell centre.
    rg.ground_.resize(static_cast<size_t>(kCells) * kCells);
    for (int32_t iy = 0; iy < kCells; ++iy) {
        for (int32_t ix = 0; ix < kCells; ++ix) {
            const Fixed a = shaped[static_cast<size_t>(iy) * n + ix], b = shaped[static_cast<size_t>(iy) * n + ix + 1];
            const Fixed c = shaped[static_cast<size_t>(iy + 1) * n + ix], d = shaped[static_cast<size_t>(iy + 1) * n + ix + 1];
            const Fixed z = (a + b + c + d) / 4;
            const Fixed slope = max(max(abs(a - b), abs(c - d)), max(abs(a - c), abs(b - d))) / kCellSize;
            const Fixed x = cell * ix + cell / 2, y = cell * iy + cell / 2;
            Ground g = Ground::Plains;
            if (z < kWater) {
                g = kWater - z < r(11, 10) ? Ground::Shallows : Ground::DeepWater;
            } else if (distanceTo(rg.roads_, x, y, m(4)) < m(3)) {
                g = Ground::Road;
            } else if (slope > r(3, 5) || z > m(70)) {
                g = Ground::Rock;
            } else if (z < kWater + r(11, 5) && flatDist(x, y, lake) < lakeRadius + m(45) &&
                       noise::fbm(sWet, x, y, 50) > r(2, 5)) {
                g = Ground::Swamp;
            } else {
                const Fixed foothills = noise::smoothstep(m(600), m(760), y);
                const Fixed nearFord = bump(flatDist(x, y, rg.ford_) / 210);
                if (noise::fbm(sForest, x, y, 140) + foothills * 3 / 20 - nearFord / 4 > r(29, 50)) g = Ground::Forest;
            }
            rg.ground_[static_cast<size_t>(iy) * kCells + ix] = g;
        }
    }

    // The village along the north road, and the temple ruin on its hill.
    for (int i = 0; i < 10; ++i) {
        Obstacle o;
        o.kind = Obstacle::Kind::House;
        const Fixed y = m(560 + i * 18);
        const Fixed x = m(300 + (i % 2 == 0 ? -15 : 15) + (i % 3));
        o.pos = {x, y, rg.terrain_.heightAt(x, y)};
        o.halfX = r(13, 5);
        o.halfY = r(18, 5);
        o.height = r(9, 2);
        o.variant = static_cast<uint8_t>(i % 3);
        rg.obstacles_.push_back(o);
    }
    for (int i = 0; i < 8; ++i) {
        Obstacle o;
        o.kind = Obstacle::Kind::Pillar;
        const Fixed angle = kTwoPi * i / 8;
        const Fixed x = temple.x + cos(angle) * 10, y = temple.y + sin(angle) * 10;
        o.pos = {x, y, rg.terrain_.heightAt(x, y)};
        o.radius = r(3, 5);
        // Some pillars still stand; others are broken stumps.
        o.height = i % 3 == 1 ? r(8, 5) : r(11, 2);
        o.variant = static_cast<uint8_t>(i % 3);
        rg.obstacles_.push_back(o);
    }
    {
        Obstacle altar;
        altar.kind = Obstacle::Kind::House;
        altar.pos = {temple.x, temple.y, rg.terrain_.heightAt(temple.x, temple.y)};
        altar.halfX = r(3, 2);
        altar.halfY = r(3, 2);
        altar.height = r(6, 5);
        altar.variant = 3;  // altar
        rg.obstacles_.push_back(altar);
    }
    const size_t buildings = rg.obstacles_.size();

    // Trees, undergrowth and rocks on a jittered 5 m grid. Exactly five RNG
    // draws per point, one per statement, so the sequence never shifts.
    Rng rng(seed, 7);
    const int32_t step = 5;
    for (int32_t gy = 0; gy < 1024 / step; ++gy) {
        for (int32_t gx = 0; gx < 1024 / step; ++gx) {
            const Fixed jx = rng.nextUnit() * 4;
            const Fixed jy = rng.nextUnit() * 4;
            const Fixed roll = rng.nextUnit();
            const uint32_t variant = rng.nextBelow(4);
            const Fixed size = rng.nextUnit();
            const Fixed x = m(gx * step) + jx, y = m(gy * step) + jy;
            const Ground g = rg.groundAt(x, y);
            if (g == Ground::Road || g == Ground::Shallows || g == Ground::DeepWater) continue;
            if (distanceTo(rg.roads_, x, y, m(5)) < r(9, 2)) continue;
            if (flatDist(x, y, temple) < m(15)) continue;
            bool nearBuilding = false;
            for (size_t b = 0; b < buildings; ++b) {
                nearBuilding = nearBuilding || flatDist(x, y, rg.obstacles_[b].pos) < m(8);
            }
            if (nearBuilding) continue;

            Obstacle o;
            o.variant = static_cast<uint8_t>(variant);
            o.pos = {x, y, rg.terrain_.heightAt(x, y)};
            bool place = true;
            const auto tree = [&](bool pine) {
                o.kind = pine ? Obstacle::Kind::Pine : Obstacle::Kind::Oak;
                o.radius = pine ? r(7, 20) + size * 3 / 20 : r(9, 20) + size / 5;
                o.height = pine ? m(9) + size * 6 : m(7) + size * 4;
            };
            const auto bush = [&] {
                o.kind = Obstacle::Kind::Bush;
                o.radius = r(9, 10) + size * 4 / 5;
                o.height = r(4, 5) + size * 4 / 5;
                o.solid = false;
            };
            const auto rock = [&](bool boulder) {
                o.kind = boulder ? Obstacle::Kind::Boulder : Obstacle::Kind::Rock;
                o.radius = boulder ? r(7, 5) + size * 8 / 5 : r(1, 2) + size * 3 / 5;
                o.height = boulder ? r(6, 5) + size * 2 : r(1, 2) + size * 3 / 5;
            };
            switch (g) {
                case Ground::Forest:
                    if (roll < r(2, 5)) tree(o.pos.z > m(30) || variant < 2);
                    else if (roll < r(31, 50)) bush();
                    else place = false;
                    break;
                case Ground::Plains:
                    if (roll < r(9, 500)) tree(false);
                    else if (roll < r(1, 20)) bush();
                    else if (roll < r(31, 500)) rock(false);
                    else place = false;
                    break;
                case Ground::Rock:
                    if (roll < r(1, 10)) rock(true);
                    else if (roll < r(13, 50)) rock(false);
                    else if (roll < r(3, 10) && o.pos.z < m(90)) tree(true);
                    else place = false;
                    break;
                case Ground::Swamp:
                    if (roll < r(2, 25)) {
                        bush();
                        o.variant = 3;  // reeds
                    } else {
                        place = false;
                    }
                    break;
                default:
                    place = false;
            }
            if (place) rg.obstacles_.push_back(o);
        }
    }
    return rg;
}

uint64_t Region::checksum() const {
    StateHasher h;
    for (Fixed s : terrain_.samples()) h.addI64(s.raw);
    for (Ground g : ground_) h.addU64(static_cast<uint64_t>(g));
    h.addU64(obstacles_.size());
    for (const Obstacle& o : obstacles_) {
        h.addU64(static_cast<uint64_t>(o.kind));
        h.addI64(o.pos.x.raw);
        h.addI64(o.pos.y.raw);
        h.addI64(o.pos.z.raw);
        h.addI64(o.radius.raw);
        h.addI64(o.height.raw);
        h.addI64(o.halfX.raw);
        h.addI64(o.halfY.raw);
        h.addU64(o.variant);
        h.addU64(o.solid);
    }
    return h.value();
}

}  // namespace illyria::sim
