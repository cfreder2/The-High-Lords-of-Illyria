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
    // flanking it (6/5 thick, their middles a thickness beyond the gate).
    std::vector<std::pair<Fixed, Fixed>> towers;
    const Fixed corner = thick * 3 / 2 + kTowerClear;
    towers.push_back({-corner, corner});
    towers.push_back({len - corner, len + corner});
    if (edge == gateEdge) {
        const Fixed reach = thick * 6 / 5 + kTowerClear;
        for (int s = -1; s <= 1; s += 2) {
            const Fixed at = len / 2 + (gateWidth / 2 + thick) * s;
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

}  // namespace illyria::sim
