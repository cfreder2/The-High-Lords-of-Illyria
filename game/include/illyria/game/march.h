#pragma once

// The March (docs/GAME_DESIGN.md §3.7): five skirmishes fought one after another by one army. The army is bought
// once. After each battle the men left alive go on to the next with their wounds half mended, their kills and their
// rank; a squad with no man left is gone for good; a small purse fills gaps in the squads that can be filled. The
// lord, the wizards and the dwarves cannot be replaced. Lose a battle and the march is over.
//
// Each battle is an ordinary skirmish (Battle, SkirmishSetup): the March only says which map, which side of the walls,
// how much gold the other side raises, and who stands in the player's army (SkirmishSetup::roster). Nothing here steps
// a world or reads a clock; it is the book kept between battles, saved as one line of text.

#include <string>
#include <vector>

#include "illyria/game/battle.h"

namespace illyria::game {

struct March {
    static constexpr int kBattles = 5;
    static constexpr int kPursePct = 14;      // gold won with each battle, as a share of what the army cost
    static constexpr int kHealShare = 2;      // a wound mends by one part in this many between battles
    static constexpr int kSeasonedBattles = 2, kVeteranBattles = 4;  // battles lived through that make a regular, a veteran
    struct Stage {
        const char* name;
        Scenario scenario;
        bool defend;
        int foePct;        // the other side's gold, as a share of what the player's army cost
        int siegeSeconds;  // a siege the player lays: the time he has to take the town (0: none)
    };
    static const Stage& stageInfo(int stage);

    struct Man {
        int hp = 1000;  // thousandths of his full health
        int rank = 0, kills = 0, battles = 0;
        int satchels = -1, ammo = -1;  // what he still carries (-1: his full load); made up in camp for gold (resupply)
    };
    struct Squad {
        std::string name;
        sim::UnitType type = sim::UnitType::LightInfantry;
        int full = 0;
        std::vector<Man> men;       // the living
        int kills = 0, battles = 0;  // all the squad has killed (its dead men's kills too), and the battles it has fought
    };
    // What the last battle did, for the results sheet and the camp.
    struct Report {
        int survivors = 0, lost = 0, promoted = 0, gold = 0;
        std::vector<std::string> promotions;  // "Archers 1: 3 promoted"
        std::vector<std::string> gone;        // the squads wiped out in it
    };

    int budget = 0;  // what the army cost: the other side's hosts are measured against it
    bool easy = false;
    int stage = 0;   // the battle to fight next (0-4); kBattles when all are won
    int purse = 0;
    bool lost = false;
    std::vector<Squad> squads;      // those with a man left, in the order bought
    std::vector<std::string> gone;  // the squads wiped out, with where: "Pikemen 2 (the Ford)"
    Report last;

    // A new march: the army bought, with its lord and a company of wizards.
    static March begin(const std::vector<Buy>& army, bool easy);
    bool over() const { return lost || stage >= kBattles; }
    bool won() const { return !lost && stage >= kBattles; }
    int men() const;
    // The next battle, as a skirmish.
    Scenario scenario() const { return stageInfo(stage).scenario; }
    SkirmishSetup setup() const;
    // A battle fought (1 won, 2 lost) and what is left of each squad (Battle::carried; matched by the squads' names).
    void settle(int outcome, const std::vector<CarriedSquad>& after);
    // Never replaced: the lord, the wizards, the dwarves and every rare kind.
    static bool irreplaceable(sim::UnitType type);
    // Gold to bring a squad back to its full strength (0: it is full, or cannot be filled), and doing it: as many men
    // as the purse pays for, recruits of no rank. How many joined.
    // The cost is for both: the men missing (never for the irreplaceable) and the squad's spent satchels and missiles.
    int reinforceCost(size_t squad) const;
    int reinforce(size_t squad);
    // The camp's powder and oil: gold to make up what a squad has laid and thrown (half its price for a whole load, in
    // proportion), and doing it (all or nothing). What it carries, and what a full load is, for the camp's list.
    int resupplyCost(size_t squad) const;
    bool resupply(size_t squad);
    void load(size_t squad, int& carried, int& full) const;
    // The purse spent as far as it goes: the squad with the smallest share of its men left first, again and again.
    int reinforceAll();

    // One line of text, with no quote in it; load is false for anything it did not write.
    std::string save() const;
    static bool load(const std::string& text, March& out);
};

}  // namespace illyria::game
