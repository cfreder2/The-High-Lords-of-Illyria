#pragma once

// The heart of a besieged town (game/src/battle_capture.cpp, docs/GAME_DESIGN.md
// §4.1.3): a flagpole in its courtyard, king of the hill. The side with more
// men standing within kRadius raises its flag there, the quicker the more
// they outnumber the others; when the attackers' flag reaches the top the
// town is taken and the battle ends, whoever is still standing elsewhere. A
// siege is won by taking the heart of the town, not by killing every last man.
// Deterministic: it counts the simulation's soldiers in fixed point.

#include <cstdint>

#include "illyria/sim/vec3.h"
#include "illyria/sim/world.h"

namespace illyria::game {

class CapturePoint {
public:
    static constexpr int32_t kFull = 1000;
    static constexpr int32_t kRadiusM = 14;
    // A tick's climb: kBase + one point for each man more than the other
    // side's, eight at most (one man more: 33 s; eight more: 10 s).
    static constexpr int32_t kBase = 2, kMostAhead = 8;

    // The pole at `at` (battle coordinates), the defenders' flag flying.
    void place(const sim::Vec3& at, sim::PlayerId defender, sim::PlayerId attacker);
    bool active() const { return active_; }
    void step(const sim::World& world);

    const sim::Vec3& at() const { return at_; }
    sim::PlayerId attacker() const { return attacker_; }
    sim::PlayerId defender() const { return defender_; }
    // Towards kFull: how high the attackers' flag has climbed.
    int32_t progress() const { return progress_; }
    bool taken() const { return taken_; }
    // Men of each side within the radius at the last step.
    int32_t attackersIn() const { return attackersIn_; }
    int32_t defendersIn() const { return defendersIn_; }

private:
    bool active_ = false, taken_ = false;
    sim::Vec3 at_;
    sim::PlayerId defender_ = 1, attacker_ = 0;
    int32_t progress_ = 0, attackersIn_ = 0, defendersIn_ = 0;
};

}  // namespace illyria::game
