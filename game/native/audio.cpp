#include "audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "assets.h"
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

constexpr int kRate = 44100;
constexpr int kStrips = 12;  // sfx.js kMaxSounds
// sfx.js settings: volume 0.8 for the effects, 1 for the ambience.
constexpr float kSfxVolume = 0.8f;

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
    int last = -1;
};

struct Strip {
    ClipPtr clip;
    double pos = 0, step = 1;
    float gl = 0, gr = 0, alpha = 1, lpl = 0, lpr = 0;
    long delay = 0;  // frames to wait
    int priority = -1;
    bool active = false;
};

struct Layer {
    ClipPtr clip;
    double pos = 0;
    float level = 0, target = 0, pan = 0, gainDb = 1;
    bool playing = false;
};

std::mutex lock;
SDL_AudioStream* stream = nullptr;
std::map<std::string, Sound> sounds;
Strip strips[kStrips];
std::map<std::string, Layer> layers;
float master = 1;
std::mt19937 rng{20261027};
std::thread loader;
bool closing = false;

float dB(float x) { return std::pow(10.0f, x / 20.0f); }
float uniform() { return std::uniform_real_distribution<float>(0, 1)(rng); }

ClipPtr decode(const std::string& path, double loopStart, double loopSeconds) {
    std::vector<uint8_t> data;
    if (!readFile(path, data)) return nullptr;
    drmp3_config cfg{};
    drmp3_uint64 frames = 0;
    float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &cfg, &frames, nullptr);
    if (!pcm || cfg.channels == 0 || cfg.sampleRate == 0) return nullptr;
    auto clip = std::make_shared<Clip>();
    const double ratio = static_cast<double>(cfg.sampleRate) / kRate;  // source frames per output frame
    const size_t n = static_cast<size_t>(static_cast<double>(frames) / ratio);
    clip->pcm.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const double at = static_cast<double>(i) * ratio;
        const size_t a = std::min(static_cast<size_t>(at), static_cast<size_t>(frames - 1)), b = std::min(a + 1, static_cast<size_t>(frames - 1));
        const float t = static_cast<float>(at - std::floor(at));
        for (int ch = 0; ch < 2; ++ch) {
            const size_t c = cfg.channels == 1 ? 0 : static_cast<size_t>(ch);
            clip->pcm[i * 2 + static_cast<size_t>(ch)] = pcm[a * cfg.channels + c] * (1 - t) + pcm[b * cfg.channels + c] * t;
        }
    }
    drmp3_free(pcm, nullptr);
    clip->loopStart = loopStart;
    clip->loopSeconds = loopSeconds;
    return clip;
}

// The manifest, then every file. (sfx.js loads on demand; all of them are three megabytes of mp3.)
void load(std::string root) {
    std::string text;
    Json manifest, stats;
    if (!readText(root + "/sfx/sounds.json", text) || !Json::parse(text, manifest)) {
        std::fprintf(stderr, "audio: no sfx/sounds.json\n");
        return;
    }
    if (readText(root + "/sfx/stats.json", text)) Json::parse(text, stats);
    {
        std::lock_guard<std::mutex> l(lock);
        for (const Json& s : manifest.list("sounds")) {
            Sound& snd = sounds[s.str("id")];
            snd.id = s.str("id");
            snd.levelDb = static_cast<float>(s.num("level"));
            for (const Json& f : s.list("files")) snd.files.push_back(f.string);
            snd.clips.assign(snd.files.size(), nullptr);
        }
    }
    // The core ones first would be sfx.js's kCore; the whole set takes well under a second, so all in order.
    std::vector<std::pair<std::string, size_t>> order;
    {
        std::lock_guard<std::mutex> l(lock);
        for (auto& [id, s] : sounds)
            for (size_t i = 0; i < s.files.size(); ++i) order.emplace_back(id, i);
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
        sounds[id].clips[i] = clip;
    }
}

// Mixes `frames` frames into out (interleaved stereo float), under the lock.
void mix(float* out, int frames) {
    std::fill(out, out + frames * 2, 0.0f);
    const float layerStep = 1.0f - std::exp(-static_cast<float>(frames) / kRate / 1.2f);
    for (auto& [id, l] : layers) {
        l.level += (l.target - l.level) * layerStep;
        if (!l.playing || !l.clip) continue;
        if (l.target <= 0.01f && l.level < 0.002f) {
            l.playing = false;
            continue;
        }
        const Clip& c = *l.clip;
        const float a = std::cos((l.pan + 1) * 0.7853982f), b = std::sin((l.pan + 1) * 0.7853982f);
        const float g = l.level * l.gainDb * 4 * master;
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
    for (Strip& s : strips) {
        if (!s.active) continue;
        const Clip& c = *s.clip;
        const size_t n = c.frames();
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
            const float l = c.pcm[p * 2] * (1 - t) + c.pcm[(p + 1) * 2] * t;
            const float r = c.pcm[p * 2 + 1] * (1 - t) + c.pcm[(p + 1) * 2 + 1] * t;
            s.lpl += (l - s.lpl) * s.alpha;
            s.lpr += (r - s.lpr) * s.alpha;
            out[i * 2] += s.lpl * s.gl;
            out[i * 2 + 1] += s.lpr * s.gr;
            s.pos += s.step;
        }
    }
    for (int i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i]);  // the limiter: nothing past full scale
}

void SDLCALL feed(void*, SDL_AudioStream* s, int additional, int) {
    constexpr int kChunk = 1024;
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
    if (!stream || gain * kSfxVolume < 0.01f) return;
    std::lock_guard<std::mutex> l(lock);
    const auto it = sounds.find(id);
    if (it == sounds.end()) return;
    Sound& snd = it->second;
    std::vector<int> ready;
    for (size_t i = 0; i < snd.clips.size(); ++i)
        if (snd.clips[i]) ready.push_back(static_cast<int>(i));
    if (ready.empty()) return;  // still loading
    int v = ready[static_cast<size_t>(uniform() * static_cast<float>(ready.size())) % ready.size()];
    if (ready.size() > 1 && v == snd.last) v = ready[(std::find(ready.begin(), ready.end(), v) - ready.begin() + 1) % static_cast<long>(ready.size())];
    snd.last = v;
    // A free strip, else the quietest-priority one that this outranks (sfx.js strip).
    Strip* best = nullptr;
    for (Strip& s : strips) {
        if (!s.active) {
            best = &s;
            break;
        }
        if (!best || s.priority < best->priority) best = &s;
    }
    if (best->active && best->priority > priority) return;
    const ClipPtr& clip = snd.clips[static_cast<size_t>(v)];
    const float rate = 1 + (uniform() * 2 - 1) * (snd.id.find("bowstring") != std::string::npos ? 0.05f : 0.035f);
    const float bend = std::clamp(doppler / 120.0f, -0.35f, 0.35f);  // (sfx.js ramps this over the sound; held at its middle here)
    const float d = std::clamp(dist, 0.0f, 1.0f);
    const float cutoff = 800 + 17000 * (1 - d) * (1 - d);
    const float p = std::clamp(pan, -1.0f, 1.0f);
    const float g = gain * dB(snd.levelDb) * kSfxVolume * master;
    Strip& s = *best;
    s.clip = clip;
    s.pos = 0;
    s.step = rate * (1 + bend * 0.1f);
    s.gl = g * std::cos((p + 1) * 0.7853982f) * 1.4142f;
    s.gr = g * std::sin((p + 1) * 0.7853982f) * 1.4142f;
    s.alpha = 1 - std::exp(-2 * 3.1415927f * cutoff / kRate);
    s.lpl = s.lpr = 0;
    s.delay = static_cast<long>(std::max(0.0f, delay) * kRate);
    s.priority = priority;
    s.active = true;
}

void ambience(float windMountain, float windValley, float swamp, float swampPan, float flies, float fliesPan, float drums, float drumsPan,
              float rain) {
    if (!stream) return;
    const float light = rain > 0 ? std::min(1.0f, rain / 0.5f) * (rain < 0.6f ? 1.0f : 1 - (rain - 0.6f) / 0.4f * 0.7f) : 0.0f;
    const struct {
        const char* id;
        float level, pan;
    } set[] = {{"snd_amb_wind_mountain", windMountain, 0},  {"snd_amb_wind_valley", windValley, 0}, {"snd_amb_swamp_insects", swamp, swampPan},
               {"snd_amb_carrion_flies", flies, fliesPan}, {"snd_amb_war_drums", drums, drumsPan}, {"snd_amb_rain_light", light, 0},
               {"snd_amb_rain_heavy", std::max(0.0f, (rain - 0.45f) / 0.55f), 0}};
    std::lock_guard<std::mutex> l(lock);
    for (const auto& a : set) {
        const auto it = sounds.find(a.id);
        if (it == sounds.end() || it->second.clips.empty()) continue;
        Layer& layer = layers[a.id];
        layer.target = a.level;
        layer.pan = a.pan;
        layer.gainDb = dB(it->second.levelDb);
        if (a.level > 0.01f && !layer.playing && it->second.clips[0]) {
            layer.clip = it->second.clips[0];
            layer.pos = layer.clip->loopStart * kRate;
            layer.playing = true;
        }
    }
}

void setMasterVolume(float volume) {
    std::lock_guard<std::mutex> l(lock);
    master = std::clamp(volume, 0.0f, 1.0f);
}

float masterVolume() { return master; }

}  // namespace illyria::native::audio
