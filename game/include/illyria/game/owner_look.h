#pragma once

// How a town looks in its holder's colours: the one table behind the tile
// roofs of its houses, the cloth of its tower and keep flags, and the
// conical roofs of its towers and keep turrets. Presentation only: it is
// derived from who holds the town now, as far as the viewer knows (a town the
// viewer cannot see is drawn as nobody's), every frame the holder changes;
// nothing here is kept in, or hashed by, the simulation.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "illyria/game/factions.h"
#include "illyria/sim/conquest.h"

namespace illyria::game {

struct OwnerLook {
    V3 roof;  // the house tiles' base colour (the shader keeps their texture and shading on it)
    V3 flag;  // the flag cloth: the lord's colour; undyed linen, with the red cross, for nobody's
    V3 cone;  // the conical roofs of towers and keep turrets (the cone's shade is already in it)
};

// A side (or sim::Continent::kNeutral) to its look. A lord's roof is his colour
// as clay, kept between a floor and a ceiling so the black lord's roof is not
// lost in the grass's shadow nor the white lord's blown out by the sun; the
// cone is a darker shade of it. Nobody's town: plain dull clay under the
// original slate.
inline OwnerLook ownerLook(uint8_t owner) {
    OwnerLook look;
    look.flag = bannerColor(owner);
    if (owner >= sim::Continent::kFactionCount) {
        look.roof = {0.55f, 0.45f, 0.40f};
        look.cone = {0.30f, 0.32f, 0.38f};
        return look;
    }
    const V3 c = factionColor(owner);
    const auto clay = [](float v) { return std::clamp(v * 0.85f, 0.17f, 0.74f); };
    look.roof = {clay(c.x), clay(c.y), clay(c.z)};
    const auto slate = [](float v) { return std::max(0.5f * v, 0.1f); };
    look.cone = {slate(look.roof.x), slate(look.roof.y), slate(look.roof.z)};
    return look;
}

// Every town's look, given who holds each.
inline std::vector<OwnerLook> townLooks(const std::vector<uint8_t>& owners) {
    std::vector<OwnerLook> out;
    out.reserve(owners.size());
    for (uint8_t w : owners) out.push_back(ownerLook(w));
    return out;
}

// Who holds each town as `viewer` knows it (Conquest::knownOwner: what the game already shows of a town's holder;
// a town out of sight is nobody's).
inline std::vector<uint8_t> knownOwners(const sim::Conquest& c, uint8_t viewer) {
    std::vector<uint8_t> out;
    out.reserve(c.cities().size());
    for (size_t i = 0; i < c.cities().size(); ++i) out.push_back(c.knownOwner(viewer, static_cast<int32_t>(i)));
    return out;
}

}  // namespace illyria::game
