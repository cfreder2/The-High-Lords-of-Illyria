// Browser entry point: WebGL 2 context, main loop, and the C functions the
// page's JavaScript calls for input and HUD state.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "../render/renderer.h"
#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/game/factions.h"
#include "illyria/game/gestures.h"

#ifndef ILLYRIA_VERSION
#define ILLYRIA_VERSION "0.0.0-dev"
#endif

using namespace illyria::game;

namespace {

constexpr uint64_t kBattleSeed = 20261027;
constexpr double kMaxPixelRatio = 2.0;
constexpr float kNorthUp = 1.5707963f;  // camera yaw looking north

struct App {
    std::unique_ptr<Battle> battle;
    Camera camera;
    std::unique_ptr<Gestures> gestures;
    Renderer renderer;
    double lastMs = 0;
    float pixelRatio = 1;
    double maxPixelRatio = kMaxPixelRatio;  // lowered by the graphics settings
    double fps = 60;
    std::string hud, labels;
    // The opening swoop from the whole continent down to the battle.
    double introStartMs = -1;
    V3 introFrom, introTo;
    float introFromDist = 0, introToDist = 0, introToYaw = 0;
};

App* app = nullptr;

void frameCamera() {
    const Battle& b = *app->battle;
    // Open just behind the Oakhaven shield wall, close enough to see soldiers, looking
    // east toward the Red Hand.
    const V3 c = b.squadCentre(b.squads()[0]);
    app->camera.lookAt(c.x + 10, c.y, 55);
}

// Start over the whole continent and swoop down to the battle.
void startIntro() {
    const Battle& b = *app->battle;
    frameCamera();
    app->introTo = app->camera.target();
    app->introToDist = app->camera.distance();
    const V3 o = b.origin();
    app->introFrom = {illyria::sim::Continent::kWidth * 0.5f - o.x, illyria::sim::Continent::kHeight * 0.5f - o.y, 0};
    app->introFromDist = 19000.0f;
    app->introToYaw = app->camera.yaw();
    // North up over the continent, like the campaign map.
    app->camera.rotate(kNorthUp - app->camera.yaw());
    app->camera.lookAt(app->introFrom.x, app->introFrom.y, app->introFromDist);
    app->introStartMs = 0;  // set on the first frame
}

void stepIntro(double now) {
    if (app->introStartMs < 0) return;
    if (app->introStartMs == 0) app->introStartMs = now;
    constexpr double kHoldMs = 1200, kSwoopMs = 5000;
    const double t = std::clamp((now - app->introStartMs - kHoldMs) / kSwoopMs, 0.0, 1.0);
    const float s = static_cast<float>(t * t * (3 - 2 * t));
    // Pan early, descend on a log scale so the zoom feels even all the way down.
    const float pan = std::min(1.0f, s * 1.6f);
    const float x = app->introFrom.x + (app->introTo.x - app->introFrom.x) * pan;
    const float y = app->introFrom.y + (app->introTo.y - app->introFrom.y) * pan;
    const float d = app->introFromDist * std::pow(app->introToDist / app->introFromDist, s);
    app->camera.lookAt(x, y, d);
    app->camera.rotate(kNorthUp + (app->introToYaw - kNorthUp) * s - app->camera.yaw());
    if (t >= 1) app->introStartMs = -1;
}

void newBattle() {
    app->battle = std::make_unique<Battle>(kBattleSeed);
    // The whole continent, in battle-local coordinates.
    const V3 o = app->battle->origin();
    app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
    app->camera.setHeightFn([](float x, float y) { return app->battle->heightAt(x, y); });
}

void resizeCanvas() {
    double cssW = 0, cssH = 0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    app->pixelRatio = static_cast<float>(std::min(emscripten_get_device_pixel_ratio(), app->maxPixelRatio));
    const int w = std::max(1, static_cast<int>(cssW * app->pixelRatio));
    const int h = std::max(1, static_cast<int>(cssH * app->pixelRatio));
    int curW = 0, curH = 0;
    emscripten_get_canvas_element_size("#canvas", &curW, &curH);
    if (curW != w || curH != h) emscripten_set_canvas_element_size("#canvas", w, h);
    app->camera.setViewport(static_cast<float>(w), static_cast<float>(h));
    app->gestures->setPixelScale(app->pixelRatio);
}

void frame() {
    const double now = emscripten_get_now();
    const double dtMs = app->lastMs > 0 ? now - app->lastMs : 16.7;
    app->lastMs = now;
    app->fps = app->fps * 0.95 + (1000.0 / std::max(dtMs, 1.0)) * 0.05;
    resizeCanvas();
    stepIntro(now);
    app->battle->advance(dtMs / 1000.0);
    app->renderer.draw(*app->battle, app->camera, now / 1000.0);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* illyria_version() { return ILLYRIA_VERSION; }

// type: 0 down, 1 move, 2 up, 3 cancel. Coordinates in CSS pixels.
EMSCRIPTEN_KEEPALIVE void illyria_pointer(int type, int id, float x, float y, int button, double timeMs) {
    if (!app) return;
    const float s = app->pixelRatio;
    if (type == 0) app->introStartMs = -1;  // any touch takes the camera
    app->gestures->pointer(static_cast<PointerEvent>(type), id, x * s, y * s, button, timeMs);
}

EMSCRIPTEN_KEEPALIVE void illyria_wheel(float deltaY, float x, float y) {
    if (app) app->introStartMs = -1;
    if (app) app->gestures->wheel(deltaY, x * app->pixelRatio, y * app->pixelRatio);
}

EMSCRIPTEN_KEEPALIVE void illyria_select_squad(int index) {
    if (app) app->battle->selectSquad(index);
}

EMSCRIPTEN_KEEPALIVE void illyria_focus_squad(int index) {
    if (!app || index < 0 || index >= static_cast<int>(app->battle->squads().size())) return;
    const V3 c = app->battle->squadCentre(app->battle->squads()[index]);
    app->camera.lookAt(c.x, c.y, std::min(app->camera.distance(), 160.0f));
}

EMSCRIPTEN_KEEPALIVE void illyria_hot_spot() {
    if (!app) return;
    V3 at;
    if (app->battle->hotSpot(at)) app->camera.lookAt(at.x, at.y, std::clamp(app->camera.distance(), 30.0f, 70.0f));
}

EMSCRIPTEN_KEEPALIVE void illyria_stop() {
    if (app) app->battle->stopSelected();
}
EMSCRIPTEN_KEEPALIVE void illyria_cycle_formation() {
    if (app) app->battle->cycleFormation();
}
EMSCRIPTEN_KEEPALIVE void illyria_toggle_pause() {
    if (app) app->battle->togglePause();
}
EMSCRIPTEN_KEEPALIVE void illyria_cycle_speed() {
    if (app) app->battle->cycleSpeed();
}
EMSCRIPTEN_KEEPALIVE void illyria_restart() {
    if (!app) return;
    newBattle();
    app->renderer.reset(*app->battle);
    frameCamera();
}
// Test hook: run the simulation forward instantly.
EMSCRIPTEN_KEEPALIVE void illyria_fast_forward(double seconds) {
    if (!app) return;
    for (double t = 0; t < seconds; t += 0.1) app->battle->advance(0.1);
}
EMSCRIPTEN_KEEPALIVE void illyria_set_camera(float x, float y, float distance, float yaw) {
    if (!app) return;
    app->introStartMs = -1;
    app->camera.lookAt(x, y, distance);
    app->camera.rotate(yaw - app->camera.yaw());
}

// Graphics settings from the page: Renderer::Feature bits and a resolution cap.
// Purely cosmetic: the simulation never sees them.
EMSCRIPTEN_KEEPALIVE void illyria_set_graphics(int features, double maxPixelRatio) {
    if (!app) return;
    app->renderer.setFeatures(features);
    app->maxPixelRatio = std::clamp(maxPixelRatio, 0.5, kMaxPixelRatio);
}
EMSCRIPTEN_KEEPALIVE int illyria_graphics_preset(int level) {
    return level <= 0 ? Renderer::kLow : level == 1 ? Renderer::kMedium : Renderer::kHigh;
}

// Test hook: look down on the whole continent.
EMSCRIPTEN_KEEPALIVE void illyria_overview() {
    if (!app) return;
    app->introStartMs = -1;
    app->camera.rotate(kNorthUp - app->camera.yaw());
    const V3 o = app->battle->origin();
    app->camera.lookAt(illyria::sim::Continent::kWidth * 0.5f - o.x, illyria::sim::Continent::kHeight * 0.5f - o.y, 19000.0f);
}

// Map labels for the page to draw: one line per visible label,
// "kind|colour|x|y|name" in CSS pixels. Kinds: 0 city, 1 capital, 2 ruin,
// 3 temple, 4 sage, 5 realm, 6 landmark. Which appear depends on the zoom.
EMSCRIPTEN_KEEPALIVE const char* illyria_labels() {
    if (!app) return "";
    std::string& out = app->labels;
    out.clear();
    const Camera& cam = app->camera;
    const float d = cam.distance();
    if (d < 700) return out.c_str();
    const illyria::sim::Continent& c = app->battle->continent();
    const V3 o = app->battle->origin();
    char buf[160];
    const auto add = [&](int kind, const V3& colour, const illyria::sim::Vec3& at, const std::string& name) {
        V3 p = toV3(at) - V3{o.x, o.y, 0};
        p.z = app->battle->heightAt(p.x, p.y) + 10;
        float px, py;
        if (!cam.project(p, px, py)) return;
        px /= app->pixelRatio;
        py /= app->pixelRatio;
        if (px < -80 || py < -40 || px > cam.width() / app->pixelRatio + 80 || py > cam.height() / app->pixelRatio + 40) return;
        std::snprintf(buf, sizeof buf, "%d|#%02x%02x%02x|%.0f|%.0f|%s\n", kind, static_cast<int>(colour.x * 255),
                      static_cast<int>(colour.y * 255), static_cast<int>(colour.z * 255), px, py, name.c_str());
        out += buf;
    };
    for (const auto& city : c.cities()) {
        if (!city.capital && d > 9000) {
            add(0, factionColor(city.owner), city.pos, "");  // a marker only
            continue;
        }
        add(city.capital ? 1 : 0, factionColor(city.owner), city.pos, city.name);
    }
    if (d < 9000) {
        for (const auto& site : c.sites()) add(2 + static_cast<int>(site.kind), {1, 1, 1}, site.pos, site.name);
    }
    for (const auto& l : c.labels()) {
        if (l.kind == 0 && d > 3000) add(5, {1, 1, 1}, l.pos, l.name);
        else if (l.kind != 0 && l.kind != 4 && d > 2000 && d < 12000) add(6, {1, 1, 1}, l.pos, l.name);
    }
    return out.c_str();
}

// HUD state as JSON, polled by the page a few times a second.
EMSCRIPTEN_KEEPALIVE const char* illyria_hud() {
    if (!app) return "{}";
    const Battle& b = *app->battle;
    std::string& s = app->hud;
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "{\"version\":\"%s\",\"tick\":%u,\"paused\":%s,\"speed\":%d,\"outcome\":%d,\"fps\":%.0f,"
                  "\"stepMs\":%.3f,\"zoom\":%.0f,\"selected\":%d,\"squads\":[",
                  ILLYRIA_VERSION, b.world().tick(), b.paused() ? "true" : "false", b.speed(), b.outcome(), app->fps,
                  b.stepMs(), app->camera.distance(), b.selectedSquad());
    s = buf;
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& q = b.squads()[i];
        static const char* kRoles[] = {"melee", "ranged", "bomber", "caster", "healer", "flier"};
        std::snprintf(buf, sizeof buf,
                      "%s{\"name\":\"%s\",\"type\":\"%s\",\"owner\":%d,\"role\":\"%s\",\"formation\":\"%s\",\"alive\":%d,\"total\":%zu}",
                      i ? "," : "", q.name.c_str(), illyria::sim::unitTypeName(q.type), q.owner,
                      kRoles[static_cast<int>(roleOf(q.type))], formationName(q.formation),
                      b.aliveCount(q), q.members.size());
        s += buf;
    }
    s += "]}";
    return s.c_str();
}

}  // extern "C"

int main() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.antialias = true;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        EM_ASM({ if (window.illyriaFatal) window.illyriaFatal("WebGL 2 is not available on this device."); });
        return 1;
    }
    emscripten_webgl_make_context_current(ctx);

    app = new App();
    app->gestures = std::make_unique<Gestures>(app->camera, [](float x, float y) {
        app->battle->tap(app->camera, x, y, app->pixelRatio);
    });
    newBattle();
    resizeCanvas();
    if (!app->renderer.init(*app->battle)) {
        EM_ASM({ if (window.illyriaFatal) window.illyriaFatal("Graphics initialisation failed."); });
        return 1;
    }
    startIntro();
    EM_ASM({ if (window.illyriaReady) window.illyriaReady(); });
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
