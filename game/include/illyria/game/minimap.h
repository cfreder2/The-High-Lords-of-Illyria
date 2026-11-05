#pragma once

#include <string>

#include "illyria/sim/campaign.h"

namespace illyria::game {

// The conquest's overview as text, one line a thing (the page draws it): "m|w|h" the map's size in metres;
// "t|x|y|colour|capital" a town in the colour of the holder the viewer knows; "s|x|y|kind" a site; "k|colour|x|y|n" a
// heap of n men; "d|colour|x|y" a man standing apart; "f|x|y|n" men fighting. Everything of another side that the
// viewer is shown goes through the one seam of sight (Campaign::canSee): a town it cannot see is nobody's, a man it
// cannot see is in no heap, no point and no fight (tests/test_campaign_sight.cpp).
std::string minimapText(const sim::Campaign& conquest, uint8_t viewer);

}  // namespace illyria::game
