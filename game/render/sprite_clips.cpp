// Sheets that list their own clips, and how a soldier's frame is chosen
// (sprite_clips.h).

#include "sprite_clips.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "figures.h"

namespace illyria::game::sprites {

namespace {

// Once through, where the clip does not say: the idle as the fixed layout
// breathes (0.45 a second), a pickup about as long as a stoop and a grab.
constexpr float kIdleSeconds = 1.0f / 0.45f;
constexpr float kPickupSeconds = 0.7f;
constexpr float kAirLoops = 1.6f;  // wingbeats (or strides) a second aloft, as the fixed layout

int clampFrame(int i, int frames) { return std::clamp(i, 0, std::max(0, frames - 1)); }

// The time into a clip about its release (releaseTime).
float aroundRelease(float release, float length, float toRelease, float sinceRelease) {
    if (toRelease >= 0 && toRelease <= release) return release - toRelease;
    if (sinceRelease >= 0 && release + sinceRelease < length) return release + sinceRelease;
    return -1;
}

}  // namespace

int SheetClip::frameAt(float t, float len, bool looped) const {
    if (frames <= 1 || !(len > 0)) return 0;
    if (looped) t -= std::floor(t / len) * len;
    if (!(t > 0)) t = 0;
    if (static_cast<int>(times.size()) == frames)
        return clampFrame(static_cast<int>(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1, frames);
    return clampFrame(static_cast<int>(t / len * static_cast<float>(frames)), frames);
}

int SheetClip::frameAtPhase(float phase) const {
    phase -= std::floor(phase);
    if (static_cast<int>(times.size()) == frames && seconds > 0) return frameAt(phase * seconds, seconds, true);
    return clampFrame(static_cast<int>(phase * static_cast<float>(frames)), frames);
}

int SheetClips::framesPerView() const {
    int n = 0;
    for (const SheetClip& c : clips) n += c.frames;
    return n;
}

const SheetClip* SheetClips::find(std::string_view name) const {
    for (const SheetClip& c : clips)
        if (c.name == name) return &c;
    return nullptr;
}

const SheetClip* SheetClips::play(std::string_view name) const {
    for (int k = 0; k < 8 && !name.empty(); ++k, name = fallbackOf(name))
        if (const SheetClip* c = find(name)) return c;
    return nullptr;
}

bool SheetClips::valid(int framesPerView) const {
    if (clips.empty()) return false;
    std::set<std::string> names;
    int first = 0;
    for (const SheetClip& c : clips) {
        if (c.name.empty() || !names.insert(c.name).second || c.first != first || c.frames < 1) return false;
        if (!(c.seconds >= 0) || !(c.stride >= 0)) return false;
        if (c.release >= 0 && c.seconds > 0 && c.release > c.seconds) return false;
        if (!c.times.empty()) {
            if (static_cast<int>(c.times.size()) != c.frames || !(c.times.front() >= 0)) return false;
            for (size_t i = 1; i < c.times.size(); ++i)
                if (!(c.times[i] >= c.times[i - 1])) return false;
        }
        first += c.frames;
    }
    return first == framesPerView;
}

std::string_view fallbackOf(std::string_view name) {
    if (name == "run") return "walk";
    if (name == "celebrate") return "idle";
    if (name == "die_front" || name == "die_crumple") return "die_back";
    if (name == "die_burn") return "die_crumple";
    if (name == "pickup" || name == "throw" || name == "slash") return "attack";
    return {};
}

SheetClips parseClips(std::string_view text) {
    SheetClips out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string line(text.substr(at, end - at));
        at = end + 1;
        std::vector<std::string> words;
        for (size_t i = 0; i < line.size();) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
            size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
            if (j > i) words.push_back(line.substr(i, j - i));
            i = j;
        }
        if (words.empty()) continue;
        if (words.size() < 7) return {};
        std::vector<float> nums;
        for (size_t k = 1; k < words.size(); ++k) {
            char* stop = nullptr;
            const float v = std::strtof(words[k].c_str(), &stop);
            if (!stop || *stop != '\0' || !std::isfinite(v)) return {};
            nums.push_back(v);
        }
        SheetClip c;
        c.name = words[0];
        c.first = static_cast<int>(nums[0]);
        c.frames = static_cast<int>(nums[1]);
        c.seconds = nums[2];
        c.loop = nums[3] != 0;
        c.stride = nums[4];
        c.release = nums[5];
        c.times.assign(nums.begin() + 6, nums.end());
        if (static_cast<float>(c.first) != nums[0] || static_cast<float>(c.frames) != nums[1]) return {};
        out.clips.push_back(std::move(c));
    }
    return out;
}

float clipSpeed(const SheetClip& c) { return c.stride > 0 ? c.stride / c.length(kMoveLoopSeconds) : 0.0f; }

float runThreshold(const SheetClip& walk, const SheetClip& run) {
    const float w = clipSpeed(walk), r = clipSpeed(run);
    return w > 0 && r > 0 ? std::sqrt(w * r) : 0.0f;
}

bool runs(bool wasRunning, float speed, float threshold) {
    if (!(threshold > 0)) return false;
    return speed > threshold * (wasRunning ? 1.0f - kRunBand : 1.0f + kRunBand);
}

float easeSpeed(float speed, float measured, float dt) {
    if (!(dt > 0)) return speed;
    return speed + (measured - speed) * (1.0f - std::exp(-dt / kSpeedEase));
}

float releaseTime(const SheetClip& c, float length, float toRelease, float sinceRelease) {
    if (c.release < 0) return -1;
    return aroundRelease(c.release, length, toRelease, sinceRelease);
}

void observeScavenger(ClipMemory& m, bool holding, bool striking, bool retreating, int timer, uint32_t tick, uint32_t attackTick,
                      float ticksPerSecond) {
    // The attack tick came since the last look: the sim's stoop (a pickup) or drawing back (a throw).
    const bool fresh = m.seen && attackTick != UINT32_MAX && attackTick > m.seenTick;
    if (m.seen && holding && !m.held) {
        // Taken up: from the stoop the sim made for it, else (snatched on the run) now.
        m.pickupAt = static_cast<float>(fresh ? attackTick : tick) / ticksPerSecond;
        if (fresh) m.claimed = attackTick, m.claimedBy = 1;
        m.throwAt = -1;
    }
    if (m.seen && m.held && !holding && (m.striking || retreating)) {
        // Out of his hand: when his wind-up ran out (it was drawn back when last seen), else now.
        m.throwAt = static_cast<float>(m.striking ? std::min(m.releaseTick, tick) : tick) / ticksPerSecond;
        if (!m.striking && fresh) m.claimed = attackTick, m.claimedBy = 2;  // drawn back and thrown between looks
        m.pickupAt = -1;
    }
    if (striking) {
        m.releaseTick = tick + static_cast<uint32_t>(std::max(0, timer));
        // Drawing back set his attack tick (Battle::attackTick: the reload the sim gives the throw).
        if (attackTick != UINT32_MAX && !(m.claimedBy == 1 && m.claimed == attackTick)) m.claimed = attackTick, m.claimedBy = 2;
    }
    m.seen = true;
    m.held = holding;
    m.striking = striking;
    m.seenTick = tick;
}

Shown chooseFrame(const SheetClips& clips, const Deeds& d, ClipMemory& m) {
    const auto shown = [](const SheetClip* c, float t, float len, bool looped) { return Shown{c, c->first + c->frameAt(t, len, looped)}; };
    const SheetClip* run = clips.find("run");
    const SheetClip* walk = clips.play("walk");
    if (!walk) walk = run;
    // On the ground and moving: walk or run by his pace, the loop carried on
    // by the ground he covers (whatever else he is shown doing meanwhile).
    if (d.moving && !d.inAir && walk) {
        m.running = run && run != walk && runs(m.running, m.speed, runThreshold(*walk, *run));
        const SheetClip* gait = m.running ? run : walk;
        const float stride = gait->stride > 0 ? gait->stride : d.stride;
        if (stride > 0) {
            m.phase += d.stepMetres / stride;
            m.phase -= std::floor(m.phase);
        }
    }
    // A scavenger taking something up; throwing it, drawn back and let go.
    if (const SheetClip* c = clips.find("pickup"); c && d.sincePickup >= 0) {
        const float len = c->length(kPickupSeconds);
        if (d.sincePickup < len) return shown(c, d.sincePickup, len, false);
    }
    if (const SheetClip* c = clips.find("throw")) {
        const float len = c->length(d.attackSeconds);
        const float t = aroundRelease(std::max(0.0f, c->release), len, d.toThrow, d.sinceThrow);
        if (t >= 0) return shown(c, t, len, false);
    }
    // The attack: a bow drawn and loosed about its release, else a blow from its start.
    const bool claimed = (d.attackFrom == 1 && clips.find("pickup")) || (d.attackFrom == 2 && clips.find("throw"));
    if (const SheetClip* c = clips.play("attack")) {
        const float len = c->length(d.attackSeconds);
        const float since = claimed ? -1.0f : d.sinceAttack;
        if (c->release >= 0) {
            if (const float t = releaseTime(*c, len, d.toRelease, since); t >= 0) return shown(c, t, len, false);
        } else if (since >= 0 && since < len) {
            return shown(c, since, len, false);
        }
    }
    if (const SheetClip* c = clips.play("flinch"); c && d.sinceHit >= 0) {
        const float len = c->length(d.flinchSeconds);
        if (d.sinceHit < len) return shown(c, d.sinceHit, len, false);
    }
    if ((d.moving || d.inAir) && walk) {
        if (d.inAir) return shown(walk, d.now, walk->length(1.0f / kAirLoops), true);
        const SheetClip* gait = m.running ? run : walk;
        return {gait, gait->first + gait->frameAtPhase(m.phase)};
    }
    // At ease, or cheering a won field (as long as an idle breath, where the clip does not say).
    const SheetClip* rest = clips.play(d.celebrating ? "celebrate" : "idle");
    if (!rest) return {};
    const float len = rest->length(kIdleSeconds);
    return shown(rest, (d.now / len + d.idlePhase) * len, len, true);
}

int fixedLayoutFrame(float struck, float strike, float hurt, bool moving, bool inAir, float walked, float stride,
                     bool prerenderedFlier, float gameSeconds, uint32_t id) {
    int frame;
    if (struck >= 0 && struck < strike) {
        frame = figures::kFirstFrame[2] + std::min(figures::kFrames[2] - 1, static_cast<int>(struck / strike * figures::kFrames[2]));
    } else if (hurt >= 0 && hurt < figures::kFlinchSeconds) {
        frame = figures::kFirstFrame[3] + (hurt < figures::kFlinchSeconds * 0.5f ? 0 : 1);
    } else if (moving || inAir) {
        const float cycle = inAir ? gameSeconds * 1.6f : walked / stride;
        frame = figures::kFirstFrame[1] + static_cast<int>((cycle - std::floor(cycle)) * figures::kFrames[1]) % figures::kFrames[1];
        // A pre-rendered flier's walk holds two cycles: frames 0-3 on foot, 4-7 a wingbeat.
        if (prerenderedFlier) {
            const int half = figures::kFrames[1] / 2;
            const float c = inAir ? gameSeconds * 1.1f : walked / stride;
            frame = figures::kFirstFrame[1] + (inAir ? half : 0) + static_cast<int>((c - std::floor(c)) * half) % half;
        }
    } else {
        const float cycle = gameSeconds * 0.45f + static_cast<float>(id % 13) * 0.37f;
        frame = static_cast<int>((cycle - std::floor(cycle)) * figures::kFrames[0]) % figures::kFrames[0];
    }
    return frame;
}

}  // namespace illyria::game::sprites
