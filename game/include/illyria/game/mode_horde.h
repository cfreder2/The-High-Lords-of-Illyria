#pragma once

// Horde (game/src/mode_horde.cpp, docs/GAME_DESIGN.md §4.5.1): as in Myth,
// hold the hill. Each seat starts with a hero and a small fixed army on the
// high ground of the chosen field; the horde comes at it in waves from the
// edges of the field, each stronger and stranger than the last, with elite
// and boss waves. Between waves a short break: each seat spends its points
// on reinforcements, the wounded mend, veterans keep their ranks and the
// heroes rise a level. A hero who falls comes back: at once in the break
// after his fall for kRecallPoints, or by himself once the next wave is
// beaten, on the hill with kReturnHpPct of his health and his levels kept. The hill is lost when the horde stands on it with none
// of the holders there for HillRule::kLoseSeconds, or when the holders are all
// dead. Up to four seats hold it together (the computer plays the empty ones
// it is given, and the horde itself, from seat kHordeSeat).

#include <array>
#include <string>
#include <vector>

#include "illyria/game/mode.h"
#include "illyria/sim/mode_rules.h"

namespace illyria::game {

struct HordeOptions {
    sim::Vec3 centre;       // the field, in continent coordinates (a Place's)
    std::string place;      // its name, for the HUD
    int difficulty = 1;     // 0 easy, 1 normal, 2 hard
    int waves = 10;         // waves to survive to win; 0: endless
    std::array<sim::SeatControl, sim::kMaxSeats> seats{sim::SeatControl::Human, sim::SeatControl::Empty,
                                                       sim::SeatControl::Empty, sim::SeatControl::Empty};
    uint64_t seed = 1;
    int localSeat = -1;      // the seat at this screen (-1: the first a person plays)
    int musterSeconds = 30;  // before the first wave
    int breakSeconds = 45;   // between waves (or until every person is ready)
};

class HordeMode : public SeatedMode {
public:
    static constexpr int kHordeSeat = sim::kMaxSeats;  // the computer, playing the horde
    static constexpr sim::PlayerId kHolders = 0, kHorde = 1;
    // The horde forms up this far out from the hill, toward its edge.
    static constexpr int32_t kSpawnM = 150;
    // Each break: the wounded mend this share of their wounds; each seat gets
    // kRoundPoints (and a point more every second wave) to spend on offers.
    static constexpr int32_t kHealPct = 50;
    static constexpr int32_t kRoundPoints = 10;
    static constexpr int kOffersShown = 6;
    // A fallen hero returns to the hill with this share of his health: in
    // the break after his fall for kRecallPoints of his seat's points, or by
    // himself when the next wave is beaten (he sits that one out).
    static constexpr int32_t kReturnHpPct = 50;
    static constexpr int32_t kRecallPoints = 5;
    enum class Phase : uint8_t { Muster, Wave, Break, Lost, Won };

    // Sets up the match on `b`, an empty field (Battle(seed, centre)): the
    // hill and its flag, each playing seat's hero and army. The battle owns it.
    static HordeMode& start(Battle& b, const HordeOptions& o);
    explicit HordeMode(const HordeOptions& o) : opt_(o) {}

    const char* id() const override { return "horde"; }
    void beforeStep(Battle& b, std::vector<sim::Command>& orders) override { gather(b, orders); }
    void afterStep(Battle& b) override;
    int outcome() const override { return phase_ == Phase::Won ? 1 : phase_ == Phase::Lost ? 2 : 0; }
    uint64_t checksum() const override;
    std::string hudJson(const Battle& b) const override;
    std::vector<ModeFlag> flags(const Battle& b) const override;

    const HordeOptions& options() const { return opt_; }
    Phase phase() const { return phase_; }
    int wave() const { return wave_; }
    int wavesSurvived() const { return survived_; }
    const sim::WavePlan& plan() const { return plan_; }
    const sim::HillRule& hill() const { return hill_; }
    int32_t points(int seat) const { return seat >= 0 && seat < sim::kMaxSeats ? points_[static_cast<size_t>(seat)] : 0; }
    // This break's offers: indices into sim::squadOffers().
    const std::vector<uint8_t>& offers() const { return offers_; }
    // Ticks until the next wave sets out (in the muster and the breaks).
    int32_t ticksToWave() const { return timer_; }
    // The horde still fighting (alive and unbroken), and how many came this wave.
    int enemiesLeft(const Battle& b) const;
    int enemiesThisWave() const { return waveSoldiers_; }
    // The holders' kills, all told.
    int kills(const Battle& b) const;
    // A seat's hero (squad index), or -1.
    int heroSquad(int seat) const { return seat >= 0 && seat < sim::kMaxSeats ? heroes_[static_cast<size_t>(seat)] : -1; }
    // A seat's hero, the soldier (kNoEntity: none), and whether he lies fallen.
    sim::EntityId hero(const Battle& b, int seat) const;
    bool heroFallen(const Battle& b, int seat) const;
    // The wave a fallen hero fell in, once a wave has ended without him (0: none);
    // he returns by himself when wave heroFellIn + 1 is beaten.
    int heroFellIn(int seat) const { return seat >= 0 && seat < sim::kMaxSeats ? fellIn_[static_cast<size_t>(seat)] : 0; }
    // Test hook: the break or muster ends now.
    void skipBreak() {
        if (phase_ == Phase::Muster || phase_ == Phase::Break) timer_ = 1;
    }

private:
    void think(Battle& b) override;
    void applyAction(Battle& b, int seat, const sim::MatchAction& a) override;
    void setUp(Battle& b);
    void startWave(Battle& b);
    void endWave(Battle& b);
    void chooseOffers();
    // A seat's fallen hero back on the hill (kReturnHpPct of his health).
    void returnHero(Battle& b, int seat);
    std::string heroJson(const Battle& b, int seat) const;
    // Where a seat's men stand on the hill: their side of it.
    sim::Vec3 sector(int seat) const;
    int playingSeats() const;

    HordeOptions opt_;
    Phase phase_ = Phase::Muster;
    int wave_ = 0, survived_ = 0;
    int32_t timer_ = 0;
    int waveSoldiers_ = 0;
    sim::HillRule hill_;
    sim::WavePlan plan_;
    std::array<int32_t, sim::kMaxSeats> points_{};
    std::array<bool, sim::kMaxSeats> ready_{};
    std::array<int, sim::kMaxSeats> heroes_{-1, -1, -1, -1};
    std::array<int, sim::kMaxSeats> recruits_{};  // squads bought, for where they arrive
    std::array<int, sim::kMaxSeats> fellIn_{};    // heroFellIn
    std::vector<uint8_t> offers_;
    int32_t contestedSeen_ = 0;  // the hill's clock when last looked at (for the herald)
    std::vector<uint8_t> reached_;  // the ground joined to the hill (mode.h reachFrom): where waves form up
};

}  // namespace illyria::game
