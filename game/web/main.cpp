// Browser entry point: WebGL 2 context, main loop, and the C functions the
// page's JavaScript calls for input and HUD state.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// Hands a voice cue to the page (game/web/voice.js), which plays it.
EM_JS(void, play_voice_cue, (const char* set, const char* situation, int enemy, float pan, float gain, int priority, int speaker, int tick), {
    if (window.IllyriaVoice) window.IllyriaVoice.cue(UTF8ToString(set), UTF8ToString(situation), !!enemy, pan, gain, priority, speaker, tick);
});

// Hands a sound effect cue, and the ambience, to the page (game/web/sfx.js).
EM_JS(void, play_sfx_cue, (const char* id, float gain, float pan, float dist, float delay, float doppler, int priority, int count, int tick), {
    if (window.IllyriaSfx) window.IllyriaSfx.cue(UTF8ToString(id), gain, pan, dist, delay, doppler, priority, count, tick);
});
EM_JS(void, set_sfx_ambience, (float wm, float wv, float sw, float swp, float fl, float flp, float dr, float drp, float rain), {
    if (window.IllyriaSfx) window.IllyriaSfx.ambience(wm, wv, sw, swp, fl, flp, dr, drp, rain);
});

// The battle's sounds since the last frame, and the ambience when it changed.
void playSounds(Sfx& sfx, uint32_t& ambienceSeen) {
    for (const SfxCue& c : sfx.take())
        play_sfx_cue(soundName(c.sound), c.gain, c.pan, c.distance, c.delay, c.doppler, c.priority, c.count, static_cast<int>(c.tick));
    if (sfx.ambienceVersion() != ambienceSeen) {
        ambienceSeen = sfx.ambienceVersion();
        const Ambience& a = sfx.ambience();
        set_sfx_ambience(a.windMountain, a.windValley, a.swamp, a.swampPan, a.flies, a.fliesPan, a.drums, a.drumsPan, a.rain);
    }
}

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
    bool dynamicResolution = true;  // off: always the settings' ceiling
    double slowSince = -1, fastSince = -1;
    double fps = 60;
    float rain = 0;              // weather hook (illyria_set_rain)
    uint32_t ambienceSeen = ~0u;  // the ambience last sent to the page
    bool hushed = false;          // ambience silenced on the campaign map
    double drawMs = 0;  // CPU time preparing and issuing a frame's drawing (smoothed)
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
        // What the renderer was last given: the towns' holders (tower banners) and the land's reckoning.
        std::vector<uint8_t> owners;
        int64_t territoryTick = -1;
    } camp;
    // The title screen: the battlefield, still, the camera slowly circling it
    // (cheaply: low resolution, every other frame) behind the page's menu.
    bool title = false;
    V3 titleAt;
    unsigned frameCount = 0;
    std::string saved, error;  // the last save written, and why the last load failed
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
    if (app->title) app->pixelRatio = std::min(app->pixelRatio, 1.0f);  // behind the menu: cheap
    const int w = std::max(1, static_cast<int>(cssW * app->pixelRatio));
    const int h = std::max(1, static_cast<int>(cssH * app->pixelRatio));
    int curW = 0, curH = 0;
    emscripten_get_canvas_element_size("#canvas", &curW, &curH);
    if (curW != w || curH != h) emscripten_set_canvas_element_size("#canvas", w, h);
    app->camera.setViewport(static_cast<float>(w), static_cast<float>(h));
    app->gestures->setPixelScale(app->pixelRatio);
    // Sprite sheets should look sharp at up to two device pixels a CSS pixel (closeup.h).
    app->renderer.setCloseupScreen(static_cast<float>(cssH * std::min(emscripten_get_device_pixel_ratio(), 2.0)));
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

// The flags on poles, every frame: on the campaign map the banners rising
// over towns being taken (the old colours coming down as the new go up) and
// the outposts' flags, with the towns' tower banners and the land in their
// holders' colours; in a siege, the capture point's pole.
void placeFlags() {
    using C = illyria::sim::Campaign;
    std::vector<Renderer::Flag> flags;
    const V3 o = app->battle->origin();
    if (app->camp.on) {
        App::CampaignView& cv = app->camp;
        const C& c = *cv.c;
        std::vector<uint8_t> owners;
        for (const C::CityState& cs : c.cities()) owners.push_back(cs.owner);
        if (owners != cv.owners) {
            cv.owners = owners;
            std::vector<V3> colours;
            for (uint8_t w : owners) colours.push_back(factionColor(w));
            app->renderer.setCityColours(colours);
        }
        const int64_t reckoned = c.tick() / C::kTerritoryTicks;
        if (reckoned != cv.territoryTick) {
            cv.territoryTick = reckoned;
            std::vector<uint8_t> rgba(c.hexes().size() * 4, 0);
            for (size_t k = 0; k < c.hexes().size(); ++k) {
                const C::Hex& h = c.hexes()[k];
                if (h.owner >= C::kNeutral) continue;
                const V3 col = factionColor(h.owner);
                rgba[k * 4] = static_cast<uint8_t>(col.x * 255);
                rgba[k * 4 + 1] = static_cast<uint8_t>(col.y * 255);
                rgba[k * 4 + 2] = static_cast<uint8_t>(col.z * 255);
                rgba[k * 4 + 3] = static_cast<uint8_t>(90 + 165 * h.claim / C::kClaimFull);  // firmer, stronger
            }
            app->renderer.setTerritory(rgba, C::kHexCols, C::kHexRows, 1.0f);
        }
        // Larger than life from high up, so they read on a phone.
        const float s = std::clamp(app->camera.distance() / 2600.0f, 0.4f, 3.0f);
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const C::CityState& cs = c.cities()[i];
            if (cs.banner == C::kNeutral) continue;
            const illyria::sim::Continent::Castle& k = c.continent().cities()[i].castle;
            V3 base = toV3(k.keep) - V3{o.x, o.y, 0};
            base.z = app->battle->heightAt(base.x, base.y) + static_cast<float>(k.keepHeight.toDouble());
            const float p = static_cast<float>(cs.bannerProgress) / C::kBannerFull;
            flags.push_back({base, 70 * s, 1 - p, 26 * s, factionColor(cs.owner)});
            flags.push_back({base, 70 * s, p, 26 * s, factionColor(cs.banner)});
        }
        for (const C::Outpost& op : c.outposts()) {
            V3 base{C::centre(op.cx) - o.x, C::centre(op.cy) - o.y, 0};
            base.z = app->battle->heightAt(base.x, base.y);
            const C::Hex& h = c.hexes()[static_cast<size_t>(op.hex)];
            flags.push_back({base, 40 * s, h.owner < C::kNeutral ? 1.0f : 0.15f, 16 * s, factionColor(h.owner)});
        }
    } else {
        app->renderer.setTerritory({}, 0, 0, 0);
        const CapturePoint& cap = app->battle->capturePoint();
        if (cap.active()) {
            V3 base = toV3(cap.at());
            base.z = app->battle->heightAt(base.x, base.y);
            const float p = static_cast<float>(cap.progress()) / CapturePoint::kFull;
            flags.push_back({base, 14, 1 - p, 3.4f, {0.86f, 0.18f, 0.14f}});  // the battle's colours: the defenders red
            flags.push_back({base, 14, p, 3.4f, {0.22f, 0.42f, 0.92f}});  // ... the player blue
        }
    }
    app->renderer.setFlags(flags);
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
        // A town of ours, or one our banner is going up over (its panel shows how far).
        if (cv.city >= 0 && c.cities()[static_cast<size_t>(cv.city)].owner != 0 && c.cities()[static_cast<size_t>(cv.city)].banner != 0) cv.city = -1;
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

// A new campaign, or one read back from a save.
void startCampaign(std::unique_ptr<illyria::sim::Campaign> campaign) {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    cv.c = std::move(campaign);
    cv.hard = cv.c->sides().size() > 1 && cv.c->sides()[1].difficulty == illyria::sim::Difficulty::Hard;
    cv.owners.clear();
    cv.territoryTick = -1;
    cv.vectorFrom = -1;
    cv.acc = 0;
    app->title = false;
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

void startCampaign(bool hard) {
    App::CampaignView& cv = app->camp;
    cv.continent = app->battle->continentShared();
    auto c = std::make_unique<illyria::sim::Campaign>(*cv.continent, kBattleSeed);
    c->setHuman(0, true);
    c->setComputerDifficulty(hard ? illyria::sim::Difficulty::Hard : illyria::sim::Difficulty::Easy);
    startCampaign(std::move(c));
}

// Out of whatever is being played (a campaign, its 3D battle, a skirmish), back
// to a battlefield to show behind the title screen.
void stopPlaying() {
    App::CampaignView& cv = app->camp;
    if (cv.fighting) {
        app->battle = std::move(cv.backdrop);
        app->renderer.reset(*app->battle);
        cv.fighting = false;
    }
    if (cv.on) {
        cv.on = false;
        cv.city = -1;
        cv.selected = 0;
        app->renderer.setTerritory({}, 0, 0, 0);
        app->renderer.setBoats({});
        app->renderer.setPiers({});
    }
    app->renderer.setShowUnits(true);
    app->battle->voice().take();
    app->battle->sfx().take();
    set_sfx_ambience(0, 0, 0, 0, 0, 0, 0, 0, 0);
    app->ambienceSeen = ~0u;
}

void startTitle() {
    stopPlaying();
    app->title = true;
    app->introStartMs = -1;
    // Circle the player's army, near enough to see men and banners.
    const Battle& b = *app->battle;
    V3 sum{0, 0, 0};
    int n = 0;
    for (const Squad& q : b.squads()) {
        if (q.owner != Battle::kPlayer || b.aliveCount(q) == 0) continue;
        sum = sum + b.squadCentre(q);
        ++n;
    }
    app->titleAt = n ? sum * (1.0f / static_cast<float>(n)) : V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
    app->camera.lookAt(app->titleAt.x, app->titleAt.y, 95);
}

void stepTitle(double dtMs) {
    app->camera.rotate(static_cast<float>(dtMs / 1000.0 * 0.035));  // a full turn in three minutes
    app->camera.lookAt(app->titleAt.x, app->titleAt.y, 95);
}

// The other game modes (Horde, Capture the Flag: game/src/mode_*.cpp) are
// listed and started here: otherModeAvailable says whether one is built in,
// startOtherMode starts it (stopPlaying() first) and is true when it has. See
// docs/GAME_DESIGN.md §4.6 for the contract; until a mode is built in, the
// title screen marks it "coming soon".
bool otherModeAvailable(const std::string& id) {
    (void)id;
    return false;
}
bool startOtherMode(const std::string& id, const std::string& options) {
    (void)options;
    if (!otherModeAvailable(id)) return false;
    stopPlaying();
    return false;
}

// A number in a flat JSON object of options, or the fallback.
double optionNumber(const std::string& json, const char* key, double fallback) {
    const std::string k = std::string("\"") + key + "\":";
    const size_t at = json.find(k);
    if (at == std::string::npos) return fallback;
    return std::atof(json.c_str() + at + k.size());
}

void frame() {
    const double now = emscripten_get_now();
    const double dtMs = app->lastMs > 0 ? now - app->lastMs : 16.7;
    app->lastMs = now;
    app->fps = app->fps * 0.95 + (1000.0 / std::max(dtMs, 1.0)) * 0.05;
    adaptResolution(now);
    ++app->frameCount;
    if (app->title) {
        // Behind the menu: half the frames, and no sharper than a pixel a point.
        stepTitle(dtMs);
        if (app->frameCount % 2) return;
    }
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
        // No battle sounds over the campaign map.
        if (!app->hushed) {
            set_sfx_ambience(0, 0, 0, 0, 0, 0, 0, 0, 0);
            app->hushed = true;
            app->ambienceSeen = ~0u;
        }
    } else if (!app->title) {
        // The soldiers are heard from the view centre, the camera's right to the right.
        const V3 eye = app->camera.eye(), at = app->camera.target();
        const V3 fwd = V3{at.x - eye.x, at.y - eye.y, 0}.normalized();
        Voice& voice = app->battle->voice();
        voice.setListener(at, V3{fwd.y, -fwd.x, 0}, app->camera.distance());
        app->battle->sfx().setListener(at, V3{fwd.y, -fwd.x, 0}, app->camera.distance());
        app->battle->sfx().setRain(app->rain);
        app->battle->advance(dtMs / 1000.0);
        for (const VoiceCue& c : voice.take())
            play_voice_cue(voiceSetName(c.set), situationName(c.situation), c.enemy, c.pan, c.gain, c.priority,
                           static_cast<int>(c.speaker), static_cast<int>(c.tick));
        app->hushed = false;
        playSounds(app->battle->sfx(), app->ambienceSeen);
    }
    placeFlags();
    const double drawStart = emscripten_get_now();
    app->renderer.draw(*app->battle, app->camera, now / 1000.0);
    app->camera.setMinDistance(app->renderer.closeupMinDistance());  // sprites never magnified past 1.5x
    app->drawMs = app->drawMs * 0.9 + (emscripten_get_now() - drawStart) * 0.1;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* illyria_version() { return ILLYRIA_VERSION; }

// The frame rate and where a frame's time goes, for the title panel (battle and campaign alike).
EMSCRIPTEN_KEEPALIVE const char* illyria_perf() {
    static char buf[96];
    if (!app) return "";
    std::snprintf(buf, sizeof buf, "%.0f fps \u00b7 sim %.1f \u00b7 draw %.1f ms \u00b7 %.2gx", app->fps, app->battle ? app->battle->stepMs() : 0.0,
                  app->drawMs, app->ratio);
    return buf;
}

// Test hook: the last `n` voice cues the battle made (JSON), with how many
// were made and held back in all.
EMSCRIPTEN_KEEPALIVE const char* illyria_voice_log(int n) {
    static std::string out;
    if (!app || !app->battle) return "{}";
    const Voice& v = app->battle->voice();
    out = "{\"emitted\":" + std::to_string(v.emitted()) + ",\"dropped\":" + std::to_string(v.dropped()) +
          ",\"tick\":" + std::to_string(app->battle->world().tick()) +
          ",\"cues\":" + v.logJson(static_cast<size_t>(std::max(n, 0))) + "}";
    return out.c_str();
}

// Test hook: the last `n` sound effect cues (JSON), with how many were made
// and held back, how many of each sound, and the ambience now.
EMSCRIPTEN_KEEPALIVE const char* illyria_sfx_log(int n) {
    static std::string out;
    if (!app || !app->battle) return "{}";
    const Sfx& s = app->battle->sfx();
    const Ambience& a = s.ambience();
    char amb[320];
    std::snprintf(amb, sizeof amb,
                  "{\"windMountain\":%.2f,\"windValley\":%.2f,\"swamp\":%.2f,\"flies\":%.2f,\"drums\":%.2f,\"rain\":%.2f}",
                  a.windMountain, a.windValley, a.swamp, a.flies, a.drums, a.rain);
    std::string counts = "{";
    for (int i = 0; i < kSoundCount; ++i) {
        const int k = s.emittedOf(static_cast<Sound>(i));
        if (k == 0) continue;
        if (counts.size() > 1) counts += ",";
        counts += "\"" + std::string(soundName(static_cast<Sound>(i))) + "\":" + std::to_string(k);
    }
    out = "{\"emitted\":" + std::to_string(s.emitted()) + ",\"dropped\":" + std::to_string(s.dropped()) +
          ",\"tick\":" + std::to_string(app->battle->world().tick()) + ",\"ambience\":" + amb + ",\"counts\":" + counts +
          "},\"cues\":" + s.logJson(static_cast<size_t>(std::max(n, 0))) + "}";
    return out.c_str();
}
// Weather hook (none is simulated yet): rain from 0 to 1, thunder above a half.
EMSCRIPTEN_KEEPALIVE void illyria_set_rain(float rain) {
    if (app) app->rain = std::clamp(rain, 0.0f, 1.0f);
}

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
// Ctrl (or Cmd) and Alt held: the next click is an attack on whatever is
// under it (a friend too), or a force march (battle_orders.cpp).
EMSCRIPTEN_KEEPALIVE void illyria_set_keys(int force, int march) {
    if (app) app->battle->setKeys(force != 0, march != 0);
}
// 0 as usual, 1 attack, 2 force march: for one order (the phone's attack button, A, M).
EMSCRIPTEN_KEEPALIVE void illyria_set_order_mode(int mode) {
    if (app && mode >= 0 && mode <= 2) app->battle->setOrderMode(static_cast<Battle::OrderMode>(mode));
}
// What a click at (x, y) CSS pixels would do, for the desktop cursor (Battle::Intent).
EMSCRIPTEN_KEEPALIVE int illyria_hover(float x, float y) {
    if (!app || app->camp.on) return 0;
    const float s = app->pixelRatio;
    return static_cast<int>(app->battle->intentAt(app->camera, x * s, y * s, s));
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

// Test hooks for screenshots of orders: a squad sent (dx, dy) metres from
// where it stands, or after the soldier of squad `foe` nearest it, the
// camera looking on from `distance`.
EMSCRIPTEN_KEEPALIVE void illyria_test_move(int squad, float dx, float dy, float distance) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return;
    Battle& b = *app->battle;
    b.selectSquad(squad);
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(squad)], false);
    V3 at{c.x + dx, c.y + dy, 0};
    at.z = b.heightAt(at.x, at.y);
    b.orderMove(at);
    app->introStartMs = -1;
    app->camera.lookAt((c.x + at.x) / 2, (c.y + at.y) / 2, distance);
}
EMSCRIPTEN_KEEPALIVE void illyria_test_attack(int squad, int foe, float distance) {
    const int n = app ? static_cast<int>(app->battle->squads().size()) : 0;
    if (!app || squad < 0 || squad >= n || foe < 0 || foe >= n) return;
    Battle& b = *app->battle;
    b.selectSquad(squad);
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(squad)], false);
    illyria::sim::EntityId best = 0;
    float bestD = 1e30f;
    for (illyria::sim::EntityId id : b.squads()[static_cast<size_t>(foe)].members) {
        if (!b.world().units()[id - 1].alive()) continue;
        const V3 d = b.unitPosition(id - 1) - c;
        if (d.length() < bestD) {
            bestD = d.length();
            best = id;
        }
    }
    if (best == 0) return;
    b.orderAttackUnit(best);
    const V3 t = b.unitPosition(best - 1);
    app->introStartMs = -1;
    // Nearer the attackers: their lines run off toward the ring.
    app->camera.lookAt(c.x + (t.x - c.x) * 0.4f, c.y + (t.y - c.y) * 0.4f, distance);
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

// Test hook: the camera close on the first squad of a unit type (0 if none in the battle).
EMSCRIPTEN_KEEPALIVE int illyria_test_focus_type(int type, float distance, float yaw) {
    if (!app) return 0;
    for (const Squad& s : app->battle->squads())
        if (static_cast<int>(s.type) == type) {
            const V3 c = app->battle->squadCentre(s);
            app->introStartMs = -1;
            app->camera.lookAt(c.x, c.y, distance);
            app->camera.rotate(yaw - app->camera.yaw());
            return 1;
        }
    return 0;
}

int nearestGate();
// Test hook: the first squad of ours of a unit type sent `inside` metres in
// through the nearest gate, or (onWall) to the wall-walk beside it (2: its
// first soldier alone); the squad's index, or -1.
EMSCRIPTEN_KEEPALIVE int illyria_test_send_type_in(int type, float inside, int onWall) {
    const int g = nearestGate();
    if (!app || g < 0) return -1;
    Battle& b = *app->battle;
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& s = b.squads()[i];
        if (s.owner != Battle::kPlayer || static_cast<int>(s.type) != type) continue;
        const illyria::sim::Gate& gate = b.region().gates()[static_cast<size_t>(g)];
        V3 to = toV3(gate.a + gate.b) * 0.5f - toV3(gate.out) * inside;
        to.z = b.heightAt(to.x, to.y);
        if (onWall) {
            // The embrasure nearest the gate but clear of its towers.
            const V3 from = to;
            float best = 1e30f;
            for (const illyria::sim::Rampart& rp : b.region().ramparts())
                for (const illyria::sim::Rampart::Embrasure& e : rp.embrasures) {
                    const V3 p = toV3(e.pos) - toV3(e.out) * 0.6f;
                    const float d = (p.x - from.x) * (p.x - from.x) + (p.y - from.y) * (p.y - from.y);
                    if (d > 18.0f * 18.0f && d < best) best = d, to = p;
                }
        }
        b.selectSquad(static_cast<int>(i));
        // onWall 2: only its first soldier (his place is the spot itself).
        if (onWall == 2)
            for (illyria::sim::EntityId id : s.members)
                if (b.world().units()[id - 1].alive()) {
                    b.select({id});
                    break;
                }
        b.orderMove(to);
        return static_cast<int>(i);
    }
    return -1;
}
// Test hook: for each living soldier of a squad, how high he stands above the
// ground and what Battle::floorAt gives there ("z-ground/floor-ground;...").
EMSCRIPTEN_KEEPALIVE const char* illyria_test_heights(int squad) {
    static std::string out;
    out.clear();
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return out.c_str();
    const Battle& b = *app->battle;
    for (illyria::sim::EntityId id : b.squads()[static_cast<size_t>(squad)].members) {
        if (!b.world().units()[id - 1].alive()) continue;
        const V3 p = b.unitPosition(id - 1);
        const float g = b.heightAt(p.x, p.y);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.2f/%.2f;", p.z - g, b.floorAt(p) - g);
        out += buf;
    }
    return out.c_str();
}
// Test hook: one soldier picked out of a squad (its first living one).
EMSCRIPTEN_KEEPALIVE void illyria_test_select_one(int squad) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return;
    for (illyria::sim::EntityId id : app->battle->squads()[static_cast<size_t>(squad)].members)
        if (app->battle->world().units()[id - 1].alive()) return app->battle->select({id});
}
// Test hook: the camera close on a squad's first living soldier.
EMSCRIPTEN_KEEPALIVE void illyria_test_view_squad(int squad, float distance, float yaw) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return;
    for (illyria::sim::EntityId id : app->battle->squads()[static_cast<size_t>(squad)].members) {
        if (!app->battle->world().units()[id - 1].alive()) continue;
        const V3 p = app->battle->unitPosition(id - 1);
        app->introStartMs = -1;
        app->camera.lookAt(p.x, p.y, distance);
        app->camera.rotate(yaw - app->camera.yaw());
        return;
    }
}

// Test hook: the camera on a squad's first living soldier from inside the
// town, looking out over him (turned by `turn`), as illyria_test_view_walk.
EMSCRIPTEN_KEEPALIVE void illyria_test_view_squad_from_town(int squad, float distance, float turn) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size()) || app->battle->region().ramparts().empty()) return;
    for (illyria::sim::EntityId id : app->battle->squads()[static_cast<size_t>(squad)].members) {
        if (!app->battle->world().units()[id - 1].alive()) continue;
        const V3 p = app->battle->unitPosition(id - 1);
        const V3 c = toV3(app->battle->region().ramparts().front().centre);
        const float yaw = std::atan2(p.y - c.y, p.x - c.x) + turn;
        app->introStartMs = -1;
        app->camera.lookAt(p.x + std::cos(yaw) * distance * 0.1f, p.y + std::sin(yaw) * distance * 0.1f, distance);
        app->camera.rotate(yaw - app->camera.yaw());
        return;
    }
}

// Test hook: every unit type in this battle and where its sprite sheet came
// from ("Name=pre", "Name=baked" or "Name=none"), comma separated.
EMSCRIPTEN_KEEPALIVE const char* illyria_test_battle_sheets() {
    static std::string out;
    out.clear();
    if (!app) return out.c_str();
    std::set<int> types;
    for (const Squad& s : app->battle->squads()) types.insert(static_cast<int>(s.type));
    for (int t : types) {
        const int source = app->renderer.sheetSource(t);
        if (!out.empty()) out += ',';
        out += illyria::sim::unitTypeName(static_cast<illyria::sim::UnitType>(t));
        out += source > 0 ? "=pre" : (source == 0 ? "=baked" : "=none");
    }
    return out.c_str();
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
    app->title = false;
    newBattle();
    app->renderer.reset(*app->battle);
    frameCamera();
}
EMSCRIPTEN_KEEPALIVE void illyria_restart() {
    if (!app) return;
    app->title = false;
    newBattle();
    app->renderer.reset(*app->battle);
    frameCamera();
}
// Test hook: run the simulation forward instantly.
EMSCRIPTEN_KEEPALIVE void illyria_fast_forward(double seconds) {
    if (!app) return;
    for (double t = 0; t < seconds; t += 0.1) app->battle->advance(0.1);
    app->battle->voice().take();  // what was said while skipping ahead goes unheard
    app->battle->sfx().take();
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
// That gate battered down at once.
EMSCRIPTEN_KEEPALIVE void illyria_test_break_gate() {
    const int g = nearestGate();
    if (g >= 0) app->battle->breakGate(g);
}
// Camera on a defender up on the walls, living (dead = 0) or dead (1): the
// first such, seen from inside the town looking out over him (turned by
// `turn` radians); returns 1 when there is one.
EMSCRIPTEN_KEEPALIVE int illyria_test_view_walk(int dead, float distance, float turn) {
    if (!app) return 0;
    const illyria::sim::Region& rg = app->battle->region();
    for (const illyria::sim::Unit& u : app->battle->world().units()) {
        if (u.owner == illyria::game::Battle::kPlayer || u.alive() == (dead != 0) || u.airborne) continue;
        if (u.pos.z < rg.terrain().heightAt(u.pos.x, u.pos.y) + illyria::sim::Fixed::fromInt(3)) continue;
        const V3 p = toV3(u.pos);
        V3 c{};
        float best = 1e30f;
        for (const illyria::sim::Rampart& rp : rg.ramparts()) {
            const V3 q = toV3(rp.centre);
            const float d = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
            if (d < best) {
                best = d;
                c = q;
            }
        }
        const float yaw = std::atan2(p.y - c.y, p.x - c.x) + turn;
        const float ahead = distance * 0.1f;
        app->introStartMs = -1;
        app->camera.lookAt(p.x + std::cos(yaw) * ahead, p.y + std::sin(yaw) * ahead, distance);
        app->camera.rotate(yaw - app->camera.yaw());
        return 1;
    }
    return 0;
}
// The player's archers loose at the defender up on the walls nearest them.
EMSCRIPTEN_KEEPALIVE void illyria_test_shoot_walk() {
    if (!app) return;
    const illyria::sim::Region& rg = app->battle->region();
    const auto& units = app->battle->world().units();
    V3 from{};
    int n = 0;
    for (const illyria::sim::Unit& u : units) {
        if (u.owner != illyria::game::Battle::kPlayer || u.stats.type != static_cast<uint8_t>(illyria::sim::UnitType::Archers)) continue;
        from = from + toV3(u.pos);
        ++n;
    }
    if (n == 0) return;
    from = from * (1.0f / static_cast<float>(n));
    const illyria::sim::Unit* best = nullptr;
    float bestD = 1e30f;
    for (const illyria::sim::Unit& u : units) {
        if (u.owner == illyria::game::Battle::kPlayer || !u.alive()) continue;
        if (u.pos.z < rg.terrain().heightAt(u.pos.x, u.pos.y) + illyria::sim::Fixed::fromInt(3)) continue;
        const V3 p = toV3(u.pos);
        const float d = (p.x - from.x) * (p.x - from.x) + (p.y - from.y) * (p.y - from.y);
        if (d < bestD) {
            bestD = d;
            best = &u;
        }
    }
    if (best) app->battle->testShootAt(toV3(best->pos));
}
// The player's archers loose at a spot this far inside the town, through its gate.
EMSCRIPTEN_KEEPALIVE void illyria_test_shoot_through_gate(float inside) {
    const int g = nearestGate();
    if (!app || g < 0) return;
    const illyria::sim::Gate& gate = app->battle->region().gates()[static_cast<size_t>(g)];
    const V3 at = toV3(gate.a + gate.b) * 0.5f - toV3(gate.out) * inside;
    app->battle->testShootAt(at);
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

// Test hook: the camera close on the `nth` body that died this way (sim::DeathKind:
// 1 fell, 2 severed, 3 gibbed, 4 pierced, 5 charred); 0 if there is none.
EMSCRIPTEN_KEEPALIVE int illyria_view_death(int kind, int nth, float distance) {
    if (!app || !app->battle) return 0;
    for (const illyria::sim::Unit& u : app->battle->world().units()) {
        if (u.alive() || static_cast<int>(u.death.kind) != kind || nth-- > 0) continue;
        const V3 p = toV3(u.pos);
        illyria_set_camera(p.x, p.y, distance, app->camera.yaw());
        return 1;
    }
    return 0;
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
    app->minPixelRatio = maxPixelRatio >= kMaxPixelRatio || !app->dynamicResolution ? app->maxPixelRatio : 1.0;
}
// Dynamic resolution on or off (Settings, Graphics); takes effect with the next illyria_set_graphics.
EMSCRIPTEN_KEEPALIVE void illyria_set_dynamic_resolution(int on) {
    if (app) app->dynamicResolution = on != 0;
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
    // The campaign's outpost towers (kind 7), in the colours of whoever holds their land.
    if (app->camp.on && d < 9000) {
        const illyria::sim::Campaign& cc = *app->camp.c;
        for (const illyria::sim::Campaign::Outpost& op : cc.outposts()) {
            const uint8_t w = cc.hexes()[static_cast<size_t>(op.hex)].owner;
            const illyria::sim::Vec3 at{illyria::sim::Fixed::fromInt(illyria::sim::Campaign::centre(op.cx)),
                                        illyria::sim::Fixed::fromInt(illyria::sim::Campaign::centre(op.cy)), {}};
            add(7, w < illyria::sim::Campaign::kNeutral ? factionColor(w) : V3{0.85f, 0.85f, 0.85f}, at, d < 4500 && !op.name.empty() ? op.name + " tower" : "");
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
    if (app) app->title = false;
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
    // Whose land it is (the home-land edge): ours, the defenders', or nobody's.
    const uint8_t land = cv.c->landOwner(p->cx, p->cy);
    plan.homeSide = land == 0 ? Battle::kPlayer : land != C::kNeutral && land == foe ? Battle::kEnemy : -1;
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
    // Taken at the capture point: the banner rose in the battle itself.
    cv.c->applyBattle(outcomes, app->battle->outcome() == 1, app->battle->capturePoint().taken());
    app->battle = std::move(cv.backdrop);
    cv.owners.clear();  // the map's dressing goes up again
    cv.territoryTick = -1;
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
// Test hook: settle the pending battle as a 3D victory in which the
// player's armies come out veterans and elites (the hero a level up),
// and open the player's capital.
EMSCRIPTEN_KEEPALIVE void illyria_campaign_test_veterans() {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on || !app->camp.c->pending()) return;
    App::CampaignView& cv = app->camp;
    std::vector<C::Outcome> out;
    int32_t k = 0;
    for (const illyria::sim::Army* a : cv.c->pendingAttackers()) {
        const int32_t rank = a->hero() ? a->rank + 1 : std::min<int32_t>(3, 2 + k++);
        out.push_back({a->id, a->soldiers, 2 + 3 * rank, rank});
    }
    for (const illyria::sim::Army* a : cv.c->pendingDefenders()) out.push_back({a->id, 0, 0, a->rank});
    cv.c->applyBattle(out, true);
    for (size_t i = 0; i < cv.c->cities().size(); ++i)
        if (cv.c->continent().cities()[i].capital && cv.c->cities()[i].owner == 0) cv.city = static_cast<int32_t>(i);
}
// Test hooks for banners. Storm a siege's capture point (gates down, every
// squad of ours to it). Take the neutral town nearest the selected stack (a
// strong stack of ours beside it, the dice settle it), let its banner climb
// for `ticks`, with an enemy stack of `foes` armies next door if asked, and
// look at it from above, paused.
EMSCRIPTEN_KEEPALIVE void illyria_test_storm() {
    if (app && !app->camp.on) app->battle->testStorm();
}
EMSCRIPTEN_KEEPALIVE int illyria_campaign_test_banner(int ticks, int foes) {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return -1;
    App::CampaignView& cv = app->camp;
    const C::Stack* s = cv.c->findStack(cv.selected);
    if (!s) return -1;
    int best = 1 << 30, target = -1;
    for (size_t i = 0; i < cv.c->cities().size(); ++i) {
        const C::CityState& city = cv.c->cities()[i];
        const int d = std::abs(city.cx - s->cx) + std::abs(city.cy - s->cy);
        if (city.owner == C::kNeutral && city.banner == C::kNeutral && d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    if (target < 0) return -1;
    const C::CityState& t = cv.c->cities()[static_cast<size_t>(target)];
    const illyria::sim::UnitType h = illyria::sim::UnitType::HeavyCavalry;
    cv.selected = cv.c->raise(0, t.cx - 1, t.cy, {h, h, h, h, h, h});
    if (foes > 0) cv.c->raise(3, t.cx + 1, t.cy + 1, std::vector<illyria::sim::UnitType>(static_cast<size_t>(foes), illyria::sim::UnitType::LightInfantry));
    cv.c->move(cv.selected, t.cx, t.cy);
    for (int k = 0; k < C::kTicksPerDay && !cv.c->pending(); ++k) cv.c->step();
    if (cv.c->pending()) cv.c->resolvePending();
    for (int k = 0; k < ticks && t.banner != C::kNeutral; ++k) cv.c->step();
    cv.paused = true;
    cv.city = target;
    const V3 o = app->battle->origin();
    const V3 at = toV3(cv.c->continent().cities()[static_cast<size_t>(target)].pos) - V3{o.x, o.y, 0};
    app->camera.lookAt(at.x, at.y - 150, 1500);
    return target;
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
    char buf[1024];
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
        int32_t strongest = 0, vets = 0;
        bool hero = false;
        for (const illyria::sim::Army& a : st.armies) {
            strongest = std::max(strongest, C::strength(a));
            hero = hero || a.hero();
            vets += !a.hero() && a.rank >= 2;  // veterans and elites
        }
        const V3 col = factionColor(st.owner);
        // Standing in a town while its banner goes up: the stack's flag says so.
        const int32_t town = c.cityAt(st.cx, st.cy);
        const int raising = st.path.empty() && town >= 0 && c.cities()[static_cast<size_t>(town)].banner == st.owner
                                ? 100 * c.cities()[static_cast<size_t>(town)].bannerProgress / C::kBannerFull : -1;
        std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"owner\":%d,\"x\":%.0f,\"y\":%.0f,\"strength\":%d,\"count\":%zu,\"hero\":%s,\"vets\":%d,\"moving\":%s,\"boat\":%s,\"raising\":%d,\"color\":\"#%02x%02x%02x\"}",
                      first ? "" : ",", st.id, st.owner, px, py, strongest, st.armies.size(), hero ? "true" : "false", vets,
                      st.path.empty() ? "false" : "true", st.boat ? "true" : "false", raising, static_cast<int>(col.x * 255),
                      static_cast<int>(col.y * 255), static_cast<int>(col.z * 255));
        s += buf;
        first = false;
    }
    // Banners going up over towns: where, whose, how far, and whether enemies near have stopped them.
    s += "],\"banners\":[";
    {
        bool any = false;
        const V3 o = app->battle->origin();
        const auto hex = [](const V3& col) {
            char h[8];
            std::snprintf(h, sizeof h, "#%02x%02x%02x", static_cast<int>(col.x * 255), static_cast<int>(col.y * 255), static_cast<int>(col.z * 255));
            return std::string(h);
        };
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const C::CityState& cs = c.cities()[i];
            if (cs.banner == C::kNeutral) continue;
            const illyria::sim::Continent::City& town = c.continent().cities()[i];
            V3 at = toV3(town.pos) - V3{o.x, o.y, 0};
            at.z = app->battle->heightAt(at.x, at.y) + 10;
            float px, py;
            if (!app->camera.project(at, px, py)) continue;
            px /= app->pixelRatio;
            py /= app->pixelRatio;
            int32_t raisers = 0;
            for (const C::Stack& st : c.stacks())
                if (st.owner == cs.banner && st.cx == cs.cx && st.cy == cs.cy) raisers += static_cast<int32_t>(st.armies.size());
            std::snprintf(buf, sizeof buf,
                          "%s{\"city\":%zu,\"name\":\"%s\",\"x\":%.0f,\"y\":%.0f,\"p\":%d,\"by\":%d,\"owner\":%d,\"byColor\":\"%s\",\"ownerColor\":\"%s\","
                          "\"foes\":%d,\"raisers\":%d,\"byName\":\"%s\"}",
                          any ? "," : "", i, town.name.c_str(), px, py, 100 * cs.bannerProgress / C::kBannerFull, cs.banner, cs.owner,
                          hex(factionColor(cs.banner)).c_str(), hex(factionColor(cs.owner)).c_str(), c.bannerContest(static_cast<int32_t>(i), cs.banner),
                          raisers, c.sides()[cs.banner].name.c_str());
            s += buf;
            any = true;
        }
    }
    std::snprintf(buf, sizeof buf, "],\"land\":%d,\"landGold\":%d", c.hexesHeld(0), c.hexesHeld(0) / C::kHexesPerGold);
    s += buf;
    s += ",\"armies\":[";
    if (sel) {
        for (size_t k = 0; k < sel->armies.size(); ++k) {
            const illyria::sim::Army& a = sel->armies[k];
            std::string arts;
            for (uint8_t i : a.artifacts) arts += (arts.empty() ? "" : ", ") + std::string(C::artifacts()[i].name);
            std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"name\":\"%s\",\"type\":\"%s\",\"strength\":%d,\"rank\":%d,\"level\":%d,\"blessings\":%d,\"soldiers\":%d,\"full\":%d,\"kills\":%d,"
                          "\"ally\":%s,\"rare\":%s,\"command\":%d,\"artifacts\":\"",
                          k ? "," : "", a.id, a.name.c_str(), illyria::sim::unitTypeName(a.type), C::strength(a), a.hero() ? 0 : a.rank,
                          a.hero() ? a.rank + 1 : 0, a.blessings, a.soldiers, a.fullStrength, a.kills, a.ally ? "true" : "false",
                          C::rare(a.type) && !a.hero() ? "true" : "false", C::command(a));
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
        std::snprintf(buf, sizeof buf, ",\"offer\":{\"name\":\"%s\",\"price\":%d,\"city\":\"%s\",\"companion\":\"%s\"}", o.name.c_str(), o.price,
                      c.continent().cities()[static_cast<size_t>(o.city)].name.c_str(), o.companions ? illyria::sim::unitTypeName(o.companion) : "");
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
            std::snprintf(buf, sizeof buf, "%s{\"type\":\"%s\",\"days\":%d,\"cost\":%d,\"upkeep\":%d,\"strength\":%d,\"levy\":%d}", k ? "," : "",
                          illyria::sim::unitTypeName(op.type), op.days, op.cost, C::upkeep(op.type), illyria::sim::warlordsStrength(op.type), op.left);
            s += buf;
        }
        // The rare troops no city trains, and where each can be had.
        s += "],\"rare\":[";
        bool any = false;
        for (int k = 0; k < illyria::sim::kUnitTypeCount; ++k) {
            const auto t = static_cast<illyria::sim::UnitType>(k);
            if (!C::rare(t) || t == illyria::sim::UnitType::Hero) continue;
            static const char* kFrom[] = {"start", "levy", "ruins", "temples", "heroes"};
            std::string from;
            for (int b = 0; b < 5; ++b)
                if (C::sources(t) & (1 << b)) from += std::string(from.empty() ? "" : ",") + kFrom[b];
            std::snprintf(buf, sizeof buf, "%s{\"type\":\"%s\",\"strength\":%d,\"from\":\"%s\"}", any ? "," : "", illyria::sim::unitTypeName(t),
                          illyria::sim::warlordsStrength(t), from.c_str());
            s += buf;
            any = true;
        }
        s += "]";
        const uint8_t land = c.landOwner(cs.cx, cs.cy);
        std::snprintf(buf, sizeof buf, ",\"mine\":%s,\"landOwner\":\"%s\",\"banner\":%d,\"bannerP\":%d,\"bannerBy\":\"%s\",\"bannerFoes\":%d}", cs.owner == 0 ? "true" : "false",
                      land < C::kNeutral ? c.sides()[land].name.c_str() : "", cs.banner == C::kNeutral ? -1 : cs.banner,
                      100 * cs.bannerProgress / C::kBannerFull, cs.banner < C::kNeutral ? c.sides()[cs.banner].name.c_str() : "",
                      cs.banner < C::kNeutral ? c.bannerContest(cv.city, cs.banner) : 0);
        s += buf;
    }
    // A battle waiting for the player's choice.
    if (const C::Pending* p = c.pending()) {
        const int32_t city = c.cityAt(p->cx, p->cy);
        int32_t mine = 0, theirs = 0;
        for (const illyria::sim::Army* a : c.pendingAttackers()) mine += C::strength(*a);
        for (const illyria::sim::Army* a : c.pendingDefenders()) theirs += C::strength(*a);
        // The small edges each side brings (the best of its armies'), and whose land it is.
        const C::Stack* att = c.findStack(p->stack);
        uint8_t defOwner = city >= 0 ? c.cities()[static_cast<size_t>(city)].owner : C::kNeutral;
        for (const C::Stack& st : c.stacks())
            if (city < 0 && st.cx == p->cx && st.cy == p->cy && st.owner != 0 && !st.armies.empty()) defOwner = st.owner;
        const int32_t ax = att ? att->cx : -1, ay = att ? att->cy : -1;
        const std::string myEdges = c.stackEdges(c.pendingAttackers(), 0, p->cx, p->cy, false, ax, ay).describe();
        const std::string theirEdges = c.stackEdges(c.pendingDefenders(), defOwner, p->cx, p->cy, true, ax, ay).describe();
        const uint8_t land = c.landOwner(p->cx, p->cy);
        std::snprintf(buf, sizeof buf,
                      ",\"pending\":{\"where\":\"%s\",\"mine\":%zu,\"theirs\":%zu,\"myStrength\":%d,\"theirStrength\":%d,\"afloat\":%s,"
                      "\"myEdges\":\"%s\",\"theirEdges\":\"%s\",\"land\":\"%s\",\"town\":%s}",
                      city >= 0 ? c.continent().cities()[static_cast<size_t>(city)].name.c_str() : c.navigable(p->cx, p->cy) ? "sea" : "the open field",
                      c.pendingAttackers().size(), c.pendingDefenders().size(), mine, theirs, pendingAfloat(c) ? "true" : "false", myEdges.c_str(),
                      theirEdges.c_str(), land == 0 ? "yours" : land < C::kNeutral ? c.sides()[land].name.c_str() : "", city >= 0 ? "true" : "false");
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
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "{\"version\":\"%s\",\"tick\":%u,\"paused\":%s,\"speed\":%d,\"outcome\":%d,\"fps\":%.0f,"
                  "\"stepMs\":%.3f,\"zoom\":%.0f,\"selected\":%d,\"scenario\":%d,\"foe\":\"%s\",",
                  ILLYRIA_VERSION, b.world().tick(), b.paused() ? "true" : "false", b.speed(), b.outcome(), app->fps,
                  b.stepMs(), app->camera.distance(), b.selectedSquad(), b.scenario() == Scenario::Siege ? 1 : 0, b.foeName().c_str());
    s = buf;
    // The town's capture point, whose land this is, and the selection's small edges.
    {
        const CapturePoint& cap = b.capturePoint();
        std::string edges;
        for (illyria::sim::EntityId id : b.selectedAlive()) {
            edges = b.world().edgesOf(*b.world().findUnit(id)).describe();
            break;
        }
        const int home = b.world().homeSide();
        std::snprintf(buf, sizeof buf, "\"capture\":{\"on\":%s,\"p\":%d,\"att\":%d,\"def\":%d,\"taken\":%s},\"land\":%d,\"edges\":\"%s\",",
                      cap.active() ? "true" : "false", cap.progress() * 100 / CapturePoint::kFull, cap.attackersIn(), cap.defendersIn(),
                      cap.taken() ? "true" : "false", home, edges.c_str());
        s += buf;
    }
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
        // The order mode, and a refused order's reason while fresh (3 s).
        const bool fresh = !b.notice().empty() && b.world().tick() - b.noticeTick() < 3 * illyria::sim::kTicksPerSecond;
        bool batter = false;
        for (illyria::sim::EntityId id : sel) batter = batter || illyria::sim::World::canBatter(units[id - 1].stats);
        std::snprintf(buf, sizeof buf, "\"mode\":%d,\"batter\":%s,\"notice\":\"%s\",", static_cast<int>(b.orderMode()),
                      batter ? "true" : "false", fresh ? b.notice().c_str() : "");
        s += buf;
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

// The game modes (docs/GAME_DESIGN.md §4.6): one entry for all of them, so
// the title screen starts each the same way. Ids: "title" (the menu's
// backdrop: the last battlefield, still), "skirmish" ({"scenario":0} Ford,
// 1 Siege), "campaign" ({"hard":1} new, {"hard":0} Easy lords, or
// {"save":"ILC1...."} to carry on a saved one), and the modes built into
// startOtherMode ("horde", "ctf"). 1 when started, 0 when the mode is not
// built in or could not start (illyria_mode_error says why).
EMSCRIPTEN_KEEPALIVE int illyria_start_mode(const char* id, const char* options) {
    if (!app) return 0;
    const std::string mode = id ? id : "", opts = options ? options : "{}";
    app->error.clear();
    if (mode == "title") {
        startTitle();
        return 1;
    }
    if (mode == "skirmish") {
        stopPlaying();
        app->title = false;
        gScenario = optionNumber(opts, "scenario", 0) == 1 ? Scenario::Siege : Scenario::Ford;
        newBattle();
        app->renderer.reset(*app->battle);
        frameCamera();
        return 1;
    }
    if (mode == "campaign") {
        const size_t at = opts.find("\"save\":\"");
        if (at != std::string::npos) {
            const size_t from = at + 8, to = opts.find('"', from);
            std::string why;
            const std::shared_ptr<const illyria::sim::Continent> continent =
                app->camp.fighting && app->camp.backdrop ? app->camp.backdrop->continentShared() : app->battle->continentShared();
            std::unique_ptr<illyria::sim::Campaign> c = illyria::sim::Campaign::load(*continent, opts.substr(from, to - from), &why);
            if (!c) {
                app->error = "This save cannot be read: " + why + ".";
                return 0;
            }
            stopPlaying();
            app->camp.continent = continent;
            startCampaign(std::move(c));
        } else {
            stopPlaying();
            startCampaign(optionNumber(opts, "hard", 1) != 0);
        }
        app->title = false;
        app->camp.speed = std::clamp(static_cast<int>(optionNumber(opts, "speed", 1)), 1, 8);
        return 1;
    }
    if (startOtherMode(mode, opts)) {
        app->title = false;
        return 1;
    }
    if (app->error.empty()) app->error = "Not built yet.";
    return 0;
}
EMSCRIPTEN_KEEPALIVE const char* illyria_mode_error() { return app ? app->error.c_str() : ""; }
// Whether a mode is built into this version (the title screen asks, to mark the rest "coming soon").
EMSCRIPTEN_KEEPALIVE int illyria_mode_available(const char* id) {
    const std::string mode = id ? id : "";
    return mode == "title" || mode == "skirmish" || mode == "campaign" || otherModeAvailable(mode) ? 1 : 0;
}

// The campaign as text, to keep in a save slot ("" when none is being played).
EMSCRIPTEN_KEEPALIVE const char* illyria_campaign_save() {
    if (!app || !app->camp.c || !(app->camp.on || app->camp.fighting)) return "";
    app->saved = app->camp.c->save();
    return app->saved.c_str();
}
EMSCRIPTEN_KEEPALIVE void illyria_campaign_set_speed(int speed) {
    if (app) app->camp.speed = std::clamp(speed, 1, 8);
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
