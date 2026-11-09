#include "illyria/game/river_ribbons.h"

namespace illyria::game {

using sim::Fixed;
using sim::Vec3;

std::vector<RibbonSegment> riverRibbons(const sim::Continent& cont, const V3& origin) {
    const auto land = [&](const Vec3& p) { return cont.landDistance(p.x, p.y) > Fixed::zero(); };
    std::vector<RibbonSegment> out;
    for (const sim::Continent::River& r : cont.rivers()) {
        const size_t n = r.points.size();
        if (n < 2) continue;
        // The first vertex at sea, when the river ends there: the cut is on the leg before it.
        size_t sea = n;
        for (size_t i = 1; i < n && sea == n; ++i)
            if (!land(r.points[i]) && land(r.points[i - 1])) sea = i;
        std::vector<Vec3> pts(r.points.begin(), r.points.begin() + static_cast<std::ptrdiff_t>(sea));
        std::vector<float> hw;
        for (size_t i = 0; i < pts.size(); ++i) hw.push_back(static_cast<float>(r.widths[i].toDouble()));
        if (sea < n) {
            Vec3 in = r.points[sea - 1], at = r.points[sea];
            for (int k = 0; k < 24; ++k) {
                const Vec3 mid = (in + at) * Fixed::fromRatio(1, 2);
                (land(mid) ? in : at) = mid;
            }
            pts.push_back(in);  // the last point still on land
            hw.push_back(hw.back());
        }
        std::vector<float> arc(pts.size(), 0.0f);
        for (size_t i = 1; i < pts.size(); ++i) arc[i] = arc[i - 1] + static_cast<float>((pts[i] - pts[i - 1]).flat().length().toDouble());
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            RibbonSegment s;
            s.a = toV3(pts[i]) - V3{origin.x, origin.y, 0};
            s.b = toV3(pts[i + 1]) - V3{origin.x, origin.y, 0};
            s.halfWidth = hw[i];
            if (sea < n) {
                s.toMouthA = arc.back() - arc[i];
                s.toMouthB = arc.back() - arc[i + 1];
            }
            out.push_back(s);
        }
    }
    return out;
}

}  // namespace illyria::game
