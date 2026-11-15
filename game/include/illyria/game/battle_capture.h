#pragma once

// The heart of a besieged town (game/src/battle_capture.cpp, docs/GAME_DESIGN.md
// §4.1.3): a flagpole in its courtyard. The attackers raise their flag there
// while they have men at the pole and no defender still holds the courtyard's
// ground: an unbroken man of the town's anywhere on the ground inside its
// walls stops the flag where it is. A capital has a second pole behind the
// doors of its inner bailey, before the keep: when the courtyard's flag is up
// the keep must be taken the same way, and only then is the town taken and
// the battle over. A siege is won by clearing the town's ground and holding
// its heart, not by standing in it for nine seconds.
//
// A pole nobody holds (the isle at Oakhaven, placeNeutral) is fought for by both hosts alike: whichever has men at the
// pole with none of the other's there raises its own flag, kNeutralSeconds of that to the top (a minute: time for the other host to come back at it by a bridge); the other host, alone at
// the pole, hauls it down three times as fast before its own goes up; with both there, or neither, it stays where it is.
// The first flag to the top wins the battle.
// Deterministic: it counts the simulation's soldiers in fixed point.

#include <cstdint>
#include <vector>

#include "illyria/sim/vec3.h"
#include "illyria/sim/world.h"

namespace illyria::game {

class CapturePoint {
public:
    static constexpr int32_t kFull = 1000;
    static constexpr int32_t kRadiusM = 14;
    // A tick's climb: kBase + one point for every kMenPerPoint attackers at the pole, kMostMen of them at most (one to
    // three men: 33 s; four: 17 s; eight or more: 11 s). With nobody of theirs at the pole it sinks kBase a tick.
    static constexpr int32_t kBase = 1, kMenPerPoint = 4, kMostMen = 8;
    static constexpr int kNoRing = -1;

    // One pole and the ground that goes with it: the inside of a ring of walls (the region's rampart `ring`, less any ring
    // within it and the walks), or with no ring the pole's own radius.
    struct Stage {
        sim::Vec3 at;
        int ring = kNoRing;
    };

    // The pole at `at` (battle coordinates), the defenders' flag flying.
    void place(const sim::Vec3& at, sim::PlayerId defender, sim::PlayerId attacker, int ring = kNoRing);
    // A bare pole at `at` that either of two hosts may raise its flag on: men within `radiusM` of it are at the pole.
    static constexpr int32_t kNeutralSeconds = 60, kNeutralRadiusM = 12, kHaulDown = 3;
    void placeNeutral(const sim::Vec3& at, sim::PlayerId a, sim::PlayerId b);
    bool neutral() const { return neutral_; }
    // A neutral pole: whose flag is on it (attacker()), and none is while it is bare.
    bool bare() const { return neutral_ && progress_ == 0 && attackersIn_ == 0; }
    // A capital's keep: a pole to be taken after the one before it.
    void addStage(const sim::Vec3& at, int ring);
    bool active() const { return active_; }
    void step(const sim::World& world);

    // The pole now fought for (the last, once all are taken).
    const sim::Vec3& at() const { return stages_[stage_].at; }
    const std::vector<Stage>& stages() const { return stages_; }
    size_t stage() const { return stage_; }
    // (A neutral pole: the host whose flag is on it, or was last.)
    sim::PlayerId attacker() const { return attacker_; }
    sim::PlayerId defender() const { return defender_; }
    // Towards kFull: how high the attackers' flag has climbed on the pole now fought for.
    int32_t progress() const { return progress_; }
    bool taken() const { return taken_; }
    // At the last step: the attackers at the pole, and the defenders who hold its ground (unbroken, on their feet).
    int32_t attackersIn() const { return attackersIn_; }
    int32_t defendersIn() const { return defendersIn_; }
    // The attackers are at the pole and the flag does not climb: defenders hold the ground.
    bool blocked() const { return !taken_ && attackersIn_ > 0 && defendersIn_ > 0; }
    // Whether a place is on the ground of the pole now fought for.
    bool onGround(const sim::World& world, const sim::Vec3& p) const;

private:
    void stepNeutral(const sim::World& world);
    bool active_ = false, taken_ = false, neutral_ = false;
    std::vector<Stage> stages_{Stage{}};
    size_t stage_ = 0;
    sim::PlayerId defender_ = 1, attacker_ = 0;
    int32_t progress_ = 0, attackersIn_ = 0, defendersIn_ = 0;
};

}  // namespace illyria::game
