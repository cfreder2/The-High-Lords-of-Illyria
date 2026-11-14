#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "illyria/sim/conquest.h"

namespace illyria::game {

class Battle;

// The conquest's overview as text, one line a thing (the page and the desktop's overlay draw it): "m|w|h" the map's
// size in metres; "t|x|y|colour|capital" a town in the colour of the holder the viewer knows; "s|x|y|kind" a site;
// "k|colour|x|y|n" a heap of n men; "d|colour|x|y" a man standing apart; "f|x|y|n" men fighting. Everything of another
// side that the viewer is shown goes through the one seam of sight (Conquest::canSee): a town it cannot see is nobody's,
// a man it cannot see is in no heap, no point and no fight (tests/test_conquest_sight.cpp).
std::string minimapText(const sim::Conquest& conquest, uint8_t viewer);

// A battle's overview, in the same lines ("m", "k", "d", "f"; no towns or sites) in the field's own metres from its
// south-west corner. The men are those Battle::sees allows, so a field with fog of war leaves out the foe it cannot see.
std::string battleMinimapText(const Battle& battle);

// The battlefield's ground as RGBA, w x h, rows from the north: water, woods, roads, rock and the lie of the hills.
std::vector<uint8_t> battleMinimapLand(const Battle& battle, int w, int h);

}  // namespace illyria::game
