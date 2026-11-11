#pragma once

#include <cstdint>

#include "illyria/game/gmath.h"
#include "illyria/game/palette.h"
#include "illyria/sim/continent.h"

namespace illyria::game {

// Banner colours of the eight sides, in sim::Continent::factions() order.
// Which faction each side plays (a campaign may start as any: Campaign::Setup, factionOf); a side keeps the
// colours of the faction it plays. The identity until a campaign sets it.
inline uint8_t* sideFactions() {
    // (a skirmish's seats: the player the Moonward blue, the enemy the Ironhold red, then gold and green)
    static uint8_t table[sim::Continent::kFactionCount] = {6, 2, 1, 4, 5, 3, 0, 7};  // (kSkirmishSeats)
    return table;
}
// The colour a side wears: its faction's, as the player's choice leaves it (palette.h: the one place colours come from).
inline V3 factionColor(uint8_t owner) {
    if (owner < sim::Continent::kFactionCount) owner = sideFactions()[owner];
    return palette::faction(owner);
}

// Whose seat each of a skirmish's four teams is (the factions behind the Blue, Red, Gold and Green teams), and the
// order sideFactions has outside a conquest.
constexpr uint8_t kSkirmishSeats[8] = {6, 2, 1, 4, 5, 3, 0, 7};

// A banner's cloth: the lord's colour, or for nobody's (a neutral town, an
// outpost unclaimed) undyed linen, which the flag shader marks with a red cross.
inline V3 bannerColor(uint8_t owner) {
    return owner < sim::Continent::kFactionCount ? factionColor(owner) : V3{0.8f, 0.74f, 0.6f};
}

}  // namespace illyria::game
