#pragma once

// The campaign: the war for all of Illyria, after Warlords (docs/GAME_DESIGN.md
// §4). Eight sides each start with a capital, a hero and some gold; the other
// 72 cities are neutral, held by small garrisons. Cities pay gold every day
// and train armies; armies cost upkeep. Stacks of up to eight armies march
// across the continent on a 200 m grid, paying Warlords' movement costs, and
// fight Warlords' dice battles where they meet (or the full 3D battle, when the
// player chooses to). Deterministic: integers and the seeded Rng only.

#include <cstdint>
#include <string>
#include <vector>

#include "illyria/sim/continent.h"
#include "illyria/sim/rng.h"
#include "illyria/sim/units.h"

namespace illyria::sim {

// One Warlords "army": a squad of soldiers of one type that fights as one.
struct Army {
    uint32_t id = 0;
    UnitType type = UnitType::LightInfantry;
    std::string name;
    int32_t soldiers = 1, fullStrength = 1;  // men alive, and the squad's full size
    int32_t kills = 0;                       // enemy soldiers it has slain
    int32_t rank = 0;                        // 0-3; heroes: level - 1 (0-8)
    int32_t blessings = 0;
    int32_t hits = 0;                        // wounds in the current fight (two destroy it)
    uint32_t bornDay = 0;
    bool hero() const { return type == UnitType::Hero; }
};

// The lay of a campaign cell, as in the Warlords movement table.
enum class Lay : uint8_t { Road, Bridge, Water, Forest, Hill, Mountain, Plain, Marsh, City };

// How well the computer plays a side (docs/GAME_DESIGN.md §4.2.5). Easy is
// Warlords' Knight: it wastes money, forgets orders, reacts late and attacks
// at poor odds. Hard is the Warlord: full strategy, and no secret bonuses
// (the Warlords manual is firm that the computer plays by the same rules).
enum class Difficulty : uint8_t { Easy, Hard };

class Campaign {
public:
    static constexpr int32_t kCell = 200;                 // metres
    static constexpr int32_t kTicksPerDay = 300;          // 30 s a day at 10 ticks a second
    static constexpr int32_t kTicksPerSecond = 10;
    static constexpr int32_t kMaxStack = 8;
    static constexpr int32_t kMaxInCity = 32;  // a Warlords city is four squares of eight
    static constexpr uint8_t kNeutral = Continent::kNeutral;
    static constexpr int32_t kProhibited = 1000;

    struct Side {
        std::string name;
        int32_t gold = 0;
        bool alive = true;
        bool human = false;  // the computer plays the others (Campaign::runAi)
        Difficulty difficulty = Difficulty::Hard;
        int32_t deserted = 0;  // armies lost for want of pay
    };
    struct Option {  // something a city can train
        UnitType type;
        int32_t days, cost;
    };
    struct CityState {
        uint8_t owner = kNeutral;
        int32_t defense = 4;  // 3 to 6, as in Warlords (capitals 6)
        int32_t income = 20;  // gold a day
        std::vector<Option> options;
        int32_t producing = -1;  // index into options, or -1
        int32_t progress = 0;    // ticks trained so far
        int32_t cx = 0, cy = 0;  // its cell
        bool razed = false;
        int32_t vectorTo = -1;   // Warlords' LOC: new armies march to this city instead (two days more)
    };
    struct Stack {
        uint32_t id = 0;
        uint8_t owner = 0;
        int32_t cx = 0, cy = 0;            // cell
        std::vector<Army> armies;
        std::vector<std::pair<int32_t, int32_t>> path;  // cells still to enter
        int32_t progress = 0;              // movement points spent towards the next cell, x kTicksPerDay
        bool garrison = false;             // a neutral city's own defenders: never moves
    };
    // What happened in one fight, for the reports.
    struct Fight {
        uint8_t attacker = 0, defender = 0;
        int32_t city = -1;      // the city fought over, or -1
        bool attackerWon = false;
        bool captured = false;  // the city changed hands
        int32_t attackerLost = 0, defenderLost = 0;  // armies destroyed
        uint32_t day = 0;
    };

    // A human's stack has marched into the enemy: the campaign waits while the
    // player fights the battle in 3D, or has it resolved with the dice.
    struct Pending {
        uint32_t stack = 0;       // the attacking stack
        int32_t cx = 0, cy = 0;   // the cell attacked
    };
    // How an army came out of a 3D battle.
    struct Outcome {
        uint32_t army = 0;
        int32_t soldiers = 0;  // still standing
        int32_t kills = 0;     // slain in this battle
        int32_t rank = 0;      // the survivors' rank (heroes: level - 1)
    };

    // An army trained for another city (LOC), on its way there.
    struct Transit {
        uint8_t owner = 0;
        int32_t from = 0, to = 0;  // cities
        uint32_t arriveDay = 0;
        Army army;
    };

    Campaign(const Continent& continent, uint64_t seed);

    // Orders.
    bool move(uint32_t stackId, int32_t cx, int32_t cy);  // false if no route
    bool produce(int32_t city, int32_t option);           // -1 stops
    // Warlords' LOC: send what a city trains to another city of ours (-1
    // stops); at most four cities may send to one.
    bool vectorProduction(int32_t city, int32_t to);
    // Warlords' BUILD: raise a city's defence by one, to 9, for gold.
    bool fortify(int32_t city);
    int32_t fortifyCost(int32_t city) const;
    // Warlords' DISBAND: send an army home to stop paying for it (never a hero).
    bool disband(uint32_t stackId, uint32_t armyId);
    void setHuman(uint8_t side, bool human) { sides_[side].human = human; }
    void setDifficulty(uint8_t side, Difficulty d) { sides_[side].difficulty = d; }
    // Every computer side at once (what the player picks at the start).
    void setComputerDifficulty(Difficulty d);
    // Scenario and test setup: a new stack of fresh armies. Returns its id.
    uint32_t raise(uint8_t owner, int32_t cx, int32_t cy, const std::vector<UnitType>& types);
    // Advances one tick (a tenth of a second at normal speed). Nothing moves
    // while a battle is pending.
    void step();
    const Pending* pending() const { return pending_.stack ? &pending_ : nullptr; }
    // The armies that will fight the pending battle.
    std::vector<const Army*> pendingAttackers() const;
    std::vector<const Army*> pendingDefenders() const;
    // Settles the pending battle with the dice, or with the outcome of the 3D battle.
    void resolvePending();
    void applyBattle(const std::vector<Outcome>& outcomes, bool attackerWon);

    // Queries.
    const Continent& continent() const { return continent_; }
    uint32_t tick() const { return tick_; }
    uint32_t day() const { return tick_ / kTicksPerDay; }
    const std::vector<Side>& sides() const { return sides_; }
    const std::vector<CityState>& cities() const { return cities_; }
    const std::vector<Stack>& stacks() const { return stacks_; }
    const std::vector<Fight>& fights() const { return fights_; }
    const Stack* findStack(uint32_t id) const;
    int32_t width() const { return nx_; }
    int32_t height() const { return ny_; }
    Lay terrainAt(int32_t cx, int32_t cy) const { return terrain_[static_cast<size_t>(cy) * nx_ + cx]; }
    // Cell centre in continent metres.
    static int32_t centre(int32_t c) { return c * kCell + kCell / 2; }
    static int32_t cellOf(int32_t metres) { return metres / kCell; }
    // Strength on the Warlords scale (1-9) of an army, before any battle modifiers.
    static int32_t strength(const Army& a);
    // Movement points a day, and the cost of a terrain, for a type.
    static int32_t movesPerDay(UnitType t);
    static int32_t moveCost(UnitType t, Lay terrain);
    // Gold a day an army costs to keep: half its training cost, as in Warlords.
    static int32_t upkeep(UnitType t);
    // How many soldiers a fresh army of this type has.
    static int32_t squadSize(UnitType t);
    int32_t cityAt(int32_t cx, int32_t cy) const;
    int32_t citiesOwned(uint8_t side) const;
    // Gold a side's cities pay it a day (an Easy computer side wastes a fifth).
    int32_t income(uint8_t side) const;
    // Gold a day its armies cost.
    int32_t upkeepOf(uint8_t side) const;
    // 0 while the war goes on, else the winning side + 1: the last side
    // standing, or the one the others surrender to (Warlords §14: more than
    // half the cities, and no rival within 15 of it).
    int32_t winner() const;
    const std::vector<Transit>& transits() const { return transits_; }
    // Warlords' combat, for prediction: the attack strength (AS) or defence
    // strength (DS) of each army at a cell, in the order they fight (weakest
    // first, heroes last), with every modifier of the manual's appendix.
    std::vector<int32_t> lineup(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending) const;
    // The chance, in thousandths, that the first lineup destroys the second
    // (exact, from the Warlords dice rules).
    static int32_t winChance(const std::vector<int32_t>& attack, const std::vector<int32_t>& defence);
    // The chance, in thousandths, that these armies of a side take the cell.
    int32_t attackOdds(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy) const;
    // Everyone not of this side standing on a cell.
    std::vector<const Army*> defendersAt(uint8_t attacker, int32_t cx, int32_t cy) const;
    static bool flies(UnitType t);
    static bool special(UnitType t);
    uint64_t checksum() const;

private:
    void initTerrain();
    void initCities();
    void newDay();
    void runAi(uint8_t side);  // campaign_ai.cpp
    friend class CampaignAi;
    void deliverTransits();
    int32_t armiesAt(uint8_t owner, int32_t cx, int32_t cy) const;
    int32_t cellCapacity(int32_t cx, int32_t cy) const;
    void moveStacks();
    void arrive(Stack& s);
    void battle(Stack& attacker, int32_t cx, int32_t cy);
    // Sorts armies into battle order and gives each its AS or DS (campaign_rules.cpp).
    void order(std::vector<Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, std::vector<int32_t>& power) const;
    int32_t forceModifier(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending) const;
    // Movement points from every cell to a city, for a walking army (the
    // computer lords' sense of distance; derived from the map, so not state).
    const std::vector<uint16_t>& marchField(int32_t city) const;
    mutable std::vector<std::vector<uint16_t>> marchFields_;
    int32_t stackCost(const Stack& s, int32_t cx, int32_t cy) const;
    int32_t stackMoves(const Stack& s) const;
    bool edgeOpen(int32_t ax, int32_t ay, int32_t bx, int32_t by) const;
    bool findPath(const Stack& s, int32_t tx, int32_t ty, std::vector<std::pair<int32_t, int32_t>>& out) const;
    Army makeArmy(UnitType type, const std::string& name);
    Stack& newStack(uint8_t owner, int32_t cx, int32_t cy);
    void promote(Army& a);

    const Continent& continent_;
    Rng rng_;
    uint32_t tick_ = 0;
    uint32_t nextId_ = 1;
    int32_t nx_ = 0, ny_ = 0;
    std::vector<Lay> terrain_;
    std::vector<uint8_t> riverEast_, riverNorth_;  // a river runs between this cell and its east / north neighbour
    std::vector<Side> sides_;
    std::vector<CityState> cities_;
    std::vector<Stack> stacks_;
    std::vector<Fight> fights_;
    std::vector<int32_t> armiesRaised_;  // per side, for naming
    Pending pending_;
    std::vector<Transit> transits_;
    Rng aiRng_;  // the computer lords' own choices, apart from the battle dice
    Stack* stackById(uint32_t id);
    void capture(Stack& attacker, int32_t cx, int32_t cy, Fight& fight);
};

}  // namespace illyria::sim
