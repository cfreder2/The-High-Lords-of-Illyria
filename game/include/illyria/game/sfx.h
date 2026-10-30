#pragma once

// Sound effects, as in Myth: no music in battle, but every bowstring, blow,
// blast and body tells the player what is happening, off screen too. This
// turns each tick's simulation events into positional sound cues (which
// sound, where, how loud, panned how) and the scene into ambience levels
// (wind by the land, swamp insects, flies over the dead, war drums, rain),
// throttled so a battle never turns to noise. The page plays them
// (game/web/sfx.js) from game/web/sfx/sounds.json, the one list of every
// sound; docs/SOUND_EFFECTS.md is generated from it. The soldiers' cries of
// pain and death go through the voices (voice.h). Presentation only.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/world.h"

namespace illyria::game {

// Every sound the code asks for; soundName() is its id in sounds.json.
enum class Sound : uint8_t {
    BowRelease,      // an arrow loosed
    ArrowWhistle,    // an arrow passing close by the listener
    SwordSwing,      // a cut or chop (the whoosh before the hit)
    HeavySwing,      // a club, a great beast's swipe
    SwordFlesh,      // a cut into flesh
    SwordArmor,      // a cut on mail and plate
    BluntCrush,      // a club, a hoof, a staff
    PierceFlesh,     // a pike, a lance, fangs, a horn
    BlockShield,     // a blow taken on a shield
    HitBone,         // a blow on the walking dead
    SatchelThrow,    // a grenadier's throw: the bag and the slosh
    SatchelFuse,     // its fuse hissing
    FireballCast,    // a wizard's fire gathering and loosed
    CatapultRelease, // the arm slamming the crossbar
    ArrowDirt,       // arrow into the ground
    ArrowWood,       // into a tree, a door, a beam
    ArrowStone,      // off a wall or rock
    ArrowShield,     // into a shield: a splintering crack
    ArrowArmor,      // off armour: a ring
    ArrowFlesh,      // into a man: a wet thwack
    ArrowWater,      // into water
    StoneSmash,      // a catapult's stone landing
    GateHit,         // a blow or blast on a gate
    GateBreak,       // the gate gives way
    SatchelBlast,    // a satchel charge going off
    FireballBlast,   // glass shatter and fire roar
    DismemberSnap,   // a limb or head cut off
    GibBurst,        // a body torn apart
    BloodRain,       // the drops and bits pattering down after
    BodyFall,        // a body hitting the ground
    BodyFallArmor,   // an armoured one
    BonesCollapse,   // the walking dead falling apart
    FleshBurn,       // a burned body sizzling
    Heal,            // a healer's touch
    Victory,         // brass fanfare
    Defeat,          // a low minor drone
    Thunder,         // after a lightning flash (weather)
    Frog,            // a croak from the marsh
    WarHorn,         // Horde: the next wave sets out
    Alarm,           // capture the flag: the player's flag in danger
    Count
};
constexpr int kSoundCount = static_cast<int>(Sound::Count);
const char* soundName(Sound s);
// 0 background .. 5 must be heard: the page cuts the lowest for the higher.
int soundPriority(Sound s);

struct SfxCue {
    uint32_t tick = 0;
    Sound sound = Sound::BowRelease;
    V3 pos;
    bool positional = true;
    float pan = 0;       // -1 left .. 1 right
    float gain = 1;      // 0..1 by distance (and how many at once)
    float distance = 0;  // 0 near .. 1 at the edge of hearing: the page muffles and reverbs it
    float delay = 0;     // seconds after now
    float doppler = 0;   // arrows passing: closing speed, m/s (positive coming nearer)
    int count = 1;       // events merged into this cue (a volley)
    int priority = 0;
};

// The ambience the page fades its loops to (0..1 each, pans -1..1).
struct Ambience {
    float windMountain = 0, windValley = 0;  // the high passes' howl, the lowland's sweep
    float swamp = 0, swampPan = 0;           // insects and frogs
    float flies = 0, fliesPan = 0;           // carrion flies over the dead
    float drums = 0, drumsPan = 0;           // the enemy's war drums
    float rain = 0;                          // light below 0.6, heavy above
};

class Sfx {
public:
    explicit Sfx(sim::PlayerId player = 0) : player_(player) {}
    // The side heard as ours (Battle::setLocalSide).
    void setPlayer(sim::PlayerId player) { player_ = player; }

    // Where the player listens from (as Voice::setListener).
    void setListener(const V3& at, const V3& right, float zoom);
    // Weather hook (no weather is simulated yet): 0 dry .. 1 a downpour, with thunder above 0.5.
    void setRain(float rain) { rain_ = rain; }
    // After each simulation step. `outcome` 0 fighting, 1 won, 2 lost; the
    // continent and the battlefield's origin in it place the wind; in
    // enemy-held land the drums beat on.
    void observe(const sim::World& world, int outcome, const sim::Continent& continent, const V3& origin, bool enemyLand);

    // A match's alarm (mode.h): heard in the player's ears with this tick's cues.
    void alert(Sound s) { want(s, listenAt_, 0, false); }

    // Cues since the last take, for the page to play.
    std::vector<SfxCue> take();
    // The ambience now, and a counter bumped each time it is worked out anew.
    const Ambience& ambience() const { return ambience_; }
    uint32_t ambienceVersion() const { return ambienceVersion_; }

    // Test hooks.
    static constexpr size_t kLogSize = 300;
    const std::deque<SfxCue>& log() const { return log_; }
    int emitted() const { return emitted_; }
    int dropped() const { return dropped_; }
    int emittedOf(Sound s) const { return perSound_[static_cast<int>(s)]; }
    std::string logJson(size_t n) const;

private:
    struct Candidate {
        bool any = false;
        SfxCue best;
        int count = 0;
    };
    void want(Sound s, const V3& pos, float delay = 0, bool positional = true, float doppler = 0);
    void flush();
    void place(SfxCue& c) const;
    void updateAmbience(const sim::World& world, const sim::Continent& continent, const V3& origin, bool enemyLand);
    void whistles(const sim::World& world);
    Sound arrowSurface(const sim::World& world, const sim::ImpactEvent& e) const;

    sim::PlayerId player_;
    bool hasListener_ = false;
    V3 listenAt_, listenRight_{1, 0, 0};
    float zoom_ = 0;
    float rain_ = 0;
    uint32_t tick_ = 0;
    bool primed_ = false;
    int lastOutcome_ = 0;

    // Missiles in flight last tick (sorted by id), to see new ones and whose blast is whose.
    struct Flight {
        sim::EntityId id;
        sim::ProjectileKind kind;
        V3 pos;
    };
    std::vector<Flight> flights_;
    std::vector<sim::EntityId> whistled_;  // arrows already heard passing (sorted)

    Candidate wanted_[kSoundCount];
    uint32_t last_[kSoundCount] = {};
    bool heard_[kSoundCount] = {};
    uint32_t nextThunder_ = 0;
    uint32_t seed_ = 12345;

    Ambience ambience_;
    uint32_t ambienceVersion_ = 0;

    std::vector<SfxCue> out_;
    std::deque<SfxCue> log_;
    int emitted_ = 0, dropped_ = 0;
    int perSound_[kSoundCount] = {};
};

}  // namespace illyria::game
