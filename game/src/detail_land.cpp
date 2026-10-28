#include "illyria/game/detail_land.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "illyria/sim/noise.h"
#include "illyria/sim/region.h"

namespace illyria::game {

namespace {

using sim::Fixed;
using Seg = DetailLand::Seg;
constexpr Fixed m(int64_t v) { return Fixed::fromInt(v); }
constexpr Fixed r(int64_t num, int64_t den) { return Fixed::fromRatio(num, den); }
constexpr int64_t kCellM = 4;
constexpr int kMacro = 4;  // samples between landform samples (16 m), as Region::fromContinent
constexpr int kApron = 1;  // samples and cells written beyond a chunk's own, for the shaders' neighbours
constexpr int kShapedSide = DetailLand::kChunk + 2 * kApron + 1;  // shaped samples -1..17

int floorDiv(int v, int d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }

// Segments of polylines whose bounding boxes come within `reach` of the box.
template <typename Lines, typename PointsOf>
std::vector<Seg> segmentsNear(const Lines& lines, PointsOf points, float ox, float oy, float x0, float y0, float x1, float y1, float reach) {
    std::vector<Seg> out;
    for (const auto& l : lines) {
        const std::vector<sim::Vec3>& p = points(l);
        for (size_t i = 0; i + 1 < p.size(); ++i) {
            const float ax = static_cast<float>(p[i].x.toDouble()) - ox, ay = static_cast<float>(p[i].y.toDouble()) - oy;
            const float bx = static_cast<float>(p[i + 1].x.toDouble()) - ox, by = static_cast<float>(p[i + 1].y.toDouble()) - oy;
            if (std::max(ax, bx) < x0 - reach || std::min(ax, bx) > x1 + reach || std::max(ay, by) < y0 - reach || std::min(ay, by) > y1 + reach)
                continue;
            out.push_back({ax, ay, bx, by});
        }
    }
    return out;
}

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void DetailLand::reset(std::shared_ptr<const sim::Continent> continent, V3 origin, int hole) {
    continent_ = std::move(continent);
    ox_ = m(static_cast<int64_t>(std::lround(origin.x)));
    oy_ = m(static_cast<int64_t>(std::lround(origin.y)));
    hole_ = hole;
    detailSeed_ = continent_ ? sim::noise::nextSeed(continent_->seed() ^ 0xDE7A11) : 0;
    mudSeed_ = continent_ ? sim::noise::nextSeed(continent_->seed() ^ 0x3D0D) : 0;
    scenerySeed_ = continent_ ? sim::Region::sceneryContinentSeed(continent_->seed()) : 0;
    wx_ = wy_ = INT32_MIN / 2;
    ++generation_;
    slots_.assign(static_cast<size_t>(kSlots) * kSlots, Slot{});
    lattice_.assign(slots_.size(), {});
    scenery_.assign(slots_.size(), {});
    raw_.assign(static_cast<size_t>(kSamples) * kSamples, Raw{});
    height_.assign(static_cast<size_t>(kSamples) * kSamples, 0.0f);
    water_.assign(height_.size(), -1e6f);
    ground_.assign(height_.size(), 0);
    flow_.assign(height_.size() * 2, 128);
    road_.assign(static_cast<size_t>(kFine) * kFine * 2, 0);
    canopy_.assign(static_cast<size_t>(kFine) * kFine, 0);
    shaped_.assign(static_cast<size_t>(kShapedSide) * kShapedSide, Fixed{});
    finishing_ = -1;
}

bool DetailLand::inHole(int cx, int cy) const {
    const int n = hole_ / kChunk;
    return cx >= 0 && cy >= 0 && cx < n && cy < n;
}

bool DetailLand::shown(int s) const {
    const Slot& sl = slots_[static_cast<size_t>(s)];
    return sl.finished && sl.cx > wx_ && sl.cy > wy_ && sl.cx < wx_ + kSlots - 1 && sl.cy < wy_ + kSlots - 1 && !inHole(sl.cx, sl.cy);
}

void DetailLand::claim(int cx, int cy) {
    const int s = slotOf(cx, cy);
    Slot& sl = slots_[static_cast<size_t>(s)];
    if (sl.cx == cx && sl.cy == cy) return;
    sl = Slot{};
    scenery_[static_cast<size_t>(s)].clear();
    sl.cx = cx;
    sl.cy = cy;
    if (finishing_ == s) finishing_ = -1;
    // Its neighbours' edges were written from the chunk that was here: redo them.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            Slot& n = slots_[static_cast<size_t>(slotOf(cx + dx, cy + dy))];
            if (slotOf(cx + dx, cy + dy) == finishing_) finishing_ = -1;
            if (n.finished || n.step > 0) {
                n.finished = false;
                n.step = 0;
            }
        }
    }
}

void DetailLand::recentre(float x, float y) {
    if (!active()) return;
    const int fx = static_cast<int>(std::floor(x / kChunkSize)), fy = static_cast<int>(std::floor(y / kChunkSize));
    focusX_ = x;
    focusY_ = y;
    const int cx = wx_ + kSlots / 2, cy = wy_ + kSlots / 2;
    if (std::abs(fx - cx) <= 1 && std::abs(fy - cy) <= 1) return;
    wx_ = fx - kSlots / 2;
    wy_ = fy - kSlots / 2;
    for (int j = 0; j < kSlots; ++j)
        for (int i = 0; i < kSlots; ++i) claim(wx_ + i, wy_ + j);
    ++generation_;
}

void DetailLand::rawRow(int s) {
    Slot& sl = slots_[static_cast<size_t>(s)];
    const sim::Continent& c = *continent_;
    std::vector<sim::Continent::Sample>& lat = lattice_[static_cast<size_t>(s)];
    constexpr int kLat = kChunk / kMacro + 1;
    if (sl.rawRows == 0) {
        // Coast and mountains change slowly: every 16 m, interpolated, as a battlefield does.
        lat.resize(static_cast<size_t>(kLat) * kLat);
        for (int j = 0; j < kLat; ++j)
            for (int i = 0; i < kLat; ++i)
                lat[static_cast<size_t>(j) * kLat + i] =
                    c.landform(ox_ + m((sl.cx * kChunk + i * kMacro) * kCellM), oy_ + m((sl.cy * kChunk + j * kMacro) * kCellM));
    }
    const int j = sl.rawRows;
    const int sy = sl.cy * kChunk + j;
    for (int i = 0; i < kChunk; ++i) {
        const int sx = sl.cx * kChunk + i;
        const int mx = i / kMacro, my = j / kMacro;
        const Fixed tx = Fixed::fromRatio(i - mx * kMacro, kMacro), ty = Fixed::fromRatio(j - my * kMacro, kMacro);
        const auto lerp2 = [&](auto field) {
            const Fixed a = field(lat[static_cast<size_t>(my) * kLat + mx]), b = field(lat[static_cast<size_t>(my) * kLat + mx + 1]);
            const Fixed cc = field(lat[static_cast<size_t>(my + 1) * kLat + mx]), d = field(lat[static_cast<size_t>(my + 1) * kLat + mx + 1]);
            const Fixed top = a + (b - a) * tx, bottom = cc + (d - cc) * tx;
            return top + (bottom - top) * ty;
        };
        sim::Continent::Sample smp;
        smp.height = lerp2([](const sim::Continent::Sample& q) { return q.height; });
        smp.land = lerp2([](const sim::Continent::Sample& q) { return q.land; });
        smp.mountain = lerp2([](const sim::Continent::Sample& q) { return q.mountain; });
        const Fixed x = ox_ + m(sx * kCellM), y = oy_ + m(sy * kCellM);
        // Fine detail. A battlefield fades it out toward its edges to meet the
        // coarse land; out here it fades in the same way from the battlefield's
        // edge, so the two meet without a step.
        int edge;
        if (sx >= 0 && sy >= 0 && sx <= hole_ && sy <= hole_) edge = std::min(std::min(sx, sy), std::min(hole_ - sx, hole_ - sy));
        else edge = std::max(std::max(-sx, sx - hole_), std::max(-sy, sy - hole_));
        const Fixed fade = sim::min(Fixed::one(), m(edge * kCellM) / 48);
        smp.height += (sim::noise::fbm(detailSeed_, x, y, 48) - r(1, 2)) * (m(2) + smp.mountain / 20) * fade;
        smp.height += (sim::noise::value(detailSeed_ ^ 0xB0B, x, y, 14) - r(1, 2)) * r(4, 5) * fade;
        c.carve(smp, x, y);
        Raw& out = raw_[static_cast<size_t>(wrap(sy, kSamples)) * kSamples + wrap(sx, kSamples)];
        out.h = smp.height;
        out.roadDist = smp.roadDist;
        out.mountain = smp.mountain;
        Fixed surface;
        out.water = c.waterSurfaceOf(smp, surface) ? surface : Fixed::fromRaw(sim::Region::kDryRaw);
    }
    ++sl.rawRows;
}

Fixed DetailLand::shapedAt(int sx, int sy) const {
    // Roads settle into smoothed, slightly sunken beds (not under water).
    const Raw& rw = raw(sx, sy);
    if (rw.roadDist >= m(7) || rw.water.raw != sim::Region::kDryRaw) return rw.h;
    Fixed sum;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) sum += raw(sx + dx, sy + dy).h;
    const Fixed smooth = sum / 25;
    const Fixed w = sim::noise::smoothstep(m(2), m(7), rw.roadDist);
    return smooth + (rw.h - smooth) * w - (Fixed::one() - w) * r(3, 10);
}

void DetailLand::growScenery(int s, const std::vector<Seg>& roads) {
    // Trees, undergrowth and rocks: the continent's 5 m scenery grid, with
    // the same rules and exclusions as Region::scatterScenery, for the points
    // that fall in this chunk.
    Slot& sl = slots_[static_cast<size_t>(s)];
    const sim::Continent& c = *continent_;
    std::vector<sim::Obstacle>& out = scenery_[static_cast<size_t>(s)];
    out.clear();
    const int64_t step = sim::Region::kSceneryStep;
    const int64_t ox = ox_.floorToInt(), oy = oy_.floorToInt();
    const int64_t x0 = ox + sl.cx * static_cast<int64_t>(kChunkSize), y0 = oy + sl.cy * static_cast<int64_t>(kChunkSize);
    const int64_t x1 = x0 + static_cast<int64_t>(kChunkSize), y1 = y0 + static_cast<int64_t>(kChunkSize);
    const auto floorDiv64 = [](int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); };
    // The castles near the chunk: nothing grows inside the walls or right against them.
    struct Keep {
        float x, y, reach;
    };
    std::vector<Keep> keeps;
    const float lx0 = sl.cx * kChunkSize, ly0 = sl.cy * kChunkSize, ox_f = static_cast<float>(ox), oy_f = static_cast<float>(oy);
    for (const sim::Continent::City& city : c.cities()) {
        const float cx = static_cast<float>(city.pos.x.toDouble()) - ox_f, cy = static_cast<float>(city.pos.y.toDouble()) - oy_f;
        for (const sim::Continent::Castle::Ring& ring : city.castle.rings) {
            float reach = 0;
            for (const sim::Vec3& q : ring.corners)
                reach = std::max(reach, std::hypot(static_cast<float>(q.x.toDouble()) - ox_f - cx, static_cast<float>(q.y.toDouble()) - oy_f - cy));
            reach += 12;
            if (cx + reach < lx0 || cy + reach < ly0 || cx - reach > lx0 + kChunkSize || cy - reach > ly0 + kChunkSize) continue;
            keeps.push_back({cx, cy, reach});
        }
    }
    const auto groundOf = [&](float x, float y) {
        const int ix = static_cast<int>(std::floor(x / kCell)), iy = static_cast<int>(std::floor(y / kCell));
        return static_cast<sim::Ground>(ground_[static_cast<size_t>(wrap(iy, kSamples)) * kSamples + wrap(ix, kSamples)] / 16);
    };
    for (int64_t gy = floorDiv64(y0, step) - 1; gy * step < y1; ++gy) {
        for (int64_t gx = floorDiv64(x0, step) - 1; gx * step < x1; ++gx) {
            const sim::Region::SceneryPoint pt = sim::Region::sceneryPoint(scenerySeed_, gx, gy);
            if (pt.x < m(x0) || pt.y < m(y0) || pt.x >= m(x1) || pt.y >= m(y1)) continue;
            const Fixed lx = pt.x - ox_, ly = pt.y - oy_;
            const float x = static_cast<float>(lx.toDouble()), y = static_cast<float>(ly.toDouble());
            const sim::Ground g = groundOf(x, y);
            if (g == sim::Ground::Road || g == sim::Ground::Shallows || g == sim::Ground::DeepWater || g == sim::Ground::Bridge) continue;
            bool clear = true;
            for (const Seg& sg : roads) {
                const float abx = sg.bx - sg.ax, aby = sg.by - sg.ay, len2 = abx * abx + aby * aby;
                const float t = len2 > 0 ? std::clamp(((x - sg.ax) * abx + (y - sg.ay) * aby) / len2, 0.0f, 1.0f) : 0.0f;
                const float ex = sg.ax + abx * t - x, ey = sg.ay + aby * t - y;
                if (ex * ex + ey * ey < 4.5f * 4.5f) clear = false;
            }
            for (const Keep& k : keeps) clear = clear && std::hypot(x - k.x, y - k.y) >= k.reach;
            for (const sim::Deck& d : decks_) clear = clear && !sim::Deck{d.centre, d.dir, d.halfSpan + m(8), d.halfWidth}.contains(lx, ly);
            if (!clear) continue;
            bool byWater = false;
            for (int k = 0; k < 4 && !byWater; ++k) {
                const sim::Ground w = groundOf(x + (k == 0 ? 4.0f : k == 1 ? -4.0f : 0.0f), y + (k == 2 ? 4.0f : k == 3 ? -4.0f : 0.0f));
                byWater = w == sim::Ground::Shallows || w == sim::Ground::DeepWater;
            }
            float z = 0;
            heightAt(x, y, z, true);
            sim::Obstacle o;
            if (!sim::Region::sceneryAt(scenerySeed_, pt, g, byWater, Fixed::fromRaw(static_cast<int64_t>(std::llround(static_cast<double>(z) * Fixed::kOneRaw))), o)) continue;
            o.pos = {lx, ly, Fixed::fromRaw(static_cast<int64_t>(std::llround(static_cast<double>(z) * Fixed::kOneRaw)))};
            out.push_back(o);
        }
    }
}

bool DetailLand::finishStep(int s) {
    Slot& sl = slots_[static_cast<size_t>(s)];
    const sim::Continent& c = *continent_;
    const int bx = sl.cx * kChunk, by = sl.cy * kChunk;  // first sample
    const auto shaped = [&](int sx, int sy) {
        return shaped_[static_cast<size_t>(sy - by + kApron) * kShapedSide + (sx - bx + kApron)];
    };
    if (finishing_ != s) {
        if (finishing_ >= 0) slots_[static_cast<size_t>(finishing_)].step = 0;  // left half done: start over later
        finishing_ = s;
        sl.step = 0;
    }
    constexpr int kRows = kChunk + 2 * kApron;  // ground rows -1..16
    if (sl.step == 0) {
        for (int j = 0; j < kShapedSide; ++j)
            for (int i = 0; i < kShapedSide; ++i)
                shaped_[static_cast<size_t>(j) * kShapedSide + i] = shapedAt(bx - kApron + i, by - kApron + j);
        // Bridge decks near the chunk mark their cells, as on a battlefield.
        decks_.clear();
        const float ox = static_cast<float>(ox_.toDouble()), oy = static_cast<float>(oy_.toDouble());
        for (const sim::Continent::Crossing& cr : c.crossings()) {
            if (!cr.bridged()) continue;
            const float px = static_cast<float>(cr.pos.x.toDouble()) - ox, py = static_cast<float>(cr.pos.y.toDouble()) - oy;
            const float reach = static_cast<float>((cr.halfSpan + cr.halfWidth).toDouble()) + 8;
            if (px < bx * kCell - reach || py < by * kCell - reach || px > (bx + kChunk) * kCell + reach || py > (by + kChunk) * kCell + reach) continue;
            decks_.push_back({{cr.pos.x - ox_, cr.pos.y - oy_, cr.deck}, cr.dir, cr.halfSpan, cr.halfWidth + m(3)});
        }
        ++sl.step;
        return false;
    }
    if (sl.step <= kRows) {
        // One row of cells: their ground kind from the four corners.
        const int iy = by - kApron + sl.step - 1;
        for (int ix = bx - kApron; ix < bx + kChunk + kApron; ++ix) {
            const Fixed a = shaped(ix, iy), b = shaped(ix + 1, iy), cc = shaped(ix, iy + 1), d = shaped(ix + 1, iy + 1);
            const Fixed z = (a + b + cc + d) / 4;
            const Fixed slope = sim::max(sim::max(sim::abs(a - b), sim::abs(cc - d)), sim::max(sim::abs(a - cc), sim::abs(b - d))) / kCellM;
            const Fixed lx = m(ix * kCellM) + m(kCellM) / 2, ly = m(iy * kCellM) + m(kCellM) / 2;
            const Fixed x = ox_ + lx, y = oy_ + ly;
            int wet = 0;
            Fixed surface, road, mtn;
            for (int k = 0; k < 4; ++k) {
                const Raw& rw = raw(ix + (k & 1), iy + (k >> 1));
                road += rw.roadDist;
                mtn += rw.mountain;
                if (rw.water.raw == sim::Region::kDryRaw) continue;
                ++wet;
                surface += rw.water;
            }
            road = road / 4;
            mtn = mtn / 4;
            bool decked = false;
            Fixed deck;
            for (const sim::Deck& dk : decks_) {
                if (dk.contains(lx, ly)) {
                    decked = true;
                    deck = dk.centre.z;
                }
            }
            sim::Ground g;
            if (decked && deck > z + r(1, 2)) {
                g = sim::Ground::Bridge;
            } else if (wet >= 2 && surface / wet > z) {
                g = surface / wet - z < r(11, 10) ? sim::Ground::Shallows : sim::Ground::DeepWater;
            } else if (slope > r(3, 5) && road >= m(3)) {
                g = sim::Ground::Rock;
            } else {
                g = c.landGround(x, y, z, mtn, road);
            }
            // Mud: the trodden margins of the water, in patches.
            if (g == sim::Ground::Plains || g == sim::Ground::Road) {
                const Fixed zc = a;
                bool margin = false;
                for (int dy = -2; dy <= 2 && !margin; ++dy)
                    for (int dx = -2; dx <= 2 && !margin; ++dx) {
                        const Fixed w = raw(ix + dx, iy + dy).water;
                        margin = w.raw != sim::Region::kDryRaw && zc - w < r(9, 10);
                    }
                if (margin && sim::noise::fbm(mudSeed_, lx, ly, 24) > r(2, 5)) g = sim::Ground::Mud;
            }
            ground_[static_cast<size_t>(wrap(iy, kSamples)) * kSamples + wrap(ix, kSamples)] = static_cast<uint8_t>(static_cast<int>(g) * 16);
        }
        ++sl.step;
        return false;
    }

    // Heights and water, with an apron, and their flow.
    const float ox = static_cast<float>(ox_.toDouble()), oy = static_cast<float>(oy_.toDouble());
    const float x0 = bx * kCell, y0 = by * kCell, x1 = (bx + kChunk) * kCell, y1 = (by + kChunk) * kCell;
    sl.wet = false;
    std::vector<Seg> rivers;
    bool rivSearched = false;
    for (int sy = by - kApron; sy <= by + kChunk + kApron; ++sy) {
        for (int sx = bx - kApron; sx <= bx + kChunk + kApron; ++sx) {
            const size_t k = static_cast<size_t>(wrap(sy, kSamples)) * kSamples + wrap(sx, kSamples);
            height_[k] = static_cast<float>(shaped(sx, sy).toDouble());
            const Raw& rw = raw(sx, sy);
            if (rw.water.raw == sim::Region::kDryRaw) {
                water_[k] = -1e6f;
                continue;
            }
            water_[k] = static_cast<float>(rw.water.toDouble());
            sl.wet = true;
            // Which way the water runs: along the nearest river or creek, downstream.
            if (!rivSearched) {
                rivSearched = true;
                const auto pts = [](const sim::Continent::River& rv) -> const std::vector<sim::Vec3>& { return rv.points; };
                rivers = segmentsNear(c.rivers(), pts, ox, oy, x0, y0, x1, y1, 60);
                std::vector<Seg> st = segmentsNear(c.streams(), pts, ox, oy, x0, y0, x1, y1, 60);
                rivers.insert(rivers.end(), st.begin(), st.end());
            }
            const float px = sx * kCell, py = sy * kCell;
            float best = 60.0f * 60.0f, dx = 0, dy = 0;
            for (const Seg& sg : rivers) {
                const float abx = sg.bx - sg.ax, aby = sg.by - sg.ay, len2 = abx * abx + aby * aby;
                if (len2 <= 0) continue;
                const float t = std::clamp(((px - sg.ax) * abx + (py - sg.ay) * aby) / len2, 0.0f, 1.0f);
                const float ex = sg.ax + abx * t - px, ey = sg.ay + aby * t - py, d2 = ex * ex + ey * ey;
                if (d2 < best) {
                    best = d2;
                    const float len = std::sqrt(len2);
                    dx = abx / len;
                    dy = aby / len;
                }
            }
            flow_[k * 2] = static_cast<uint8_t>(std::clamp(128.0f + dx * 127.0f, 0.0f, 255.0f));
            flow_[k * 2 + 1] = static_cast<uint8_t>(std::clamp(128.0f + dy * 127.0f, 0.0f, 255.0f));
        }
    }

    const auto pts = [](const sim::Polyline& p) -> const std::vector<sim::Vec3>& { return p.points; };
    const std::vector<Seg> roads = segmentsNear(c.roads(), pts, ox, oy, x0, y0, x1, y1, 16);
    growScenery(s, roads);

    // Road field at 2 m (signed distance across the nearest road, for ruts) and
    // the shade of the trees' crowns, kFineApron texels beyond the chunk: enough
    // for filtering, and for the crowns of this chunk's trees to reach into its
    // neighbours' (whose trees, where they are built, are counted too).
    std::vector<const sim::Obstacle*> crowns;
    for (int k = 0; k < 9; ++k) {
        const Slot& n = slots_[static_cast<size_t>(slotOf(sl.cx + k % 3 - 1, sl.cy + k / 3 - 1))];
        if (k != 4 && !n.finished) continue;
        for (const sim::Obstacle& o : scenery_[static_cast<size_t>(slotOf(sl.cx + k % 3 - 1, sl.cy + k / 3 - 1))])
            if (o.kind == sim::Obstacle::Kind::Pine || o.kind == sim::Obstacle::Kind::Oak || o.kind == sim::Obstacle::Kind::Bush) crowns.push_back(&o);
    }
    const int fx0 = bx * 2 - kFineApron, fy0 = by * 2 - kFineApron, fn = kChunk * 2 + 2 * kFineApron;
    for (int ty = fy0; ty < fy0 + fn; ++ty) {
        for (int tx = fx0; tx < fx0 + fn; ++tx) {
            const size_t k = static_cast<size_t>(wrap(ty, kFine)) * kFine + wrap(tx, kFine);
            const float px = (tx + 0.5f) * 2.0f, py = (ty + 0.5f) * 2.0f;
            float best = 8.0f;
            uint8_t lateral = 0, weight = 0;
            for (const Seg& sg : roads) {
                const float abx = sg.bx - sg.ax, aby = sg.by - sg.ay, len2 = abx * abx + aby * aby;
                if (len2 <= 0) continue;
                const float t = std::clamp(((px - sg.ax) * abx + (py - sg.ay) * aby) / len2, 0.0f, 1.0f);
                const float ex = sg.ax + abx * t - px, ey = sg.ay + aby * t - py;
                const float dist = std::sqrt(ex * ex + ey * ey);
                if (dist >= best) continue;
                best = dist;
                const float side = (abx * (py - sg.ay) - aby * (px - sg.ax)) >= 0 ? 1.0f : -1.0f;
                lateral = static_cast<uint8_t>(std::clamp(128.0f + side * dist * 16.0f, 0.0f, 255.0f));
                weight = 255;
            }
            road_[k * 2] = lateral;
            road_[k * 2 + 1] = weight;
            // Groves shade the ground beneath them, as on a battlefield.
            float shade = 0;
            for (const sim::Obstacle* o : crowns) {
                const float crown = o->kind == sim::Obstacle::Kind::Pine ? 2.6f : o->kind == sim::Obstacle::Kind::Oak ? 3.6f : 1.2f;
                const float dx = px - static_cast<float>(o->pos.x.toDouble()), dy = py - static_cast<float>(o->pos.y.toDouble());
                const float reach = crown * 1.6f;
                if (std::fabs(dx) >= reach || std::fabs(dy) >= reach) continue;
                const float d = std::sqrt(dx * dx + dy * dy) / reach;
                if (d < 1) shade += (1 - d * d) * (o->kind == sim::Obstacle::Kind::Bush ? 0.3f : 0.6f);
            }
            canopy_[k] = static_cast<uint8_t>(std::min(1.0f, shade) * 255);
        }
    }
    sl.finished = true;
    finishing_ = -1;
    ++generation_;
    return true;
}

std::vector<int> DetailLand::work(double budgetMs) {
    std::vector<int> done;
    if (!active() || wx_ == INT32_MIN / 2) return done;
    const double start = nowMs();
    // The window's inner chunks still to build, nearest the focus first.
    std::vector<std::pair<float, int>> order;
    for (int j = 1; j < kSlots - 1; ++j) {
        for (int i = 1; i < kSlots - 1; ++i) {
            const int cx = wx_ + i, cy = wy_ + j;
            if (inHole(cx, cy) || slots_[static_cast<size_t>(slotOf(cx, cy))].finished) continue;
            const float dx = (cx + 0.5f) * kChunkSize - focusX_, dy = (cy + 0.5f) * kChunkSize - focusY_;
            order.push_back({dx * dx + dy * dy, slotOf(cx, cy)});
        }
    }
    std::sort(order.begin(), order.end());
    for (const auto& [d2, s] : order) {
        (void)d2;
        const Slot& sl = slots_[static_cast<size_t>(s)];
        while (!sl.finished) {
            if (nowMs() - start > budgetMs) return done;
            // Its own raw samples and its neighbours' first.
            int need = -1;
            for (int k = 0; k < 9 && need < 0; ++k) {
                const int n = slotOf(sl.cx + k % 3 - 1, sl.cy + k / 3 - 1);
                if (slots_[static_cast<size_t>(n)].rawRows < kChunk) need = n;
            }
            if (need >= 0) {
                rawRow(need);
                continue;
            }
            if (finishStep(s)) done.push_back(s);
        }
    }
    return done;
}

void DetailLand::finishAll() {
    while (true) {
        const std::vector<int> d = work(1e9);
        if (d.empty()) return;
    }
}

bool DetailLand::heightAt(float x, float y, float& z, bool unchecked) const {
    if (!active()) return false;
    const float fx = x / kCell, fy = y / kCell;
    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
    const Slot& sl = slots_[static_cast<size_t>(slotOf(floorDiv(ix, kChunk), floorDiv(iy, kChunk)))];
    if (!unchecked && (!sl.finished || sl.cx != floorDiv(ix, kChunk) || sl.cy != floorDiv(iy, kChunk))) return false;
    const auto h = [&](int i, int j) { return height_[static_cast<size_t>(wrap(j, kSamples)) * kSamples + wrap(i, kSamples)]; };
    const float tx = fx - ix, ty = fy - iy;
    const float a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    z = tx >= ty ? a + (b - a) * tx + (d - b) * ty : a + (c - a) * ty + (d - c) * tx;
    return true;
}

}  // namespace illyria::game
