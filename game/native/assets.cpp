#include "assets.h"

#include <webp/decode.h>

#include <condition_variable>
#include <cstdio>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "json.h"

// The page's entry points for what has been fetched (game/app/sheets.cpp, models.cpp).
extern "C" {
int illyria_sheet_loaded3(int type, const uint8_t* rgba, int width, int height, const float* cells, int cellCount, int framesPerView, int closeup,
                          const char* clips);
void illyria_sheet_closeup_listed(int type, int width, int height, float ppm);
int illyria_model_loaded(int kind, const uint8_t* mesh, int meshBytes, const uint8_t* rgba, int width, int height);
}

namespace illyria::native {

namespace {

std::string root;

std::string join(const std::string& a, const std::string& b) { return (std::filesystem::path(a) / b).string(); }

struct Pixels {
    uint8_t* rgba = nullptr;
    int w = 0, h = 0;
    ~Pixels() { WebPFree(rgba); }
    bool decode(const std::vector<uint8_t>& data) {
        rgba = WebPDecodeRGBA(data.data(), data.size(), &w, &h);  // straight alpha, top row first, as the page's canvas gives it
        return rgba != nullptr;
    }
};

// One thread does the reading and decoding; finished work waits for the main thread.
std::mutex lock;
std::condition_variable wake;
std::deque<std::function<void()>> todo;      // on the thread
std::deque<std::function<void()>> finished;  // for the main thread
std::thread worker;
bool quitting = false;

void work() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> l(lock);
            wake.wait(l, [] { return quitting || !todo.empty(); });
            if (quitting) return;
            job = std::move(todo.front());
            todo.pop_front();
        }
        job();
    }
}

void post(std::function<void()> job) {
    std::lock_guard<std::mutex> l(lock);
    if (!worker.joinable()) worker = std::thread(work);
    todo.push_back(std::move(job));
    wake.notify_one();
}

void deliver(std::function<void()> job) {
    std::lock_guard<std::mutex> l(lock);
    finished.push_back(std::move(job));
}

bool readJson(const std::string& rel, Json& out) {
    std::string text;
    return readText(join(root, rel), text) && Json::parse(text, out);
}

// Both indexes, once (on the thread): sprites/index.json maps a unit type to its sheet's name, index_hd.json a
// type to its close-up sheet {name, size, ppm}. The close-ups are listed to the renderer as soon as they are known.
Json sheetIndex, closeupIndex;
bool indexesRead = false;

void readIndexes() {
    if (indexesRead) return;
    indexesRead = true;
    readJson("sprites/index.json", sheetIndex);
    readJson("sprites/index_hd.json", closeupIndex);
    deliver([listing = closeupIndex] {
        for (const auto& m : listing.members) {
            const Json* size = m.second.find("size");
            if (!size || size->items.size() < 2) continue;
            illyria_sheet_closeup_listed(std::atoi(m.first.c_str()), static_cast<int>(size->items[0].number), static_cast<int>(size->items[1].number),
                                         static_cast<float>(m.second.num("ppm")));
        }
    });
}

// A sheet's own clips, a line each: "name first frames seconds loop stride release [times...]" (sprite_clips.h
// parseClips). The page makes the same text (game/web/platform_web.cpp).
std::string clipsText(const Json& meta) {
    const auto number = [](const Json* j, double none) { return j && j->type == Json::Type::Number && std::isfinite(j->number) ? j->number : none; };
    std::string out;
    for (const Json& k : meta.list("clips")) {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s %.9g %.9g %.9g %d %.9g %.9g", k.str("name").c_str(), k.num("first"), k.num("frames"),
                      number(k.find("seconds"), 0), k.flag("loop") ? 1 : 0, number(k.find("stride"), 0), number(k.find("release"), -1));
        if (!out.empty()) out += '\n';
        out += buf;
        for (const Json& t : k.list("times")) {
            std::snprintf(buf, sizeof buf, " %.9g", t.number);
            out += buf;
        }
    }
    return out;
}

void loadSheet(int type, bool closeup, int standIn) {
    readIndexes();
    const Json& index = closeup ? closeupIndex : sheetIndex;
    const Json* entry = index.find(std::to_string(type));
    if (!entry) entry = index.find(std::to_string(standIn));  // its stand-in's (sprite_layout.h sheetStandIn)
    std::string name;
    if (entry) name = closeup ? entry->str("name") : entry->string;
    if (name.empty()) return;
    Json meta;
    std::vector<uint8_t> file;
    auto px = std::make_shared<Pixels>();
    if (!readJson("sprites/" + name + ".json", meta) || !readFile(join(root, "sprites/" + name + ".webp"), file) || !px->decode(file)) {
        std::fprintf(stderr, "sprite sheet %s: not read\n", name.c_str());
        return;
    }
    auto cells = std::make_shared<std::vector<float>>();
    for (const Json& row : meta.list("cells"))
        for (const Json& v : row.items) cells->push_back(static_cast<float>(v.number));
    const int cellCount = static_cast<int>(meta.list("cells").size());
    // Old sheets have 26 frames a view and say nothing; new ones say 44 (sprite_layout.h), or as their clips list.
    const int frames = static_cast<int>(meta.num("framesPerView", 26));
    deliver([=] {
        const std::string clips = clipsText(meta);
        if (!illyria_sheet_loaded3(type, px->rgba, px->w, px->h, cells->data(), cellCount, frames, closeup ? 1 : 0, clips.c_str()))
            std::fprintf(stderr, "sprite sheet %s rejected\n", name.c_str());
    });
}

void loadModel(int kind) {
    static const char* const kNames[] = {"ruin"};
    if (kind < 0 || kind >= static_cast<int>(sizeof kNames / sizeof *kNames)) return;
    const std::string name = kNames[kind];
    auto mesh = std::make_shared<std::vector<uint8_t>>();
    std::vector<uint8_t> file;
    auto px = std::make_shared<Pixels>();
    if (!readFile(join(root, "models/" + name + ".bin"), *mesh) || !readFile(join(root, "models/" + name + ".webp"), file) || !px->decode(file)) {
        std::fprintf(stderr, "model %s: not read\n", name.c_str());
        return;
    }
    deliver([=] {
        if (!illyria_model_loaded(kind, mesh->data(), static_cast<int>(mesh->size()), px->rgba, px->w, px->h))
            std::fprintf(stderr, "model %s rejected\n", name.c_str());
    });
}

}  // namespace

bool findAssets(const std::vector<std::string>& candidates) {
    for (const std::string& dir : candidates) {
        std::error_code ec;
        if (!dir.empty() && std::filesystem::exists(join(dir, "sfx/sounds.json"), ec)) {
            root = std::filesystem::absolute(dir).lexically_normal().string();
            return true;
        }
    }
    return false;
}

const std::string& assetRoot() { return root; }

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

void requestSheet(int type, bool closeup, int standIn) {
    post([=] { loadSheet(type, closeup, standIn); });
}

void requestModel(int kind) {
    post([=] { loadModel(kind); });
}

void pumpAssets() {
    std::function<void()> job;
    {
        std::lock_guard<std::mutex> l(lock);
        if (finished.empty()) return;
        job = std::move(finished.front());
        finished.pop_front();
    }
    job();
}

void stopAssets() {
    {
        std::lock_guard<std::mutex> l(lock);
        quitting = true;
        wake.notify_one();
    }
    if (worker.joinable()) worker.join();
}

}  // namespace illyria::native
