// The top of a curtain wall: where its embrasures open in the parapet
// (docs/GAME_DESIGN.md, castles). The sim's missiles and the renderer's
// merlons both come from here, so what is drawn is what stops an arrow.
#include <algorithm>
#include <utility>

#include "illyria/sim/continent.h"

namespace illyria::sim {

namespace {
// Embrasures keep this far clear of a tower's side.
constexpr Fixed kTowerClear = Fixed::fromRatio(3, 10);
}  // namespace

std::vector<Fixed> Continent::Castle::Ring::crenels(size_t edge) const {
    const size_t n = corners.size();
    if (n < 2) return {};
    const Fixed len = (corners[(edge + 1) % n] - corners[edge]).flat().length();
    // The corner towers (radius 3/2 thick) and, on the gate's edge, the pair
    // flanking it (6/5 thick, their faces flush with the passage).
    std::vector<std::pair<Fixed, Fixed>> towers;
    const Fixed corner = thick * 3 / 2 + kTowerClear;
    if (towerAt(edge)) towers.push_back({-corner, corner});
    if (towerAt((edge + 1) % n)) towers.push_back({len - corner, len + corner});
    if (edge == gateEdge) {
        const Fixed reach = thick * 6 / 5 + kTowerClear;
        for (int s = -1; s <= 1; s += 2) {
            const Fixed at = len / 2 + gateTowerAt() * s;
            towers.push_back({at - reach, at + reach});
        }
    }
    std::sort(towers.begin(), towers.end());
    // Each free stretch between towers holds as many embrasures as fit, centred in it.
    std::vector<Fixed> out;
    Fixed from = Fixed::zero();
    for (size_t i = 0; i <= towers.size(); ++i) {
        const Fixed to = i < towers.size() ? min(len, towers[i].first) : len;
        const Fixed room = to - from;
        if (room >= kCrenelWidth) {
            const int64_t count = ((room - kCrenelWidth) / kCrenelPitch).floorToInt() + 1;
            Fixed at = from + (room - kCrenelPitch * (count - 1)) / 2;
            for (int64_t k = 0; k < count; ++k, at += kCrenelPitch) out.push_back(at);
        }
        if (i < towers.size()) from = max(from, towers[i].second);
    }
    return out;
}

namespace {
// A gallery's arc is split until its pieces turn no more than about 16 degrees.
constexpr Fixed kArcPieceCos = Fixed::fromRatio(24, 25);

Vec3 unitFlat(const Vec3& v) {
    const Fixed len = v.flat().length();
    return len > Fixed::zero() ? v.flat() / len : Vec3{};
}
}  // namespace

std::vector<Vec3> Continent::Castle::Gallery::arc(Fixed r) const {
    // Halve every piece until they are short enough (from -> mid and mid -> to
    // turn alike, so every piece of a pass does).
    std::vector<Vec3> dirs{from, mid, to};
    while (dirs[0].dot(dirs[1]) < kArcPieceCos) {
        std::vector<Vec3> finer{dirs[0]};
        for (size_t i = 1; i < dirs.size(); ++i) {
            finer.push_back(unitFlat(dirs[i - 1] + dirs[i]));
            finer.push_back(dirs[i]);
        }
        dirs = std::move(finer);
    }
    std::vector<Vec3> out;
    for (const Vec3& d : dirs) out.push_back(pos + d * r);
    return out;
}

std::vector<Continent::Castle::Gallery> Continent::Castle::Ring::galleries() const {
    const size_t n = corners.size();
    if (n < 3) return {};
    // Which side of each edge is inside: the left for corners running
    // anticlockwise (measured from the first corner, so the sums stay small).
    Fixed area;
    for (size_t i = 0; i < n; ++i) {
        const Vec3 a = corners[i] - corners[0], b = corners[(i + 1) % n] - corners[0];
        area += a.x * b.y - b.x * a.y;
    }
    const auto inward = [&](const Vec3& along) {
        return area > Fixed::zero() ? Vec3{-along.y, along.x, Fixed::zero()} : Vec3{along.y, -along.x, Fixed::zero()};
    };
    std::vector<Gallery> out;
    for (size_t e = 0; e < n; ++e) {
        const Vec3& prev = corners[(e + n - 1) % n];
        const Vec3& at = corners[e];
        const Vec3& next = corners[(e + 1) % n];
        const Vec3 in = unitFlat(at - prev), onward = unitFlat(next - at);
        // Round the corner tower from the walk coming in to the walk going on,
        // through the middle of the angle inside (wider than a half turn where
        // the corner points inward).
        if (towerAt(e)) out.push_back({at, thick * 3 / 2, -in, unitFlat(inward(in) + inward(onward)), onward});
        if (e == gateEdge) {
            const Vec3 mid = (at + next) * Fixed::fromRatio(1, 2);
            for (int s = -1; s <= 1; s += 2) out.push_back({mid + onward * (gateTowerAt() * s), thick * 6 / 5, -onward, inward(onward), onward});
        }
    }
    return out;
}

std::vector<Vec3> Continent::Castle::Ring::way() const {
    const std::vector<Gallery> gs = galleries();
    const size_t n = gs.size();
    std::vector<Vec3> out;
    for (size_t i = 0; i < n; ++i) {
        const Gallery& prev = gs[(i + n - 1) % n];
        const Gallery& next = gs[(i + 1) % n];
        const auto within = [](const Gallery& g, const Vec3& p) { return (p - g.pos).flat().length() < g.radius + kGalleryWay; };
        for (const Vec3& p : gs[i].arc(gs[i].radius + kGalleryWay))
            if (!within(prev, p) && !within(next, p)) out.push_back(p);
    }
    return out;
}

}  // namespace illyria::sim
