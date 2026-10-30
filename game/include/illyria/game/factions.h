#pragma once

#include <cstdint>

#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"

namespace illyria::game {

// Banner colours of the eight sides, in sim::Continent::factions() order.
inline V3 factionColor(uint8_t owner) {
    static const V3 kColors[sim::Continent::kFactionCount] = {
        {0.95f, 0.95f, 0.92f},  // the Aurelian Throne: white
        {0.92f, 0.75f, 0.15f},  // the Highcrag Giants: gold
        {0.80f, 0.15f, 0.12f},  // the Ironhold Dwarves: red
        {0.92f, 0.48f, 0.12f},  // the Orcs of Grath: orange
        {0.16f, 0.60f, 0.24f},  // the Sylvaran Elves: green
        {0.60f, 0.40f, 0.24f},  // the Sunreach Riders: tan
        {0.20f, 0.36f, 0.82f},  // the Moonward Order: blue
        {0.10f, 0.10f, 0.10f},  // Lord Malvoren: black
    };
    return owner < sim::Continent::kFactionCount ? kColors[owner] : V3{0.6f, 0.6f, 0.6f};
}

// A banner's cloth: the lord's colour, or for nobody's (a neutral town, an
// outpost unclaimed) undyed linen, which the flag shader marks with a red cross.
inline V3 bannerColor(uint8_t owner) {
    return owner < sim::Continent::kFactionCount ? factionColor(owner) : V3{0.8f, 0.74f, 0.6f};
}

}  // namespace illyria::game
