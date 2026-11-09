#pragma once

// The campaign: the war for all of Illyria, after Warlords (docs/GAME_DESIGN.md
// §4). Eight sides each start with a capital, a hero and some gold; the other
// 72 cities are neutral, held by small garrisons. Cities pay gold every day
// and train armies; armies cost upkeep. A stack is a group of squads that
// march together across the continent on a grid of kCell cells at the
// battle's own speeds, on the battle's 30 Hz clock (docs/ONE_WORLD.md). Every
// squad is its real men, each with a place on the ground, and where armies
// meet they fight there as in any battle, with the battle's own rules
// (campaign_world.cpp): there are no dice and no battle windows.
// Deterministic: integers, fixed point and the seeded Rng only.

#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "illyria/sim/continent.h"
#include "illyria/sim/edges.h"
#include "illyria/sim/rng.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

// One soldier of a squad (campaign_world.cpp). Every man on the land is a unit of the campaign's one sim::World,
// stepped with the battle's own rules whether he marches, rests or fights: there is no other way he moves. Only a man
// aboard a boat is out of it (the ship carries him); he is put ashore as a unit where it lands.
struct Man {
    uint32_t id = 0;       // for ever his own
    int32_t hp = -1;       // -1: whole; 0: fallen
    int32_t kills = 0;
    int32_t morale = 1000;
    int32_t rank = 0;
    uint32_t unit = 0;     // his unit in the world, or 0 (aboard a boat, or not yet put in the world)
    // Where he stands, in millimetres on the continent, and the way he faces (thousandths of a unit vector): what a
    // save keeps of him, and what the drawing glides from (bx, by: where he stood a tick ago). `placed`: ax, ay say
    // where to put him when he is next put in the world (loaded, or put ashore), else he takes his slot at his stack.
    bool placed = false;
    int32_t ax = 0, ay = 0, bx = 0, by = 0, fx = 0, fy = -1000;
    int32_t lastCool = 0, lastReload = 0;
    uint32_t struckTick = 0, hurtTick = 0, fightTick = 0;  // when he last struck, was hurt, or fought at all (presentation)
    bool alive() const { return hp != 0; }
};

// One Warlords "army": a squad of soldiers of one type that fights as one.
struct Army {
    uint32_t id = 0;
    UnitType type = UnitType::LightInfantry;
    std::string name;
    int32_t soldiers = 1, fullStrength = 1;  // men alive, and the squad's full size
    int32_t kills = 0;                       // enemy soldiers it has slain
    int32_t rank = 0;                        // 0-3; heroes: level - 1 (0-8)
    int32_t blessings = 0;
    std::vector<Man> men;                    // every soldier it was raised with, the fallen among them
    bool seated = false;                     // its men are in the world (false: new, or aboard a boat)
    uint32_t bornDay = 0;
    bool ally = false;               // joined a hero from a ruin: fights for no pay
    uint8_t temples = 0;             // the temples that have blessed it, a bit each
    std::vector<uint8_t> artifacts;  // heroes: what they carry (Campaign::artifacts())
    bool hero() const { return type == UnitType::Hero; }
};

// The lay of a campaign cell, as in the Warlords movement table.
enum class Lay : uint8_t { Road, Bridge, Water, Forest, Hill, Mountain, Plain, Marsh, City };

// How well the computer plays a side (docs/GAME_DESIGN.md §4.2.5). Easy is
// Warlords' Knight: it wastes money, forgets orders, reacts late and attacks
// at poor odds. Hard is the Warlord: full strategy, and no secret bonuses
// (the Warlords manual is firm that the computer plays by the same rules).
enum class Difficulty : uint8_t { Easy, Hard };
class StateHasher;

class Campaign {
public:
    // Metres. The grid is 64 x 80 cells, as it was before the map shrank to two
    // thirds and then to a third each way. 64 x 44 m and 80 x 44 m fall a
    // cell short of the map's edges, which are open sea. Nothing may assume
    // the size: marches are timed from it.
    static constexpr int32_t kCell = 44;
    // One clock with the battle (docs/ONE_WORLD.md): 30 ticks a second, and a
    // day of two minutes at normal speed. Days stay the unit of the economy
    // (gold, upkeep, training, boats), with the Warlords tables' numbers.
    static constexpr int32_t kTicksPerSecond = illyria::sim::kTicksPerSecond;
    static constexpr int32_t kTicksPerDay = 120 * kTicksPerSecond;
    // Travel times (stepTime, Stack::progress) are in hundredths of a tick.
    static constexpr int32_t kTimeScale = 100;
    static constexpr int32_t kClosed = INT32_MAX;  // a travel time: no way through
    static constexpr int32_t kMaxStack = 8;  // squads in a group that march together; how many stand in one place is not limited
    static constexpr uint8_t kNeutral = Continent::kNeutral;
    // Boats (campaign_sea.cpp), after Warlords' navies: built in ports, they
    // carry up to seven armies over the sea, lakes and wide rivers at their
    // own speed, whatever the armies aboard could walk.
    static constexpr int32_t kBoatCapacity = 7;  // armies aboard, as a Warlords navy
    static constexpr int32_t kSeaSpeed = 6000;   // mm a second (6 m/s, about 12 knots): quicker than any walker but horse on a road
    static constexpr int32_t kBoatCost = 20, kBoatDays = 4;  // gold and days to build one
    static constexpr int32_t kBoatUpkeep = 2;    // gold a day
    // Raising a banner (campaign_capture.cpp): a city changes hands only when
    // the victors' banner has risen over it, while they stand in it unopposed.
    // Each tick it climbs kBannerBase + one point an army there (eight at
    // most): a lone army takes 0.53 of a day (about 13 hours of campaign time,
    // 64 s at normal speed), a full stack 0.16 (about 4 hours). Enemies in or
    // beside the city stop it, and pull it down when they outnumber the raisers.
    static constexpr int32_t kBannerFull = kTicksPerDay * 8 / 5;
    static constexpr int32_t kBannerBase = 2;
    // Territory (campaign_territory.cpp): the map's campaign hexes, 89 m
    // between centres (pointy-top; odd rows shifted half a hex east), each
    // claimed by the side whose armies stand in it uncontested.
    static constexpr int32_t kHexSpacing = 89;   // metres between neighbouring centres
    static constexpr int32_t kHexRowX100 = 7708; // rows apart, in centimetres (89 * sqrt(3) / 2)
    static constexpr int32_t kHexCols = 33, kHexRows = 47;
    static constexpr int32_t kClaimFull = 100;
    static constexpr int32_t kTerritoryTicks = kTicksPerDay / 10;  // the land is reckoned ten times a day

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
        int32_t left = -1;  // a levy of rare troops: armies still to raise, once each (-1: common, for ever)
    };
    struct CityState {
        uint8_t owner = kNeutral;
        int32_t defense = 4;  // 3 to 6, as in Warlords (capitals 6)
        int32_t income = 20;  // gold a day
        std::vector<Option> options;
        int32_t producing = -1;  // index into options, or -1
        int32_t progress = 0;    // days trained so far
        int32_t cx = 0, cy = 0;  // its cell
        bool razed = false;
        int32_t vectorTo = -1;   // Warlords' LOC: new armies march to this city instead (two days more)
        bool port = false;          // on the coast: builds boats, which put out to its harbour cell
        int32_t hx = -1, hy = -1;   // the harbour: the nearest navigable water cell
        int32_t boatDays = 0;       // days left on a boat being built, or 0
        uint8_t banner = kNeutral;  // the side raising its banner here (its armies in the city), or kNeutral
        int32_t bannerProgress = 0; // to kBannerFull
    };
    // A campaign hex: who holds it and how firmly (0 to kClaimFull).
    struct Hex {
        uint8_t owner = kNeutral;
        int32_t claim = 0;
        bool land = false;     // its centre is not open water (only land is claimed)
        int32_t outpost = -1;  // the outpost tower in it (outposts()), or -1
    };
    // An outpost tower with a flag, after Warlords' towers: at the river
    // crossings, a stronger claim on the land around it.
    struct Outpost {
        int32_t hex = 0, cx = 0, cy = 0;
        std::string name;
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
        int32_t progress = 0;              // time on the way to the next cell (kTimeScale a tick); there at legTime()
        bool garrison = false;             // a neutral city's own defenders: never moves
        uint32_t boat = 0;                 // the boat it sails in, or 0 on land
        // Where the middle of its men is, in millimetres on the continent (campaign_world.cpp), and the way it faces
        // (a unit vector in thousandths; south until it first moves). Afloat, the boat carries it: its cell says where.
        int32_t px = 0, py = 0;
        int32_t hx = 0, hy = -1000;
        int32_t orderCell = -1;  // the cell its men were last sent marching to (-1: none)
        // Bound somewhere and held up (a foe it cannot get at, behind a wall): where it was and when it last got
        // anywhere, and how long it goes deaf to the enemy to pull itself free.
        int32_t lx = 0, ly = 0;
        uint32_t lt = 0, forceUntil = 0;
        // Its men were given orders in the world (an Order::Command): they stand to those, as a skirmish's do, and the
        // stack's own steering and habits (steer, fightAi) leave them alone until it is marched again.
        bool commanded = false;
    };
    // A city that changed hands, for the reports.
    struct Fight {
        uint8_t attacker = 0, defender = 0;
        int32_t city = -1;
        uint32_t day = 0;
    };

    // An army trained for another city (LOC), on its way there.
    struct Transit {
        uint8_t owner = 0;
        int32_t from = 0, to = 0;  // cities
        uint32_t arriveDay = 0;
        Army army;
    };
    // Heroes, ruins, temples and sages, after Warlords (campaign_quests.cpp).
    // Battle artifacts add to the hero's strength, command artifacts to every
    // army fighting beside the hero; some speed the stack (percent quicker:
    // the best of its heroes' counts) or pay gold daily.
    struct Artifact {
        const char* name;
        int32_t battle, command, speed, gold;
    };
    static const std::vector<Artifact>& artifacts();
    struct SiteState {
        Continent::Site::Kind kind = Continent::Site::Kind::Ruin;
        int32_t cx = 0, cy = 0;
        bool searched = false;  // ruins: the guardian slain and the treasure taken; sages: already consulted
        int32_t guardian = -1;  // ruins: which monster waits inside (guardianName, guardianStrength)
        uint8_t bit = 0;        // temples: their bit in Army::temples
        bool gifted = false;    // temples: their guard has gone with the first hero to kneel there
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
        UnitType companion = UnitType::Count;  // rare troops who ride with the hero, if any
        int32_t companions = 0;
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

    // Who plays: the faction the player starts as (the one whose hero and capital are seat 0, as ever: the
    // player is always side 0 and blue), and which of the other factions are computer lords (a bit a faction,
    // by the continent's order). A faction in neither is not in the game at all: its capital and towns are
    // plain neutral places with neutral garrisons, and no lord, army or AI. The default is as it always was:
    // the first faction against all the others. Part of the saved game.
    struct Setup {
        uint8_t player = 0;
        uint8_t enemies = 0xFF;
        bool operator==(const Setup& o) const { return player == o.player && (enemies & ~(1u << player)) == (o.enemies & ~(1u << o.player)); }
    };
    Campaign(const Continent& continent, uint64_t seed) : Campaign(continent, seed, Setup{0, 0xFF}) {}
    Campaign(const Continent& continent, uint64_t seed, Setup setup);
    const Setup& setup() const { return setup_; }
    // The faction (the continent's order, for names and what it trains) a side plays: sides 0 and `player` swap places.
    uint8_t factionOf(uint8_t side) const { return side == 0 ? setup_.player : side == setup_.player ? 0 : side; }
    // Whether a faction is in the game (the player's, or a chosen computer lord's).
    bool inGame(uint8_t faction) const { return faction == setup_.player || (faction < 8 && (setup_.enemies >> faction & 1)); }

    // What a side asks of the game is an Order: a plain, ordered record stamped with the tick it takes effect at.
    // Players only ever submit Orders; they are applied inside step(), at the start of the step that makes their
    // tick, in the order they were submitted, so the same Orders over the same saved game give the same game on
    // every machine (the test in tests/test_campaign_save.cpp). The calls below are what an Order does.
    struct Order {
        enum class Kind : uint8_t { Move, Produce, VectorProduction, Disband, Embark, Land, Search, Hire, BuildBoat, Command };
        Kind kind = Kind::Move;
        uint8_t side = 0;
        uint32_t tick = 0;     // the tick it takes effect at; 0: the next
        uint32_t stack = 0;    // the stack, if it concerns one
        int32_t a = 0, b = 0;  // Move: the cell; Produce: city, option; VectorProduction: city, to; Disband: the army; BuildBoat: city; Hire: accept
        // Move: only these squads of the stack (empty: all of it); the rest stay, a stack of their own.
        std::vector<uint32_t> armies;
        // Command: an order to a man of the world (his unit), as a skirmish's orders are: the stack he is of is the player's
        // to command, his men leave their march and stand to the orders they are given (Stack::commanded).
        sim::Command command;
    };
    // Queues an Order for its tick (the next, if it names none). Nothing is applied until a step runs, paused or not.
    void submit(Order order);

    void setHuman(uint8_t side, bool human);  // (the gates of its towns are kept by a garrison, or by his hand)
    void setDifficulty(uint8_t side, Difficulty d) { sides_[side].difficulty = d; }
    // Every computer side at once (what the player picks at the start).
    void setComputerDifficulty(Difficulty d);
    // Scenario and test setup: a new stack of fresh armies. Returns its id.
    uint32_t raise(uint8_t owner, int32_t cx, int32_t cy, const std::vector<UnitType>& types);
    // Advances one tick (a thirtieth of a second at normal speed).
    void step();

    // The men on the land (campaign_world.cpp): every man of every stack that is not aboard a boat is a unit of the
    // campaign's one world, and the world steps them all, every tick, as it steps a skirmish. The stacks only think
    // (look where their men are, send them on to the next cell of a route, meet whoever comes near) every kThinkTicks.
    static constexpr int32_t kThinkTicks = 15;
    // Where a stack's body is, in millimetres on the continent: the middle of its men; afloat, its cell's centre and
    // how far along its leg the boat has got, `ahead` hundredths of a tick on.
    void bodyOf(const Stack& s, int32_t ahead, int32_t& x, int32_t& y) const;
    // A soldier for drawing, picking and the minimap, where he stands (metres
    // on the continent). `alive` false: a body lying where it fell, in a fight
    // still going on.
    struct Soldier {
        uint32_t id = 0, stack = 0, army = 0;
        float x = 0, y = 0, yaw = 0;
        float speed = 0;  // metres a second, as he walks
        uint8_t owner = 0;
        UnitType type = UnitType::LightInfantry;
        bool alive = true, aloft = false;
        uint32_t struckTick = 0, hurtTick = 0, fightTick = 0;  // when he last struck, was hurt, or fought at all
        const Unit* unit = nullptr;  // his man in the world (to draw him hurt, fallen, or with arrows in him), until the next step
        uint32_t unitId = 0;
    };
    // Every soldier, as he stands `ahead` hundredths of a tick after the last
    // step (the presentation glides on between ticks; nothing steps).
    void soldiers(int32_t ahead, std::vector<Soldier>& out) const;
    // The fight's world, for presentation: the page's field shows it as it shows any battle (Battle::borrow), and is
    // told round each step of it (before and after), and when a fight is cleared away. Not part of the game's state.
    World* presentedWorld() { return world_.get(); }
    // The stack whose man is this unit of the fight's world (0: none).
    uint32_t stackOfUnit(uint32_t unit) const;

    // The one seam of sight (fog of war, step 4 of docs/ONE_WORLD.md): may this side see a man of `owner` standing
    // at x, y (continent metres)? Everything a side is shown that is not its own goes through it: the men drawn,
    // picked and looked at, the minimap, the stacks listed, the fight the camera is taken to. Until step 4 builds
    // sight everything is seen; `setSight` replaces the answer (tests, and then the fog).
    bool canSee(uint8_t viewer, uint8_t owner, float x, float y) const { return owner == viewer || !sight_ || sight_(viewer, owner, x, y); }
    void setSight(std::function<bool(uint8_t viewer, uint8_t owner, float x, float y)> sight) { sight_ = std::move(sight); }
    // Whose eyes the page looks through: the first side a person plays.
    uint8_t localSide() const {
        for (size_t i = 0; i < sides_.size(); ++i)
            if (sides_[i].human) return static_cast<uint8_t>(i);
        return 0;
    }
    // Whether the viewer can see a town (as it can see a man of the town's holder standing in it), and so who it knows
    // holds it: its holder, or nobody it knows of. The colour of a town on the map and the minimap, the banner going up
    // over it and the news of its changing hands go through these (the land's colour and the outposts' are map knowledge,
    // seen always by decision until fog is built: ONE_WORLD.md step 4).
    bool seesCity(uint8_t viewer, int32_t city) const;
    uint8_t knownOwner(uint8_t viewer, int32_t city) const { return seesCity(viewer, city) ? cities_[static_cast<size_t>(city)].owner : kNeutral; }
    // Every man the viewer may see (soldiers() less the rest).
    void visibleSoldiers(uint8_t viewer, int32_t ahead, std::vector<Soldier>& out) const;
    void onWorldStep(std::function<void()> before, std::function<void()> after, std::function<void()> cleared) {
        beforeHook_ = std::move(before), stepHook_ = std::move(after), clearedHook_ = std::move(cleared);
    }
    int32_t menInWorld() const;  // how many men are units of the world
    // Makes the island's ground now rather than at the first fight (a few seconds, once a process).
    void prepareWorld();  // (and puts the men in it)
    const World* world() const { return world_.get(); }  // null until the first fight or prepareWorld
    // Everyone is taken out of the world and put back where he stands, with his wounds, kills and morale, and the
    // world cleared of what is in the air (what a save writes: nothing else of a moment is kept, so a game saved and
    // the game loaded from the save carry on as one).
    void reseatAll();

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
    // Marching at the battle's speeds (campaign_march.cpp): a stack goes at
    // the pace of its slowest army, the unit's ground speed times its gait's
    // multiplier for the ground it is crossing (gait.h), climbs slowing it as
    // in battle; a stack of fliers alone goes at the long-run pace of their
    // flight cycle over anything. Speeds in millimetres a second.
    static int32_t groundSpeed(UnitType t);
    static int32_t flightPace(UnitType t);  // 0 for walkers
    static bool airborne(const Stack& s);   // every army in it flies
    // The battle ground a walker crosses a cell as: a road (and a town a road
    // runs through) is road, a hill plains to be climbed, marsh swamp;
    // Ground::DeepWater for the water and mountains closed to walkers.
    Ground groundAt(int32_t cx, int32_t cy) const;
    // The level plains (in mm) worth of walking for this gait from one cell
    // centre to a neighbour's: half the way over each ground, the climb
    // (rise: metres up, negative down) by the battle's rule. kClosed if closed.
    static int32_t effort(Gait gait, Ground from, Ground to, int32_t rise, bool diagonal);
    // How long the stack takes from a cell to a neighbour, in kTimeScale-ths
    // of a tick (afloat, at the boat's speed); kClosed if it cannot.
    int32_t stepTime(const Stack& s, int32_t ax, int32_t ay, int32_t bx, int32_t by) const;
    // The step it is on (0 if it is not marching), and the time to the end of its route.
    int32_t legTime(const Stack& s) const;
    int32_t timeLeft(const Stack& s) const;
    // Whether the stack can stand on the cell at all (on its feet or wings).
    bool passable(const Stack& s, int32_t cx, int32_t cy) const;
    // Gold a day an army costs to keep: half its training cost, as in Warlords.
    static int32_t upkeep(UnitType t);
    static int32_t armyUpkeep(const Army& a) { return a.ally ? 0 : upkeep(a.type); }  // allies serve for nothing
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
    // A town's defenders lose a point of their walls' bonus for each of the
    // attackers' siege engines (engines: how many catapults attack).
    // In hundredths (100-900): the small edges (armyEdges) make strengths fractional.
    // opp: the cell the other side fights from, for the high ground.
    std::vector<int32_t> lineup(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, int32_t engines = 0,
                                int32_t oppX = -1, int32_t oppY = -1) const;
    static int32_t engines(const std::vector<const Army*>& armies);
    // The chance, in thousandths, that the first lineup destroys the second
    // (exact, from the Warlords dice rules).
    static int32_t winChance(const std::vector<int32_t>& attack, const std::vector<int32_t>& defence);
    // The chance, in thousandths, that these armies of a side take the cell.
    int32_t attackOdds(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy) const;
    // Everyone not of this side standing on a cell.
    std::vector<const Army*> defendersAt(uint8_t attacker, int32_t cx, int32_t cy) const;
    static bool flies(UnitType t);
    static bool special(UnitType t);
    // Common troops are trained in cities, slowly and dearly; rare ones
    // (heroes, beasts, monsters, allies, Myth's specialists) never are: each
    // that falls is gone for good (campaign_rare.cpp, docs/GAME_DESIGN.md §4.2.4).
    static bool trainable(UnitType t);
    static bool rare(UnitType t) { return !trainable(t) && !fieldOnly(t) && !retired(t); }
    // Types not in the campaign at all (no city trains them, no realm has
    // them): the hyena-folk, so far only in the sandbox and the battles laid out with them.
    static bool fieldOnly(UnitType t) { return t == UnitType::Hyena; }
    // "No companion" as saves and checksums write it: the number
    // UnitType::Count had when both were pinned (the hyena-folk have it now).
    // A companion is only ever a hireable rare type, never a hyena, so the
    // slot reads it back unambiguously.
    static constexpr uint64_t kNoCompanionCode = 27;
    // Where a rare type can be had: a bit each.
    enum Source : uint8_t { kFromStart = 1, kFromLevy = 2, kFromRuin = 4, kFromTemple = 8, kFromHire = 16 };
    static uint8_t sources(UnitType t);
    // The band of rare troops guarding a temple, who follow the first hero to kneel there.
    static UnitType templeGuard(uint8_t bit, int32_t& count);
    // Veterans (and better) and rare troops: what a lord cannot replace.
    static bool precious(const Army& a) { return a.hero() || rare(a.type) || a.rank >= 2; }
    // Territory: the hex of a cell, a hex's centre in continent metres, and
    // who holds the land a cell lies in (kNeutral: nobody).
    static int32_t hexOfMetres(int32_t x, int32_t y);
    static void hexCentre(int32_t hex, int32_t& x, int32_t& y);
    static void hexNeighbours(int32_t hex, std::vector<int32_t>& out);
    int32_t hexAt(int32_t cx, int32_t cy) const { return cellHex_[static_cast<size_t>(cy) * nx_ + cx]; }
    const std::vector<Hex>& hexes() const { return hexes_; }
    const std::vector<Outpost>& outposts() const { return outposts_; }
    uint8_t landOwner(int32_t cx, int32_t cy) const { return hexes_[static_cast<size_t>(hexAt(cx, cy))].owner; }
    int32_t hexesHeld(uint8_t side) const;
    // Gold a day a side's land pays: one for every kHexesPerGold hexes it holds.
    static constexpr int32_t kHexesPerGold = 5;
    // The small edges an army fights with here (edges.h); opp: the cell the
    // other side fights from, for the high ground (-1: unknown).
    Edges armyEdges(const Army& a, const std::vector<const Army*>& side, uint8_t owner, int32_t cx, int32_t cy, bool defending,
                    int32_t oppX = -1, int32_t oppY = -1) const;
    // The same for a whole stack: the best any of its armies has.
    Edges stackEdges(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, int32_t oppX = -1,
                     int32_t oppY = -1) const;
    // Metres above the sea at a cell's centre.
    int32_t cellHeight(int32_t cx, int32_t cy) const { return cellHeight_[static_cast<size_t>(cy) * nx_ + cx]; }
    // The armies of other sides than `raiser` that have men in the town: they
    // stop its banner rising while as many as the raisers, and pull it down while more.
    int32_t bannerContest(int32_t city, uint8_t raiser) const;
    uint64_t checksum() const;
    // Saved games (campaign_save.cpp): the whole state as printable text, and
    // a campaign rebuilt from it on the same continent, identical to the one
    // saved (nullptr, and why, for a damaged save or one from another map).
    // New state must be written and read there too: tests/test_campaign_save.cpp
    // checks that a restored campaign carries on exactly like the original
    // (while no fight is on: a save settles any, where they stand).
    std::string save();
    static std::unique_ptr<Campaign> load(const Continent& continent, const std::string& text, std::string* error = nullptr);
    uint64_t seed() const { return seed_; }

private:
    // What an Order does (applyPending, and nothing else, calls them; the computer lords' own choices, CampaignAi, are
    // theirs). A march to a cell (false if no route); LOC; a banner's army sent home; boats: lay one down in a port
    // (kBoatCost gold, ready in kBoatDays), board the stack (and friends in the port, up to kBoatCapacity) onto a free
    // boat there and put out to the harbour, or step ashore from the sea; a hero searching the ruin or sage the stack
    // stands on, or the whole stack blessed at a temple; taking (or turning down) the hero offering their service.
    friend class CampaignAi;
    friend struct CampaignOrders;  // the tests' way to ask one of them for its answer (tests/campaign_orders.h)
    bool move(uint32_t stackId, int32_t cx, int32_t cy);
    bool produce(int32_t city, int32_t option);  // -1 stops
    // Warlords' LOC: send what a city trains to another city of ours (-1 stops); at most four cities may send to one.
    bool vectorProduction(int32_t city, int32_t to);
    // Warlords' DISBAND: send an army home to stop paying for it (never a hero).
    bool disband(uint32_t stackId, uint32_t armyId);
    bool buildBoat(int32_t city);
    bool embark(uint32_t stackId);
    bool land(uint32_t stackId);
    bool search(uint32_t stackId);
    bool hire(uint8_t side, bool accept);
    // Move for chosen squads: they are split off into a stack of their own first (their men stay units of the world,
    // so a stack split in a fight is still in it), and the whole stack otherwise. The march is the stack's; whoever is
    // ordered is, like his men, the player's: a stack of another side is never moved.
    Stack* stackFor(const Order& o, bool splitting);
    // A Command: whose unit it is (his stack, of the side that gives it), else it is refused.
    void commandMen(const Order& o);
    bool underOrders(const Stack& s) const;  // a man of it has somewhere to go or someone to fight on orders
    void initTerrain();
    void initCities();
    void initRare();  // campaign_rare.cpp: the realms' rare troops and the levies
    void newDay();
    void runAi(uint8_t side);  // campaign_ai.cpp
    void bannerAlarm(int32_t city);  // campaign_ai.cpp: a Hard lord answers an enemy banner over his town at once
    void deliverTransits();
    int32_t armiesAt(uint8_t owner, int32_t cx, int32_t cy) const;
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
    int32_t afloatPenalty(const Stack& s) const { return s.boat ? 1 : 0; }
    void moveStacks();
    void arrive(Stack& s);
    // Sorts armies into battle order and gives each its AS or DS (campaign_rules.cpp).
    void order(std::vector<Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, std::vector<int32_t>& power, int32_t engines = 0,
               int32_t oppX = -1, int32_t oppY = -1) const;
    int32_t forceModifier(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, int32_t engines) const;
    // How a stack marches (campaign_march.cpp): each gait among its walkers at
    // the speed of its slowest army of that gait (on each step the slowest of
    // them sets the time), or its fliers' pace; and its heroes' speed items.
    struct Pace {
        bool air = false;
        int32_t flight = 0;   // mm a second, for a stack in the air
        int32_t quicker = 0;  // percent
        int32_t gaits = 0;
        Gait gait[kGaitCount] = {};
        int32_t speed[kGaitCount] = {};  // mm a second
        int32_t best = 1;     // mm a second: the most it could make on any ground (the route search's estimate)
    };
    Pace paceOf(const Stack& s) const;
    // A step from a cell to its neighbour in direction dir (0-7, east first,
    // round through the north: direction(dx, dy), -1 if not a neighbour), on foot or wing.
    static int32_t direction(int32_t dx, int32_t dy);
    int32_t marchTime(const Pace& p, int32_t cell, int32_t dir) const;
    int32_t seaTime(int32_t dx, int32_t dy) const;  // afloat, so many cells across and up
    // Per gait, built on first use: the effort() of every step, cell x 8 + direction.
    const std::vector<int32_t>& efforts(Gait gait) const;
    mutable std::vector<std::vector<int32_t>> efforts_;
    std::vector<uint8_t> roadCell_;  // a road runs through the cell (a town's too)
    // Level plains metres from every cell to a city for a man on foot (the
    // computer lords' sense of distance; derived from the map, so not state).
    const std::vector<uint16_t>& marchField(int32_t city) const;
    mutable std::vector<std::vector<uint16_t>> marchFields_;
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
    void templeGift(Stack& s, SiteState& site, const std::string& where);  // campaign_rare.cpp
    void offerCompanions(Offer& o);
    void hireCompanions(Stack*& home, uint8_t side, const Offer& o, int32_t city);

    const Continent& continent_;
    uint64_t seed_ = 0;
    Rng rng_;
    uint32_t tick_ = 0;
    uint32_t nextId_ = 1;
    int32_t nx_ = 0, ny_ = 0;
    std::vector<Lay> terrain_;
    std::vector<uint8_t> riverEast_, riverNorth_;  // a river runs between this cell and its east / north neighbour
    std::vector<Side> sides_;
    std::vector<CityState> cities_;
    std::vector<int16_t> cityCell_;  // per cell: the city in it, or -1 (derived from the cities, not state)
    std::vector<Stack> stacks_;
    std::vector<Fight> fights_;
    std::vector<int32_t> armiesRaised_;  // per side, for naming
    std::vector<Transit> transits_;
    Rng aiRng_;  // the computer lords' own choices, apart from the battle dice
    std::vector<SiteState> sites_;
    std::vector<Loot> loot_;
    std::vector<Deed> deeds_;
    std::vector<Offer> offers_;           // per side
    std::vector<uint8_t> artifactFound_;  // per artifact: found by someone (each exists once)
    int32_t heroesHired_ = 0;             // for naming
    std::vector<uint8_t> navigable_;
    std::vector<Boat> boats_;
    std::vector<int32_t> boatsBuilt_;  // per side, for naming
    int32_t voyages_ = 0;
    Stack* stackById(uint32_t id);
    // campaign_capture.cpp: the banners rising over cities, and a city changing hands.
    void bannerStep();
    int32_t armiesInTown(int32_t city, int32_t (&in)[9]) const;
    void transfer(int32_t city, uint8_t side);
    // campaign_world.cpp: the men on the land.
    void ensureWorld();
    void seat(Stack& s);                 // puts the men of the stack that are not in the world into it
    void release(Army& a);               // takes an army's men out of the world (boarding, disbanded), keeping what they are
    void freshWorld();                   // the world cleared of what is in the air and the men not in it put in (a save, a load)
    void seatNew();                      // seat() for each stack with an army not seated
    void stepWorld();
    void steer(Stack& s, std::vector<Command>& commands, bool think);
    void fightAi(Stack& s, std::vector<Command>& commands);
    // fightAi's grid of the units a man may fight, built at the first use in a think (scratch, not state).
    bool foeGridBuilt_ = false;
    int32_t foeCells_ = 0;
    std::vector<uint32_t> foeStart_, foeItems_;
    void syncMen(Stack& s);
    // The slots of a stack's living men in its formation, in millimetres to
    // the right of its body and ahead of it, in army order.
    void layout(const Stack& s, std::vector<std::pair<int32_t, int32_t>>& slots, bool column) const;
    void anchorOf(int32_t cx, int32_t cy, int32_t& x, int32_t& y) const;
    void holdGates(int32_t city);
    std::shared_ptr<const Region> region_;
    std::unique_ptr<World> world_;
    std::vector<int32_t> gateCity_;  // per gate of the world's region: the city it belongs to
    Setup setup_;
    std::vector<Order> pending_;
    void applyPending();  // the Orders whose tick has come, in the order submitted (step)
    std::function<void()> beforeHook_, stepHook_, clearedHook_;
    std::function<bool(uint8_t, uint8_t, float, float)> sight_;
    void endFight(uint64_t seed);  // the world cleared of a fight (and the presentation told)
    std::vector<int32_t> gateHp_;  // the gates' wounds as saved, until the world exists to hold them
    void gateHps(std::vector<int32_t>& out) const;
    std::vector<int32_t> gatePin_;  // the gates the player holds open or shut, as saved (pairs: gate, GatePin), likewise
    void gatePinList(std::vector<int32_t>& out) const;
    std::vector<uint32_t> boarding_;  // (transient, within a step) the stacks that reached a quay with the sea next on their route
    std::vector<std::pair<uint32_t, uint32_t>> corpses_;  // (tick to take away, unit): the fallen lie a while
    std::vector<Command> commands_, playerCommands_;  // (the player's go into the world's step after the stacks' own)
    // campaign_territory.cpp
    void initTerritory();
    void territoryStep();
    void hashTerritory(StateHasher& h) const;
    std::vector<Hex> hexes_;
    std::vector<Outpost> outposts_;
    std::vector<int16_t> cellHex_;
    std::vector<int16_t> cellHeight_;
};

}  // namespace illyria::sim
