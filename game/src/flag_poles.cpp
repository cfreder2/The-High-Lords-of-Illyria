#include "illyria/game/flag_poles.h"

#include <algorithm>

namespace illyria::game {

namespace {
bool same(const V3& a, const V3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
}  // namespace

PoleShown PoleTween::advance(float dt, const PoleTarget& want) {
    const float level = std::clamp(want.level, 0.0f, 1.0f);
    if (!started_) {
        started_ = true;
        colour_ = want.colour;
        raised_ = level;
    } else if (!same(colour_, want.colour)) {
        // The old holder's flag comes all the way down before the new one goes up.
        raised_ = std::max(0.0f, raised_ - dt / kLowerSeconds);
        if (raised_ <= 0.0f) colour_ = want.colour;
    } else if (raised_ < level) {
        raised_ = std::min(level, raised_ + dt / kRaiseSeconds);
    } else {
        raised_ = std::max(level, raised_ - dt / kLowerSeconds);
    }
    return {colour_, raised_};
}

PoleShown PoleSet::advance(uint64_t key, float dt, const PoleTarget& want) {
    Entry& e = poles_[key];
    e.seen = true;
    return e.tween.advance(dt, want);
}

void PoleSet::endFrame() {
    for (auto it = poles_.begin(); it != poles_.end();) {
        if (!it->second.seen) {
            it = poles_.erase(it);
        } else {
            it->second.seen = false;
            ++it;
        }
    }
}

}  // namespace illyria::game
