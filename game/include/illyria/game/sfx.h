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
#include <utility>
#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/units.h"
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
    // Movement: one textured sound for a body of men (or beasts) on the move, asked for by Sfx itself about twice a second.
    FootMarch,       // men on foot over earth
    FootArmor,       // men in mail and plate
    FootWet,         // men in mud, marsh or shallows
    Hooves,          // horses and wolves
    GiantTread,      // giants, minotaurs, elephants: one great footfall
    Wheels,          // an engine rolling
    Wings,           // great wings
    SatchelLay,      // a satchel set down and its match struck (heard by its owner)
    // The player's own acts (Sfx::ui: in his ears, at once, paused or not).
    OrderConfirm,    // an order given
    OrderRefuse,     // an order that cannot be done
    Toggle,          // Hold / Support, a gate held open or shut
    Select,          // a squad selected
    RallyHorn,       // the lord's Rally
    DeployBegin,     // the battle begins
    Results,         // the results sheet
    // The Conquest (Sfx::conquest).
    TownCaptured,
    TownLost,
    DayTick,         // dawn
    Recruit,         // a squad trained
    LordFallen,      // one of the player's lords is dead
    // Hooks: features being built ask for these with Sfx::hook(sound, where) (docs/SOUND_EFFECTS.md, Hooks).
    BoulderLaunch,   // a boulder levered loose
    BoulderRumble,   // rolling (asked for by Sfx itself while a landed stone moves)
    BoulderImpact,   // striking ground or a wall
    BoulderCrush,    // men under it
    CallHelp,        // a squad calls for help
    CallAnswer,      // and is answered
    ChargeImpact,    // cavalry crashing into a line
    Rout,            // a line breaking
    // Water (sim world_drift.cpp, Sfx::wading).
    Wade,            // men and beasts going through water
    WaterPlop,       // a part of a body landing in water
    BodySplash,      // a body going down into water
    StoneWater,      // a catapult's stone into water: the column
    Steam,           // fire on water
    Count
};
constexpr int kSoundCount = static_cast<int>(Sound::Count);
const char* soundName(Sound s);
// 0 background .. 5 must be heard: the page cuts the lowest for the higher.
int soundPriority(Sound s);

// How a sound at `horiz` metres from the view centre is heard with the camera `zoom` away: what is on screen is heard. The
// reach widens with the camera; its height takes the level down only to a floor (about -13 dB, -8 for the loud ones) and
// dulls the tone instead, so a battle at the far camera is a mass, not silence; past 600 it fades toward nothing.
struct Hearing {
    float gain = 0;      // 0..1
    float distance = 1;  // 0 near and bright .. 1 far and dull
};
Hearing hear(float horiz, float zoom, bool loud);

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
    // The loops sent by name (platform::sfxLayer): the mass of a fight seen from far above, columns on the march,
    // the day's own air (never quite silent), ground and bodies burning.
    float battle = 0, battlePan = 0;
    float march = 0, marchPan = 0;
    float bed = 0;
    float fire = 0, firePan = 0;
};
// The loops of `Ambience` sent by name, in order: battle, march, bed, fire.
constexpr const char* kLayerNames[4] = {"snd_amb_battle_far", "snd_amb_march_column", "snd_amb_bed_day", "snd_amb_fire"};

// What Sfx::conquest watches for change (filled by the app from the conquest, once a frame).
struct ConquestPulse {
    uint32_t day = 0;
    int winner = 0;  // 0 none yet, 1 the player, 2 another
    std::vector<uint8_t> cityOwner;  // every town's lord
    uint8_t side = 0;                // the player's
    int lords = 0;                   // the player's living heroes
    int squads = 0;                  // the player's squads
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
    // A hook for a feature (the Sound enum's last block, or any sound): heard from where it happens, merged and throttled
    // with this tick's cues. Call it during the step, before observe.
    void hook(Sound s, const V3& pos, float delay = 0) { want(s, pos, delay); }
    // The player's own act: in his ears at once, whether or not the world is stepping (deployment, a pause).
    void ui(Sound s);
    // The game's speed (1, 2, 4, 8): gaps are kept in real time, delays shortened, the slightest cues thinned.
    void setSpeed(float speed) { speed_ = speed < 1 ? 1 : speed; }
    float speed() const { return speed_; }
    // Where the ground and the dead burn, for the fire's loop.
    void setFires(const std::vector<V3>& fires) { fires_ = fires; }
    // The ambience worked out now (deployment and the pause do not step the world).
    void refreshAmbience(const sim::World& world, const sim::Continent& continent, const V3& origin, bool enemyLand) {
        tick_ = world.tick();
        updateAmbience(world, continent, origin, enemyLand);
    }
    // The picture's blow: who struck this tick (Battle::attackTick), and how long after the strike each kind's attack
    // clip shows the blow land (half its length: the renderer's clips, else the default of blowSeconds). The whoosh of
    // a swing leads; what it met sounds on the frame that shows it.
    struct Striker {
        V3 pos;
        uint8_t type = 0;
        sim::PlayerId owner = 0;
    };
    void setStrikers(std::vector<Striker> strikers) { strikers_ = std::move(strikers); }
    void setBlowSeconds(int type, float seconds) {
        if (type >= 0 && type < sim::kUnitTypeCount) blow_[type] = seconds;
    }
    float blowSeconds(int type) const;
    // The battle is decided: the drums stop and the field goes quiet under the results.
    void calm();
    // The Conquest's events, from what changed since the last pulse: dawn, towns taken and lost, a squad trained, a lord
    // fallen, the war won or lost.
    void conquest(const ConquestPulse& now);

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
    void want(Sound s, const V3& pos, float delay = 0, bool positional = true, float doppler = 0, float scale = 1);
    void movement(const sim::World& world);
    void fuses(const sim::World& world);
    void rolling(const sim::World& world);
    void flush();
    void place(SfxCue& c) const;
    void updateAmbience(const sim::World& world, const sim::Continent& continent, const V3& origin, bool enemyLand);
    void whistles(const sim::World& world);
    void wading(const sim::World& world);
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
        int32_t fuse = 0;
    };
    std::vector<Flight> flights_;
    std::vector<sim::EntityId> whistled_;  // arrows already heard passing (sorted)

    Candidate wanted_[kSoundCount];
    uint32_t last_[kSoundCount] = {};
    bool heard_[kSoundCount] = {};
    uint32_t nextThunder_ = 0;
    uint32_t seed_ = 12345;

    float speed_ = 1;
    float moveBudget_ = 2;  // and footfalls, which have a small share of their own
    float budget_ = 8;  // cues that may still start (refilled in real time: flush)
    // Movement: where every man was at the last look, and when each body of men next sounds.
    std::vector<V3> movedFrom_;
    struct Tread {
        uint64_t key;
        uint32_t next;
    };
    std::vector<Tread> treads_;
    float marching_ = 0, marchSide_ = 0, fighting_ = 0, fightSide_ = 0;
    float contact_ = 1e9f;
    uint32_t closingUntil_ = 0;
    bool calm_ = false;
    std::vector<V3> fires_;
    std::vector<Striker> strikers_;
    float blow_[sim::kUnitTypeCount] = {};  // 0: not told, the default
    std::vector<std::pair<sim::EntityId, uint32_t>> fuseNext_;  // lit satchels: when each next hisses
    bool pulsed_ = false;
    ConquestPulse pulse_;

    Ambience ambience_;
    uint32_t ambienceVersion_ = 0;

    std::vector<SfxCue> out_;
    std::deque<SfxCue> log_;
    int emitted_ = 0, dropped_ = 0;
    int perSound_[kSoundCount] = {};
};

}  // namespace illyria::game
