#pragma once

// The one place a side's colour comes from. Everything that draws a side (the banner, the roofs, flags, cones, dots,
// health bars, the squad cards' bars, the minimap, the sprites' team key, the corpses) asks factionColor
// (factions.h) or teamColour (mode.h), and both end here.
//
// A side's colour is cosmetic: the player may choose their own from a curated palette (or, with a check, a colour of
// their own), and the other sides keep theirs unless one is the chosen colour, in which case it takes the colour the
// player gave up. Nothing here is read by the simulation, a command, a hash or a save: it is a presentation table, set
// by the shell from a setting kept on the device.

#include <cstdint>
#include <string>

#include "illyria/game/gmath.h"

namespace illyria::game::palette {

constexpr int kFactions = 8;   // sim::Continent::kFactionCount
constexpr int kSwatches = 12;  // the curated palette: the eight lords' own colours, then four more

// Two colours are apart when their CIE76 distance (Lab, D65) is at least kApart; a colour of the player's own must also
// keep kApartLand from the land and water it is seen against.
constexpr float kApart = 25.0f;
constexpr float kApartLand = 20.0f;

struct Swatch {
    const char* name;
    V3 rgb;
};
const Swatch& swatch(int index);  // 0..kSwatches-1; swatch(f) for f < 8 is faction f's own colour

float distance(const V3& a, const V3& b);  // perceptual (Lab) distance
V3 factionBase(uint8_t faction);           // a lord's own colour, before any choice
int land(V3* out, int cap);                // the ground colours a banner is seen against (for the checks)

// What the player asked for: a swatch, or a colour of their own. Cleared: everyone keeps their own.
void choose(const V3& rgb);
void clear();
bool chosen();
V3 wanted();

// The faction the player leads (the local side's); rebuilds the table. Sides other than that one change only to make
// room for the player's colour.
void bind(uint8_t faction);
uint8_t bound();

// A colour as it would be given to `faction` after the player's choice: the swatch itself; a custom colour that is
// apart from every other lord and from the land; otherwise the nearest swatch.
V3 snap(const V3& rgb, uint8_t faction);

// The colour a faction wears now.
V3 faction(uint8_t faction);

// "#rrggbb".
std::string hex(const V3& rgb);
bool parseHex(const char* text, V3& out);

}  // namespace illyria::game::palette
