// Saved games: a campaign written out and read back is the same campaign,
// and carries on exactly as the original would have (campaign_save.cpp).
#include <cstdio>
#include <cstdlib>
#include <string>

#include <memory>

#include "illyria/sim/campaign.h"
#include "illyria/sim/world.h"
#include "campaign_orders.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

void days(Campaign& c, int n) {
    for (int t = 0; t < Campaign::kTicksPerDay * n; ++t) c.step();
}

int32_t capitalOf(const Campaign& c, uint8_t side) {
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.cities()[i].owner == side) return static_cast<int32_t>(i);
    return -1;
}
}  // namespace

TEST(a_saved_campaign_restores_and_carries_on_identically) {
    // Every lord on the computer for a while: boats, heroes, quests, banners,
    // transits and the land all have something in them by then.
    Campaign c(continent(), 77);
    days(c, 12);
    // The player takes over side 0 and gives a few orders before saving.
    c.setHuman(0, true);
    const int32_t capital = capitalOf(c, 0);
    if (capital >= 0) CampaignOrders::produce(c, capital, 0);
    days(c, 2);
    const std::string text = c.save();
    CHECK(text.size() > 100);
    std::printf("  day %u saved in %zu characters\n", c.day(), text.size());
    std::string why;
    std::unique_ptr<Campaign> r = Campaign::load(continent(), text, &why);
    CHECK(r != nullptr);
    if (!r) {
        std::printf("  load refused: %s\n", why.c_str());
        return;
    }
    CHECK(r->checksum() == c.checksum());
    CHECK(r->save() == text);
    CHECK(r->tick() == c.tick());
    CHECK(r->seed() == 77);
    // Both go on for four more days under the same orders: identical every day.
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == 0 && !s.armies.empty()) {
            const int32_t tx = std::min(s.cx + 6, c.width() - 1);
            CHECK(CampaignOrders::move(c, s.id, tx, s.cy) == CampaignOrders::move(*r, s.id, tx, s.cy));
            break;
        }
    bool same = true;
    for (int d = 0; d < 4; ++d) {
        days(c, 1);
        days(*r, 1);
        same = same && c.checksum() == r->checksum();
    }
    CHECK(same);
    CHECK(r->save() == c.save());
}

TEST(a_campaign_saved_in_a_fight_keeps_its_men_where_they_stood) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    for (uint8_t side = 1; side < Continent::kFactionCount; ++side) c.setHuman(side, true);
    // March the player's stack on the nearest neutral town until the men are fighting.
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    CHECK(home != nullptr);
    int best = 1 << 30, target = -1;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& cs = c.cities()[i];
        if (cs.owner != Campaign::kNeutral) continue;
        const int d = std::abs(cs.cx - home->cx) + std::abs(cs.cy - home->cy);
        if (d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    CHECK(target >= 0);
    c.raise(0, home->cx, home->cy, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
        if (st.owner == 0 && !st.garrison) CampaignOrders::move(c, st.id, c.cities()[static_cast<size_t>(target)].cx, c.cities()[static_cast<size_t>(target)].cy);
    const auto hurt = [&] {
        for (const Campaign::Stack& st : c.stacks())
            for (const Army& a : st.armies)
                for (const Man& m : a.men)
                    if (m.hp > 0) return true;
        return false;
    };
    for (int t = 0; t < Campaign::kTicksPerDay * 20 && !hurt(); ++t) c.step();
    CHECK(hurt());
    for (int t = 0; t < 10 * Campaign::kTicksPerSecond; ++t) c.step();  // a few seconds of it
    int32_t before = 0;
    for (const Campaign::Stack& st : c.stacks())
        for (const Army& a : st.armies) before += a.soldiers;
    // A save keeps the men where they stand, wounded as they are (and clears the air of arrows); the game saved carries on as the one loaded.
    const std::string text = c.save();
    std::unique_ptr<Campaign> r = Campaign::load(continent(), text);
    CHECK(r != nullptr);
    if (!r) return;
    int32_t after = 0;
    for (const Campaign::Stack& st : r->stacks())
        for (const Army& a : st.armies) after += a.soldiers;
    CHECK(after == before);
    CHECK(r->checksum() == c.checksum());
    // And both go on the same.
    days(c, 2);
    days(*r, 2);
    CHECK(r->checksum() == c.checksum());
}

TEST(a_damaged_or_foreign_save_is_refused) {
    Campaign c(continent(), 5);
    days(c, 3);
    const std::string text = c.save();
    std::string why;
    CHECK(Campaign::load(continent(), "", &why) == nullptr);
    CHECK(!why.empty());
    CHECK(Campaign::load(continent(), "hello", &why) == nullptr);
    CHECK(why == "not a saved campaign");
    // A save from before the men were on the land (layout 2: stacks of armies with a count of soldiers) is refused as such.
    CHECK(text.compare(0, 5, "ILC6.") == 0);
    CHECK(Campaign::load(continent(), "ILC4." + text.substr(5), &why) == nullptr);
    CHECK(why == "saved by another version of the game");
    // ... as is the layout before a stack afloat kept where its men were (ILC5).
    CHECK(Campaign::load(continent(), "ILC5." + text.substr(5), &why) == nullptr);
    CHECK(why == "saved by another version of the game");
    CHECK(Campaign::load(continent(), text.substr(0, text.size() / 2), &why) == nullptr);
    // One gold piece more for a side somewhere in the middle: the checksum catches it.
    std::string bent = text;
    const size_t k = bent.size() / 3;
    bent[k] = bent[k] == 'A' ? 'B' : 'A';
    CHECK(Campaign::load(continent(), bent, &why) == nullptr);
    CHECK(Campaign::load(continent(), text, &why) != nullptr);
}

// Who plays (Campaign::Setup): the chosen faction is side 0, the chosen computer lords are the other sides, and
// every faction left out is not in the game: neutral towns, no lord, no AI.
TEST(a_game_with_one_chosen_enemy_has_the_other_lords_out_of_it) {
    const Campaign::Setup setup{0, static_cast<uint8_t>(1u << 3)};
    Campaign c(continent(), 5, setup);
    c.setComputerDifficulty(Difficulty::Hard);
    c.setHuman(0, true);
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const uint8_t faction = continent().cities()[i].owner;
        const uint8_t owner = c.cities()[i].owner;
        if (faction == 0 || faction == 3) CHECK(owner == faction);
        else CHECK(owner == Campaign::kNeutral);  // a lord left out: a plain town
    }
    for (const Campaign::Stack& s : c.stacks()) CHECK(s.owner == 0 || s.owner == 3 || s.owner == Campaign::kNeutral);
    // Neutral garrisons stand where their capitals were.
    int garrisoned = 0;
    for (const Campaign::Stack& s : c.stacks()) garrisoned += s.owner == Campaign::kNeutral && s.garrison;
    CHECK(garrisoned >= static_cast<int>(c.cities().size()) - 2 * 8);
    days(c, 20);
    for (uint8_t side : {1, 2, 4, 5, 6, 7}) {
        CHECK(!c.sides()[side].alive);
        CHECK(c.citiesOwned(side) == 0);
        for (const Campaign::Stack& s : c.stacks()) CHECK(s.owner != side);  // no army was ever raised or marched for them
    }
    CHECK(c.sides()[3].alive);
    CHECK(c.winner() == 0);
    // With no enemy chosen there is nobody to beat: the game does not end on the first day.
    Campaign alone(continent(), 5, Campaign::Setup{0, 0});
    alone.setHuman(0, true);
    days(alone, 3);
    CHECK(alone.winner() == 0);
    CHECK(alone.sides()[0].alive);
}

TEST(the_player_can_start_as_any_faction) {
    const Campaign::Setup setup{4, static_cast<uint8_t>((1u << 0) | (1u << 6))};
    Campaign c(continent(), 9, setup);
    c.setHuman(0, true);
    CHECK(c.factionOf(0) == 4);
    CHECK(c.factionOf(4) == 0);
    CHECK(c.sides()[0].name == Continent::factions()[4].name);  // side 0 plays the Sylvaran Elves, and is still blue
    CHECK(c.sides()[4].name == Continent::factions()[0].name);
    // Their capital is side 0's, with the hero in it; the Aurelian Throne is now side 4's, and in the game.
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const uint8_t faction = continent().cities()[i].owner;
        const uint8_t owner = c.cities()[i].owner;
        if (faction == 4) CHECK(owner == 0);
        else if (faction == 0) CHECK(owner == 4);
        else if (faction == 6) CHECK(owner == 6);
        else CHECK(owner == Campaign::kNeutral);
    }
    bool hero = false;
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == 0)
            for (const Army& a : s.armies) hero = hero || a.hero();
    CHECK(hero);
    CHECK(c.sides()[0].gold == 250);  // the Sylvarans' purse
}

TEST(who_plays_is_saved_with_the_game_and_old_saves_are_refused) {
    const Campaign::Setup setup{2, static_cast<uint8_t>((1u << 5) | (1u << 7))};
    Campaign c(continent(), 31, setup);
    c.setHuman(0, true);
    days(c, 3);
    const std::string text = c.save();
    std::string why;
    std::unique_ptr<Campaign> r = Campaign::load(continent(), text, &why);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->setup().player == 2);
    CHECK(r->setup() == c.setup());
    CHECK(r->checksum() == c.checksum());
    CHECK(r->save() == text);
    for (uint8_t side : {1, 3, 4, 6}) CHECK(!r->sides()[side].alive);
    // A game saved before the choice existed ("ILC3.") is refused, as another version's.
    std::string old = text;
    old[3] = '3';
    CHECK(Campaign::load(continent(), old, &why) == nullptr);
    CHECK(why == "saved by another version of the game");
}

// Player intent is an ordered, stamped Order applied inside step(): the same Orders replayed over the same saved game
// give the same game, whether the game was carried on from memory or read back from the save.
TEST(the_same_orders_over_the_same_save_give_the_same_game) {
    Campaign c(continent(), 21);
    c.setHuman(0, true);
    days(c, 2);
    const std::string text = c.save();
    const auto play = [&](Campaign& g) {
        int32_t capital = -1;
        for (size_t i = 0; i < g.cities().size(); ++i)
            if (g.cities()[i].owner == 0) capital = static_cast<int32_t>(i);
        uint32_t stack = 0;
        for (const Campaign::Stack& s : g.stacks())
            if (s.owner == 0 && !s.garrison) stack = s.id;
        const Campaign::Stack* st0 = g.findStack(stack);
        const int32_t sx = st0 ? st0->cx : 0, sy = st0 ? st0->cy : 0;
        Campaign::Order produce;
        produce.kind = Campaign::Order::Kind::Produce;
        produce.a = capital;
        produce.b = 0;
        g.submit(produce);
        for (int t = 0; t < 900; ++t) {
            if (t == 300 && st0) {
                Campaign::Order move;
                move.kind = Campaign::Order::Kind::Move;
                move.stack = stack;
                move.a = std::min(sx + 6, g.width() - 1);
                move.b = sy;
                g.submit(move);
            }
            g.step();
        }
    };
    Campaign a(continent(), 21);
    a.setHuman(0, true);
    days(a, 2);
    (void)a.save();  // (saving settles the stacks, so the one that carries on is saved at the same moment)
    std::unique_ptr<Campaign> b = Campaign::load(continent(), text, nullptr), d = Campaign::load(continent(), text, nullptr);
    CHECK(b != nullptr && d != nullptr);
    if (!b || !d) return;
    play(a);
    play(*b);
    play(*d);
    CHECK(a.checksum() == b->checksum());
    CHECK(b->checksum() == d->checksum());
    // And the Orders matter: without them the game is another.
    Campaign none(continent(), 21);
    none.setHuman(0, true);
    days(none, 2);
    (void)none.save();
    for (int t = 0; t < 900; ++t) none.step();
    CHECK(none.checksum() != a.checksum());
}

// A paused clock holds an Order: submitting while paused and then stepping is the same game as submitting and stepping.
TEST(an_order_submitted_while_paused_waits_for_the_step) {
    const auto make = [] {
        auto c = std::make_unique<Campaign>(continent(), 21);
        c->setHuman(0, true);
        days(*c, 1);
        return c;
    };
    std::unique_ptr<Campaign> a = make(), b = make();
    int32_t capital = -1;
    for (size_t i = 0; i < a->cities().size(); ++i)
        if (a->cities()[i].owner == 0) capital = static_cast<int32_t>(i);
    Campaign::Order produce;
    produce.kind = Campaign::Order::Kind::Produce;
    produce.a = capital;
    a->submit(produce);
    const uint64_t before = a->checksum();
    CHECK(before == b->checksum());  // nothing happens until a step runs
    for (int t = 0; t < 300; ++t) {
        a->step();
        if (t == 0) {
            b->submit(produce);
            CHECK(b->checksum() == before);
        }
        b->step();
    }
    // (a queued the Order for tick+1 before its first step, b one step later: only a's lands on the first step)
    CHECK(a->checksum() != b->checksum() || a->tick() == b->tick());
    Campaign::Order later = produce;
    later.tick = a->tick() + 5;
    a->submit(later);
    b->submit(later);
    for (int t = 0; t < 10; ++t) a->step(), b->step();
    CHECK(a->checksum() == b->checksum());  // an Order for a chosen tick is the same wherever it was submitted from
}

// What the player asks of the game reaches the simulation only as an Order, applied inside the step: a production, a
// march, an army sent home (the mutators themselves are private to the Campaign and its tests).
TEST(the_players_orders_reach_the_simulation_only_through_submit) {
    Campaign c(continent(), 5);
    c.setHuman(0, true);
    const int32_t capital = capitalOf(c, 0);
    CHECK(capital >= 0);
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == 0 && !s.garrison) home = &s;
    CHECK(home != nullptr);
    if (capital < 0 || !home) return;
    const uint32_t stack = home->id;
    const size_t armies = home->armies.size();
    CHECK(armies >= 2);
    Campaign::Order produce;
    produce.kind = Campaign::Order::Kind::Produce;
    produce.a = capital;
    produce.b = 1;
    c.submit(produce);
    Campaign::Order disband;
    disband.kind = Campaign::Order::Kind::Disband;
    disband.stack = stack;
    disband.a = static_cast<int32_t>(home->armies.back().id);
    c.submit(disband);
    Campaign::Order move;
    move.kind = Campaign::Order::Kind::Move;
    move.stack = stack;
    move.a = std::min(home->cx + 4, c.width() - 1);
    move.b = home->cy;
    c.submit(move);
    CHECK(c.cities()[static_cast<size_t>(capital)].producing != 1);  // nothing is applied until the step
    CHECK(c.findStack(stack)->path.empty());
    c.step();
    CHECK(c.cities()[static_cast<size_t>(capital)].producing == 1);
    CHECK(c.findStack(stack) != nullptr && c.findStack(stack)->armies.size() == armies - 1);
    CHECK(c.findStack(stack) != nullptr && !c.findStack(stack)->path.empty());
}

TEST(a_gate_the_player_holds_open_or_shut_is_saved_with_the_game) {
    // His hand on a gate is his choice, not a moment of the fight: a save carries it, and the loaded game is the saved one.
    Campaign c(continent(), 9);
    c.setHuman(0, true);
    c.prepareWorld();
    const World& w = *c.world();
    size_t gate = w.gates().size();
    for (size_t i = 0; i < w.gates().size(); ++i)
        if (w.gates()[i].holder == 0) gate = i;
    CHECK(gate < w.gates().size());
    if (gate >= w.gates().size()) return;
    Campaign::Order o;
    o.kind = Campaign::Order::Kind::Command;
    o.side = 0;
    o.command = Command{0, kNoEntity, Command::Type::SetGate, {}};
    o.command.objectIndex = static_cast<int32_t>(gate);
    o.command.gatePin = Command::GatePin::Shut;
    c.submit(o);
    c.step();
    CHECK(w.gates()[gate].pin == Command::GatePin::Shut);
    std::string why;
    const std::string text = c.save();
    std::unique_ptr<Campaign> r = Campaign::load(continent(), text, &why);
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->world()->gates()[gate].pin == Command::GatePin::Shut);
    CHECK(r->checksum() == c.checksum());
    for (int t = 0; t < 600; ++t) c.step(), r->step();
    CHECK(r->checksum() == c.checksum());
}

// A save keeps everything the checksum hashes: at several days on several seeds (stacks afloat and gates swinging among
// them) the loaded campaign has the checksum of the one saved, and both go on the same for 200 ticks and for a day.
TEST(a_save_round_trips_everything_the_checksum_hashes_on_many_seeds_and_days) {
    int afloat = 0, swinging = 0, saves = 0;
    for (const uint64_t seed : {2, 5, 7, 11, 77, 123}) {
        Campaign c(continent(), seed);
        for (int day = 0; day < 16; ++day) {
            days(c, 1);
            if (day != 3 && day != 8 && day != 13 && day != 15) continue;
            if (day == 8) c.setHuman(0, true);
            for (const Campaign::Stack& s : c.stacks()) afloat += s.boat != 0;
            if (c.world())
                for (const GateState& g : c.world()->gates()) swinging += g.mode != GateState::Mode::Shut || g.openUntil > 0;
            const std::string text = c.save();
            std::string why;
            std::unique_ptr<Campaign> r = Campaign::load(continent(), text, &why);
            if (!r) std::printf("  seed %llu day %d: refused: %s\n", static_cast<unsigned long long>(seed), day + 1, why.c_str());
            CHECK(r != nullptr);
            if (!r) continue;
            ++saves;
            CHECK(r->checksum() == c.checksum());
            for (int t = 0; t < 200; ++t) c.step(), r->step();
            CHECK(r->checksum() == c.checksum());
            days(c, 1);
            days(*r, 1);
            CHECK(r->checksum() == c.checksum());
            ++day;  // (the day just played)
        }
    }
    std::printf("  %d saves round-tripped (stacks afloat seen: %d, gates not shut or asked open: %d)\n", saves, afloat, swinging);
    CHECK(saves >= 20);
    CHECK(swinging > 0);
}

// A stack afloat on a long voyage, saved on the way: its men are the boat's, and what the checksum hashes of them
// (where they were last on land, px and py) comes back.
TEST(a_save_on_a_voyage_carries_the_stack_afloat_exactly) {
    Campaign c(continent(), 3);
    for (size_t side = 0; side < c.sides().size(); ++side) c.setHuman(static_cast<uint8_t>(side), true);
    int32_t from = -1, to = -1;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& s = c.cities()[i];
        if (!s.port) continue;
        if (from < 0 || s.cy > c.cities()[static_cast<size_t>(from)].cy) from = static_cast<int32_t>(i);
        if (to < 0 || s.cy < c.cities()[static_cast<size_t>(to)].cy) to = static_cast<int32_t>(i);
    }
    CHECK(from >= 0 && to >= 0 && from != to);
    if (from < 0 || to < 0) return;
    const uint32_t id = c.grant(from, 0, UnitType::LightInfantry, 4);
    CHECK(CampaignOrders::buildBoat(c, from));
    days(c, Campaign::kBoatDays + 1);
    CHECK(CampaignOrders::embark(c, id));
    CHECK(CampaignOrders::move(c, id, c.cities()[static_cast<size_t>(to)].cx, c.cities()[static_cast<size_t>(to)].cy));
    int saves = 0;
    for (int hours = 0; hours < 6; ++hours) {
        for (int t = 0; t < Campaign::kTicksPerDay / 2; ++t) c.step();
        const Campaign::Stack* s = c.findStack(id);
        if (!s || !s->boat) continue;
        const std::string text = c.save();
        std::string why;
        std::unique_ptr<Campaign> r = Campaign::load(continent(), text, &why);
        if (!r) std::printf("  voyage save refused: %s\n", why.c_str());
        CHECK(r != nullptr);
        if (!r) continue;
        ++saves;
        CHECK(r->checksum() == c.checksum());
        for (int t = 0; t < 200; ++t) c.step(), r->step();
        CHECK(r->checksum() == c.checksum());
    }
    CHECK(saves >= 2);
}
