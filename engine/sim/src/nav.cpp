#include "illyria/sim/nav.h"

#include <algorithm>
#include <climits>
#include <queue>

namespace illyria::sim {

namespace {

// Phones route hundreds of soldiers: a search gives up after this many cells.
constexpr int32_t kMaxExpansions = 20000;
// Path smoothing looks this many waypoints ahead for a shortcut.
constexpr size_t kSmoothWindow = 24;
// Moves shorter than this many cells go straight if they can.
constexpr int64_t kShortMoveCells = 2;
// Snap a start or goal on blocked ground (a deck, a wall's foot) to an open cell this near.
constexpr int32_t kSnapCells = 4;

// Step cost per metre, x100: time over level plains is 100.
int32_t perMetre(Gait gait, Ground g) {
    const Fixed s = gaitSpeed(gait, g);
    if (s <= Fixed::zero()) return 0;
    return static_cast<int32_t>((Fixed::fromInt(100) / s).floorToInt());
}

}  // namespace

NavGrid::NavGrid(const Region& region) : terrain_(&region.terrain()), decks_(region.decks()) {
    const Terrain& t = region.terrain();
    if (!region.walls().empty()) cell_ = kCastleCellSize;
    sub_ = static_cast<int32_t>(cell_ / t.cellSize().floorToInt());
    const int32_t sub = sub_;
    n_ = t.cells() / sub;
    fineCells_ = t.cells();
    fineSize_ = t.cellSize();
    fine_.resize(static_cast<size_t>(fineCells_) * fineCells_);
    for (int32_t y = 0; y < fineCells_; ++y)
        for (int32_t x = 0; x < fineCells_; ++x) fine_[static_cast<size_t>(y) * fineCells_ + x] = static_cast<uint8_t>(region.cellGround(x, y));
    const size_t count = static_cast<size_t>(n_) * n_;
    open_.assign(count, 0);
    mask_.assign(count, 0);
    steep_.assign(count, 0);
    height_.assign(count, Fixed::zero());
    // Cells an open deck passes over carry the way over the water, however narrow.
    std::vector<uint8_t> decked(count, 0);
    for (const Deck& d : decks_) {
        if (d.raised) continue;
        const int64_t steps = (d.halfSpan * 2).floorToInt() + 1;
        for (int64_t i = 0; i <= steps; ++i) {
            const Vec3 p = d.centre + d.dir * (d.halfSpan * (Fixed::fromRatio(2 * i, steps) - Fixed::one()));
            decked[static_cast<size_t>(index(cellOf(p.x), cellOf(p.y)))] = 1;
        }
    }
    for (int32_t y = 0; y < n_; ++y) {
        for (int32_t x = 0; x < n_; ++x) {
            // Deep water anywhere in the cell blocks it; otherwise each ground
            // found in it is a way across. A bridge carries the way over.
            const int32_t k = index(x, y);
            uint16_t mask = 0;
            bool water = false, bridge = decked[static_cast<size_t>(k)] != 0;
            for (int32_t sy = 0; sy < sub; ++sy) {
                for (int32_t sx = 0; sx < sub; ++sx) {
                    const Ground g = region.cellGround(x * sub + sx, y * sub + sy);
                    water = water || g == Ground::DeepWater;
                    bridge = bridge || g == Ground::Bridge;
                    if (g != Ground::DeepWater) mask = static_cast<uint16_t>(mask | (1u << static_cast<unsigned>(g)));
                }
            }
            if (bridge) {
                water = false;
                mask = static_cast<uint16_t>(1u << static_cast<unsigned>(Ground::Bridge));
            }
            // A face too steep to walk (a cliff) is a wall; a steep one is slow.
            const Fixed step = t.cellSize() * sub;
            const Vec3 p0{t.cellSize() * (x * sub), t.cellSize() * (y * sub), Fixed::zero()};
            const Fixed h00 = t.heightAt(p0.x, p0.y), h10 = t.heightAt(p0.x + step, p0.y);
            const Fixed h01 = t.heightAt(p0.x, p0.y + step), h11 = t.heightAt(p0.x + step, p0.y + step);
            const Fixed slope = max(max(abs(h10 - h00), abs(h11 - h01)), max(abs(h01 - h00), abs(h11 - h10))) / step;
            if (!bridge && slope > Fixed::one()) water = true;
            steep_[static_cast<size_t>(k)] = !bridge && slope > kSteepSlope;
            height_[static_cast<size_t>(k)] = t.heightAt(p0.x + step / 2, p0.y + step / 2);
            mask_[static_cast<size_t>(k)] = mask;
            open_[static_cast<size_t>(k)] = !water && mask != 0;
        }
    }
    // Castle walls and towers close the cells they stand in (the centre within reach).
    std::vector<Obstacle> towers;
    for (const Obstacle& o : region.obstacles())
        if (o.kind == Obstacle::Kind::Tower) towers.push_back(o);
    for (int32_t y = 0; y < n_; ++y) {
        for (int32_t x = 0; x < n_; ++x) {
            if (region.walls().empty()) break;
            const Vec3 p{Fixed::fromInt(x * cell_) + Fixed::fromInt(cell_) / 2, Fixed::fromInt(y * cell_) + Fixed::fromInt(cell_) / 2, Fixed::zero()};
            for (const WallSeg& w : region.walls()) {
                const Vec3 ab = (w.b - w.a).flat();
                const Fixed len2 = ab.lengthSq();
                const Fixed t = len2 > Fixed::zero() ? std::clamp((p - w.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
                const Fixed reach = w.halfThick + Fixed::fromRatio(1, 2);
                if ((p - (w.a + ab * t)).flat().lengthSq() < reach * reach) open_[static_cast<size_t>(index(x, y))] = 0;
            }
            for (const Obstacle& o : towers) {
                const Fixed reach = o.radius + Fixed::fromRatio(1, 2);
                if ((p - o.pos).flat().lengthSq() < reach * reach) open_[static_cast<size_t>(index(x, y))] = 0;
            }
        }
    }
    // Gates: every cell the doors cross, sampled every half metre along them.
    for (const Gate& g : region.gates()) {
        std::vector<std::pair<int32_t, uint8_t>> cells;
        const Fixed len = (g.b - g.a).flat().length();
        const int64_t steps = (len * 2).floorToInt() + 1;
        for (int64_t i = 0; i <= steps; ++i) {
            const Vec3 p = g.a + (g.b - g.a) * Fixed::fromRatio(i, steps);
            const int32_t k = index(cellOf(p.x), cellOf(p.y));
            bool seen = false;
            for (const auto& c : cells) seen = seen || c.first == k;
            if (!seen) cells.push_back({k, open_[static_cast<size_t>(k)]});
        }
        for (const auto& c : cells) open_[static_cast<size_t>(c.first)] = 0;
        gateCells_.push_back(std::move(cells));
    }
    for (const Obstacle& o : region.obstacles()) {
        if (o.kind != Obstacle::Kind::House) continue;
        const int32_t x0 = cellOf(o.pos.x - o.halfX), x1 = cellOf(o.pos.x + o.halfX);
        const int32_t y0 = cellOf(o.pos.y - o.halfY), y1 = cellOf(o.pos.y + o.halfY);
        for (int32_t y = y0; y <= y1; ++y)
            for (int32_t x = x0; x <= x1; ++x) open_[static_cast<size_t>(index(x, y))] = 0;
    }
}

int32_t NavGrid::cellOf(Fixed v) const {
    return static_cast<int32_t>(std::clamp<int64_t>(v.floorToInt() / cell_, 0, n_ - 1));
}

uint16_t NavGrid::cellCost(Gait gait, int32_t cell) const {
    const size_t k = static_cast<size_t>(cell);
    if (!open_[k]) return 0;
    // The best ground in the cell: a walker keeps to it.
    int32_t perGround[kGroundCount];
    for (int g = 0; g < kGroundCount; ++g) perGround[g] = perMetre(gait, static_cast<Ground>(g));
    int32_t best = INT_MAX;
    for (int g = 0; g < kGroundCount; ++g) {
        if (!(mask_[k] & (1u << static_cast<unsigned>(g)))) continue;
        if (perGround[g] > 0) best = std::min(best, perGround[g]);
    }
    if (best == INT_MAX) return 0;
    if (steep_[k]) best = best * 5 / 4;
    return static_cast<uint16_t>(best);
}

const std::vector<uint16_t>& NavGrid::costs(Gait gait) const {
    std::vector<uint16_t>& c = cost_[static_cast<size_t>(gait)];
    if (c.empty()) {
        c.resize(open_.size());
        for (size_t k = 0; k < c.size(); ++k) c[k] = cellCost(gait, static_cast<int32_t>(k));
    }
    return c;
}

void NavGrid::openGate(size_t gate) {
    if (gate >= gateCells_.size()) return;
    for (const auto& c : gateCells_[gate]) {
        open_[static_cast<size_t>(c.first)] = c.second;
        for (int gi = 0; gi < kGaitCount; ++gi)
            if (!cost_[static_cast<size_t>(gi)].empty()) cost_[static_cast<size_t>(gi)][static_cast<size_t>(c.first)] = cellCost(static_cast<Gait>(gi), c.first);
    }
    gateCells_[gate].clear();
}

bool NavGrid::blocked(const Vec3& p) const { return open_[static_cast<size_t>(index(cellOf(p.x), cellOf(p.y)))] == 0; }

Fixed NavGrid::speedAt(const Vec3& p, Gait gait) const {
    const Fixed s = gaitSpeed(gait, fineGround(p.x, p.y));
    return s > Fixed::zero() ? s : Fixed::one();  // on a deck
}

Ground NavGrid::fineGround(Fixed x, Fixed y) const {
    const int64_t fx = std::clamp<int64_t>((x / fineSize_).floorToInt(), 0, fineCells_ - 1);
    const int64_t fy = std::clamp<int64_t>((y / fineSize_).floorToInt(), 0, fineCells_ - 1);
    return static_cast<Ground>(fine_[static_cast<size_t>(fy * fineCells_ + fx)]);
}

bool NavGrid::clearLine(const Vec3& from, const Vec3& to) const {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    const int64_t steps = (len / 2).floorToInt() + 1;
    for (int64_t i = 0; i <= steps; ++i) {
        const Vec3 p = from + d * Fixed::fromRatio(i, steps);
        if (blocked(p)) return false;
        if (decks_.empty()) continue;
        // Over deep water only on a deck.
        if (fineGround(p.x, p.y) != Ground::DeepWater) continue;
        bool onDeck = false;
        for (const Deck& k : decks_) onDeck = onDeck || (!k.raised && k.contains(p.x, p.y));
        if (!onDeck) return false;
    }
    return true;
}

Fixed NavGrid::travelTime(const Vec3& from, const Vec3& to, Gait gait, Fixed* slowest) const {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    if (len == Fixed::zero()) return Fixed::zero();
    // Cell by terrain cell along the line (every one it crosses, however
    // little of it), each piece at the speed of that cell's ground.
    const Fixed climb = kClimbCost * gaitClimbPct(gait) / 100;
    const Fixed tiny = Fixed::fromRatio(1, 32);
    const Fixed big = Fixed::fromInt(1 << 20);
    int64_t ix = (from.x / fineSize_).floorToInt(), iy = (from.y / fineSize_).floorToInt();
    const int64_t sx = d.x > Fixed::zero() ? 1 : -1, sy = d.y > Fixed::zero() ? 1 : -1;
    // Where (as a share of the line) it next crosses a cell edge in x and in y.
    const auto firstEdge = [&](Fixed p, Fixed dp, int64_t i, int64_t step) {
        if (dp == Fixed::zero()) return big;
        const Fixed edge = fineSize_ * (step > 0 ? i + 1 : i);
        return (edge - p) / dp;
    };
    Fixed tMaxX = firstEdge(from.x, d.x, ix, sx), tMaxY = firstEdge(from.y, d.y, iy, sy);
    const Fixed tDeltaX = d.x == Fixed::zero() ? big : fineSize_ / abs(d.x);
    const Fixed tDeltaY = d.y == Fixed::zero() ? big : fineSize_ / abs(d.y);
    Fixed t, total;
    Fixed h = terrain_->heightAt(from.x, from.y);
    const int64_t limit = (len / fineSize_).floorToInt() * 2 + 8;
    for (int64_t guard = 0; guard < limit; ++guard) {
        const Fixed tn = min(min(tMaxX, tMaxY), Fixed::one());
        const Fixed piece = len * (tn - t);
        if (piece > Fixed::zero()) {
            const int64_t cx = std::clamp<int64_t>(ix, 0, fineCells_ - 1), cy = std::clamp<int64_t>(iy, 0, fineCells_ - 1);
            Fixed s = gaitSpeed(gait, static_cast<Ground>(fine_[static_cast<size_t>(cy * fineCells_ + cx)]));
            if (s <= Fixed::zero()) s = Fixed::one();  // on a deck over the water
            if (slowest && piece > tiny) *slowest = min(*slowest, s);
            const Vec3 mid = from + d * ((t + tn) / 2);
            if (steep_[static_cast<size_t>(index(cellOf(mid.x), cellOf(mid.y)))]) s = s * kSteepSpeed;
            Fixed time = piece / s;
            const Vec3 end = from + d * tn;
            const Fixed h2 = terrain_->heightAt(end.x, end.y);
            if (h2 > h) time = time * (Fixed::one() + (h2 - h) / piece * climb);
            h = h2;
            total += time;
        }
        if (tn >= Fixed::one()) break;
        t = tn;
        if (tMaxX <= tMaxY) {
            ix += sx;
            tMaxX += tDeltaX;
        } else {
            iy += sy;
            tMaxY += tDeltaY;
        }
    }
    return total;
}

Fixed NavGrid::travelTime(const Vec3& from, const std::vector<Vec3>& points, Gait gait) const {
    Fixed total;
    Vec3 at = from;
    for (const Vec3& p : points) {
        total += travelTime(at, p, gait);
        at = p;
    }
    return total;
}

Vec3 NavGrid::waypoint(int32_t cell, Gait gait) const {
    const int32_t cx = cell % n_, cy = cell / n_;
    const Fixed half = Fixed::fromInt(cell_) / 2;
    const Vec3 centre{Fixed::fromInt(cx * cell_) + half, Fixed::fromInt(cy * cell_) + half, Fixed::zero()};
    if (sub_ <= 1) return centre;
    // The middle of the cell's best ground for this gait (the road through a field).
    Fixed best;
    for (int32_t sy = 0; sy < sub_; ++sy)
        for (int32_t sx = 0; sx < sub_; ++sx)
            best = max(best, gaitSpeed(gait, static_cast<Ground>(fine_[static_cast<size_t>(cy * sub_ + sy) * fineCells_ + cx * sub_ + sx])));
    if (best == Fixed::zero()) return centre;
    Vec3 sum;
    int32_t count = 0;
    for (int32_t sy = 0; sy < sub_; ++sy) {
        for (int32_t sx = 0; sx < sub_; ++sx) {
            const Ground g = static_cast<Ground>(fine_[static_cast<size_t>(cy * sub_ + sy) * fineCells_ + cx * sub_ + sx]);
            if (gaitSpeed(gait, g) != best) continue;
            sum += Vec3{fineSize_ * (cx * sub_ + sx) + fineSize_ / 2, fineSize_ * (cy * sub_ + sy) + fineSize_ / 2, Fixed::zero()};
            ++count;
        }
    }
    if (count == 0 || count == sub_ * sub_) return centre;
    const Vec3 mid = sum / Fixed::fromInt(count);
    if (gaitSpeed(gait, fineGround(mid.x, mid.y)) == best) return mid;
    // Best ground in opposite corners: the first of them.
    for (int32_t sy = 0; sy < sub_; ++sy)
        for (int32_t sx = 0; sx < sub_; ++sx)
            if (gaitSpeed(gait, static_cast<Ground>(fine_[static_cast<size_t>(cy * sub_ + sy) * fineCells_ + cx * sub_ + sx])) == best)
                return {fineSize_ * (cx * sub_ + sx) + fineSize_ / 2, fineSize_ * (cy * sub_ + sy) + fineSize_ / 2, Fixed::zero()};
    return mid;
}

int32_t NavGrid::nearestOpen(const Vec3& p) const {
    const int32_t px = cellOf(p.x), py = cellOf(p.y);
    int32_t best = -1;
    Fixed bestD;
    const Fixed half = Fixed::fromInt(cell_) / 2;
    for (int32_t y = std::max(0, py - kSnapCells); y <= std::min(n_ - 1, py + kSnapCells); ++y) {
        for (int32_t x = std::max(0, px - kSnapCells); x <= std::min(n_ - 1, px + kSnapCells); ++x) {
            if (!open_[static_cast<size_t>(index(x, y))]) continue;
            const Vec3 c{Fixed::fromInt(x * cell_) + half, Fixed::fromInt(y * cell_) + half, Fixed::zero()};
            const Fixed d = (c - p).flat().lengthSq();
            if (best < 0 || d < bestD) {
                best = index(x, y);
                bestD = d;
            }
        }
    }
    return best;
}

int32_t NavGrid::climbCost(int32_t from, int32_t to, int32_t step, bool diagonal, int32_t climbPct) const {
    const int64_t dh = (height_[static_cast<size_t>(to)] - height_[static_cast<size_t>(from)]).raw;
    if (dh <= 0) return step;
    // step x (1 + grade x kClimbCost (5/2) x climbPct / 100), in integers.
    const int64_t extra = static_cast<int64_t>(step) * dh * 5 * climbPct / (2 * cell_ * (diagonal ? 141 : 100) * Fixed::kOneRaw);
    return static_cast<int32_t>(std::min<int64_t>(step + extra, INT_MAX / 4));
}

bool NavGrid::findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait) const {
    // On a bridge deck the way on or off is by its ends (its sides drop to the water).
    const auto deckOf = [&](const Vec3& p) -> const Deck* {
        for (const Deck& d : decks_)
            if (!d.raised && d.contains(p.x, p.y)) return &d;
        return nullptr;
    };
    const Deck* fd = deckOf(from);
    const Deck* td = deckOf(to);
    if (fd == td) return route(from, to, out, gait);
    // The end nearest `toward`, and a step beyond it onto the bank.
    const auto end = [&](const Deck& d, const Vec3& toward, Vec3& beyond) {
        const Vec3 a = d.centre + d.dir * d.halfSpan, b = d.centre - d.dir * d.halfSpan;
        const bool first = (a - toward).flat().lengthSq() <= (b - toward).flat().lengthSq();
        beyond = (first ? a : b) + d.dir * Fixed::fromInt(first ? 2 : -2);
        beyond.z = Fixed::zero();
        Vec3 e = first ? a : b;
        e.z = Fixed::zero();
        return e;
    };
    Vec3 a = from, b = to, offA, onB;
    Vec3 endA, endB;
    if (fd) {
        endA = end(*fd, to, offA);
        a = offA;
    }
    if (td) {
        endB = end(*td, a, onB);
        b = onB;
    }
    std::vector<Vec3> mid;
    if (!route(a, b, mid, gait)) mid = {b};
    out.clear();
    if (fd) {
        out.push_back(endA);
        out.push_back(offA);
    }
    out.insert(out.end(), mid.begin(), mid.end());
    if (td) {
        out.push_back(endB);
        out.push_back(to);
    }
    return true;
}

bool NavGrid::route(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait) const {
    out.clear();
    // The straight line, if it is open: short moves take it, and so does any
    // line no slower than the best a detour could do.
    const bool line = clearLine(from, to);
    Fixed lineTime;
    const std::vector<uint16_t>& cost = costs(gait);
    int32_t cheapest = INT_MAX;
    for (int g = 0; g < kGroundCount; ++g) {
        const int32_t c = perMetre(gait, static_cast<Ground>(g));
        if (c > 0) cheapest = std::min(cheapest, c);
    }
    if (line) {
        const Fixed dist = (to - from).flat().length();
        if (dist <= Fixed::fromInt(cell_ * kShortMoveCells)) {
            out.push_back(to);
            return true;
        }
        lineTime = travelTime(from, to, gait);
        if (lineTime * 100 <= dist * cheapest) {
            out.push_back(to);
            return true;
        }
    }
    const auto straight = [&]() {
        out.clear();
        out.push_back(to);
        return true;
    };
    // A start or goal on a deck or against a wall routes from the open cell nearest it.
    int32_t start = index(cellOf(from.x), cellOf(from.y)), goal = index(cellOf(to.x), cellOf(to.y));
    if (!cost[static_cast<size_t>(start)]) start = nearestOpen(from);
    if (!cost[static_cast<size_t>(goal)]) goal = nearestOpen(to);
    if (start < 0 || goal < 0) return line ? straight() : false;
    const int32_t gx = goal % n_, gy = goal / n_;

    // Octile heuristic at the cheapest ground's cost: admissible.
    const int64_t cheapCell = static_cast<int64_t>(cheapest) * cell_;
    const auto heuristic = [&](int32_t i) {
        const int64_t dx = std::abs(i % n_ - gx), dy = std::abs(i / n_ - gy);
        return static_cast<int32_t>(cheapCell * (100 * std::max(dx, dy) + 40 * std::min(dx, dy)) / 100);
    };
    // A detour much slower than the open straight line is not worth finding.
    const int64_t bound = line ? (lineTime * 110).floorToInt() + cheapCell * 2 : INT64_MAX;
    struct Node {
        int32_t f, h, idx;
        bool operator>(const Node& o) const {
            if (f != o.f) return f > o.f;
            if (h != o.h) return h > o.h;
            return idx > o.idx;
        }
    };
    const int32_t climbPct = gaitClimbPct(gait);
    g_.assign(cost.size(), INT_MAX);
    parent_.assign(cost.size(), -1);
    std::vector<Node> heap;
    heap.reserve(1024);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open(std::greater<Node>(), std::move(heap));
    g_[static_cast<size_t>(start)] = 0;
    open.push({heuristic(start), heuristic(start), start});
    int32_t expansions = 0;
    bool found = false;
    while (!open.empty() && expansions < kMaxExpansions) {
        const Node cur = open.top();
        open.pop();
        if (cur.f - cur.h > g_[static_cast<size_t>(cur.idx)]) continue;  // stale entry
        if (cur.idx == goal) {
            found = true;
            break;
        }
        ++expansions;
        const int32_t cx = cur.idx % n_, cy = cur.idx / n_;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int32_t nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= n_ || ny >= n_) continue;
                const int32_t ni = index(nx, ny);
                const int32_t c = cost[static_cast<size_t>(ni)];
                if (c == 0) continue;
                // No cutting corners past blocked cells.
                const bool diagonal = dx != 0 && dy != 0;
                if (diagonal && (cost[static_cast<size_t>(index(cx + dx, cy))] == 0 || cost[static_cast<size_t>(index(cx, cy + dy))] == 0)) continue;
                const int32_t step = climbCost(cur.idx, ni, static_cast<int32_t>(c * cell_ * (diagonal ? 141 : 100) / 100), diagonal, climbPct);
                const int32_t ng = g_[static_cast<size_t>(cur.idx)] + step;
                if (ng < g_[static_cast<size_t>(ni)]) {
                    const int32_t h = heuristic(ni);
                    if (static_cast<int64_t>(ng) + h > bound) continue;
                    g_[static_cast<size_t>(ni)] = ng;
                    parent_[static_cast<size_t>(ni)] = cur.idx;
                    open.push({ng + h, h, ni});
                }
            }
        }
    }
    if (!found) return line ? straight() : false;

    std::vector<Vec3> cells;
    for (int32_t i = goal; i != -1 && i != start; i = parent_[static_cast<size_t>(i)]) cells.push_back(waypoint(i, gait));
    if (start != index(cellOf(from.x), cellOf(from.y))) cells.push_back(waypoint(start, gait));
    std::reverse(cells.begin(), cells.end());
    if (goal != index(cellOf(to.x), cellOf(to.y))) cells.push_back(to);
    if (!cells.empty()) cells.back() = to;
    else cells.push_back(to);

    // Over a bridge, walk its deck from end to end: those two points must stay.
    std::vector<bool> pinned(cells.size(), false);
    for (const Deck& d : decks_) {
        if (d.raised) continue;
        size_t first = cells.size(), last = 0;
        for (size_t k = 0; k + 1 < cells.size(); ++k) {
            if (!Deck{d.centre, d.dir, d.halfSpan + Fixed::fromInt(cell_), d.halfWidth + Fixed::fromInt(cell_)}.contains(cells[k].x, cells[k].y)) continue;
            first = std::min(first, k);
            last = k;
        }
        if (first > last) continue;
        const Vec3 a = d.centre + d.dir * d.halfSpan, b = d.centre - d.dir * d.halfSpan;
        const Vec3 before = first > 0 ? cells[first - 1] : from;
        const bool aFirst = (a - before).flat().lengthSq() < (b - before).flat().lengthSq();
        std::vector<Vec3> rest(cells.begin() + static_cast<long>(last) + 1, cells.end());
        std::vector<bool> restPinned(pinned.begin() + static_cast<long>(last) + 1, pinned.end());
        cells.resize(first);
        pinned.resize(first);
        cells.push_back({aFirst ? a.x : b.x, aFirst ? a.y : b.y, Fixed::zero()});
        cells.push_back({aFirst ? b.x : a.x, aFirst ? b.y : a.y, Fixed::zero()});
        pinned.push_back(true);
        pinned.push_back(true);
        cells.insert(cells.end(), rest.begin(), rest.end());
        pinned.insert(pinned.end(), restPinned.begin(), restPinned.end());
    }

    // String-pull: from each point, skip ahead to the farthest waypoint in
    // clear sight whose straight line is no slower than the path to it and
    // crosses no slower ground than the path keeps to (so a road is never cut
    // across a field, nor a corner across a swamp), but never past a pinned
    // one. Times along the path are summed up front.
    std::vector<Fixed> along(cells.size() + 1), speed(cells.size());  // along[k]: time to cells[k - 1]
    for (size_t k = 0; k < cells.size(); ++k) {
        along[k + 1] = along[k] + travelTime(k == 0 ? from : cells[k - 1], cells[k], gait);
        speed[k] = speedAt(cells[k], gait);
    }
    const auto slowestOn = [&](const Vec3& a, const Vec3& b) {
        Fixed slowest = Fixed::fromInt(100);
        travelTime(a, b, gait, &slowest);
        return slowest;
    };
    Vec3 at = from;
    Fixed atSpeed = speedAt(from, gait);
    size_t i = 0;  // `at` is cells[i - 1], or `from` when 0
    while (i < cells.size()) {
        size_t limit = std::min(cells.size(), i + kSmoothWindow);
        for (size_t k = i; k < limit; ++k) {
            if (pinned[k]) {
                limit = k + 1;
                break;
            }
        }
        // The slowest ground the path keeps to from here to each waypoint.
        std::vector<Fixed> worst(limit - i);
        for (size_t k = i; k < limit; ++k) worst[k - i] = min(k == i ? atSpeed : worst[k - i - 1], speed[k]);
        size_t far = i;
        for (size_t j = limit; j-- > i + 1;) {
            if (!clearLine(at, cells[j])) continue;
            Fixed lineSlowest = Fixed::fromInt(100);
            if (travelTime(at, cells[j], gait, &lineSlowest) > along[j + 1] - along[i]) continue;
            if (lineSlowest < worst[j - i]) continue;
            far = j;
            break;
        }
        // A step between two cells that clips slower ground at a corner goes
        // round it by an elbow instead.
        if (far == i && !pinned[i] && slowestOn(at, cells[i]) < worst[0]) {
            for (const Vec3& e : {Vec3{at.x, cells[i].y, Fixed::zero()}, Vec3{cells[i].x, at.y, Fixed::zero()}}) {
                if (!clearLine(at, e) || !clearLine(e, cells[i]) || slowestOn(at, e) < worst[0] || slowestOn(e, cells[i]) < worst[0]) continue;
                out.push_back(e);
                break;
            }
        }
        out.push_back(cells[far]);
        at = cells[far];
        atSpeed = speed[far];
        i = far + 1;
    }
    // The detour must beat the open straight line, or there is no point to it.
    // (by a little at least: no zigzags for nothing).
    if (line && travelTime(from, out, gait) * 100 >= lineTime * 99) return straight();
    return true;
}

}  // namespace illyria::sim
