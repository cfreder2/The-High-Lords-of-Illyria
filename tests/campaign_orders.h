#pragma once

// The tests' way into what an Order does (Campaign's private mutators), where a test is about the thing itself and
// wants its answer at once (is there a route? may the hero search here?). The game never does: it submits Orders
// (Campaign::submit), which are applied inside step(); the tests of that path are in test_campaign_save.cpp.
#include "illyria/sim/campaign.h"

namespace illyria::sim {

struct CampaignOrders {
    static bool move(Campaign& c, uint32_t stack, int32_t cx, int32_t cy) { return c.move(stack, cx, cy); }
    static bool produce(Campaign& c, int32_t city, int32_t option) { return c.produce(city, option); }
    static bool vectorProduction(Campaign& c, int32_t city, int32_t to) { return c.vectorProduction(city, to); }
    static bool disband(Campaign& c, uint32_t stack, uint32_t army) { return c.disband(stack, army); }
    static bool buildBoat(Campaign& c, int32_t city) { return c.buildBoat(city); }
    static bool embark(Campaign& c, uint32_t stack) { return c.embark(stack); }
    static bool land(Campaign& c, uint32_t stack) { return c.land(stack); }
    static bool search(Campaign& c, uint32_t stack) { return c.search(stack); }
    static bool hire(Campaign& c, uint8_t side, bool accept) { return c.hire(side, accept); }
};

}  // namespace illyria::sim
