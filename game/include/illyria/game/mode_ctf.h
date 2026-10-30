#pragma once

// Capture the flag (game/src/mode_ctf.cpp, docs/GAME_DESIGN.md §4.5.2), Myth's
// rule: two to four teams, each of one or more seats, each with its flag at
// its start and a fixed army bought before the match from a points budget. A
// flag falls the moment an enemy soldier stands on it while none of its own
// team do (sim::FlagRule); a team whose flag falls is out of the match: its
// army breaks and flees, never to rally (World::breakSide), and can no longer
// win. The last team with its flag standing wins. With a time limit, when it
// runs out the team with its flag standing and the most strength left wins.
// The computer plays a team by holding its flag with part of its army and
// sending the rest at the nearest enemy flag.

#include <array>
#include <string>
#include <vector>

#include "illyria/game/mode.h"
#include "illyria/sim/mode_rules.h"

namespace illyria::game {

struct CtfOptions {
    sim::Vec3 centre;  // the field, in continent coordinates
    std::string place;
    int teams = 2;  // 2 to 4
    // Who sits where. Unset (all empty): seat t plays team t, the first a person, the rest the computer.
    struct SeatSpec {
        sim::SeatControl control = sim::SeatControl::Empty;
        int team = 0;
    };
    std::array<SeatSpec, sim::kMaxSeats> seats{};
    int budget = 36;  // points for each team's army, shared by its seats
    // The local seat's picks (indices into sim::squadOffers()); empty: a balanced army.
    std::vector<uint8_t> army;
    int timeLimitSeconds = 900;  // 0: none
    int ringM = 150;  // the flags stand this far from the field's middle
    uint64_t seed = 1;
};

class CtfMode : public SeatedMode {
public:
    static constexpr sim::PlayerId kNobody = sim::FlagRule::kNobody;
    enum class Phase : uint8_t { Battle, Over };

    // Sets up the match on `b`, an empty field: the flags and every seat's army. The battle owns it.
    static CtfMode& start(Battle& b, const CtfOptions& o);
    explicit CtfMode(const CtfOptions& o) : opt_(o) {}
    // A seat's army for `budget` points: the computer's pick (and the
    // default), a balance of line, missiles and healers.
    static std::vector<uint8_t> defaultArmy(int budget, int variant);
    // The picks that fit the budget, in order (the rest dropped).
    static std::vector<uint8_t> affordable(const std::vector<uint8_t>& picks, int budget);

    const char* id() const override { return "ctf"; }
    void beforeStep(Battle& b, std::vector<sim::Command>& orders) override { gather(b, orders); }
    void afterStep(Battle& b) override;
    int outcome() const override;
    uint64_t checksum() const override;
    std::string hudJson(const Battle& b) const override;
    std::vector<ModeFlag> flags(const Battle& b) const override;

    Phase phase() const { return phase_; }
    const sim::FlagRule& flagRule() const { return flags_; }
    int teams() const { return teams_; }
    sim::PlayerId localTeam() const { return seats_[static_cast<size_t>(localSeat_)].team; }
    // The winner (kNobody while playing, or after a draw).
    sim::PlayerId winner() const { return winner_; }
    bool draw() const { return phase_ == Phase::Over && winner_ == kNobody; }
    int32_t elapsedTicks() const { return elapsed_; }
    // A team's strength: the hit points of its men still standing.
    int32_t strength(const Battle& b, sim::PlayerId team) const;
    // When the local flag last came under threat (for the HUD's alert), or -1.
    int32_t alertTick() const { return alertTick_; }

private:
    void think(Battle& b) override;
    void applyAction(Battle&, int, const sim::MatchAction&) override {}
    void setUp(Battle& b);
    sim::Vec3 startOf(int team) const;

    CtfOptions opt_;
    int teams_ = 2;
    Phase phase_ = Phase::Battle;
    sim::FlagRule flags_;
    sim::PlayerId winner_ = kNobody;
    int32_t elapsed_ = 0;
    int32_t alertTick_ = -1;
    bool threatened_ = false;  // the local flag had enemies near it at the last step
    std::vector<uint8_t> defends_;  // by squad: held back to guard its flag
    std::vector<sim::PlayerId> out_;  // the teams out, in the order they fell
};

}  // namespace illyria::game
