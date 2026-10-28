// Draws the soldiers' sprite frames in software, the way the renderer bakes
// them on the GPU (same views, lighting and dark outline), to look at the
// figures without a browser:
//   illyria_spritesheet out.ppm          one row per type: the five views, the
//                                        walk, the attack, the flinch, the death
//   illyria_spritesheet out.ppm <type>   that type's whole sheet: five rows of
//                                        all 26 frames

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "figures.h"

using namespace illyria;
using namespace illyria::game;
using figures::Anim;

namespace {

struct Image {
    int w = 0, h = 0;
    std::vector<float> rgb;
    void init(int width, int height, V3 bg) {
        w = width, h = height;
        rgb.assign(static_cast<size_t>(w) * h * 3, 0);
        for (size_t i = 0; i < rgb.size(); i += 3) rgb[i] = bg.x, rgb[i + 1] = bg.y, rgb[i + 2] = bg.z;
    }
    void put(int x, int y, V3 c, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        float* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
        p[0] = p[0] * (1 - a) + c.x * a, p[1] = p[1] * (1 - a) + c.y * a, p[2] = p[2] * (1 - a) + c.z * a;
    }
    void save(const char* path) const {
        FILE* f = std::fopen(path, "wb");
        if (!f) return;
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (float v : rgb) std::fputc(static_cast<int>(std::clamp(std::pow(std::clamp(v, 0.0f, 1.0f), 1 / 2.2f) * 255.0f + 0.5f, 0.0f, 255.0f)), f);
        std::fclose(f);
    }
};

// One frame, rasterised at twice its size and resolved down.
struct Cell {
    int w, h;
    std::vector<float> col, alpha;  // w x h, row 0 at the top
};

V3 shade(V3 n, V3 c, float z, const figures::View& v) {
    if (c.x > 1.5f) return c - V3{2, 2, 2};
    const V3 key = (v.toward * 0.45f + v.up * 0.75f - v.right * 0.55f).normalized();
    const float diffuse = std::max(0.0f, n.dot(key));
    const float rim = std::pow(std::max(0.0f, 1.0f - std::fabs(n.dot(v.toward))), 3.0f) * 0.18f;
    const float ao = 0.68f + 0.32f * std::clamp(z / 1.2f, 0.0f, 1.0f);
    return c * ((0.47f + 0.6f * diffuse + rim) * ao);
}

Cell render(const figures::Plan& plan, int viewIndex, int frame, float scale, V3 cloth) {
    const std::vector<float>& m = plan.frames[frame];
    const figures::Extent e = plan.extents[viewIndex][frame];
    const figures::View v = figures::view(viewIndex);
    const int nw = std::max(1, static_cast<int>(std::ceil((e.u1 - e.u0) * scale)));
    const int nh = std::max(1, static_cast<int>(std::ceil((e.v1 - e.v0) * scale)));
    const int NW = nw * 2, NH = nh * 2;
    std::vector<float> depth(static_cast<size_t>(NW) * NH, -1e9f), col(static_cast<size_t>(NW) * NH * 3, 0);
    std::vector<uint8_t> hit(static_cast<size_t>(NW) * NH, 0);
    const auto px = [&](V3 p, float& x, float& y, float& d) {
        x = (p.dot(v.right) - e.u0) / (e.u1 - e.u0) * NW;
        y = (1.0f - (p.dot(v.up) - e.v0) / (e.v1 - e.v0)) * NH;
        d = p.dot(v.toward);
    };
    for (size_t t = 0; t + 3 * figures::kVertexFloats - 1 < m.size(); t += 3 * figures::kVertexFloats) {
        float xs[3], ys[3], ds[3];
        for (int k = 0; k < 3; ++k) {
            const float* q = &m[t + k * figures::kVertexFloats];
            px({q[0], q[1], q[2]}, xs[k], ys[k], ds[k]);
        }
        const float area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0]);
        if (std::fabs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({xs[0], xs[1], xs[2]}))));
        const int x1 = std::min(NW - 1, static_cast<int>(std::ceil(std::max({xs[0], xs[1], xs[2]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ys[0], ys[1], ys[2]}))));
        const int y1 = std::min(NH - 1, static_cast<int>(std::ceil(std::max({ys[0], ys[1], ys[2]}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float sx = x + 0.5f, sy = y + 0.5f;
                const float w0 = ((xs[1] - sx) * (ys[2] - sy) - (xs[2] - sx) * (ys[1] - sy)) / area;
                const float w1 = ((xs[2] - sx) * (ys[0] - sy) - (xs[0] - sx) * (ys[2] - sy)) / area;
                const float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float d = ds[0] * w0 + ds[1] * w1 + ds[2] * w2;
                const size_t i = static_cast<size_t>(y) * NW + x;
                if (d <= depth[i]) continue;
                depth[i] = d;
                hit[i] = 1;
                V3 nrm{}, c{};
                float z = 0, team = 0;
                for (int k = 0; k < 3; ++k) {
                    const float* q = &m[t + k * figures::kVertexFloats];
                    const float w = k == 0 ? w0 : (k == 1 ? w1 : w2);
                    nrm += V3{q[3], q[4], q[5]} * w;
                    c += V3{q[6], q[7], q[8]} * w;
                    z += q[2] * w;
                    team += q[9] * w;
                }
                V3 sh = shade(nrm.normalized(), c, z, v);
                if (team > 0.5f) sh = {sh.x * cloth.x, sh.y * cloth.y, sh.z * cloth.z};
                col[i * 3] = sh.x, col[i * 3 + 1] = sh.y, col[i * 3 + 2] = sh.z;
            }
    }
    Cell out{nw, nh, std::vector<float>(static_cast<size_t>(nw) * nh * 3, 0), std::vector<float>(static_cast<size_t>(nw) * nh, 0)};
    std::vector<float> cover(static_cast<size_t>(nw) * nh, 0);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            int k = 0;
            V3 c{};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const size_t s = static_cast<size_t>(y * 2 + j) * NW + (x * 2 + i);
                    if (!hit[s]) continue;
                    ++k;
                    c += V3{col[s * 3], col[s * 3 + 1], col[s * 3 + 2]};
                }
            const size_t o = static_cast<size_t>(y) * nw + x;
            cover[o] = k / 4.0f;
            if (k) c = c * (1.0f / k);
            out.col[o * 3] = c.x, out.col[o * 3 + 1] = c.y, out.col[o * 3 + 2] = c.z;
        }
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            const size_t o = static_cast<size_t>(y) * nw + x;
            const auto solid = [&](int i, int j) { return i >= 0 && j >= 0 && i < nw && j < nh && cover[static_cast<size_t>(j) * nw + i] >= 0.5f; };
            const bool edge = !solid(x - 1, y) || !solid(x + 1, y) || !solid(x, y - 1) || !solid(x, y + 1);
            if (cover[o] >= 0.5f) {
                out.alpha[o] = 1;
                if (edge) for (int c = 0; c < 3; ++c) out.col[o * 3 + c] *= 0.78f;
            } else if (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1)) {
                out.alpha[o] = 0.85f;
                out.col[o * 3] = 0.05f, out.col[o * 3 + 1] = 0.045f, out.col[o * 3 + 2] = 0.04f;
            }
        }
    return out;
}

void blit(Image& img, const Cell& c, int ox, int oy) {
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const size_t o = static_cast<size_t>(y) * c.w + x;
            if (c.alpha[o] > 0) img.put(ox + x, oy + y, {c.col[o * 3], c.col[o * 3 + 1], c.col[o * 3 + 2]}, c.alpha[o]);
        }
}

// Draws a frame with its feet at (fx, fy).
void place(Image& img, const figures::Plan& plan, int v, int frame, float scale, V3 cloth, int fx, int fy) {
    const figures::Extent e = plan.extents[v][frame];
    blit(img, render(plan, v, frame, scale, cloth), fx + static_cast<int>(std::floor(e.u0 * scale)), fy - static_cast<int>(std::ceil(e.v1 * scale)));
}

float largest(const figures::Plan& plan) {
    float m = 0.1f;
    for (int v = 0; v < figures::kViews; ++v)
        for (int f = 0; f < figures::kFramesPerView; ++f) {
            const figures::Extent& e = plan.extents[v][f];
            m = std::max({m, e.u1 - e.u0, e.v1 - e.v0, -e.u0 * 2, e.u1 * 2});
        }
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.ppm [type [zoom]]\n", argv[0]);
        return 1;
    }
    const V3 bg{0.2f, 0.26f, 0.15f};
    const float blue[3] = {0.22f, 0.42f, 0.92f};
    const V3 cloth = figures::cloth(blue);
    Image img;
    if (argc > 2) {
        const auto type = static_cast<sim::UnitType>(std::atoi(argv[2]));
        const figures::Plan plan = figures::plan(type);
        const float big = largest(plan);
        const float zoom = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 2.0f;
        const float scale = std::min(figures::kPixelsPerMetre, figures::kMaxPixels / big) * zoom;
        const int n = static_cast<int>(big * scale) + 4;
        img.init(n * figures::kFramesPerView, n * figures::kViews, bg);
        for (int v = 0; v < figures::kViews; ++v)
            for (int f = 0; f < figures::kFramesPerView; ++f) place(img, plan, v, f, scale, cloth, n * f + n / 2, n * v + n * 9 / 10);
        std::printf("%s: %.1f px/m in game\n", sim::unitTypeName(type), scale / zoom);
    } else {
        const int n = 120, types = static_cast<int>(sim::UnitType::Count);
        img.init(n * 27, n * types, bg);
        for (int t = 0; t < types; ++t) {
            const auto type = static_cast<sim::UnitType>(t);
            const figures::Plan plan = figures::plan(type);
            const float scale = std::min(40.0f, n * 0.95f / largest(plan));
            int col = 0;
            const auto put = [&](int v, int frame) { place(img, plan, v, frame, scale, cloth, n * col++ + n / 2, n * t + n * 9 / 10); };
            for (int v = 0; v < 5; ++v) put(v, 0);
            for (int f = 0; f < 8; ++f) put(2, figures::kFirstFrame[1] + f);
            for (int f = 0; f < 6; ++f) put(1, figures::kFirstFrame[2] + f);
            for (int f = 0; f < 2; ++f) put(1, figures::kFirstFrame[3] + f);
            for (int f = 0; f < 6; ++f) put(1, figures::kFirstFrame[4] + f);
            // What its sheet costs: every cell at the in-game scale.
            const float s = std::min(figures::kPixelsPerMetre, figures::kMaxPixels / largest(plan));
            double bytes = 0;
            for (int v = 0; v < figures::kViews; ++v)
                for (int f = 0; f < figures::kFramesPerView; ++f) {
                    const figures::Extent& e = plan.extents[v][f];
                    bytes += (std::ceil((e.u1 - e.u0) * s) + 4) * (std::ceil((e.v1 - e.v0) * s) + 4) * 4;
                }
            std::printf("%-20s %5.1f px/m  sheet %.1f MB\n", sim::unitTypeName(type), s, bytes / 1e6);
        }
    }
    img.save(argv[1]);
    return 0;
}
