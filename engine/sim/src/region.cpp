#include "illyria/sim/region.h"

#include <algorithm>

#include "illyria/sim/continent.h"
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
        case Ground::Bridge: return Fixed::one();
        case Ground::Mud: return r(11, 20);
        case Ground::Sand: return r(17, 20);
    }
    return Fixed::one();
}

bool Deck::contains(Fixed x, Fixed y) const {
    const Fixed dx = x - centre.x, dy = y - centre.y;
    if (abs(dx) > halfSpan + halfWidth || abs(dy) > halfSpan + halfWidth) return false;
    return abs(dx * dir.x + dy * dir.y) <= halfSpan && abs(dx * dir.y - dy * dir.x) <= halfWidth;
}

Fixed Deck::heightAt(Fixed x, Fixed y) const {
    if (rise == Fixed::zero() || halfSpan == Fixed::zero()) return centre.z;
    const Fixed along = (x - centre.x) * dir.x + (y - centre.y) * dir.y;
    return centre.z + rise * along / (halfSpan * 2);
}

bool Region::deckAt(Fixed x, Fixed y, Fixed& height) const {
    bool found = false;
    for (const Deck& d : decks_) {
        if (!d.contains(x, y)) continue;
        const Fixed h = d.heightAt(x, y);
        if (!found || h > height) height = h;
        found = true;
    }
    return found;
}

bool Region::deckBelow(Fixed x, Fixed y, Fixed ceiling, Fixed& height) const {
    bool found = false;
    for (const Deck& d : decks_) {
        if (!d.contains(x, y)) continue;
        const Fixed h = d.heightAt(x, y);
        if (h > ceiling || (found && h <= height)) continue;
        height = h;
        found = true;
    }
    return found;
}

int Region::rampartAt(Fixed x, Fixed y, Fixed z) const {
    for (size_t i = 0; i < ramparts_.size(); ++i) {
        const Rampart& rp = ramparts_[i];
        const size_t n = rp.corners.size();
        for (size_t e = 0; e < n; ++e) {
            const Vec3& a = rp.corners[e];
            const Vec3& b = rp.corners[(e + 1) % n];
            if (abs(z - a.z) > Fixed::fromInt(2)) continue;
            Fixed t;
            const Vec3 ab = (b - a).flat();
            const Fixed len2 = ab.lengthSq();
            t = len2 > Fixed::zero() ? clamp((Vec3{x, y, Fixed::zero()} - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
            const Vec3 p = a + ab * t;
            if ((Vec3{x - p.x, y - p.y, Fixed::zero()}).lengthSq() <= (rp.halfThick + Fixed::fromRatio(1, 2)) * (rp.halfThick + Fixed::fromRatio(1, 2))) return static_cast<int>(i);
        }
    }
    return -1;
}

bool Region::underWater(Fixed x, Fixed y) const {
    const int32_t n = terrain_.cells() + 1;
    const Fixed cs = terrain_.cellSize();
    const int64_t ix = std::clamp<int64_t>((x / cs).floorToInt(), 0, n - 2), iy = std::clamp<int64_t>((y / cs).floorToInt(), 0, n - 2);
    Fixed surface = -Fixed::fromInt(100000);
    for (int64_t k = 0; k < 4; ++k) {
        const Fixed w = water_[static_cast<size_t>((iy + k / 2) * n + ix + k % 2)];
        if (w.raw != kDryRaw) surface = max(surface, w);
    }
    return terrain_.heightAt(x, y) < surface;
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
    rg.water_.resize(shaped.size());
    for (size_t i = 0; i < shaped.size(); ++i) rg.water_[i] = shaped[i] < kWater ? kWater : Fixed::fromRaw(kDryRaw);

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

    rg.scatterScenery(seed, buildings, temple, m(15));
    return rg;
}

void Region::scatterScenery(uint64_t seed, size_t buildings, const Vec3& clearCentre, Fixed clearRadius) {
    // Trees, undergrowth and rocks on a jittered 5 m grid. Exactly five RNG
    // draws per point, one per statement, so the sequence never shifts.
    Rng rng(seed, 7);
    const int32_t extent = static_cast<int32_t>(terrain_.size().floorToInt());
    const int32_t step = 5;
    for (int32_t gy = 0; gy < extent / step; ++gy) {
        for (int32_t gx = 0; gx < extent / step; ++gx) {
            const Fixed jx = rng.nextUnit() * 4;
            const Fixed jy = rng.nextUnit() * 4;
            const Fixed roll = rng.nextUnit();
            const uint32_t variant = rng.nextBelow(4);
            const Fixed size = rng.nextUnit();
            const Fixed x = m(gx * step) + jx, y = m(gy * step) + jy;
            const Ground g = groundAt(x, y);
            if (g == Ground::Road || g == Ground::Shallows || g == Ground::DeepWater || g == Ground::Bridge) continue;
            if (distanceTo(roads_, x, y, m(5)) < r(9, 2)) continue;
            if (flatDist(x, y, clearCentre) < clearRadius) continue;
            bool inCastle = false;  // nothing grows inside the walls or right against them
            for (const Rampart& rp : ramparts_) {
                Fixed reach;
                for (const Vec3& q : rp.corners) reach = max(reach, flatDist(q.x, q.y, rp.centre));
                inCastle = inCastle || flatDist(x, y, rp.centre) < reach + m(12);
            }
            if (inCastle) continue;
            bool nearDeck = false;  // keep bridge approaches open
            for (const Deck& d : decks_) nearDeck = nearDeck || Deck{d.centre, d.dir, d.halfSpan + m(8), d.halfWidth + m(3)}.contains(x, y);
            if (nearDeck) continue;
            bool nearBuilding = false;
            for (size_t b = 0; b < buildings; ++b) {
                nearBuilding = nearBuilding || flatDist(x, y, obstacles_[b].pos) < m(8);
            }
            if (nearBuilding) continue;

            Obstacle o;
            o.variant = static_cast<uint8_t>(variant);
            o.pos = {x, y, terrain_.heightAt(x, y)};
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
            const auto log = [&] {
                // A fallen trunk: radius is half its length, height its girth.
                o.kind = Obstacle::Kind::Log;
                o.radius = r(3, 2) + size * 2;
                o.height = r(2, 5) + size * r(2, 5);
                o.solid = false;
            };
            const auto reeds = [&] {
                bush();
                o.variant = 3;
            };
            // The open country is not uniform: groves, rocky outcrops and
            // bare meadow come in patches (noise fields, not extra RNG draws).
            const Fixed grove = noise::value(seed ^ 0x6A0E, x, y, 70);
            const Fixed outcrop = noise::value(seed ^ 0x0C7, x, y, 55);
            bool byWater = false;
            for (int k = 0; k < 4 && !byWater; ++k) {
                const Ground w = groundAt(x + m(k == 0 ? 4 : k == 1 ? -4 : 0), y + m(k == 2 ? 4 : k == 3 ? -4 : 0));
                byWater = w == Ground::Shallows || w == Ground::DeepWater;
            }
            switch (g) {
                case Ground::Forest:
                    if (roll < r(2, 5)) tree(o.pos.z > m(30) || variant < 2);
                    else if (roll < r(31, 50)) bush();
                    else if (roll < r(33, 50)) log();
                    else if (roll < r(67, 100) && outcrop > r(3, 4)) rock(variant == 0);
                    else place = false;
                    break;
                case Ground::Plains:
                    if (byWater) {
                        // Only reeds at the water's edge: nothing solid to shove waders in.
                        if (roll < r(2, 5)) reeds();
                        else place = false;
                    } else if (grove > r(18, 25)) {
                        if (roll < r(1, 3)) tree(variant == 0);
                        else if (roll < r(1, 2)) bush();
                        else if (roll < r(53, 100)) log();
                        else place = false;
                    } else if (outcrop > r(4, 5)) {
                        if (roll < r(3, 25)) rock(true);
                        else if (roll < r(8, 25)) rock(false);
                        else if (roll < r(9, 25)) bush();
                        else place = false;
                    } else {
                        if (roll < r(6, 1000)) tree(false);
                        else if (roll < r(2, 50)) bush();
                        else if (roll < r(27, 500)) rock(false);
                        else if (roll < r(28, 500)) log();
                        else place = false;
                    }
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
            if (place) obstacles_.push_back(o);
        }
    }
}

Region Region::fromContinent(const Continent& c, const Vec3& centre, int32_t cells) {
    Region rg;
    const Fixed cell = m(kCellSize);
    const Fixed size = cell * cells;
    // Align the window to the 4 m grid.
    const int64_t ox = ((centre.x - size / 2).floorToInt() / kCellSize) * kCellSize;
    const int64_t oy = ((centre.y - size / 2).floorToInt() / kCellSize) * kCellSize;
    rg.origin_ = {m(ox), m(oy), Fixed::zero()};
    const Fixed OX = rg.origin_.x, OY = rg.origin_.y;
    const auto toLocal = [&](const Vec3& p) { return Vec3{p.x - OX, p.y - OY, p.z}; };

    // Coast and mountains change slowly: sample them every 16 m and interpolate.
    const int32_t macroStep = 4;  // samples
    const int32_t mn = cells / macroStep + 1;
    std::vector<Continent::Sample> macro(static_cast<size_t>(mn) * mn);
    for (int32_t my = 0; my < mn; ++my)
        for (int32_t mx = 0; mx < mn; ++mx)
            macro[static_cast<size_t>(my) * mn + mx] = c.landform(OX + cell * (mx * macroStep), OY + cell * (my * macroStep));

    const int32_t n = cells + 1;
    std::vector<Fixed> h(static_cast<size_t>(n) * n), roadDist(h.size()), mountain(h.size());
    rg.water_.assign(h.size(), Fixed::fromRaw(kDryRaw));
    const uint64_t sDetail = noise::nextSeed(c.seed() ^ 0xDE7A11);
    for (int32_t iy = 0; iy < n; ++iy) {
        for (int32_t ix = 0; ix < n; ++ix) {
            const int32_t mx = std::min(ix / macroStep, mn - 2), my = std::min(iy / macroStep, mn - 2);
            const Fixed tx = Fixed::fromRatio(ix - mx * macroStep, macroStep), ty = Fixed::fromRatio(iy - my * macroStep, macroStep);
            const auto lerp2 = [&](auto field) {
                const Fixed a = field(macro[static_cast<size_t>(my) * mn + mx]), b = field(macro[static_cast<size_t>(my) * mn + mx + 1]);
                const Fixed cc = field(macro[static_cast<size_t>(my + 1) * mn + mx]), d = field(macro[static_cast<size_t>(my + 1) * mn + mx + 1]);
                const Fixed top = a + (b - a) * tx, bottom = cc + (d - cc) * tx;
                return top + (bottom - top) * ty;
            };
            Continent::Sample s;
            s.height = lerp2([](const Continent::Sample& q) { return q.height; });
            s.land = lerp2([](const Continent::Sample& q) { return q.land; });
            s.mountain = lerp2([](const Continent::Sample& q) { return q.mountain; });
            const Fixed x = OX + cell * ix, y = OY + cell * iy;
            // Fine detail, faded out at the window's edge so it meets the coarse continent.
            const Fixed edge = m(std::min(std::min(ix, iy), std::min(cells - ix, cells - iy)) * kCellSize);
            const Fixed fade = min(Fixed::one(), edge / 48);
            s.height += (noise::fbm(sDetail, x, y, 48) - r(1, 2)) * (m(2) + s.mountain / 20) * fade;
            s.height += (noise::value(sDetail ^ 0xB0B, x, y, 14) - r(1, 2)) * r(4, 5) * fade;  // tussocks and dips
            c.carve(s, x, y);
            const size_t i = static_cast<size_t>(iy) * n + ix;
            h[i] = s.height;
            roadDist[i] = s.roadDist;
            mountain[i] = s.mountain;
            Fixed surface;
            if (c.waterSurfaceOf(s, surface)) rg.water_[i] = surface;
        }
    }

    // Roads settle into smoothed, slightly sunken beds (not under water).
    std::vector<Fixed> shaped = h;
    for (int32_t iy = 0; iy < n; ++iy) {
        for (int32_t ix = 0; ix < n; ++ix) {
            const size_t i = static_cast<size_t>(iy) * n + ix;
            if (roadDist[i] >= m(7) || rg.water_[i].raw != kDryRaw) continue;
            Fixed sum;
            for (int32_t dy = -2; dy <= 2; ++dy)
                for (int32_t dx = -2; dx <= 2; ++dx)
                    sum += h[static_cast<size_t>(std::clamp(iy + dy, 0, n - 1)) * n + std::clamp(ix + dx, 0, n - 1)];
            const Fixed smooth = sum / 25;
            const Fixed w = noise::smoothstep(m(2), m(7), roadDist[i]);
            shaped[i] = smooth + (h[i] - smooth) * w - (Fixed::one() - w) * r(3, 10);
        }
    }

    // The ford: an earthwork berm on the west bank, a road through it.
    Vec3 ford = c.crossings().front().pos;
    Fixed best = m(1000000);
    for (const Continent::Crossing& cr : c.crossings()) {
        const Fixed d = (cr.pos - centre).flat().length();
        if (d < best) {
            best = d;
            ford = cr.pos;
        }
    }
    rg.ford_ = toLocal(ford);
    for (const Continent::Crossing& cr : c.crossings()) {
        if (!cr.bridged()) continue;
        const Vec3 local = toLocal(cr.pos);
        const Fixed reach = cr.halfSpan + cr.halfWidth;
        const Fixed size = cell * cells;
        if (local.x < -reach || local.y < -reach || local.x > size + reach || local.y > size + reach) continue;
        rg.decks_.push_back({{local.x, local.y, cr.deck}, cr.dir, cr.halfSpan, cr.halfWidth, cr.kind == Continent::Crossing::Kind::Footbridge});
    }
    // Castles: walls and towers that block, a walk along the top, stairs up to it.
    for (const Continent::City& city : c.cities()) {
        const Vec3 at = toLocal(city.pos);
        const Fixed size = cell * cells, reach = city.radius * 2;
        if (at.x < -reach || at.y < -reach || at.x > size + reach || at.y > size + reach) continue;
        const Continent::Castle& k = city.castle;
        const auto unit = [](const Vec3& v) {
            const Fixed len = v.flat().length();
            return len > Fixed::zero() ? v.flat() / len : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
        };
        for (const Continent::Castle::Ring& ring : k.rings) {
            Rampart rp;
            rp.halfThick = ring.thick / 2;
            rp.centre = at;
            const Fixed walk = city.pos.z + ring.height - Fixed::fromRatio(13, 10);
            const size_t nc = ring.corners.size();
            for (size_t e = 0; e < nc; ++e) {
                const Vec3 a = toLocal(ring.corners[e]), b = toLocal(ring.corners[(e + 1) % nc]);
                rp.corners.push_back({a.x, a.y, walk});
                const Vec3 along = unit(b - a);
                const Fixed len = (b - a).flat().length();
                const Fixed top = city.pos.z + ring.height;
                if (e == ring.gateEdge) {
                    const Vec3 mid = a + along * (len / 2);
                    rg.walls_.push_back({a, mid - along * (ring.gateWidth / 2), ring.thick / 2, top, walk});
                    rg.walls_.push_back({mid + along * (ring.gateWidth / 2), b, ring.thick / 2, top, walk});
                } else {
                    rg.walls_.push_back({a, b, ring.thick / 2, top, walk});
                }
                // The walk runs the whole edge, over the gate too, into the corner towers.
                Deck d{{(a.x + b.x) / 2, (a.y + b.y) / 2, walk}, along, len / 2 + ring.thick / 2, ring.thick / 2};
                d.raised = true;
                rg.decks_.push_back(d);
            }
            rg.ramparts_.push_back(rp);
        }
        for (const Continent::Castle::Tower& t : k.towers) {
            Obstacle o;
            o.kind = Obstacle::Kind::Tower;
            o.pos = toLocal(t.pos);
            o.pos.z = city.pos.z;
            o.radius = t.radius;
            o.height = t.height;
            // Soldiers on the wall-walk pass through the tower (its doors at walk level).
            o.passAbove = k.rings.front().height - Fixed::fromInt(3);
            rg.obstacles_.push_back(o);
        }
        {
            // The keep: four walls nobody walks on.
            const Vec3 kc = toLocal(k.keep), ax = k.keepAxis, ay{-ax.y, ax.x, Fixed::zero()};
            const Fixed hk = k.keepHalf;
            const Vec3 corner[4] = {kc + (ax + ay) * hk, kc + (ax - ay) * hk, kc - (ax + ay) * hk, kc - (ax - ay) * hk};
            for (int i = 0; i < 4; ++i) {
                Vec3 a = corner[i], b = corner[(i + 1) % 4];
                a.z = b.z = city.pos.z;
                rg.walls_.push_back({a, b, Fixed::one(), city.pos.z + k.keepHeight});
            }
        }
        for (const Continent::Castle::Stair& st : k.stairs) {
            const Vec3 foot = toLocal(st.foot), head = toLocal(st.head);
            const Vec3 dir = unit(head - foot);
            Deck d{{(foot.x + head.x) / 2, (foot.y + head.y) / 2, (city.pos.z + st.head.z) / 2}, dir, (head - foot).flat().length() / 2, st.halfWidth};
            d.rise = st.head.z - city.pos.z;
            d.raised = true;
            rg.decks_.push_back(d);
            // A level landing at the top, beside the walk.
            const Fixed landing = Fixed::fromInt(3);
            Deck top{{head.x + dir.x * landing / 2, head.y + dir.y * landing / 2, st.head.z}, dir, landing / 2, st.halfWidth};
            top.raised = true;
            rg.decks_.push_back(top);
            const Vec3 end = head + dir * (landing - Fixed::fromRatio(1, 2));
            if (!rg.ramparts_.empty()) rg.ramparts_[rg.ramparts_.size() - k.rings.size()].stairs.push_back({{foot.x, foot.y, city.pos.z}, {end.x, end.y, st.head.z}});
        }
        for (const Continent::Castle::House& hs : k.houses) {
            Obstacle o;
            o.kind = Obstacle::Kind::House;
            o.pos = toLocal(hs.pos);
            o.pos.z = city.pos.z;
            o.halfX = hs.halfX;
            o.halfY = hs.halfY;
            o.height = hs.height;
            rg.obstacles_.push_back(o);
        }
    }
    rg.berms_.push_back({{Vec3{rg.ford_.x - m(57), rg.ford_.y - m(72), Fixed::zero()},
                          Vec3{rg.ford_.x - m(50), rg.ford_.y + m(78), Fixed::zero()}}, r(7, 2)});
    for (int32_t iy = 0; iy < n; ++iy) {
        for (int32_t ix = 0; ix < n; ++ix) {
            const size_t i = static_cast<size_t>(iy) * n + ix;
            const Fixed d = distanceTo(rg.berms_, cell * ix, cell * iy, m(5));
            if (d < m(5) && rg.water_[i].raw == kDryRaw) shaped[i] += r(9, 5) * bump(d / 5);
        }
    }
    // Ponds: hollows in the open country holding still water, some shallow
    // enough to wade, some not. One candidate per 96 m block.
    {
        const uint64_t sp = noise::nextSeed(c.seed() ^ 0x90D5);
        const int64_t block = 96;
        const int64_t bx0 = ox / block, by0 = oy / block;
        for (int64_t by = by0; by * block < oy + cells * kCellSize; ++by) {
            for (int64_t bx = bx0; bx * block < ox + cells * kCellSize; ++bx) {
                if (noise::lattice(sp, bx, by) > r(1, 4)) continue;
                const Fixed px = m(bx * block + 20) + noise::lattice(sp ^ 1, bx, by) * 56 - OX;
                const Fixed py = m(by * block + 20) + noise::lattice(sp ^ 2, bx, by) * 56 - OY;
                const Fixed radius = m(6) + noise::lattice(sp ^ 3, bx, by) * 12;
                const Fixed depth = r(3, 5) + noise::lattice(sp ^ 4, bx, by) * r(6, 5);
                const int32_t ci = static_cast<int32_t>((px / cell).floorToInt()), cj = static_cast<int32_t>((py / cell).floorToInt());
                const int32_t reach = static_cast<int32_t>((radius * 2 / cell).floorToInt()) + 1;
                if (ci - reach < 2 || cj - reach < 2 || ci + reach > cells - 2 || cj + reach > cells - 2) continue;
                if (flatDist(px, py, rg.ford_) < m(140)) continue;
                bool town = false;  // not inside a city's walls
                for (const Continent::City& city : c.cities()) town = town || flatDist(px + OX, py + OY, city.pos) < city.radius * 2;
                if (town) continue;
                // Dry, gentle, open ground well clear of rivers, roads and the berm.
                bool ok = true;
                Fixed rim = m(100000), top = -m(100000);
                for (int32_t j = cj - reach; j <= cj + reach && ok; ++j) {
                    for (int32_t i = ci - reach; i <= ci + reach && ok; ++i) {
                        const size_t k = static_cast<size_t>(j) * n + i;
                        ok = rg.water_[k].raw == kDryRaw && roadDist[k] > m(12) && mountain[k] < m(30);
                        const Fixed d = flatDist(cell * i, cell * j, {px, py, Fixed::zero()});
                        if (d >= radius && d < radius + cell) rim = min(rim, shaped[k]);
                        if (d < radius) top = max(top, shaped[k]);
                    }
                }
                if (!ok || rim > m(99999) || top - rim > m(3)) continue;
                if (distanceTo(rg.berms_, px, py, radius + m(10)) < radius + m(8)) continue;
                const Fixed surface = rim - r(1, 4);
                for (int32_t j = cj - reach; j <= cj + reach; ++j) {
                    for (int32_t i = ci - reach; i <= ci + reach; ++i) {
                        const size_t k = static_cast<size_t>(j) * n + i;
                        const Fixed t = flatDist(cell * i, cell * j, {px, py, Fixed::zero()}) / radius;
                        if (t >= Fixed::one()) continue;
                        shaped[k] = min(shaped[k], surface - r(1, 5) - depth * (Fixed::one() - t * t));
                        rg.water_[k] = surface;
                    }
                }
                rg.ponds_.push_back({px, py, radius});
            }
        }
    }
    rg.terrain_ = Terrain::fromSamples(cells, cell, shaped);
    rg.waterLevel_ = c.sample(ford.x, ford.y).riverSurface;

    for (const Polyline& road : c.roads()) {
        Polyline local{{}, road.halfWidth};
        for (const Vec3& p : road.points) local.points.push_back(toLocal(p));
        rg.roads_.push_back(local);
    }
    for (const std::vector<Continent::River>* list : {&c.rivers(), &c.streams()}) {
        for (const Continent::River& rv : *list) {
            Polyline local{{}, rv.halfWidth};
            for (const Vec3& p : rv.points) local.points.push_back(toLocal(p));
            rg.rivers_.push_back(local);
        }
    }

    // Ground types per cell, from the cell's four corners.
    rg.ground_.resize(static_cast<size_t>(cells) * cells);
    for (int32_t iy = 0; iy < cells; ++iy) {
        for (int32_t ix = 0; ix < cells; ++ix) {
            const size_t i00 = static_cast<size_t>(iy) * n + ix, i10 = i00 + 1, i01 = i00 + n, i11 = i01 + 1;
            const Fixed a = shaped[i00], b = shaped[i10], cc = shaped[i01], d = shaped[i11];
            const Fixed z = (a + b + cc + d) / 4;
            const Fixed slope = max(max(abs(a - b), abs(cc - d)), max(abs(a - cc), abs(b - d))) / kCellSize;
            const Fixed x = OX + cell * ix + cell / 2, y = OY + cell * iy + cell / 2;
            int wet = 0;
            Fixed surface;
            for (size_t k : {i00, i10, i01, i11}) {
                if (rg.water_[k].raw == kDryRaw) continue;
                ++wet;
                surface += rg.water_[k];
            }
            const Fixed road = (roadDist[i00] + roadDist[i10] + roadDist[i01] + roadDist[i11]) / 4;
            Ground g;
            // Any cell the deck passes through, however narrow the deck.
            bool decked = false;
            Fixed deck;
            for (const Deck& dk : rg.decks_) {
                if (Deck{dk.centre, dk.dir, dk.halfSpan, dk.halfWidth + cell * 3 / 4}.contains(x - OX, y - OY)) {
                    decked = true;
                    deck = dk.centre.z;
                }
            }
            if (decked && deck > z + r(1, 2)) {
                g = Ground::Bridge;
            } else if (wet >= 2 && surface / wet > z) {
                g = surface / wet - z < r(11, 10) ? Ground::Shallows : Ground::DeepWater;
            } else if (slope > r(3, 5) && road >= m(3)) {
                g = Ground::Rock;
            } else {
                const Fixed mtn = (mountain[i00] + mountain[i10] + mountain[i01] + mountain[i11]) / 4;
                g = c.landGround(x, y, z, mtn, road);
            }
            rg.ground_[static_cast<size_t>(iy) * cells + ix] = g;
        }
    }

    // Mud: the trodden margins of the water, where the ground sits barely above
    // it, in patches; and the churned approaches to the ford.
    {
        const uint64_t sm = noise::nextSeed(c.seed() ^ 0x3D0D);
        std::vector<Ground> muddy = rg.ground_;
        for (int32_t iy = 2; iy + 2 < cells; ++iy) {
            for (int32_t ix = 2; ix + 2 < cells; ++ix) {
                const size_t k = static_cast<size_t>(iy) * cells + ix;
                const Ground g = rg.ground_[k];
                if (g != Ground::Plains && g != Ground::Road) continue;
                const Fixed x = cell * ix + cell / 2, y = cell * iy + cell / 2;
                const Fixed z = shaped[static_cast<size_t>(iy) * n + ix];
                bool margin = false;
                for (int32_t dy = -2; dy <= 2 && !margin; ++dy)
                    for (int32_t dx = -2; dx <= 2 && !margin; ++dx) {
                        const Fixed w = rg.water_[static_cast<size_t>(iy + dy) * n + ix + dx];
                        margin = w.raw != kDryRaw && z - w < r(9, 10);
                    }
                const Fixed patch = noise::fbm(sm, x, y, 24);
                const bool ford = g == Ground::Road && flatDist(x, y, rg.ford_) < m(40);
                if ((margin && patch > r(2, 5)) || ford) muddy[k] = Ground::Mud;
            }
        }
        rg.ground_.swap(muddy);
    }

    // A hamlet along the road west of the ford.
    {
        const Polyline* road = nullptr;
        Fixed nearest = m(1000000);
        for (const Polyline& rd : rg.roads_) {
            const Fixed d = distanceTo({rd}, rg.ford_.x, rg.ford_.y, m(1000000));
            if (d < nearest) {
                nearest = d;
                road = &rd;
            }
        }
        int placed = 0;
        for (int64_t along = 120; road && along <= 330 && placed < 9; along += 24) {
            // Walk west from the ford along the road's general direction.
            const Fixed px = rg.ford_.x - m(along);
            Fixed bestD = m(1000000), py = rg.ford_.y;
            for (int64_t dy = -200; dy <= 200; dy += 2) {
                const Fixed d = distanceTo({*road}, px, rg.ford_.y + m(dy), m(1000));
                if (d < bestD) {
                    bestD = d;
                    py = rg.ford_.y + m(dy);
                }
            }
            const Fixed side = (placed % 2 == 0) ? m(15) : -m(15);
            const Fixed hx = px, hy = py + side;
            if (rg.groundAt(hx, hy) == Ground::DeepWater || rg.groundAt(hx, hy) == Ground::Shallows) continue;
            Obstacle o;
            o.kind = Obstacle::Kind::House;
            o.pos = {hx, hy, rg.terrain_.heightAt(hx, hy)};
            o.halfX = r(18, 5);
            o.halfY = r(13, 5);
            o.height = r(9, 2);
            o.variant = static_cast<uint8_t>(placed % 3);
            rg.obstacles_.push_back(o);
            ++placed;
        }
    }
    // A shrine of standing pillars on the highest dry ground in the window.
    Vec3 shrine{m(200), m(800), Fixed::zero()};
    {
        Fixed top = -m(1000);
        for (int32_t iy = 32; iy < cells - 32; iy += 4) {
            for (int32_t ix = 32; ix < cells - 32; ix += 4) {
                const Fixed z = shaped[static_cast<size_t>(iy) * n + ix];
                const Ground g = rg.cellGround(ix, iy);
                if (z > top && g != Ground::Rock && g != Ground::Shallows && g != Ground::DeepWater) {
                    top = z;
                    shrine = {cell * ix, cell * iy, z};
                }
            }
        }
        for (int i = 0; i < 8; ++i) {
            Obstacle o;
            o.kind = Obstacle::Kind::Pillar;
            const Fixed angle = kTwoPi * i / 8;
            const Fixed x = shrine.x + cos(angle) * 10, y = shrine.y + sin(angle) * 10;
            o.pos = {x, y, rg.terrain_.heightAt(x, y)};
            o.radius = r(3, 5);
            o.height = i % 3 == 1 ? r(8, 5) : r(11, 2);
            o.variant = static_cast<uint8_t>(i % 3);
            rg.obstacles_.push_back(o);
        }
        Obstacle altar;
        altar.kind = Obstacle::Kind::House;
        altar.pos = {shrine.x, shrine.y, rg.terrain_.heightAt(shrine.x, shrine.y)};
        altar.halfX = r(3, 2);
        altar.halfY = r(3, 2);
        altar.height = r(6, 5);
        altar.variant = 3;
        rg.obstacles_.push_back(altar);
    }
    rg.scatterScenery(c.seed() ^ static_cast<uint64_t>(ox * 31 + oy), rg.obstacles_.size(), shrine, m(15));
    return rg;
}

uint64_t Region::checksum() const {
    StateHasher h;
    for (Fixed s : terrain_.samples()) h.addI64(s.raw);
    for (Ground g : ground_) h.addU64(static_cast<uint64_t>(g));
    h.addI64(origin_.x.raw);
    h.addI64(origin_.y.raw);
    for (Fixed w : water_) h.addI64(w.raw);
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
    for (const Deck& d : decks_)
        for (Fixed v : {d.centre.x, d.centre.y, d.centre.z, d.dir.x, d.dir.y, d.halfSpan, d.halfWidth, d.rise}) h.addI64(v.raw);
    for (const WallSeg& w : walls_)
        for (Fixed v : {w.a.x, w.a.y, w.b.x, w.b.y, w.halfThick, w.top}) h.addI64(v.raw);
    return h.value();
}

}  // namespace illyria::sim
