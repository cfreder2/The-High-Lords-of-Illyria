// Browser entry point: WebGL 2 context, main loop, and the C functions the
// page's JavaScript calls for input and HUD state.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

#include "../render/renderer.h"
#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/game/factions.h"
#include "illyria/game/gestures.h"
#include "illyria/sim/campaign.h"

#ifndef ILLYRIA_VERSION
#define ILLYRIA_VERSION "0.0.0-dev"
#endif

using namespace illyria::game;

namespace illyria::game {
void fetchPrerenderedSheets(Renderer& renderer);  // sheets.cpp
}

namespace {

constexpr uint64_t kBattleSeed = 20261027;
constexpr double kMaxPixelRatio = 3.0;
constexpr float kNorthUp = 1.5707963f;  // camera yaw looking north

struct App {
    std::unique_ptr<Battle> battle;
    Camera camera;
    std::unique_ptr<Gestures> gestures;
    Renderer renderer;
    double lastMs = 0;
    float pixelRatio = 1;
    double maxPixelRatio = kMaxPixelRatio;  // lowered by the graphics settings
    // Dynamic resolution: as sharp as the device can draw at a steady rate,
    // between the settings' floor and ceiling (see adaptResolution).
    double minPixelRatio = 1.0, ratio = 1.5;
    double slowSince = -1, fastSince = -1;
    double fps = 60;
    std::string hud, labels;
    bool shift = false;  // held on the desktop
    // The campaign, when playing it: the war on the continent map.
    struct CampaignView {
        std::unique_ptr<illyria::sim::Campaign> c;
        std::shared_ptr<const illyria::sim::Continent> continent;  // outlives restarts of the battle
        bool on = false, paused = false;
        int speed = 1;
        uint32_t selected = 0;
        int city = -1;  // the city whose panel is open
        double acc = 0;
        std::string json;
        // While a campaign battle is fought in 3D, the campaign's own scene waits here.
        std::unique_ptr<Battle> backdrop;
        bool fighting = false;
        bool hard = true;  // how the computer lords play (the player picks at the start)
        int vectorFrom = -1;  // choosing where this city sends its new armies: the next city tapped
        // Ports: where each city's pier runs out to the water (battle-local), and its heading.
        std::vector<V3> pier;
        std::vector<float> pierYaw;
        std::map<uint32_t, float> heading;  // each boat's last course, so it turns smoothly
    } camp;
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

Scenario gScenario = Scenario::Ford;

void newBattle() {
    app->battle = std::make_unique<Battle>(kBattleSeed, gScenario);
    // The whole continent, in battle-local coordinates.
    const V3 o = app->battle->origin();
    app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
    app->camera.setHeightFn([](float x, float y) { return app->battle->heightAt(x, y); });
}

// Below 28 frames a second for two seconds: a quarter step coarser; above 50
// for four: a quarter step finer. Phones differ by ten times in speed.
void adaptResolution(double now) {
    if (app->fps < 28) {
        app->fastSince = -1;
        if (app->slowSince < 0) app->slowSince = now;
        if (now - app->slowSince > 2000 && app->ratio > app->minPixelRatio) {
            app->ratio = std::max(app->minPixelRatio, app->ratio - 0.25);
            app->slowSince = now;
        }
    } else if (app->fps > 50) {
        app->slowSince = -1;
        if (app->fastSince < 0) app->fastSince = now;
        if (now - app->fastSince > 4000 && app->ratio < std::min(app->maxPixelRatio, emscripten_get_device_pixel_ratio())) {
            app->ratio = std::min(app->maxPixelRatio, app->ratio + 0.25);
            app->fastSince = now;
        }
    } else {
        app->slowSince = app->fastSince = -1;
    }
}

void resizeCanvas() {
    double cssW = 0, cssH = 0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    app->pixelRatio = static_cast<float>(std::min({emscripten_get_device_pixel_ratio(), app->maxPixelRatio, std::max(app->ratio, app->minPixelRatio)}));
    const int w = std::max(1, static_cast<int>(cssW * app->pixelRatio));
    const int h = std::max(1, static_cast<int>(cssH * app->pixelRatio));
    int curW = 0, curH = 0;
    emscripten_get_canvas_element_size("#canvas", &curW, &curH);
    if (curW != w || curH != h) emscripten_set_canvas_element_size("#canvas", w, h);
    app->camera.setViewport(static_cast<float>(w), static_cast<float>(h));
    app->gestures->setPixelScale(app->pixelRatio);
}

// The ground, or the water over it, at a battle-local point.
float surfaceAt(float x, float y) {
    const V3 o = app->battle->origin();
    float z = app->battle->heightAt(x, y);
    illyria::sim::Fixed w;
    if (app->battle->continent().waterSurface(illyria::sim::Fixed::fromInt(static_cast<int64_t>(x + o.x)),
                                              illyria::sim::Fixed::fromInt(static_cast<int64_t>(y + o.y)), w))
        z = std::max(z, static_cast<float>(w.toDouble()));
    return z;
}

// Where a campaign stack stands now, in battle-local metres: between the cell
// it holds and the next on its road, by how far it has got (afloat, at the boat's pace).
V3 stackPosition(const illyria::sim::Campaign& c, const illyria::sim::Campaign::Stack& s) {
    using C = illyria::sim::Campaign;
    const V3 o = app->battle->origin();
    float x = static_cast<float>(C::centre(s.cx)), y = static_cast<float>(C::centre(s.cy));
    if (!s.path.empty()) {
        int32_t cost = 0;
        for (const illyria::sim::Army& a : s.armies) cost = std::max(cost, C::moveCost(a.type, c.terrainAt(s.path.front().first, s.path.front().second)));
        if (s.boat) cost = C::kSeaCost;
        const float t = cost > 0 ? std::clamp(static_cast<float>(s.progress) / (cost * C::kTicksPerDay), 0.0f, 1.0f) : 0;
        x += (C::centre(s.path.front().first) - x) * t;
        y += (C::centre(s.path.front().second) - y) * t;
    }
    V3 p{x - o.x, y - o.y, 0};
    p.z = s.boat ? surfaceAt(p.x, p.y) : app->battle->heightAt(p.x, p.y);
    return p;
}

// Every port's pier: from the shore below its walls out toward its harbour.
void placePiers() {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
    const V3 o = app->battle->origin();
    cv.pier.assign(c.cities().size(), V3{});
    cv.pierYaw.assign(c.cities().size(), 0);
    std::vector<Renderer::Prop> piers;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const C::CityState& cs = c.cities()[i];
        if (!cs.port) continue;
        const V3 from = toV3(c.continent().cities()[i].pos) - V3{o.x, o.y, 0};
        V3 dir = V3{C::centre(cs.hx) - o.x - from.x, C::centre(cs.hy) - o.y - from.y, 0};
        const float len = dir.length();
        dir = dir * (1.0f / std::max(len, 1.0f));
        // Walk out to the water's edge.
        V3 shore = from;
        for (float d = 0; d < len + 200; d += 6) {
            shore = from + dir * d;
            const illyria::sim::Continent::Sample smp = c.continent().sample(illyria::sim::Fixed::fromInt(static_cast<int64_t>(shore.x + o.x)),
                                                                             illyria::sim::Fixed::fromInt(static_cast<int64_t>(shore.y + o.y)));
            illyria::sim::Fixed w;
            if (c.continent().waterSurfaceOf(smp, w) && smp.height + illyria::sim::Fixed::fromRatio(1, 2) < w) break;
        }
        shore = shore - dir * 12.0f;
        const float yaw = std::atan2(dir.y, dir.x);
        shore.z = surfaceAt(shore.x + dir.x * 20, shore.y + dir.y * 20) + 2.0f;
        piers.push_back({shore, yaw, 70.0f, {1, 1, 1}});
        cv.pier[i] = shore + dir * 62.0f;
        cv.pierYaw[i] = yaw;
    }
    app->renderer.setPiers(piers);
}

// The boats, every frame: with their stacks as they sail, along a pier in
// port, or riding at anchor.
void placeBoats() {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
    const V3 o = app->battle->origin();
    std::vector<Renderer::Prop> out;
    std::map<int32_t, int> moored;  // boats already at each pier
    for (const C::Boat& b : c.boats()) {
        Renderer::Prop p;
        p.scale = std::clamp(app->camera.distance() / 80.0f, 26.0f, 80.0f);  // larger than life, to read on the map
        p.tint = factionColor(b.owner);
        float& yaw = cv.heading.try_emplace(b.id, 0.0f).first->second;
        if (const C::Stack* s = b.stack ? c.findStack(b.stack) : nullptr) {
            p.pos = stackPosition(c, *s);
            p.pos.z = surfaceAt(p.pos.x, p.pos.y);
            if (!s->path.empty()) {
                const float want = std::atan2(C::centre(s->path.front().second) - o.y - p.pos.y, C::centre(s->path.front().first) - o.x - p.pos.x);
                float turn = std::remainder(want - yaw, 6.2831853f);
                yaw += turn * 0.15f;
            }
        } else if (b.port >= 0 && static_cast<size_t>(b.port) < cv.pier.size()) {
            const int k = moored[b.port]++;
            const float py = cv.pierYaw[static_cast<size_t>(b.port)];
            const V3 side{-std::sin(py), std::cos(py), 0};
            p.pos = cv.pier[static_cast<size_t>(b.port)] + side * (k % 2 ? -22.0f : 22.0f) - V3{std::cos(py), std::sin(py), 0} * (20.0f * static_cast<float>(k / 2));
            p.pos.z = surfaceAt(p.pos.x, p.pos.y);
            yaw = py;
        } else {
            p.pos = {C::centre(b.cx) - o.x, C::centre(b.cy) - o.y, 0};
            p.pos.z = surfaceAt(p.pos.x, p.pos.y);
        }
        p.yaw = yaw;
        out.push_back(p);
    }
    app->renderer.setBoats(out);
}

// Whether the pending battle is fought from a boat or against one (settled with the dice).
bool pendingAfloat(const illyria::sim::Campaign& c) {
    const illyria::sim::Campaign::Pending* p = c.pending();
    if (!p) return false;
    const illyria::sim::Campaign::Stack* att = c.findStack(p->stack);
    bool afloat = (att && att->boat) || c.navigable(p->cx, p->cy);
    for (const illyria::sim::Campaign::Stack& s : c.stacks()) afloat = afloat || (s.cx == p->cx && s.cy == p->cy && s.boat);
    return afloat;
}

void campaignTap(float px, float py) {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
    if (cv.vectorFrom >= 0) {
        // Sending a city's new armies: the tapped city of ours receives them.
        V3 hit;
        const int from = cv.vectorFrom;
        cv.vectorFrom = -1;
        if (!app->camera.pickGround(px, py, hit)) return;
        const V3 o = app->battle->origin();
        const int32_t tx = std::clamp(C::cellOf(static_cast<int32_t>(hit.x + o.x)), 0, c.width() - 1);
        const int32_t ty = std::clamp(C::cellOf(static_cast<int32_t>(hit.y + o.y)), 0, c.height() - 1);
        int32_t to = -1;
        for (int dy = -1; dy <= 1 && to < 0; ++dy)
            for (int dx = -1; dx <= 1 && to < 0; ++dx) {
                const int32_t k = c.cityAt(std::clamp(tx + dx, 0, c.width() - 1), std::clamp(ty + dy, 0, c.height() - 1));
                if (k >= 0 && c.cities()[static_cast<size_t>(k)].owner == 0) to = k;
            }
        if (to >= 0) cv.c->vectorProduction(from, to);
        return;
    }
    // A banner near the tap: select one of ours (or fall through to march on theirs).
    const illyria::sim::Campaign::Stack* nearest = nullptr;
    float best = 34.0f * app->pixelRatio;
    for (const C::Stack& s : c.stacks()) {
        if (s.armies.empty()) continue;
        V3 p = stackPosition(c, s);
        p.z += 30;
        float sx, sy;
        if (!app->camera.project(p, sx, sy)) continue;
        const float d = std::hypot(sx - px, sy - py);
        if (d < best) {
            best = d;
            nearest = &s;
        }
    }
    if (nearest && nearest->owner == 0 && nearest->id != cv.selected) {
        cv.selected = nearest->id;
        cv.city = c.cityAt(nearest->cx, nearest->cy);
        if (cv.city >= 0 && c.cities()[static_cast<size_t>(cv.city)].owner != 0) cv.city = -1;
        return;
    }
    V3 hit;
    if (!app->camera.pickGround(px, py, hit)) return;
    const V3 o = app->battle->origin();
    const int32_t cx = std::clamp(C::cellOf(static_cast<int32_t>(hit.x + o.x)), 0, c.width() - 1);
    const int32_t cy = std::clamp(C::cellOf(static_cast<int32_t>(hit.y + o.y)), 0, c.height() - 1);
    if (nearest && nearest->owner != 0) {
        // On an enemy banner: attack it.
        if (cv.selected) cv.c->move(cv.selected, nearest->cx, nearest->cy);
        return;
    }
    const int32_t city = c.cityAt(cx, cy);
    const C::Stack* sel = c.findStack(cv.selected);
    if (sel && !(sel->cx == cx && sel->cy == cy)) {
        cv.c->move(cv.selected, cx, cy);
        return;
    }
    if (city >= 0 && c.cities()[static_cast<size_t>(city)].owner == 0) {
        cv.city = city;
        return;
    }
    cv.selected = 0;
    cv.city = -1;
}

void startCampaign(bool hard) {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    cv.continent = app->battle->continentShared();
    cv.c = std::make_unique<C>(*cv.continent, kBattleSeed);
    cv.c->setHuman(0, true);
    cv.hard = hard;
    cv.c->setComputerDifficulty(hard ? illyria::sim::Difficulty::Hard : illyria::sim::Difficulty::Easy);
    cv.on = true;
    cv.paused = false;
    cv.speed = 1;
    cv.selected = 0;
    cv.city = -1;
    app->renderer.setShowUnits(false);
    cv.heading.clear();
    placePiers();
    // Over your capital, high enough to see the neighbouring towns.
    for (const C::Stack& s : cv.c->stacks()) {
        if (s.owner != 0) continue;
        cv.selected = s.id;
        const V3 p = stackPosition(*cv.c, s);
        app->introStartMs = -1;
        app->camera.lookAt(p.x, p.y - 400, 2600);
        app->camera.rotate(kNorthUp - app->camera.yaw());
        break;
    }
}

void frame() {
    const double now = emscripten_get_now();
    const double dtMs = app->lastMs > 0 ? now - app->lastMs : 16.7;
    app->lastMs = now;
    app->fps = app->fps * 0.95 + (1000.0 / std::max(dtMs, 1.0)) * 0.05;
    adaptResolution(now);
    resizeCanvas();
    stepIntro(now);
    app->gestures->update(now);
    if (app->camp.on) {
        // Ten campaign ticks a second, times the speed.
        App::CampaignView& cv = app->camp;
        if (!cv.paused && cv.c->winner() == 0) {
            cv.acc += dtMs / 1000.0 * illyria::sim::Campaign::kTicksPerSecond * cv.speed;
            for (int k = 0; k < 40 && cv.acc >= 1; ++k, cv.acc -= 1) cv.c->step();
            cv.acc = std::min(cv.acc, 1.0);
        }
        placeBoats();
    } else {
        app->battle->advance(dtMs / 1000.0);
    }
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

// Commanding (game/src/battle_command.cpp). Shift held: a press starts a
// selection box at once, and a tap adds to the selection.
EMSCRIPTEN_KEEPALIVE void illyria_set_shift(int down) {
    if (!app) return;
    app->shift = down != 0;
    app->gestures->setShift(app->shift && !app->camp.on);
}
EMSCRIPTEN_KEEPALIVE void illyria_clear_selection() {
    if (app) app->battle->clearSelection();
}
EMSCRIPTEN_KEEPALIVE void illyria_select_heroes() {
    if (app) app->battle->selectHeroes();
}
// A formation for the selection, by Formation value.
EMSCRIPTEN_KEEPALIVE void illyria_set_formation(int f) {
    if (app && f >= 0 && f < kFormationCount) app->battle->setFormation(static_cast<Formation>(f));
}
EMSCRIPTEN_KEEPALIVE void illyria_scatter() {
    if (app) app->battle->scatter();
}
// Groups, by slot (the key 0-9): 0 recall, 1 assign the selection, 2 save
// the selection in the first free slot (returns it), 3 forget.
EMSCRIPTEN_KEEPALIVE int illyria_group(int op, int slot) {
    if (!app) return -1;
    Battle& b = *app->battle;
    switch (op) {
        case 0: return b.recallGroup(slot) ? slot : -1;
        case 1: b.assignGroup(slot); return slot;
        case 2: return b.saveGroup();
        case 3: b.clearGroup(slot); return slot;
    }
    return -1;
}
// The camera to the selection.
EMSCRIPTEN_KEEPALIVE void illyria_focus_selection() {
    if (!app) return;
    const std::vector<illyria::sim::EntityId> alive = app->battle->selectedAlive();
    if (alive.empty()) return;
    V3 c;
    for (illyria::sim::EntityId id : alive) c += app->battle->unitPosition(id - 1);
    c = c * (1.0f / static_cast<float>(alive.size()));
    app->introStartMs = -1;
    app->camera.lookAt(c.x, c.y, std::min(app->camera.distance(), 160.0f));
}
// Every formation's shape for the picker's glyphs, in picker order:
// [{"id","name","slots":[[x,y],...]}].
EMSCRIPTEN_KEEPALIVE const char* illyria_formations() {
    static std::string out;
    out = "[";
    char buf[96];
    for (int k = 0; k < kFormationCount; ++k) {
        const Formation f = kFormationOrder[k];
        std::snprintf(buf, sizeof buf, "%s{\"id\":%d,\"name\":\"%s\",\"slots\":[", k ? "," : "", static_cast<int>(f), formationName(f));
        out += buf;
        const std::vector<FormationSlot> slots = formationLayout(f, 9, 1.0f);
        for (size_t i = 0; i < slots.size(); ++i) {
            std::snprintf(buf, sizeof buf, "%s[%.2f,%.2f]", i ? "," : "", slots[i].pos.x, slots[i].pos.y);
            out += buf;
        }
        out += "]}";
    }
    out += "]";
    return out.c_str();
}
// The selection box being drawn, in CSS pixels: "x0 y0 x1 y1", or "" when none.
EMSCRIPTEN_KEEPALIVE const char* illyria_drag_box() {
    static char buf[96];
    buf[0] = 0;
    if (!app || app->camp.on) return buf;
    const Battle::Drag& d = app->battle->drag();
    if (d.mode != Battle::Drag::Mode::Box) return buf;
    const float s = app->pixelRatio;
    std::snprintf(buf, sizeof buf, "%.0f %.0f %.0f %.0f", d.x0 / s, d.y0 / s, d.x1 / s, d.y1 / s);
    return buf;
}
// Test hooks for screenshots: a press held and dragged but not let go. From
// the squad's centre (dx, dy) metres to the destination, then (fx, fy) metres
// on for the facing line; or a selection box in CSS pixels.
EMSCRIPTEN_KEEPALIVE void illyria_test_press_facing(int squad, float dx, float dy, float fx, float fy, float distance) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return;
    Battle& b = *app->battle;
    b.selectSquad(squad);
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(squad)], false);
    V3 at{c.x + dx, c.y + dy, 0}, tip{c.x + dx + fx, c.y + dy + fy, 0};
    at.z = b.heightAt(at.x, at.y);
    tip.z = b.heightAt(tip.x, tip.y);
    app->introStartMs = -1;
    app->camera.lookAt((c.x + at.x) / 2, (c.y + at.y) / 2, distance);
    float px, py, qx, qy;
    if (!app->camera.project(at, px, py) || !app->camera.project(tip, qx, qy)) return;
    b.pressBegin(app->camera, px, py, app->pixelRatio, false);
    b.pressMove(app->camera, qx, qy);
}
EMSCRIPTEN_KEEPALIVE void illyria_test_press_box(float x0, float y0, float x1, float y1) {
    if (!app) return;
    const float s = app->pixelRatio;
    app->battle->pressBegin(app->camera, x0 * s, y0 * s, s, true);
    app->battle->pressMove(app->camera, x1 * s, y1 * s);
}
EMSCRIPTEN_KEEPALIVE void illyria_test_press_end() {
    if (app) app->battle->pressEnd(app->camera, app->battle->drag().x1, app->battle->drag().y1, false);
}

EMSCRIPTEN_KEEPALIVE void illyria_focus_squad(int index) {
    if (!app || index < 0 || index >= static_cast<int>(app->battle->squads().size())) return;
    const V3 c = app->battle->squadCentre(app->battle->squads()[index]);
    app->camera.lookAt(c.x, c.y, std::min(app->camera.distance(), 160.0f));
}

// Test hook: a squad's soldiers promoted to a rank (0..3), to see their shield pips.
EMSCRIPTEN_KEEPALIVE void illyria_test_promote(int squad, int rank) {
    if (app && squad >= 0) app->battle->promoteSquad(static_cast<size_t>(squad), rank);
}

EMSCRIPTEN_KEEPALIVE void illyria_hot_spot() {
    if (!app) return;
    V3 at;
    if (app->battle->hotSpot(at)) app->camera.lookAt(at.x, at.y, std::clamp(app->camera.distance(), 30.0f, 70.0f));
}

EMSCRIPTEN_KEEPALIVE void illyria_stop() {
    if (app) app->battle->stopSelected();
}
// Debugging: draw a unit type's sprite sheet over the screen (-1 hides it).
EMSCRIPTEN_KEEPALIVE void illyria_show_sheet(int type) {
    if (app) app->renderer.showSheet(type);
}
EMSCRIPTEN_KEEPALIVE void illyria_charge() {
    if (app) app->battle->chargeAll();
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
// Switch battle: 0 Oakhaven Ford, 1 the siege of a neutral town.
EMSCRIPTEN_KEEPALIVE void illyria_set_scenario(int scenario) {
    gScenario = scenario == 1 ? Scenario::Siege : Scenario::Ford;
    if (!app) return;
    newBattle();
    app->renderer.reset(*app->battle);
    frameCamera();
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
// Test hooks for sieges: the battlefield's gate nearest the player's first squad.
int nearestGate() {
    if (!app || app->battle->squads().empty()) return -1;
    const V3 c = app->battle->squadCentre(app->battle->squads().back());
    int best = -1;
    float bestD = 1e30f;
    const auto& gates = app->battle->region().gates();
    for (size_t i = 0; i < gates.size(); ++i) {
        const V3 g = toV3(gates[i].a + gates[i].b) * 0.5f;
        const float d = (g - c).length();
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}
// The squad's orders at that gate, as if the player tapped the squad, then the gate.
EMSCRIPTEN_KEEPALIVE void illyria_test_order_gate(int squad) {
    if (!app) return;
    app->battle->selectSquad(squad);
    app->battle->orderAtGate(nearestGate());
}
// Hit points left in that gate (-1: none).
EMSCRIPTEN_KEEPALIVE int illyria_test_gate_hp() {
    const int g = nearestGate();
    return g < 0 ? -1 : app->battle->world().gates()[static_cast<size_t>(g)].hp;
}
// Camera on that gate, from outside the town.
EMSCRIPTEN_KEEPALIVE void illyria_test_view_gate(float distance, float yaw) {
    const int g = nearestGate();
    if (g < 0) return;
    const illyria::sim::Gate& gate = app->battle->region().gates()[static_cast<size_t>(g)];
    const V3 c = toV3(gate.a + gate.b) * 0.5f;
    app->introStartMs = -1;
    app->camera.lookAt(c.x, c.y, distance);
    app->camera.rotate(yaw - app->camera.yaw());
}
// Test hook: march a squad to a spot on a road 200-320 m off whose straight
// way lies mostly over the fields (it should make for the road), and look on
// from above the middle of the way.
EMSCRIPTEN_KEEPALIVE int illyria_test_march_road(int squad, float distance) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return 0;
    const illyria::game::Squad& sq = app->battle->squads()[static_cast<size_t>(squad)];
    V3 c{};
    int n = 0;
    for (illyria::sim::EntityId id : sq.members) {
        c = c + toV3(app->battle->world().units()[id - 1].pos);
        ++n;
    }
    if (n == 0) return 0;
    c = c * (1.0f / static_cast<float>(n));
    // Away from the enemy, so the march is not a charge.
    V3 foe{};
    int foes = 0;
    for (const illyria::sim::Unit& u : app->battle->world().units()) {
        if (u.owner == sq.owner || !u.alive()) continue;
        foe = foe + toV3(u.pos);
        ++foes;
    }
    if (foes > 0) foe = foe * (1.0f / static_cast<float>(foes));
    const illyria::sim::Region& rg = app->battle->region();
    const auto ground =[&](const V3& p) { return rg.groundAt(illyria::sim::Fixed::fromRatio(static_cast<int64_t>(p.x * 1000), 1000), illyria::sim::Fixed::fromRatio(static_cast<int64_t>(p.y * 1000), 1000)); };
    for (const illyria::sim::Polyline& road : rg.roads()) {
        for (size_t k = 0; k + 1 < road.points.size(); ++k) {
            const V3 a = toV3(road.points[k]), b = toV3(road.points[k + 1]);
            const int steps = static_cast<int>((b - a).length() / 8.0f) + 1;
            for (int i = 0; i < steps; ++i) {
                const V3 g = a + (b - a) * (static_cast<float>(i) / static_cast<float>(steps));
                const float d = V3{g.x - c.x, g.y - c.y, 0}.length();
                if (d < 200 || d > 320 || g.x < 40 || g.y < 40 || g.x > 984 || g.y > 984) continue;
                if (foes > 0 && (g.x - c.x) * (c.x - foe.x) + (g.y - c.y) * (c.y - foe.y) < 0) continue;
                if (ground(g) != illyria::sim::Ground::Road) continue;
                int onRoad = 0, wet = 0;
                for (int q = 0; q <= 40; ++q) {
                    const illyria::sim::Ground gr = ground(c + (g - c) * (static_cast<float>(q) / 40.0f));
                    onRoad += gr == illyria::sim::Ground::Road;
                    wet += gr == illyria::sim::Ground::DeepWater;
                }
                if (onRoad > 6 || wet > 0) continue;
                app->battle->selectSquad(squad);
                app->battle->orderMove(g);
                const V3 mid = (c + g) * 0.5f;
                app->introStartMs = -1;
                app->camera.lookAt(mid.x, mid.y, distance);
                return 1;
            }
        }
    }
    return 0;
}
EMSCRIPTEN_KEEPALIVE void illyria_set_camera(float x, float y, float distance, float yaw) {
    if (!app) return;
    app->introStartMs = -1;
    app->camera.lookAt(x, y, distance);
    app->camera.rotate(yaw - app->camera.yaw());
}

// Test hook: build the close-up land around the camera at once (screenshots).
EMSCRIPTEN_KEEPALIVE void illyria_finish_detail() {
    if (app) app->renderer.finishDetail(*app->battle, app->camera);
}

// Graphics settings from the page: Renderer::Feature bits and a resolution cap.
// Purely cosmetic: the simulation never sees them.
EMSCRIPTEN_KEEPALIVE void illyria_set_graphics(int features, double maxPixelRatio) {
    if (!app) return;
    app->renderer.setFeatures(features);
    // "Full resolution" pins the device's own; otherwise the frame rate decides, up to 2.
    app->maxPixelRatio = std::clamp(maxPixelRatio, 0.5, kMaxPixelRatio);
    app->minPixelRatio = maxPixelRatio >= kMaxPixelRatio ? kMaxPixelRatio : 1.0;
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
    for (size_t ci = 0; ci < c.cities().size(); ++ci) {
        const auto& city = c.cities()[ci];
        // In the campaign, cities fly the colours of whoever holds them now.
        const uint8_t owner = app->camp.on ? app->camp.c->cities()[ci].owner : city.owner;
        if (!city.capital && d > 9000) {
            add(0, factionColor(owner), city.pos, "");  // a marker only
            continue;
        }
        add(city.capital ? 1 : 0, factionColor(owner), city.pos, city.name);
    }
    if (d < 9000) {
        for (size_t k = 0; k < c.sites().size(); ++k) {
            const auto& site = c.sites()[k];
            // In the campaign, a ruin whose guardian is slain lies empty.
            const bool explored = app->camp.on && site.kind == illyria::sim::Continent::Site::Kind::Ruin && app->camp.c->sites()[k].searched;
            add(2 + static_cast<int>(site.kind), explored ? V3{0.6f, 0.6f, 0.6f} : V3{1, 1, 1}, site.pos, explored ? site.name + " (explored)" : site.name);
        }
    }
    for (const auto& l : c.labels()) {
        if (l.kind == 0 && d > 3000) add(5, {1, 1, 1}, l.pos, l.name);
        else if (l.kind != 0 && l.kind != 4 && d > 2000 && d < 12000) add(6, {1, 1, 1}, l.pos, l.name);
    }
    return out.c_str();
}

// The campaign: start it (0: Easy computer lords, 1: Hard), give orders, and read its state.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_start(int hard) {
    if (app) startCampaign(hard != 0);
}
// The pending battle: settle it with the dice, or fight it in 3D.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_resolve() {
    if (app && app->camp.on && app->camp.c->pending()) app->camp.c->resolvePending();
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_fight() {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return;
    App::CampaignView& cv = app->camp;
    const C::Pending* p = cv.c->pending();
    if (!p) return;
    // Fought from the boats, or against them: there is no ground to fight on, so the dice decide.
    if (pendingAfloat(*cv.c)) {
        cv.c->resolvePending();
        return;
    }
    BattlePlan plan;
    plan.city = cv.c->cityAt(p->cx, p->cy);
    plan.centre = plan.city >= 0 ? cv.c->continent().cities()[static_cast<size_t>(plan.city)].pos
                                 : illyria::sim::Vec3{illyria::sim::Fixed::fromInt(C::centre(p->cx)), illyria::sim::Fixed::fromInt(C::centre(p->cy)), {}};
    // Blessings, a hero's battle artifacts and the command artifacts of the
    // heroes alongside all count as extra strength in the 3D battle too.
    const auto command = [](const std::vector<const illyria::sim::Army*>& side) {
        int n = 0;
        for (const illyria::sim::Army* a : side) n += C::command(*a);
        return n;
    };
    const auto take = [](const illyria::sim::Army* a, int command) {
        int arms = 0;
        for (uint8_t k : a->artifacts) arms += C::artifacts()[k].battle;
        return BattleArmy{a->id, a->type, a->name, a->soldiers, a->rank, a->blessings + arms + command};
    };
    const int ours = command(cv.c->pendingAttackers()), theirs = command(cv.c->pendingDefenders());
    for (const illyria::sim::Army* a : cv.c->pendingAttackers()) plan.player.push_back(take(a, ours));
    uint8_t foe = C::kNeutral;
    for (const C::Stack& s : cv.c->stacks())
        if (s.cx == p->cx && s.cy == p->cy && s.owner != 0) foe = s.owner;
    for (const illyria::sim::Army* a : cv.c->pendingDefenders()) plan.foe.push_back(take(a, theirs));
    plan.foeName = plan.city >= 0 ? (foe == C::kNeutral ? "Garrison of " : "Defenders of ") + cv.c->continent().cities()[static_cast<size_t>(plan.city)].name
                                  : cv.c->sides()[foe == C::kNeutral ? 0 : foe].name;
    cv.backdrop = std::move(app->battle);
    app->battle = std::make_unique<Battle>(kBattleSeed + cv.c->day(), plan);
    app->renderer.reset(*app->battle);
    app->renderer.setShowUnits(true);
    cv.on = false;
    cv.fighting = true;
    const V3 o = app->battle->origin();
    app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
    // Frame the player's armies and the ground they march on.
    V3 c = app->battle->squadCentre(app->battle->squads()[0]);
    for (const Squad& s : app->battle->squads())
        if (s.owner == Battle::kPlayer) {
            c = app->battle->squadCentre(s);
            break;
        }
    const V3 at = plan.city >= 0 ? (c + (toV3(plan.centre) - V3{o.x, o.y, 0})) * 0.5f : V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
    app->camera.lookAt(at.x, at.y, 190);
}
// Back from the 3D battle (won, lost or withdrawn): the survivors, their kills
// and their ranks go back on the campaign map.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_return() {
    if (!app || !app->camp.fighting) return;
    App::CampaignView& cv = app->camp;
    std::vector<illyria::sim::Campaign::Outcome> outcomes;
    for (const ArmyResult& r : app->battle->results()) outcomes.push_back({r.id, r.soldiers, r.kills, r.rank});
    cv.c->applyBattle(outcomes, app->battle->outcome() == 1);
    app->battle = std::move(cv.backdrop);
    app->renderer.reset(*app->battle);
    app->renderer.setShowUnits(false);
    cv.fighting = false;
    cv.on = true;
    const V3 o = app->battle->origin();
    app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
    if (const illyria::sim::Campaign::Stack* s = cv.c->findStack(cv.selected)) {
        const V3 p = stackPosition(*cv.c, *s);
        app->camera.lookAt(p.x, p.y, 2600);
    }
}
EMSCRIPTEN_KEEPALIVE int illyria_campaign_fighting() { return app && app->camp.fighting ? 1 : 0; }

// Test hook: march the selected stack on the nearest neutral town.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_test_attack() {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return;
    const C::Stack* s = app->camp.c->findStack(app->camp.selected);
    if (!s) return;
    int best = 1 << 30, target = -1;
    for (size_t i = 0; i < app->camp.c->cities().size(); ++i) {
        const C::CityState& city = app->camp.c->cities()[i];
        if (city.owner != C::kNeutral) continue;
        const int d = std::abs(city.cx - s->cx) + std::abs(city.cy - s->cy);
        if (d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    if (target >= 0) app->camp.c->move(s->id, app->camp.c->cities()[static_cast<size_t>(target)].cx, app->camp.c->cities()[static_cast<size_t>(target)].cy);
}
// Heroes: search the ruin (or sage) the selected stack stands on, or be
// blessed at a temple; take or turn down a hero offering their sword.
EMSCRIPTEN_KEEPALIVE int illyria_campaign_search() {
    if (!app || !app->camp.on) return 0;
    return app->camp.c->search(app->camp.selected) ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE int illyria_campaign_hire(int accept) {
    if (!app || !app->camp.on) return 0;
    return app->camp.c->hire(0, accept != 0) ? 1 : 0;
}
// Test hook: march the selected stack on the nearest unsearched ruin (kind 0), temple (1) or sage (2).
EMSCRIPTEN_KEEPALIVE void illyria_campaign_test_site(int kind) {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return;
    const C::Stack* s = app->camp.c->findStack(app->camp.selected);
    if (!s) return;
    int best = 1 << 30, target = -1;
    for (size_t i = 0; i < app->camp.c->sites().size(); ++i) {
        const C::SiteState& st = app->camp.c->sites()[i];
        const int d = std::max(std::abs(st.cx - s->cx), std::abs(st.cy - s->cy));
        if (static_cast<int>(st.kind) == kind && !st.searched && d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    if (target >= 0) app->camp.c->move(s->id, app->camp.c->sites()[static_cast<size_t>(target)].cx, app->camp.c->sites()[static_cast<size_t>(target)].cy);
}
// Test hook: run the campaign forward a number of days at once.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_fast_forward(int days) {
    if (!app || !app->camp.on) return;
    for (int t = 0; t < days * illyria::sim::Campaign::kTicksPerDay; ++t) app->camp.c->step();
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_leave() {
    if (!app) return;
    app->camp.on = false;
    app->renderer.setShowUnits(true);
    frameCamera();
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_produce(int city, int option) {
    if (app && app->camp.on) app->camp.c->produce(city, option);
}
// Warlords' BUILD: raise the open city's walls by one, for gold.
// Where a city sends its new armies: start choosing (the next city tapped), or -1 to keep them home.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_vector(int city, int choose) {
    if (!app || !app->camp.on) return;
    if (choose) app->camp.vectorFrom = city;
    else app->camp.c->vectorProduction(city, -1), app->camp.vectorFrom = -1;
}
// Sends an army of the selected stack home for good (no more upkeep).
EMSCRIPTEN_KEEPALIVE void illyria_campaign_disband(int army) {
    if (app && app->camp.on && app->camp.selected) app->camp.c->disband(app->camp.selected, static_cast<uint32_t>(army));
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_fortify(int city) {
    if (app && app->camp.on) app->camp.c->fortify(city);
}
// Boats: lay one down in the open city's port; board or leave one with the selected stack.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_build_boat(int city) {
    if (app && app->camp.on) app->camp.c->buildBoat(city);
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_embark() {
    if (app && app->camp.on && app->camp.selected) app->camp.c->embark(app->camp.selected);
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_land() {
    if (app && app->camp.on && app->camp.selected) app->camp.c->land(app->camp.selected);
}
// Test hook: order the selected stack to a cell (by land or by sea).
EMSCRIPTEN_KEEPALIVE int illyria_campaign_test_move(int cx, int cy) {
    if (!app || !app->camp.on || !app->camp.selected) return 0;
    return app->camp.c->move(app->camp.selected, cx, cy) ? 1 : 0;
}
// Test hook: look at the selected stack from this distance.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_test_look(float distance) {
    if (!app || !app->camp.on) return;
    if (const illyria::sim::Campaign::Stack* s = app->camp.c->findStack(app->camp.selected)) {
        const V3 p = stackPosition(*app->camp.c, *s);
        app->camera.lookAt(p.x, p.y, distance);
    }
}
// Test hook: hand the player the northernmost port, a stack there and a boat
// built for it, and look at it with the city panel open.
EMSCRIPTEN_KEEPALIVE int illyria_campaign_test_port() {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return -1;
    App::CampaignView& cv = app->camp;
    int32_t port = -1;
    for (size_t i = 0; i < cv.c->cities().size(); ++i)
        if (cv.c->cities()[i].port && (port < 0 || cv.c->cities()[i].cy > cv.c->cities()[static_cast<size_t>(port)].cy)) port = static_cast<int32_t>(i);
    if (port < 0) return -1;
    cv.selected = cv.c->grant(port, 0, illyria::sim::UnitType::HeavyInfantry, 5);
    cv.c->buildBoat(port);
    for (int t = 0; t < (C::kBoatDays + 1) * C::kTicksPerDay && !cv.c->pending(); ++t) cv.c->step();
    cv.city = port;
    const C::CityState& cs = cv.c->cities()[static_cast<size_t>(port)];
    const V3 o = app->battle->origin();
    app->camera.lookAt(C::centre(cs.cx) - o.x - 150, C::centre(cs.cy) - o.y, 1300);
    return port;
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_close_city() {
    if (app) app->camp.city = -1;
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_pause() {
    if (app) app->camp.paused = !app->camp.paused;
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_speed() {
    if (app) app->camp.speed = app->camp.speed >= 8 ? 1 : app->camp.speed * 2;
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_select(unsigned id) {
    if (!app || !app->camp.on) return;
    app->camp.selected = id;
    if (const illyria::sim::Campaign::Stack* s = app->camp.c->findStack(id)) {
        const V3 p = stackPosition(*app->camp.c, *s);
        app->camera.lookAt(p.x, p.y, std::min(app->camera.distance(), 2600.0f));
    }
}
// Everything the campaign HUD shows, as JSON: the day and the treasury, every
// banner on screen, the selected stack's armies and the open city's panel.
EMSCRIPTEN_KEEPALIVE const char* illyria_campaign_state() {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return "{}";
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
    std::string& s = cv.json;
    char buf[512];
    const C::Stack* sel = c.findStack(cv.selected);
    if (!sel) cv.selected = 0;
    std::snprintf(buf, sizeof buf, "{\"day\":%u,\"gold\":%d,\"cities\":%d,\"speed\":%d,\"paused\":%s,\"winner\":%d,\"selected\":%u,\"hard\":%s,\"stacks\":[",
                  c.day() + 1, c.sides()[0].gold, c.citiesOwned(0), cv.speed, cv.paused ? "true" : "false", c.winner(), cv.selected,
                  cv.hard ? "true" : "false");
    s = buf;
    bool first = true;
    for (const C::Stack& st : c.stacks()) {
        if (st.armies.empty()) continue;
        V3 p = stackPosition(c, st);
        p.z += 30;
        float px, py;
        if (!app->camera.project(p, px, py)) continue;
        px /= app->pixelRatio;
        py /= app->pixelRatio;
        if (px < -40 || py < -40 || px > app->camera.width() / app->pixelRatio + 40 || py > app->camera.height() / app->pixelRatio + 40) continue;
        int32_t strongest = 0;
        bool hero = false;
        for (const illyria::sim::Army& a : st.armies) {
            strongest = std::max(strongest, C::strength(a));
            hero = hero || a.hero();
        }
        const V3 col = factionColor(st.owner);
        std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"owner\":%d,\"x\":%.0f,\"y\":%.0f,\"strength\":%d,\"count\":%zu,\"hero\":%s,\"moving\":%s,\"boat\":%s,\"color\":\"#%02x%02x%02x\"}",
                      first ? "" : ",", st.id, st.owner, px, py, strongest, st.armies.size(), hero ? "true" : "false",
                      st.path.empty() ? "false" : "true", st.boat ? "true" : "false", static_cast<int>(col.x * 255), static_cast<int>(col.y * 255),
                      static_cast<int>(col.z * 255));
        s += buf;
        first = false;
    }
    s += "],\"armies\":[";
    if (sel) {
        for (size_t k = 0; k < sel->armies.size(); ++k) {
            const illyria::sim::Army& a = sel->armies[k];
            std::string arts;
            for (uint8_t i : a.artifacts) arts += (arts.empty() ? "" : ", ") + std::string(C::artifacts()[i].name);
            std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"name\":\"%s\",\"type\":\"%s\",\"strength\":%d,\"rank\":%d,\"level\":%d,\"blessings\":%d,\"soldiers\":%d,\"full\":%d,\"kills\":%d,"
                          "\"ally\":%s,\"command\":%d,\"artifacts\":\"",
                          k ? "," : "", a.id, a.name.c_str(), illyria::sim::unitTypeName(a.type), C::strength(a), a.hero() ? 0 : a.rank,
                          a.hero() ? a.rank + 1 : 0, a.blessings, a.soldiers, a.fullStrength, a.kills, a.ally ? "true" : "false", C::command(a));
            s += buf;
            s += arts + "\"}";
        }
    }
    s += "]";
    // The ruin, temple or sage the selected stack stands on, and whether it can search there.
    if (sel && sel->path.empty()) {
        const int32_t site = c.siteAt(sel->cx, sel->cy);
        if (site >= 0) {
            static const char* kKinds[] = {"ruin", "temple", "sage"};
            const C::SiteState& st = c.sites()[static_cast<size_t>(site)];
            bool hero = false;
            for (const illyria::sim::Army& a : sel->armies) hero = hero || a.hero();
            std::snprintf(buf, sizeof buf, ",\"site\":{\"name\":\"%s\",\"kind\":\"%s\",\"can\":%s,\"searched\":%s,\"hero\":%s}",
                          c.continent().sites()[static_cast<size_t>(site)].name.c_str(), kKinds[static_cast<int>(st.kind)],
                          c.canSearch(sel->id) ? "true" : "false", st.searched ? "true" : "false", hero ? "true" : "false");
            s += buf;
        }
    }
    // A hero offering their sword.
    if (c.offer(0).open) {
        const C::Offer& o = c.offer(0);
        std::snprintf(buf, sizeof buf, ",\"offer\":{\"name\":\"%s\",\"price\":%d,\"city\":\"%s\"}", o.name.c_str(), o.price,
                      c.continent().cities()[static_cast<size_t>(o.city)].name.c_str());
        s += buf;
    }
    // The selected stack's boat, or the boat it could board here.
    if (sel) {
        if (const C::Boat* b = sel->boat ? c.findBoat(sel->boat) : nullptr) {
            std::snprintf(buf, sizeof buf, ",\"aboard\":{\"name\":\"%s\",\"count\":%zu,\"cap\":%d,\"canLand\":%s}", b->name.c_str(), sel->armies.size(),
                          C::kBoatCapacity, c.navigable(sel->cx, sel->cy) ? "true" : "false");
            s += buf;
        } else if (const C::Boat* p = c.boatInPort(c.cityAt(sel->cx, sel->cy)); p && p->owner == sel->owner) {
            std::snprintf(buf, sizeof buf, ",\"embark\":{\"name\":\"%s\",\"cap\":%d}", p->name.c_str(), C::kBoatCapacity);
            s += buf;
        }
    }
    if (cv.city >= 0) {
        const C::CityState& cs = c.cities()[static_cast<size_t>(cv.city)];
        int docked = 0;
        for (const C::Boat& b : c.boats()) docked += b.port == cv.city && !b.stack;
        std::snprintf(buf, sizeof buf,
                      ",\"city\":{\"index\":%d,\"name\":\"%s\",\"defense\":%d,\"income\":%d,\"producing\":%d,\"progress\":%d,"
                      "\"port\":%s,\"boatDays\":%d,\"boats\":%d,\"boatCost\":%d,\"boatTime\":%d,\"boatUpkeep\":%d,\"wallsCost\":%d,"
                      "\"vectorTo\":\"%s\",\"choosing\":%s,\"options\":[",
                      cv.city, c.continent().cities()[static_cast<size_t>(cv.city)].name.c_str(), cs.defense, cs.income, cs.producing, cs.progress,
                      cs.port ? "true" : "false", cs.boatDays, docked, C::kBoatCost, C::kBoatDays, C::kBoatUpkeep, cs.defense < 9 ? c.fortifyCost(cv.city) : 0,
                      cs.vectorTo >= 0 ? c.continent().cities()[static_cast<size_t>(cs.vectorTo)].name.c_str() : "",
                      cv.vectorFrom == cv.city ? "true" : "false");
        s += buf;
        for (size_t k = 0; k < cs.options.size(); ++k) {
            const C::Option& op = cs.options[k];
            std::snprintf(buf, sizeof buf, "%s{\"type\":\"%s\",\"days\":%d,\"cost\":%d,\"upkeep\":%d,\"strength\":%d}", k ? "," : "",
                          illyria::sim::unitTypeName(op.type), op.days, op.cost, C::upkeep(op.type), illyria::sim::warlordsStrength(op.type));
            s += buf;
        }
        s += "]}";
    }
    // A battle waiting for the player's choice.
    if (const C::Pending* p = c.pending()) {
        const int32_t city = c.cityAt(p->cx, p->cy);
        int32_t mine = 0, theirs = 0;
        for (const illyria::sim::Army* a : c.pendingAttackers()) mine += C::strength(*a);
        for (const illyria::sim::Army* a : c.pendingDefenders()) theirs += C::strength(*a);
        std::snprintf(buf, sizeof buf, ",\"pending\":{\"where\":\"%s\",\"mine\":%zu,\"theirs\":%zu,\"myStrength\":%d,\"theirStrength\":%d,\"afloat\":%s}",
                      city >= 0 ? c.continent().cities()[static_cast<size_t>(city)].name.c_str() : c.navigable(p->cx, p->cy) ? "sea" : "the open field",
                      c.pendingAttackers().size(), c.pendingDefenders().size(), mine, theirs, pendingAfloat(c) ? "true" : "false");
        s += buf;
    }
    // The latest fights and the player's heroes' deeds, newest first.
    s += ",\"news\":[";
    std::vector<const C::Deed*> deeds;
    for (size_t k = c.deeds().size(); k-- > 0 && deeds.size() < 4;)
        if (c.deeds()[k].side == 0) deeds.push_back(&c.deeds()[k]);
    size_t fk = 0, dk = 0;
    const size_t nf = c.fights().size();
    for (size_t k = 0; k < 5; ++k) {
        const bool fightLeft = fk < std::min<size_t>(nf, 4), deedLeft = dk < deeds.size();
        if (!fightLeft && !deedLeft) break;
        if (deedLeft && (!fightLeft || deeds[dk]->day >= c.fights()[nf - 1 - fk].day)) {
            std::snprintf(buf, sizeof buf, "%s\"Day %u: %s\"", k ? "," : "", deeds[dk]->day + 1, deeds[dk]->text.c_str());
            s += buf;
            ++dk;
            continue;
        }
        const C::Fight& f = c.fights()[nf - 1 - fk++];
        const std::string where = f.city >= 0 ? c.continent().cities()[static_cast<size_t>(f.city)].name : std::string("the field");
        const auto sideName = [&](uint8_t side) { return side == C::kNeutral ? std::string("the garrison") : c.sides()[side].name; };
        if (f.captured) std::snprintf(buf, sizeof buf, "%s\"Day %u: %s took %s\"", k ? "," : "", f.day + 1, sideName(f.attacker).c_str(), where.c_str());
        else if (f.attackerWon) std::snprintf(buf, sizeof buf, "%s\"Day %u: %s beat %s at %s\"", k ? "," : "", f.day + 1, sideName(f.attacker).c_str(), sideName(f.defender).c_str(), where.c_str());
        else std::snprintf(buf, sizeof buf, "%s\"Day %u: %s were thrown back at %s\"", k ? "," : "", f.day + 1, sideName(f.attacker).c_str(), where.c_str());
        s += buf;
    }
    s += "]}";
    return s.c_str();
}

// HUD state as JSON, polled by the page a few times a second.
EMSCRIPTEN_KEEPALIVE const char* illyria_hud() {
    if (!app) return "{}";
    const Battle& b = *app->battle;
    std::string& s = app->hud;
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"version\":\"%s\",\"tick\":%u,\"paused\":%s,\"speed\":%d,\"outcome\":%d,\"fps\":%.0f,"
                  "\"stepMs\":%.3f,\"zoom\":%.0f,\"selected\":%d,\"scenario\":%d,\"foe\":\"%s\",",
                  ILLYRIA_VERSION, b.world().tick(), b.paused() ? "true" : "false", b.speed(), b.outcome(), app->fps,
                  b.stepMs(), app->camera.distance(), b.selectedSquad(), b.scenario() == Scenario::Siege ? 1 : 0, b.foeName().c_str());
    s = buf;
    // The selection (how many, in what formation, of what kind) and the saved groups.
    {
        const auto& units = b.world().units();
        const auto typeName = [&](illyria::sim::EntityId id) {
            return illyria::sim::unitTypeName(static_cast<illyria::sim::UnitType>(units[id - 1].stats.type));
        };
        const auto label = [&](const std::vector<illyria::sim::EntityId>& ids) {
            bool one = true;
            for (illyria::sim::EntityId id : ids) one = one && units[id - 1].stats.type == units[ids.front() - 1].stats.type;
            return ids.empty() ? "" : one ? typeName(ids.front()) : "Mixed";
        };
        const std::vector<illyria::sim::EntityId> sel = b.selectedAlive();
        std::snprintf(buf, sizeof buf, "\"selCount\":%zu,\"selLabel\":\"%s\",\"formation\":\"%s\",\"formationId\":%d,\"groups\":[",
                      sel.size(), label(sel), formationName(b.selectionFormation()), static_cast<int>(b.selectionFormation()));
        s += buf;
        bool first = true;
        for (int k = 1; k <= Battle::kGroupSlots; ++k) {
            const int slot = k % Battle::kGroupSlots;
            const Battle::Group& g = b.group(slot);
            if (g.units.empty()) continue;
            int alive = 0;
            for (illyria::sim::EntityId id : g.units) alive += units[id - 1].alive();
            std::snprintf(buf, sizeof buf, "%s{\"slot\":%d,\"alive\":%d,\"total\":%zu,\"label\":\"%s\",\"sel\":%d}", first ? "" : ",", slot,
                          alive, g.units.size(), label(g.units), b.groupSelection(slot));
            s += buf;
            first = false;
        }
        s += "],\"squads\":[";
    }
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& q = b.squads()[i];
        static const char* kRoles[] = {"melee", "ranged", "bomber", "caster", "healer", "flier", "siege"};
        const Standing st = b.standing(q);
        std::snprintf(buf, sizeof buf,
                      "%s{\"name\":\"%s\",\"type\":\"%s\",\"owner\":%d,\"role\":\"%s\",\"formation\":\"%s\",\"alive\":%d,\"total\":%zu,"
                      "\"strength\":%d,\"rank\":%d,\"level\":%d,\"blessings\":%d,\"kills\":%d,\"broken\":%d,\"sel\":%d}",
                      i ? "," : "", q.name.c_str(), illyria::sim::unitTypeName(q.type), q.owner,
                      kRoles[static_cast<int>(roleOf(q.type))], formationName(q.formation),
                      b.aliveCount(q), q.members.size(), st.strength, st.rank, st.level, st.blessings, st.kills, st.broken,
                      q.owner == Battle::kPlayer ? b.squadSelection(q) : 0);
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
        if (app->camp.on) campaignTap(x, y);
        else app->battle->tap(app->camera, x, y, app->pixelRatio, app->shift);
    });
    // A press held on the battlefield: a selection box, or a move's facing line.
    app->gestures->setHold(
        [](float x, float y, bool box) { return !app->camp.on && app->battle->pressBegin(app->camera, x, y, app->pixelRatio, box); },
        [](float x, float y) { app->battle->pressMove(app->camera, x, y); },
        [](float x, float y, bool cancelled) { app->battle->pressEnd(app->camera, x, y, cancelled, app->shift); });
    newBattle();
    resizeCanvas();
    if (!app->renderer.init(*app->battle)) {
        EM_ASM({ if (window.illyriaFatal) window.illyriaFatal("Graphics initialisation failed."); });
        return 1;
    }
    fetchPrerenderedSheets(app->renderer);
    startIntro();
    EM_ASM({ if (window.illyriaReady) window.illyriaReady(); });
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
