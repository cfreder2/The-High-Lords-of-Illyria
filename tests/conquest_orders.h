#pragma once

// The tests' way into what an Order does (Conquest's private mutators), where a test is about the thing itself and
// wants its answer at once (is there a route? may the hero search here?). The game never does: it submits Orders
// (Conquest::submit), which are applied inside step(); the tests of that path are in test_conquest_save.cpp.
#include "illyria/sim/conquest.h"

namespace illyria::sim {

struct ConquestOrders {
    static bool move(Conquest& c, uint32_t stack, int32_t cx, int32_t cy) { return c.move(stack, cx, cy); }
    static bool produce(Conquest& c, int32_t city, int32_t option) { return c.produce(city, option); }
    static bool vectorProduction(Conquest& c, int32_t city, int32_t to) { return c.vectorProduction(city, to); }
    static bool disband(Conquest& c, uint32_t stack, uint32_t army) { return c.disband(stack, army); }
    static bool buildBoat(Conquest& c, int32_t city) { return c.buildBoat(city); }
    static bool embark(Conquest& c, uint32_t stack) { return c.embark(stack); }
    static bool land(Conquest& c, uint32_t stack) { return c.land(stack); }
    static bool search(Conquest& c, uint32_t stack) { return c.search(stack); }
    static bool hire(Conquest& c, uint8_t side, bool accept) { return c.hire(side, accept); }
};

}  // namespace illyria::sim
