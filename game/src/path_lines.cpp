#include "illyria/game/path_lines.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
float flatDist(const V3& a, const V3& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
}  // namespace

float routeLength(const std::vector<V3>& points) {
    float len = 0;
    for (size_t i = 1; i < points.size(); ++i) len += flatDist(points[i - 1], points[i]);
    return len;
}

V3 alongMetres(const std::vector<V3>& points, float metres, float* yaw) {
    if (points.empty()) return {};
    float left = std::max(metres, 0.0f);
    size_t i = 1;
    for (; i + 1 < points.size(); ++i) {
        const float seg = flatDist(points[i - 1], points[i]);
        if (left <= seg) break;
        left -= seg;
    }
    if (points.size() < 2) return points.front();
    const V3 &a = points[i - 1], &b = points[i];
    const float seg = flatDist(a, b);
    if (yaw != nullptr) *yaw = std::atan2(b.y - a.y, b.x - a.x);
    return seg > 0 ? lerp(a, b, std::min(left / seg, 1.0f)) : b;
}

V3 along(const std::vector<V3>& points, float fraction) {
    return alongMetres(points, routeLength(points) * std::clamp(fraction, 0.0f, 1.0f));
}

std::vector<RouteLine> mergeRoutes(const std::vector<RouteIn>& routes, float tolerance) {
    struct Group {
        const RouteIn* rep;
        V3 startSum, endSum;
        V3 mid, end;
        float tol;
        RouteLine line;
    };
    std::vector<Group> groups;
    for (const RouteIn& r : routes) {
        if (r.points.size() < 2 || r.stance == sim::World::Stance::Idle) continue;
        const V3 mid = along(r.points, 0.5f), end = r.points.back();
        const float tol = tolerance < 0 ? -1.0f : tolerance + 0.02f * routeLength(r.points);
        Group* home = nullptr;
        float nearest = 1e30f;
        for (Group& g : groups) {
            if (g.rep->stance != r.stance || g.rep->quarry != r.quarry) continue;
            const float d = flatDist(g.end, end);
            if (tol >= 0 && d <= tol && flatDist(g.mid, mid) <= (r.squad >= 0 && g.rep->squad == r.squad ? 4 : 2) * tol && d < nearest) {
                home = &g;
                nearest = d;
            }
        }
        if (home == nullptr && groups.size() >= kMaxRouteLines) {
            for (Group& g : groups) {
                const float d = flatDist(g.end, end);
                if (g.rep->stance == r.stance && d < nearest) home = &g, nearest = d;
            }
            if (home == nullptr) continue;
        }
        if (home == nullptr) {
            Group g{&r, r.points.front(), end, mid, end, tol, {}};
            g.line.stance = r.stance;
            g.line.quarry = r.quarry;
            g.line.opacity = r.opacity;
            g.line.men = 1;
            groups.push_back(std::move(g));
            continue;
        }
        home->startSum = home->startSum + r.points.front();
        home->endSum = home->endSum + end;
        home->line.opacity = std::max(home->line.opacity, r.opacity);
        ++home->line.men;
    }
    std::vector<RouteLine> out;
    for (Group& g : groups) {
        g.line.points = g.rep->points;
        if (g.line.men > 1) {
            const float k = 1.0f / static_cast<float>(g.line.men);
            // The middle of the men, going to the middle of where they end (a hunter's end is the quarry's place).
            g.line.points.front() = g.startSum * k;
            if (g.rep->stance != sim::World::Stance::Hunt) g.line.points.back() = g.endSum * k;
        }
        out.push_back(std::move(g.line));
    }
    return out;
}

}  // namespace illyria::game
