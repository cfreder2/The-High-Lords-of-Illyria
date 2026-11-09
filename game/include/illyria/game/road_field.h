#pragma once
// The road field the ground shader reads (uRoad, RG8, linear): one texel per 2 m, R the metres from the nearest road's middle
// line (of kRoadFieldRange), G that road's half width there (of kRoadFieldHalfWidth). Where no road is within
// kRoadFieldReach, R is 255 and G 0, so the linear filter fades a road out instead of drawing a seam at the field's edge.
// The width is the sim's (sim::Polyline::widthAt), so what is drawn is what Ground::Road covers.
#include <algorithm>
#include <cstdint>

namespace illyria::game {

constexpr float kRoadFieldRange = 16.0f;      // metres R can say
constexpr float kRoadFieldHalfWidth = 8.0f;   // metres G can say
constexpr float kRoadFieldReach = 12.0f;      // roads farther than this from a texel are not in it

// A texel for a road `dist` metres off, `halfWidth` metres to a side.
inline void roadTexel(float dist, float halfWidth, uint8_t* out) {
    out[0] = static_cast<uint8_t>(std::clamp(dist / kRoadFieldRange * 255.0f + 0.5f, 0.0f, 255.0f));
    out[1] = static_cast<uint8_t>(std::clamp(halfWidth / kRoadFieldHalfWidth * 255.0f + 0.5f, 0.0f, 255.0f));
}
inline void noRoadTexel(uint8_t* out) {
    out[0] = 255;
    out[1] = 0;
}

}  // namespace illyria::game
