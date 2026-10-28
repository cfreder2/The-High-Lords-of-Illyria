// Voiced lines (game/src/voice.cpp) and their manifest (game/web/voice/lines.json).
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/voice.h"
#include "test.h"

extern const char* const kVoiceManifest;  // voice_manifest.cpp, generated

using namespace illyria::game;
using illyria::sim::UnitType;

namespace {

// Just enough JSON for the manifest.
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    double number = 0;
    std::string str;
    std::vector<Json> items;
    // An object's fields (two lists: a pair could not hold the incomplete Json).
    std::vector<std::string> keys;
    std::vector<Json> values;
    const Json& operator[](const std::string& key) const {
        static const Json null;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return values[i];
        return null;
    }
};

struct Parser {
    const char* p;
    bool ok = true;
    void ws() {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
    }
    std::string string() {
        std::string s;
        if (*p != '"') { ok = false; return s; }
        ++p;
        while (*p && *p != '"') {
            if (*p == '\\') {
                ++p;
                switch (*p) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'u': s += '?'; p += 4; break;
                    default: s += *p;
                }
                ++p;
            } else {
                s += *p++;
            }
        }
        if (*p == '"') ++p;
        else ok = false;
        return s;
    }
    Json value() {
        ws();
        Json j;
        if (*p == '{') {
            j.type = Json::Type::Object;
            ++p;
            ws();
            if (*p == '}') { ++p; return j; }
            while (ok) {
                ws();
                std::string key = string();
                ws();
                if (*p++ != ':') ok = false;
                j.keys.push_back(key);
                j.values.push_back(value());
                ws();
                if (*p == ',') { ++p; continue; }
                if (*p++ != '}') ok = false;
                break;
            }
        } else if (*p == '[') {
            j.type = Json::Type::Array;
            ++p;
            ws();
            if (*p == ']') { ++p; return j; }
            while (ok) {
                j.items.push_back(value());
                ws();
                if (*p == ',') { ++p; continue; }
                if (*p++ != ']') ok = false;
                break;
            }
        } else if (*p == '"') {
            j.type = Json::Type::String;
            j.str = string();
        } else if (*p == 't' || *p == 'f') {
            j.type = Json::Type::Bool;
            j.number = *p == 't';
            p += *p == 't' ? 4 : 5;
        } else if (*p == 'n') {
            p += 4;
        } else {
            j.type = Json::Type::Number;
            char* end = nullptr;
            j.number = std::strtod(p, &end);
            if (end == p) ok = false;
            p = end;
        }
        return j;
    }
};

const Json& manifest() {
    static const Json m = [] {
        Parser parser{kVoiceManifest};
        Json j = parser.value();
        CHECK(parser.ok);
        return j;
    }();
    return m;
}

const Json* findSet(const std::string& id) {
    for (const Json& s : manifest()["sets"].items)
        if (s["id"].str == id) return &s;
    return nullptr;
}

size_t linesFor(const Json& set, const std::string& situation) {
    for (const Json& s : set["situations"].items)
        if (s["id"].str == situation) return s["lines"].items.size();
    return 0;
}

}  // namespace

TEST(voice_manifest_has_lines_for_everything_the_code_says) {
    const Json& m = manifest();
    CHECK(m["sets"].items.size() == static_cast<size_t>(kVoiceSetCount));
    int missing = 0, total = 0;
    for (int v = 0; v < kVoiceSetCount; ++v) {
        const VoiceSet vs = static_cast<VoiceSet>(v);
        const Json* set = findSet(voiceSetName(vs));
        CHECK(set != nullptr);
        if (!set) continue;
        const Json* fallback = set->operator[]("fallback").type == Json::Type::String ? findSet((*set)["fallback"].str) : nullptr;
        for (int s = 0; s < kSituationCount; ++s) {
            const Situation sit = static_cast<Situation>(s);
            const size_t own = linesFor(*set, situationName(sit));
            if (!voices(vs, sit)) {
                CHECK(own == 0);  // lines nobody would ever hear
                continue;
            }
            const size_t n = own > 0 ? own : fallback ? linesFor(*fallback, situationName(sit)) : 0;
            if (n == 0) {
                std::printf("  no lines: %s %s\n", voiceSetName(vs), situationName(sit));
                ++missing;
            }
            if (own > 0) CHECK(own >= 3 && own <= 5);  // variety, but not too much to record
        }
        for (const Json& s : (*set)["situations"].items) total += static_cast<int>(s["lines"].items.size());
    }
    CHECK(missing == 0);
    std::printf("  %d lines in %d voices\n", total, kVoiceSetCount);
}

TEST(voice_manifest_lines_are_well_formed) {
    const Json& m = manifest();
    std::set<std::string> known, ids, files;
    for (int s = 0; s < kSituationCount; ++s) known.insert(situationName(static_cast<Situation>(s)));
    // The manifest's situations are the code's, in its order and with its priorities.
    CHECK(m["situations"].items.size() == static_cast<size_t>(kSituationCount));
    for (size_t i = 0; i < m["situations"].items.size() && i < static_cast<size_t>(kSituationCount); ++i) {
        const Json& s = m["situations"].items[i];
        CHECK(s["id"].str == situationName(static_cast<Situation>(i)));
        CHECK(static_cast<int>(s["priority"].number) == priorityOf(static_cast<Situation>(i)));
    }
    for (const Json& set : m["sets"].items) {
        CHECK(!set["casting"].str.empty());
        for (const Json& s : set["situations"].items) {
            CHECK(known.count(s["id"].str) == 1);
            CHECK(!s["note"].str.empty());
            int n = 0;
            for (const Json& line : s["lines"].items) {
                ++n;
                const std::string file = "voice/" + set["id"].str + "/" + s["id"].str + "_" + std::to_string(n) + ".mp3";
                CHECK(line["file"].str == file);
                CHECK(!line["text"].str.empty() && line["text"].str.size() < 60);
                CHECK(ids.insert(line["id"].str).second);
                CHECK(files.insert(file).second);
            }
        }
    }
    // Every kind of soldier has a voice the manifest knows.
    for (int t = 0; t < illyria::sim::kUnitTypeCount; ++t) CHECK(findSet(voiceSetName(voiceSetOf(static_cast<UnitType>(t)))));
}

namespace {
struct Heard {
    std::vector<VoiceCue> cues;
    void take(Battle& b) {
        for (const VoiceCue& c : b.voice().take()) cues.push_back(c);
    }
    bool has(Situation s) const {
        for (const VoiceCue& c : cues)
            if (c.situation == s) return true;
        return false;
    }
};
}  // namespace

TEST(orders_are_answered_by_one_of_those_ordered) {
    Battle b(42);
    Heard h;
    b.selectSquad(1);  // the Grey Watch
    h.take(b);
    CHECK(h.cues.size() == 1);
    CHECK(h.has(Situation::Selected));
    CHECK(b.squadOf(h.cues.front().speaker) == 1);
    CHECK(h.cues.front().set == VoiceSet::Soldier);
    b.orderMove(b.squadCentre(b.squads()[1]) + V3{0, 30, 0});
    h.take(b);
    CHECK(h.has(Situation::Move));
    // The hero answers for the army.
    b.chargeAll();
    h.take(b);
    CHECK(h.has(Situation::Charge));
    CHECK(h.cues.back().set == VoiceSet::Hero);
    CHECK(!h.cues.back().enemy);
}

TEST(a_battle_speaks_sensibly_without_chattering) {
    Battle b(42);
    Heard h;
    b.chargeAll();
    const int seconds = 150;
    for (int i = 0; i < seconds * 10; ++i) {
        b.advance(0.1);
        h.take(b);
    }
    std::map<std::string, int> bySituation;
    int ambient = 0, tooClose = 0, sameSpeaker = 0;
    uint32_t lastAmbient = 0;
    std::map<illyria::sim::EntityId, uint32_t> lastBy;
    int perMinute[3] = {0, 0, 0};
    for (const VoiceCue& c : h.cues) {
        ++bySituation[std::string(situationName(c.situation)) + (c.enemy ? "(enemy)" : "")];
        perMinute[std::min<uint32_t>(c.tick / (60 * illyria::sim::kTicksPerSecond), 2)]++;
        if (c.priority <= 2) {
            if (ambient > 0 && c.tick - lastAmbient < 45) ++tooClose;
            if (lastBy.count(c.speaker) && c.tick - lastBy[c.speaker] < 150) ++sameSpeaker;
            lastAmbient = c.tick;
            ++ambient;
        }
        lastBy[c.speaker] = c.tick;
    }
    std::printf("  %zu cues in %d s (%d, %d, %d a minute), %d dropped:", h.cues.size(), seconds, perMinute[0], perMinute[1],
                perMinute[2], b.voice().dropped());
    for (const auto& [k, n] : bySituation) std::printf(" %s %d", k.c_str(), n);
    std::printf("\n");
    CHECK(h.has(Situation::Charge));
    CHECK(h.has(Situation::EnemySighted));
    CHECK(bySituation.size() >= 8);  // a battle has many moods
    CHECK(tooClose == 0);
    CHECK(sameSpeaker == 0);
    for (int n : perMinute) CHECK(n <= 45);  // never a babble
    CHECK(b.voice().dropped() > static_cast<int>(h.cues.size()));  // far more happened than was said
}

TEST(only_what_is_in_earshot_is_heard_but_orders_always_are) {
    Battle b(42);
    // Listening from a far corner, zoomed right out.
    b.voice().setListener({0, 0, 0}, {1, 0, 0}, 400);
    Heard h;
    b.selectSquad(1);
    b.chargeAll();
    for (int i = 0; i < 90 * 10; ++i) {
        b.advance(0.1);
        h.take(b);
    }
    int positional = 0;
    for (const VoiceCue& c : h.cues)
        if (c.situation != Situation::Selected && c.situation != Situation::Charge && c.situation != Situation::Victory &&
            c.situation != Situation::Defeat && c.situation != Situation::HeroFell)
            ++positional;
    CHECK(h.has(Situation::Selected));
    CHECK(positional == 0);
}
