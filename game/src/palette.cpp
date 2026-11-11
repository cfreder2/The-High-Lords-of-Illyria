// The side palette (game/include/illyria/game/palette.h).
#include "illyria/game/palette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace illyria::game::palette {

namespace {

// In sim::Continent::factions() order, then four more apart from those and from the ground.
const Swatch kTable[kSwatches] = {
    {"White", {0.95f, 0.95f, 0.92f}},   // the Aurelian Throne
    {"Gold", {0.92f, 0.75f, 0.15f}},    // the Highcrag Giants
    {"Red", {0.80f, 0.15f, 0.12f}},     // the Ironhold Dwarves
    {"Orange", {0.92f, 0.48f, 0.12f}},  // the Orcs of Grath
    {"Green", {0.16f, 0.60f, 0.24f}},   // the Sylvaran Elves
    {"Brown", {0.60f, 0.40f, 0.24f}},   // the Sunreach Riders
    {"Blue", {0.20f, 0.36f, 0.82f}},    // the Moonward Order
    {"Black", {0.10f, 0.10f, 0.10f}},   // Lord Malvoren
    {"Purple", {0.50f, 0.22f, 0.70f}},
    {"Pink", {0.96f, 0.50f, 0.72f}},
    {"Teal", {0.10f, 0.62f, 0.62f}},
    {"Maroon", {0.45f, 0.10f, 0.20f}},
};

// The ground and water the banners are seen on: grass, dry grass, soil, shallows, rock, deep water.
const V3 kLand[6] = {{0.34f, 0.47f, 0.22f}, {0.55f, 0.50f, 0.30f}, {0.40f, 0.30f, 0.20f}, {0.15f, 0.35f, 0.55f}, {0.50f, 0.48f, 0.45f}, {0.05f, 0.18f, 0.35f}};

struct State {
    bool chosen = false;
    V3 wanted;
    uint8_t bound = 6;  // the Moonward blue: a skirmish's first seat
    V3 table[kFactions];
    State() { rebuild(); }
    void rebuild();
};

State& state() {
    static State s;
    return s;
}

float lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
struct Lab {
    float l, a, b;
};
Lab toLab(const V3& c) {
    const float r = lin(c.x), g = lin(c.y), b = lin(c.z);
    const float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f, y = 0.2126f * r + 0.7152f * g + 0.0722f * b,
                z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    const auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    return {116.0f * f(y) - 16.0f, 500.0f * (f(x) - f(y)), 200.0f * (f(y) - f(z))};
}

bool sameColour(const V3& a, const V3& b) { return std::fabs(a.x - b.x) < 0.005f && std::fabs(a.y - b.y) < 0.005f && std::fabs(a.z - b.z) < 0.005f; }

V3 clamp01(V3 c) { return {std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f), std::clamp(c.z, 0.0f, 1.0f)}; }

}  // namespace

const Swatch& swatch(int index) { return kTable[std::clamp(index, 0, kSwatches - 1)]; }

float distance(const V3& a, const V3& b) {
    const Lab p = toLab(a), q = toLab(b);
    return std::sqrt((p.l - q.l) * (p.l - q.l) + (p.a - q.a) * (p.a - q.a) + (p.b - q.b) * (p.b - q.b));
}

V3 factionBase(uint8_t f) { return f < kFactions ? kTable[f].rgb : V3{0.6f, 0.6f, 0.6f}; }

int land(V3* out, int cap) {
    const int n = std::min(cap, 6);
    for (int i = 0; i < n; ++i) out[i] = kLand[i];
    return n;
}

V3 snap(const V3& in, uint8_t f) {
    const V3 rgb = clamp01(in);
    for (const Swatch& s : kTable)
        if (sameColour(rgb, s.rgb)) return s.rgb;
    // A colour of the player's own: apart from every other lord and from the ground, or the nearest swatch.
    bool apart = true;
    for (int g = 0; g < kFactions; ++g)
        if (g != f && distance(rgb, kTable[g].rgb) < kApart) apart = false;
    for (const V3& l : kLand)
        if (distance(rgb, l) < kApartLand) apart = false;
    if (apart) return rgb;
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < kSwatches; ++i) {
        const float d = distance(rgb, kTable[i].rgb);
        if (d < bestD) bestD = d, best = i;
    }
    return kTable[best].rgb;
}

void State::rebuild() {
    for (int f = 0; f < kFactions; ++f) table[f] = kTable[f].rgb;
    if (!chosen || bound >= kFactions) return;
    const V3 mine = snap(wanted, bound), gone = table[bound];
    // Whoever wore this colour takes the one the player gave up (a custom colour is apart from every lord's, so nobody).
    for (int g = 0; g < kFactions; ++g)
        if (g != bound && distance(table[g], mine) < kApart) table[g] = gone;
    table[bound] = mine;
}

void choose(const V3& rgb) {
    State& s = state();
    s.chosen = true;
    s.wanted = clamp01(rgb);
    s.rebuild();
}
void clear() {
    State& s = state();
    s.chosen = false;
    s.rebuild();
}
bool chosen() { return state().chosen; }
V3 wanted() { return state().wanted; }

void bind(uint8_t f) {
    State& s = state();
    if (f >= kFactions || f == s.bound) return;
    s.bound = f;
    s.rebuild();
}
uint8_t bound() { return state().bound; }

V3 faction(uint8_t f) { return f < kFactions ? state().table[f] : V3{0.6f, 0.6f, 0.6f}; }

std::string hex(const V3& c) {
    // (truncated, as the minimap and the HUD have always written colours)
    const auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255); };
    char h[8];
    std::snprintf(h, sizeof h, "#%02x%02x%02x", b(c.x), b(c.y), b(c.z));
    return h;
}

bool parseHex(const char* text, V3& out) {
    if (!text || text[0] != '#' || std::strlen(text) != 7) return false;
    for (int i = 1; i < 7; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    const unsigned v = static_cast<unsigned>(std::strtoul(text + 1, nullptr, 16));
    out = {static_cast<float>((v >> 16) & 255) / 255.0f, static_cast<float>((v >> 8) & 255) / 255.0f, static_cast<float>(v & 255) / 255.0f};
    return true;
}

}  // namespace illyria::game::palette
