#pragma once

// The rules that decide a match's outcome (mode_rules.cpp, docs/GAME_DESIGN.md
// §4.5): holding Horde's hill, capture the flag's flags, the horde's waves and
// the price of a squad. Integers and fixed point, read from the simulation
// only, so every peer reaches the same verdict at the same tick.

#include <cstdint>
#include <vector>

#include "illyria/sim/hash.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

// A soldier who counts on a hill or a flag: alive, on his feet, not broken.
bool standsGround(const Unit& u);

// Horde: the holders must keep the hill. While the horde has men on it and
// the holders have none, the hill's clock runs down; kLoseSeconds of that and
// it is lost. A holder setting foot back on it stops the clock and winds it
// back up at once.
class HillRule {
public:
    static constexpr int32_t kRadiusM = 22;
    static constexpr int32_t kLoseSeconds = 12;
    static constexpr int32_t kLoseTicks = kLoseSeconds * kTicksPerSecond;

    void place(const Vec3& at, PlayerId holders, int32_t radiusM = kRadiusM);
    void step(const World& world);
    // Only who stands on it (between waves, when the clock does not run).
    void count(const World& world);

    const Vec3& at() const { return at_; }
    Fixed radius() const { return Fixed::fromInt(radiusM_); }
    PlayerId holders() const { return holders_; }
    // Men of each side on the hill at the last step.
    int32_t holdersIn() const { return holdersIn_; }
    int32_t foesIn() const { return foesIn_; }
    // How long it has stood empty of holders with the horde on it, and how
    // long the holders have stood on it in all (the score's "time on the hill").
    int32_t contestedTicks() const { return contested_; }
    int32_t heldTicks() const { return held_; }
    bool lost() const { return lost_; }
    void hash(StateHasher& h) const;

private:
    Vec3 at_;
    PlayerId holders_ = 0;
    int32_t radiusM_ = kRadiusM;
    int32_t holdersIn_ = 0, foesIn_ = 0, contested_ = 0, held_ = 0;
    bool lost_ = false;
};

// Capture the flag, Myth's rule: a flag falls the moment an enemy soldier
// stands on it (within kRadiusM) while none of its own team do. A team whose
// flag falls is out; the last team with its flag standing wins.
class FlagRule {
public:
    static constexpr int32_t kRadiusM = 5;
    static constexpr int32_t kThreatM = 35;  // enemies this near: "Our flag is under attack!"
    static constexpr PlayerId kNobody = 255;
    struct Flag {
        Vec3 at;
        PlayerId team = 0;
        bool standing = true;
        PlayerId takenBy = kNobody;  // whose men stood on it when it fell
        uint32_t fellTick = 0;
        // At the last step: its own men on it, enemies on it, enemies near it.
        int32_t defenders = 0, attackers = 0, threats = 0;
    };

    void add(const Vec3& at, PlayerId team);
    // The flags that fell this step (indices).
    std::vector<size_t> step(const World& world);

    const std::vector<Flag>& flags() const { return flags_; }
    const Flag* flagOf(PlayerId team) const;
    int standing() const;
    // The one team whose flag still stands, or kNobody while two or more do.
    PlayerId lastStanding() const;
    void hash(StateHasher& h) const;

private:
    std::vector<Flag> flags_;
};

// Horde's waves: what comes at the hill in round `wave` (1, 2, ...). Each
// wave is stronger than the last and brings more kinds: the dead first, then
// men, beasts, brutes, fiends and sorcerers; every third an elite wave of
// seasoned troops, every fifth a boss (a demon, every tenth a dragon) at the
// head of a host. They come from 1 to 4 of the eight compass edges.
struct WaveGroup {
    UnitType type = UnitType::Undead;
    int32_t count = 1;
    int32_t rank = 0;
    uint8_t edge = 0;   // compass direction they come from: 0 east, counter-clockwise in eighths
    bool boss = false;  // the wave's champion
};
struct WavePlan {
    int32_t wave = 0;
    bool elite = false, boss = false;
    int32_t strength = 0;  // the threat it carries (waveThreat of every soldier)
    std::vector<WaveGroup> groups;
};
// `difficulty` 0 easy, 1 normal, 2 hard; `holders` the seats defending.
WavePlan hordeWave(int32_t wave, int32_t difficulty, int32_t holders, uint64_t seed);
// What one soldier of this kind is worth to a wave.
int32_t waveThreat(UnitType type);
void hashWave(StateHasher& h, const WavePlan& plan);

// Myth-style picks: squads bought with points, for Horde's reinforcements
// between rounds and the armies of capture the flag.
struct Offer {
    UnitType type;
    int32_t count;  // soldiers in the squad
    int32_t cost;   // points
};
const std::vector<Offer>& squadOffers();

}  // namespace illyria::sim
