// Sound effects (game/src/sfx.cpp), their manifest (game/web/sfx/sounds.json)
// and the rendered files' stats (tools/sfx/synth); and the herald's casualty
// reports and the cries of the dying (game/src/voice.cpp).
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/sfx.h"
#include "illyria/game/voice.h"
#include "illyria/sim/region.h"
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
        CHECK(by == "game" || by == "page" || by == "future" || by == "ambience" || by == "hook");
        // The loops the ambience fades (Ambience in sfx.h, set by sfx.js), and nothing else, are "ambience".
        CHECK((by == "ambience") == (s["behaviour"].str.rfind("loop", 0) == 0 && s["id"].str != "snd_amb_subterranean_rumble"));
        // One-shots have variety; loops are one long seamless file.
        if (s["behaviour"].str.rfind("loop", 0) == 0) CHECK(n == 1);
        else if (by == "game" && s["id"].str.find("state_") == std::string::npos) CHECK(n >= (s["id"].str.find("_ui_") != std::string::npos || s["id"].str.find("_conq_") != std::string::npos ? 1u : 3u) && n <= 6);
    }
    // Every sound the code asks for is there, and every "game" sound is one the code asks for.
    std::set<std::string> code;
    for (int i = 0; i < kSoundCount; ++i) {
        const std::string id = soundName(static_cast<Sound>(i));
        code.insert(id);
        CHECK(byId.count(id) == 1);
        if (byId.count(id)) CHECK((*byId[id])["emitted_by"].str == "game" || (*byId[id])["emitted_by"].str == "hook");
    }
    for (const auto& [id, s] : byId)
        if (((*s)["emitted_by"].str == "game" || (*s)["emitted_by"].str == "hook") && !code.count(id)) {
            std::printf("  in the manifest but never played: %s\n", id.c_str());
            CHECK(false);
        }
    // The ambience loops the page fades (sfx.js) are there too.
    for (const char* loop : {"snd_amb_wind_mountain", "snd_amb_wind_valley", "snd_amb_swamp_insects", "snd_amb_carrion_flies",
                             "snd_amb_war_drums", "snd_amb_rain_light", "snd_amb_rain_heavy", "snd_ui_click", "snd_ui_page_turn"})
        CHECK(byId.count(loop) == 1);
    for (const char* loop : kLayerNames) CHECK(byId.count(loop) == 1 && (*byId[loop])["emitted_by"].str == "ambience");
    // The numbers both players mix by.
    for (const char* key : {"strips", "fade_ms", "lowpass_near_hz", "lowpass_far_hz", "reverb_near", "reverb_far", "loop_gain"}) CHECK(m["mix"][key].number > 0);
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
            CHECK(st["seconds"].number >= 0.04 && st["seconds"].number <= (loop ? 21 : 12));  // (the day's air is a 20 s loop)
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
    CHECK(busiest <= 8);  // (five, and the grave ones that are always heard)
    CHECK(tooSoon == 0);
    for (int n : perMinute) CHECK(n <= 60 * 24 + 8);  // a busy battle, never more than the channels carry (24 a second)
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
        // A satchel laid among them, set off by the grenadier's Molotov.
        const sim::EntityId dwarf = g.spawnUnit(0, {m(-14), m(0), m(0)}, kEast, sim::unitStats(UnitType::DwarfGrenadier));
        for (int i = 0; i < 6; ++i) g.spawnUnit(1, {m(i % 3), m(i / 3 * 2 - 1), m(0)}, kWest, sim::unitStats(UnitType::Archers));
        g.placeSatchel(0, {m(1), m(0), m(0)}, sim::unitStats(UnitType::DwarfGrenadier).satchels.charge);
        const sim::Command bomb{0, dwarf, sim::Command::Type::AttackGround, {m(1), m(0), m(0)}};
        g.step(std::span(&bomb, 1));
        const std::vector<SfxCue> c = listen(g, {0, 0, 0}, 20);
        gibs += count(c, Sound::GibBurst);
        rain += count(c, Sound::BloodRain);
        blasts += count(c, Sound::SatchelBlast);
        CHECK(count(c, Sound::SatchelThrow) > 0);    // the bottle thrown
        CHECK(count(c, Sound::FireballBlast) > 0);   // and shattering into flame
    }
    CHECK(blasts > 0 && gibs > 0 && rain > 0);
}

TEST(the_water_is_heard) {
    // A fight stood in the old ford (the river's 0.6 m of water at (560, 512)): the slosh of the men in it, and the dead
    // going down with a splash, not a thud. A head thrown into the deep river above it: a plop.
    sim::WorldConfig cfg;
    cfg.seed = 3;
    cfg.region = std::make_shared<const sim::Region>(sim::Region::generateIllyria(20261027));
    sim::World w(cfg);
    for (int i = 0; i < 8; ++i) {
        const sim::Fixed y = m(506 + i * 2);
        w.spawnUnit(0, {m(556), y, w.groundHeight(m(556), y)}, kEast, sim::unitStats(UnitType::Warrior));
        w.spawnUnit(1, {m(564), y, w.groundHeight(m(564), y)}, kWest, sim::unitStats(UnitType::Berserker));
    }
    w.launchPart(sim::RemainKind::Head, 0, {m(555), m(570), cfg.region->waterSurfaceAt(m(555), m(570)) + m(3)}, {});
    const std::vector<SfxCue> cues = listen(w, {560, 512, 0}, 45);
    int dead = 0;
    for (const sim::Unit& u : w.units()) dead += !u.alive();
    std::printf("  %d dead in the ford: %d wading, %d splashes, %d falls on ground, %d plops\n", dead, count(cues, Sound::Wade), count(cues, Sound::BodySplash),
                count(cues, Sound::BodyFall) + count(cues, Sound::BodyFallArmor), count(cues, Sound::WaterPlop));
    CHECK(dead > 0);
    CHECK(count(cues, Sound::Wade) > 0);
    CHECK(count(cues, Sound::Wade) < 45 * 2);  // a slosh now and then, not a roar
    CHECK(count(cues, Sound::BodySplash) > 0);
    CHECK(count(cues, Sound::WaterPlop) >= 1);  // (the head, and whatever the berserkers cut off)
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
    // (the drums beat only while he comes on)
    std::vector<sim::Command> come;
    for (sim::EntityId id = 7; id <= 16; ++id) come.push_back({1, id, sim::Command::Type::Move, {m(100), m(10), m(0)}});
    w.step(come);
    listen(w, {0, 5, 0}, 2, 40, 0, &s);
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

// A man taken out of the world (the conquest's fights put men in it and take them out) is no casualty, no body for the
// flies, and no voice; and the next man to take his place has none of his past (what he was last tick, when he last spoke).
TEST(a_man_taken_out_of_the_world_is_no_casualty_no_fly_and_no_voice_and_his_place_has_no_past) {
    sim::World w(sim::WorldConfig{});
    std::vector<sim::EntityId> mine;
    for (int i = 0; i < 10; ++i) mine.push_back(w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry)));
    for (int i = 0; i < 4; ++i) w.spawnUnit(1, {m(300), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    Voice v(0);
    v.setListener({0, 10, 0}, {1, 0, 0}, 30);
    const auto hear = [&](int ticks) {
        std::vector<VoiceCue> out;
        for (int i = 0; i < ticks; ++i) {
            w.step({});
            v.observe(w, 0);
            for (const VoiceCue& c : v.take()) out.push_back(c);
        }
        return out;
    };
    hear(2);
    // Eight of the ten are taken out of the world (not killed): the herald has no casualties to report.
    for (int i = 0; i < 8; ++i) w.removeUnit(mine[static_cast<size_t>(i)]);
    for (const VoiceCue& c : hear(10 * sim::kTicksPerSecond)) {
        CHECK(c.situation != Situation::Casualties && c.situation != Situation::SevereCasualties && c.situation != Situation::TotalCasualties);
        CHECK(c.speaker == sim::kNoEntity || w.findUnit(c.speaker) != nullptr);  // (nobody speaks who is not in the world)
    }

    // The flies: bodies near the view draw them; the same bodies taken out of the world do not.
    const auto flies = [](bool removeThem) {
        sim::World f(sim::WorldConfig{});
        std::vector<sim::EntityId> ids;
        for (int i = 0; i < 14; ++i) {
            sim::UnitStats frail = sim::unitStats(UnitType::LightInfantry);
            frail.maxHp = 1;
            frail.aggroRange = sim::Fixed::zero();
            ids.push_back(f.spawnUnit(1, {m(4), m(i), m(0)}, kWest, frail));
        }
        for (int i = 0; i < 14; ++i) f.launchPart(sim::RemainKind::Head, 0, {m(-1), m(i), sim::Fixed::fromRatio(6, 5)}, {m(14), m(0), sim::Fixed::one()});
        for (int i = 0; i < 3 * sim::kTicksPerSecond; ++i) f.step({});
        int fallen = 0;
        for (sim::EntityId id : ids) fallen += !f.findUnit(id)->alive();
        CHECK(fallen > 6);
        if (removeThem)
            for (sim::EntityId id : ids) f.removeUnit(id);
        Sfx s;
        listen(f, {4, 7, 0}, 30, 20, 0, &s);
        return s.ambience().flies;
    };
    CHECK(flies(false) > 0);
    CHECK(flies(true) == 0);

    // The place he left, given to another the same tick: that man was not seen to be wounded, to have won a kill, or to
    // have spoken. A cue for him is a cue for a man who is there (kills and wounds count from the day he comes).
    sim::World r(sim::WorldConfig{});
    const sim::EntityId old = r.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry));
    r.spawnUnit(1, {m(300), m(0), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    Voice rv(0);
    rv.setListener({0, 0, 0}, {1, 0, 0}, 30);
    const auto hearR = [&](int ticks) {
        std::vector<VoiceCue> out;
        for (int i = 0; i < ticks; ++i) {
            r.step({});
            rv.observe(r, 0);
            for (const VoiceCue& c : rv.take()) out.push_back(c);
        }
        return out;
    };
    hearR(2);
    r.removeUnit(old);
    const sim::EntityId next = r.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry));
    CHECK(next == old);
    r.restoreUnit(next, 5, 9, sim::kMoraleFull);  // five hit points of his thirty-odd and nine kills: he comes wounded and proven
    for (const VoiceCue& c : hearR(3 * sim::kTicksPerSecond)) CHECK(c.speaker != next || (c.situation != Situation::Wounded && c.situation != Situation::Kill && c.situation != Situation::VetKill));
}

// The hearing curve against the cameras players use: a skirmish opens at 120, a Conquest stack at 583.
TEST(a_battle_is_heard_at_the_cameras_it_is_played_from) {
    const float open = hear(0, 120, false).gain, mid = hear(0, 300, false).gain, stack = hear(0, 583, false).gain;
    std::printf("  centre gain at camera 40 %.2f, 120 %.2f, 300 %.2f, 583 %.2f (loud %.2f), 1500 %.2f, 4233 %.3f\n", hear(0, 40, false).gain, open, mid, stack,
                hear(0, 583, true).gain, hear(0, 1500, false).gain, hear(0, 4233, false).gain);
    CHECK(hear(0, 40, false).gain > 0.99f);
    CHECK(open > 0.7f);                     // within 3 dB of full at the opening camera (it was -10 dB)
    CHECK(mid > 0.4f && mid < open);
    CHECK(stack > 0.2f && stack < mid);     // a floor of about -13 dB at the Conquest's camera (it was silent from 220)
    CHECK(hear(0, 583, true).gain > 0.38f);
    CHECK(hear(0, 4233, false).gain < 0.04f);  // the whole island in view: only the loops
    // Height changes the tone more than the level; what is at the edge of the view is quieter than the middle.
    CHECK(hear(0, 120, false).distance < 0.3f && hear(0, 583, false).distance > 0.6f);
    CHECK(hear(150, 120, false).gain < open * 0.5f && hear(150, 120, false).gain > 0);
    CHECK(hear(900, 120, false).gain == 0);
    float last = 2;
    for (float z = 20; z < 4500; z += 20) {
        const float g = hear(0, z, false).gain;
        CHECK(g <= last + 1e-6f);
        last = g;
    }
}

namespace {
// Two lines at grips for `seconds` of game time at a game speed, heard from the middle of them.
struct Heard {
    std::vector<SfxCue> cues;
    int most = 0;  // the most in any second of real time
};
Heard melee(float speed, int seconds) {
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 60; ++i) w.spawnUnit(0, {m(-3 - (i / 20) * 2), m(i % 20 * 2), m(0)}, kEast, sim::unitStats(UnitType::HeavyInfantry));
    for (int i = 0; i < 60; ++i) w.spawnUnit(1, {m(3 + (i / 20) * 2), m(i % 20 * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    Sfx s;
    s.setListener({0, 20, 0}, {1, 0, 0}, 60);
    s.setSpeed(speed);
    Heard h;
    const int perSecond = static_cast<int>(static_cast<float>(sim::kTicksPerSecond) * speed);  // ticks in a second of real time
    int inSecond = 0;
    for (int i = 0; i < seconds * sim::kTicksPerSecond; ++i) {
        w.step({});
        s.observe(w, 0, scene().continent(), scene().origin(), false);
        for (const SfxCue& c : s.take()) h.cues.push_back(c), ++inSecond;
        if ((i + 1) % perSecond == 0) h.most = std::max(h.most, inSecond), inSecond = 0;
    }
    return h;
}
}  // namespace

TEST(cues_are_paced_in_real_time_whatever_the_game_speed) {
    const Heard one = melee(1, 16), four = melee(4, 16), eight = melee(8, 16);
    std::printf("  a 120-man melee, 16 s of game time: %zu cues at 1x (most %d a second), %zu at 4x (%d), %zu at 8x (%d)\n", one.cues.size(), one.most,
                four.cues.size(), four.most, eight.cues.size(), eight.most);
    CHECK(one.cues.size() > 40);
    // No more than the channels can carry, at any speed: 24 a second and a burst of 8.
    CHECK(one.most <= 32 && four.most <= 32 && eight.most <= 32);
    // The same game time at 4x is a quarter of the real time: far fewer cues, not the same number squeezed together.
    CHECK(four.cues.size() * 2 < one.cues.size());
    CHECK(eight.cues.size() <= four.cues.size());
    // No one sound rattles: two of a kind are never nearer than its gap in real time (4 ticks at 1x, 16 at 4x).
    for (const Heard* h : {&one, &four}) {
        std::map<Sound, uint32_t> last;
        const uint32_t gap = h == &one ? 4 : 16;
        for (const SfxCue& c : h->cues) {
            if (c.sound != Sound::SwordFlesh && c.sound != Sound::SwordArmor && c.sound != Sound::SwordSwing) continue;
            if (last.count(c.sound)) CHECK(c.tick - last[c.sound] >= gap);
            last[c.sound] = c.tick;
        }
    }
    // At 4x the slightest sounds are left out, and a delay is real time.
    for (const SfxCue& c : four.cues) {
        CHECK(c.priority > 0);
        CHECK(c.delay <= 0.6f / 4 + 1e-4f);
    }
}

TEST(a_body_of_men_on_the_move_is_one_sound_not_a_cue_a_man) {
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 40; ++i) w.spawnUnit(0, {m(i % 8 * 2), m(i / 8 * 2), m(0)}, kEast, sim::unitStats(UnitType::LightInfantry));
    for (int i = 0; i < 12; ++i) w.spawnUnit(0, {m(i % 6 * 3), m(60 + i / 6 * 3), m(0)}, kEast, sim::unitStats(UnitType::HeavyCavalry));
    w.spawnUnit(0, {m(0), m(-40), m(0)}, kEast, sim::unitStats(UnitType::Giants));
    Sfx s;
    // Standing still: no footfalls.
    const std::vector<SfxCue> still = listen(w, {40, 20, 0}, 3, 60, 0, &s);
    CHECK(count(still, Sound::FootMarch) + count(still, Sound::Hooves) + count(still, Sound::GiantTread) == 0);
    std::vector<sim::Command> go;
    for (sim::EntityId id = 1; id <= 53; ++id) go.push_back({0, id, sim::Command::Type::Move, {m(400), w.units()[id - 1].pos.y, m(0)}});
    w.step(go);
    std::vector<SfxCue> cues;
    s.setListener({40, 20, 0}, {1, 0, 0}, 60);
    for (int i = 0; i < 6 * sim::kTicksPerSecond; ++i) {
        // (the camera goes with them)
        V3 at{};
        for (int k = 0; k < 40; ++k) at = at + toV3(w.units()[static_cast<size_t>(k)].pos);
        s.setListener(at * (1.0f / 40) + V3{0, 15, 0}, {1, 0, 0}, 60);
        w.step({});
        s.observe(w, 0, scene().continent(), scene().origin(), false);
        for (const SfxCue& c : s.take()) cues.push_back(c);
    }
    const int foot = count(cues, Sound::FootMarch), hooves = count(cues, Sound::Hooves), giant = count(cues, Sound::GiantTread);
    std::printf("  6 s on the march: %d footfall cues for 40 men, %d for 12 horse, %d for a giant\n", foot, hooves, giant);
    CHECK(foot >= 6 && foot <= 40);  // a texture, a few a second: not 40 men x 2 steps a second
    CHECK(hooves >= 4 && hooves <= 60);
    CHECK(giant >= 3 && giant <= 12);
    // From the far camera the single sounds give way to the column's loop; at 4x they are left out.
    Sfx far;
    far.setListener({200, 20, 0}, {1, 0, 0}, 583);
    int steps = 0;
    for (int i = 0; i < 3 * sim::kTicksPerSecond; ++i) {
        w.step({});
        far.observe(w, 0, scene().continent(), scene().origin(), false);
        for (const SfxCue& c : far.take()) steps += c.sound == Sound::FootMarch || c.sound == Sound::Hooves;
    }
    CHECK(steps == 0);
    CHECK(far.ambience().march > 0.1f);
    CHECK(far.ambience().bed > 0.2f);
}

TEST(the_players_own_acts_sound_at_once_and_the_grave_cues_always) {
    Sfx s;
    s.ui(Sound::OrderConfirm);
    s.ui(Sound::OrderConfirm);  // (twice in a frame is once)
    s.ui(Sound::OrderRefuse);
    const std::vector<SfxCue> cues = s.take();  // no world stepped: a pause, a deployment
    CHECK(cues.size() == 2);
    CHECK(cues[0].sound == Sound::OrderConfirm && !cues[0].positional && cues[0].gain == 1.0f);
    CHECK(std::string(soundName(Sound::OrderRefuse)) == "snd_ui_order_refuse");
    // A horn in the thick of a melee that has used the budget up is still heard.
    sim::World w(sim::WorldConfig{});
    for (int i = 0; i < 60; ++i) w.spawnUnit(0, {m(-3), m(i * 2), m(0)}, kEast, sim::unitStats(UnitType::HeavyInfantry));
    for (int i = 0; i < 60; ++i) w.spawnUnit(1, {m(3), m(i * 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
    Sfx t;
    listen(w, {0, 60, 0}, 6, 60, 0, &t);
    int horns = 0;
    for (int i = 0; i < 30; ++i) {
        w.step({});
        t.alert(Sound::WarHorn);
        t.hook(Sound::BoulderImpact, {0, 60, 0});
        t.observe(w, 0, scene().continent(), scene().origin(), false);
        for (const SfxCue& c : t.take()) horns += c.sound == Sound::WarHorn;
    }
    CHECK(horns == 30);
    CHECK(t.emittedOf(Sound::BoulderImpact) >= 5);  // a hook is merged and throttled like any cue (its gap is 3 ticks)
}

TEST(the_conquest_sounds_its_events_one_at_a_time_the_gravest_first) {
    Sfx s;
    ConquestPulse p;
    p.side = 2;
    p.cityOwner = {2, 0, 1, 255};
    p.lords = 2, p.squads = 5;
    s.conquest(p);  // the first look: nothing has happened yet
    CHECK(s.take().empty());
    const auto one = [&](Sound want) {
        s.conquest(p);
        const std::vector<SfxCue> cues = s.take();
        CHECK(cues.size() == 1);
        if (!cues.empty()) CHECK(cues[0].sound == want && !cues[0].positional);
    };
    p.day = 1;
    one(Sound::DayTick);
    p.squads = 6;
    one(Sound::Recruit);
    p.cityOwner[3] = 2;
    one(Sound::TownCaptured);
    p.cityOwner[0] = 1, p.cityOwner[1] = 2, p.day = 2;  // one lost and one taken on the same dawn: the loss is heard
    one(Sound::TownLost);
    p.lords = 1, p.squads = 5;
    one(Sound::LordFallen);
    p.cityOwner[2] = 0;  // others' towns changing hands are not the player's news
    s.conquest(p);
    CHECK(s.take().empty());
    p.winner = 1;
    one(Sound::Victory);
    s.conquest(p);
    CHECK(s.take().empty());
}

// The audit page's finding: the swing and the hit were sent at the start of the attack clip, 1.3 s before a giant's
// club is shown to land. Now the hit waits for the blow's frame (half the clip) and the whoosh leads it.
TEST(a_blow_sounds_on_the_frame_that_shows_it_land) {
    Sfx probe;
    CHECK(probe.blowSeconds(static_cast<int>(UnitType::LightInfantry)) >= 0.2f && probe.blowSeconds(static_cast<int>(UnitType::LightInfantry)) <= 0.45f);
    for (const float giantClip : {0.0f, 2.6f}) {
        sim::World w(sim::WorldConfig{});
        w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sim::unitStats(UnitType::Giants));
        for (int i = 0; i < 3; ++i) w.spawnUnit(1, {m(3), m(i * 2 - 2), m(0)}, kWest, sim::unitStats(UnitType::LightInfantry));
        Sfx s;
        s.setListener({0, 0, 0}, {1, 0, 0}, 30);
        if (giantClip > 0) s.setBlowSeconds(static_cast<int>(UnitType::Giants), giantClip * 0.5f);
        const float want = s.blowSeconds(static_cast<int>(UnitType::Giants));
        std::vector<uint16_t> cooldown(w.units().size(), 0);
        int giantHits = 0;
        for (int i = 0; i < 20 * sim::kTicksPerSecond && giantHits < 2; ++i) {
            for (size_t k = 0; k < w.units().size(); ++k) cooldown[k] = static_cast<uint16_t>(w.units()[k].meleeCooldown);
            w.step({});
            std::vector<Sfx::Striker> strikers;  // as Battle::afterWorldStep finds them
            for (size_t k = 0; k < w.units().size(); ++k)
                if (w.units()[k].meleeCooldown > cooldown[k]) strikers.push_back({toV3(w.units()[k].pos), static_cast<uint8_t>(w.units()[k].stats.type), w.units()[k].owner});
            const bool giantStruck = !strikers.empty() && strikers[0].owner == 0;
            s.setStrikers(strikers);
            s.observe(w, 0, scene().continent(), scene().origin(), false);
            float swing = -1, hit = -1;
            for (const SfxCue& c : s.take()) {
                if (c.sound == Sound::HeavySwing) swing = c.delay;
                if (c.sound == Sound::BluntCrush || c.sound == Sound::SwordArmor || c.sound == Sound::SwordFlesh) hit = c.delay;
            }
            if (!giantStruck || strikers.size() != 1 || hit < 0) continue;
            ++giantHits;
            CHECK(std::abs(hit - want) < 1e-3f);                  // on the blow's frame
            if (swing >= 0) CHECK(swing < hit && swing >= hit - 0.3f);  // the whoosh leads, and not by much
        }
        std::printf("  a giant's blow: the hit sounds %.2f s after the strike (clip %s)\n", want, giantClip > 0 ? "2.6 s, from its sheet" : "default");
        CHECK(giantHits >= 1);
        if (giantClip > 0) CHECK(std::abs(want - 1.3f) < 1e-3f);
    }
}
