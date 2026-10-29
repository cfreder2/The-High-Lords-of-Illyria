#include "closeup.h"

#include <algorithm>
#include <cmath>

namespace illyria::game::closeup {

std::vector<int> choose(std::vector<Need> needs, double budget) {
    const auto magnified = [](const Need& n) { return n.sheetPpm > 0 ? n.pxPerMetre / n.sheetPpm : 0.0f; };
    std::vector<Need> wanted;
    for (const Need& n : needs) {
        const float m = magnified(n);
        if (n.available && (m > kLoadAbove || (n.loaded && m > kKeepAbove))) wanted.push_back(n);
    }
    // The most magnified first; ties to the one already held, then by type, so the choice is stable.
    std::sort(wanted.begin(), wanted.end(), [&](const Need& a, const Need& b) {
        const float ma = magnified(a), mb = magnified(b);
        if (ma != mb) return ma > mb;
        if (a.loaded != b.loaded) return a.loaded;
        return a.type < b.type;
    });
    std::vector<int> out;
    double used = 0;
    for (const Need& n : wanted) {
        if (used + n.texels > budget) continue;
        used += n.texels;
        out.push_back(n.type);
    }
    return out;
}

float pxPerMetre(float screenPx, float fovY, float d) {
    return screenPx / (2.0f * std::tan(fovY / 2) * std::max(d, 1e-3f));
}

float minDistance(float screenPx, float fovY, float ppm, float floor) {
    if (ppm <= 0) return floor;
    return std::max(floor, screenPx / (2.0f * std::tan(fovY / 2) * kMaxMagnification * ppm));
}

float minDistanceFor(float cameraDistance, float pxPerMetre, float ppm) {
    return ppm > 0 ? cameraDistance * pxPerMetre / (kMaxMagnification * ppm) : 0.0f;
}

}  // namespace illyria::game::closeup
