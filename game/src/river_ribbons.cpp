#include "illyria/game/river_ribbons.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

using sim::Fixed;
using sim::Vec3;

namespace {
// One vertex of a river's course on land: where, how wide, and how far from the sea mouth along the river (the arc length).
struct Pt {
    Vec3 p;
    float hw = 0;
    float arc = 0;
};
}  // namespace

std::vector<RibbonSegment> riverRibbons(const sim::Continent& cont, const V3& origin) {
    const auto land = [&](const Vec3& p) { return cont.landDistance(p.x, p.y) > Fixed::zero(); };
    // Open lake water (the lake's own surface over its bed, not an isle or a shore): the river's course through it is the
    // lake, so no ribbon is drawn over it. The sim's rivers do run on through the lakes (they meet at the Heartmere's
    // middle), but there the water is the lake's.
    const auto lake = [&](const Vec3& p) {
        const sim::Continent::Sample s = cont.sample(p.x, p.y);
        return s.inLake && s.height < s.lakeSurface;
    };
    constexpr float kStep = 6.0f;           // metres between the points tested for the lake
    constexpr float kMouthLength = 36.0f;   // a river's mouth on a lake opens out over this many metres
    constexpr float kMouthWiden = 0.9f;     // to this much wider again at the shore
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

        // The course as short pieces, each kept or dropped by whether its middle is in a lake; the course is then a set of
        // runs of dry-land pieces, each cut where it meets the lake's shore (by bisection).
        std::vector<Pt> run;
        bool runStartsAtLake = false;
        const auto flush = [&](bool endsAtLake) {
            if (run.size() >= 2) {
                // A mouth on a lake opens out (a small delta, not a pipe's end) over its last kMouthLength metres.
                std::vector<Pt> v;
                const auto widen = [&](float distToShore) { return 1.0f + kMouthWiden * std::max(0.0f, 1.0f - distToShore / kMouthLength); };
                for (size_t k = 0; k < run.size(); ++k) {
                    Pt q = run[k];
                    float f = 1.0f;
                    if (endsAtLake) f = std::max(f, widen(run.back().arc - q.arc));
                    if (runStartsAtLake) f = std::max(f, widen(q.arc - run.front().arc));
                    q.hw *= f;
                    v.push_back(q);
                }
                for (size_t k = 0; k + 1 < v.size(); ++k) {
                    RibbonSegment s;
                    s.a = toV3(v[k].p) - V3{origin.x, origin.y, 0};
                    s.b = toV3(v[k + 1].p) - V3{origin.x, origin.y, 0};
                    s.halfWidth = std::max(v[k].hw, v[k + 1].hw);
                    if (sea < n) {
                        s.toMouthA = arc.back() - v[k].arc;
                        s.toMouthB = arc.back() - v[k + 1].arc;
                    }
                    out.push_back(s);
                }
            }
            run.clear();
        };
        const auto lerpPt = [&](size_t i, float t) {
            Pt q;
            q.p = pts[i] + (pts[i + 1] - pts[i]) * Fixed::fromRatio(static_cast<int64_t>(std::lround(t * 1000)), 1000);
            q.hw = hw[i] + (hw[i + 1] - hw[i]) * t;
            q.arc = arc[i] + (arc[i + 1] - arc[i]) * t;
            return q;
        };
        bool inLake = false;  // the previous sample
        bool first = true;
        float prevT = 0;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const float len = arc[i + 1] - arc[i];
            const int steps = std::max(1, static_cast<int>(std::ceil(len / kStep)));
            prevT = 0;  // (the last sample of the leg before is this leg's start)
            for (int k = (i == 0 ? 0 : 1); k <= steps; ++k) {
                const float t = static_cast<float>(k) / steps;
                Pt q = lerpPt(i, t);
                const bool wet = lake(q.p);
                if (first) {
                    first = false;
                    runStartsAtLake = false;
                    if (!wet) run.push_back(q);
                } else if (!wet && !inLake) {
                    run.push_back(q);
                } else if (wet != inLake) {
                    // The shore lies between the last sample and this one, on this leg: bisect for it.
                    float lo = prevT, hi = t;
                    for (int b = 0; b < 12; ++b) {
                        const float mid = (lo + hi) / 2;
                        (lake(lerpPt(i, mid).p) == inLake ? lo : hi) = mid;
                    }
                    const Pt shore = lerpPt(i, (lo + hi) / 2);
                    if (wet) {  // leaves the land: the run ends at the shore
                        run.push_back(shore);
                        flush(true);
                    } else {  // comes ashore: a new run starts at the shore
                        run.clear();
                        runStartsAtLake = true;
                        run.push_back(shore);
                        run.push_back(q);
                    }
                }
                inLake = wet;
                prevT = t;
            }
        }
        flush(false);
    }
    return out;
}

}  // namespace illyria::game
