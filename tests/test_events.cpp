// The strip of events (battle_events.cpp) and the first-order count: a squad wiped out, a hero fallen, said once and
// kept a little while; an order given counts.
#include <algorithm>
#include <string>

#include "illyria/game/battle.h"
#include "test.h"

using illyria::game::Battle;
using illyria::game::Squad;

namespace {
bool said(const Battle& b, const std::string& part) {
    const auto& e = b.events();
    return std::any_of(e.begin(), e.end(), [&](const Battle::Event& x) { return x.text.find(part) != std::string::npos; });
}
}  // namespace

TEST(a_squad_wiped_out_and_a_hero_fallen_are_said_once) {
    Battle b(42);
    b.advance(0.5);
    CHECK(b.events().empty());  // (nothing has happened yet)
    const Squad* watch = nullptr;
    const Squad* lord = nullptr;
    for (const Squad& s : b.squads()) {
        if (s.name == "The Grey Watch") watch = &s;
        if (s.name == "Lord Aldric") lord = &s;
    }
    CHECK(watch != nullptr && lord != nullptr);
    if (!watch || !lord) return;
    for (illyria::sim::EntityId id : watch->members) b.testRemove(id);
    b.advance(0.5);
    CHECK(said(b, "The Grey Watch was wiped out"));
    CHECK(!said(b, "Lord Aldric"));
    for (illyria::sim::EntityId id : lord->members) b.testRemove(id);
    b.advance(0.5);
    CHECK(said(b, "Lord Aldric has fallen"));
    // Once each: the squads stay dead and say nothing more.
    const size_t n = b.events().size();
    b.advance(2.0);
    CHECK(b.events().size() == n);
    // Fresh for a few seconds (the HUD reads the clock against kEventShown).
    CHECK(b.eventClock() - b.events().front().at < Battle::kEventShown);
}

TEST(an_order_given_is_counted_for_the_first_order_hint) {
    Battle b(42);
    CHECK(b.ordersGiven() == 0);
    b.advance(0.2);
    b.selectSquad(1);
    b.orderMove(b.squadCentre(b.squads()[1]) + illyria::game::V3{20, 0, 0});
    CHECK(b.ordersGiven() > 0);
}
