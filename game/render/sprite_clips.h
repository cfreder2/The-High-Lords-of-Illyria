#pragma once

// Sheets that list their own clips (tools/sprites/import.py, from a unit's
// clips.json; docs/SPRITE_SPEC.md): any clips, of any number of frames, each
// with its own timing, in place of sprite_layout.h's fixed eight. The sheet
// JSON's "clips" reach the renderer as text (parseClips, from
// game/web/sheets.cpp); the renderer plays them by name, a clip the sheet
// lacks falling back to one it has (fallbackOf).
//
// And how a soldier's frame is chosen, from what the renderer sees of him
// (Deeds) and remembers between frames (ClipMemory): on such a sheet
// (chooseFrame), and on the fixed layout as it always was (fixedLayoutFrame).
// No GL here: tests/test_sprite_clips.cpp checks it all natively.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace illyria::game::sprites {

struct SheetClip {
    std::string name;
    int first = 0;  // its first frame within a view
    int frames = 0;
    float seconds = 0;   // once through (0: not given)
    bool loop = false;
    float stride = 0;    // metres of ground one loop covers (0: not given)
    float release = -1;  // seconds into the clip the missile (or the thrown thing) leaves (<0: none)
    std::vector<float> times;  // when each frame shows, seconds from the clip's start (empty: evenly)

    // Once through: `seconds`, else `fallback`.
    float length(float fallback) const { return seconds > 0 ? seconds : fallback; }
    // The frame (0 .. frames-1) shown t seconds in, once through taking `length`:
    // looped (whatever the clip says) or held on the last.
    int frameAt(float t, float length, bool looped) const;
    // The frame at `phase` (0 .. 1) through one loop (a stride, by distance).
    int frameAtPhase(float phase) const;
};

class SheetClips {
public:
    std::vector<SheetClip> clips;

    bool empty() const { return clips.empty(); }
    int framesPerView() const;
    // The clip of that name, or null.
    const SheetClip* find(std::string_view name) const;
    // The clip played for `name`: itself, else down its fallbacks (fallbackOf); null if none.
    const SheetClip* play(std::string_view name) const;
    // In atlas order with no gap or overlap, framesPerView frames in all, each
    // clip's times one a frame and rising, its release inside it.
    bool valid(int framesPerView) const;
};

// Every clip the game plays a soldier, by name, in the order the sandbox
// offers them: a sheet may list others of its own besides.
constexpr const char* kKnownClips[] = {"idle",   "walk",      "run",      "attack",    "throw",       "pickup",
                                       "flinch", "celebrate", "die_back", "die_front", "die_crumple", "die_burn"};

// What a sheet without the clip `name` plays instead ("" nothing): run the
// walk, celebrate the idle, the falls die_back (burning: die_crumple first),
// a scavenger's pickup and throw (and a slash) the attack.
std::string_view fallbackOf(std::string_view name);

// "clips" as game/web/sheets.cpp hands them over, a clip a line:
//     name first frames seconds loop stride release [t0 t1 ...]
// (loop 0 or 1; seconds and stride 0, release -1 when not given; then the
// times, if any). Empty when malformed.
SheetClips parseClips(std::string_view text);

// Seconds a moving clip takes once through when it does not say (a walk's
// stride, as the sandbox has always played it).
constexpr float kMoveLoopSeconds = 1.0f;
// Seconds the smoothed ground speed takes to follow the soldier's.
constexpr float kSpeedEase = 0.3f;
// Walk or run: either side of the threshold by this share before changing.
constexpr float kRunBand = 0.12f;

// A moving clip's own pace (m/s): its stride over its length.
float clipSpeed(const SheetClip& c);
// The pace above which a soldier runs: the geometric mean of the walk's and
// the run's, so he changes where each is as far off as the other.
float runThreshold(const SheetClip& walk, const SheetClip& run);
// Running at `speed` (smoothed, m/s): a walker runs once above the threshold
// by kRunBand, a runner walks once below it by kRunBand.
bool runs(bool wasRunning, float speed, float threshold);
// The smoothed speed `dt` seconds on, eased toward `measured`.
float easeSpeed(float speed, float measured, float dt);

// A clip that lets go of something at `release` seconds in: the time into
// it to show. toRelease: seconds until the next release, while he is set to
// (else negative): the frames before it, so the release frame comes as it
// does. sinceRelease: seconds since the last (negative: none): the frames
// after it. Negative when neither (the clip is not playing). The draw comes
// first where they meet, so a short reload cuts the follow-through.
float releaseTime(const SheetClip& c, float length, float toRelease, float sinceRelease);
// Seconds until a countdown of the sim's lets go: `ticksLeft` of it to run
// as of the last tick, `alpha` of the next one gone (Battle::alpha). A
// missile soldier looses in the tick his reload reaches 0, a scavenger
// throws in the tick his wind-up does (sim::World). Negative: run out.
inline float secondsToRelease(int ticksLeft, float alpha, float ticksPerSecond) {
    if (ticksLeft <= 0) return -1;
    const float s = (static_cast<float>(ticksLeft) - alpha) / ticksPerSecond;
    return s > 0 ? s : 0.0f;
}

// What the renderer knows of a living soldier this frame (game seconds; a
// negative "since" or "to": not happening).
struct Deeds {
    float now = 0;
    float sinceAttack = -1;  // since he last struck or loosed (Battle::attackTick)
    // That attack tick was his stoop for a pickup (1) or his drawing back
    // for a throw (2), not a blow (0): on a sheet with that clip, it plays instead.
    int attackFrom = 0;
    float sinceHit = -1;
    bool moving = false, inAir = false;
    float stepMetres = 0;    // ground covered since the last frame
    float toRelease = -1;    // missile troops set to loose: seconds to the next
    float sincePickup = -1;  // a scavenger: since he took something up
    float toThrow = -1;      // drawn back to throw: seconds until it leaves his hand
    float sinceThrow = -1;   // since it did
    bool celebrating = false;  // his side has won
    float idlePhase = 0;       // loops his idle is offset by, so a rank does not breathe as one
    // The type's own, where a clip gives none (figures.h).
    float attackSeconds = 0.9f, flinchSeconds = 0.3f, stride = 1.5f;
};

// What the renderer remembers of a soldier between frames.
struct ClipMemory {
    float speed = 0;       // smoothed ground speed, m/s
    bool running = false;  // the run shown, not the walk
    float phase = 0;       // through the walk (or run) loop, 0 .. 1
    // A scavenger (sim::Unit::scav) as last seen.
    bool seen = false, held = false, striking = false;
    uint32_t seenTick = 0, releaseTick = 0;
    float pickupAt = -1, throwAt = -1;  // game seconds
    uint32_t claimed = UINT32_MAX;      // the attack tick a pickup's stoop or a throw's drawing back made
    int claimedBy = 0;                  // 1 pickup, 2 throw
    // Missile troops: whether he will loose when his reload ends, and when that was worked out.
    bool engaged = false;
    uint32_t engagedTick = UINT32_MAX;
};

// A scavenger seen at `tick` (holding something; drawn back to throw it,
// `timer` ticks of his wind-up left; falling back after a throw), his last
// attack tick `attackTick` (UINT32_MAX: none): what he took up and threw
// since the last look, and when. A thing taken up has its pickup from the
// sim's stoop (his attack tick then) or, snatched on the run, from now; a
// thing thrown leaves his hand as his wind-up runs out.
void observeScavenger(ClipMemory& m, bool holding, bool striking, bool retreating, int timer, uint32_t tick, uint32_t attackTick,
                      float ticksPerSecond);

struct Shown {
    const SheetClip* clip = nullptr;
    int frame = 0;  // within the view: clip->first + the frame in the clip
};
// A living soldier's clip and frame on a sheet that lists its own. Moving,
// it moves his stride on by d.stepMetres and chooses walk or run (in m).
// First of: a pickup, a throw, the attack (drawn, loosed or struck), a
// flinch, the walk or run, the celebration, the idle.
Shown chooseFrame(const SheetClips& clips, const Deeds& d, ClipMemory& m);

// The frame within a view a living soldier shows on a sheet of the fixed
// layout (sprite_layout.h), as the renderer has always chosen it: striking
// (struck seconds into a stroke of strike), hurt, walking `walked` metres of
// `stride` strides (in the air by time; a pre-rendered flier's walk 0-3 on
// foot, 4-7 a wingbeat), else at ease.
int fixedLayoutFrame(float struck, float strike, float hurt, bool moving, bool inAir, float walked, float stride,
                     bool prerenderedFlier, float gameSeconds, uint32_t id);

}  // namespace illyria::game::sprites
