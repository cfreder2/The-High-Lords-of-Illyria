#pragma once

#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"

namespace illyria::game {

// The rivers as the far map's ribbons (renderer.cpp draws them, kRibbonVs shades them): presentation only, nothing here
// feeds the simulation. A river that reaches the sea is cut where it crosses the coastline, so no ribbon lies over open
// sea (the sim carries a mouth 80 m out so that the estuary can be carved there), and every vertex knows how far it is from
// that shore along the river: over the last kMouthFade metres (the flare of the mouth) the ribbon fades into the water the
// land mesh itself draws there, so a river widens and runs into the sea's own colour.
struct RibbonSegment {
    V3 a, b;               // battle-local, z = the water's surface
    float halfWidth = 0;
    float toMouthA = kNoMouth, toMouthB = kNoMouth;  // metres to the shore along the river (kNoMouth: it does not end in the sea)
    static constexpr float kNoMouth = 1e5f;
};
constexpr float kMouthFade = 140.0f;  // sim::Continent's kMouthFlareLength

std::vector<RibbonSegment> riverRibbons(const sim::Continent& continent, const V3& origin);

}  // namespace illyria::game
