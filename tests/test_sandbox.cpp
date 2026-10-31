// The sandbox (game/src/mode_sandbox.cpp): where its squads are set down.
#include <memory>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/mode.h"
#include "illyria/sim/region.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;

namespace {

std::shared_ptr<const sim::Continent> continent() {
    static const auto c = std::make_shared<const sim::Continent>(Battle::kRegionSeed);
    return c;
}

// Inside a ruin's, temple's or tower's stones (Region: Obstacle::kSiteStone).
bool insideStone(const Battle& b, const sim::Vec3& p) {
    for (const sim::Obstacle& o : b.region().obstacles()) {
        if (o.variant != sim::Obstacle::kSiteStone) continue;
        if (o.kind == sim::Obstacle::Kind::House ? abs(p.x - o.pos.x) < o.halfX && abs(p.y - o.pos.y) < o.halfY
                                                 : (p - o.pos).flat().lengthSq() < o.radius * o.radius)
            return true;
    }
    return false;
}

}  // namespace

// On a ruin's or a temple's field no one stands inside its stones (on
// 2026-10-30 men were set down inside a ruin, which was only drawn: its
// stones are solid now, and the squads are set down clear of them).
TEST(sandbox_squads_stand_clear_of_buildings) {
    const std::vector<Place> places = battlePlaces(*continent());
    int fields = 0;
    for (size_t i = 0; i < places.size(); ++i) {
        if (places[i].kind != Place::Kind::Ruin && places[i].kind != Place::Kind::Temple) continue;
        std::unique_ptr<Battle> b = makeMatch("sandbox", "{\"place\":" + std::to_string(i) + "}", places, continent());
        CHECK(b != nullptr);
        if (!b) continue;
        b->advance(1.0);
        for (const sim::Unit& u : b->world().units()) {
            CHECK(b->world().standable(u.pos));
            CHECK(!insideStone(*b, u.pos));
        }
        ++fields;
    }
    CHECK(fields > 0);
}

// A field picked on the setup map in a lake moves out to the shore.
TEST(sandbox_field_picked_in_water_moves_ashore) {
    const std::vector<Place> places = battlePlaces(*continent());
    const sim::Continent& c = *continent();
    // The middle of the first lake.
    CHECK(!c.lakes().empty());
    if (c.lakes().empty()) return;
    const sim::Vec3 lake = c.lakes().front().centre;
    std::unique_ptr<Battle> b = makeMatch("sandbox", "{\"place\":0,\"x\":" + std::to_string(lake.x.floorToInt()) + ",\"y\":" +
                                                         std::to_string(lake.y.floorToInt()) + "}",
                                          places, continent());
    CHECK(b != nullptr);
    if (!b) return;
    for (const sim::Unit& u : b->world().units()) CHECK(b->world().standable(u.pos));
}


