#pragma once

// Voiced lines ("barks"), as in Myth: soldiers acknowledge orders, call out
// what they see, cheer, curse and despair. This turns the simulation's events
// and the player's orders into cues (who speaks, which voice, what about,
// where), and throttles them so the battle never chatters. The page plays them
// (game/web/voice.js) from game/web/voice/lines.json, the one list of every
// line; docs/VOICE_LINES.md, the recording script, is generated from it.
// Presentation only: nothing here feeds back into the simulation.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::game {

// Who is speaking: one voice (one actor's recordings) per kind of soldier.
enum class VoiceSet : uint8_t {
    Soldier,    // light and heavy infantry, pikemen, warriors
    Archer,     // bowmen
    Dwarf,      // dwarves and dwarf grenadiers
    Rider,      // cavalry and wolf riders
    Hero,       // heroes
    Wizard,     // wizards and journeymen (healers)
    Berserker,  // wildmen
    Archon,     // the winged host
    Brute,      // giants and minotaurs: grunts and a few words
    Undead,     // moans, no words
    Fiend,      // devils and demons: snarls
    Beast,      // spiders, griffins, pegasi, elephants, unicorns, dragons
    Engine,     // a catapult's crew
    Herald,     // the battle's narrator: casualty reports, no soldier of its own
    Count
};
constexpr int kVoiceSetCount = static_cast<int>(VoiceSet::Count);

// What they are speaking about.
enum class Situation : uint8_t {
    Selected,       // the player picked them
    Move,           // a march ordered
    Attack,         // an attack ordered
    Charge,         // charging (ordered, or riders at the gallop)
    ChargeSeen,     // horse thundering at them
    HoldLine,       // a veteran's rally call (World::rallyCalls)
    HeroRally,      // a hero's rally call
    Rallied,        // turned back after breaking
    Wavering,       // fear
    Breaking,       // broken and fleeing
    HeroFell,       // our hero is dead
    EnemyHeroFell,  // theirs is
    FriendKilled,   // a friend cut down beside him
    Kill,           // his first kill
    VetKill,        // a veteran's kill: a taunt
    Promoted,       // rank up (a hero: level up)
    Wounded,        // badly hurt
    ArrowFire,      // arrows falling on them
    Flanked,        // struck from the side or behind
    FriendlyFire,   // hit by his own side's missiles
    Fuse,           // a grenadier's satchel lit
    Healing,        // a healer mending a friend
    EngineLoose,    // the catapult throws
    GateBroken,     // the gate is down
    EnemySighted,   // first contact
    Victory,
    Defeat,
    Idle,           // rare chatter when all is quiet
    // Cries (the death matrix's voices): heard through their own throttle, so
    // they never crowd out the talk.
    Pain,              // struck and hurt
    Death,             // killed: a scream or a last grunt
    DeathMoan,         // shot dead: a short choking moan
    Cackle,            // a grenadier's glee when his satchel kills
    Groan,             // the walking dead, idly
    // The herald's reports of the player's losses (a quarter, half, four fifths).
    Casualties,
    SevereCasualties,
    TotalCasualties,
    Count
};
constexpr int kSituationCount = static_cast<int>(Situation::Count);

// The names lines.json uses ("soldier", "hold_line").
const char* voiceSetName(VoiceSet set);
const char* situationName(Situation s);
VoiceSet voiceSetOf(sim::UnitType type);
// Whether this voice is ever asked to say this: the manifest must have lines
// for every such pair (in the set or its fallback set; tests/test_voice.cpp).
bool voices(VoiceSet set, Situation s);
// 0 idle chatter .. 5 the gravest news; the player hears the higher first.
int priorityOf(Situation s);

struct VoiceCue {
    uint32_t tick = 0;
    sim::EntityId speaker = sim::kNoEntity;
    sim::PlayerId side = 0;
    bool enemy = false;  // the other side: the page pitches it lower
    VoiceSet set = VoiceSet::Soldier;
    Situation situation = Situation::Selected;
    V3 pos;
    int priority = 0;
    float pan = 0;   // -1 left .. 1 right of the listener
    float gain = 1;  // 0..1 by distance
};

class Voice {
public:
    explicit Voice(sim::PlayerId player = 0) : player_(player) {}

    // Where the player listens from: the camera's view centre, the way to its
    // right, and how far out it is zoomed (hearing fades with both).
    void setListener(const V3& at, const V3& right, float zoom);
    // After each simulation step: the events of that tick. `outcome` is the
    // battle's (0 fighting, 1 won, 2 lost).
    void observe(const sim::World& world, int outcome);
    // The player's order to these soldiers: one of them answers.
    void order(Situation s, const sim::World& world, const std::vector<sim::EntityId>& units);

    // Cues since the last take, for the page to play.
    std::vector<VoiceCue> take();
    // The last kLogSize cues, oldest first (test hooks).
    static constexpr size_t kLogSize = 200;
    const std::deque<VoiceCue>& log() const { return log_; }
    int emitted() const { return emitted_; }
    int dropped() const { return dropped_; }
    // A JSON array of the last `n` cues (test hook).
    std::string logJson(size_t n) const;

private:
    struct Candidate {
        Situation situation;
        sim::EntityId speaker;
        bool positional;
        VoiceSet set;  // Count: the speaker's own
    };
    // Queues a cue for this tick; flush() gates them, gravest first.
    void want(Situation s, sim::EntityId speaker, bool positional = true, VoiceSet set = VoiceSet::Count);
    void flush(const sim::World& world);
    bool gate(const sim::World& world, const Candidate& c);
    // The nearest living soldier of `side` within `reach` of `at` whose voice says `s`.
    sim::EntityId nearestVoice(const sim::World& world, sim::PlayerId side, const V3& at, float reach, Situation s,
                               sim::EntityId except = sim::kNoEntity) const;
    void track(const sim::World& world);

    sim::PlayerId player_;
    bool hasListener_ = false;
    V3 listenAt_, listenRight_{1, 0, 0};
    float zoom_ = 0;
    uint32_t tick_ = 0;

    // Per soldier (by index), what he was last tick.
    struct Seen {
        bool alive = false, routed = false, wavering = false, wounded = false, galloping = false;
        int32_t rank = 0, kills = 0;
    };
    std::vector<Seen> seen_;
    bool primed_ = false;
    // Missiles in flight last tick (sorted by id): whose, and what.
    struct Flight {
        sim::EntityId id, shooter;
        sim::PlayerId owner;
        sim::ProjectileKind kind;
        V3 pos;
    };
    std::vector<Flight> flights_;
    int lastOutcome_ = 0;
    // The player's losses, for the herald: soldiers seen, and reports made (0-3).
    int strength_ = 0;
    int reported_ = 0;
    uint32_t lastCry_ = 0;
    bool anyCry_ = false;
    bool contact_[2] = {false, false};
    uint32_t lastContact_[2] = {0, 0};
    uint32_t lastFight_ = 0;  // the player's last blow struck or taken

    std::vector<Candidate> wanted_;
    // Throttles, in ticks: when each (side, situation) was last heard, each
    // speaker last spoke, the last ambient cue and those of the last window.
    uint32_t lastSituation_[2][kSituationCount] = {};
    bool heardSituation_[2][kSituationCount] = {};
    std::vector<uint32_t> lastSpoke_;
    uint32_t lastAmbient_ = 0;
    bool anyAmbient_ = false;
    std::deque<uint32_t> window_;
    uint32_t idleSeed_ = 1;

    std::vector<VoiceCue> out_;
    std::deque<VoiceCue> log_;
    int emitted_ = 0, dropped_ = 0;
};

}  // namespace illyria::game
