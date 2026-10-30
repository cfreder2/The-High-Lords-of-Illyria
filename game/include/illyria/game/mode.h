#pragma once

// Matches played on the 3D battle (docs/GAME_DESIGN.md §4.5): Horde
// (mode_horde.h) and capture the flag (mode_ctf.h). A mode is set on a Battle
// (Battle::setMode) and runs every tick: before the world steps it gathers
// each seat's input (the local player's orders from the UI, the computer
// seats' from their AI, a remote peer's from the network one day) into the
// match's log and the world's commands; after it, it applies its rules (the
// hill, the flags, the waves), which read the simulation only.
//
// Lockstep (docs/NETCODE.md): everything that changes the match goes through
// a seat's input or a rule driven by the simulation's state, so a replay of
// the log reproduces the match exactly (tests/test_horde.cpp).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/match.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::game {

class Battle;

// --- Where to fight (mode_places.cpp) ---------------------------------------

// A named place on the continent a match can be fought over.
struct Place {
    enum class Kind : uint8_t { Hill, Ford, Bridge, Pass, Ruin, Temple };
    Kind kind = Kind::Hill;
    std::string name;
    sim::Vec3 pos;  // continent coordinates
    int32_t score = 0;  // how good a field it makes (higher first)
    bool recommended = false;
};
const char* placeKindName(Place::Kind k);
// Every place worth a match, the recommended first: hills, fords and bridges,
// passes, ruins and temples, read from the continent (so they follow it).
std::vector<Place> battlePlaces(const sim::Continent& c);
// The hill to hold: the highest open, dry, walkable ground within `searchM`
// of `near` (battle coordinates), nearer the centre on a tie.
sim::Vec3 highGround(const sim::Region& r, const sim::Vec3& near, int32_t searchM);
// Open, dry, walkable ground at p or the nearest such within `searchM`.
sim::Vec3 firmGround(const sim::Region& r, const sim::Vec3& p, int32_t searchM);

// Exact compass directions for placing armies (no sines: every peer places
// them alike): eighths counter-clockwise from east, as unit vectors.
sim::Vec3 compass(int eighth);
// The way to the right of a flat direction.
inline sim::Vec3 rightOf(const sim::Vec3& d) { return {d.y, -d.x, sim::Fixed::zero()}; }
// p kept `margin` metres inside a battlefield `size` metres square.
sim::Vec3 insideField(const sim::Vec3& p, int32_t size, int32_t margin);

// The colours of a match's teams: blue (the local player's), red, gold, green.
V3 teamColour(sim::PlayerId team);
const char* teamName(sim::PlayerId team);

// A flag on a pole, for the renderer (Renderer::Flag).
struct ModeFlag {
    V3 base;
    float raised = 1;  // 0..1 up the pole
    V3 colour;
    float height = 12, size = 3;
};

// --- Starting one (mode_setup.cpp) -----------------------------------------

// The places as JSON for the page's picker.
std::string placesJson(const std::vector<Place>& places);
// A match ("horde" or "ctf") from the page's options, a flat JSON object:
// "place" (an index into `places`), "seed", "seats" (["human", "computer",
// "empty", ...]); Horde: "difficulty" 0-2, "waves" (0 endless),
// "breakSeconds", "musterSeconds"; capture the flag: "teams" 2-4,
// "seatTeams" ([0, 1, ...]), "budget", "army" (offer indices), "time"
// (seconds; 0 none). Null without a valid place or id.
std::unique_ptr<Battle> makeMatch(const std::string& id, const std::string& options, const std::vector<Place>& places,
                                  std::shared_ptr<const sim::Continent> continent);

// --- The mode on a battle ---------------------------------------------------

class BattleMode {
public:
    virtual ~BattleMode() = default;
    virtual const char* id() const = 0;  // "horde", "ctf"
    // Before the world steps; `orders` holds the local player's from the UI
    // and leaves as every seat's, merged.
    virtual void beforeStep(Battle& b, std::vector<sim::Command>& orders) = 0;
    virtual void afterStep(Battle& b) = 0;
    // 0 playing, 1 the local player's team won, 2 it lost.
    virtual int outcome() const = 0;
    // The match's state beyond the world's, for desync checks.
    virtual uint64_t checksum() const = 0;
    // For the page: the mode's HUD as a JSON object.
    virtual std::string hudJson(const Battle& b) const = 0;
    virtual std::vector<ModeFlag> flags(const Battle& b) const = 0;
    // What the local player chose on the HUD (a pick, ready): sent with the next tick's input.
    void act(const sim::MatchAction& a) { localActions_.push_back(a); }

protected:
    std::vector<sim::MatchAction> localActions_;
};

// The seats and their input streams, shared by Horde and capture the flag.
class SeatedMode : public BattleMode {
public:
    // The computer's seats think every this many ticks (as the built-in enemy).
    static constexpr uint32_t kThinkTicks = 2 * sim::kTicksPerSecond;

    const std::vector<sim::Seat>& seats() const { return seats_; }
    int localSeat() const { return localSeat_; }
    // Everything every seat did, tick by tick (docs/NETCODE.md).
    const sim::InputLog& log() const { return log_; }
    const sim::SeatRoster& roster() const { return roster_; }
    // Plays the match from a log instead of the local player and the
    // computer: a replay (and the determinism tests).
    void replay(const sim::InputLog& log) {
        feed_ = log;
        replaying_ = true;
    }
    // The seat a squad belongs to (kNoSeat: none).
    int seatOfSquad(size_t squad) const { return squad < squadSeat_.size() ? squadSeat_[squad] : sim::kNoSeat; }

protected:
    // The tick's inputs: the local seat's (`orders` and the HUD's actions),
    // the computer seats' (think, every kThinkTicks), logged, merged into
    // `orders` and their actions applied, in seat order.
    void gather(Battle& b, std::vector<sim::Command>& orders);
    // The computer seats' turn: orders through Battle's ai* calls, actions through computerAct.
    virtual void think(Battle& b) = 0;
    virtual void applyAction(Battle& b, int seat, const sim::MatchAction& a) = 0;
    void computerAct(int seat, const sim::MatchAction& a) { computerActions_.push_back({seat, a}); }
    // A squad raised for a seat (kNoSeat for none): its soldiers are that seat's.
    int raise(Battle& b, int seat, const std::string& name, sim::PlayerId team, sim::UnitType type, int count, V3 at,
              V3 facing, int rank = 0);
    void hashSeats(sim::StateHasher& h) const;

    std::vector<sim::Seat> seats_;
    int localSeat_ = 0;
    sim::SeatRoster roster_;
    std::vector<int8_t> squadSeat_;  // by squad index

private:
    sim::InputLog log_, feed_;
    bool replaying_ = false;
    std::vector<std::pair<int, sim::MatchAction>> computerActions_;
};

}  // namespace illyria::game
