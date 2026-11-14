// The game: its loop and the C functions the page's JavaScript (or the native
// shell) calls for input and HUD state. The machine it runs on is behind
// platform.h.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../render/renderer.h"
#include "platform.h"
#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/game/factions.h"
#include "illyria/game/flag_poles.h"
#include "illyria/game/minimap.h"
#include "illyria/game/gestures.h"
#include "illyria/game/mode.h"
#include "illyria/game/mode_horde.h"
#include "illyria/game/mode_sandbox.h"
#include "illyria/game/underwater.h"
#include "illyria/sim/campaign.h"

#ifndef ILLYRIA_VERSION
#define ILLYRIA_VERSION "0.0.0-dev"
#endif

using namespace illyria::game;
namespace platform = illyria::platform;

namespace illyria::game {
void fetchPrerenderedSheets(Renderer& renderer);  // sheets.cpp
void fetchModels(Renderer& renderer);  // models.cpp
}

namespace {

void play_voice_cue(const char* set, const char* situation, int enemy, float pan, float gain, int priority, int speaker, int tick) {
    platform::voiceCue(set, situation, enemy != 0, pan, gain, priority, speaker, tick);
}
void play_sfx_cue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority, int count, int tick) {
    platform::sfxCue(id, gain, pan, dist, delay, doppler, priority, count, tick);
}
// What the game last asked of the surface soundscape, and how much of it is let through now: under water the wind, the birds
// and the clamour of the surface are muffled further (underwater.h surfaceScale), sent again as that changes.
float ambienceAsked[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
float ambienceScale = 1, ambienceScaleSent = 1;
void sendAmbience() {
    const float* a = ambienceAsked;
    const float k = ambienceScale;  // (levels only: the pans, a[3], a[5], a[7], stay)
    platform::sfxAmbience(a[0] * k, a[1] * k, a[2] * k, a[3], a[4] * k, a[5], a[6] * k, a[7], a[8] * k);
    ambienceScaleSent = k;
}
void set_sfx_ambience(float wm, float wv, float sw, float swp, float fl, float flp, float dr, float drp, float rain) {
    const float v[9] = {wm, wv, sw, swp, fl, flp, dr, drp, rain};
    std::copy(v, v + 9, ambienceAsked);
    sendAmbience();
}

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
// Which faction each seat plays outside a conquest: the colours of a skirmish (factions.h).
using illyria::game::kSkirmishSeats;
constexpr double kMaxPixelRatio = 3.0;
constexpr float kNorthUp = 1.5707963f;  // camera yaw looking north
// The whole continent in view (2.8 x 3.6 km), and a campaign stack with the
// towns round it.
constexpr float kOverviewDistance = 4233.0f;
constexpr float kStackView = 583.0f;

struct App {
    std::unique_ptr<Battle> battle;
    Camera camera;
    std::unique_ptr<Gestures> gestures;
    Renderer renderer;
    PoleSet poles;  // the flag poles' changeovers (flag_poles.h)
    double lastMs = 0;
    float pixelRatio = 1;
    double maxPixelRatio = kMaxPixelRatio;  // lowered by the graphics settings
    // Dynamic resolution: as sharp as the device can draw at a steady rate,
    // between the settings' floor and ceiling (see adaptResolution).
    double minPixelRatio = 1.0, ratio = 1.5;
    bool dynamicResolution = true;  // off: always the settings' ceiling
    double slowSince = -1, fastSince = -1;
    double ceiling = 99, ceilingUntil = -1;  // the finest ratio worth trying again, until then (adaptResolution)
    double fps = 60;
    float rain = 0;              // weather hook (illyria_set_rain)
    uint32_t ambienceSeen = ~0u;  // the ambience last sent to the page
    bool hushed = false;          // ambience silenced on the campaign map
    // Under water (underwater.h): what the eye is in, the sound's latched state and its glided values.
    Underwater under;
    SoundLatch underLatch;
    UnderwaterSound underSound;
    double drawMs = 0;  // CPU time preparing and issuing a frame's drawing (smoothed)
    std::string hud, labels;
    bool shift = false;  // held on the desktop
    bool mouse = false;  // the pointer is a mouse: left selects, right orders (Gestures)
    bool boxPress = false;  // the press in hand began as a selection box
    float pressX = 0, pressY = 0;
    // The campaign, when playing it: the war on the continent map.
    struct CampaignView {
        std::unique_ptr<illyria::sim::Campaign> c;
        std::shared_ptr<const illyria::sim::Continent> continent;  // outlives restarts of the battle
        bool on = false, paused = false;
        int speed = 1;
        illyria::sim::Campaign::Setup setup{0, 0xFF};  // who plays, as the last campaign was started (a restart keeps it)
        int city = -1;  // the city whose panel is open
        uint32_t panelStack = 0;  // the stack the panel was last opened for (the selection's: Battle::selectedStack)
        double acc = 0;
        std::string json;
        bool hard = true;  // how the computer lords play (the player picks at the start)
        int vectorFrom = -1;  // choosing where this city sends its new armies: the next city tapped
        // Ports: where each city's pier runs out to the water (battle-local), and its heading.
        std::vector<V3> pier;
        std::vector<float> pierYaw;
        std::map<uint32_t, float> heading;  // each boat's last course, so it turns smoothly
        // What the renderer was last given: the towns' holders (tower banners) and the land's reckoning.
        std::vector<uint8_t> owners, keepsBare;
        int64_t territoryTick = -1;
        std::vector<illyria::sim::Campaign::Soldier> men;  // every man of the campaign, as it says he stands (for picking, the minimap and the tests)
        double layoutMs = 0;  // smoothed
        // The minimap: its land, drawn once (RGBA, one pixel to 22 m), and the text handed to the page.
        std::vector<uint8_t> miniLand;
        std::string mini;
    } camp;
    // A battle's minimap: its ground (drawn once per battle) and the text handed to the page or the overlay.
    std::vector<uint8_t> battleMiniLand;
    const Battle* battleMiniFor = nullptr;
    uint32_t battleMiniTick = 0;
    std::string battleMini;
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
#ifdef ILLYRIA_DEV_HOOKS
double shaderTimeSkew = 0;  // illyria_set_shader_time
#endif

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
    app->introFromDist = kOverviewDistance;
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
    app->battleMiniLand.clear();
    // The whole continent, in battle-local coordinates.
    const V3 o = app->battle->origin();
    app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
    app->camera.setHeightFn([](float x, float y) { return app->battle->heightAt(x, y); });
}

// Aims at 60: under 56 frames a second for a second (under 28 for half of one): a quarter step coarser, and that
// ratio is not tried again for half a minute; at 58 or more for four seconds: a quarter step finer (the display
// holds the rate at its own, so "fast" can only be read as "keeping up"). Phones differ by ten times in speed.
void adaptResolution(double now) {
    if (app->fps < 56) {
        app->fastSince = -1;
        if (app->slowSince < 0) app->slowSince = now;
        if (now - app->slowSince > (app->fps < 28 ? 500 : 1000) && app->ratio > app->minPixelRatio) {
            app->ceiling = app->ratio - 0.25;  // (this one was too much for now)
            app->ceilingUntil = now + 30000;
            app->ratio = std::max(app->minPixelRatio, app->ratio - 0.25);
            app->slowSince = now;
        }
    } else if (app->fps > 58) {
        app->slowSince = -1;
        if (app->fastSince < 0) app->fastSince = now;
        const double most = std::min(app->maxPixelRatio, platform::canvas().pixelRatio);
        const double cap = now < app->ceilingUntil ? std::min(most, app->ceiling) : most;
        if (now - app->fastSince > 4000 && app->ratio < cap) {
            app->ratio = std::min(cap, app->ratio + 0.25);
            app->fastSince = now;
        }
    } else {
        app->slowSince = app->fastSince = -1;
    }
}

void resizeCanvas() {
    const platform::Canvas cv = platform::canvas();
    app->pixelRatio = static_cast<float>(std::min({cv.pixelRatio, app->maxPixelRatio, std::max(app->ratio, app->minPixelRatio)}));
    if (app->title) app->pixelRatio = std::min(app->pixelRatio, 1.0f);  // behind the menu: cheap
    int w = std::max(1, static_cast<int>(cv.cssW * app->pixelRatio));
    int h = std::max(1, static_cast<int>(cv.cssH * app->pixelRatio));
    if (!platform::resizeCanvas(w, h)) {  // a window's own drawable: drawn at its own ratio
        app->pixelRatio = static_cast<float>(cv.pixelRatio);
        w = std::max(1, static_cast<int>(cv.cssW * cv.pixelRatio));
        h = std::max(1, static_cast<int>(cv.cssH * cv.pixelRatio));
    }
    app->camera.setViewport(static_cast<float>(w), static_cast<float>(h));
    app->gestures->setPixelScale(app->pixelRatio);
    // Sprite sheets should look sharp at up to two device pixels a CSS pixel (closeup.h).
    app->renderer.setCloseupScreen(static_cast<float>(cv.cssH * std::min(cv.pixelRatio, 2.0)));
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

// The share of a tick the clock has run since the last, in the stacks' hundredths
// of a tick: between ticks everything glides on by it, so nothing steps.
double mapAhead() {
    using C = illyria::sim::Campaign;
    const App::CampaignView& cv = app->camp;
    return cv.paused ? 0.0 : std::clamp(cv.acc, 0.0, 1.0) * C::kTimeScale;
}

// Where a campaign stack stands now, in battle-local metres: the middle of its men (Campaign::bodyOf), or, afloat, its
// boat along its route.
V3 stackPosition(const illyria::sim::Campaign& c, const illyria::sim::Campaign::Stack& s) {
    const V3 o = app->battle->origin();
    int32_t bx, by;
    c.bodyOf(s, static_cast<int32_t>(mapAhead()), bx, by);
    V3 p{static_cast<float>(bx) / 1000.0f - o.x, static_cast<float>(by) / 1000.0f - o.y, 0};
    p.z = s.boat ? surfaceAt(p.x, p.y) : app->battle->heightAt(p.x, p.y);
    return p;
}

// The conquest's men for this frame: as the conquest says they stand (cv.men, for the minimap and the hot spot) and
// as the field presents them: its squads, its selection (Battle::
// presentConquest). The field's effects, sounds, voices and gates are its own presentation of the conquest's world; here
// it is only told how far between ticks the clock is, and moves on.
void presentConquest(float dt) {
    App::CampaignView& cv = app->camp;
    const double started = platform::nowMs();
    cv.c->visibleSoldiers(cv.c->localSide(), static_cast<int32_t>(mapAhead()), cv.men);
    app->battle->presentConquest(cv.men);
    cv.layoutMs = cv.layoutMs * 0.9 + (platform::nowMs() - started) * 0.1;
    app->battle->setAlpha(static_cast<float>(std::clamp(cv.acc, 0.0, 1.0)));
    app->battle->stepEffects(dt);
}

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

#ifdef ILLYRIA_DEV_HOOKS
// Test hook, for pictures of towns in colours the war has not given them yet (presentation only: the sim is
// untouched). ILLYRIA_LIST_CITIES=1 prints every town (number, name, holder, place in battle metres);
// ILLYRIA_DEMO_OWNERS="town:side,town:side" shows those towns held by those sides (255: nobody); with
// ILLYRIA_DEMO_FROM_TICK=N it starts at tick N, so a picture before and after a capture can be had in one run.
void devOwners(std::vector<uint8_t>& owners, const illyria::sim::Campaign& c) {
    static bool listed = false;
    if (!listed && std::getenv("ILLYRIA_LIST_CITIES")) {
        listed = true;
        const V3 o = app->battle->origin();
        for (size_t i = 0; i < owners.size(); ++i)
            std::fprintf(stderr, "CITY %zu %s holder=%d at %.1f,%.1f\n", i, c.continent().cities()[i].name.c_str(), static_cast<int>(owners[i]),
                         c.continent().cities()[i].pos.x.toDouble() - o.x, c.continent().cities()[i].pos.y.toDouble() - o.y);
    }
    const char* from = std::getenv("ILLYRIA_DEMO_FROM_TICK");
    if (from && c.tick() < std::atoll(from)) return;
    const char* spec = std::getenv("ILLYRIA_DEMO_OWNERS");
    int town, side, used = 0;
    while (spec && std::sscanf(spec, "%d:%d%n", &town, &side, &used) == 2) {
        if (town >= 0 && static_cast<size_t>(town) < owners.size()) owners[static_cast<size_t>(town)] = static_cast<uint8_t>(side);
        spec += used;
        if (*spec == ',') ++spec;
    }
}
#endif

// The flags on poles, every frame: on the campaign map the banners rising
// over towns being taken (the old colours coming down as the new go up) and
// the outposts' flags, with the towns' tower banners and the land in their
// holders' colours; in a siege, the capture point's pole.
void placeFlags(float dt) {
    using C = illyria::sim::Campaign;
    std::vector<Renderer::Flag> flags;
    // One flag to a pole: the old holder's comes down, then the new one's goes up (flag_poles.h). Presentation
    // only, on the frame's real time; the sim's state is untouched.
    enum : uint64_t { kModePole = 1ull << 32, kCityPole = 2ull << 32, kOutpostPole = 3ull << 32, kSiegePole = 4ull << 32 };
    const auto put = [&](uint64_t key, Renderer::Flag f) {
        const PoleShown s = app->poles.advance(key, dt, {f.colour, f.raised});
        f.colour = s.colour;
        f.raised = s.raised;
        if (s.visible()) flags.push_back(f);
    };
    const V3 o = app->battle->origin();
    if (app->camp.on) {
        App::CampaignView& cv = app->camp;
        const C& c = *cv.c;
        std::vector<uint8_t> owners = knownOwners(c, c.localSide());  // (as the viewer knows them)
#ifdef ILLYRIA_DEV_HOOKS
        devOwners(owners, c);
#endif
        // A town whose banner is going up has its tall pole on the keep: the keep's own small banner stands
        // aside meanwhile, so the pole carries one flag (the moment it is done, the keep's flies the new colours).
        std::vector<uint8_t> flying(c.cities().size(), 0);
        for (size_t i = 0; i < flying.size(); ++i)
            flying[i] = c.cities()[i].banner != C::kNeutral && c.seesCity(c.localSide(), static_cast<int32_t>(i));
        if (owners != cv.owners || flying != cv.keepsBare) {
            cv.owners = owners;
            cv.keepsBare = flying;
            app->renderer.setCityColours(townLooks(owners), flying);
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
        const float s = std::clamp(app->camera.distance() / 867.0f, 0.4f, 3.0f);
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const C::CityState& cs = c.cities()[i];
            if (!flying[i]) continue;  // (no banner going up, or over a town the viewer cannot see)
            const illyria::sim::Continent::Castle& k = c.continent().cities()[i].castle;
            V3 base = toV3(k.keep) - V3{o.x, o.y, 0};
            base.z = app->battle->heightAt(base.x, base.y) + static_cast<float>(k.keepHeight.toDouble());
            const float p = static_cast<float>(cs.bannerProgress) / C::kBannerFull;
            const PoleTarget t = contestedPole(bannerColor(c.knownOwner(c.localSide(), static_cast<int32_t>(i))), bannerColor(cs.banner), p);
            put(kCityPole + i, {base, 70 * s, t.level, 26 * s, t.colour});
        }
        for (const C::Outpost& op : c.outposts()) {
            V3 base{C::centre(op.cx) - o.x, C::centre(op.cy) - o.y, 0};
            // The flag stands on the dry bank at the bridge's head, beside the road, not mid-river on the cell's middle (the
            // campaign's own position of the outpost is the cell's).
            const illyria::sim::Continent::Crossing* span = nullptr;
            float nearest = 1e9f;
            for (const illyria::sim::Continent::Crossing& k : app->battle->continent().crossings()) {
                if (k.name != op.name) continue;
                const float d = std::hypot(static_cast<float>(k.pos.x.toDouble()) - (base.x + o.x), static_cast<float>(k.pos.y.toDouble()) - (base.y + o.y));
                if (d < nearest) nearest = d, span = &k;
            }
            if (span && span->bridged()) {
                const illyria::sim::Vec3 head = span->bridgehead();
                base.x = static_cast<float>(head.x.toDouble()) - o.x;
                base.y = static_cast<float>(head.y.toDouble()) - o.y;
            } else if (span) {
                // A ford: the nearest dry ground on a ring round it (not mid-stream).
                const illyria::sim::Continent& cont = app->battle->continent();
                bool placed = false;
                for (int radius = 6; radius <= 90 && !placed; radius += 6) {
                    for (int k = 0; k < 16 && !placed; ++k) {
                        const float a = 6.2831853f * static_cast<float>(k) / 16.0f;
                        const illyria::sim::Vec3 q = span->pos + illyria::sim::Vec3{illyria::sim::Fixed::fromRatio(static_cast<int64_t>(std::cos(a) * radius * 100), 100),
                                                                                       illyria::sim::Fixed::fromRatio(static_cast<int64_t>(std::sin(a) * radius * 100), 100), illyria::sim::Fixed::zero()};
                        const illyria::sim::Ground g = cont.groundAt(q.x, q.y);
                        if (g == illyria::sim::Ground::Plains || g == illyria::sim::Ground::Road || g == illyria::sim::Ground::Forest || g == illyria::sim::Ground::Sand) {
                            base.x = static_cast<float>(q.x.toDouble()) - o.x;
                            base.y = static_cast<float>(q.y.toDouble()) - o.y;
                            placed = true;
                        }
                    }
                }
            }
            base.z = app->battle->heightAt(base.x, base.y);
            const C::Hex& h = c.hexes()[static_cast<size_t>(op.hex)];
            put(kOutpostPole + static_cast<uint64_t>(&op - c.outposts().data()), {base, 40 * s, h.owner < C::kNeutral ? 1.0f : 0.15f, 16 * s, bannerColor(h.owner)});
        }
    } else {
        app->renderer.setTerritory({}, 0, 0, 0);
        // A match's flags: Horde's hill, capture the flag's (mode.h).
        if (const BattleMode* mode = app->battle->mode()) {
            const std::vector<ModeFlag> poles = mode->flags(*app->battle);
            for (size_t k = 0; k < poles.size(); ++k) put(kModePole + k, {poles[k].base, poles[k].height, poles[k].raised, poles[k].size, poles[k].colour});
        }
        // ... and the stakes round its field's edge.
        for (const ModeFlag& f : zoneStakes(*app->battle)) flags.push_back({f.base, f.height, f.raised, f.size, f.colour});
        const CapturePoint& cap = app->battle->capturePoint();
        if (cap.active()) {
            V3 base = toV3(cap.at());
            base.z = app->battle->heightAt(base.x, base.y);
            const float p = static_cast<float>(cap.progress()) / CapturePoint::kFull;
            // the battle's colours (factions.h: the defenders are the second seat, the player the first)
            const PoleTarget t = contestedPole(illyria::game::factionColor(1), illyria::game::factionColor(0), p);
            put(kSiegePole, {base, 14, t.level, 3.4f, t.colour});
        }
    }
    app->poles.endFrame();
    app->renderer.setFlags(flags);
}

// What the player asks of the conquest is an Order, stamped and applied inside the next step (Campaign::submit); the
// page never calls the campaign's own mutators. It takes effect with the next step (a paused clock holds it).
void submitOrder(illyria::sim::Campaign::Order::Kind kind, uint32_t stack = 0, int32_t a = 0, int32_t b = 0) {
    illyria::sim::Campaign::Order o;
    o.kind = kind;
    o.side = app->camp.c->localSide();
    o.stack = stack;
    o.a = a;
    o.b = b;
    app->camp.c->submit(o);
}

// The town screens are the conquest's own (what a town trains, its boats, where its new armies go), and a tap that
// belongs to them is theirs: the tap that says which town new armies go to, and a tap on one of the player's towns
// with nothing selected, which opens its panel. Every other tap is the battle's (Battle::tap).
bool townTap(float px, float py) {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
    const uint8_t me = c.localSide();
    if (cv.vectorFrom < 0 && app->battle->selectionSize() > 0) return false;
    V3 hit;
    if (!app->camera.pickGround(px, py, hit)) return cv.vectorFrom >= 0 && (cv.vectorFrom = -1, true);
    const V3 o = app->battle->origin();
    const int32_t tx = std::clamp(C::cellOf(static_cast<int32_t>(hit.x + o.x)), 0, c.width() - 1);
    const int32_t ty = std::clamp(C::cellOf(static_cast<int32_t>(hit.y + o.y)), 0, c.height() - 1);
    // A town of the player's near the tap (a cell about it).
    int32_t town = -1;
    for (int dy = -1; dy <= 1 && town < 0; ++dy)
        for (int dx = -1; dx <= 1 && town < 0; ++dx) {
            const int32_t k = c.cityAt(std::clamp(tx + dx, 0, c.width() - 1), std::clamp(ty + dy, 0, c.height() - 1));
            if (k >= 0 && c.cities()[static_cast<size_t>(k)].owner == me) town = k;
        }
    if (cv.vectorFrom >= 0) {
        // Sending a city's new armies: the tapped city of ours receives them.
        const int from = cv.vectorFrom;
        cv.vectorFrom = -1;
        if (town >= 0) submitOrder(illyria::sim::Campaign::Order::Kind::VectorProduction, 0, from, town);
        return true;
    }
    // Nothing of ours under the tap, but a town: its panel. (A man or a gate under it is the battle's to select or look at.)
    const Battle::Pick pick = app->battle->pickAt(app->camera, px, py, app->pixelRatio);
    if (town < 0 || (pick.kind != Battle::Pick::Kind::Ground && pick.kind != Battle::Pick::Kind::Wall && pick.kind != Battle::Pick::Kind::Obstacle)) return false;
    cv.city = town;
    return true;
}

// A click or tap on the battlefield: the town's, or a battle tap. A mouse's left click only selects (right clicks order).
void clickAt(float x, float y) {
    if (::app->camp.on && townTap(x, y)) return;
    if (::app->mouse) ::app->battle->selectTap(::app->camera, x, y, ::app->pixelRatio, ::app->shift);
    else ::app->battle->tap(::app->camera, x, y, ::app->pixelRatio, ::app->shift);
}

// The town's panel opens only when the player taps the town (townTap above, or its marker on the map: illyria_campaign_open_city),
// never by itself, and shuts when the selection comes to be of another stack.
void syncTownPanel() {
    App::CampaignView& cv = app->camp;
    const uint32_t stack = app->battle->selectedStack();
    if (stack == cv.panelStack) return;
    cv.panelStack = stack;
    cv.city = -1;
}

// A new campaign, or one read back from a save.
void startCampaign(std::unique_ptr<illyria::sim::Campaign> campaign) {
    using C = illyria::sim::Campaign;
    App::CampaignView& cv = app->camp;
    cv.c = std::move(campaign);
    for (uint8_t side = 0; side < illyria::sim::Continent::kFactionCount; ++side) illyria::game::sideFactions()[side] = cv.c->factionOf(side);
    illyria::game::palette::bind(cv.c->factionOf(cv.c->localSide()));  // (the player's own colour, before anything is drawn in it)
    cv.c->prepareWorld();  // the island's ground for the fights, a few seconds, once (the page has said so)
    // The field is made on the island's own origin and presents the campaign's world as it presents any battle's.
    {
        const illyria::sim::Vec3 ro = cv.c->presentedWorld()->region()->origin();
        const illyria::sim::Vec3 centre{ro.x + illyria::sim::Fixed::fromInt(Battle::kTerrainCells * 2), ro.y + illyria::sim::Fixed::fromInt(Battle::kTerrainCells * 2), illyria::sim::Fixed::zero()};
        app->battle = std::make_unique<Battle>(kBattleSeed, centre, cv.continent);
        const V3 o = app->battle->origin();
        app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
        app->camera.setHeightFn([](float x, float y) { return app->battle->heightAt(x, y); });
        app->renderer.reset(*app->battle);
        app->battle->borrow(cv.c.get());
        cv.c->onWorldStep([] { app->battle->beforeWorldStep(); }, [] { app->battle->afterWorldStep(); }, [] { app->battle->forgetMen(); });
    }
    cv.hard = cv.c->sides().size() > 1 && cv.c->sides()[1].difficulty == illyria::sim::Difficulty::Hard;
    cv.owners.clear();
    cv.territoryTick = -1;
    cv.vectorFrom = -1;
    cv.acc = 0;
    app->title = false;
    cv.on = true;
    cv.paused = false;
    cv.speed = 1;
    cv.city = -1;
    cv.panelStack = 0;
    app->renderer.setShowUnits(true);
    cv.heading.clear();
    placePiers();
    presentConquest(0);
    // Over your capital, high enough to see the neighbouring towns, its army selected.
    for (const C::Stack& s : cv.c->stacks()) {
        if (s.owner != cv.c->localSide()) continue;
        app->battle->selectStack(s.id);
        const V3 p = stackPosition(*cv.c, s);
        // Looking from the courtyard to the keep (the men in front of it, not hidden behind it) when it stands in a town, else north.
        float yaw = kNorthUp;
        if (const int town = cv.c->cityAt(s.cx, s.cy); town >= 0) {
            const V3 keep = toV3(cv.c->continent().cities()[static_cast<size_t>(town)].castle.keep) - V3{app->battle->origin().x, app->battle->origin().y, 0};
            yaw = std::atan2(keep.y - p.y, keep.x - p.x);
        }
        app->introStartMs = -1;
        app->camera.lookAt(p.x - std::cos(yaw) * 90, p.y - std::sin(yaw) * 90, kStackView);
        app->camera.rotate(yaw - app->camera.yaw());
        break;
    }
}

void startCampaign(bool hard, illyria::sim::Campaign::Setup setup) {
    App::CampaignView& cv = app->camp;
    cv.continent = app->battle->continentShared();
    cv.setup = setup;
    auto c = std::make_unique<illyria::sim::Campaign>(*cv.continent, kBattleSeed, setup);
    c->setHuman(0, true);
    c->setComputerDifficulty(hard ? illyria::sim::Difficulty::Hard : illyria::sim::Difficulty::Easy);
    startCampaign(std::move(c));
}

// Out of whatever is being played (a campaign, its 3D battle, a skirmish), back
// to a battlefield to show behind the title screen.
void stopPlaying() {
    app->camera.setCircle(0, 0, 0, 0);  // a match's field no longer holds the camera
    App::CampaignView& cv = app->camp;
    for (uint8_t side = 0; side < illyria::sim::Continent::kFactionCount; ++side) illyria::game::sideFactions()[side] = kSkirmishSeats[side];
    if (cv.on && cv.c) cv.c->onWorldStep({}, {}, {});
    if (app->battle->borrowing()) {
        app->battle->borrow(nullptr);
        newBattle();
        app->renderer.reset(*app->battle);
    }
    if (cv.on) {
        cv.on = false;
        cv.city = -1;
        cv.panelStack = 0;
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
bool otherModeAvailable(const std::string& id) { return id == "horde" || id == "ctf" || id == "sandbox"; }

// The places a match can be fought over, read once from the continent.
const std::vector<Place>& modePlaces() {
    static std::vector<Place> places;
    if (places.empty() && app) places = battlePlaces(app->battle->continent());
    return places;
}

bool startOtherMode(const std::string& id, const std::string& options) {
    if (!otherModeAvailable(id)) return false;
    // Without a place (the title's button pressed before the setup page): the
    // first recommended one (a hill for Horde).
    std::string opts = options;
    if (opts.find("\"place\"") == std::string::npos) {
        int place = 0;
        for (size_t i = 0; i < modePlaces().size(); ++i)
            if (modePlaces()[i].recommended && (id != "horde" || modePlaces()[i].kind == Place::Kind::Hill)) {
                place = static_cast<int>(i);
                break;
            }
        const size_t brace = opts.find('{');
        const std::string rest = brace == std::string::npos ? "}" : opts.substr(brace + 1);
        opts = "{\"place\":" + std::to_string(place) + (rest.find_first_not_of(" }") == std::string::npos ? "}" : "," + rest);
    }
    const std::shared_ptr<const illyria::sim::Continent> continent = app->battle->continentShared();
    std::unique_ptr<Battle> b = makeMatch(id, opts, modePlaces(), continent);
    if (!b) {
        app->error = "That place cannot be fought over.";
        return false;
    }
    stopPlaying();
    app->introStartMs = -1;
    app->battle = std::move(b);
    const V3 o = app->battle->origin();
    app->camera.setBounds(-o.x, -o.y, illyria::sim::Continent::kWidth - o.x, illyria::sim::Continent::kHeight - o.y);
    // A match's field: the camera stays over it, and no further out than it needs to see all of it.
    if (const float r = app->battle->zoneRadius(); r > 0) {
        const V3 z = app->battle->zoneCentre();
        app->camera.setCircle(z.x, z.y, r + 30, std::max(300.0f, r * 2.6f));
    }
    app->camera.setHeightFn([](float x, float y) { return app->battle->heightAt(x, y); });
    app->renderer.reset(*app->battle);
    app->renderer.setShowUnits(true);
    // Over the local player's army.
    V3 c{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
    for (const Squad& q : app->battle->squads())
        if (app->battle->controllable(q)) {
            c = app->battle->squadCentre(q);
            break;
        }
    app->camera.lookAt(c.x, c.y, id == "sandbox" ? 60 : 120);  // the sandbox: near enough to watch them move
    platform::modeStarted(id.c_str());
    return true;
}

// A number in a flat JSON object of options, or the fallback.
double optionNumber(const std::string& json, const char* key, double fallback) {
    const std::string k = std::string("\"") + key + "\":";
    const size_t at = json.find(k);
    if (at == std::string::npos) return fallback;
    return std::atof(json.c_str() + at + k.size());
}

// The soldiers are heard from the view centre, the camera's right to the right: before the world steps (the sounds
// are made in the steps), for a skirmish and a conquest alike.
void listenFromCamera() {
    const V3 eye = app->camera.eye(), at = app->camera.target();
    const V3 fwd = V3{at.x - eye.x, at.y - eye.y, 0}.normalized();
    app->battle->voice().setListener(at, V3{fwd.y, -fwd.x, 0}, app->camera.distance());
    app->battle->sfx().setListener(at, V3{fwd.y, -fwd.x, 0}, app->camera.distance());
}

// What the battle made during the steps: its shouts and its sounds, to the page.
void playCues() {
    for (const VoiceCue& c : app->battle->voice().take())
        play_voice_cue(voiceSetName(c.set), situationName(c.situation), c.enemy, c.pan, c.gain, c.priority, static_cast<int>(c.speaker), static_cast<int>(c.tick));
    for (const SfxCue& c : app->battle->sfx().take())
        play_sfx_cue(soundName(c.sound), c.gain, c.pan, c.distance, c.delay, c.doppler, c.priority, c.count, static_cast<int>(c.tick));
}

// The eye in the water, once a frame, for the picture and the sound alike (underwater.h). The camera is not the simulation's:
// nothing here is read by it.
void updateUnderwater(float dt) {
    Underwater u;
    if (app->battle && !app->title) {
        const V3 o = app->battle->origin();
        const illyria::sim::Continent& c = app->battle->continent();
        u = underwaterAt(app->camera.eye(), [&](float x, float y) { return continentWater(c, o.x, o.y, x, y); });
    }
    app->under = u;
    app->renderer.setUnderwater(u.blend, u.depth, u.murk, u.surface);
    const bool was = app->underLatch.in(), now = app->underLatch.update(u.below);
    app->underSound = glideUnderwaterSound(app->underSound, now, u.depth, dt);
    const UnderwaterSound e = effectiveUnderwaterSound(app->underSound);
    platform::sfxUnderwater(e.cutoffHz, e.gain, e.wobble, e.ambience);
    if (now != was) platform::sfxUnderwaterCue(now);
    ambienceScale = e.surfaceScale;
    if (std::abs(ambienceScale - ambienceScaleSent) > 0.02f || (ambienceScale == 1 && ambienceScaleSent != 1)) sendAmbience();
}

// False when this frame drew nothing (the title screen draws every other).
bool frame() {
    const double now = platform::nowMs();
    const double dtMs = app->lastMs > 0 ? now - app->lastMs : 16.7;
    app->lastMs = now;
    app->fps = app->fps * 0.95 + (1000.0 / std::max(dtMs, 1.0)) * 0.05;
    adaptResolution(now);
    ++app->frameCount;
    if (app->title) {
        // Behind the menu: half the frames, and no sharper than a pixel a point.
        stepTitle(dtMs);
        if (app->frameCount % 2) return false;
    }
    resizeCanvas();
    stepIntro(now);
    app->gestures->update(now);
    if (app->camp.on) {
        // The battle's thirty ticks a second, times the speed (8x: 240 a second).
        App::CampaignView& cv = app->camp;
        listenFromCamera();
        app->battle->flushOrders();  // (what was ordered since the last frame: the clock, paused or not, applies it with its next step)
        if (!cv.paused && cv.c->winner() == 0) {
            cv.acc += dtMs / 1000.0 * illyria::sim::Campaign::kTicksPerSecond * cv.speed;
            // As many ticks as the clock owes, within a share of the frame: a big fight on may not keep up, and
            // then the game runs slower than the clock (docs/ONE_WORLD.md, the fast-forward limit, step 4).
            const double began = platform::nowMs();
            for (int k = 0; k < 40 && cv.acc >= 1 && platform::nowMs() - began < 14.0; ++k, cv.acc -= 1) cv.c->step();
            cv.acc = std::min(cv.acc, 1.0);
        }
        placeBoats();
        presentConquest(static_cast<float>(dtMs / 1000.0));
        syncTownPanel();
        playCues();
        // No battle sounds over the campaign map.
        if (!app->hushed) {
            set_sfx_ambience(0, 0, 0, 0, 0, 0, 0, 0, 0);
            app->hushed = true;
            app->ambienceSeen = ~0u;
        }
    } else if (!app->title) {
        listenFromCamera();
        app->battle->sfx().setRain(app->rain);
        app->battle->advance(dtMs / 1000.0);
        playCues();
        app->hushed = false;
        playSounds(app->battle->sfx(), app->ambienceSeen);
    }
    placeFlags(static_cast<float>(dtMs / 1000.0));
    updateUnderwater(static_cast<float>(dtMs / 1000.0));
    const double drawStart = platform::nowMs();
    app->renderer.setScreenFramebuffer(platform::screenFramebuffer());
#ifdef ILLYRIA_DEV_HOOKS
    app->renderer.draw(*app->battle, app->camera, now / 1000.0 + shaderTimeSkew);
#else
    app->renderer.draw(*app->battle, app->camera, now / 1000.0);
#endif
    app->drawMs = app->drawMs * 0.9 + (platform::nowMs() - drawStart) * 0.1;
    return true;
}

}  // namespace

extern "C" {

ILLYRIA_API const char* illyria_version() { return ILLYRIA_VERSION; }

// The frame rate and where a frame's time goes, for the title panel (battle and campaign alike).
ILLYRIA_API const char* illyria_perf() {
    static char buf[96];
    if (!app) return "";
    std::snprintf(buf, sizeof buf, "%.0f fps \u00b7 sim %.1f \u00b7 draw %.1f ms \u00b7 %.2gx", app->fps, app->battle ? app->battle->stepMs() : 0.0,
                  app->drawMs, app->ratio);
    return buf;
}

// The last frame's drawing, counted per kind ("name draws triangles" lines; Renderer::Part), for the desktop's --stats.
ILLYRIA_API const char* illyria_draw_counts() {
    static std::string out;
    out.clear();
    if (!app) return "";
    for (int i = 0; i < static_cast<int>(Renderer::Part::Count); ++i) {
        const auto part = static_cast<Renderer::Part>(i);
        out += std::string(Renderer::partName(part)) + " " + std::to_string(app->renderer.partCount(part).draws) + " " +
               std::to_string(app->renderer.partCount(part).triangles) + "\n";
    }
    return out.c_str();
}
// How trees are drawn (Renderer::TreeStyle): 0 one card, 1 two crossed, 2 three, 3 four.
ILLYRIA_API void illyria_set_tree_style(int style) {
    if (app && style >= 0 && style < static_cast<int>(Renderer::TreeStyle::Count)) app->renderer.setTreeStyle(static_cast<Renderer::TreeStyle>(style));
}
// Leaves out the drawing of the kinds in the mask (a bit for each Renderer::Part), to time what each costs.
ILLYRIA_API void illyria_hide_parts(unsigned mask) {
    if (app) app->renderer.hideParts(mask);
}

// Test hook: the last `n` voice cues the battle made (JSON), with how many
// were made and held back in all.
ILLYRIA_API const char* illyria_voice_log(int n) {
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
ILLYRIA_API const char* illyria_sfx_log(int n) {
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
ILLYRIA_API void illyria_set_rain(float rain) {
    if (app) app->rain = std::clamp(rain, 0.0f, 1.0f);
}

// type: 0 down, 1 move, 2 up, 3 cancel. Coordinates in CSS pixels.
ILLYRIA_API void illyria_pointer(int type, int id, float x, float y, int button, double timeMs) {
    if (!app) return;
    const float s = app->pixelRatio;
    if (type == 0) app->introStartMs = -1;  // any touch takes the camera
    app->gestures->pointer(static_cast<PointerEvent>(type), id, x * s, y * s, button, timeMs);
}

ILLYRIA_API void illyria_wheel(float deltaY, float x, float y) {
    if (app) app->introStartMs = -1;
    if (app) app->gestures->wheel(deltaY, x * app->pixelRatio, y * app->pixelRatio);
}
ILLYRIA_API void illyria_pinch_wheel(float deltaY, float x, float y) {
    if (app) app->introStartMs = -1;
    if (app) app->gestures->pinchWheel(deltaY, x * app->pixelRatio, y * app->pixelRatio);
}
// A trackpad (the page tells it from a mouse wheel): a two-finger swipe of
// (dx, dy) CSS pixels pans; a pinch zooms by `factor` about (x, y) and a
// twist turns by `radians`.
ILLYRIA_API void illyria_scroll(float dx, float dy, float x, float y) {
    if (!app) return;
    const float s = app->pixelRatio;
    app->introStartMs = -1;
    app->gestures->scroll(dx * s, dy * s, x * s, y * s);
}
ILLYRIA_API void illyria_pinch(float factor, float radians, float x, float y) {
    if (!app) return;
    app->introStartMs = -1;
    app->gestures->pinch(factor, radians, x * app->pixelRatio, y * app->pixelRatio);
}
// The camera keys held (desktop), each -1, 0 or 1: W A S D (the arrows)
// pan right and forward, Q E (shift + left and right) turn (1: to the left),
// R F zoom (1: in).
ILLYRIA_API void illyria_camera_keys(int right, int forward, int turn, int zoom) {
    if (!app) return;
    if (right || forward || turn || zoom) app->introStartMs = -1;
    app->gestures->setKeys(right, forward, turn, zoom);
}
// Z (or N), or the compass: the view turns north up.
ILLYRIA_API void illyria_north_up() {
    if (!app) return;
    app->introStartMs = -1;
    app->gestures->turnTo(kNorthUp);
}
// The way the view looks, in radians anticlockwise from north (for the
// compass: north is that much clockwise of straight up the screen).
ILLYRIA_API float illyria_heading() {
    return app ? std::remainder(app->camera.yaw() - kNorthUp, 6.2831853f) : 0.0f;
}
// Test hook: where the camera looks, as JSON.
// Test hook: the eye's place in the water, as JSON (metres below the surface; negative above or on dry land), and the sound's amount.
ILLYRIA_API const char* illyria_underwater_state() {
    static char buf[160];
    if (!app) return "{}";
    const V3 e = app->camera.eye();
    std::snprintf(buf, sizeof buf, "{\"eyeZ\":%.2f,\"below\":%.2f,\"blend\":%.2f,\"murk\":%.2f,\"sound\":%.2f}", e.z, app->under.below > -1e8f ? app->under.below : -999.0f,
                  app->under.blend, app->under.murk, app->underSound.amount);
    return buf;
}
ILLYRIA_API const char* illyria_camera_state() {
    static char buf[160];
    if (!app) return "{}";
    const V3 t = app->camera.target();
    std::snprintf(buf, sizeof buf, "{\"x\":%.3f,\"y\":%.3f,\"distance\":%.3f,\"yaw\":%.5f,\"heading\":%.5f,\"turning\":%s}", t.x, t.y,
                  app->camera.distance(), app->camera.yaw(), illyria_heading(), app->gestures->turning() ? "true" : "false");
    return buf;
}

ILLYRIA_API void illyria_select_squad(int index) {
    if (app) app->battle->selectSquad(index);
}

// Commanding (game/src/battle_command.cpp). Shift held: a press starts a
// selection box at once, and a tap adds to the selection.
// The pointers that go down from now on are a mouse's (1) or a finger's or pen's (0).
ILLYRIA_API void illyria_set_mouse(int mouse) {
    if (!app) return;
    app->mouse = mouse != 0;
    app->gestures->setMouse(app->mouse);
    app->battle->setMouseScheme(app->mouse);
}
ILLYRIA_API void illyria_set_shift(int down) {
    if (!app) return;
    app->shift = down != 0;
    app->gestures->setShift(app->shift);
}
// Ctrl (or Cmd) and Alt held: the next click is an attack on whatever is
// under it (a friend too), or a force march (battle_orders.cpp).
ILLYRIA_API void illyria_set_keys(int force, int march) {
    if (app) app->battle->setKeys(force != 0, march != 0);
}
// 0 as usual, 1 attack, 2 force march, 3 lay satchels: for one order (the phone's attack button, A, M, L).
ILLYRIA_API void illyria_set_order_mode(int mode) {
    if (app && mode >= 0 && mode <= 3) app->battle->setOrderMode(static_cast<Battle::OrderMode>(mode));
}
// What a click at (x, y) CSS pixels would do, for the desktop cursor (Battle::Intent).
ILLYRIA_API int illyria_hover(float x, float y) {
    if (!app) return 0;
    const float s = app->pixelRatio;
    return static_cast<int>(app->battle->intentAt(app->camera, x * s, y * s, s));
}
ILLYRIA_API void illyria_clear_selection() {
    if (app) app->battle->clearSelection();
}
ILLYRIA_API void illyria_select_heroes() {
    if (app) app->battle->selectHeroes();
}
// Everyone of ours in view (Ctrl/⌘ + A).
ILLYRIA_API void illyria_select_on_screen() {
    if (app) app->battle->selectInRect(app->camera, 0, 0, app->camera.width(), app->camera.height());
}
// A formation for the selection, by Formation value; kKeepShapeId: as they stand.
ILLYRIA_API void illyria_set_formation(int f) {
    if (app && f >= 0 && f < kFormationCount) app->battle->setFormation(static_cast<Formation>(f));
    else if (app && f == kKeepShapeId) app->battle->keepShape();
}
ILLYRIA_API void illyria_scatter() {
    if (app) app->battle->scatter();
}
// Groups, by slot (the key 0-9): 0 recall, 1 assign the selection, 2 save
// the selection in the first free slot (returns it), 3 forget.
ILLYRIA_API int illyria_group(int op, int slot) {
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
// Ungroup: forgets every group wholly selected; how many went.
ILLYRIA_API int illyria_ungroup() {
    return app ? app->battle->ungroupSelection() : 0;
}
// The camera to the selection (only when `onlyIfOffScreen`: when its middle is out of view).
static void focusSelection(bool onlyIfOffScreen) {
    if (!app) return;
    const std::vector<illyria::sim::EntityId> alive = app->battle->selectedAlive();
    if (alive.empty()) return;
    V3 c;
    for (illyria::sim::EntityId id : alive) c += app->battle->unitPosition(id - 1);
    c = c * (1.0f / static_cast<float>(alive.size()));
    if (onlyIfOffScreen) {
        float px, py;
        if (app->camera.project(c, px, py) && px >= 0 && py >= 0 && px <= app->camera.width() && py <= app->camera.height()) return;
    }
    app->introStartMs = -1;
    app->camera.lookAt(c.x, c.y, std::min(app->camera.distance(), 160.0f));
}
ILLYRIA_API void illyria_focus_selection() {
    focusSelection(false);
}
// Tab (dir 1) and Shift+Tab (-1): the next or previous group of units (Battle::cycleGroup), and the camera to it if it is off
// screen. 1 when the selection moved.
ILLYRIA_API int illyria_cycle_group(int dir) {
    if (!app || !app->battle->cycleGroup(dir)) return 0;
    focusSelection(true);
    return 1;
}
// Every formation's shape for the picker's glyphs, in picker order, then as
// they stand (any shape: a face drawn in men): [{"id","name","slots":[[x,y],...]}].
ILLYRIA_API const char* illyria_formations() {
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
    std::snprintf(buf, sizeof buf, ",{\"id\":%d,\"name\":\"%s\",\"slots\":", kKeepShapeId, kKeepShapeName);
    out += buf;
    out += "[[-1,1.4],[1,1.4],[0,0.4],[-2,-0.2],[-1.3,-1.1],[-0.45,-1.5],[0.45,-1.5],[1.3,-1.1],[2,-0.2]]}]";
    return out.c_str();
}
// The selection box being drawn, in CSS pixels: "x0 y0 x1 y1", or "" when none.
ILLYRIA_API const char* illyria_drag_box() {
    static char buf[96];
    buf[0] = 0;
    if (!app) return buf;
    const Battle::Drag& d = app->battle->drag();
    if (d.mode != Battle::Drag::Mode::Box) return buf;
    const float s = app->pixelRatio;
    std::snprintf(buf, sizeof buf, "%.0f %.0f %.0f %.0f", d.x0 / s, d.y0 / s, d.x1 / s, d.y1 / s);
    return buf;
}
// Test hooks for screenshots: a press held and dragged but not let go. From
// the squad's centre (dx, dy) metres to the destination, then (fx, fy) metres
// on for the facing line; or a selection box in CSS pixels.
ILLYRIA_API void illyria_test_press_facing(int squad, float dx, float dy, float fx, float fy, float distance) {
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
ILLYRIA_API void illyria_test_press_box(float x0, float y0, float x1, float y1) {
    if (!app) return;
    const float s = app->pixelRatio;
    app->battle->pressBegin(app->camera, x0 * s, y0 * s, s, true);
    app->battle->pressMove(app->camera, x1 * s, y1 * s);
}
ILLYRIA_API void illyria_test_press_end() {
    if (app) app->battle->pressEnd(app->camera, app->battle->drag().x1, app->battle->drag().y1, false);
}

// Test hooks for screenshots of orders: a squad sent (dx, dy) metres from
// where it stands, or after the soldier of squad `foe` nearest it, the
// camera looking on from `distance`.
ILLYRIA_API void illyria_test_move(int squad, float dx, float dy, float distance) {
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
ILLYRIA_API void illyria_test_attack(int squad, int foe, float distance) {
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

#ifdef ILLYRIA_DEV_HOOKS
// Test hook (--paths-demo on the desktop): the player's first three squads sent three ways at once and all
// selected, to see the three path styles side by side: the first goes after the nearest foe (a hunt), the
// second marches (a patrol), the third force marches. Does not move the camera.
ILLYRIA_API void illyria_paths_demo() {
    if (!app) return;
    Battle& b = *app->battle;
    std::vector<size_t> mine;
    size_t foe = SIZE_MAX;
    for (size_t i = 0; i < b.squads().size(); ++i) {
        if (b.squads()[i].owner == Battle::kPlayer && mine.size() < 3) mine.push_back(i);
        else if (b.squads()[i].owner != Battle::kPlayer && foe == SIZE_MAX && b.aliveCount(b.squads()[i]) > 0) foe = i;
    }
    if (mine.size() < 3 || foe == SIZE_MAX) return;
    std::vector<illyria::sim::EntityId> all;
    const auto centre = [&](size_t s) { return b.squadCentre(b.squads()[s], false); };
    const auto at = [&](V3 p) { p.z = b.heightAt(p.x, p.y); return p; };
    b.selectSquad(static_cast<int>(mine[0]));
    illyria::sim::EntityId nearest = 0;
    float bestD = 1e30f;
    for (illyria::sim::EntityId id : b.squads()[foe].members) {
        const float d = (b.unitPosition(id - 1) - centre(mine[0])).length();
        if (b.world().units()[id - 1].alive() && d < bestD) bestD = d, nearest = id;
    }
    if (nearest != 0) b.orderAttackUnit(nearest);
    b.selectSquad(static_cast<int>(mine[1]));
    b.orderMove(at(centre(mine[1]) + V3{40, -14, 0}));
    b.selectSquad(static_cast<int>(mine[2]));
    b.orderForceMarch(at(centre(mine[2]) + V3{38, 30, 0}));
    for (size_t s : mine)
        for (illyria::sim::EntityId id : b.squads()[s].members) all.push_back(id);
    b.select(all);
    app->introStartMs = -1;
}

// Test hook (--garrison-demo on the desktop, in a conquest): four giants of the second side stand 60 m outside the doors of the
// player's capital, to see its idle garrison take its stations (World::keepGates).
ILLYRIA_API void illyria_garrison_demo() {
    using namespace illyria::sim;
    if (!app || !app->camp.on || !app->camp.c || !app->camp.c->presentedWorld()) return;
    Campaign& c = *app->camp.c;
    World& w = *c.presentedWorld();
    for (size_t i = 0; i < c.cities().size(); ++i) {
        if (c.cities()[i].owner != 0 || !c.continent().cities()[i].capital) continue;
        const size_t gate = w.gateOfTown(c.continent().cities()[i].pos - w.region()->origin());
        if (gate == World::kNoGate) return;
        const Gate& g = w.region()->gates()[gate];
        const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
        for (int k = 0; k < 4; ++k) {
            Vec3 p = mid + g.out * Fixed::fromInt(60) + Vec3{Fixed::fromInt(k * 3 - 4), Fixed::zero(), Fixed::zero()};
            p.z = w.groundHeight(p.x, p.y);
            const EntityId id = w.spawnUnit(1, p, -g.out, unitStats(UnitType::Giants));
            if (k == 1) std::fprintf(stderr, "garrison-demo: gate at continent %.1f %.1f, out %.3f %.3f\n", (mid.x + w.region()->origin().x).toDouble(), (mid.y + w.region()->origin().y).toDouble(), g.out.x.toDouble(), g.out.y.toDouble());
            (void)id;
        }
        return;
    }
}

// Test hook (--queue-demo on the desktop): the first squad given a march and two queued after it (Shift + right-click), the
// second given the same while planning (held: dashed, violet, numbered), the third a plain march; none selected, the camera
// above them. Right clicks are made through the camera, as the player's are.
ILLYRIA_API void illyria_queue_demo() {
    if (!app) return;
    Battle& b = *app->battle;
    std::vector<size_t> mine;
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].owner == Battle::kPlayer && mine.size() < 3) mine.push_back(i);
    if (mine.size() < 3) return;
    const V3 c0 = b.squadCentre(b.squads()[mine[0]], false);
    app->introStartMs = -1;
    app->camera.lookAt(c0.x + 10, c0.y, 110.0f);
    const auto rightClick = [&](size_t squad, float dx, float dy, bool shift) {
        const V3 c = b.squadCentre(b.squads()[squad], false);
        V3 at{c.x + dx, c.y + dy, 0};
        at.z = b.heightAt(at.x, at.y);
        float px = 0, py = 0;
        if (app->camera.project(at, px, py)) b.orderTap(app->camera, px, py, 1.0f, shift);
    };
    b.selectSquad(static_cast<int>(mine[0]));
    rightClick(mine[0], 30, -10, false);
    rightClick(mine[0], 30, 25, true);
    rightClick(mine[0], 70, 25, true);
    b.selectSquad(static_cast<int>(mine[2]));
    rightClick(mine[2], 40, 8, false);
    b.selectSquad(static_cast<int>(mine[1]));
    b.setPlanning(true);
    rightClick(mine[1], 25, 12, false);
    rightClick(mine[1], 25, 40, true);
    rightClick(mine[1], 60, 45, true);
    b.clearSelection();
}
#endif

ILLYRIA_API void illyria_focus_squad(int index) {
    if (!app || index < 0 || index >= static_cast<int>(app->battle->squads().size())) return;
    const V3 c = app->battle->squadCentre(app->battle->squads()[index]);
    app->camera.lookAt(c.x, c.y, std::min(app->camera.distance(), 160.0f));
}

// Test hook: a squad's soldiers promoted to a rank (0..3), to see their shield pips.
ILLYRIA_API void illyria_test_promote(int squad, int rank) {
    if (app && squad >= 0) app->battle->promoteSquad(static_cast<size_t>(squad), rank);
}

// Test hook: the camera close on the first squad of a unit type (0 if none in the battle).
ILLYRIA_API int illyria_test_focus_type(int type, float distance, float yaw) {
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
ILLYRIA_API int illyria_test_send_type_in(int type, float inside, int onWall) {
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
ILLYRIA_API const char* illyria_test_heights(int squad) {
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
// Test hook: for each living soldier of a squad, where he stands, the way he
// faces, and where he is bound if anywhere ("x,y fx,fy -> tx,ty;").
ILLYRIA_API const char* illyria_test_facing(int squad) {
    static std::string out;
    out.clear();
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return out.c_str();
    const Battle& b = *app->battle;
    // The view the unit shader picks (kUnitVs): d 0 front, 2 his right side, 4 back; past 4 mirrored.
    const float toViewer = std::atan2(-std::sin(app->camera.yaw()), -std::cos(app->camera.yaw()));
    for (illyria::sim::EntityId id : b.squads()[static_cast<size_t>(squad)].members) {
        const illyria::sim::Unit& u = b.world().units()[id - 1];
        if (!u.alive()) continue;
        const V3 p = b.unitPosition(id - 1);
        const float yaw = std::atan2(static_cast<float>(u.facing.y.toDouble()), static_cast<float>(u.facing.x.toDouble()));
        const int d = static_cast<int>(std::floor(-(toViewer - yaw) / 0.7853982f + 0.5f)) & 7;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%.2f,%.2f %.2f,%.2f -> %s%.1f,%.1f cam %.2f d %d;", p.x, p.y, u.facing.x.toDouble(),
                      u.facing.y.toDouble(), u.hasMoveTarget ? "" : "(none) ", u.moveTarget.x.toDouble(), u.moveTarget.y.toDouble(),
                      app->camera.yaw(), d);
        out += buf;
    }
    return out.c_str();
}
// Test hook: one soldier picked out of a squad (its first living one).
ILLYRIA_API void illyria_test_select_one(int squad) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size())) return;
    for (illyria::sim::EntityId id : app->battle->squads()[static_cast<size_t>(squad)].members)
        if (app->battle->world().units()[id - 1].alive()) return app->battle->select({id});
}
// Test hook: the camera close on a squad's first living soldier.
ILLYRIA_API void illyria_test_view_squad(int squad, float distance, float yaw) {
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
ILLYRIA_API void illyria_test_view_squad_from_town(int squad, float distance, float turn) {
    if (!app || squad < 0 || squad >= static_cast<int>(app->battle->squads().size()) || app->battle->region().ramparts().empty()) return;
    for (illyria::sim::EntityId id : app->battle->squads()[static_cast<size_t>(squad)].members) {
        if (!app->battle->world().units()[id - 1].alive()) continue;
        const V3 p = app->battle->unitPosition(id - 1);
        // The walls of the town he is in (towns stand close together).
        V3 c = toV3(app->battle->region().ramparts().front().centre);
        for (const illyria::sim::Rampart& rp : app->battle->region().ramparts()) {
            const V3 q = toV3(rp.centre);
            if ((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) < (c.x - p.x) * (c.x - p.x) + (c.y - p.y) * (c.y - p.y)) c = q;
        }
        const float yaw = std::atan2(p.y - c.y, p.x - c.x) + turn;
        app->introStartMs = -1;
        app->camera.lookAt(p.x + std::cos(yaw) * distance * 0.1f, p.y + std::sin(yaw) * distance * 0.1f, distance);
        app->camera.rotate(yaw - app->camera.yaw());
        return;
    }
}

// Test hook: every unit type in this battle and where its sprite sheet came
// from ("Name=pre", "Name=baked" or "Name=none"), comma separated.
ILLYRIA_API const char* illyria_test_battle_sheets() {
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

ILLYRIA_API void illyria_hot_spot() {
    if (!app) return;
    V3 at;
    if (app->battle->hotSpot(at)) app->camera.lookAt(at.x, at.y, std::clamp(app->camera.distance(), 30.0f, 70.0f));
}

// The O key: open or shut the gate looked at (else our nearest) and hold it so; with `release`, let it go.
ILLYRIA_API void illyria_gate(int release) {
    if (app) app->battle->swingGateByKey(release != 0);
}
// The planning mode (`): 0 toggles it (off releases what is held), 1 releases, 2 discards (Backspace). The count held after.
ILLYRIA_API int illyria_plan(int op) {
    if (!app) return 0;
    Battle& b = *app->battle;
    switch (op) {
        case 0: b.togglePlanning(); break;
        case 1: b.releasePlan(); break;
        case 2: b.discardPlan(); break;
    }
    return static_cast<int>(b.plannedCount());
}
ILLYRIA_API void illyria_stop() {
    if (app) app->battle->stopSelected();
}
// The selected hyena-folk let loose to raid from where they stand (the Raid button).
ILLYRIA_API void illyria_raid() {
    if (app) app->battle->orderRaid();
}
// The special action (T): the hyena-folk raid at once; grenadiers' next tap lays satchels.
ILLYRIA_API void illyria_special() {
    if (app) app->battle->specialAction();
}
// Debugging: draw a unit type's sprite sheet over the screen (-1 hides it).
ILLYRIA_API void illyria_show_sheet(int type) {
    if (app) app->renderer.showSheet(type);
}
ILLYRIA_API void illyria_charge() {
    if (app) app->battle->chargeAll();
}
ILLYRIA_API void illyria_cycle_formation() {
    if (app) app->battle->cycleFormation();
}
ILLYRIA_API void illyria_toggle_pause() {
    if (app) app->battle->togglePause();
}
// 1×, 2×, 4×, and from slow motion back to 1×.
ILLYRIA_API void illyria_cycle_speed() {
    if (!app) return;
    if (app->battle->slowMotion() < 1) app->battle->setSlowMotion(1);
    else app->battle->cycleSpeed();
}
// The battle's pace: 0.25 or 0.5 (slow motion), 1, 2 or 4.
ILLYRIA_API void illyria_set_pace(float pace) {
    if (!app) return;
    // Single player only. Once battles are played over the network
    // (docs/NETCODE.md), every seat must agree to a change of pace: check here.
    app->battle->setSlowMotion(std::min(pace, 1.0f));
    app->battle->setSpeed(static_cast<int>(std::max(pace, 1.0f)));
}
// Switch battle: 0 Oakhaven Ford, 1 the siege of a neutral town.
ILLYRIA_API void illyria_set_scenario(int scenario) {
    gScenario = scenario == 1 ? Scenario::Siege : Scenario::Ford;
    if (!app) return;
    app->title = false;
    newBattle();
    app->renderer.reset(*app->battle);
    frameCamera();
}
ILLYRIA_API void illyria_restart() {
    if (!app) return;
    app->title = false;
    newBattle();
    app->renderer.reset(*app->battle);
    frameCamera();
}
// Test hook: run the simulation forward instantly.
ILLYRIA_API void illyria_fast_forward(double seconds) {
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
ILLYRIA_API void illyria_test_order_gate(int squad) {
    if (!app) return;
    app->battle->selectSquad(squad);
    app->battle->orderAtGate(nearestGate());
}
// Hit points left in that gate (-1: none).
ILLYRIA_API int illyria_test_gate_hp() {
    const int g = nearestGate();
    return g < 0 ? -1 : app->battle->world().gates()[static_cast<size_t>(g)].hp;
}
// That gate battered down at once.
ILLYRIA_API void illyria_test_break_gate() {
    const int g = nearestGate();
    if (g >= 0) app->battle->breakGate(g);
}
// Camera on a defender up on the walls, living (dead = 0) or dead (1): the
// first such, seen from inside the town looking out over him (turned by
// `turn` radians); returns 1 when there is one.
ILLYRIA_API int illyria_test_view_walk(int dead, float distance, float turn) {
    if (!app) return 0;
    const illyria::sim::Region& rg = app->battle->region();
    for (const illyria::sim::Unit& u : app->battle->world().units()) {
        if (!u.present() || u.owner == illyria::game::Battle::kPlayer || u.alive() == (dead != 0) || u.airborne) continue;
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
ILLYRIA_API void illyria_test_shoot_walk() {
    if (!app) return;
    const illyria::sim::Region& rg = app->battle->region();
    const auto& units = app->battle->world().units();
    V3 from{};
    int n = 0;
    for (const illyria::sim::Unit& u : units) {
        if (!u.present() || u.owner != illyria::game::Battle::kPlayer || u.stats.type != static_cast<uint8_t>(illyria::sim::UnitType::Archers)) continue;
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
ILLYRIA_API void illyria_test_shoot_through_gate(float inside) {
    const int g = nearestGate();
    if (!app || g < 0) return;
    const illyria::sim::Gate& gate = app->battle->region().gates()[static_cast<size_t>(g)];
    const V3 at = toV3(gate.a + gate.b) * 0.5f - toV3(gate.out) * inside;
    app->battle->testShootAt(at);
}
// Camera on that gate, from outside the town.
ILLYRIA_API void illyria_test_view_gate(float distance, float yaw) {
    const int g = nearestGate();
    if (g < 0) return;
    const illyria::sim::Gate& gate = app->battle->region().gates()[static_cast<size_t>(g)];
    const V3 c = toV3(gate.a + gate.b) * 0.5f;
    app->introStartMs = -1;
    app->camera.lookAt(c.x, c.y, distance);
    app->camera.rotate(yaw - app->camera.yaw());
}
// Test hook (screenshots of the ramps): the town's militia sent up the
// ramp nearest its courtyard onto the walk, half to each side, and the two
// archers on the walk nearest its head sent the other way past them; the
// camera on the ramp from inside the town. Returns 1 when there is a ramp.
ILLYRIA_API int illyria_test_ramp(float distance, float yaw) {
    if (!app) return 0;
    const illyria::sim::Region& rg = app->battle->region();
    // The town the militia holds (the outer ring nearest them).
    V3 militia{};
    int n = 0;
    for (const illyria::game::Squad& s : app->battle->squads())
        if (s.owner != illyria::game::Battle::kPlayer && s.type == illyria::sim::UnitType::LightInfantry)
            for (illyria::sim::EntityId id : s.members) {
                militia = militia + toV3(app->battle->world().findUnit(id)->pos);
                ++n;
            }
    if (n == 0) return 0;
    militia = militia * (1.0f / static_cast<float>(n));
    const illyria::sim::Rampart* rp = nullptr;
    float best = 1e30f;
    for (const illyria::sim::Rampart& r : rg.ramparts()) {
        const V3 q = toV3(r.centre);
        const float d = (q.x - militia.x) * (q.x - militia.x) + (q.y - militia.y) * (q.y - militia.y);
        if (!r.ramps.empty() && (d < best - 1 || (d < best + 1 && rp && r.ramps.size() > rp->ramps.size()))) {
            best = std::min(best, d);
            rp = &r;
        }
    }
    if (!rp) return 0;
    const V3 foot = toV3(rp->ramps.front().first), head = toV3(rp->ramps.front().second), centre = toV3(rp->centre);
    V3 dir{head.x - foot.x, head.y - foot.y, 0};
    dir = dir * (1.0f / dir.length());
    V3 in{-dir.y, dir.x, 0};
    if (in.dot(V3{centre.x - head.x, centre.y - head.y, 0}) < 0) in = in * -1.0f;
    const float half = static_cast<float>(rp->halfThick.toDouble());
    const V3 walk = head - in * (half + 1.5f);
    const V3 c = foot + (head - foot) * 0.5f;
    int sent = 0, archers = 0;
    for (const illyria::game::Squad& s : app->battle->squads()) {
        if (s.owner == illyria::game::Battle::kPlayer) continue;
        if (s.type == illyria::sim::UnitType::LightInfantry)
            for (illyria::sim::EntityId id : s.members) {
                const float side = sent % 2 ? 1.0f : -1.0f;
                app->battle->testMove(id, walk + dir * side * (6.0f + 1.5f * static_cast<float>(sent / 2)));
                ++sent;
            }
        if (s.type == illyria::sim::UnitType::Archers)
            for (illyria::sim::EntityId id : s.members) {
                const V3 p = toV3(app->battle->world().findUnit(id)->pos);
                if (archers < 2 && (p - walk).length() < 16) {
                    app->battle->testMove(id, walk - dir * ((p - walk).dot(dir) > 0 ? 18.0f : -18.0f));
                    ++archers;
                }
            }
    }
    app->introStartMs = -1;
    app->camera.lookAt(c.x + in.x * 4, c.y + in.y * 4, distance);
    app->camera.rotate(yaw + std::atan2(-in.y, -in.x) - app->camera.yaw());
    return 1;
}
// Test hook: march a squad to a spot on a road 200-320 m off whose straight
// way lies mostly over the fields (it should make for the road), and look on
// from above the middle of the way.
ILLYRIA_API int illyria_test_march_road(int squad, float distance) {
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
ILLYRIA_API void illyria_set_camera(float x, float y, float distance, float yaw) {
    if (!app) return;
    app->introStartMs = -1;
    app->camera.lookAt(x, y, distance);
    app->camera.rotate(yaw - app->camera.yaw());
}

// Test hook (screenshots): the camera at continent metres, not the battle window's.
ILLYRIA_API void illyria_set_camera_world(float x, float y, float distance, float yaw) {
    if (!app || !app->battle) return;
    const V3 o = app->battle->origin();
    illyria_set_camera(x - o.x, y - o.y, distance, yaw);
}

// Test hook: the camera close on the `nth` body that died this way (sim::DeathKind:
// 1 fell, 2 severed, 3 gibbed, 4 pierced, 5 charred); 0 if there is none.
ILLYRIA_API int illyria_view_death(int kind, int nth, float distance) {
    if (!app || !app->battle) return 0;
    for (const illyria::sim::Unit& u : app->battle->world().units()) {
        if (!u.present() || u.alive() || static_cast<int>(u.death.kind) != kind || nth-- > 0) continue;
        const V3 p = toV3(u.pos);
        illyria_set_camera(p.x, p.y, distance, app->camera.yaw());
        return 1;
    }
    return 0;
}

#ifdef ILLYRIA_DEV_HOOKS
// Test hook: moves the renderer's clock (the water's animation) on, to take pictures of it some seconds apart.
ILLYRIA_API void illyria_set_shader_time(double seconds) { shaderTimeSkew = seconds; }
#endif

// Test hook: build the close-up land around the camera at once (screenshots).
ILLYRIA_API void illyria_finish_detail() {
    if (app) app->renderer.finishDetail(*app->battle, app->camera);
}

// Graphics settings from the page: Renderer::Feature bits and a resolution cap.
// Purely cosmetic: the simulation never sees them.
ILLYRIA_API void illyria_set_graphics(int features, double maxPixelRatio) {
    if (!app) return;
    app->renderer.setFeatures(features);
    // "Full resolution" pins the device's own; otherwise the frame rate decides, up to 2.
    app->maxPixelRatio = std::clamp(maxPixelRatio, 0.5, kMaxPixelRatio);
    app->minPixelRatio = maxPixelRatio >= kMaxPixelRatio || !app->dynamicResolution ? app->maxPixelRatio : 1.0;
}
// Dynamic resolution on or off (Settings, Graphics); takes effect with the next illyria_set_graphics.
ILLYRIA_API void illyria_set_dynamic_resolution(int on) {
    if (app) app->dynamicResolution = on != 0;
}
ILLYRIA_API int illyria_graphics_preset(int level) {
    return level <= 0 ? Renderer::kLow : level == 1 ? Renderer::kMedium : Renderer::kHigh;
}

// Test hook: look down on the whole continent.
ILLYRIA_API void illyria_overview() {
    if (!app) return;
    app->introStartMs = -1;
    app->camera.rotate(kNorthUp - app->camera.yaw());
    const V3 o = app->battle->origin();
    app->camera.lookAt(illyria::sim::Continent::kWidth * 0.5f - o.x, illyria::sim::Continent::kHeight * 0.5f - o.y, kOverviewDistance);
}

// --- The minimap (the page draws it over a canvas from what these hand over) ---

// The land from above, north at the top: sea, water, and land shaded by its
// height and lit from the north-west, dark under the woods (the cover of the trees the battlefield is drawn with,
// Renderer::farForest, so the map and the view show the same forest); roads pale. Drawn once, a pixel to 22 m.
constexpr int kMiniW = 128, kMiniH = 160;
void buildMiniLand() {
    using illyria::sim::Fixed;
    App::CampaignView& cv = app->camp;
    const illyria::sim::Continent& land = cv.c->continent();
    std::vector<float> height(static_cast<size_t>(kMiniW) * kMiniH);
    std::vector<uint8_t> kind(height.size(), 0);  // 0 land, 1 sea, 2 fresh water, 3 road
    for (int j = 0; j < kMiniH; ++j)
        for (int i = 0; i < kMiniW; ++i) {
            const int64_t x = illyria::sim::Continent::kWidth * (2 * i + 1) / (2 * kMiniW);
            const int64_t y = illyria::sim::Continent::kHeight - illyria::sim::Continent::kHeight * (2 * j + 1) / (2 * kMiniH);
            const illyria::sim::Continent::Sample smp = land.sample(Fixed::fromInt(x), Fixed::fromInt(y));
            const size_t at = static_cast<size_t>(j) * kMiniW + i;
            height[at] = static_cast<float>(smp.height.toDouble());
            if (smp.land.toDouble() < 0) kind[at] = 1;
            else if (smp.inLake || smp.inBasin || smp.riverDist.toDouble() < smp.riverHalfWidth.toDouble() + 6) kind[at] = 2;
            else if (smp.roadDist.toDouble() < 10) kind[at] = 3;
        }
    cv.miniLand.assign(height.size() * 4, 255);
    for (int j = 0; j < kMiniH; ++j)
        for (int i = 0; i < kMiniW; ++i) {
            const size_t at = static_cast<size_t>(j) * kMiniW + i;
            const auto h = [&](int di, int dj) { return height[static_cast<size_t>(std::clamp(j + dj, 0, kMiniH - 1)) * kMiniW + std::clamp(i + di, 0, kMiniW - 1)]; };
            // Lit from the north-west: higher to the south-east than to the north-west means facing the sun.
            const float lit = std::clamp(0.5f + (h(-1, -1) - h(1, 1)) * 0.03f, 0.0f, 1.0f);
            const float elev = std::clamp(height[at] / 120.0f, 0.0f, 1.0f);
            V3 c;
            if (kind[at] == 1) c = {0.10f, 0.22f, 0.34f};
            else if (kind[at] == 2) c = {0.20f, 0.40f, 0.58f};
            else {
                const V3 low{0.30f, 0.43f, 0.24f}, mid{0.48f, 0.44f, 0.28f}, high{0.74f, 0.72f, 0.68f};
                c = elev < 0.5f ? low + (mid - low) * (elev * 2) : mid + (high - mid) * ((elev - 0.5f) * 2);
                c = c * (0.62f + 0.7f * lit);
                // The cover over the pixel's 22 m: nine samples of the 8 m cover map.
                float cover = 0;
                const illyria::game::V3 origin = app->battle->origin();
                for (int k = 0; k < 9; ++k)
                    cover += app->renderer.farForest().densityAt(static_cast<float>(illyria::sim::Continent::kWidth * (2 * i + 1) / (2 * kMiniW)) - origin.x + static_cast<float>(k % 3 - 1) * 7.0f,
                                                                 static_cast<float>(illyria::sim::Continent::kHeight - illyria::sim::Continent::kHeight * (2 * j + 1) / (2 * kMiniH)) - origin.y + static_cast<float>(k / 3 - 1) * 7.0f);
                c = c + (V3{0.10f, 0.22f, 0.09f} * (0.62f + 0.5f * lit) - c) * std::clamp(cover / 9.0f * 2.0f, 0.0f, 0.85f);
                if (kind[at] == 3) c = c * 0.55f + V3{0.78f, 0.7f, 0.5f} * 0.45f;
            }
            for (int k = 0; k < 3; ++k) cv.miniLand[at * 4 + k] = static_cast<uint8_t>(std::clamp((&c.x)[k], 0.0f, 1.0f) * 255);
        }
}

// Map labels for the page to draw: one line per visible label,
// "kind|colour|x|y|town|name" in CSS pixels (town: the city's number, for a tap on its marker, else -1). Kinds: 0 city,
// 1 capital, 2 ruin, 3 temple, 4 sage, 5 realm, 6 landmark. Which appear depends on the zoom.
ILLYRIA_API const char* illyria_labels() {
    if (!app) return "";
    std::string& out = app->labels;
    out.clear();
    const Camera& cam = app->camera;
    const float d = cam.distance();
    if (d < 400) return out.c_str();
    const illyria::sim::Continent& c = app->battle->continent();
    const V3 o = app->battle->origin();
    char buf[160];
    // Every label of the frame is collected, then thinned (below): names that would print over another label, a town's
    // marker or an army's box are left off, the least important first, so the map reads at any zoom. A marker is never
    // dropped (it is the tap on its town), only its name.
    struct Entry {
        int kind, town;
        V3 colour;
        float px, py, priority;
        std::string name;
    };
    std::vector<Entry> entries;
    const auto add = [&](int kind, const V3& colour, const illyria::sim::Vec3& at, const std::string& name, int town = -1, float priority = 0) {
        V3 p = toV3(at) - V3{o.x, o.y, 0};
        p.z = app->battle->heightAt(p.x, p.y) + 10;
        float px, py;
        if (!cam.project(p, px, py)) return;
        px /= app->pixelRatio;
        py /= app->pixelRatio;
        // A town's marker stands above its men's dots, never over them (a dot reaches dotReachPx from his place; the marker is 12 px).
        if (kind <= 1) py -= app->renderer.dotReachPx() / app->pixelRatio + 8.0f;
        if (px < -80 || py < -40 || px > cam.width() / app->pixelRatio + 80 || py > cam.height() / app->pixelRatio + 40) return;
        entries.push_back({kind, town, colour, px, py, priority, name});
    };
    for (size_t ci = 0; ci < c.cities().size(); ++ci) {
        const auto& city = c.cities()[ci];
        // In the campaign, cities fly the colours of whoever holds them now, as far as the viewer knows.
        const uint8_t owner = app->camp.on ? app->camp.c->knownOwner(app->camp.c->localSide(), static_cast<int32_t>(ci)) : city.owner;
        if (!city.capital && d > 2000) {
            add(0, factionColor(owner), city.pos, "", static_cast<int>(ci));  // a marker only
            continue;
        }
        add(city.capital ? 1 : 0, factionColor(owner), city.pos, city.name, static_cast<int>(ci), (city.capital ? 100.0f : 60.0f) + static_cast<float>(city.radius.toDouble()) * 0.05f);
    }
    if (d < 2000) {
        for (size_t k = 0; k < c.sites().size(); ++k) {
            const auto& site = c.sites()[k];
            // In the campaign, a ruin whose guardian is slain lies empty.
            const bool explored = app->camp.on && site.kind == illyria::sim::Continent::Site::Kind::Ruin && app->camp.c->sites()[k].searched;
            add(2 + static_cast<int>(site.kind), explored ? V3{0.6f, 0.6f, 0.6f} : V3{1, 1, 1}, site.pos, explored ? site.name + " (explored)" : site.name, -1,
                site.kind == illyria::sim::Continent::Site::Kind::Ruin ? 20.0f : 35.0f);
        }
    }
    // The campaign's outpost towers (kind 7), in the colours of whoever holds their land.
    if (app->camp.on && d < 2000) {
        const illyria::sim::Campaign& cc = *app->camp.c;
        for (const illyria::sim::Campaign::Outpost& op : cc.outposts()) {
            const uint8_t w = cc.hexes()[static_cast<size_t>(op.hex)].owner;
            const illyria::sim::Vec3 at{illyria::sim::Fixed::fromInt(illyria::sim::Campaign::centre(op.cx)),
                                        illyria::sim::Fixed::fromInt(illyria::sim::Campaign::centre(op.cy)), {}};
            add(7, w < illyria::sim::Campaign::kNeutral ? factionColor(w) : V3{0.85f, 0.85f, 0.85f}, at, d < 1000 && !op.name.empty() ? op.name + " tower" : "", -1, 30.0f);
        }
    }
    for (const auto& l : c.labels()) {
        if (l.kind == 0 && d > 667) add(5, {1, 1, 1}, l.pos, l.name, -1, 50.0f);
        else if (l.kind != 0 && l.kind != 4 && d > 450 && d < 2667) add(6, {1, 1, 1}, l.pos, l.name, -1, 40.0f);
    }
    // Thinning. Text is about 0.6 of its height wide a letter (the page's 14 px capital, 22 px realm, 11 px the rest).
    struct Box {
        float x0, y0, x1, y1;
    };
    const auto overlaps = [](const Box& a, const Box& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; };
    std::vector<Box> taken;
    for (const Entry& e : entries)
        if (e.kind <= 1 || e.kind == 7) taken.push_back({e.px - 6, e.py + 1, e.px + 5.5f, e.py + 15});  // every marker keeps its square
    std::vector<Box> flags;
    if (app->camp.on) {  // and every army its flag (the native HUD's box, 18 x 14 about the stack's point, with its ring)
        const illyria::sim::Campaign& cc = *app->camp.c;
        for (const illyria::sim::Campaign::Stack& st : cc.stacks()) {
            if (st.armies.empty()) continue;
            V3 p = stackPosition(cc, st);
            if (!cc.canSee(cc.localSide(), st.owner, p.x + o.x, p.y + o.y)) continue;
            p.z += 30;
            float px, py;
            if (!cam.project(p, px, py)) continue;
            px /= app->pixelRatio;
            py /= app->pixelRatio;
            flags.push_back({px - 12, py - 11, px + 12, py + 9});
        }
    }
    std::vector<size_t> order(entries.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return entries[a].priority > entries[b].priority; });
    for (const size_t i : order) {
        Entry& e = entries[i];
        if (e.name.empty()) continue;
        const float size = e.kind == 1 ? 14.0f : e.kind == 5 ? 22.0f : 11.0f;
        const float w = static_cast<float>(e.name.size()) * size * (e.kind == 5 ? 0.78f : 0.62f) + 8.0f;
        const Box b{e.px + 6, e.py + 1, e.px + 6 + w, e.py + 3 + size + 2};
        bool clear = true;
        for (const Box& t : taken) {
            // (a town's own marker is the square at its left: its name is clear of it by the 6 px)
            if (overlaps(b, t)) {
                clear = false;
                break;
            }
        }
        if (e.kind > 1 && e.kind != 7)  // (a town's name outranks an army's flag; the lesser names keep clear of them)
            for (const Box& t : flags) clear = clear && !overlaps(b, t);
        if (clear) {
            taken.push_back(b);
        } else if (e.kind <= 1 || e.kind == 7) {
            e.name.clear();  // the marker stays, the name goes
        } else {
            e.kind = -1;  // not drawn
        }
    }
    for (const Entry& e : entries) {
        if (e.kind < 0) continue;
        std::snprintf(buf, sizeof buf, "%d|#%02x%02x%02x|%.0f|%.0f|%d|%s\n", e.kind, static_cast<int>(e.colour.x * 255),
                      static_cast<int>(e.colour.y * 255), static_cast<int>(e.colour.z * 255), e.px, e.py, e.town, e.name.c_str());
        out += buf;
    }
    return out.c_str();
}


// What the minimap shows: 0 nothing (the title), 1 the conquest's island, 2 a battle's field. The page and the
// desktop's overlay ask first, and again for the land when it changes.
ILLYRIA_API int illyria_minimap_kind() {
    if (!app || app->title) return 0;
    return app->camp.on ? 1 : 2;
}
constexpr int kBattleMiniSize = 128;
// The minimap's land, to the page (IllyriaMinimap.land): RGBA, rows from the north. A conquest's island, or the field.
ILLYRIA_API void illyria_minimap_land() {
    if (!app || app->title) return;
    if (app->camp.on) {
        if (app->camp.miniLand.empty()) buildMiniLand();
        platform::minimapLand(app->camp.miniLand.data(), kMiniW, kMiniH);
        return;
    }
    const Battle& b = *app->battle;
    if (app->battleMiniLand.empty() || app->battleMiniFor != &b || b.world().tick() < app->battleMiniTick) {
        app->battleMiniLand = illyria::game::battleMinimapLand(b, kBattleMiniSize, kBattleMiniSize);
        app->battleMiniFor = &b;
    }
    app->battleMiniTick = b.world().tick();
    platform::minimapLand(app->battleMiniLand.data(), kBattleMiniSize, kBattleMiniSize);
}
// The minimap's contents, a line each, in metres from the map's south-west corner (minimap.h):
//   m|width|height          the map's size
//   t|x|y|colour|capital    every town, in its holder's colour (a conquest)
//   s|x|y|kind              every ruin (0), temple (1) and sage (2) (a conquest)
//   k|colour|x|y|count      a heap of soldiers too close to tell apart: a heat blob
//   d|colour|x|y            a soldier (or two) standing apart, a point
//   f|x|y|men               men fighting there now (the page glows it)
// Only what the viewer is allowed to see (Campaign::canSee; a battle's Battle::sees).
ILLYRIA_API const char* illyria_minimap() {
    if (!app || app->title) return "";
    if (app->camp.on) {
        app->camp.mini = minimapText(*app->camp.c, app->camp.c->localSide());
        return app->camp.mini.c_str();
    }
    app->battleMini = illyria::game::battleMinimapText(*app->battle);
    return app->battleMini.c_str();
}
// The ground the camera sees, as four corners in the map's metres (top left,
// top right, bottom right, bottom left of the screen), "x,y x,y x,y x,y"; "" if the sky shows.
ILLYRIA_API const char* illyria_minimap_view() {
    static char buf[160];
    buf[0] = 0;
    if (!app || app->title) return buf;
    const V3 o = app->camp.on ? app->battle->origin() : V3{};
    const float w = app->camera.width(), h = app->camera.height();
    const float corners[4][2] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    int n = 0;
    for (const auto& c : corners) {
        V3 hit;
        // The corners that look at the sky are brought down to the horizon's ground.
        float py = c[1];
        bool ok = app->camera.pickGround(c[0], py, hit);
        for (int tries = 0; !ok && tries < 24; ++tries) ok = app->camera.pickGround(c[0], py += h * 0.04f, hit);
        if (!ok) return buf;
        n += std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), "%s%.0f,%.0f", n ? " " : "", hit.x + o.x, hit.y + o.y);
    }
    return buf;
}
// A tap on the minimap: the camera over that point, as high as it was.
ILLYRIA_API void illyria_minimap_look(float x, float y) {
    if (!app || app->title) return;
    app->introStartMs = -1;
    if (app->camp.on) {
        const V3 o = app->battle->origin();
        app->camera.lookAt(x - o.x, y - o.y, app->camera.distance());
    } else {
        const float size = Battle::kWorldSize;
        app->camera.lookAt(std::clamp(x, 0.0f, size), std::clamp(y, 0.0f, size), app->camera.distance());
    }
}
// Test hook: how many soldiers the campaign holds, how many are drawn as sprites and
// as dots, and what laying them out and drawing a frame cost (milliseconds, smoothed).
ILLYRIA_API const char* illyria_campaign_map_stats() {
    static char buf[200];
    buf[0] = 0;
    if (!app || !app->camp.on) return buf;
    std::snprintf(buf, sizeof buf, "{\"soldiers\":%zu,\"menInWorld\":%d,\"sprites\":%zu,\"dots\":%d,\"layoutMs\":%.3f,\"drawMs\":%.3f}",
                  app->camp.men.size(), app->camp.c->menInWorld(), app->renderer.mapSprites(), app->renderer.mapDots(),
                  app->camp.layoutMs, app->drawMs);
    return buf;
}

// The campaign: start it (0: Easy computer lords, 1: Hard), give orders, and read its state.
ILLYRIA_API void illyria_campaign_start(int hard) {
    if (app) startCampaign(hard != 0, app->camp.setup);
    if (app) app->title = false;
}
// Test hook: the 3D siege's capture point stormed (the title's set battle).
ILLYRIA_API void illyria_test_storm() {
    if (app && !app->camp.on) app->battle->testStorm();
}
// Heroes: search the ruin (or sage) the selected stack stands on, or be
// blessed at a temple; take or turn down a hero offering their sword.
ILLYRIA_API int illyria_campaign_search() {
    if (!app || !app->camp.on) return 0;
    const uint32_t stack = app->battle->selectedStack();
    if (!app->camp.c->canSearch(stack)) return 0;
    submitOrder(illyria::sim::Campaign::Order::Kind::Search, stack);
    return 1;
}
ILLYRIA_API int illyria_campaign_hire(int accept) {
    if (!app || !app->camp.on) return 0;
    if (!app->camp.c->offer(app->camp.c->localSide()).open) return 0;
    submitOrder(illyria::sim::Campaign::Order::Kind::Hire, 0, accept != 0);
    return 1;
}
ILLYRIA_API void illyria_campaign_produce(int city, int option) {
    if (app && app->camp.on) submitOrder(illyria::sim::Campaign::Order::Kind::Produce, 0, city, option);
}
// Where a city sends its new armies: start choosing (the next city tapped), or -1 to keep them home.
ILLYRIA_API void illyria_campaign_vector(int city, int choose) {
    if (!app || !app->camp.on) return;
    if (choose) app->camp.vectorFrom = city;
    else submitOrder(illyria::sim::Campaign::Order::Kind::VectorProduction, 0, city, -1), app->camp.vectorFrom = -1;
}
// Sends an army of the selected stack home for good (no more upkeep).
ILLYRIA_API void illyria_campaign_disband(int army) {
    if (!app || !app->camp.on) return;
    for (const Squad& q : app->battle->squads())
        if (q.army == static_cast<uint32_t>(army) && app->battle->controllable(q)) submitOrder(illyria::sim::Campaign::Order::Kind::Disband, q.stack, army);
}
// Boats: lay one down in the open city's port; board or leave one with the selected stack.
ILLYRIA_API void illyria_campaign_build_boat(int city) {
    if (app && app->camp.on) submitOrder(illyria::sim::Campaign::Order::Kind::BuildBoat, 0, city);
}
ILLYRIA_API void illyria_campaign_embark() {
    if (app && app->camp.on && app->battle->selectedStack()) submitOrder(illyria::sim::Campaign::Order::Kind::Embark, app->battle->selectedStack());
}
ILLYRIA_API void illyria_campaign_land() {
    if (app && app->camp.on && app->battle->selectedStack()) submitOrder(illyria::sim::Campaign::Order::Kind::Land, app->battle->selectedStack());
}
// A tap on a town's marker: the panel of the player's town, whatever is selected.
ILLYRIA_API void illyria_campaign_open_city(int city) {
    if (!app || !app->camp.on || city < 0 || city >= static_cast<int>(app->camp.c->cities().size())) return;
    if (app->camp.c->cities()[static_cast<size_t>(city)].owner == app->camp.c->localSide()) app->camp.city = city;
}
ILLYRIA_API void illyria_campaign_close_city() {
    if (app) app->camp.city = -1;
}
ILLYRIA_API void illyria_campaign_pause() {
    if (app) app->camp.paused = !app->camp.paused;
}
ILLYRIA_API void illyria_campaign_speed() {
    if (app) app->camp.speed = app->camp.speed >= 8 ? 1 : app->camp.speed * 2;
}
ILLYRIA_API void illyria_campaign_select(unsigned id) {
    if (!app || !app->camp.on) return;
    app->battle->selectStack(id);
    if (const illyria::sim::Campaign::Stack* s = app->camp.c->findStack(id)) {
        const V3 p = stackPosition(*app->camp.c, *s);
        app->camera.lookAt(p.x, p.y, std::min(app->camera.distance(), kStackView));
    }
}
// Everything the campaign HUD shows, as JSON: the day and the treasury, every
// banner on screen, the selected stack's armies and the open city's panel.
ILLYRIA_API const char* illyria_campaign_state() {
    using C = illyria::sim::Campaign;
    if (!app || !app->camp.on) return "{}";
    App::CampaignView& cv = app->camp;
    const C& c = *cv.c;
    std::string& s = cv.json;
    char buf[1024];
    const uint8_t me = c.localSide();
    // The stack the selection is of (Battle::selectedStack): its march, its boat, the site it stands on.
    const C::Stack* sel = c.findStack(app->battle->selectedStack());
    std::snprintf(buf, sizeof buf, "{\"day\":%u,\"gold\":%d,\"cities\":%d,\"speed\":%d,\"paused\":%s,\"winner\":%d,\"selected\":%u,\"hard\":%s,\"stacks\":[",
                  c.day() + 1, c.sides()[me].gold, c.citiesOwned(me), cv.speed, cv.paused ? "true" : "false", c.winner(), sel ? sel->id : 0,
                  cv.hard ? "true" : "false");
    s = buf;
    bool first = true;
    for (const C::Stack& st : c.stacks()) {
        if (st.armies.empty()) continue;
        V3 p = stackPosition(c, st);
        if (!c.canSee(app->camp.c->localSide(), st.owner, p.x + app->battle->origin().x, p.y + app->battle->origin().y)) continue;
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
            if (cs.banner == C::kNeutral || !c.seesCity(me, static_cast<int32_t>(i))) continue;
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
                          any ? "," : "", i, town.name.c_str(), px, py, 100 * cs.bannerProgress / C::kBannerFull, cs.banner, c.knownOwner(me, static_cast<int32_t>(i)),
                          hex(factionColor(cs.banner)).c_str(), hex(factionColor(c.knownOwner(me, static_cast<int32_t>(i)))).c_str(), c.bannerContest(static_cast<int32_t>(i), cs.banner),
                          raisers, c.sides()[cs.banner].name.c_str());
            s += buf;
            any = true;
        }
    }
    std::snprintf(buf, sizeof buf, "],\"land\":%d,\"landGold\":%d", c.hexesHeld(me), c.hexesHeld(me) / C::kHexesPerGold);
    s += buf;
    // The squads wholly selected that can be sent home (a hero cannot): the Disband button's.
    s += ",\"armies\":[";
    {
        bool any = false;
        for (const Squad& q : app->battle->squads()) {
            if (q.owner != me || q.type == illyria::sim::UnitType::Hero || !app->battle->controllable(q) || app->battle->squadSelection(q) != 2) continue;
            std::snprintf(buf, sizeof buf, "%s{\"id\":%u,\"name\":\"%s\",\"rare\":%s,\"rank\":%d}", any ? "," : "", q.army, q.name.c_str(),
                          q.rare ? "true" : "false", q.standing.rank);
            s += buf;
            any = true;
        }
    }
    s += "]";
    // The selected stack on the march: how long until it is there, in ticks (the page puts it in days and minutes).
    if (sel && !sel->path.empty()) {
        const int32_t left = c.timeLeft(*sel);
        if (left != C::kClosed) {
            std::snprintf(buf, sizeof buf, ",\"march\":{\"ticks\":%d,\"perDay\":%d,\"perSecond\":%d}", (left + C::kTimeScale - 1) / C::kTimeScale,
                          C::kTicksPerDay, C::kTicksPerSecond);
            s += buf;
        }
    }
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
    if (c.offer(me).open) {
        const C::Offer& o = c.offer(me);
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
        // A capital drills two days in one (Campaign::newDay): the panel counts its days as they pass there.
        const bool capitalPace = c.continent().cities()[static_cast<size_t>(cv.city)].capital;
        std::snprintf(buf, sizeof buf,
                      ",\"city\":{\"index\":%d,\"name\":\"%s\",\"defense\":%d,\"income\":%d,\"producing\":%d,\"progress\":%d,"
                      "\"port\":%s,\"boatDays\":%d,\"boats\":%d,\"boatCost\":%d,\"boatTime\":%d,\"boatUpkeep\":%d,"
                      "\"vectorTo\":\"%s\",\"choosing\":%s,\"options\":[",
                      cv.city, c.continent().cities()[static_cast<size_t>(cv.city)].name.c_str(), cs.defense, cs.income, cs.producing, capitalPace ? cs.progress / 2 : cs.progress,
                      cs.port ? "true" : "false", cs.boatDays, docked, C::kBoatCost, C::kBoatDays, C::kBoatUpkeep,
                      cs.vectorTo >= 0 ? c.continent().cities()[static_cast<size_t>(cs.vectorTo)].name.c_str() : "",
                      cv.vectorFrom == cv.city ? "true" : "false");
        s += buf;
        for (size_t k = 0; k < cs.options.size(); ++k) {
            const C::Option& op = cs.options[k];
            std::snprintf(buf, sizeof buf, "%s{\"type\":\"%s\",\"days\":%d,\"cost\":%d,\"upkeep\":%d,\"strength\":%d,\"levy\":%d}", k ? "," : "",
                          illyria::sim::unitTypeName(op.type), capitalPace ? (op.days + 1) / 2 : op.days, op.cost, C::upkeep(op.type), illyria::sim::warlordsStrength(op.type), op.left);
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
        std::snprintf(buf, sizeof buf, ",\"mine\":%s,\"landOwner\":\"%s\",\"banner\":%d,\"bannerP\":%d,\"bannerBy\":\"%s\",\"bannerFoes\":%d}", cs.owner == me ? "true" : "false",
                      land < C::kNeutral ? c.sides()[land].name.c_str() : "", cs.banner == C::kNeutral ? -1 : cs.banner,
                      100 * cs.bannerProgress / C::kBannerFull, cs.banner < C::kNeutral ? c.sides()[cs.banner].name.c_str() : "",
                      cs.banner < C::kNeutral ? c.bannerContest(cv.city, cs.banner) : 0);
        s += buf;
    }
    // The latest fights and the player's heroes' deeds, newest first: a town changing hands is news of the viewer's only
    // if it can see the town (a hero's deed is its own side's, seen by it always).
    s += ",\"news\":[";
    std::vector<const C::Deed*> deeds;
    for (size_t k = c.deeds().size(); k-- > 0 && deeds.size() < 4;)
        if (c.deeds()[k].side == me) deeds.push_back(&c.deeds()[k]);
    std::vector<const C::Fight*> fights;
    for (size_t k = c.fights().size(); k-- > 0 && fights.size() < 4;)
        if (c.fights()[k].city < 0 || c.seesCity(me, c.fights()[k].city)) fights.push_back(&c.fights()[k]);
    size_t fk = 0, dk = 0;
    for (size_t k = 0; k < 5; ++k) {
        const bool fightLeft = fk < fights.size(), deedLeft = dk < deeds.size();
        if (!fightLeft && !deedLeft) break;
        if (deedLeft && (!fightLeft || deeds[dk]->day >= fights[fk]->day)) {
            std::snprintf(buf, sizeof buf, "%s\"Day %u: %s\"", k ? "," : "", deeds[dk]->day + 1, deeds[dk]->text.c_str());
            s += buf;
            ++dk;
            continue;
        }
        const C::Fight& f = *fights[fk++];
        const std::string where = f.city >= 0 ? c.continent().cities()[static_cast<size_t>(f.city)].name : std::string("the field");
        const auto sideName = [&](uint8_t side) { return side == C::kNeutral ? std::string("the garrison") : c.sides()[side].name; };
        std::snprintf(buf, sizeof buf, "%s\"Day %u: %s took %s from %s\"", k ? "," : "", f.day + 1, sideName(f.attacker).c_str(), where.c_str(), sideName(f.defender).c_str());
        s += buf;
    }
    s += "]}";
    return s.c_str();
}

// --- Matches: Horde and capture the flag (mode.h), started by startOtherMode ---

// The match's HUD as JSON ("{}" when none).
ILLYRIA_API const char* illyria_mode_hud() {
    static std::string s;
    s = app && app->battle->mode() ? app->battle->mode()->hudJson(*app->battle) : "{}";
    return s.c_str();
}
// The local player's action: 0 recruit offer `option`, 1 ready, 2 recall the fallen hero.
ILLYRIA_API void illyria_mode_act(int kind, int option) {
    if (!app || !app->battle->mode()) return;
    using K = illyria::sim::MatchAction::Kind;
    app->battle->mode()->act({kind == 1 ? K::Ready : kind == 2 ? K::Recall : K::Recruit,
                              static_cast<uint8_t>(std::clamp(option, 0, 255))});
}
// The places to fight over, for the setup panel.
ILLYRIA_API const char* illyria_mode_places() {
    static std::string s;
    s = placesJson(modePlaces());
    return s.c_str();
}
// The squads on offer (Horde's reinforcements, capture the flag's armies).
ILLYRIA_API const char* illyria_mode_offers() {
    static std::string s;
    s = "[";
    char buf[160];
    const auto& offers = illyria::sim::squadOffers();
    static const char* kRoles[] = {"melee", "ranged", "bomber", "caster", "healer", "flier", "siege", "scout"};
    for (size_t i = 0; i < offers.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%s{\"i\":%zu,\"type\":\"%s\",\"role\":\"%s\",\"count\":%d,\"cost\":%d}", i ? "," : "", i,
                      illyria::sim::unitTypeName(offers[i].type), kRoles[static_cast<int>(roleOf(offers[i].type))], offers[i].count,
                      offers[i].cost);
        s += buf;
    }
    s += "]";
    return s.c_str();
}
// While choosing: the camera over a spot on the continent (metres), high
// enough to see the land around it.
static void previewAt(float x, float y) {
    app->introStartMs = -1;
    // Off the title's slow circling, the battlefield behind it held still.
    if (app->title) {
        app->title = false;
        if (!app->battle->paused()) app->battle->togglePause();
    }
    const V3 o = app->battle->origin();
    app->camera.lookAt(x - o.x, y - o.y, 2200);
}
ILLYRIA_API void illyria_mode_preview(int place) {
    if (!app || place < 0 || place >= static_cast<int>(modePlaces().size())) return;
    const V3 p = toV3(modePlaces()[static_cast<size_t>(place)].pos);
    previewAt(p.x, p.y);
}
ILLYRIA_API void illyria_mode_preview_at(int x, int y) {
    if (app) previewAt(static_cast<float>(x), static_cast<float>(y));
}
// The continent's size in metres ("width,height"), for the setup map.
ILLYRIA_API const char* illyria_continent_extent() {
    static std::string s;
    s = std::to_string(illyria::sim::Continent::kWidth) + "," + std::to_string(illyria::sim::Continent::kHeight);
    return s.c_str();
}
// The continent from above for the sandbox's setup map: `w` pixels wide and
// as tall as the land is, RGBA, north at the top. Sea and lakes, then the
// ground by kind, shaded by its slope (lit from the north-west) and paling
// toward the peaks. Rows j0 to j1 are drawn now (the page asks for a band at
// a time, top first, so it is not held up); the whole picture is returned.
ILLYRIA_API const uint8_t* illyria_continent_map(int w, int j0, int j1) {
    using illyria::sim::Continent;
    using illyria::sim::Fixed;
    static std::vector<uint8_t> px;
    static std::vector<float> north;  // the row above's heights, for the shading
    if (!app || w < 16 || w > 1024) return nullptr;
    const Continent& c = app->battle->continent();
    const int h = static_cast<int>(std::lround(static_cast<double>(w) * Continent::kHeight / Continent::kWidth));
    const double step = static_cast<double>(Continent::kWidth) / w;
    if (j0 <= 0 || px.size() != static_cast<size_t>(w) * h * 4) {
        px.assign(static_cast<size_t>(w) * h * 4, 255);
        north.assign(static_cast<size_t>(w), 0.0f);
        j0 = 0;
    }
    for (int j = j0; j < std::min(j1, h); ++j) {
        float west = 0;
        for (int i = 0; i < w; ++i) {
            const Fixed x = Fixed::fromInt(static_cast<int64_t>((i + 0.5) * step));
            const Fixed y = Fixed::fromInt(static_cast<int64_t>(Continent::kHeight - (j + 0.5) * step));
            const Continent::Sample s = c.sample(x, y);
            const float z = static_cast<float>(s.height.toDouble());
            float col[3];
            Fixed surface;
            if (c.waterSurfaceOf(s, surface)) {
                const bool sea = s.land < Fixed::zero();
                col[0] = sea ? 0.17f : 0.24f, col[1] = sea ? 0.31f : 0.44f, col[2] = sea ? 0.43f : 0.58f;
            } else {
                static const float kinds[][3] = {{0.48f, 0.60f, 0.31f}, {0.70f, 0.61f, 0.43f}, {0.25f, 0.42f, 0.21f}, {0.36f, 0.42f, 0.27f},
                                                 {0.36f, 0.56f, 0.68f}, {0.17f, 0.31f, 0.43f}, {0.55f, 0.52f, 0.47f}, {0.60f, 0.52f, 0.38f},
                                                 {0.48f, 0.42f, 0.30f}, {0.79f, 0.72f, 0.50f}};
                const int k = std::min(9, static_cast<int>(c.landGround(x, y, s.height, s.mountain, s.roadDist)));
                const float lit = std::clamp(1.0f + 0.9f * ((z - (i ? west : z)) + (z - (j ? north[static_cast<size_t>(i)] : z))) / static_cast<float>(step),
                                             0.55f, 1.35f);
                const float pale = std::clamp((z - 50.0f) / 233.0f, 0.0f, 0.6f);  // the mountains' scale of height
                for (int q = 0; q < 3; ++q) col[q] = (kinds[k][q] * (1 - pale) + 0.86f * pale) * lit;
            }
            uint8_t* p = &px[(static_cast<size_t>(j) * w + i) * 4];
            for (int q = 0; q < 3; ++q) p[q] = static_cast<uint8_t>(std::clamp(col[q] * 255.0f, 0.0f, 255.0f));
            west = z;
            north[static_cast<size_t>(i)] = z;
        }
    }
    return px.data();
}
// Test hook: Horde's muster or break ends now.
ILLYRIA_API void illyria_mode_test_skip() {
    if (!app) return;
    if (auto* h = dynamic_cast<HordeMode*>(app->battle->mode())) h->skipBreak();
}
// Test hook (the manual's pictures of a fallen hero): the local seat's first
// squad after its hero turns on him, an order like any other.
ILLYRIA_API void illyria_mode_test_strike_hero() {
    if (!app) return;
    auto* h = dynamic_cast<HordeMode*>(app->battle->mode());
    if (!h || h->heroSquad(h->localSeat()) < 0) return;
    app->battle->selectSquad(h->heroSquad(h->localSeat()) + 1);
    app->battle->orderAttackUnit(h->hero(*app->battle, h->localSeat()));
    app->battle->clearSelection();
}
// Test hook: the camera over a flag of the match (capture the flag's, or the hill's).
ILLYRIA_API void illyria_mode_test_view_flag(int index, float distance, float yaw) {
    if (!app || !app->battle->mode()) return;
    const std::vector<ModeFlag> f = app->battle->mode()->flags(*app->battle);
    if (f.empty()) return;
    const V3 p = f[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(f.size()) - 1))].base;
    app->camera.rotate(yaw - app->camera.yaw());
    app->camera.lookAt(p.x, p.y, distance);
}

// The sandbox (mode_sandbox.h, the page's sandbox.js); nothing when another mode runs.
static SandboxMode* sandbox() {
    if (!app || !app->battle->mode() || std::string(app->battle->mode()->id()) != "sandbox") return nullptr;
    return static_cast<SandboxMode*>(app->battle->mode());
}
// Every unit type's name, by type number, for the page's picker.
ILLYRIA_API const char* illyria_sandbox_types() {
    static std::string s;
    s = "[";
    for (int i = 0; i < illyria::sim::kUnitTypeCount; ++i) {
        const auto t = static_cast<illyria::sim::UnitType>(i);
        s += (i ? ",\"" : "\"") + std::string(illyria::sim::retired(t) ? "" : illyria::sim::unitTypeName(t)) + "\"";
    }
    s += "]";
    return s.c_str();
}
// A squad of `count` of `type` for `side` where the page was clicked (CSS
// pixels); 1 when it was set down (on the field, inside its edge).
ILLYRIA_API int illyria_sandbox_spawn(int type, int side, int count, float x, float y) {
    SandboxMode* m = sandbox();
    if (!m || type < 0 || type >= illyria::sim::kUnitTypeCount) return 0;
    V3 at;
    if (!app->camera.pickGround(x * app->pixelRatio, y * app->pixelRatio, at)) return 0;
    const V3 c = app->battle->zoneCentre();
    if (V3{at.x - c.x, at.y - c.y, 0}.length() > app->battle->zoneRadius() || !app->battle->inBattlefield(at.x, at.y)) return 0;
    m->spawn(*app->battle, static_cast<illyria::sim::UnitType>(type), static_cast<illyria::sim::PlayerId>(side & 3),
             std::clamp(count, 1, 60), at);
    return 1;
}
// The side commanded from this screen.
ILLYRIA_API void illyria_sandbox_side(int side) {
    if (sandbox()) app->battle->setLocalSide(static_cast<illyria::sim::PlayerId>(side & 3));
}
// Whether `side` fights on its own.
ILLYRIA_API void illyria_sandbox_fight(int side, int on) {
    if (SandboxMode* m = sandbox()) m->setFights(static_cast<illyria::sim::PlayerId>(side & 3), on != 0);
}
// Time: `share` of its pace (1 as usual), and, paused, one tick on.
ILLYRIA_API void illyria_sandbox_slow(float share) {
    if (sandbox()) app->battle->setSlowMotion(share);
}
ILLYRIA_API void illyria_sandbox_speed(int speed) {
    if (sandbox()) app->battle->setSpeed(speed);
}
ILLYRIA_API void illyria_sandbox_step() {
    if (sandbox()) app->battle->stepOnce();
}
// A unit type's fighting numbers (SandboxMode::statsJson), and tuning them (tune; "{}": the unit sheet's).
ILLYRIA_API const char* illyria_sandbox_stats(int type) {
    static std::string s;
    SandboxMode* m = sandbox();
    s = m && type >= 0 && type < illyria::sim::kUnitTypeCount ? m->statsJson(static_cast<illyria::sim::UnitType>(type)) : "{}";
    return s.c_str();
}
ILLYRIA_API void illyria_sandbox_tune(int type, const char* json) {
    SandboxMode* m = sandbox();
    if (m && type >= 0 && type < illyria::sim::kUnitTypeCount)
        m->tune(*app->battle, static_cast<illyria::sim::UnitType>(type), json ? json : "{}");
}
// The selected soldiers play clip `clip` (sprites::Clip; -1: what they are doing), once or looped.
ILLYRIA_API void illyria_sandbox_clip(int clip, int loop) {
    if (sandbox()) app->renderer.playClip(app->battle->selectedAlive(), clip, loop != 0);
}
// Every clip the selected soldiers can be made to play, as a JSON array of
// {"name", "as"}: each the game plays (sprites::kKnownClips), then any other
// a selected sheet lists of its own; "as" when a selected sheet has none of
// that name and plays another in its place (the first such sheet's).
ILLYRIA_API const char* illyria_sandbox_clips() {
    static std::string s;
    std::vector<std::string> names(std::begin(illyria::game::sprites::kKnownClips), std::end(illyria::game::sprites::kKnownClips));
    std::vector<int> types;
    if (sandbox())
        for (illyria::sim::EntityId id : app->battle->selectedAlive())
            if (const illyria::sim::Unit* u = app->battle->world().findUnit(id);
                u && std::find(types.begin(), types.end(), u->stats.type) == types.end()) {
                types.push_back(u->stats.type);
                for (const std::string& n : app->renderer.clipNames(u->stats.type))
                    if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
            }
    s = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        std::string as;
        for (int t : types)
            if (const std::string p = app->renderer.clipPlayed(t, names[i]); p != names[i]) {
                as = p.empty() ? "nothing" : p;
                break;
            }
        s += (i ? ",{\"name\":\"" : "{\"name\":\"") + names[i] + "\"" + (as.empty() ? "" : ",\"as\":\"" + as + "\"") + "}";
    }
    s += "]";
    return s.c_str();
}
// The selected soldiers play the clip of that name (or what stands in for it
// on a sheet without it), once or looped; "" hands them back.
ILLYRIA_API void illyria_sandbox_play(const char* clip, int loop) {
    if (sandbox()) app->renderer.playClip(app->battle->selectedAlive(), std::string(clip ? clip : ""), loop != 0);
}

// HUD state as JSON, polled by the page a few times a second.
ILLYRIA_API const char* illyria_hud() {
    if (!app) return "{}";
    const Battle& b = *app->battle;
    std::string& s = app->hud;
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "{\"version\":\"%s\",\"tick\":%u,\"paused\":%s,\"speed\":%d,\"slow\":%.3g,\"outcome\":%d,\"fps\":%.0f,"
                  "\"stepMs\":%.3f,\"zoom\":%.0f,\"selected\":%d,\"scenario\":%d,\"foe\":\"%s\",\"side\":%d,\"colour\":\"%s\",",
                  ILLYRIA_VERSION, b.world().tick(), b.paused() ? "true" : "false", b.speed(), b.slowMotion(), b.outcome(), app->fps,
                  b.stepMs(), app->camera.distance(), b.selectedSquad(), b.scenario() == Scenario::Siege ? 1 : 0, b.foeName().c_str(),
                  b.localSide(), illyria::game::palette::hex(illyria::game::factionColor(static_cast<uint8_t>(b.localSide()))).c_str());
    s = buf;
    // The town's capture point, and the selection's small edges.
    {
        const CapturePoint& cap = b.capturePoint();
        const std::string edges = b.selectionEdges();
        std::snprintf(buf, sizeof buf, "\"capture\":{\"on\":%s,\"p\":%d,\"att\":%d,\"def\":%d,\"taken\":%s},\"edges\":\"%s\",",
                      cap.active() ? "true" : "false", cap.progress() * 100 / CapturePoint::kFull, cap.attackersIn(), cap.defendersIn(),
                      cap.taken() ? "true" : "false", edges.c_str());
        s += buf;
    }
    // The selection (how many, in what formation, of what kind) and the saved groups.
    {
        // The order mode, and a refused order's reason while fresh (3 s).
        const bool fresh = !b.notice().empty() && b.world().tick() - b.noticeTick() < 3 * illyria::sim::kTicksPerSecond;
        // Hyena-folk among them: the Raid button shows.
        std::snprintf(buf, sizeof buf, "\"mode\":%d,\"batter\":%s,\"raid\":%s,\"planning\":%s,\"planned\":%zu,\"notice\":\"%s\",", static_cast<int>(b.orderMode()),
                      b.selectionBatters() ? "true" : "false", b.selectionRaids() ? "true" : "false", b.planning() ? "true" : "false", b.plannedCount(),
                      fresh ? b.notice().c_str() : "");
        s += buf;
        const bool asTheyStand = b.keepingShape();
        std::snprintf(buf, sizeof buf, "\"selCount\":%zu,\"selLabel\":\"%s\",\"formation\":\"%s\",\"formationId\":%d,\"groups\":[",
                      b.selectionSize(), b.selectionLabel(), asTheyStand ? kKeepShapeName : formationName(b.selectionFormation()),
                      asTheyStand ? kKeepShapeId : static_cast<int>(b.selectionFormation()));
        s += buf;
        bool first = true;
        for (int k = 1; k <= Battle::kGroupSlots; ++k) {
            const int slot = k % Battle::kGroupSlots;
            if (b.groupSize(slot) == 0) continue;
            std::snprintf(buf, sizeof buf, "%s{\"slot\":%d,\"alive\":%d,\"total\":%zu,\"label\":\"%s\",\"sel\":%d}", first ? "" : ",", slot,
                          b.groupMen(slot).alive(), b.groupSize(slot), b.groupLabel(slot), b.groupSelection(slot));
            s += buf;
            first = false;
        }
        s += "],\"squads\":[";
    }
    // The squads that are the local side's (its own to command, or an ally's), as cards; the others, in all, as the foe.
    int foeAlive = 0, foeTotal = 0;
    bool firstSquad = true;
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& q = b.squads()[i];
        static const char* kRoles[] = {"melee", "ranged", "bomber", "caster", "healer", "flier", "siege", "scout"};
        const bool friendly = q.owner == b.localSide();
        if (!friendly) {
            foeAlive += b.aliveCount(q);
            foeTotal += b.totalCount(q);
            continue;
        }
        const Standing st = b.standing(q);
        // Whose it is, seen from the local side (Battle::localSide): mine to order, or an ally's (my side, another seat).
        const bool mine = b.controllable(q);
        std::snprintf(buf, sizeof buf,
                      "%s{\"i\":%zu,\"key\":\"%s%u\",\"name\":\"%s\",\"type\":\"%s\",\"owner\":%d,\"role\":\"%s\",\"formation\":\"%s\",\"alive\":%d,\"total\":%d,"
                      "\"strength\":%d,\"rank\":%d,\"level\":%d,\"blessings\":%d,\"kills\":%d,\"broken\":%d,\"sel\":%d,\"mine\":%s,"
                      "\"ally\":%s,\"rare\":%s,\"hired\":%s,\"command\":%d,\"arms\":\"%s\"}",
                      firstSquad ? "" : ",", i, q.army ? "a" : "s", q.army ? q.army : static_cast<uint32_t>(i), q.name.c_str(), illyria::sim::unitTypeName(q.type), q.owner,
                      kRoles[static_cast<int>(roleOf(q.type))], formationName(q.formation), b.aliveCount(q), b.totalCount(q), st.strength, st.rank,
                      st.level, st.blessings, st.kills, st.broken, mine ? b.squadSelection(q) : 0, mine ? "true" : "false", !mine ? "true" : "false",
                      q.rare ? "true" : "false", q.hired ? "true" : "false", q.command, q.arms.c_str());
        s += buf;
        firstSquad = false;
    }
    std::snprintf(buf, sizeof buf, "],\"foeAlive\":%d,\"foeTotal\":%d", foeAlive, foeTotal);
    s += buf;
    // What is looked at and cannot be ordered (a man not ours, or a gate): view-only.
    if (const Battle::Glance g = b.glance(); g.any) {
        if (g.gate)
            std::snprintf(buf, sizeof buf, ",\"inspect\":{\"kind\":\"gate\",\"hp\":%d,\"max\":%d,\"broken\":%s,\"ours\":%s,\"opens\":%s,\"swing\":%d,\"pin\":%d}", g.hp, g.max,
                          g.broken ? "true" : "false", g.ours ? "true" : "false", g.opens ? "true" : "false", g.swing, g.pin);
        else
            std::snprintf(buf, sizeof buf,
                          ",\"inspect\":{\"kind\":\"man\",\"type\":\"%s\",\"owner\":%d,\"hp\":%d,\"max\":%d,\"rank\":%d,\"morale\":%d,\"awake\":%s,\"stack\":%d}",
                          g.type, g.owner, g.hp, g.max, g.rank, g.morale, g.awake ? "true" : "false", g.stack);
        s += buf;
    }
    s += "}";
    return s.c_str();
}

// The game modes (docs/GAME_DESIGN.md §4.6): one entry for all of them, so
// the title screen starts each the same way. Ids: "title" (the menu's
// backdrop: the last battlefield, still), "skirmish" ({"scenario":0} Ford,
// 1 Siege), "campaign" ({"hard":1} new, {"hard":0} Easy lords, "player" the faction to start as and "enemies" a bit for each
// computer lord in the game, or
// {"save":"ILC2...."} to carry on a saved one), and the modes built into
// startOtherMode ("horde", "ctf"). 1 when started, 0 when the mode is not
// built in or could not start (illyria_mode_error says why).
static int startMode(const char* id, const char* options);
ILLYRIA_API int illyria_start_mode(const char* id, const char* options) {
    const int ok = startMode(id, options);
    // The player's colour follows the side they lead in what began (palette.h); nothing the simulation reads.
    if (ok && app) illyria::game::palette::bind(illyria::game::sideFactions()[app->battle->localSide() & 7]);
    return ok;
}
// The colour the player chose for their side ("#rrggbb"; "" for each lord's own), kept by the shell on the device.
ILLYRIA_API void illyria_set_side_colour(const char* hex) {
    illyria::game::V3 c;
    if (illyria::game::palette::parseHex(hex, c)) illyria::game::palette::choose(c);
    else illyria::game::palette::clear();
}
// What a colour would become for the side led (the swatch itself, a colour apart from the others, or the nearest swatch).
ILLYRIA_API const char* illyria_snap_colour(const char* hex, int faction) {
    static std::string out;
    illyria::game::V3 c;
    out = illyria::game::palette::parseHex(hex, c) ? illyria::game::palette::hex(illyria::game::palette::snap(c, static_cast<uint8_t>(faction & 7))) : "";
    return out.c_str();
}
static int startMode(const char* id, const char* options) {
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
            const std::shared_ptr<const illyria::sim::Continent> continent = app->battle->continentShared();
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
            // Who plays: the faction the player starts as, and a bit for each computer lord in the game (all, by default).
            illyria::sim::Campaign::Setup setup;
            setup.player = static_cast<uint8_t>(std::clamp(static_cast<int>(optionNumber(opts, "player", 0)), 0, 7));
            setup.enemies = static_cast<uint8_t>(std::clamp(static_cast<int>(optionNumber(opts, "enemies", 255)), 0, 255));
            startCampaign(optionNumber(opts, "hard", 1) != 0, setup);
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
ILLYRIA_API const char* illyria_mode_error() { return app ? app->error.c_str() : ""; }
// Whether a mode is built into this version (the title screen asks, to mark the rest "coming soon").
ILLYRIA_API int illyria_mode_available(const char* id) {
    const std::string mode = id ? id : "";
    return mode == "title" || mode == "skirmish" || mode == "campaign" || otherModeAvailable(mode) ? 1 : 0;
}

// The campaign as text, to keep in a save slot ("" when none is being played).
ILLYRIA_API const char* illyria_campaign_save() {
    if (!app || !app->camp.c || !app->camp.on) return "";
    app->saved = app->camp.c->save();
    return app->saved.c_str();
}
ILLYRIA_API void illyria_campaign_set_speed(int speed) {
    if (app) app->camp.speed = std::clamp(speed, 1, 8);
}

}  // extern "C"

namespace illyria::run {

bool start() {
    ::app = new App();
    ::app->gestures = std::make_unique<Gestures>(::app->camera, [](float x, float y) { clickAt(x, y); });
    // A right click on the battlefield: an order, never a selection.
    ::app->gestures->setSecondaryTap([](float x, float y) { ::app->battle->orderTap(::app->camera, x, y, ::app->pixelRatio, ::app->shift); });
    // A press held on the battlefield: a selection box, or a move's facing line.
    ::app->gestures->setHold(
        [](float x, float y, bool box) {
            // A mouse's right button held: a facing line, and only with men to order.
            if (::app->mouse && !box && ::app->battle->selectionSize() == 0) return false;
            ::app->pressX = x, ::app->pressY = y;
            ::app->boxPress = box;
            return ::app->battle->pressBegin(::app->camera, x, y, ::app->pixelRatio, box);
        },
        [](float x, float y) { ::app->battle->pressMove(::app->camera, x, y); },
        [](float x, float y, bool cancelled) {
            // A mouse's click is a press that never became a box: the town's, or a selection's.
            const bool click = ::app->mouse && ::app->boxPress && !cancelled &&
                               std::abs(x - ::app->pressX) < 12.0f * ::app->pixelRatio && std::abs(y - ::app->pressY) < 12.0f * ::app->pixelRatio;
            ::app->boxPress = false;
            if (click) {
                ::app->battle->pressEnd(::app->camera, x, y, true, ::app->shift);
                clickAt(::app->pressX, ::app->pressY);
                return;
            }
            ::app->battle->pressEnd(::app->camera, x, y, cancelled, ::app->shift);
        });
    newBattle();
    resizeCanvas();
    if (!::app->renderer.init(*::app->battle)) return false;
    fetchPrerenderedSheets(::app->renderer);
    fetchModels(::app->renderer);
    startIntro();
    return true;
}

bool frame() { return ::frame(); }

}  // namespace illyria::run
