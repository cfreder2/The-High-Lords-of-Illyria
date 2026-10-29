// Sound effects (game/src/sfx.cpp), their manifest (game/web/sfx/sounds.json)
// and the rendered files' stats (tools/sfx/synth); and the herald's casualty
// reports and the cries of the dying (game/src/voice.cpp).
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/sfx.h"
#include "illyria/game/voice.h"
#include "illyria/sim/units.h"
#include "json.h"
#include "test.h"

extern const char* const kSfxManifest;  // sfx_manifest.cpp, generated
extern const char* const kSfxStats;

using namespace illyria::game;
using illyria::sim::UnitType;
using testjson::Json;
using testjson::Parser;
namespace sim = illyria::sim;

namespace {

const Json& parsed(const char* text) {
    static std::map<const char*, Json> cache;
    auto it = cache.find(text);
    if (it != cache.end()) return it->second;
    Parser p{text};
    Json j = p.value();
    CHECK(p.ok);
    return cache[text] = j;
}

sim::Fixed m(int64_t v) { return sim::Fixed::fromInt(v); }
const sim::Vec3 kEast{sim::Fixed::one(), sim::Fixed::zero(), sim::Fixed::zero()};
const sim::Vec3 kWest{-sim::Fixed::one(), sim::Fixed::zero(), sim::Fixed::zero()};

// A battle's continent, for the wind (any will do for the tests).
const Battle& scene() {
    static const Battle b(42);
    return b;
}

// Runs a world, listening at `at` from low down, and gathers the cues.
std::vector<SfxCue> listen(sim::World& w, const V3& at, int seconds, float zoom = 25, float rain = 0, Sfx* keep = nullptr) {
    Sfx local;
    Sfx& s = keep ? *keep : local;
    s.setListener(at, {1, 0, 0}, zoom);
    s.setRain(rain);
    std::vector<SfxCue> cues;
    for (int i = 0; i < seconds * sim::kTicksPerSecond; ++i) {
        w.step({});
        s.observe(w, 0, scene().continent(), scene().origin(), false);
        for (const SfxCue& c : s.take()) cues.push_back(c);
    }
    return cues;
}

int count(const std::vector<SfxCue>& cues, Sound s) {
    int n = 0;
    for (const SfxCue& c : cues) n += c.sound == s;
    return n;
}

}  // namespace

TEST(sfx_manifest_covers_every_sound_the_code_plays) {
    const Json& m = parsed(kSfxManifest);
    std::set<std::string> cats, ids;
    for (const Json& c : m["categories"].items) cats.insert(c["id"].str);
    std::map<std::string, const Json*> byId;
    for (const Json& s : m["sounds"].items) {
        CHECK(ids.insert(s["id"].str).second);
        byId[s["id"].str] = &s;
        CHECK(cats.count(s["category"].str) == 1);
        CHECK(!s["title"].str.empty() && !s["when"].str.empty() && !s["record"].str.empty() && !s["behaviour"].str.empty());
        const size_t n = s["files"].items.size();
        CHECK(n >= 1 && n == static_cast<size_t>(s["variants"].number));
        for (size_t i = 0; i < n; ++i) CHECK(s["files"].items[i].str == "sfx/" + s["id"].str + "_" + std::to_string(i + 1) + ".mp3");
        const std::string by = s["emitted_by"].str;
        CHECK(by == "game" || by == "page" || by == "future" || by == "ambience");
        // The loops the ambience fades (Ambience in sfx.h, set by sfx.js), and nothing else, are "ambience".
        CHECK((by == "ambience") == (s["behaviour"].str.rfind("loop", 0) == 0 && s["id"].str != "snd_amb_subterranean_rumble"));
        // One-shots have variety; loops are one long seamless file.
        if (s["behaviour"].str.rfind("loop", 0) == 0) CHECK(n == 1);
        else if (by == "game" && s["id"].str.find("state_") == std::string::npos) CHECK(n >= 3 && n <= 6);
    }
    // Every sound the code asks for is there, and every "game" sound is one the code asks for.
    std::set<std::string> code;
    for (int i = 0; i < kSoundCount; ++i) {
        const std::string id = soundName(static_cast<Sound>(i));
        code.insert(id);
        CHECK(byId.count(id) == 1);
        if (byId.count(id)) CHECK((*byId[id])["emitted_by"].str == "game");
    }
    for (const auto& [id, s] : byId)
        if ((*s)["emitted_by"].str == "game" && !code.count(id)) {
            std::printf("  in the manifest but never played: %s\n", id.c_str());
            CHECK(false);
        }
    // The ambience loops the page fades (sfx.js) are there too.
    for (const char* loop : {"snd_amb_wind_mountain", "snd_amb_wind_valley", "snd_amb_swamp_insects", "snd_amb_carrion_flies",
                             "snd_amb_war_drums", "snd_amb_rain_light", "snd_amb_rain_heavy", "snd_ui_click", "snd_ui_page_turn"})
        CHECK(byId.count(loop) == 1);
}

TEST(rendered_sound_files_are_neither_silent_nor_clipped) {
    const Json& m = parsed(kSfxManifest);
    const Json& stats = parsed(kSfxStats);
    int files = 0;
    double bytes = 0;
    for (const Json& s : m["sounds"].items) {
        const bool loop = s["behaviour"].str.rfind("loop", 0) == 0;
        for (const Json& f : s["files"].items) {
            const Json& st = stats[f.str];
            CHECK(st.type == Json::Type::Object);
            if (st.type != Json::Type::Object) {
                std::printf("  no stats for %s\n", f.str.c_str());
                continue;
            }
            ++files;
            bytes += st["bytes"].number;
            CHECK(st["peak_db"].number <= -0.5);                         // no clipping
            CHECK(st["lufs"].number > -45);                              // not silent
            CHECK(std::abs(st["dc"].number) < 0.01);                     // no DC offset
            CHECK(st["seconds"].number >= 0.04 && st["seconds"].number <= 12);
            if (loop) CHECK(st["loop_seconds"].number > 2);
            else CHECK(st["seconds"].number <= 9);                       // one-shots stay short (phones)
        }
    }
    std::printf("  %d sound files, %.2f MB\n", files, bytes / 1e6);
    CHECK(bytes < 6e6);
}

TEST(a_battle_sounds_sensibly_without_turning_to_noise) {
    Battle b(42);
    b.chargeAll();
    std::vector<SfxCue> cues;
    const int seconds = 150;
    V3 at = b.squadCentre(b.squads().front());
    for (int i = 0; i < seconds * 10; ++i) {
        // The camera on the fiercest fighting, low down.
        V3 hot;
        if (i % 10 == 0 && b.hotSpot(hot)) at = hot;
        b.sfx().setListener(at, {1, 0, 0}, 40);
        b.advance(0.1);
        for (const SfxCue& c : b.sfx().take()) cues.push_back(c);
    }
    std::map<std::string, int> bySound;
    std::map<uint32_t, int> perTick;
    int perMinute[3] = {0, 0, 0};
    std::map<int, uint32_t> last;
    int tooSoon = 0;
    for (const SfxCue& c : cues) {
        ++bySound[soundName(c.sound)];
        ++perTick[c.tick];
        perMinute[std::min<uint32_t>(c.tick / (60 * sim::kTicksPerSecond), 2)]++;
        const int i = static_cast<int>(c.sound);
        if (last.count(i) && c.tick - last[i] < 2 && c.sound != Sound::GateBreak && c.sound != Sound::SatchelBlast &&
            c.sound != Sound::FireballBlast)
            ++tooSoon;
        last[i] = c.tick;
        CHECK(c.gain > 0 && c.gain <= 1);
        CHECK(c.pan >= -1 && c.pan <= 1);
    }
    int busiest = 0;
    for (const auto& [t, n] : perTick) busiest = std::max(busiest, n);
    std::printf("  %zu cues in %d s (%d, %d, %d a minute), %d held back; busiest tick %d:\n   ", cues.size(), seconds, perMinute[0],
                perMinute[1], perMinute[2], b.sfx().dropped(), busiest);
    for (const auto& [k, n] : bySound) std::printf(" %s %d", k.c_str() + 4, n);
    std::printf("\n");
    CHECK(busiest <= 6);
    CHECK(tooSoon == 0);
    for (int n : perMinute) CHECK(n <= 60 * 14);  // a busy battle, never a wall of noise
    CHECK(bySound.size() >= 8);
    CHECK(b.sfx().dropped() > 0);  // more happened than was played
    CHECK(count(cues, Sound::SwordSwing) + count(cues, Sound::HeavySwing) > 0);
    CHECK(count(cues, Sound::BodyFall) + count(cues, Sound::BodyFallArmor) + count(cues, Sound::BonesCollapse) > 0);
}

TEST(sounds_follow_the_death_matrix) {
    // Heroes hew archers and pikemen (limbs and heads off), wizards burn healers, a grenadier blasts archers.
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 6; ++i) {
        w.spawnUnit(0, {m(0), m(i * 50), m(0)}, kEast, sim::unitStats(UnitType::Hero));
        w.spawnUnit(1, {m(3), m(i * 50), m(0)}, kWest, sim::unitStats(i % 2 ? UnitType::Archers : UnitType::Pikemen));
    }
    const std::vector<SfxCue> cuts = listen(w, {1, 125, 0}, 40, 20);
    CHECK(count(cuts, Sound::DismemberSnap) > 0);
    CHECK(count(cuts, Sound::SwordFlesh) + count(cuts, Sound::SwordArmor) > 0);

    sim::World f(sim::WorldConfig{});
    for (int i = 0; i < 3; ++i) f.spawnUnit(0, {m(0), m(i * 3), m(0)}, kEast, sim::unitStats(UnitType::Wizards));
    for (int i = 0; i < 6; ++i) f.spawnUnit(1, {m(35), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::Journeyman));
    const std::vector<SfxCue> fire = listen(f, {30, 5, 0}, 90);
    CHECK(count(fire, Sound::FireballCast) > 0);
    CHECK(count(fire, Sound::FireballBlast) > 0);
    CHECK(count(fire, Sound::FleshBurn) > 0);

    int gibs = 0, rain = 0, blasts = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        sim::WorldConfig cfg;
        cfg.seed = seed;
        sim::World g(cfg);
        const sim::EntityId dwarf = g.spawnUnit(0, {m(-14), m(0), m(0)}, kEast, sim::unitStats(UnitType::DwarfGrenadier));
        for (int i = 0; i < 6; ++i) g.spawnUnit(1, {m(i % 3), m(i / 3 * 2 - 1), m(0)}, kWest, sim::unitStats(UnitType::Archers));
        const sim::Command bomb{0, dwarf, sim::Command::Type::AttackGround, {m(1), m(0), m(0)}};
        g.step(std::span(&bomb, 1));
        const std::vector<SfxCue> c = listen(g, {0, 0, 0}, 20);
        gibs += count(c, Sound::GibBurst);
        rain += count(c, Sound::BloodRain);
        blasts += count(c, Sound::SatchelBlast);
        CHECK(count(c, Sound::SatchelThrow) > 0);
    }
    CHECK(blasts > 0 && gibs > 0 && rain > 0);
}

TEST(arrows_whistle_past_a_listener_among_them) {
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::Archers));
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(50), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    // Halfway, under the arrows' arc, close down.
    const std::vector<SfxCue> near = listen(w, {25, 5, 0}, 20, 8);
    CHECK(count(near, Sound::BowRelease) > 0);
    CHECK(count(near, Sound::ArrowWhistle) > 0);
    bool closing = false;
    for (const SfxCue& c : near) closing = closing || (c.sound == Sound::ArrowWhistle && c.doppler > 5);
    CHECK(closing);
    // Zoomed out over the field: no whistles at all.
    sim::World w2(sim::WorldConfig{});
    for (int i = 0; i < 6; ++i) w2.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::Archers));
    for (int i = 0; i < 6; ++i) w2.spawnUnit(1, {m(50), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    CHECK(count(listen(w2, {25, 5, 0}, 20, 300), Sound::ArrowWhistle) == 0);
}

TEST(only_what_is_in_earshot_is_heard) {
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::Archers));
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(50), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    const std::vector<SfxCue> far = listen(w, {900, 900, 0}, 20, 40);
    CHECK(far.empty());
}

TEST(ambience_follows_the_scene) {
    // The enemy far off and coming: war drums. Bodies lying near the view: flies, after a while.
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::Berserker));
    for (int i = 0; i < 10; ++i) w.spawnUnit(1, {m(150), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    Sfx s;
    listen(w, {0, 5, 0}, 1, 40, 0, &s);
    const float drumsFar = s.ambience().drums;
    CHECK(drumsFar > 0.2f);
    CHECK(s.ambience().flies == 0);
    CHECK(s.ambience().windMountain + s.ambience().windValley > 0);
    // They meet: the drums fall silent in the fighting.
    std::vector<sim::Command> go;
    for (sim::EntityId id = 1; id <= 6; ++id) go.push_back({0, id, sim::Command::Type::Move, {m(150), m(10), m(0)}});
    w.step(go);
    float drumsMin = 1;
    for (int k = 0; k < 120; ++k) {
        listen(w, {75, 10, 0}, 1, 40, 0, &s);
        drumsMin = std::min(drumsMin, s.ambience().drums);
    }
    CHECK(drumsMin == 0);
    // The dead near the view draw flies.
    V3 bodies{};
    int dead = 0;
    for (const sim::Unit& u : w.units())
        if (!u.alive()) {
            bodies = bodies + toV3(u.pos);
            ++dead;
        }
    CHECK(dead > 2);
    if (dead == 0) return;
    listen(w, bodies * (1.0f / static_cast<float>(dead)), 20, 40, 0, &s);
    std::printf("  drums %.2f far, %.2f at grips; flies %.2f over %d dead\n", drumsFar, drumsMin, s.ambience().flies, dead);
    CHECK(s.ambience().flies > 0.1f);
}

TEST(rain_hook_brings_thunder) {
    sim::World w(sim::WorldConfig{});
    w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry));
    Sfx s;
    const std::vector<SfxCue> wet = listen(w, {0, 0, 0}, 60, 40, 0.9f, &s);
    CHECK(s.ambience().rain > 0.8f);
    CHECK(count(wet, Sound::Thunder) >= 1);
    for (const SfxCue& c : wet)
        if (c.sound == Sound::Thunder) CHECK(c.delay >= 3 && c.delay <= 8 && !c.positional);
    sim::World dry(sim::WorldConfig{});
    dry.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry));
    CHECK(count(listen(dry, {0, 0, 0}, 60), Sound::Thunder) == 0);
}

TEST(the_herald_reports_casualties_and_the_dying_cry_out) {
    // The player's light infantry against berserkers: they fall fast.
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 10; ++i) w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry));
    for (int i = 0; i < 8; ++i) w.spawnUnit(1, {m(6), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::Berserker));
    Voice v(0);
    v.setListener({3, 10, 0}, {1, 0, 0}, 30);
    std::vector<VoiceCue> heard;
    for (int i = 0; i < 90 * sim::kTicksPerSecond; ++i) {
        w.step({});
        v.observe(w, 0);
        for (const VoiceCue& c : v.take()) heard.push_back(c);
    }
    std::vector<Situation> reports;
    int deaths = 0, pains = 0;
    for (const VoiceCue& c : heard) {
        if (c.situation == Situation::Casualties || c.situation == Situation::SevereCasualties || c.situation == Situation::TotalCasualties) {
            reports.push_back(c.situation);
            CHECK(c.set == VoiceSet::Herald);
            CHECK(c.pan == 0 && c.gain == 1);
        }
        deaths += c.situation == Situation::Death || c.situation == Situation::DeathMoan;
        pains += c.situation == Situation::Pain;
    }
    int lost = 0;
    for (const sim::Unit& u : w.units()) lost += u.owner == 0 && !u.alive();
    std::printf("  %d of 10 lost: %zu reports, %d death cries, %d cries of pain\n", lost, reports.size(), deaths, pains);
    CHECK(lost >= 8);
    // In order, each at most once (losses that pass two marks at once are reported as the graver).
    CHECK(!reports.empty() && reports.size() <= 3);
    for (size_t i = 1; i < reports.size(); ++i) CHECK(reports[i] > reports[i - 1]);
    if (!reports.empty()) CHECK(reports.back() == Situation::TotalCasualties);
    CHECK(deaths > 0 && pains > 0);
}
