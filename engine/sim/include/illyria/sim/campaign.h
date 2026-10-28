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
    bool ally = false;               // joined a hero from a ruin: fights for no pay
    uint8_t temples = 0;             // the temples that have blessed it, a bit each
    std::vector<uint8_t> artifacts;  // heroes: what they carry (Campaign::artifacts())
    bool hero() const { return type == UnitType::Hero; }
};

// The lay of a campaign cell, as in the Warlords movement table.
enum class Lay : uint8_t { Road, Bridge, Water, Forest, Hill, Mountain, Plain, Marsh, City };

class StateHasher;

class Campaign {
public:
    static constexpr int32_t kCell = 200;                 // metres
    static constexpr int32_t kTicksPerDay = 300;          // 30 s a day at 10 ticks a second
    static constexpr int32_t kTicksPerSecond = 10;
    static constexpr int32_t kMaxStack = 8;
    static constexpr uint8_t kNeutral = Continent::kNeutral;
    static constexpr int32_t kProhibited = 1000;

    struct Side {
        std::string name;
        int32_t gold = 0;
        bool alive = true;
        bool human = false;  // the computer plays the others (Campaign::runAi)
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

    // Heroes, ruins, temples and sages, after Warlords (campaign_quests.cpp).
    // Battle artifacts add to the hero's strength, command artifacts to every
    // army fighting beside the hero; some speed the stack or pay gold daily.
    struct Artifact {
        const char* name;
        int32_t battle, command, moves, gold;
    };
    static const std::vector<Artifact>& artifacts();
    struct SiteState {
        Continent::Site::Kind kind = Continent::Site::Kind::Ruin;
        int32_t cx = 0, cy = 0;
        bool searched = false;  // ruins: the guardian slain and the treasure taken; sages: already consulted
        int32_t guardian = -1;  // ruins: which monster waits inside (guardianName, guardianStrength)
        uint8_t bit = 0;        // temples: their bit in Army::temples
    };
    struct Loot {  // an artifact lying where its hero fell
        int32_t cx = 0, cy = 0;
        uint8_t artifact = 0;
    };
    struct Deed {  // a line for the news: what a side's heroes did
        uint32_t day = 0;
        uint8_t side = 0;
        std::string text;
    };
    struct Offer {  // a hero offering their sword, until the next dawn
        bool open = false;
        int32_t city = -1, price = 0;
        std::string name;
    };
    static const char* guardianName(int32_t guardian);
    static int32_t guardianStrength(int32_t guardian);
    // The site at a cell, or -1; sites() follows Continent::sites().
    int32_t siteAt(int32_t cx, int32_t cy) const;
    const std::vector<SiteState>& sites() const { return sites_; }
    const std::vector<Loot>& loot() const { return loot_; }
    const std::vector<Deed>& deeds() const { return deeds_; }
    const Offer& offer(uint8_t side) const { return offers_[side]; }
    // Whether the stack could search where it stands now (a hero at a ruin or
    // sage, anyone at a temple that has not yet blessed them all).
    bool canSearch(uint32_t stackId) const;
    // Command bonus an army brings its side in battle (heroes' command artifacts).
    static int32_t command(const Army& a);

    Campaign(const Continent& continent, uint64_t seed);

    // Orders.
    bool move(uint32_t stackId, int32_t cx, int32_t cy);  // false if no route
    bool produce(int32_t city, int32_t option);           // -1 stops
    void setHuman(uint8_t side, bool human) { sides_[side].human = human; }
    // A hero searches the ruin or sage the stack stands on, or the whole stack
    // is blessed at a temple. False if there is nothing to search.
    bool search(uint32_t stackId);
    // Take (or turn down) the hero offering their service to this side.
    bool hire(uint8_t side, bool accept);
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
    static int32_t armyUpkeep(const Army& a) { return a.ally ? 0 : upkeep(a.type); }  // allies serve for nothing
    // How many soldiers a fresh army of this type has.
    static int32_t squadSize(UnitType t);
    int32_t cityAt(int32_t cx, int32_t cy) const;
    int32_t citiesOwned(uint8_t side) const;
    // 0 while the war goes on, else the winning side + 1 (holding two thirds of the cities).
    int32_t winner() const;
    uint64_t checksum() const;

private:
    void initTerrain();
    void initCities();
    void newDay();
    void runAi(uint8_t side);
    void moveStacks();
    void arrive(Stack& s);
    void battle(Stack& attacker, int32_t cx, int32_t cy);
    int32_t stackCost(const Stack& s, int32_t cx, int32_t cy) const;
    int32_t stackMoves(const Stack& s) const;
    bool edgeOpen(int32_t ax, int32_t ay, int32_t bx, int32_t by) const;
    bool findPath(const Stack& s, int32_t tx, int32_t ty, std::vector<std::pair<int32_t, int32_t>>& out) const;
    Army makeArmy(UnitType type, const std::string& name);
    Stack& newStack(uint8_t owner, int32_t cx, int32_t cy);
    void promote(Army& a);
    // campaign_quests.cpp
    void initSites();
    void questsNewDay();                            // artifact gold and heroes for hire
    void questAi(uint8_t side);                     // the computer's heroes go exploring
    void bury(Stack& s, int32_t cx, int32_t cy);    // the fallen leave; heroes drop what they carry
    void pickUp(Stack& s);                          // a hero takes up artifacts lying here
    void hashQuests(StateHasher& h) const;
    void deed(uint8_t side, std::string text);

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
    std::vector<SiteState> sites_;
    std::vector<Loot> loot_;
    std::vector<Deed> deeds_;
    std::vector<Offer> offers_;           // per side
    std::vector<uint8_t> artifactFound_;  // per artifact: found by someone (each exists once)
    int32_t heroesHired_ = 0;             // for naming
    Stack* stackById(uint32_t id);
    void capture(Stack& attacker, int32_t cx, int32_t cy, Fight& fight);
};

}  // namespace illyria::sim
