#pragma once

// One flag to a pole (docs/GAME_DESIGN.md §4.1.3, §4.2.8, §4.5). The
// simulation says who holds a pole and how high that holder's flag should
// stand (a siege's capture progress, a banner rising over a town, the horde's
// hold on the hill, a fallen team's flag); this is the presentation that
// shows it. When the holder changes the old flag is lowered to the foot of the
// pole, and only then the new one is raised, each taking kLowerSeconds /
// kRaiseSeconds: there are never two flags on a pole. It reads sim state and
// never writes it (the lockstep checksums cannot move), and runs on the
// frame's real time.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "illyria/game/gmath.h"

namespace illyria::game {

// What the simulation wants flying on a pole: the holder's colour and how far
// up (0 to 1). A different colour is a different holder.
struct PoleTarget {
    V3 colour;
    float level = 1;
};

// A pole being taken, `progress` (0 to 1) of the way from `holder` to `taker`:
// the first half lowers the holder's flag, the second raises the taker's. One
// flag, whatever the progress (a capture point, a banner over a town, a hill).
inline PoleTarget contestedPole(const V3& holder, const V3& taker, float progress) {
    const float p = progress < 0 ? 0 : progress > 1 ? 1 : progress;
    return p < 0.5f ? PoleTarget{holder, 1 - 2 * p} : PoleTarget{taker, 2 * p - 1};
}

// The one flag actually drawn: its colour and how far up the pole it is.
struct PoleShown {
    V3 colour;
    float raised = 1;
    // Lowered to nothing between the old flag and the new: nothing is drawn.
    bool visible() const { return raised > 0.0f; }
};

class PoleTween {
public:
    // A full pole's climb or descent, seconds.
    static constexpr float kLowerSeconds = 1.5f, kRaiseSeconds = 1.5f;

    // Advance `dt` seconds towards `want`. The first call shows it at once.
    PoleShown advance(float dt, const PoleTarget& want);

private:
    bool started_ = false;
    V3 colour_;
    float raised_ = 1;
};

// The tweens of all of a scene's poles, by key (kind and index chosen by the
// caller; stable from frame to frame). Poles not asked for in a frame are forgotten.
class PoleSet {
public:
    PoleShown advance(uint64_t key, float dt, const PoleTarget& want);
    // Call once a frame, after all advance()s: drops the poles not seen in it.
    void endFrame();

private:
    struct Entry {
        PoleTween tween;
        bool seen = false;
    };
    std::unordered_map<uint64_t, Entry> poles_;
};

}  // namespace illyria::game
