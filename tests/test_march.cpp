// The March (game/src/march.cpp, docs/GAME_DESIGN.md §4.5.3): one army through five skirmishes. What carries from a
// battle to the next (the living, their wounds half mended, their kills and rank), what does not (a squad wiped out),
// the purse and what it can and cannot buy, the save, and the roster seated in a real Battle by the one setup code.
#include <algorithm>
#include <cstdio>
#include <string>

#include "illyria/game/march.h"
#include "illyria/sim/conquest.h"
#include "test.h"

using namespace illyria::game;
using illyria::sim::Conquest;
using illyria::sim::UnitType;

namespace {

March fresh() { return March::begin({{UnitType::HeavyInfantry, 2}, {UnitType::Archers, 1}, {UnitType::LightCavalry, 1}, {UnitType::DwarfGrenadier, 1}}, false); }

// What a battle would hand back if nobody had been touched.
std::vector<CarriedSquad> untouched(const March& m) { return m.setup().roster; }

const March::Squad* find(const March& m, const std::string& name) {
    for (const March::Squad& s : m.squads)
        if (s.name == name) return &s;
    return nullptr;
}

}  // namespace

TEST(march_begins_with_the_army_bought_a_lord_and_the_magi) {
    const March m = fresh();
    CHECK(m.squads.size() == 7 && m.stage == 0 && m.purse == 0 && !m.over());
    CHECK(m.budget == 2 * Conquest::price(UnitType::HeavyInfantry) + Conquest::price(UnitType::Archers) + Conquest::price(UnitType::LightCavalry) + Conquest::price(UnitType::DwarfGrenadier));
    CHECK(find(m, "Heavy Infantry 1") && find(m, "Heavy Infantry 2") && find(m, "Archers") && find(m, "The Lord") && find(m, "Magus of the Tower"));
    CHECK(find(m, "The Lord")->men.size() == 1 && find(m, "Archers")->men.size() == static_cast<size_t>(Conquest::squadSize(UnitType::Archers)));
    // Five battles: a field first, a wall to storm second (the first that asks for an order, and it has a time
    // limit: a march that is given none ends there), the Ford, a wall to hold, the capital last.
    CHECK(March::stageInfo(0).scenario == Scenario::Field && March::stageInfo(1).scenario == Scenario::Siege && !March::stageInfo(1).defend);
    CHECK(March::stageInfo(2).scenario == Scenario::Ford && March::stageInfo(3).scenario == Scenario::Siege && March::stageInfo(3).defend && March::stageInfo(4).scenario == Scenario::Capital);
    CHECK(m.setup().foePurse == m.budget * March::stageInfo(0).foePct / 100 && m.setup().roster.size() == m.squads.size() && m.setup().siegeSeconds == 0);
    for (int k = 0; k < March::kBattles; ++k) {
        const March::Stage& st = March::stageInfo(k);
        const bool storm = (st.scenario == Scenario::Siege || st.scenario == Scenario::Capital) && !st.defend;
        CHECK(st.foePct >= 30 && st.foePct <= 200);
        CHECK(storm ? st.siegeSeconds >= 5 * 60 : st.siegeSeconds == 0);  // a storm has its time; nothing else has
    }
    CHECK(March::stageInfo(4).siegeSeconds > March::stageInfo(1).siegeSeconds);  // (a capital has two walls)
}

TEST(march_survivors_carry_their_wounds_half_mended_their_kills_and_rank_and_a_wiped_squad_is_gone) {
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    // Heavy Infantry 1: three men left; the first at a fifth of his health with six kills and a rank; the others whole.
    after[0].men = {{200, 1, 6, 0}, {1000, 0, 0, 4}, {1000, 0, 1, 7}};
    after[2].men.clear();  // the Archers: to a man
    m.settle(1, after);
    CHECK(m.stage == 1 && !m.lost && m.squads.size() == 6);
    CHECK(!find(m, "Archers") && m.gone.size() == 1 && m.gone[0] == "Archers (The open field)");
    const March::Squad* h = find(m, "Heavy Infantry 1");
    CHECK(h && h->men.size() == 3 && h->full == Conquest::squadSize(UnitType::HeavyInfantry) && h->battles == 1 && h->kills == 7);
    CHECK(h->men[0].hp == 600 && h->men[0].rank == 1 && h->men[0].kills == 6 && h->men[0].battles == 1);  // 200, and half of the 800 missing
    CHECK(h->men[1].hp == 1000 && h->men[2].kills == 1);
    CHECK(m.last.lost == Conquest::squadSize(UnitType::HeavyInfantry) - 3 + Conquest::squadSize(UnitType::Archers));
    CHECK(m.last.survivors == m.men() && m.last.promoted == 1 && m.last.gone.size() == 1 && m.last.gone[0] == "Archers");
    CHECK(m.last.promotions.size() == 1 && m.last.promotions[0] == "Heavy Infantry 1: 1 promoted");
    // The purse: a share of what the army cost, never enough to buy it again.
    CHECK(m.purse == m.budget * March::kPursePct / 100 && m.purse * 4 < m.budget);
    // The next battle's roster is what is left, as it stands.
    const SkirmishSetup s = m.setup();
    CHECK(s.roster.size() == 6 && s.roster[0].name == "Heavy Infantry 1" && s.roster[0].men.size() == 3 && s.roster[0].men[0].hp == 600 && s.roster[0].men[0].kills == 6);
    CHECK(s.roster[0].full == Conquest::squadSize(UnitType::HeavyInfantry));
}

TEST(march_living_through_battles_makes_a_regular_then_a_veteran) {
    March m = fresh();
    for (int k = 1; k <= 4; ++k) {
        m.settle(1, untouched(m));
        const March::Man& man = find(m, "Heavy Infantry 2")->men[0];
        CHECK(man.battles == k && man.rank == (k >= March::kVeteranBattles ? 2 : k >= March::kSeasonedBattles ? 1 : 0));
        CHECK(find(m, "The Lord")->men[0].rank == 0);  // (a hero's level is his kills' alone)
    }
    // A man the battle itself promoted higher keeps it.
    March n = fresh();
    std::vector<CarriedSquad> after = untouched(n);
    after[1].men[0].rank = 3, after[1].men[0].kills = 11;
    n.settle(1, after);
    n.settle(1, untouched(n));
    CHECK(find(n, "Heavy Infantry 2")->men[0].rank == 3 && find(n, "Heavy Infantry 2")->men[1].rank == 1);
}

TEST(march_a_lost_battle_ends_it_and_five_won_finish_it) {
    March m = fresh();
    m.settle(1, untouched(m));
    const int purse = m.purse;
    m.settle(2, untouched(m));
    CHECK(m.lost && m.over() && !m.won() && m.stage == 1 && m.purse == purse);
    m.settle(1, untouched(m));  // (nothing is fought after the end)
    CHECK(m.lost && m.stage == 1);
    CHECK(m.reinforceCost(0) == 0 && m.reinforce(0) == 0);

    March w = fresh();
    for (int k = 0; k < March::kBattles; ++k) {
        CHECK(!w.over());
        w.settle(1, untouched(w));
    }
    CHECK(w.won() && w.over() && w.stage == March::kBattles && w.squads.size() == 7);
    CHECK(w.purse == 4 * (w.budget * March::kPursePct / 100));  // (no purse after the last: there is nothing left to buy for)
    // Won with nobody left standing is no win.
    March e = fresh();
    std::vector<CarriedSquad> none = untouched(e);
    for (CarriedSquad& c : none) c.men.clear();
    e.settle(1, none);
    CHECK(e.lost && e.squads.empty() && e.gone.size() == 7);
}

TEST(march_the_purse_fills_gaps_a_man_at_a_time_and_never_the_irreplaceable) {
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    const int full = Conquest::squadSize(UnitType::HeavyInfantry), price = Conquest::price(UnitType::HeavyInfantry);
    after[0].men.resize(static_cast<size_t>(full - 6));  // six gone from Heavy Infantry 1
    after[4].men.resize(2);                              // the dwarves cut to two
    after[6].men.resize(1);                              // one magus left
    m.settle(1, after);
    CHECK(March::irreplaceable(UnitType::Hero) && March::irreplaceable(UnitType::Wizards) && March::irreplaceable(UnitType::DwarfGrenadier) && March::irreplaceable(UnitType::Giants));
    CHECK(!March::irreplaceable(UnitType::HeavyInfantry) && !March::irreplaceable(UnitType::LightCavalry));
    CHECK(m.reinforceCost(4) == 0 && m.reinforceCost(6) == 0 && m.reinforceCost(5) == 0 && m.reinforce(4) == 0 && m.reinforce(6) == 0);
    CHECK(m.reinforceCost(1) == 0);  // (a full squad)
    CHECK(m.reinforceCost(0) == (price * 6 + full - 1) / full);
    // A purse too small for all six buys what it can.
    m.purse = (price * 2 + full - 1) / full;  // what two men cost
    const int joined = m.reinforce(0);
    CHECK(joined == 2 && m.purse == 0 && m.squads[0].men.size() == static_cast<size_t>(full - 4));
    CHECK(m.squads[0].men.back().rank == 0 && m.squads[0].men.back().kills == 0 && m.squads[0].men.back().hp == 1000);  // recruits
    m.purse = 0;
    CHECK(m.reinforce(0) == 0);
    m.purse = 1000;
    const int cost = m.reinforceCost(0);
    CHECK(m.reinforce(0) == 4 && m.purse == 1000 - cost && m.reinforceCost(0) == 0 && m.squads[0].men.size() == static_cast<size_t>(full));
}

TEST(march_the_purse_spent_at_once_goes_to_the_most_worn_squads_first) {
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    const int full = Conquest::squadSize(UnitType::HeavyInfantry);
    after[0].men.resize(static_cast<size_t>(full - 2));  // Heavy Infantry 1: two gone
    after[1].men.resize(2);                              // Heavy Infantry 2: cut to two
    after[4].men.resize(1);                              // the dwarves: never filled
    m.settle(1, after);
    const int need = m.reinforceCost(0) + m.reinforceCost(1) + m.reinforceCost(2) + m.reinforceCost(3);
    m.purse = m.reinforceCost(1);  // enough for the worst of them and no more
    CHECK(m.reinforceAll() == full - 2 && m.squads[1].men.size() == static_cast<size_t>(full) && m.squads[0].men.size() == static_cast<size_t>(full - 2) && m.purse == 0);
    CHECK(m.reinforceAll() == 0);
    m.purse = 1000;
    CHECK(m.reinforceAll() == 2 && m.squads[0].men.size() == static_cast<size_t>(full) && m.squads[4].men.size() == 1);
    CHECK(m.purse > 1000 - need && m.reinforceAll() == 0);
}

TEST(march_saves_to_one_line_and_loads_the_same) {
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    after[0].men = {{200, 1, 6, 0}, {1000, 0, 0, 4}};
    after[3].men.clear();
    m.settle(1, after);
    m.reinforce(0);
    const std::string text = m.save();
    CHECK(text.find('"') == std::string::npos && text.find('\n') == std::string::npos && text.find('\\') == std::string::npos);
    March back;
    CHECK(March::load(text, back));
    CHECK(back.save() == text);
    CHECK(back.stage == m.stage && back.purse == m.purse && back.budget == m.budget && back.squads.size() == m.squads.size() && back.gone == m.gone);
    CHECK(back.squads[0].men[0].hp == 600 && back.squads[0].men[0].kills == 6 && back.squads[0].kills == m.squads[0].kills && back.squads[0].battles == 1);
    CHECK(back.setup().roster[0].men.size() == m.setup().roster[0].men.size());
    // A lost march loads as lost; rubbish does not load, and leaves what was there.
    m.settle(2, untouched(m));
    March ended;
    CHECK(March::load(m.save(), ended) && ended.lost && ended.over());
    for (const char* bad : {"", "march2;300;0;0;0;0", "march1;300;0;9;0;0;S|A|1|12|0|0|1000.0.0.0", "march1;300;0;0;0;0;S|A|1|2|0|0|1000.0.0.0,1000.0.0.0,1000.0.0.0",
                            "march1;300;0;0;0;0;S|A\"b|1|12|0|0|1000.0.0.0", "march1;300;0;0;0;0;S|A|999|12|0|0|1000.0.0.0", "march1;300;0;0;0;0;S|A|1|12|0|0|0.0.0.0", "march1;300;0;0;0;0"})
        CHECK(!March::load(bad, back));
    CHECK(back.save() == text);
}

TEST(march_roster_is_seated_in_a_battle_by_the_skirmish_setup_and_read_back) {
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    after[0].men = {{400, 2, 6, 0}, {1000, 0, 0, 4}, {1000, 1, 2, 7}};
    after[2].men.clear();
    m.settle(1, after);  // (the men stand at 700, 1000, 1000 after the road)
    for (int stage : {0, 3, 4}) {  // open field, the village held (the keepers' stations), the capital stormed (two camps)
        m.stage = stage;
        const SkirmishSetup setup = m.setup();
        const Battle b(42, m.scenario(), setup);
        CHECK(b.defending() == March::stageInfo(stage).defend);
        // Every squad of the roster, by name, once; no second lord; the squad wiped out is not there.
        std::vector<CarriedSquad> now = b.carried();
        CHECK(now.size() == m.squads.size());
        int lords = 0, mine = 0;
        for (const illyria::sim::Unit& u : b.world().units()) {
            if (u.owner != Battle::kPlayer) continue;
            ++mine;
            lords += u.stats.type == static_cast<uint8_t>(UnitType::Hero);
        }
        CHECK(lords == 1 && mine == m.men());
        // The order may differ (ranks by kind, the keepers by station): match by name.
        for (const March::Squad& q : m.squads) {
            const auto it = std::find_if(now.begin(), now.end(), [&](const CarriedSquad& c) { return c.name == q.name; });
            CHECK(it != now.end());
            if (it == now.end()) continue;
            CHECK(it->men.size() == q.men.size() && it->full == q.full && it->type == q.type);
            for (size_t i = 0; i < q.men.size() && i < it->men.size(); ++i) {
                CHECK(it->men[i].slot == static_cast<int>(i) && it->men[i].rank == q.men[i].rank && it->men[i].kills == q.men[i].kills);
                CHECK(std::abs(it->men[i].hp - q.men[i].hp) <= 20);  // (whole hit points: within a point or two in a thousand)
            }
        }
        // The card counts the men against the squad's full strength; the result against what it brought.
        for (const Squad& q : b.squads())
            if (q.name == "Heavy Infantry 1" && q.owner == Battle::kPlayer) {  // (the other host may have a squad of the name)
                CHECK(b.aliveCount(q) == 3 && b.totalCount(q) == Conquest::squadSize(UnitType::HeavyInfantry) && b.standing(q).kills == 8);
            }
        const Battle::Result r = b.result();
        CHECK(r.lost == 0 && r.promoted == 0);
        // The other side raised what the stage says, not the player's purse.
        int foes = 0;
        for (const illyria::sim::Unit& u : b.world().units()) foes += u.owner == Battle::kEnemy;
        CHECK(foes > 0);
        CHECK(b.siegeSecondsLeft() == (stage == 4 ? March::stageInfo(4).siegeSeconds : -1) && !b.siegeTimedOut());
    }
}

TEST(march_a_siege_the_player_lays_is_lost_when_its_time_runs_out) {
    SkirmishSetup setup;
    setup.budget = 100;
    setup.siegeSeconds = 20;
    Battle b(42, Scenario::Siege, setup);
    CHECK(b.siegeSecondsLeft() == 20 && b.outcome() == 0);
    CHECK(b.siegeLine().find("0:20 left") != std::string::npos);
    for (int k = 0; k < 190; ++k) b.advance(0.1);
    CHECK(b.outcome() == 0 && b.siegeSecondsLeft() > 0 && b.siegeSecondsLeft() <= 2);
    for (int k = 0; k < 40 && !b.over(); ++k) b.advance(0.1);
    CHECK(b.siegeTimedOut() && b.outcome() == 2 && b.over() && b.siegeResult() == "The siege ran out of time.");
    // The keeper of a wall has no such clock, nor has a plain skirmish's besieger.
    setup.defend = true;
    CHECK(Battle(42, Scenario::Siege, setup).siegeSecondsLeft() == -1);
    setup.defend = false;
    setup.siegeSeconds = 0;
    const Battle plain(42, Scenario::Siege, setup);
    CHECK(plain.siegeSecondsLeft() == -1 && plain.siegeLine().find("left") == std::string::npos);
}

TEST(march_settles_by_the_squads_names_whatever_order_the_battle_tells_them_in) {
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    after[0].men.resize(5);
    std::reverse(after.begin(), after.end());
    after.erase(after.begin());  // (the magi are not told of: gone)
    m.settle(1, after);
    CHECK(m.squads.size() == 6 && m.squads[0].name == "Heavy Infantry 1" && m.squads[0].men.size() == 5 && m.squads[5].name == "The Lord");
    CHECK(m.gone.size() == 1 && m.gone[0] == "Magus of the Tower (The open field)");
}

TEST(march_stores_stay_spent_between_battles_and_the_camp_makes_them_up_for_gold) {
    // One dwarf of the pair comes out of a battle with two of his three satchels and one of his three bottles; the wizard
    // with one fireball.
    March m = fresh();
    std::vector<CarriedSquad> after = untouched(m);
    for (CarriedSquad& c : after) {
        if (c.type == UnitType::DwarfGrenadier) c.men[0].satchels = 2, c.men[0].ammo = 1;
        if (c.type == UnitType::Wizards) c.men[0].ammo = 1;
    }
    m.settle(1, after);
    const size_t dwarf = static_cast<size_t>(find(m, "Dwarven Grenadiers") - m.squads.data());
    CHECK(m.squads[dwarf].men[0].satchels == 2 && m.squads[dwarf].men[0].ammo == 1);
    int carried = 0, full = 0;
    m.load(dwarf, carried, full);
    CHECK(carried == 9 && full == 12);  // (his fellow's three and three untouched)
    // The next battle's roster says so, and the battle seats him so; a line of text keeps it.
    for (const CarriedSquad& c : m.setup().roster)
        if (c.type == UnitType::DwarfGrenadier) CHECK(c.men[0].satchels == 2 && c.men[0].ammo == 1);
    Battle b(3, m.scenario(), m.setup());
    int seen = 0;
    for (const illyria::sim::Unit& u : b.world().units()) {
        if (u.owner != Battle::kPlayer) continue;
        if (static_cast<UnitType>(u.stats.type) == UnitType::DwarfGrenadier) seen += u.satchels == 2 && u.ammo == 1;
        if (static_cast<UnitType>(u.stats.type) == UnitType::Wizards) seen += u.ammo == 1;
    }
    CHECK(seen == 2);  // (the one dwarf who had spent, and the wizard; his fellow has his full load)
    for (const CarriedSquad& c : b.carried())
        if (c.type == UnitType::DwarfGrenadier) CHECK(c.men[0].satchels == 2 && c.men[0].ammo == 1);
    March again;
    CHECK(March::load(m.save(), again) && again.save() == m.save());
    CHECK(find(again, "Dwarven Grenadiers")->men[0].satchels == 2 && find(again, "Dwarven Grenadiers")->men[0].ammo == 1);
    // A line written before loads were kept (four numbers a man) reads as a full load.
    March old;
    CHECK(March::load(fresh().save(), old) && old.squads[0].men[0].satchels == -1);
    // The camp: 3 of the pair's 12 spent is 3/24 of their price, with the men (they cannot be replaced) costing nothing.
    const int cost = m.resupplyCost(dwarf);
    std::printf("  the dwarf's load made up for %d gold of a purse of %d (his price %d)\n", cost, m.purse, Conquest::price(UnitType::DwarfGrenadier));
    CHECK(cost == (Conquest::price(UnitType::DwarfGrenadier) * 3 + 23) / 24 && m.reinforceCost(dwarf) == cost);
    m.purse = 30;
    const int purse = m.purse;
    CHECK(m.reinforce(dwarf) > 0 && m.purse == purse - cost);
    CHECK(m.squads[dwarf].men[0].satchels == -1 && m.squads[dwarf].men[0].ammo == -1 && m.resupplyCost(dwarf) == 0);
    // An empty purse makes up nothing.
    m.purse = 0;
    const size_t wizard = static_cast<size_t>(find(m, "Magus of the Tower") - m.squads.data());
    CHECK(m.resupplyCost(wizard) > 0 && m.reinforce(wizard) == 0 && m.squads[wizard].men[0].ammo == 1);
}
