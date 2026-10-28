// Bakes the continent's relief: the procedural design worn down by rain
// (droplet hydraulic erosion, then slope collapse), and its drainage traced
// into streams and pooled hollows. The output is data compiled into the game
// (engine/sim/data/illyria_relief.bin), so every platform has exactly the same
// land; this tool may use floating point because it runs once, offline.
//
//   illyria_terrainbake engine/sim/data/illyria_relief.bin [preview.ppm]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <random>
#include <vector>

#include "illyria/sim/continent.h"

using namespace illyria::sim;

namespace {

constexpr int kCell = 32;  // metres between samples
const int kStreamArea = static_cast<int>(std::getenv("BAKE_AREA") ? std::atoi(std::getenv("BAKE_AREA")) : 400);  // cells draining into a point before a stream rises there
constexpr float kBasinDepth = 1.2f;  // metres of water before a hollow counts as a pool
constexpr int kBasinCells = 4;

struct Grid {
    int nx = 0, ny = 0;
    std::vector<float> h;
    float at(int i, int j) const { return h[static_cast<size_t>(std::clamp(j, 0, ny - 1)) * nx + std::clamp(i, 0, nx - 1)]; }
};

float param(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v ? static_cast<float>(std::atof(v)) : fallback;
}

// Floods every hollow to its spill height from the outlets (water and the
// map's edge) and links each cell to the one it drains into. `order` lists
// cells so that each comes after the cell it drains into.
void drainage(const Grid& g, const std::vector<uint8_t>& water, std::vector<float>& fill, std::vector<int>& down,
              std::vector<int>& order, std::mt19937* jitter = nullptr) {
    const int nx = g.nx, ny = g.ny;
    const size_t n = g.h.size();
    fill.assign(n, 1e9f);
    down.assign(n, -1);
    order.clear();
    order.reserve(n);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    std::vector<uint8_t> done(n);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const size_t c = static_cast<size_t>(j) * nx + i;
            if (water[c] || i == 0 || j == 0 || i == nx - 1 || j == ny - 1) {
                fill[c] = g.h[c];
                done[c] = 1;
                open.push({fill[c], static_cast<int>(c)});
            }
        }
    const int di[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        const auto [level, c] = open.top();
        open.pop();
        order.push_back(c);
        const int i = c % nx, j = c / nx;
        for (int k = 0; k < 8; ++k) {
            const int a = i + di[k], b = j + dj[k];
            if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
            const size_t m = static_cast<size_t>(b) * nx + a;
            if (done[m]) continue;
            done[m] = 1;
            // Drain toward the cell that reached us; flats get a slight tilt.
            fill[m] = std::max(g.h[m], level + 1e-3f);
            down[m] = c;
            open.push({fill[m], static_cast<int>(m)});
        }
    }
    // Then each cell drains the steepest way down the filled surface (cells
    // lower than it were all ordered before it), not just toward whichever
    // neighbour the flood came from, which would leave every valley at 45 degrees.
    for (size_t c = 0; c < n; ++c) {
        if (down[c] < 0) continue;
        const int i = static_cast<int>(c % nx), j = static_cast<int>(c / nx);
        // While carving, a random pick weighted by slope: over many rounds the
        // water spreads in its true direction rather than along the grid.
        float best = 0, total = 0;
        float drops[8];
        for (int k = 0; k < 8; ++k) {
            drops[k] = 0;
            const int a = i + di[k], b = j + dj[k];
            if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
            const size_t m = static_cast<size_t>(b) * nx + a;
            const float drop = (fill[c] - fill[m]) / (k < 4 ? 1.0f : 1.41421f);
            if (drop <= 0) continue;
            drops[k] = drop * drop;
            total += drops[k];
            if (drop > best) best = drop, down[c] = static_cast<int>(m);
        }
        if (jitter && total > 0) {
            float pick = std::uniform_real_distribution<float>(0, total)(*jitter);
            for (int k = 0; k < 8; ++k) {
                pick -= drops[k];
                if (drops[k] > 0 && pick <= 0) {
                    down[c] = (j + dj[k]) * nx + i + di[k];
                    break;
                }
            }
        }
    }
}

void accumulate(const std::vector<uint8_t>& water, const std::vector<int>& down, const std::vector<int>& order,
                std::vector<float>& area) {
    area.assign(down.size(), 1.0f);
    for (size_t k = order.size(); k-- > 0;) {
        const int c = order[k];
        if (down[c] >= 0 && !water[c]) area[down[c]] += area[c];
    }
}

// Rivers cut down in proportion to the water through them and the slope
// (the stream power law), solved implicitly from the outlets upstream after
// Braun and Willett (2013). Branching valleys and sharp spurs come of it;
// hillslope creep then rounds what the streams leave.
void incise(Grid& g, const std::vector<uint8_t>& water, const std::vector<float>& hardness) {
    const int steps = static_cast<int>(param("BAKE_STEPS", 60));
    const float kTotal = param("BAKE_K", 3.0f), mExp = param("BAKE_M", 0.5f), creep = param("BAKE_CREEP", 0.01f);
    const float uplift = param("BAKE_UPLIFT", 0.0f);
    const std::vector<float> design = g.h;
    std::vector<float> fill, area;
    std::vector<int> down, order;
    std::mt19937 jitter(7);
    const float channel = param("BAKE_CHANNEL", 12);  // cells draining before water cuts a channel
    for (int step = 0; step < steps; ++step) {
        drainage(g, water, fill, down, order, &jitter);
        accumulate(water, down, order, area);
        const float k = kTotal / steps;
        for (int c : order) {
            const int r = down[c];
            if (r < 0 || water[c]) continue;
            if (g.h[c] <= g.h[r]) continue;  // a hollow: water ponds rather than cuts
            const int di = c % g.nx - r % g.nx, dj = c / g.nx - r / g.nx;
            const float dist = (di != 0 && dj != 0 ? 1.41421f : 1.0f) * kCell;
            const float channelled = std::min(1.0f, area[c] / channel);
            const float f = k * hardness[c] * channelled * channelled * std::pow(area[c] * kCell * kCell, mExp) / dist;
            const float rise = uplift / steps * std::max(0.0f, design[c]);
            g.h[c] = (g.h[c] + rise + f * g.h[r]) / (1 + f);
        }
        // Creep: a little diffusion rounds crests and fills the stair steps.
        std::vector<float> next = g.h;
        for (int j = 1; j + 1 < g.ny; ++j)
            for (int i = 1; i + 1 < g.nx; ++i) {
                const size_t c = static_cast<size_t>(j) * g.nx + i;
                if (water[c]) continue;
                const float lap = g.h[c - 1] + g.h[c + 1] + g.h[c - g.nx] + g.h[c + g.nx] - 4 * g.h[c];
                next[c] = g.h[c] + creep * lap;
            }
        g.h.swap(next);
    }
}

void slump(Grid& g, const std::vector<uint8_t>& water) {
    // Slopes steeper than the talus angle collapse onto their neighbours.
    const float talus = std::tan(38.0f * 3.14159265f / 180.0f) * kCell;
    for (int pass = 0; pass < 12; ++pass) {
        for (int j = 1; j + 1 < g.ny; ++j) {
            for (int i = 1; i + 1 < g.nx; ++i) {
                const size_t c = static_cast<size_t>(j) * g.nx + i;
                if (water[c]) continue;
                const int nb[4] = {1, -1, g.nx, -g.nx};
                for (int k : nb) {
                    const size_t n = static_cast<size_t>(static_cast<long>(c) + k);
                    const float diff = g.h[c] - g.h[n];
                    if (diff > talus) {
                        const float move = (diff - talus) * 0.25f;
                        g.h[c] -= move;
                        g.h[n] += move;
                    }
                }
            }
        }
    }
}

struct Stream {
    std::vector<float> x, y, surface, half;
};

// Writes little-endian values.
struct Writer {
    std::vector<unsigned char> bytes;
    void u32(uint32_t v) {
        for (int k = 0; k < 4; ++k) bytes.push_back(static_cast<unsigned char>(v >> (8 * k)));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void i16(int v) {
        const uint16_t u = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(v, -32768, 32767)));
        bytes.push_back(static_cast<unsigned char>(u));
        bytes.push_back(static_cast<unsigned char>(u >> 8));
    }
};

double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: illyria_terrainbake out.bin [preview.ppm]\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const Continent design(20261027, Continent::Relief::Design);
    const int nx = static_cast<int>(Continent::kWidth / kCell) + 1, ny = static_cast<int>(Continent::kHeight / kCell) + 1;
    const size_t n = static_cast<size_t>(nx) * ny;

    // The design as the player would see it (valleys, lakes, pads) is what the
    // rain falls on; the bake keeps only how much the rain changed it, laid
    // over the uncarved landform, so the game carves rivers afresh on top.
    Grid g;
    g.nx = nx;
    g.ny = ny;
    g.h.resize(n);
    std::vector<float> carved(n), soft(n);
    std::vector<uint8_t> water(n);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const Fixed x = Fixed::fromInt(i * kCell), y = Fixed::fromInt(j * kCell);
            const size_t c = static_cast<size_t>(j) * nx + i;
            Continent::Sample s = design.landform(x, y);
            // Mountains are cut deepest; the farmland plains mostly keep their shape.
            const float mountain = static_cast<float>(s.mountain.toDouble());
            soft[c] = param("BAKE_PLAIN", 0.2f) + (1 - param("BAKE_PLAIN", 0.2f)) * std::clamp(mountain / 120.0f, 0.0f, 1.0f);
            design.carve(s, x, y);
            carved[c] = static_cast<float>(s.height.toDouble());
            Fixed surface;
            water[c] = design.waterSurfaceOf(s, surface) || s.land < Fixed::zero() ? 1 : 0;
        }
    }
    g.h = carved;
    std::fprintf(stderr, "sampled %dx%d in %.1fs\n", nx, ny, seconds(t0));

    incise(g, water, soft);
    std::fprintf(stderr, "eroded in %.1fs\n", seconds(t0));
    slump(g, water);

    // What erosion did to the land, softened so no valley is narrower than a
    // few samples (the game adds it to the full-detail design).
    std::vector<float> delta(n);
    for (size_t c = 0; c < n; ++c) delta[c] = g.h[c] - carved[c];
    for (int pass = 0; pass < static_cast<int>(param("BAKE_SOFTEN", 2)); ++pass) {
        std::vector<float> next = delta;
        for (int j = 1; j + 1 < ny; ++j)
            for (int i = 1; i + 1 < nx; ++i) {
                const size_t c = static_cast<size_t>(j) * nx + i;
                next[c] = (4 * delta[c] + 2 * (delta[c - 1] + delta[c + 1] + delta[c - nx] + delta[c + nx]) + delta[c - nx - 1] +
                           delta[c - nx + 1] + delta[c + nx - 1] + delta[c + nx + 1]) / 16;
            }
        delta.swap(next);
    }
    for (size_t c = 0; c < n; ++c) g.h[c] = carved[c] + delta[c];

    std::vector<float> fill, area;
    std::vector<int> down, order;
    drainage(g, water, fill, down, order);
    accumulate(water, down, order, area);

    // Pools: hollows deep enough to hold water, kept when they cover a few cells.
    std::vector<int16_t> basin(n, INT16_MIN);
    int pools = 0;
    {
        std::vector<uint8_t> seen(n);
        for (size_t c0 = 0; c0 < n; ++c0) {
            if (seen[c0] || water[c0] || fill[c0] - g.h[c0] < kBasinDepth) continue;
            std::vector<size_t> group{c0}, stack{c0};
            seen[c0] = 1;
            while (!stack.empty()) {
                const size_t c = stack.back();
                stack.pop_back();
                const int i = static_cast<int>(c % nx), j = static_cast<int>(c / nx);
                const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int a = i + di[k], b = j + dj[k];
                    if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                    const size_t m = static_cast<size_t>(b) * nx + a;
                    if (seen[m] || water[m] || fill[m] - g.h[m] < 0.3f) continue;
                    seen[m] = 1;
                    group.push_back(m);
                    stack.push_back(m);
                }
            }
            if (static_cast<int>(group.size()) < kBasinCells) continue;
            ++pools;
            for (size_t c : group) basin[c] = static_cast<int16_t>(std::lround((fill[c] - 0.3f) * 10));
        }
        // The level spreads a little past the flooded samples, so the shore
        // is wherever the land rises through it, not the edge of a 32 m cell.
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<int16_t> grown = basin;
            for (int j = 1; j + 1 < ny; ++j)
                for (int i = 1; i + 1 < nx; ++i) {
                    const size_t c = static_cast<size_t>(j) * nx + i;
                    if (basin[c] != INT16_MIN || water[c]) continue;
                    for (const size_t m : {c - 1, c + 1, c - nx, c + nx})
                        if (basin[m] != INT16_MIN) grown[c] = std::max(grown[c], basin[m]);
                }
            basin.swap(grown);
        }
    }

    // Streams: wherever enough land drains through, a creek runs down to the
    // next stream, a river, a lake or the sea.
    std::vector<Stream> streams;
    {
        std::vector<uint8_t> isStream(n), hasUp(n), traced(n);
        for (size_t c = 0; c < n; ++c) isStream[c] = !water[c] && area[c] >= kStreamArea && basin[c] == INT16_MIN;
        for (size_t c = 0; c < n; ++c)
            if (isStream[c] && down[c] >= 0 && isStream[down[c]]) hasUp[down[c]] = 1;
        // Trace longest first so main stems are continuous and tributaries join them.
        std::vector<int> heads;
        for (size_t c = 0; c < n; ++c)
            if (isStream[c] && !hasUp[c]) heads.push_back(static_cast<int>(c));
        std::sort(heads.begin(), heads.end(), [&](int a, int b) { return fill[a] > fill[b]; });
        for (int head : heads) {
            Stream st;
            int c = head;
            float surface = 1e9f;
            while (c >= 0) {
                const float x = static_cast<float>((c % nx) * kCell), y = static_cast<float>((c / nx) * kCell);
                surface = std::min(surface, fill[c] - 0.4f);
                st.x.push_back(x);
                st.y.push_back(y);
                st.surface.push_back(surface);
                st.half.push_back(std::min(6.0f, 0.8f + 0.25f * std::sqrt(area[c] / kStreamArea)));
                if (traced[c] || water[c] || basin[c] != INT16_MIN) break;  // joined something
                traced[c] = 1;
                c = down[c];
            }
            if (st.x.size() < 4) continue;
            // Averaging takes out the grid's zigzag (the ends stay put, so
            // streams still meet what they flow into), then corner cutting rounds it.
            for (int round = 0; round < 4; ++round) {
                std::vector<float> xs = st.x, ys = st.y;
                for (size_t k = 1; k + 1 < st.x.size(); ++k) {
                    xs[k] = (st.x[k - 1] + 2 * st.x[k] + st.x[k + 1]) / 4;
                    ys[k] = (st.y[k - 1] + 2 * st.y[k] + st.y[k + 1]) / 4;
                }
                st.x.swap(xs);
                st.y.swap(ys);
            }
            for (int round = 0; round < 2; ++round) {
                Stream sm;
                const auto push = [&](size_t a, size_t b, float t) {
                    sm.x.push_back(st.x[a] + (st.x[b] - st.x[a]) * t);
                    sm.y.push_back(st.y[a] + (st.y[b] - st.y[a]) * t);
                    sm.surface.push_back(st.surface[a] + (st.surface[b] - st.surface[a]) * t);
                    sm.half.push_back(st.half[a] + (st.half[b] - st.half[a]) * t);
                };
                push(0, 0, 0);
                for (size_t k = 0; k + 1 < st.x.size(); ++k) {
                    push(k, k + 1, 0.25f);
                    push(k, k + 1, 0.75f);
                }
                push(st.x.size() - 1, st.x.size() - 1, 0);
                st = std::move(sm);
            }
            streams.push_back(std::move(st));
        }
    }
    size_t points = 0;
    for (const Stream& st : streams) points += st.x.size();
    std::fprintf(stderr, "%zu streams (%zu points), %d pools, %.1fs\n", streams.size(), points, pools, seconds(t0));

    Writer w;
    for (char ch : {'I', 'L', 'R', 'F'}) w.bytes.push_back(static_cast<unsigned char>(ch));
    w.u32(3);
    w.i32(nx);
    w.i32(ny);
    w.i32(kCell);
    for (size_t c = 0; c < n; ++c) w.i16(static_cast<int>(std::lround(delta[c] * 10)));
    for (size_t c = 0; c < n; ++c) w.i16(basin[c]);
    w.u32(static_cast<uint32_t>(streams.size()));
    for (const Stream& st : streams) {
        w.u32(static_cast<uint32_t>(st.x.size()));
        for (size_t k = 0; k < st.x.size(); ++k) {
            w.i32(static_cast<int32_t>(std::lround(st.x[k] * 10)));
            w.i32(static_cast<int32_t>(std::lround(st.y[k] * 10)));
            w.i16(static_cast<int>(std::lround(st.surface[k] * 10)));
            w.i16(static_cast<int>(std::lround(st.half[k] * 100)));
        }
    }
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) return 1;
    std::fwrite(w.bytes.data(), 1, w.bytes.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "wrote %s (%zu KB)\n", argv[1], w.bytes.size() / 1024);

    if (argc > 2) {
        // Hillshaded preview: water blue, pools cyan, streams dark blue.
        FILE* p = std::fopen(argv[2], "wb");
        if (!p) return 1;
        std::fprintf(p, "P6\n%d %d\n255\n", nx, ny);
        std::vector<unsigned char> px(n * 3);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const size_t c = static_cast<size_t>(j) * nx + i;
                const float gx = (g.at(i + 1, j) - g.at(i - 1, j)) / (2 * kCell), gy = (g.at(i, j + 1) - g.at(i, j - 1)) / (2 * kCell);
                const float shade = std::clamp(0.6f + (gx - gy) * 1.5f, 0.0f, 1.0f);
                const float e = std::clamp(g.h[c] / 500.0f, 0.0f, 1.0f);
                float r = (0.35f + 0.5f * e) * shade, gr = (0.55f - 0.1f * e) * shade, b = (0.3f + 0.4f * e) * shade;
                if (water[c]) r = 0.1f, gr = 0.25f, b = 0.5f;
                else if (basin[c] != INT16_MIN) r = 0.2f, gr = 0.6f, b = 0.8f;
                const size_t o = (static_cast<size_t>(ny - 1 - j) * nx + i) * 3;
                px[o] = static_cast<unsigned char>(r * 255), px[o + 1] = static_cast<unsigned char>(gr * 255), px[o + 2] = static_cast<unsigned char>(b * 255);
            }
        for (const Stream& st : streams)
            for (size_t k = 0; k < st.x.size(); ++k) {
                const int i = static_cast<int>(st.x[k] / kCell), j = static_cast<int>(st.y[k] / kCell);
                const size_t o = (static_cast<size_t>(ny - 1 - j) * nx + i) * 3;
                px[o] = 20, px[o + 1] = 60, px[o + 2] = 150;
            }
        std::fwrite(px.data(), 1, px.size(), p);
        std::fclose(p);
    }
    return 0;
}
