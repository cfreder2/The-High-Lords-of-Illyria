#pragma once

// The sandbox (game/src/mode_sandbox.cpp): a field to try things on, not a
// match. Any unit type for any of four sides is set down where the player
// clicks; he commands whichever side he picks; each side fights on its own
// when told to (the computer's general handling, Battle::aiFight) or stands
// until ordered. Time runs slow, fast, or a tick at a time, and the renderer
// can be told to play any clip on the selected soldiers, so animations and
// combat are tuned without playing a match. One person, no seats, no log.

#include <array>
#include <map>
#include <string>

#include "illyria/game/mode.h"

namespace illyria::game {

struct SandboxOptions {
    sim::Vec3 centre;  // the field, in continent coordinates
    std::string place;
    int radiusM = 260;  // the field's edge (Battle::setZone)
    uint64_t seed = 1;
};

class SandboxMode : public BattleMode {
public:
    static constexpr int kSides = 4;

    // Sets up the field on `b`: the edge, a squad of heavy infantry for each
    // of the first two sides facing each other. The battle owns it.
    static SandboxMode& start(Battle& b, const SandboxOptions& o);
    explicit SandboxMode(const SandboxOptions& o) : opt_(o) {}

    const char* id() const override { return "sandbox"; }
    void beforeStep(Battle& b, std::vector<sim::Command>& orders) override;
    void afterStep(Battle&) override {}
    int outcome() const override { return 0; }
    uint64_t checksum() const override { return 0; }
    std::string hudJson(const Battle& b) const override;
    std::vector<ModeFlag> flags(const Battle&) const override { return {}; }  // the stakes are the field's (zoneStakes)

    // A squad of `count` of `type` for `side`, at `at` (battle coordinates),
    // facing the nearest enemy squad (else the middle). Its index.
    int spawn(Battle& b, sim::UnitType type, sim::PlayerId side, int count, V3 at);
    // Whether `side` fights on its own (else it stands until ordered).
    void setFights(sim::PlayerId side, bool on) { fights_[side & 3] = on; }
    bool fights(sim::PlayerId side) const { return fights_[side & 3]; }

    // Tuning a type's fighting numbers (World::retune), for every soldier of
    // it on the field and every squad set down after. As JSON, in the units
    // a person thinks in (metres, seconds, percent), with the unit sheet's
    // values beside them: {"hp":..,"speed":..,"melee":..,"reach":..,
    // "swing":..,"arrow":..,"reload":..,"range":..,"block":..,"aggro":..,"sheet":{..}}.
    std::string statsJson(sim::UnitType type) const;
    // Any of those keys (others ignored); "{}" puts back the unit sheet's.
    void tune(Battle& b, sim::UnitType type, const std::string& json);

private:
    sim::UnitStats statsOf(sim::UnitType type) const;

    SandboxOptions opt_;
    std::array<bool, kSides> fights_{};
    std::map<int, sim::UnitStats> tuned_;  // by type
};

}  // namespace illyria::game
