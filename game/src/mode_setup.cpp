// A match from the page's options (mode.h makeMatch): the place, the seats,
// the difficulty and the army, read from a small JSON object.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "illyria/game/battle.h"
#include "illyria/game/mode_ctf.h"
#include "illyria/game/mode_horde.h"

namespace illyria::game {

namespace {

// The text after "key": in a flat JSON object, or null.
const char* valueOf(const std::string& json, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t at = json.find(k);
    if (at == std::string::npos) return nullptr;
    const size_t colon = json.find(':', at + k.size());
    if (colon == std::string::npos) return nullptr;
    const char* p = json.c_str() + colon + 1;
    while (*p == ' ') ++p;
    return p;
}

int intOf(const std::string& json, const char* key, int fallback) {
    const char* p = valueOf(json, key);
    return p && (*p == '-' || (*p >= '0' && *p <= '9')) ? std::atoi(p) : fallback;
}

// The items of an array value, as raw text (numbers, or strings without their quotes).
std::vector<std::string> listOf(const std::string& json, const char* key) {
    std::vector<std::string> out;
    const char* p = valueOf(json, key);
    if (!p || *p != '[') return out;
    std::string item;
    for (++p; *p && *p != ']'; ++p) {
        if (*p == ',') {
            out.push_back(item);
            item.clear();
        } else if (*p != '"' && *p != ' ') {
            item += *p;
        }
    }
    if (!item.empty()) out.push_back(item);
    return out;
}

sim::SeatControl controlOf(const std::string& s) {
    if (s == "human") return sim::SeatControl::Human;
    if (s == "computer" || s == "ai") return sim::SeatControl::Computer;
    return sim::SeatControl::Empty;
}

}  // namespace

std::string placesJson(const std::vector<Place>& places) {
    std::string s = "[";
    char buf[256];
    for (size_t i = 0; i < places.size(); ++i) {
        const Place& p = places[i];
        std::snprintf(buf, sizeof buf, "%s{\"i\":%zu,\"name\":\"%s\",\"kind\":\"%s\",\"recommended\":%s,\"x\":%.0f,\"y\":%.0f}", i ? "," : "", i,
                      p.name.c_str(), placeKindName(p.kind), p.recommended ? "true" : "false", p.pos.x.toDouble(), p.pos.y.toDouble());
        s += buf;
    }
    return s + "]";
}

std::unique_ptr<Battle> makeMatch(const std::string& id, const std::string& options, const std::vector<Place>& places,
                                  std::shared_ptr<const sim::Continent> continent) {
    const int place = intOf(options, "place", -1);
    if (places.empty() || place < 0 || place >= static_cast<int>(places.size())) return nullptr;
    const Place& where = places[static_cast<size_t>(place)];
    const uint64_t seed = static_cast<uint64_t>(intOf(options, "seed", 20261029));
    const std::vector<std::string> seats = listOf(options, "seats");
    if (id == "horde") {
        auto b = std::make_unique<Battle>(seed, where.pos, std::move(continent));
        HordeOptions o;
        o.centre = where.pos;
        o.place = where.name;
        o.seed = seed;
        o.difficulty = std::clamp(intOf(options, "difficulty", 1), 0, 2);
        o.waves = std::max(0, intOf(options, "waves", o.waves));
        o.breakSeconds = std::clamp(intOf(options, "breakSeconds", o.breakSeconds), 5, 300);
        o.musterSeconds = std::clamp(intOf(options, "musterSeconds", o.musterSeconds), 3, 300);
        for (size_t k = 0; k < seats.size() && k < o.seats.size(); ++k) o.seats[k] = controlOf(seats[k]);
        HordeMode::start(*b, o);
        return b;
    }
    if (id == "ctf") {
        auto b = std::make_unique<Battle>(seed, where.pos, std::move(continent));
        CtfOptions o;
        o.centre = where.pos;
        o.place = where.name;
        o.seed = seed;
        o.teams = std::clamp(intOf(options, "teams", 2), 2, 4);
        o.budget = std::clamp(intOf(options, "budget", o.budget), 8, 120);
        o.timeLimitSeconds = std::max(0, intOf(options, "time", o.timeLimitSeconds));
        const std::vector<std::string> teams = listOf(options, "seatTeams");
        for (size_t k = 0; k < seats.size() && k < o.seats.size(); ++k)
            o.seats[k] = {controlOf(seats[k]), k < teams.size() ? std::atoi(teams[k].c_str()) : static_cast<int>(k)};
        for (const std::string& a : listOf(options, "army")) o.army.push_back(static_cast<uint8_t>(std::atoi(a.c_str())));
        CtfMode::start(*b, o);
        return b;
    }
    return nullptr;
}

}  // namespace illyria::game
