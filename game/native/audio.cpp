#include "audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "assets.h"
#include "audio_mix.h"
#include "illyria/game/underwater_audio.h"
#include "json.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wpedantic"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "third_party/dr_mp3.h"
#pragma clang diagnostic pop

namespace illyria::native::audio {

namespace {

using mixing::MixSettings;

constexpr int kRate = 44100;
constexpr int kChunk = 1024;  // frames mixed at a time
// The pools. A stolen strip fades out rather than stopping dead, so each pool has a few more strips than may play at once:
// the old sound's tail finishes in one while the new sound starts in another.
constexpr int kMaxStrips = 64, kSpare = 6;
constexpr int kMaxVoices = 8, kVoiceSpare = 3;
constexpr int kUiStrips = 4;   // three playing and a spare
constexpr float kWet = 0.55f;  // the reverb's return
constexpr float kPauseFadeMs = 80, kPausedAmbience = 0.35f;
constexpr float kEnemyRate = 0.88f;  // voice.js: the other side sounds darker
constexpr size_t kMaxHeld = 64;

struct Clip {
    std::vector<float> pcm;  // interleaved stereo at kRate
    double loopStart = 0, loopSeconds = 0;
    size_t frames() const { return pcm.size() / 2; }
};
using ClipPtr = std::shared_ptr<const Clip>;

struct Sound {
    std::string id;
    float levelDb = 0;
    std::vector<std::string> files;
    std::vector<ClipPtr> clips;  // as decoded (null while loading or if it failed)
    size_t tried = 0;            // files decoded or failed so far
    int last = -1;
};

struct Strip {
    ClipPtr clip;
    double pos = 0, step = 1, stepBy = 0;  // stepBy: the doppler's slide, a frame
    long slide = 0;                        // frames the slide lasts
    float gain = 0, panL = 1, panR = 1, send = 0;
    mixing::Biquad lowpass;
    bool filtered = false;
    long delay = 0;  // frames to wait
    int priority = -1;
    float fade = 1, fadeBy = 0;  // fading out (stolen, or the game paused)
    bool active = false, releasing = false;
    float remaining() const {
        const double left = clip ? (static_cast<double>(clip->frames()) - pos) / std::max(0.05, step) : 0;
        return static_cast<float>((left + static_cast<double>(delay)) / kRate);
    }
};

struct Layer {
    ClipPtr clip;
    double pos = 0;
    float level = 0, target = 0, pan = 0, gainDb = 1;
    float logged = 0;  // the level last written to the cue log
    bool playing = false;
};

// A cue as it was asked for (and, held, when: its files were not decoded yet).
struct Held {
    std::string id;
    float gain, pan, dist, delay, doppler;
    int priority;
    double asked;
};

struct VoiceLine {
    std::string source;  // the recording, else the placeholder, else empty (not recorded: silent)
    bool recorded = false;
    ClipPtr clip;
    bool tried = false;
};
struct VoiceSituation {
    std::string id;
    std::vector<VoiceLine> lines;
    int last = -1;
};
struct VoiceSet {
    std::string fallback;
    std::vector<VoiceSituation> situations;
};

std::mutex lock;
SDL_AudioStream* stream = nullptr;
MixSettings cfg;
std::map<std::string, Sound> sounds;
bool manifestRead = false, soundsDone = false;
Strip strips[kMaxStrips + kSpare], voiceStrips[kMaxVoices + kVoiceSpare], uiStrips[kUiStrips];
std::map<std::string, Layer> layers;
std::vector<Held> held;
std::map<std::string, VoiceSet> voiceSets;
bool voicesRead = false;
double lastVoice = -1e9;
float vol[4] = {1, 0.8f, 0.8f, 1}, volNow[4] = {1, 0.8f, 0.8f, 1};  // by Bus: asked, and smoothed
bool effectsVolumeChosen = false;  // (else the manifest's mix.sfx_volume is the default)
bool paused = false;
float duckNow = 1;
mixing::Reverb reverb(kRate);
mixing::Limiter limiter(kRate);
float dryBuf[kChunk * 2], sendBuf[kChunk * 2], wetBuf[kChunk * 2];
std::mt19937 rng{20261027};
std::thread loader;
bool closing = false;

// Under water: the filter over the whole mix, and the sounds that are made for it, played after the filter (synthesised).
illyria::game::UnderwaterBus bus(kRate);
std::vector<float> uwLoop, uwPlunge, uwSurfacing;  // stereo interleaved at kRate
float uwAmbience = 0, uwAmbienceNow = 0;           // the loop's asked and its smoothed level
double uwLoopPos = 0;
struct Cue {
    const std::vector<float>* pcm = nullptr;
    size_t pos = 0;
} cues[2];

float dB(float x) { return std::pow(10.0f, x / 20.0f); }
float uniform() { return std::uniform_real_distribution<float>(0, 1)(rng); }
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool isUi(const std::string& id) { return id.rfind("snd_ui_", 0) == 0; }

bool& logOn() {
    static bool on = [] {
        const char* e = std::getenv("ILLYRIA_CUE_LOG");
        return e && *e && *e != '0';
    }();
    return on;
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void say(const char* fmt, ...) {
    if (!logOn()) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}
void sayCue(const Held& c, const char* what) {
    say("cue %s %.2f %.2f %.2f %.2f %d -> %s", c.id.c_str(), static_cast<double>(c.gain), static_cast<double>(c.pan), static_cast<double>(c.dist),
        static_cast<double>(c.delay), c.priority, what);
}

ClipPtr decode(const std::string& path, double loopStart, double loopSeconds) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) return nullptr;
    drmp3_config mp3{};
    drmp3_uint64 frames = 0;
    float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &mp3, &frames, nullptr);
    if (!pcm) return nullptr;
    if (mp3.channels == 0 || mp3.sampleRate == 0 || frames == 0) {
        drmp3_free(pcm, nullptr);
        return nullptr;
    }
    auto clip = std::make_shared<Clip>();
    const double ratio = static_cast<double>(mp3.sampleRate) / kRate;  // source frames per output frame
    const size_t n = static_cast<size_t>(static_cast<double>(frames) / ratio);
    clip->pcm.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const double at = static_cast<double>(i) * ratio;
        const size_t a = std::min(static_cast<size_t>(at), static_cast<size_t>(frames - 1)), b = std::min(a + 1, static_cast<size_t>(frames - 1));
        const float t = static_cast<float>(at - std::floor(at));
        for (int ch = 0; ch < 2; ++ch) {
            const size_t c = mp3.channels == 1 ? 0 : static_cast<size_t>(ch);
            clip->pcm[i * 2 + static_cast<size_t>(ch)] = pcm[a * mp3.channels + c] * (1 - t) + pcm[b * mp3.channels + c] * t;
        }
    }
    drmp3_free(pcm, nullptr);
    clip->loopStart = loopStart;
    clip->loopSeconds = loopSeconds;
    return clip;
}

// --- the strips --------------------------------------------------------------------------------------------------------------

void release(Strip& s, float ms) {
    if (!s.active || s.releasing) return;
    if (s.delay > 0) {  // (not heard yet: nothing to fade)
        s.active = false;
        s.clip.reset();
        return;
    }
    s.releasing = true;
    s.fadeBy = 1.0f / std::max(1.0f, ms * 0.001f * kRate);
}

// A strip of `pool` for a cue of `priority`, with at most `limit` playing at once: a free one, or (all playing) the victim's
// place, the victim fading out in the strip it has. Null: the cue outranks nothing that is playing.
Strip* claim(Strip* pool, int size, int limit, int priority, bool voiceRule) {
    mixing::Playing playing[kMaxStrips + kSpare];
    int index[kMaxStrips + kSpare];
    int n = 0;
    for (int i = 0; i < size; ++i)
        if (pool[i].active && !pool[i].releasing) {
            playing[n] = {pool[i].priority, pool[i].remaining()};
            index[n++] = i;
        }
    if (n >= limit) {
        int victim;
        if (voiceRule) {
            victim = mixing::pickVictim(playing, n, 1 << 20);  // the slightest, whatever it is
            if (victim >= 0 && !mixing::voiceMayReplace(playing[victim].priority, priority)) victim = -1;
        } else {
            victim = mixing::pickVictim(playing, n, priority);
        }
        if (victim < 0) return nullptr;
        release(pool[index[victim]], cfg.fadeMs);
    }
    Strip* faded = nullptr;
    for (int i = 0; i < size; ++i) {
        if (!pool[i].active) return &pool[i];
        if (pool[i].releasing && (!faded || pool[i].fade < faded->fade)) faded = &pool[i];
    }
    return faded;  // (every spare still fading: the one nearest silence)
}

void begin(Strip& s, const ClipPtr& clip, float gain, float pan, mixing::Sweep rate, float delay, int priority) {
    const float p = std::clamp(pan, -1.0f, 1.0f);
    s.clip = clip;
    s.pos = 0;
    s.step = static_cast<double>(rate.from);
    s.slide = rate.from == rate.to ? 0 : static_cast<long>(static_cast<double>(clip->frames()) * 0.9);  // (sfx.js: over 0.9 of its length)
    s.stepBy = s.slide > 0 ? (static_cast<double>(rate.to) - static_cast<double>(rate.from)) / static_cast<double>(s.slide) : 0;
    s.gain = gain;
    s.panL = std::cos((p + 1) * 0.7853982f) * 1.4142f;
    s.panR = std::sin((p + 1) * 0.7853982f) * 1.4142f;
    s.send = 0;
    s.filtered = false;
    s.lowpass.reset();
    s.delay = static_cast<long>(std::max(0.0f, delay) * kRate);
    s.priority = priority;
    s.fade = 1;
    s.fadeBy = 0;
    s.releasing = false;
    s.active = true;
}

// Adds `frames` of a strip to `dry` (panned, times `busGain`) and, if there is one, what it sends to the room to `wet`.
void run(Strip& s, float* dry, float* wet, int frames, float busGain) {
    if (!s.active) return;
    const Clip& c = *s.clip;
    const size_t n = c.frames();
    const float g = s.gain * busGain;
    for (int i = 0; i < frames; ++i) {
        if (s.delay > 0) {
            --s.delay;
            continue;
        }
        const size_t p = static_cast<size_t>(s.pos);
        if (p + 1 >= n) {
            s.active = false;
            break;
        }
        const float t = static_cast<float>(s.pos - std::floor(s.pos));
        float l = c.pcm[p * 2] * (1 - t) + c.pcm[(p + 1) * 2] * t;
        float r = c.pcm[p * 2 + 1] * (1 - t) + c.pcm[(p + 1) * 2 + 1] * t;
        if (s.filtered) {
            l = s.lowpass.run(0, l);
            r = s.lowpass.run(1, r);
        }
        const float f = g * s.fade;
        dry[i * 2] += l * f * s.panL;
        dry[i * 2 + 1] += r * f * s.panR;
        if (wet) {
            wet[i * 2] += l * f * s.send;
            wet[i * 2 + 1] += r * f * s.send;
        }
        s.pos += s.step;
        if (s.slide > 0) {
            --s.slide;
            s.step = std::max(0.05, s.step + s.stepBy);
        }
        if (s.releasing && (s.fade -= s.fadeBy) <= 0) {
            s.active = false;
            break;
        }
    }
    if (!s.active) s.clip.reset();
}

// --- the effects -------------------------------------------------------------------------------------------------------------

// Plays a cue whose sound has a decoded file, `waited` seconds after it was asked for. What became of it, for the log.
const char* start(Sound& snd, const Held& c, float waited) {
    std::vector<int> ready;
    for (size_t i = 0; i < snd.clips.size(); ++i)
        if (snd.clips[i]) ready.push_back(static_cast<int>(i));
    if (ready.empty()) return "dropped:missing";
    int v = ready[static_cast<size_t>(uniform() * static_cast<float>(ready.size())) % ready.size()];
    if (ready.size() > 1 && v == snd.last) v = ready[static_cast<size_t>(std::find(ready.begin(), ready.end(), v) - ready.begin() + 1) % ready.size()];
    const bool ui = isUi(snd.id);
    Strip* s = ui ? claim(uiStrips, kUiStrips, kUiStrips - 1, c.priority, false) : claim(strips, cfg.strips + kSpare, cfg.strips, c.priority, false);
    if (!s) return "dropped:busy";
    snd.last = v;
    // No two alike: a little off pitch (the bowstring more) and off level.
    const float rate = 1 + (uniform() * 2 - 1) * (snd.id.find("bowstring") != std::string::npos ? 0.08f : 0.06f);
    const float level = dB((uniform() * 2 - 1) * 1.5f);
    const mixing::Sweep sweep = c.doppler != 0 ? mixing::dopplerSweep(rate, c.doppler) : mixing::Sweep{rate, rate};
    begin(*s, snd.clips[static_cast<size_t>(v)], c.gain * dB(snd.levelDb) * level, c.pan, sweep, std::max(0.0f, c.delay - waited), c.priority);
    if (!ui) {  // (the HUD's own sounds are not out in the field: no distance, no room)
        s->filtered = true;
        s->lowpass.lowpass(mixing::lowpassHz(cfg, c.dist), kRate);
        s->send = mixing::reverbSend(cfg, c.dist);
    }
    return "played";
}

void startLayer(const std::string& id, Layer& layer) {
    if (layer.playing || layer.target <= 0.01f) return;
    const auto it = sounds.find(id);
    if (it == sounds.end() || it->second.clips.empty() || !it->second.clips[0]) return;
    layer.gainDb = dB(it->second.levelDb);
    layer.clip = it->second.clips[0];
    layer.pos = layer.clip->loopStart * kRate;
    layer.playing = true;
}

// A file of `id` has been decoded (or, id empty, a look over everything still waiting): the cues held for it play, if they are
// not too late; so does a layer asked for before its loop was there.
void arrived(const std::string& id) {
    const double t = now();
    for (size_t i = 0; i < held.size();) {
        const Held& c = held[i];
        const float waited = static_cast<float>(t - c.asked);
        const auto it = sounds.find(c.id);
        const char* what = nullptr;
        char buf[48];
        if (manifestRead && it == sounds.end()) {
            what = "dropped:unknown";
        } else if (!mixing::heldInTime(waited, c.delay)) {
            what = "dropped:late";
        } else if (paused && !isUi(c.id)) {
            what = "dropped:paused";
        } else if (it != sounds.end() && c.id == id) {
            what = start(it->second, c, waited);
            if (what[0] == 'p') {
                std::snprintf(buf, sizeof buf, "played (held %.2f s)", static_cast<double>(waited));
                what = buf;
            }
        } else if (soundsDone) {
            what = "dropped:missing";
        }
        if (!what) {
            ++i;
            continue;
        }
        sayCue(c, what);
        held.erase(held.begin() + static_cast<long>(i));
    }
    if (const auto l = layers.find(id); l != layers.end()) startLayer(id, l->second);
}

double setting(const Json& mix, const char* key, double fallback, double lo, double hi) { return std::clamp(mix.num(key, fallback), lo, hi); }

// The manifest, then every file. (sfx.js loads on demand; all of them are three megabytes of mp3.)
void loadSounds(const std::string& root) {
    std::string text;
    Json manifest, stats;
    if (!readText(root + "/sfx/sounds.json", text) || !Json::parse(text, manifest)) {
        std::fprintf(stderr, "audio: no sfx/sounds.json\n");
        std::lock_guard<std::mutex> l(lock);
        manifestRead = soundsDone = true;
        arrived("");
        return;
    }
    if (readText(root + "/sfx/stats.json", text)) Json::parse(text, stats);
    std::vector<std::pair<std::string, size_t>> order, rest;
    {
        std::lock_guard<std::mutex> l(lock);
        if (const Json* mix = manifest.find("mix")) {  // (optional: the defaults are MixSettings')
            const MixSettings d;
            cfg.strips = static_cast<int>(setting(*mix, "strips", d.strips, 1, kMaxStrips));
            cfg.voiceStrips = static_cast<int>(setting(*mix, "voice_strips", d.voiceStrips, 1, kMaxVoices));
            cfg.fadeMs = static_cast<float>(setting(*mix, "fade_ms", static_cast<double>(d.fadeMs), 1, 500));
            cfg.lowpassNearHz = static_cast<float>(setting(*mix, "lowpass_near_hz", static_cast<double>(d.lowpassNearHz), 100, 20000));
            cfg.lowpassFarHz = static_cast<float>(setting(*mix, "lowpass_far_hz", static_cast<double>(d.lowpassFarHz), 100, 20000));
            cfg.reverbNear = static_cast<float>(setting(*mix, "reverb_near", static_cast<double>(d.reverbNear), 0, 1));
            cfg.reverbFar = static_cast<float>(setting(*mix, "reverb_far", static_cast<double>(d.reverbFar), 0, 1));
            cfg.loopGain = static_cast<float>(setting(*mix, "loop_gain", static_cast<double>(d.loopGain), 0, 16));
            cfg.sfxVolume = static_cast<float>(setting(*mix, "sfx_volume", static_cast<double>(d.sfxVolume), 0, 1));
        }
        if (!effectsVolumeChosen) vol[1] = cfg.sfxVolume;
        for (const Json& s : manifest.list("sounds")) {
            Sound& snd = sounds[s.str("id")];
            snd.id = s.str("id");
            snd.levelDb = static_cast<float>(s.num("level"));
            for (const Json& f : s.list("files")) snd.files.push_back(f.string);
            snd.clips.assign(snd.files.size(), nullptr);
        }
        manifestRead = true;
        // The HUD's own first (the menu is up before anything else is heard), then the rest in order.
        for (auto& [id, s] : sounds)
            for (size_t i = 0; i < s.files.size(); ++i) (isUi(id) ? order : rest).emplace_back(id, i);
        order.insert(order.end(), rest.begin(), rest.end());
        arrived("");  // (cues held for ids the manifest does not have)
    }
    for (const auto& [id, i] : order) {
        std::string file;
        {
            std::lock_guard<std::mutex> l(lock);
            if (closing) return;
            file = sounds[id].files[i];
        }
        double ls = 0, lsec = 0;
        if (const Json* st = stats.find(file)) {
            ls = st->num("loop_start");
            lsec = st->num("loop_seconds");
        }
        ClipPtr clip = decode(root + "/" + file, ls, lsec);
        std::lock_guard<std::mutex> l(lock);
        Sound& snd = sounds[id];
        snd.clips[i] = clip;
        ++snd.tried;
        if (clip || snd.tried == snd.files.size()) arrived(id);
    }
    std::lock_guard<std::mutex> l(lock);
    soundsDone = true;
    arrived("");
}

// The voices: every line with a file to play (a recording listed in voice/available.json, else its rendered placeholder).
void loadVoices(const std::string& root) {
    std::string text;
    Json manifest, have;
    if (!readText(root + "/voice/lines.json", text) || !Json::parse(text, manifest)) {
        std::fprintf(stderr, "audio: no voice/lines.json\n");
        std::lock_guard<std::mutex> l(lock);
        voicesRead = true;
        return;
    }
    std::vector<std::string> available;
    if (readText(root + "/voice/available.json", text) && Json::parse(text, have))
        for (const Json& f : have.items) available.push_back(f.string);
    std::sort(available.begin(), available.end());
    std::map<std::string, VoiceSet> sets;
    struct Job {
        std::string set;
        size_t situation, line;
        std::string source;
    };
    std::vector<Job> jobs;
    for (const Json& s : manifest.list("sets")) {
        const std::string id = s.str("id");
        VoiceSet& set = sets[id];
        set.fallback = s.str("fallback");
        for (const Json& sit : s.list("situations")) {
            VoiceSituation situation;
            situation.id = sit.str("id");
            for (const Json& l : sit.list("lines")) {
                VoiceLine line;
                const std::string file = l.str("file");
                line.recorded = std::binary_search(available.begin(), available.end(), file);
                line.source = line.recorded ? file : l.str("placeholder");
                if (!line.source.empty()) jobs.push_back({id, set.situations.size(), situation.lines.size(), line.source});
                situation.lines.push_back(std::move(line));
            }
            set.situations.push_back(std::move(situation));
        }
    }
    {
        std::lock_guard<std::mutex> l(lock);
        voiceSets = std::move(sets);
        voicesRead = true;
    }
    for (const Job& j : jobs) {
        {
            std::lock_guard<std::mutex> l(lock);
            if (closing) return;
        }
        ClipPtr clip = decode(root + "/" + j.source, 0, 0);
        std::lock_guard<std::mutex> l(lock);
        VoiceLine& line = voiceSets[j.set].situations[j.situation].lines[j.line];
        line.clip = clip;
        line.tried = true;
    }
}

void load(std::string root) {
    loadSounds(root);
    loadVoices(root);
}

// Mixes `frames` frames (at most kChunk) into out (interleaved stereo float), under the lock.
void mix(float* out, int frames) {
    const float seconds = static_cast<float>(frames) / kRate;
    std::fill(out, out + frames * 2, 0.0f);
    for (int b = 0; b < 4; ++b) volNow[b] += (vol[b] - volNow[b]) * (1.0f - std::exp(-seconds / 0.03f));
    duckNow += ((paused ? kPausedAmbience : 1.0f) - duckNow) * (1.0f - std::exp(-seconds / (kPauseFadeMs * 0.001f)));
    const float master = volNow[0], effects = volNow[1] * master, voices = volNow[2] * master;

    // The ambience.
    const float layerStep = 1.0f - std::exp(-seconds / 1.2f);
    for (auto& [id, l] : layers) {
        l.level += (l.target - l.level) * layerStep;
        if (!l.playing || !l.clip) continue;
        if (l.target <= 0.01f && l.level < 0.002f) {
            l.playing = false;
            continue;
        }
        const Clip& c = *l.clip;
        const float a = std::cos((l.pan + 1) * 0.7853982f), b = std::sin((l.pan + 1) * 0.7853982f);
        const float g = l.level * l.gainDb * cfg.loopGain * volNow[3] * master * duckNow;
        const size_t n = c.frames();
        const size_t loopFrom = static_cast<size_t>(c.loopStart * kRate);
        size_t loopTo = c.loopSeconds > 0 ? std::min(n, static_cast<size_t>((c.loopStart + c.loopSeconds) * kRate)) : n;
        if (loopTo <= loopFrom + 1) loopTo = n;
        for (int i = 0; i < frames; ++i) {
            size_t p = static_cast<size_t>(l.pos);
            if (p >= loopTo) {
                l.pos = static_cast<double>(loopFrom);
                p = loopFrom;
            }
            out[i * 2] += c.pcm[p * 2] * g * a * 1.4142f;
            out[i * 2 + 1] += c.pcm[p * 2 + 1] * g * b * 1.4142f;
            l.pos += 1;
        }
    }
    // The effects, and what each sends to the one room.
    std::fill(dryBuf, dryBuf + frames * 2, 0.0f);
    std::fill(sendBuf, sendBuf + frames * 2, 0.0f);
    for (int i = 0; i < cfg.strips + kSpare; ++i) run(strips[i], dryBuf, sendBuf, frames, 1);
    reverb.process(sendBuf, wetBuf, frames);
    for (int i = 0; i < frames * 2; ++i) out[i] += (dryBuf[i] + wetBuf[i] * kWet) * effects;
    for (int i = 0; i < cfg.voiceStrips + kVoiceSpare; ++i) run(voiceStrips[i], out, nullptr, frames, voices);

    bus.process(out, frames);
    // The underwater loop and the cues over it, not filtered again (the loop is the water's own, the cues are the crossing of it).
    uwAmbienceNow += (uwAmbience - uwAmbienceNow) * (1.0f - std::exp(-seconds / 0.25f));
    if (uwAmbienceNow > 0.002f && !uwLoop.empty()) {
        const size_t n = uwLoop.size() / 2;
        const float g = uwAmbienceNow * 0.55f * effects;
        for (int i = 0; i < frames; ++i) {
            const size_t p = static_cast<size_t>(uwLoopPos) % n;
            out[i * 2] += uwLoop[p * 2] * g;
            out[i * 2 + 1] += uwLoop[p * 2 + 1] * g;
            uwLoopPos += 1;
        }
    }
    for (Cue& c : cues) {
        if (!c.pcm) continue;
        const size_t n = c.pcm->size() / 2;
        for (int i = 0; i < frames && c.pos < n; ++i, ++c.pos) {
            out[i * 2] += (*c.pcm)[c.pos * 2] * 0.5f * effects;
            out[i * 2 + 1] += (*c.pcm)[c.pos * 2 + 1] * 0.5f * effects;
        }
        if (c.pos >= n) c.pcm = nullptr;
    }
    // The HUD's own sounds: straight to the output.
    for (Strip& s : uiStrips) run(s, out, nullptr, frames, effects);
    limiter.process(out, frames);  // not touched below -3 dBFS, never past -0.3
}

void SDLCALL feed(void*, SDL_AudioStream* s, int additional, int) {
    float buf[kChunk * 2];
    int frames = additional / static_cast<int>(sizeof(float) * 2);
    while (frames > 0) {
        const int n = std::min(frames, kChunk);
        {
            std::lock_guard<std::mutex> l(lock);
            mix(buf, n);
        }
        SDL_PutAudioStreamData(s, buf, n * static_cast<int>(sizeof(float)) * 2);
        frames -= n;
    }
}

void setLayer(const char* id, float level, float pan) {
    Layer& layer = layers[id];
    layer.target = std::clamp(level, 0.0f, 1.0f);
    layer.pan = std::clamp(pan, -1.0f, 1.0f);
    if (std::fabs(layer.target - layer.logged) > 0.05f) {
        layer.logged = layer.target;
        say("layer %s %.2f%s", id, static_cast<double>(layer.target), manifestRead && !sounds.count(id) ? " dropped:unknown" : "");
    }
    startLayer(id, layer);
}

}  // namespace

bool open(const char* assetRoot) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "audio: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kRate;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
    if (!stream) {
        std::fprintf(stderr, "audio: %s\n", SDL_GetError());
        return false;
    }
    uwLoop = illyria::game::synthUnderwaterAmbience(kRate);
    uwPlunge = illyria::game::synthPlunge(kRate);
    uwSurfacing = illyria::game::synthSurfacing(kRate);
    loader = std::thread(load, std::string(assetRoot));
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void close() {
    {
        std::lock_guard<std::mutex> l(lock);
        closing = true;
    }
    if (loader.joinable()) loader.join();
    if (stream) SDL_DestroyAudioStream(stream);
    stream = nullptr;
}

void sfxCue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority) {
    const Held c{id, gain, pan, dist, delay, doppler, priority, now()};
    if (!stream) return sayCue(c, "dropped:no audio");
    std::lock_guard<std::mutex> l(lock);
    if (paused && !isUi(c.id)) return sayCue(c, "dropped:paused");
    const auto it = sounds.find(c.id);
    if (manifestRead && it == sounds.end()) return sayCue(c, "dropped:unknown");
    if (gain * vol[1] < 0.01f) return sayCue(c, "dropped:quiet");
    if (it != sounds.end()) {
        Sound& snd = it->second;
        if (std::any_of(snd.clips.begin(), snd.clips.end(), [](const ClipPtr& p) { return p != nullptr; })) return sayCue(c, start(snd, c, 0));
        if (snd.tried == snd.files.size()) return sayCue(c, "dropped:missing");
    }
    // Its files are still being decoded: held, and played when they arrive (arrived) if that is soon enough.
    if (held.size() >= kMaxHeld) return sayCue(c, "dropped:loading");
    held.push_back(c);
    sayCue(c, "held");
}

void uiCue(const char* id) { sfxCue(id, 1, 0, 0, 0, 0, 1); }

void voiceCue(const char* setId, const char* situation, bool enemy, float pan, float gain, int priority) {
    const auto report = [&](const char* what) { say("voice %s/%s -> %s", setId, situation, what); };
    if (!stream) return report("dropped:no audio");
    std::lock_guard<std::mutex> l(lock);
    if (paused) return report("dropped:paused");
    if (!voicesRead) return report("dropped:loading");
    // The lines for the situation in this voice, or its fallback's (voice.js linesFor).
    VoiceSituation* found = nullptr;
    std::string at = setId;
    for (int hops = 0; hops < 3 && !found && !at.empty(); ++hops) {
        const auto s = voiceSets.find(at);
        if (s == voiceSets.end()) break;
        for (VoiceSituation& sit : s->second.situations)
            if (sit.id == situation) found = &sit;
        at = s->second.fallback;
    }
    if (!found || found->lines.empty()) return report("dropped:no lines");
    if (gain * vol[2] < 0.02f) return report("dropped:quiet");
    const double t = now();
    if (mixing::voiceGapApplies(priority) && t - lastVoice < static_cast<double>(mixing::kVoiceGapSeconds)) return report("dropped:gap");
    // Not the line said last (voice.js pickVariant).
    const int n = static_cast<int>(found->lines.size());
    int v = std::min(n - 1, static_cast<int>(uniform() * static_cast<float>(n)));
    if (n > 1 && v == found->last) v = (v + 1 + std::min(n - 2, static_cast<int>(uniform() * static_cast<float>(n - 1)))) % n;
    found->last = v;
    const VoiceLine& line = found->lines[static_cast<size_t>(v)];
    if (line.source.empty()) return report("unrecorded");  // (no speech synthesis on the desktop: silent until it is recorded)
    if (!line.clip) return report(line.tried ? "dropped:missing" : "dropped:loading");
    Strip* s = claim(voiceStrips, cfg.voiceStrips + kVoiceSpare, cfg.voiceStrips, priority, true);
    if (!s) return report("dropped:busy");
    const float rate = enemy ? kEnemyRate : 1.0f;
    begin(*s, line.clip, gain, pan, {rate, rate}, 0, priority);
    lastVoice = t;
    report(line.recorded ? "file" : "placeholder");
}

void layer(const char* id, float level, float pan) {
    if (!stream) {
        // (no mixer: the log still says what was asked, a change at a time)
        if (!logOn()) return;
        static std::map<std::string, float> logged;
        float& was = logged[id];
        if (std::fabs(level - was) > 0.05f) {
            was = level;
            say("layer %s %.2f dropped:no audio", id, static_cast<double>(level));
        }
        return;
    }
    std::lock_guard<std::mutex> l(lock);
    setLayer(id, level, pan);
}

void ambience(float windMountain, float windValley, float swamp, float swampPan, float flies, float fliesPan, float drums, float drumsPan,
              float rain) {
    const float light = rain > 0 ? std::min(1.0f, rain / 0.5f) * (rain < 0.6f ? 1.0f : 1 - (rain - 0.6f) / 0.4f * 0.7f) : 0.0f;
    layer("snd_amb_wind_mountain", windMountain, 0);
    layer("snd_amb_wind_valley", windValley, 0);
    layer("snd_amb_swamp_insects", swamp, swampPan);
    layer("snd_amb_carrion_flies", flies, fliesPan);
    layer("snd_amb_war_drums", drums, drumsPan);
    layer("snd_amb_rain_light", light, 0);
    layer("snd_amb_rain_heavy", std::max(0.0f, (rain - 0.45f) / 0.55f), 0);
}

void setPaused(bool on) {
    std::lock_guard<std::mutex> l(lock);
    if (paused == on) return;
    paused = on;
    say("paused %d", on ? 1 : 0);
    if (!on) return;
    for (Strip& s : strips) release(s, kPauseFadeMs);
    for (Strip& s : voiceStrips) release(s, kPauseFadeMs);
    for (size_t i = 0; i < held.size();) {  // (nothing waits to burst out on resuming)
        if (isUi(held[i].id)) {
            ++i;
            continue;
        }
        sayCue(held[i], "dropped:paused");
        held.erase(held.begin() + static_cast<long>(i));
    }
}

void setVolume(Bus b, float volume) {
    std::lock_guard<std::mutex> l(lock);
    vol[static_cast<int>(b)] = std::clamp(volume, 0.0f, 1.0f);
    if (b == Bus::Effects) effectsVolumeChosen = true;
}

float volume(Bus b) {
    std::lock_guard<std::mutex> l(lock);
    return vol[static_cast<int>(b)];
}

void setCueLog(bool on) { logOn() = on; }
bool cueLog() { return logOn(); }

void underwater(float cutoffHz, float gain, float wobble, float ambience) {
    if (!stream) return;
    illyria::game::UnderwaterSound s;
    s.cutoffHz = cutoffHz;
    s.gain = gain;
    s.wobble = wobble;
    std::lock_guard<std::mutex> l(lock);
    bus.set(s);
    uwAmbience = ambience;
}

void underwaterCue(bool entering) {
    if (!stream) return;
    std::lock_guard<std::mutex> l(lock);
    Cue& c = cues[entering ? 0 : 1];
    c.pcm = entering ? &uwPlunge : &uwSurfacing;
    c.pos = 0;
}

}  // namespace illyria::native::audio
