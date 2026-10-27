#include "illyria/sim/nav.h"

#include <algorithm>
#include <climits>
#include <queue>

namespace illyria::sim {

namespace {

constexpr int32_t kMaxExpansions = 20000;

int32_t groundCost(Ground g) {
    switch (g) {
        case Ground::Road: return 8;
        case Ground::Plains: return 10;
        case Ground::Forest: return 14;
        case Ground::Swamp: return 20;
        case Ground::Shallows: return 25;
        case Ground::Rock: return 20;
        case Ground::DeepWater: return 0;
    }
    return 10;
}

}  // namespace

NavGrid::NavGrid(const Region& region) {
    const Terrain& t = region.terrain();
    const int32_t sub = static_cast<int32_t>(kCellSize / t.cellSize().floorToInt());
    n_ = t.cells() / sub;
    cost_.assign(static_cast<size_t>(n_) * n_, 0);
    for (int32_t y = 0; y < n_; ++y) {
        for (int32_t x = 0; x < n_; ++x) {
            // Deep water anywhere in the cell blocks it; otherwise the worst ground decides.
            int32_t c = 0;
            bool water = false;
            for (int32_t sy = 0; sy < sub; ++sy) {
                for (int32_t sx = 0; sx < sub; ++sx) {
                    const Ground g = region.cellGround(x * sub + sx, y * sub + sy);
                    water = water || g == Ground::DeepWater;
                    c = std::max(c, groundCost(g));
                }
            }
            cost_[index(x, y)] = water ? 0 : c;
        }
    }
    for (const Obstacle& o : region.obstacles()) {
        if (o.kind != Obstacle::Kind::House) continue;
        const int32_t x0 = cellOf(o.pos.x - o.halfX), x1 = cellOf(o.pos.x + o.halfX);
        const int32_t y0 = cellOf(o.pos.y - o.halfY), y1 = cellOf(o.pos.y + o.halfY);
        for (int32_t y = y0; y <= y1; ++y)
            for (int32_t x = x0; x <= x1; ++x) cost_[index(x, y)] = 0;
    }
}

int32_t NavGrid::cellOf(Fixed v) const {
    return static_cast<int32_t>(std::clamp<int64_t>(v.floorToInt() / kCellSize, 0, n_ - 1));
}

bool NavGrid::blocked(const Vec3& p) const { return cost_[index(cellOf(p.x), cellOf(p.y))] == 0; }

bool NavGrid::clearLine(const Vec3& from, const Vec3& to) const {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    const int64_t steps = (len / 4).floorToInt() + 1;
    for (int64_t i = 0; i <= steps; ++i) {
        const Vec3 p = from + d * Fixed::fromRatio(i, steps);
        if (blocked(p)) return false;
    }
    return true;
}

bool NavGrid::findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const {
    out.clear();
    if (clearLine(from, to)) {
        out.push_back(to);
        return true;
    }
    const int32_t sx = cellOf(from.x), sy = cellOf(from.y), gx = cellOf(to.x), gy = cellOf(to.y);
    if (cost_[index(gx, gy)] == 0) return false;
    const int32_t start = index(sx, sy), goal = index(gx, gy);

    // Octile heuristic with the cheapest step cost (a road): admissible.
    const auto heuristic = [&](int32_t i) {
        const int32_t dx = std::abs(i % n_ - gx), dy = std::abs(i / n_ - gy);
        return 8 * std::max(dx, dy) + 3 * std::min(dx, dy);
    };
    struct Node {
        int32_t f, h, idx;
        bool operator>(const Node& o) const {
            if (f != o.f) return f > o.f;
            if (h != o.h) return h > o.h;
            return idx > o.idx;
        }
    };
    std::vector<int32_t> g(cost_.size(), INT_MAX), parent(cost_.size(), -1);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    g[start] = 0;
    open.push({heuristic(start), heuristic(start), start});
    int32_t expansions = 0;
    bool found = false;
    while (!open.empty() && expansions < kMaxExpansions) {
        const Node cur = open.top();
        open.pop();
        if (cur.f - cur.h > g[cur.idx]) continue;  // stale entry
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
                if (cost_[ni] == 0) continue;
                // No cutting corners past blocked cells.
                if (dx != 0 && dy != 0 && (cost_[index(cx + dx, cy)] == 0 || cost_[index(cx, cy + dy)] == 0)) continue;
                const int32_t step = dx != 0 && dy != 0 ? cost_[ni] * 141 / 100 : cost_[ni];
                const int32_t ng = g[cur.idx] + step;
                if (ng < g[ni]) {
                    g[ni] = ng;
                    parent[ni] = cur.idx;
                    open.push({ng + heuristic(ni), heuristic(ni), ni});
                }
            }
        }
    }
    if (!found) return false;

    std::vector<Vec3> cells;
    for (int32_t i = goal; i != -1 && i != start; i = parent[i]) {
        const Fixed half = Fixed::fromInt(kCellSize) / 2;
        cells.push_back({Fixed::fromInt((i % n_) * kCellSize) + half, Fixed::fromInt((i / n_) * kCellSize) + half,
                         Fixed::zero()});
    }
    std::reverse(cells.begin(), cells.end());
    if (!cells.empty()) cells.back() = to;
    else cells.push_back(to);

    // String-pull: from each point, skip ahead to the farthest waypoint in clear sight.
    Vec3 at = from;
    size_t i = 0;
    while (i < cells.size()) {
        size_t far = i;
        for (size_t j = cells.size(); j-- > i + 1;) {
            if (clearLine(at, cells[j])) {
                far = j;
                break;
            }
        }
        out.push_back(cells[far]);
        at = cells[far];
        i = far + 1;
    }
    return true;
}

}  // namespace illyria::sim
