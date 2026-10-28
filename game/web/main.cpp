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
#include "illyria/sim/campaign.h"

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

// Where a campaign stack stands now, in battle-local metres: between the cell
// it holds and the next on its road, by how far it has got.
V3 stackPosition(const illyria::sim::Campaign& c, const illyria::sim::Campaign::Stack& s) {
    using C = illyria::sim::Campaign;
    const V3 o = app->battle->origin();
    float x = static_cast<float>(C::centre(s.cx)), y = static_cast<float>(C::centre(s.cy));
    if (!s.path.empty()) {
        int32_t cost = 0;
        for (const illyria::sim::Army& a : s.armies) cost = std::max(cost, C::moveCost(a.type, c.terrainAt(s.path.front().first, s.path.front().second)));
        const float t = cost > 0 ? std::clamp(static_cast<float>(s.progress) / (cost * C::kTicksPerDay), 0.0f, 1.0f) : 0;
        x += (C::centre(s.path.front().first) - x) * t;
        y += (C::centre(s.path.front().second) - y) * t;
    }
    V3 p{x - o.x, y - o.y, 0};
    p.z = app->battle->heightAt(p.x, p.y);
    return p;
}

void campaignTap(float px, float py) {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
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

void startCampaign() {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    cv.continent = app->battle->continentShared();
    cv.c = std::make_unique<C>(*cv.continent, kBattleSeed);
    cv.c->setHuman(0, true);
    cv.on = true;
    cv.paused = false;
    cv.speed = 1;
    cv.selected = 0;
    cv.city = -1;
    app->renderer.setShowUnits(false);
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
    resizeCanvas();
    stepIntro(now);
    if (app->camp.on) {
        // Ten campaign ticks a second, times the speed.
        App::CampaignView& cv = app->camp;
        if (!cv.paused && cv.c->winner() == 0) {
            cv.acc += dtMs / 1000.0 * illyria::sim::Campaign::kTicksPerSecond * cv.speed;
            for (int k = 0; k < 40 && cv.acc >= 1; ++k, cv.acc -= 1) cv.c->step();
            cv.acc = std::min(cv.acc, 1.0);
        }
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
        for (const auto& site : c.sites()) add(2 + static_cast<int>(site.kind), {1, 1, 1}, site.pos, site.name);
    }
    for (const auto& l : c.labels()) {
        if (l.kind == 0 && d > 3000) add(5, {1, 1, 1}, l.pos, l.name);
        else if (l.kind != 0 && l.kind != 4 && d > 2000 && d < 12000) add(6, {1, 1, 1}, l.pos, l.name);
    }
    return out.c_str();
}

// The campaign: start it, give orders, and read its state.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_start() {
    if (app) startCampaign();
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
    BattlePlan plan;
    plan.city = cv.c->cityAt(p->cx, p->cy);
    plan.centre = plan.city >= 0 ? cv.c->continent().cities()[static_cast<size_t>(plan.city)].pos
                                 : illyria::sim::Vec3{illyria::sim::Fixed::fromInt(C::centre(p->cx)), illyria::sim::Fixed::fromInt(C::centre(p->cy)), {}};
    const auto take = [](const illyria::sim::Army* a) {
        return BattleArmy{a->id, a->type, a->name, a->soldiers, a->rank, a->blessings};
    };
    for (const illyria::sim::Army* a : cv.c->pendingAttackers()) plan.player.push_back(take(a));
    uint8_t foe = C::kNeutral;
    for (const C::Stack& s : cv.c->stacks())
        if (s.cx == p->cx && s.cy == p->cy && s.owner != 0) foe = s.owner;
    for (const illyria::sim::Army* a : cv.c->pendingDefenders()) plan.foe.push_back(take(a));
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
    std::snprintf(buf, sizeof buf, "{\"day\":%u,\"gold\":%d,\"cities\":%d,\"speed\":%d,\"paused\":%s,\"winner\":%d,\"selected\":%u,\"stacks\":[",
                  c.day() + 1, c.sides()[0].gold, c.citiesOwned(0), cv.speed, cv.paused ? "true" : "false", c.winner(), cv.selected);
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
        std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"owner\":%d,\"x\":%.0f,\"y\":%.0f,\"strength\":%d,\"count\":%zu,\"hero\":%s,\"moving\":%s,\"color\":\"#%02x%02x%02x\"}",
                      first ? "" : ",", st.id, st.owner, px, py, strongest, st.armies.size(), hero ? "true" : "false",
                      st.path.empty() ? "false" : "true", static_cast<int>(col.x * 255), static_cast<int>(col.y * 255), static_cast<int>(col.z * 255));
        s += buf;
        first = false;
    }
    s += "],\"armies\":[";
    if (sel) {
        for (size_t k = 0; k < sel->armies.size(); ++k) {
            const illyria::sim::Army& a = sel->armies[k];
            std::snprintf(buf, sizeof buf, "%s{\"name\":\"%s\",\"type\":\"%s\",\"strength\":%d,\"rank\":%d,\"level\":%d,\"blessings\":%d,\"soldiers\":%d,\"full\":%d,\"kills\":%d}",
                          k ? "," : "", a.name.c_str(), illyria::sim::unitTypeName(a.type), C::strength(a), a.hero() ? 0 : a.rank,
                          a.hero() ? a.rank + 1 : 0, a.blessings, a.soldiers, a.fullStrength, a.kills);
            s += buf;
        }
    }
    s += "]";
    if (cv.city >= 0) {
        const C::CityState& cs = c.cities()[static_cast<size_t>(cv.city)];
        std::snprintf(buf, sizeof buf, ",\"city\":{\"index\":%d,\"name\":\"%s\",\"defense\":%d,\"income\":%d,\"producing\":%d,\"progress\":%d,\"options\":[",
                      cv.city, c.continent().cities()[static_cast<size_t>(cv.city)].name.c_str(), cs.defense, cs.income, cs.producing, cs.progress);
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
        std::snprintf(buf, sizeof buf, ",\"pending\":{\"where\":\"%s\",\"mine\":%zu,\"theirs\":%zu,\"myStrength\":%d,\"theirStrength\":%d}",
                      city >= 0 ? c.continent().cities()[static_cast<size_t>(city)].name.c_str() : "the open field", c.pendingAttackers().size(),
                      c.pendingDefenders().size(), mine, theirs);
        s += buf;
    }
    // The latest fights, newest first.
    s += ",\"news\":[";
    for (size_t k = 0, n = c.fights().size(); k < std::min<size_t>(n, 4); ++k) {
        const C::Fight& f = c.fights()[n - 1 - k];
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
                  "\"stepMs\":%.3f,\"zoom\":%.0f,\"selected\":%d,\"scenario\":%d,\"foe\":\"%s\",\"squads\":[",
                  ILLYRIA_VERSION, b.world().tick(), b.paused() ? "true" : "false", b.speed(), b.outcome(), app->fps,
                  b.stepMs(), app->camera.distance(), b.selectedSquad(), b.scenario() == Scenario::Siege ? 1 : 0, b.foeName().c_str());
    s = buf;
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& q = b.squads()[i];
        static const char* kRoles[] = {"melee", "ranged", "bomber", "caster", "healer", "flier"};
        const Standing st = b.standing(q);
        std::snprintf(buf, sizeof buf,
                      "%s{\"name\":\"%s\",\"type\":\"%s\",\"owner\":%d,\"role\":\"%s\",\"formation\":\"%s\",\"alive\":%d,\"total\":%zu,"
                      "\"strength\":%d,\"rank\":%d,\"level\":%d,\"blessings\":%d,\"kills\":%d}",
                      i ? "," : "", q.name.c_str(), illyria::sim::unitTypeName(q.type), q.owner,
                      kRoles[static_cast<int>(roleOf(q.type))], formationName(q.formation),
                      b.aliveCount(q), q.members.size(), st.strength, st.rank, st.level, st.blessings, st.kills);
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
        else app->battle->tap(app->camera, x, y, app->pixelRatio);
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
