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

    Campaign(const Continent& continent, uint64_t seed);

    // Orders.
    bool move(uint32_t stackId, int32_t cx, int32_t cy);  // false if no route
    bool produce(int32_t city, int32_t option);           // -1 stops
    void setHuman(uint8_t side, bool human) { sides_[side].human = human; }
    // Advances one tick (a tenth of a second at normal speed).
    void step();

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
};

}  // namespace illyria::sim
