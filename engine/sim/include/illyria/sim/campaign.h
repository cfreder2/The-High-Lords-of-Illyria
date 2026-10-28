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
    // Boats (campaign_sea.cpp), after Warlords' navies: built in ports, they
    // carry up to seven armies over the sea, lakes and wide rivers at their
    // own speed, whatever the armies aboard could walk.
    static constexpr int32_t kBoatCapacity = 7;  // armies aboard, as a Warlords navy
    static constexpr int32_t kSeaMoves = 40;     // a boat's movement points a day
    static constexpr int32_t kSeaCost = 2;       // per water cell (plain is 2 on foot): 20 cells, 4 km, a day
    static constexpr int32_t kBoatCost = 20, kBoatDays = 4;  // gold and days to build one
    static constexpr int32_t kBoatUpkeep = 2;    // gold a day

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
        bool port = false;          // on the coast: builds boats, which put out to its harbour cell
        int32_t hx = -1, hy = -1;   // the harbour: the nearest navigable water cell
        int32_t boatDays = 0;       // days left on a boat being built, or 0
    };
    struct Boat {
        uint32_t id = 0;
        uint8_t owner = 0;
        std::string name;
        int32_t cx = 0, cy = 0;  // a water cell; the port's own cell while docked
        int32_t port = -1;       // the city it lies in, or -1 at sea
        uint32_t stack = 0;      // the stack aboard, or 0 (empty: docked, or anchored off a coast)
    };
    struct Stack {
        uint32_t id = 0;
        uint8_t owner = 0;
        int32_t cx = 0, cy = 0;            // cell
        std::vector<Army> armies;
        std::vector<std::pair<int32_t, int32_t>> path;  // cells still to enter
        int32_t progress = 0;              // movement points spent towards the next cell, x kTicksPerDay
        bool garrison = false;             // a neutral city's own defenders: never moves
        uint32_t boat = 0;                 // the boat it sails in, or 0 on land
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

    Campaign(const Continent& continent, uint64_t seed);

    // Orders.
    bool move(uint32_t stackId, int32_t cx, int32_t cy);  // false if no route
    bool produce(int32_t city, int32_t option);           // -1 stops
    // Boats: lay one down in a port (kBoatCost gold, ready in kBoatDays);
    // board the stack (and friends in the port, up to kBoatCapacity) onto a
    // free boat there and put out to the harbour; or step ashore from the sea.
    bool buildBoat(int32_t city);
    bool embark(uint32_t stackId);
    bool land(uint32_t stackId);
    void setHuman(uint8_t side, bool human) { sides_[side].human = human; }
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
    const std::vector<Boat>& boats() const { return boats_; }
    const Boat* findBoat(uint32_t id) const;
    // A free boat of the city's owner docked there, or nullptr.
    const Boat* boatInPort(int32_t city) const;
    // Water a boat can sail: sea, lakes and wide rivers big enough to matter.
    bool navigable(int32_t cx, int32_t cy) const { return navigable_[static_cast<size_t>(cy) * nx_ + cx] != 0; }
    int32_t voyages() const { return voyages_; }  // times a stack has put to sea
    // For tests and set pieces: hand a city to a side and raise armies there.
    uint32_t grant(int32_t city, uint8_t side, UnitType type, int32_t armies);
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
    // Ports and boats (campaign_sea.cpp).
    void initSea();
    void seaDay();
    void runSeaAi(uint8_t side);
    bool planRoute(const Stack& s, int32_t tx, int32_t ty, std::vector<std::pair<int32_t, int32_t>>& out) const;
    // Quickest times from the stack to every cell (state = cell * 2 + afloat),
    // walking and, with sail, boarding own boats (assumePort: as if one lay there).
    void searchRoutes(const Stack& s, bool sail, int32_t assumePort, int32_t goal, std::vector<int32_t>& g, std::vector<int32_t>& parent) const;
    bool routeTo(const Stack& s, int32_t cell, const std::vector<int32_t>& g, const std::vector<int32_t>& parent,
                 std::vector<std::pair<int32_t, int32_t>>& out) const;
    int32_t routeTime(const Stack& s, const std::vector<std::pair<int32_t, int32_t>>& path) const;
    bool boardsBoat(const Stack& s, const std::vector<std::pair<int32_t, int32_t>>& path) const;
    bool boardIfSailing(size_t index);
    void syncBoats();
    void splitOff(size_t index, size_t keep);
    int32_t stepMoves(const Stack& s) const;
    int32_t stepCost(const Stack& s, int32_t cx, int32_t cy) const;
    int32_t afloatPenalty(const Stack& s) const { return s.boat ? 1 : 0; }
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
    Pending pending_;
    std::vector<uint8_t> navigable_;
    std::vector<Boat> boats_;
    std::vector<int32_t> boatsBuilt_;  // per side, for naming
    int32_t voyages_ = 0;
    Stack* stackById(uint32_t id);
    void capture(Stack& attacker, int32_t cx, int32_t cy, Fight& fight);
};

}  // namespace illyria::sim
