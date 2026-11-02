// A match from the page's options (mode.h makeMatch): the place, the seats,
// the difficulty and the army, read from a small JSON object.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "illyria/game/battle.h"
#include "illyria/game/mode_ctf.h"
#include "illyria/game/mode_horde.h"
#include "illyria/game/mode_sandbox.h"

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

// A field anywhere on the map (the sandbox's, picked on its setup map): the
// dry ground nearest (x, y) in metres, a field's width inside the map's edge
// (a click in a lake or the sea moves out to the shore), named for the place
// it is near if one is close.
Place openGround(const sim::Continent& c, int x, int y, const std::vector<Place>& places) {
    constexpr int kEdge = 300;
    const auto inside = [](int v, int64_t size) { return static_cast<int>(std::clamp<int64_t>(v, kEdge, size - kEdge)); };
    const auto dry = [&](int px, int py) {
        sim::Fixed surface;
        return !c.waterSurface(sim::Fixed::fromInt(px), sim::Fixed::fromInt(py), surface);
    };
    x = inside(x, sim::Continent::kWidth);
    y = inside(y, sim::Continent::kHeight);
    int bx = x, by = y;
    for (int r = 40; !dry(bx, by) && r <= 3000; r += 40)
        for (int k = 0; k < 24; ++k) {
            const double a = k * (2 * 3.14159265358979 / 24);
            const int px = inside(x + static_cast<int>(std::lround(r * std::cos(a))), sim::Continent::kWidth);
            const int py = inside(y + static_cast<int>(std::lround(r * std::sin(a))), sim::Continent::kHeight);
            if (dry(px, py)) {
                bx = px, by = py;
                break;
            }
        }
    Place p;
    p.name = "Open ground";
    p.pos = {sim::Fixed::fromInt(bx), sim::Fixed::fromInt(by), c.heightAt(sim::Fixed::fromInt(bx), sim::Fixed::fromInt(by))};
    double best = 500.0 * 500.0;
    for (const Place& q : places) {
        if (q.name.empty()) continue;
        const double dx = q.pos.x.toDouble() - bx, dy = q.pos.y.toDouble() - by;
        if (dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            p.name = "Near " + q.name;
        }
    }
    return p;
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
    if (id == "sandbox") {
        // Anywhere on the map when the page gives x and y (metres), else the place.
        const int x = intOf(options, "x", -1), y = intOf(options, "y", -1);
        const Place here = x >= 0 && y >= 0 ? openGround(*continent, x, y, places) : where;
        auto b = std::make_unique<Battle>(seed, here.pos, std::move(continent), kMatchFarReachM);
        SandboxOptions o;
        o.centre = here.pos;
        o.place = here.name;
        o.seed = seed;
        o.radiusM = std::clamp(intOf(options, "radius", o.radiusM), 80, 480);
        SandboxMode::start(*b, o);
        return b;
    }
    if (id == "horde") {
        auto b = std::make_unique<Battle>(seed, where.pos, std::move(continent), kMatchFarReachM);
        HordeOptions o;
        o.centre = where.pos;
        o.place = where.name;
        o.seed = seed;
        o.difficulty = std::clamp(intOf(options, "difficulty", 1), 0, 2);
        o.waves = std::max(0, intOf(options, "waves", o.waves));
        o.breakSeconds = std::clamp(intOf(options, "breakSeconds", o.breakSeconds), 5, 300);
        o.musterSeconds = std::clamp(intOf(options, "musterSeconds", o.musterSeconds), 3, 300);
        for (size_t k = 0; k < seats.size() && k < o.seats.size(); ++k) o.seats[k] = controlOf(seats[k]);
        o.localSeat = intOf(options, "local", -1);
        const HordeMode& m = HordeMode::start(*b, o);
        // The field: round the hill, out past where the waves form up.
        b->setZone(toV3(m.hill().at()), static_cast<float>(HordeMode::kSpawnM + 120), kZonePctPerSecond);
        return b;
    }
    if (id == "ctf") {
        auto b = std::make_unique<Battle>(seed, where.pos, std::move(continent), kMatchFarReachM);
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
        const std::vector<std::string> starts = listOf(options, "starts");
        for (size_t t = 0; t < starts.size() && t < o.starts.size(); ++t) o.starts[t] = std::atoi(starts[t].c_str());
        o.localSeat = intOf(options, "local", -1);
        CtfMode::start(*b, o);
        // The field: round the middle, a hundred metres past the flags.
        b->setZone({Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0}, static_cast<float>(o.ringM + 100), kZonePctPerSecond);
        return b;
    }
    return nullptr;
}

}  // namespace illyria::game
