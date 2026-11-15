#include "meshes.h"

#include <algorithm>
#include <cmath>

#include "illyria/game/gmath.h"

namespace illyria::game {

namespace {

constexpr float kPi = 3.14159265f;

struct Builder {
    Mesh mesh;

    void tri(V3 a, V3 b, V3 c, V3 color) {
        V3 n = (b - a).cross(c - a).normalized();
        for (const V3& p : {a, b, c}) mesh.data.insert(mesh.data.end(), {p.x, p.y, p.z, n.x, n.y, n.z, color.x, color.y, color.z});
    }
    void quad(V3 a, V3 b, V3 c, V3 d, V3 color) {
        tri(a, b, c, color);
        tri(a, c, d, color);
    }
    // Axis-aligned box from lo to hi (no bottom face).
    void box(V3 lo, V3 hi, V3 color) {
        const V3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        quad(c[0], c[1], c[5], c[4], color);
        quad(c[1], c[2], c[6], c[5], color * 0.9f);
        quad(c[2], c[3], c[7], c[6], color);
        quad(c[3], c[0], c[4], c[7], color * 0.9f);
        quad(c[4], c[5], c[6], c[7], color * 1.08f);
    }
    // Cone or frustum around z: radius r0 at z0, r1 at z1, n sides.
    void frustum(float z0, float r0, float z1, float r1, int n, V3 color, float twist = 0) {
        for (int i = 0; i < n; ++i) {
            const float a0 = 2 * kPi * i / n + twist, a1 = 2 * kPi * (i + 1) / n + twist;
            const V3 p0{std::cos(a0) * r0, std::sin(a0) * r0, z0}, p1{std::cos(a1) * r0, std::sin(a1) * r0, z0};
            const V3 q0{std::cos(a0) * r1, std::sin(a0) * r1, z1}, q1{std::cos(a1) * r1, std::sin(a1) * r1, z1};
            if (r1 > 0) quad(p0, p1, q1, q0, color);
            else tri(p0, p1, q0, color);
        }
    }
    // A tapered limb from a to b (radius r0 to r1).
    void limb(V3 a, V3 b, float r0, float r1, int n, V3 color) {
        const V3 axis = (b - a).normalized();
        const V3 helper = std::fabs(axis.z) < 0.9f ? V3{0, 0, 1} : V3{1, 0, 0};
        const V3 u = axis.cross(helper).normalized(), v = axis.cross(u);
        for (int i = 0; i < n; ++i) {
            const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            const V3 d0 = u * std::cos(a0) + v * std::sin(a0), d1 = u * std::cos(a1) + v * std::sin(a1);
            quad(a + d0 * r0, a + d1 * r0, b + d1 * r1, b + d0 * r1, color * (0.85f + 0.15f * std::cos(a0 - 0.6f)));
        }
    }
    // A box on axes ax, ay, az (unit) with half sizes h, all six faces.
    void block(V3 c, V3 ax, V3 ay, V3 az, V3 h, V3 color) {
        const V3 X = ax * h.x, Y = ay * h.y, Z = az * h.z;
        const V3 p[8] = {c - X - Y - Z, c + X - Y - Z, c + X + Y - Z, c - X + Y - Z,
                         c - X - Y + Z, c + X - Y + Z, c + X + Y + Z, c - X + Y + Z};
        const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
        const float shade[6] = {0.7f, 1.08f, 0.95f, 0.95f, 0.88f, 0.88f};
        for (int i = 0; i < 6; ++i) quad(p[f[i][0]], p[f[i][1]], p[f[i][2]], p[f[i][3]], color * shade[i]);
    }
    // A quad turned to face `want`, whichever way its corners were given.
    void facing(V3 a, V3 b, V3 c, V3 d, V3 want, V3 color) {
        if ((b - a).cross(c - a).dot(want) < 0) quad(d, c, b, a, color);
        else quad(a, b, c, d, color);
    }
    // A quad whose foot (a, b) and head (c, d) differ in colour: the shade that gathers in a corner, laid in the vertices.
    void quad2(V3 a, V3 b, V3 c, V3 d, V3 low, V3 high) {
        const V3 n = (b - a).cross(c - a).normalized();
        const auto put = [&](V3 p, V3 col) { mesh.data.insert(mesh.data.end(), {p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z}); };
        put(a, low), put(b, low), put(c, high), put(a, low), put(c, high), put(d, high);
    }
    // A fluted shaft around z: `flutes` hollows between sharp arrises, radius r0 at z0 to r1 at z1.
    void fluted(float z0, float r0, float z1, float r1, int flutes, V3 color, float hollow = 0.9f) {
        const int n = flutes * 2;
        for (int i = 0; i < n; ++i) {
            const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            const float k0 = i % 2 ? hollow : 1.0f, k1 = i % 2 ? 1.0f : hollow;
            const V3 p0{std::cos(a0) * r0 * k0, std::sin(a0) * r0 * k0, z0}, p1{std::cos(a1) * r0 * k1, std::sin(a1) * r0 * k1, z0};
            const V3 q0{std::cos(a0) * r1 * k0, std::sin(a0) * r1 * k0, z1}, q1{std::cos(a1) * r1 * k1, std::sin(a1) * r1 * k1, z1};
            quad(p0, p1, q1, q0, color);
        }
    }
    // A flat disc at z, radius r, n sides, facing up (or down).
    void disc(V3 c, float r, int n, V3 color, bool down = false) {
        for (int i = 0; i < n; ++i) {
            const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            const V3 p0 = c + V3{std::cos(a0) * r, std::sin(a0) * r, 0}, p1 = c + V3{std::cos(a1) * r, std::sin(a1) * r, 0};
            if (down) tri(c, p1, p0, color);
            else tri(c, p0, p1, color);
        }
    }
    size_t mark() const { return mesh.data.size(); }
    // What was built since `from`, moved by d.
    void shift(size_t from, V3 d) {
        for (size_t v = from; v < mesh.data.size(); v += 9) mesh.data[v] += d.x, mesh.data[v + 1] += d.y, mesh.data[v + 2] += d.z;
    }
    // What was built since `from` is turned stone (a column's drums): the mesh shader reads green + 2 as that and
    // courses it in drums, not in blocks (shaders.h kMeshVs).
    void turned(size_t from) {
        for (size_t v = from; v < mesh.data.size(); v += 9) mesh.data[v + 7] += 2.0f;
    }
    // A wall's face with openings cut in it: from `origin` along u (unit, flat) for `width` and up for `height`, facing
    // u x up. Each opening (u0, u1, z0, z1 on the face) goes back `depth` between reveals in the wall's own stone,
    // shaded, to a pane of its own colour: a window's dark, a door's planks.
    struct Opening {
        float u0, u1, z0, z1;
        V3 pane;
    };
    void pierced(V3 origin, V3 u, float width, float height, const std::vector<Opening>& openings, float depth, V3 color) {
        const V3 out{u.y, -u.x, 0}, up{0, 0, 1};
        std::vector<float> zs{0, height};
        for (const Opening& o : openings) zs.push_back(o.z0), zs.push_back(o.z1);
        std::sort(zs.begin(), zs.end());
        for (size_t k = 0; k + 1 < zs.size(); ++k) {
            const float z0 = zs[k], z1 = zs[k + 1];
            if (z1 - z0 < 1e-5f) continue;
            std::vector<std::pair<float, float>> gaps;
            for (const Opening& o : openings)
                if (o.z0 <= z0 + 1e-5f && o.z1 >= z1 - 1e-5f) gaps.push_back({o.u0, o.u1});
            std::sort(gaps.begin(), gaps.end());
            float from = 0;
            const auto run = [&](float a, float b2) {
                if (b2 - a > 1e-5f) quad(origin + u * a + up * z0, origin + u * b2 + up * z0, origin + u * b2 + up * z1, origin + u * a + up * z1, color);
            };
            for (const auto& [a, b2] : gaps) run(from, a), from = b2;
            run(from, width);
        }
        for (const Opening& o : openings) {
            const V3 a = origin + u * o.u0 + up * o.z0, b2 = origin + u * o.u1 + up * o.z0, c = origin + u * o.u1 + up * o.z1, d = origin + u * o.u0 + up * o.z1;
            const V3 in = out * -depth;
            const V3 shade = color * 0.62f;
            facing(a, a + in, d + in, d, u, shade);
            facing(b2, b2 + in, c + in, c, u * -1.0f, shade * 0.9f);
            facing(d, d + in, c + in, c, up * -1.0f, shade * 0.7f);
            if (o.z0 > 1e-4f) facing(a, a + in, b2 + in, b2, up, shade * 1.25f);
            quad2(a + in, b2 + in, c + in, d + in, o.pane, o.pane * 0.55f);  // (darker under its head)
        }
    }
    // Faceted blob: an icosahedron, optionally squashed and dented.
    void blob(V3 centre, V3 radii, V3 color, float dent = 0, int seed = 0) {
        const float t = (1.0f + std::sqrt(5.0f)) / 2;
        V3 v[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                    {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        for (int i = 0; i < 12; ++i) {
            const V3 u = v[i].normalized();
            const float wobble = 1.0f - dent * (0.5f + 0.5f * std::sin(static_cast<float>(i * 7 + seed * 13)));
            v[i] = centre + V3{u.x * radii.x, u.y * radii.y, u.z * radii.z} * wobble;
        }
        const int f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                              {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                              {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        for (const auto& face : f) {
            // Slight per-face shade variation reads as foliage or rock facets.
            const float shade = 0.9f + 0.2f * (0.5f + 0.5f * std::sin(static_cast<float>(face[0] * 3 + face[1] * 5 + seed)));
            tri(v[face[0]], v[face[1]], v[face[2]], color * shade);
        }
    }
};

const V3 kBark{0.36f, 0.26f, 0.17f};

}  // namespace

namespace meshes {

std::vector<Mesh> pineVariants() {
    std::vector<Mesh> out;
    const V3 needles{0.13f, 0.30f, 0.17f};
    out.push_back(pine());
    {   // Tall narrow fir: five tight tiers.
        Builder b;
        b.frustum(0, 0.07f, 0.3f, 0.05f, 5, kBark);
        for (int i = 0; i < 5; ++i) {
            const float z0 = 0.16f + i * 0.16f, r = 0.24f - i * 0.035f;
            b.frustum(z0, r, z0 + 0.26f, 0, 7, needles * (0.92f + i * 0.07f), i * 0.5f);
        }
        out.push_back(b.mesh);
    }
    {   // Young pine: low, wide and bushy, a bluer green.
        Builder b;
        const V3 blue{0.14f, 0.30f, 0.24f};
        b.frustum(0, 0.07f, 0.2f, 0.06f, 5, kBark);
        b.frustum(0.08f, 0.42f, 0.5f, 0.05f, 8, blue);
        b.frustum(0.35f, 0.32f, 0.75f, 0, 8, blue * 1.12f, 0.3f);
        b.frustum(0.6f, 0.2f, 0.9f, 0, 8, blue * 1.25f, 0.7f);
        out.push_back(b.mesh);
    }
    {   // Dead snag: a grey spike with broken branch stubs.
        Builder b;
        const V3 grey{0.45f, 0.42f, 0.38f};
        b.frustum(0, 0.08f, 0.85f, 0.015f, 6, grey);
        for (int i = 0; i < 6; ++i) {
            const float z = 0.3f + i * 0.09f, a = i * 2.4f;
            b.limb({0, 0, z}, {std::cos(a) * 0.16f, std::sin(a) * 0.16f, z + 0.05f}, 0.02f, 0.006f, 4, grey * 0.9f);
        }
        out.push_back(b.mesh);
    }
    return out;
}

std::vector<Mesh> oakVariants() {
    std::vector<Mesh> out;
    const V3 leaves{0.22f, 0.38f, 0.14f};
    out.push_back(oak());
    {   // Broad oak: a wide, flat-topped crown on a short trunk.
        Builder b;
        b.frustum(0, 0.12f, 0.42f, 0.08f, 6, kBark);
        b.limb({0, 0, 0.38f}, {0.3f, 0.05f, 0.58f}, 0.06f, 0.03f, 5, kBark);
        b.limb({0, 0, 0.38f}, {-0.25f, -0.15f, 0.56f}, 0.06f, 0.03f, 5, kBark);
        b.blob({0.25f, 0.05f, 0.66f}, {0.38f, 0.36f, 0.22f}, leaves * 0.95f, 0.18f, 31);
        b.blob({-0.22f, -0.12f, 0.64f}, {0.36f, 0.34f, 0.21f}, leaves * 1.08f, 0.18f, 32);
        b.blob({0, 0.08f, 0.78f}, {0.32f, 0.3f, 0.2f}, leaves * 1.18f, 0.15f, 33);
        out.push_back(b.mesh);
    }
    {   // Elm: tall trunk, a high rounded crown.
        Builder b;
        const V3 elm{0.25f, 0.36f, 0.15f};
        b.frustum(0, 0.08f, 0.66f, 0.05f, 6, kBark);
        b.blob({0, 0, 0.8f}, {0.3f, 0.3f, 0.24f}, elm, 0.15f, 34);
        b.blob({0.1f, -0.08f, 0.93f}, {0.22f, 0.22f, 0.16f}, elm * 1.15f, 0.12f, 35);
        b.blob({-0.12f, 0.1f, 0.68f}, {0.2f, 0.2f, 0.14f}, elm * 0.9f, 0.12f, 36);
        out.push_back(b.mesh);
    }
    {   // Autumn oak.
        Builder b;
        const V3 gold{0.62f, 0.42f, 0.12f}, rust{0.58f, 0.24f, 0.08f};
        b.frustum(0, 0.1f, 0.5f, 0.07f, 6, kBark);
        b.blob({0, 0, 0.68f}, {0.42f, 0.42f, 0.3f}, gold, 0.15f, 37);
        b.blob({0.16f, 0.1f, 0.82f}, {0.28f, 0.28f, 0.2f}, rust, 0.1f, 38);
        b.blob({-0.15f, -0.08f, 0.78f}, {0.26f, 0.26f, 0.2f}, gold * 1.1f, 0.1f, 39);
        out.push_back(b.mesh);
    }
    for (int autumn = 0; autumn < 2; ++autumn) {
        // Birch: slim white trunk with dark marks, small light crowns.
        Builder b;
        const V3 bark{0.86f, 0.84f, 0.78f}, mark{0.15f, 0.14f, 0.13f};
        const V3 crown = autumn ? V3{0.78f, 0.65f, 0.2f} : V3{0.36f, 0.52f, 0.2f};
        b.frustum(0, 0.05f, 0.18f, 0.045f, 6, bark);
        b.frustum(0.18f, 0.045f, 0.2f, 0.045f, 6, mark);
        b.frustum(0.2f, 0.045f, 0.42f, 0.04f, 6, bark);
        b.frustum(0.42f, 0.04f, 0.44f, 0.04f, 6, mark);
        b.frustum(0.44f, 0.04f, 0.7f, 0.025f, 6, bark);
        b.blob({0.03f, 0, 0.66f}, {0.2f, 0.2f, 0.26f}, crown, 0.15f, 40 + autumn);
        b.blob({-0.06f, 0.05f, 0.84f}, {0.15f, 0.15f, 0.18f}, crown * 1.12f, 0.12f, 42 + autumn);
        b.blob({0.08f, -0.06f, 0.52f}, {0.14f, 0.14f, 0.14f}, crown * 0.9f, 0.12f, 44 + autumn);
        out.push_back(b.mesh);
    }
    {   // Dead oak: bare grey limbs.
        Builder b;
        const V3 grey{0.4f, 0.36f, 0.32f};
        b.frustum(0, 0.1f, 0.45f, 0.07f, 6, grey);
        const V3 fork{0, 0, 0.45f};
        const V3 tips[5] = {{0.32f, 0.1f, 0.8f}, {-0.28f, 0.18f, 0.75f}, {0.05f, -0.3f, 0.85f}, {-0.1f, 0.05f, 0.95f}, {0.22f, -0.2f, 0.66f}};
        for (int i = 0; i < 5; ++i) {
            b.limb(fork, tips[i], 0.05f, 0.012f, 4, grey * (0.9f + 0.05f * i));
            const V3 twig = tips[i] + V3{tips[i].y * 0.4f, -tips[i].x * 0.4f, 0.06f};
            b.limb(tips[i], twig, 0.012f, 0.004f, 3, grey);
        }
        out.push_back(b.mesh);
    }
    return out;
}

std::vector<Mesh> bushVariants() {
    std::vector<Mesh> out;
    out.push_back(bush());
    {   // Berry bush: speckled with red.
        Builder b;
        const V3 leaves{0.22f, 0.34f, 0.15f};
        b.blob({0, 0, 0.45f}, {0.75f, 0.75f, 0.5f}, leaves, 0.2f, 50);
        b.blob({-0.3f, 0.2f, 0.4f}, {0.45f, 0.45f, 0.4f}, leaves * 1.1f, 0.15f, 51);
        for (int i = 0; i < 9; ++i) {
            const float a = i * 2.39f, r = 0.55f + 0.1f * std::sin(i * 1.7f);
            b.blob({std::cos(a) * r, std::sin(a) * r, 0.35f + 0.05f * (i % 3)}, {0.07f, 0.07f, 0.07f}, {0.7f, 0.08f, 0.08f}, 0, 52 + i);
        }
        out.push_back(b.mesh);
    }
    {   // Dark thorn: taller, darker, scraggly.
        Builder b;
        const V3 dark{0.16f, 0.24f, 0.11f};
        b.blob({0, 0, 0.6f}, {0.55f, 0.55f, 0.65f}, dark, 0.35f, 60);
        b.blob({0.3f, -0.15f, 0.45f}, {0.4f, 0.4f, 0.45f}, dark * 1.2f, 0.3f, 61);
        b.blob({-0.25f, 0.2f, 0.75f}, {0.3f, 0.3f, 0.35f}, dark * 0.9f, 0.3f, 62);
        out.push_back(b.mesh);
    }
    {   // Reeds: a clump of tall thin blades with seed heads.
        Builder b;
        const V3 green{0.45f, 0.52f, 0.25f}, tan{0.62f, 0.55f, 0.32f}, head{0.38f, 0.26f, 0.15f};
        for (int i = 0; i < 14; ++i) {
            const float a = i * 2.39f, r = 0.15f + 0.6f * std::fmod(i * 0.618f, 1.0f);
            const V3 base{std::cos(a) * r, std::sin(a) * r, 0};
            const V3 top = base + V3{std::cos(a) * 0.15f, std::sin(a) * 0.15f, 1.1f + 0.4f * std::sin(i * 3.1f)};
            b.limb(base, top, 0.02f, 0.005f, 3, i % 3 ? green : tan);
            if (i % 4 == 0) b.limb(top - V3{0, 0, 0.18f}, top, 0.035f, 0.03f, 4, head);
        }
        out.push_back(b.mesh);
    }
    return out;
}

std::vector<Mesh> rockVariants() {
    std::vector<Mesh> out;
    const V3 stone{0.5f, 0.48f, 0.45f};
    out.push_back(rock());
    {   // Slab: flat and broad, a mossy top.
        Builder b;
        b.blob({0, 0, 0.18f}, {1.1f, 0.9f, 0.32f}, stone * 1.05f, 0.25f, 70);
        b.blob({0.1f, 0.05f, 0.33f}, {0.6f, 0.5f, 0.08f}, {0.32f, 0.4f, 0.22f}, 0.2f, 71);
        out.push_back(b.mesh);
    }
    {   // Shard: tall and angular, darker.
        Builder b;
        b.blob({0, 0, 0.5f}, {0.6f, 0.5f, 1.0f}, stone * 0.85f, 0.45f, 72);
        b.blob({0.45f, 0.2f, 0.2f}, {0.35f, 0.3f, 0.3f}, stone * 0.9f, 0.4f, 73);
        out.push_back(b.mesh);
    }
    {   // Cluster: three stones together, warmer.
        Builder b;
        const V3 warm{0.55f, 0.5f, 0.44f};
        b.blob({0, 0, 0.3f}, {0.7f, 0.6f, 0.6f}, warm, 0.35f, 74);
        b.blob({0.7f, 0.3f, 0.18f}, {0.4f, 0.35f, 0.35f}, warm * 0.9f, 0.3f, 75);
        b.blob({-0.5f, 0.55f, 0.14f}, {0.32f, 0.3f, 0.28f}, warm * 1.1f, 0.3f, 76);
        out.push_back(b.mesh);
    }
    return out;
}

Mesh stump() {
    Builder b;
    const V3 wood{0.62f, 0.5f, 0.34f};
    // Base at the origin, about 0.5 across; the broken top lies beside it.
    b.frustum(0, 0.32f, 0.35f, 0.26f, 8, kBark);
    b.frustum(0.35f, 0.26f, 0.36f, 0, 8, wood);
    for (int i = 0; i < 4; ++i) {
        const float a = i * 1.57f + 0.4f;
        b.limb({std::cos(a) * 0.25f, std::sin(a) * 0.25f, 0.08f}, {std::cos(a) * 0.5f, std::sin(a) * 0.5f, -0.05f}, 0.07f, 0.03f, 4, kBark * 0.9f);
    }
    b.limb({0.4f, 0.1f, 0.18f}, {1.6f, 0.5f, 0.15f}, 0.2f, 0.16f, 7, kBark);
    return b.mesh;
}

Mesh pine() {
    Builder b;
    b.frustum(0, 0.08f, 0.35f, 0.06f, 5, kBark);
    const V3 needles{0.13f, 0.30f, 0.17f};
    b.frustum(0.18f, 0.32f, 0.55f, 0, 7, needles);
    b.frustum(0.42f, 0.25f, 0.80f, 0, 7, needles * 1.12f, 0.4f);
    b.frustum(0.66f, 0.17f, 1.0f, 0, 7, needles * 1.25f, 0.8f);
    return b.mesh;
}

Mesh oak() {
    Builder b;
    b.frustum(0, 0.1f, 0.5f, 0.07f, 6, kBark);
    const V3 leaves{0.22f, 0.38f, 0.14f};
    b.blob({0, 0, 0.68f}, {0.42f, 0.42f, 0.3f}, leaves, 0.15f, 1);
    b.blob({0.16f, 0.1f, 0.82f}, {0.28f, 0.28f, 0.2f}, leaves * 1.15f, 0.1f, 2);
    b.blob({-0.15f, -0.08f, 0.78f}, {0.26f, 0.26f, 0.2f}, leaves * 0.9f, 0.1f, 3);
    return b.mesh;
}

Mesh bush() {
    Builder b;
    const V3 leaves{0.26f, 0.38f, 0.16f};
    b.blob({0, 0, 0.45f}, {0.8f, 0.8f, 0.55f}, leaves, 0.2f, 4);
    b.blob({0.35f, 0.2f, 0.4f}, {0.5f, 0.5f, 0.45f}, leaves * 1.1f, 0.15f, 5);
    return b.mesh;
}

Mesh rock() {
    Builder b;
    b.blob({0, 0, 0.35f}, {1.0f, 0.85f, 0.75f}, {0.5f, 0.48f, 0.45f}, 0.35f, 6);
    return b.mesh;
}

Mesh house() {
    // The shader (shaders.h houseSurface) tells the parts apart by where they are in this unit space: keep them together.
    Builder b;
    const V3 wall{0.82f, 0.76f, 0.62f}, roof{0.55f, 0.22f, 0.14f}, door{0.3f, 0.2f, 0.12f}, beam{0.35f, 0.25f, 0.15f};
    const V3 c[8] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    b.quad(c[0], c[1], c[5], c[4], wall);
    b.quad(c[1], c[2], c[6], c[5], wall * 0.92f);
    b.quad(c[2], c[3], c[7], c[6], wall);
    b.quad(c[3], c[0], c[4], c[7], wall * 0.92f);
    // Gables and a pitched roof along y: a slab with its own thickness, so the eaves and the verges have an edge (boards
    // under the tiles: a fascia along each eave, a bargeboard up each verge) and the overhang an underside.
    const float eaveX = 1.15f, eaveZ = 0.9f, ridgeZ = 1.7f, endY = 1.1f, thick = 0.05f;
    const V3 ridge0{0, -endY, ridgeZ}, ridge1{0, endY, ridgeZ}, down{0, 0, -thick};
    b.tri({-1, -1, 1}, {1, -1, 1}, {0, -1, ridgeZ}, wall * 0.95f);
    b.tri({1, 1, 1}, {-1, 1, 1}, {0, 1, ridgeZ}, wall * 0.95f);
    for (float side : {-1.0f, 1.0f}) {
        const V3 e0{side * eaveX, -endY, eaveZ}, e1{side * eaveX, endY, eaveZ};
        const V3 tile = roof * (side < 0 ? 1.0f : 0.85f);
        if (side < 0) b.quad(e0, ridge0, ridge1, e1, tile);
        else b.quad(e1, ridge1, ridge0, e0, tile);
        if (side < 0) b.quad(e1 + down, ridge1 + down, ridge0 + down, e0 + down, beam * 0.7f);  // the underside
        else b.quad(e0 + down, ridge0 + down, ridge1 + down, e1 + down, beam * 0.7f);
        if (side < 0) b.quad(e1, e1 + down, e0 + down, e0, beam);  // the fascia
        else b.quad(e0, e0 + down, e1 + down, e1, beam);
        for (float end : {-1.0f, 1.0f}) {  // the bargeboards
            const V3 e{side * eaveX, end * endY, eaveZ}, r{0, end * endY, ridgeZ};
            if (side * end > 0) b.quad(e, r, r + down, e + down, beam * 0.9f);
            else b.quad(r, e, e + down, r + down, beam * 0.9f);
        }
    }
    // The ridge: a course of tiles laid over the joint.
    const float capX = 0.09f, capZ = ridgeZ - 0.035f, capTop = ridgeZ + 0.035f;
    b.quad({-capX, -endY, capZ}, {0, -endY, capTop}, {0, endY, capTop}, {-capX, endY, capZ}, roof * 0.78f);
    b.quad({capX, endY, capZ}, {0, endY, capTop}, {0, -endY, capTop}, {capX, -endY, capZ}, roof * 0.7f);
    // A chimney of stone through the roof, off the ridge; its flue.
    const V3 flue{0.5f, 0.47f, 0.43f};
    const V3 lo{0.24f, 0.36f, 1.3f}, hi{0.44f, 0.58f, 1.9f};
    b.quad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, flue);
    b.quad({hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, flue * 0.9f);
    b.quad({hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, flue);
    b.quad({lo.x, hi.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, flue * 0.9f);
    b.quad({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, flue * 0.3f);
    // The rafters' ends under each eave, out of the wall to the fascia.
    for (float side : {-1.0f, 1.0f}) {
        for (int i = 0; i < 7; ++i) {
            const float y = -0.9f + 0.3f * static_cast<float>(i), w = 0.035f;
            const V3 in{side * 1.0f, y, 0.955f}, out{side * (eaveX - 0.01f), y, eaveZ - thick}, drop{0, 0, -0.045f};
            b.facing(in + V3{0, -w, 0}, out + V3{0, -w, 0}, out + V3{0, -w, 0} + drop, in + V3{0, -w, 0} + drop, {0, -1, 0}, beam * 0.75f);
            b.facing(in + V3{0, w, 0}, out + V3{0, w, 0}, out + V3{0, w, 0} + drop, in + V3{0, w, 0} + drop, {0, 1, 0}, beam * 0.75f);
            b.facing(in + V3{0, -w, 0} + drop, out + V3{0, -w, 0} + drop, out + V3{0, w, 0} + drop, in + V3{0, w, 0} + drop, {0, 0, -1}, beam * 0.55f);
        }
    }
    // The door in its frame (two posts and a lintel standing proud of the wall, the door set back between them), a
    // stone step before it; timber beams on the long sides.
    b.quad({1.002f, -0.2f, 0}, {1.002f, 0.2f, 0}, {1.002f, 0.2f, 0.6f}, {1.002f, -0.2f, 0.6f}, door);
    const float proud = 1.035f;
    for (float side : {-1.0f, 1.0f}) {
        const float y0 = side * 0.2f, y1 = side * 0.25f;
        b.facing({proud, y0, 0}, {proud, y1, 0}, {proud, y1, 0.66f}, {proud, y0, 0.66f}, {1, 0, 0}, door * 1.25f);
        b.facing({1.002f, y0, 0}, {proud, y0, 0}, {proud, y0, 0.6f}, {1.002f, y0, 0.6f}, {0, -side, 0}, door * 0.7f);
        b.facing({1.0f, y1, 0}, {proud, y1, 0}, {proud, y1, 0.66f}, {1.0f, y1, 0.66f}, {0, side, 0}, door * 0.95f);
    }
    b.facing({proud, -0.2f, 0.6f}, {proud, 0.2f, 0.6f}, {proud, 0.2f, 0.66f}, {proud, -0.2f, 0.66f}, {1, 0, 0}, door * 1.25f);
    b.facing({1.002f, -0.2f, 0.6f}, {proud, -0.2f, 0.6f}, {proud, 0.2f, 0.6f}, {1.002f, 0.2f, 0.6f}, {0, 0, -1}, door * 0.55f);
    b.quad({1.003f, -1, 0.95f}, {1.003f, 1, 0.95f}, {1.003f, 1, 1.0f}, {1.003f, -1, 1.0f}, beam);
    b.quad({-1.003f, 1, 0.95f}, {-1.003f, -1, 0.95f}, {-1.003f, -1, 1.0f}, {-1.003f, 1, 1.0f}, beam);
    return b.mesh;
}

Mesh pillar() {
    Builder b;
    const V3 stone{0.78f, 0.75f, 0.68f};
    b.frustum(0, kPillarFoot, 0.06f, kPillarFoot, 8, stone * 0.9f);
    b.frustum(0.06f, 1.0f, 0.92f, 0.9f, 8, stone);
    b.frustum(0.92f, 1.2f, 1.0f, 1.2f, 8, stone * 1.05f);
    b.frustum(1.0f, 1.2f, 1.0f, 0, 8, stone * 1.05f);
    return b.mesh;
}

Mesh capsule() {
    Builder b;
    // Along x: an octagonal tube with pointed caps.
    const int n = 8;
    for (int i = 0; i < n; ++i) {
        const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        const V3 r0{0, std::cos(a0) * 0.5f, std::sin(a0) * 0.5f}, r1{0, std::cos(a1) * 0.5f, std::sin(a1) * 0.5f};
        const V3 left{-0.5f, 0, 0}, right{0.5f, 0, 0};
        const V3 l0 = left + r0 * 0.9f + V3{0.1f, 0, 0}, l1 = left + r1 * 0.9f + V3{0.1f, 0, 0};
        const V3 q0 = right + r0 * 0.9f - V3{0.1f, 0, 0}, q1 = right + r1 * 0.9f - V3{0.1f, 0, 0};
        b.quad(l0, q0, q1, l1, {1, 1, 1});
        b.tri(left, l1, l0, {1, 1, 1});
        b.tri(right, q0, q1, {1, 1, 1});
    }
    return b.mesh;
}

Mesh sphere() {
    Builder b;
    b.blob({0, 0, 0}, {0.5f, 0.5f, 0.5f}, {1, 1, 1});
    return b.mesh;
}

namespace {
// A cone's colour is a marker, not a colour: the mesh shader (uOwned) paints it in its town holder's colour, times
// colour.r (its shade).
const V3 kStone{0.66f, 0.63f, 0.57f}, kCone{1.0f, 1.0f, 3.0f};
}  // namespace

Mesh wall() {
    Builder b;
    b.box({-1, -1, 0}, {1, 1, 1}, kStone);
    // Merlons along the top, in unit space: the instance's x scale sets the length.
    for (int i = 0; i < 10; ++i) {
        const float x0 = -1 + i * 0.2f + 0.02f;
        b.box({x0, -1, 1}, {x0 + 0.1f, 1, 1.18f}, kStone * 0.95f);
    }
    return b.mesh;
}

Mesh tower() {
    Builder b;
    b.frustum(0, 1.04f, 0.1f, 1.0f, 10, kStone * 0.92f);  // battered foot
    b.frustum(0.1f, 1.0f, 1.0f, 0.92f, 10, kStone);
    b.frustum(0.93f, 0.93f, 0.97f, 1.08f, 10, kStone * 0.8f);  // corbelled out under the eave
    b.frustum(0.97f, 1.08f, 1.06f, 1.08f, 10, kStone * 0.9f);
    // The roof: a bell-cast eave thrown out over the wall, then the spire (the banner's pole is its finial).
    b.frustum(1.06f, 1.2f, 1.17f, 1.0f, 10, V3{0.86f, 1.0f, 3.0f});
    b.frustum(1.17f, 1.0f, 1.75f, 0, 10, kCone);
    b.frustum(1.06f, 1.2f, 1.06f, 0, 10, kStone * 0.5f);  // (the eave's underside)
    // Arrow loops.
    for (int i = 0; i < 5; ++i) {
        const float a = 2 * kPi * (2 * i + 0.5f) / 10, z = 0.38f + 0.2f * static_cast<float>(i % 2);
        const float r = (1.0f - 0.089f * (z - 0.04f)) * std::cos(kPi / 10) + 0.004f;
        const V3 o{std::cos(a) * r, std::sin(a) * r, z}, t{-std::sin(a) * 0.025f, std::cos(a) * 0.025f, 0};
        b.quad(o - t, o + t, o + t + V3{0, 0, 0.13f}, o - t + V3{0, 0, 0.13f}, V3{0.1f, 0.09f, 0.08f});
    }
    return b.mesh;
}

Mesh curtain() {
    Builder b;
    b.box({-1, -1, 0}, {1, 1, 1}, kStone);
    // The walk itself, worn darker (the parapet is drawn on it, as blocks).
    b.quad({-1, -1, 1.002f}, {1, -1, 1.002f}, {1, 1, 1.002f}, {-1, 1, 1.002f}, kStone * 0.8f);
    return b.mesh;
}

Mesh block() {
    Builder b;
    b.box({-1, -1, 0}, {1, 1, 1}, kStone * 0.95f);
    return b.mesh;
}

Mesh gallery(float width) {
    // One piece of a gallery round a tower at the walk (the sim's
    // Continent::Castle::Gallery): its floor from inside the tower's face
    // (y = 0) out to its edge (y = width), its top at z = 0, worn like the
    // walk, and under it a corbel stepped back to the tower. x runs across
    // the piece (scaled to its width); the rest is in metres.
    Builder b;
    b.box({-1, -0.5f, -0.4f}, {1, width, 0}, kStone * 0.95f);
    b.quad({-1, -0.5f, 0.002f}, {1, -0.5f, 0.002f}, {1, width, 0.002f}, {-1, width, 0.002f}, kStone * 0.8f);
    b.box({-0.22f, 0, -0.75f}, {0.22f, width * 0.9f, -0.4f}, kStone * 0.9f);
    b.box({-0.18f, 0, -1.1f}, {0.18f, width * 0.6f, -0.75f}, kStone * 0.85f);
    b.box({-0.14f, 0, -1.45f}, {0.14f, width * 0.3f, -1.1f}, kStone * 0.8f);
    return b.mesh;
}

namespace {
const V3 kOak{0.40f, 0.27f, 0.15f}, kIron{0.17f, 0.17f, 0.19f};
// One door leaf of planks, hinged at `hinge`, running `dir` (unit, flat) for
// `len`, swung so its outer face looks along `face`. Heights are per plank
// (a broken leaf is jagged); bands of iron cross what is left.
void leaf(Builder& b, V3 hinge, V3 dir, V3 face, float len, const float* heights, int planks, float thick) {
    const V3 up{0, 0, 1};
    const float w = len / planks;
    float lowest = 1;
    for (int i = 0; i < planks; ++i) {
        const float h = heights[i];
        lowest = std::min(lowest, h);
        const float shade = 0.9f + 0.2f * std::fabs(std::sin(static_cast<float>(i) * 2.3f + hinge.x * 5));
        b.block(hinge + dir * (w * (i + 0.5f)) + up * (h / 2), dir, face, up, {w * 0.47f, thick, h / 2}, kOak * shade);
    }
    // Iron bands and their studs, on the outer face, only where every plank still reaches.
    for (float z : {0.12f, 0.48f, 0.84f}) {
        if (z + 0.03f > lowest) break;
        b.block(hinge + dir * (len / 2) + face * (thick * 1.25f) + up * z, dir, face, up, {len * 0.48f, thick * 0.3f, 0.025f}, kIron);
        for (int i = 0; i < planks; i += 2)
            b.block(hinge + dir * (w * (i + 0.5f)) + face * (thick * 1.6f) + up * z, dir, face, up, {0.012f, 0.012f, 0.012f}, kIron * 1.4f);
    }
}
}  // namespace

Mesh gate(bool broken) {
    // Across x in [-1, 1], +y outward, z in [0, 1]: scaled by the half-width
    // across and out, and by the passage height up.
    Builder b;
    const float thick = 0.03f;
    const int planks = 6;
    if (!broken) {
        const float full[planks] = {1, 1, 1, 1, 1, 1};
        leaf(b, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0.995f, full, planks, thick);
        leaf(b, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, 0.995f, full, planks, thick);
        // Ring pulls on the outer face, and the crossbar's ends showing past the posts.
        for (float x : {-0.12f, 0.12f}) b.limb({x, thick * 2.2f, 0.43f}, {x, thick * 2.2f, 0.37f}, 0.03f, 0.03f, 5, kIron * 1.3f);
        b.block({0, -thick * 2, 0.5f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1.0f, thick, 0.035f}, kOak * 0.8f);
        return b.mesh;
    }
    // Battered in: each leaf hangs inward off its hinges, splintered, and
    // the rest of the timbers lie strewn in the passage.
    const float left[planks] = {0.62f, 0.44f, 0.51f, 0.3f, 0.22f, 0.14f};
    const float right[planks] = {0.55f, 0.6f, 0.33f, 0.4f, 0.18f, 0.25f};
    const float swing = 1.15f;
    leaf(b, {-1, 0, 0}, {std::cos(swing), -std::sin(swing), 0}, {std::sin(swing), std::cos(swing), 0}, 0.9f, left, planks, thick);
    leaf(b, {1, 0, 0}, {-std::cos(swing * 0.8f), -std::sin(swing * 0.8f), 0}, {-std::sin(swing * 0.8f), std::cos(swing * 0.8f), 0}, 0.9f, right, planks, thick);
    for (int i = 0; i < 7; ++i) {
        const float a = static_cast<float>(i) * 1.7f + 0.4f;
        const V3 dir{std::cos(a), std::sin(a), 0};
        const V3 at{std::sin(static_cast<float>(i) * 2.9f) * 0.55f, -0.15f - 0.13f * static_cast<float>(i % 4), 0.012f};
        b.block(at, dir, {-dir.y, dir.x, 0}, {0, 0, 1}, {0.16f + 0.06f * static_cast<float>(i % 3), 0.035f, 0.012f}, kOak * (0.75f + 0.05f * static_cast<float>(i % 4)));
    }
    // A bent band lying among them.
    b.block({0.2f, -0.35f, 0.006f}, {0.94f, 0.34f, 0}, {-0.34f, 0.94f, 0}, {0, 0, 1}, {0.3f, 0.02f, 0.005f}, kIron);
    return b.mesh;
}

Mesh gateLeaf(bool right) {
    // One leaf alone, to swing on its hinge at the origin: it reaches along +x (or -x for the right-hand leaf) the half of the
    // passage, +y its outer face, z in [0, 1]; the renderer turns it about the hinge as the doors open and shut.
    Builder b;
    const float thick = 0.03f, dir = right ? -1.0f : 1.0f;
    const float full[6] = {1, 1, 1, 1, 1, 1};
    leaf(b, {0, 0, 0}, {dir, 0, 0}, {0, 1, 0}, 0.995f, full, 6, thick);
    b.limb({dir * 0.88f, thick * 2.2f, 0.43f}, {dir * 0.88f, thick * 2.2f, 0.37f}, 0.03f, 0.03f, 5, kIron * 1.3f);  // the ring pull
    return b.mesh;
}

Mesh drum() {
    Builder b;
    b.frustum(0, 1.08f, 0.15f, 1.0f, 14, kStone * 0.92f);  // battered base
    b.frustum(0.15f, 1.0f, 0.95f, 0.96f, 14, kStone);
    b.frustum(0.95f, 1.06f, 1.0f, 1.06f, 14, kStone * 0.9f);  // corbels
    b.frustum(1.0f, 1.06f, 1.0f, 0.0f, 14, kStone * 0.8f);    // the top
    for (int i = 0; i < 10; ++i) {
        const float a = 2 * kPi * i / 10;
        const V3 c{std::cos(a) * 0.98f, std::sin(a) * 0.98f, 1.0f};
        const V3 t{-std::sin(a), std::cos(a), 0};
        const V3 r{std::cos(a), std::sin(a), 0};
        const V3 p0 = c - t * 0.16f - r * 0.08f, p1 = c + t * 0.16f - r * 0.08f;
        const V3 p2 = c + t * 0.16f + r * 0.08f, p3 = c - t * 0.16f + r * 0.08f;
        const V3 up{0, 0, 0.16f};
        b.quad(p3, p2, p2 + up, p3 + up, kStone * 0.95f);
        b.quad(p1, p0, p0 + up, p1 + up, kStone * 0.85f);
        b.quad(p2, p1, p1 + up, p2 + up, kStone * 0.9f);
        b.quad(p0, p3, p3 + up, p0 + up, kStone * 0.9f);
        b.quad(p0 + up, p1 + up, p2 + up, p3 + up, kStone);
    }
    // Arrow slits.
    for (int i = 0; i < 4; ++i) {
        const float a = 2 * kPi * (i + 0.5f) / 4;
        const V3 r{std::cos(a) * 1.002f, std::sin(a) * 1.002f, 0}, t{-std::sin(a) * 0.03f, std::cos(a) * 0.03f, 0};
        for (float z : {0.45f, 0.72f}) b.quad(r - t + V3{0, 0, z}, r + t + V3{0, 0, z}, r + t + V3{0, 0, z + 0.12f}, r - t + V3{0, 0, z + 0.12f}, V3{0.12f, 0.11f, 0.1f});
    }
    return b.mesh;
}

Mesh keep() {
    // Everything a man could touch stands within the battle's box for the keep ([-1, 1]^2): the wall's face is set
    // back (kWall), so the plinth, the buttresses, the string courses and the sills have room to stand proud of it.
    // Only the parapet on its corbels and the corner turrets, far overhead, oversail the box.
    Builder b;
    const V3 dark{0.075f, 0.07f, 0.065f}, oak{0.25f, 0.17f, 0.1f};
    const auto up = [](float z) { return V3{0, 0, z}; };
    const float kWall = 0.962f, kHead = 0.94f, kOver = 1.03f;
    for (int side = 0; side < 4; ++side) {
        const V3 u = side == 0 ? V3{1, 0, 0} : side == 1 ? V3{0, 1, 0} : side == 2 ? V3{-1, 0, 0} : V3{0, -1, 0};  // along the face
        const V3 o{u.y, -u.x, 0};                                                                               // out of it
        // The face, its windows and (in the -y face) the door let into it: loops to the lowest floor, a pair of
        // lancets to each floor above, each with a sill under it and a hood over it.
        std::vector<Builder::Opening> openings;
        for (float at : {-0.45f, 0.45f}) {
            const float x = at + kWall;
            if (side == 0 && at < 0) openings.push_back({x - 0.085f, x + 0.085f, 0, 0.155f, oak});
            else openings.push_back({x - 0.013f, x + 0.013f, 0.19f, 0.27f, dark});
            for (float z : {0.42f, 0.68f}) {
                openings.push_back({x - 0.036f, x + 0.036f, z, z + 0.115f, dark});
                const V3 c = o * kWall + u * at + up(z);
                for (float edge : {-0.016f, 0.115f}) {  // sill, hood
                    const float w = 0.052f;
                    const V3 lo = c + up(edge), tip = o * 0.014f;
                    b.quad(lo - u * w + tip, lo + u * w + tip, lo + u * w + tip + up(0.016f), lo - u * w + tip + up(0.016f), kStone * 1.1f);
                    b.quad(lo - u * w + tip + up(0.016f), lo + u * w + tip + up(0.016f), lo + u * w + up(0.024f), lo - u * w + up(0.024f), kStone * 1.14f);
                    b.facing(lo - u * w, lo + u * w, lo + u * w + tip, lo - u * w + tip, {0, 0, -1}, kStone * 0.6f);
                }
            }
        }
        b.pierced(o * kWall - u * kWall, u, 2 * kWall, kHead, openings, 0.035f, kStone);
        if (side == 0) {  // the door's lintel and its step
            const V3 c = o * kWall + u * -0.45f;
            b.block(c + up(0.167f), u, o, {0, 0, 1}, {0.11f, 0.014f, 0.012f}, kStone * 1.1f);
            b.block(c + o * 0.015f + up(0.006f), u, o, {0, 0, 1}, {0.1f, 0.02f, 0.006f}, kStone * 0.95f);
        }
        // A battered plinth, clasping buttresses at the corners and a pilaster up the middle of each face, a string
        // course at each floor, and a row of corbels that carries the parapet out over the wall.
        b.quad(o - u, o + u, o * kWall + u * kWall + up(0.06f), o * kWall - u * kWall + up(0.06f), kStone * 0.9f);
        for (float at : {-0.867f, 0.0f, 0.867f}) {
            const float half = at == 0 ? 0.08f : 0.095f, top = at == 0 ? 0.84f : 0.9f, deep = 0.995f - kWall;
            const V3 c = o * 0.995f + u * at;
            b.quad2(c - u * half, c + u * half, c + u * half + up(top), c - u * half + up(top), kStone * 0.95f, kStone * 1.04f);
            b.quad(c + u * half, c + u * half - o * deep, c + u * half - o * deep + up(top), c + u * half + up(top), kStone * 0.82f);
            b.quad(c - u * half - o * deep, c - u * half, c - u * half + up(top), c - u * half - o * deep + up(top), kStone * 0.82f);
            b.quad(c - u * half + up(top), c + u * half + up(top), c + u * half - o * deep + up(top + 0.04f), c - u * half - o * deep + up(top + 0.04f), kStone * 1.06f);
        }
        for (float z : {0.33f, 0.6f}) {
            const float out = kWall + 0.014f;
            const V3 c = o * out + up(z);
            b.quad(c - u * out, c + u * out, c + u * out + up(0.018f), c - u * out + up(0.018f), kStone * 1.06f);
            b.quad(c - u * out + up(0.018f), c + u * out + up(0.018f), o * kWall + u * kWall + up(z + 0.028f), o * kWall - u * kWall + up(z + 0.028f), kStone * 1.1f);
            b.facing(c - u * out, c + u * out, o * kWall + u * kWall + up(z), o * kWall - u * kWall + up(z), {0, 0, -1}, kStone * 0.55f);
        }
        for (int i = 0; i < 12; ++i) {
            const V3 c = o * kWall + u * (-0.9f + i * 0.1636f) + up(0.87f), tip = o * (kOver - kWall) + up(0.07f);
            b.quad(c - u * 0.03f, c + u * 0.03f, c + u * 0.03f + tip, c - u * 0.03f + tip, kStone * 0.7f);
            b.tri(c + u * 0.03f, c + u * 0.03f + up(0.07f), c + u * 0.03f + tip, kStone * 0.66f);
            b.tri(c - u * 0.03f, c - u * 0.03f + tip, c - u * 0.03f + up(0.07f), kStone * 0.66f);
        }
        // (the parapet's underside between them, in its own shade)
        b.facing(o * kWall - u * kWall + up(kHead), o * kWall + u * kWall + up(kHead), o * kOver + u * kOver + up(kHead), o * kOver - u * kOver + up(kHead), {0, 0, -1}, kStone * 0.45f);
    }
    b.box({-kOver, -kOver, kHead}, {kOver, kOver, 1.0f}, kStone * 0.9f);
    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < 7; ++i) {
            const float t0 = -1 + i * 0.3f + 0.05f, t1 = t0 + 0.15f, in = kOver - 0.09f;
            const V3 lo = side == 0 ? V3{t0, -kOver, 1} : side == 1 ? V3{t0, in, 1} : side == 2 ? V3{-kOver, t0, 1} : V3{in, t0, 1};
            const V3 hi = side == 0 ? V3{t1, -in, 1.08f} : side == 1 ? V3{t1, kOver, 1.08f} : side == 2 ? V3{-in, t1, 1.08f} : V3{kOver, t1, 1.08f};
            b.box(lo, hi, kStone * 0.95f);
        }
    }
    // Corner turrets rise above the roof on their corbels, with tiled caps and a gilt ball on each.
    for (int k = 0; k < 4; ++k) {
        const size_t from = b.mark();
        b.frustum(0.52f, 0.06f, 0.62f, 0.2f, 8, kStone * 0.75f);
        b.frustum(0.62f, 0.2f, 1.18f, 0.2f, 8, kStone * 0.95f);
        b.frustum(1.14f, 0.225f, 1.18f, 0.225f, 8, kStone * 0.85f);  // (a ring under the eave)
        b.frustum(1.18f, 0.28f, 1.24f, 0.21f, 8, V3{0.86f, 1.0f, 3.0f});
        b.frustum(1.24f, 0.21f, 1.46f, 0.0f, 8, kCone);
        b.frustum(1.18f, 0.28f, 1.18f, 0.0f, 8, kStone * 0.5f);
        b.blob({0, 0, 1.47f}, {0.022f, 0.022f, 0.022f}, {1.0f, 0.9f, 0.5f}, 0, 11);
        b.shift(from, {k & 1 ? 0.92f : -0.92f, k & 2 ? 0.92f : -0.92f, 0});
    }
    return b.mesh;
}

Mesh ramp() {
    // A stair of stone, kRampSteps treads from the ground (x = -1) to the landing (x = 1, z = 1). The sim walks the
    // one plane from foot to head; the treads lie half a riser under it at their backs and half over it at their
    // noses, so a man's feet are never more than half a riser (a hand's breadth) from the stone. The sides run on
    // below the ground (z < 0), so the foot meets the ground wherever it dips a little; they are the curtain's faces,
    // and the landing at the top is a curtain block: stair, landing and walk read as one paved way.
    Builder b;
    const float base = -0.15f;
    const int n = kRampSteps;
    for (int i = 0; i < n; ++i) {
        const float x0 = -1 + 2.0f * static_cast<float>(i) / n, x1 = -1 + 2.0f * static_cast<float>(i + 1) / n;
        const float z = (static_cast<float>(i) + 0.5f) / n, below = i == 0 ? base : (static_cast<float>(i) - 0.5f) / n;
        b.quad({x0, 1, below}, {x0, -1, below}, {x0, -1, z}, {x0, 1, z}, kStone * 0.74f);  // the riser
        b.quad2({x0, 1, z}, {x0, -1, z}, {x1, -1, z}, {x1, 1, z}, kStone * 0.86f, kStone * 0.66f);  // the tread, shaded at its back
        b.quad({x0, -1, base}, {x1, -1, base}, {x1, -1, z}, {x0, -1, z}, kStone);    // the sides
        b.quad({x1, 1, base}, {x0, 1, base}, {x0, 1, z}, {x1, 1, z}, kStone);
    }
    b.quad({1, -1, base}, {1, 1, base}, {1, 1, 1}, {1, -1, 1}, kStone * 0.9f);  // the high end, against the landing
    return b.mesh;
}

namespace {
// The banners' turned pole, collar and gilt finial, z from 0 to 1.
void turnedPole(Builder& b) {
    const V3 wood{0.3f, 0.22f, 0.15f}, gilt{0.85f, 0.66f, 0.25f};
    b.frustum(0, 0.016f, 0.955f, 0.011f, 8, wood);
    b.frustum(0.93f, 0.017f, 0.945f, 0.017f, 8, gilt);
    b.frustum(0.955f, 0.011f, 0.97f, 0.024f, 8, gilt);
    b.frustum(0.97f, 0.024f, 1.0f, 0, 8, gilt);
}
// The cloth: one sheet of cells along x (the fly) and z (the hoist), from
// x0 at the pole, z from z0 to z1, so the mesh shader can wave it (uFlag).
// Its colour holds where on the cloth each vertex is (u along, v up) and 2
// as a mark, for the shader to paint the owner's colour and device;
// one-sided, lit from either face.
void bannerCells(Builder& b, float z0, float z1) {
    constexpr int kAlong = 12, kUp = 4;
    constexpr float kX0 = 0.012f, kX1 = 0.52f;
    const auto vertex = [&](int i, int j) {
        const float u = static_cast<float>(i) / kAlong, v = static_cast<float>(j) / kUp;
        b.mesh.data.insert(b.mesh.data.end(), {kX0 + (kX1 - kX0) * u, 0, z0 + (z1 - z0) * v, 0, 1, 0, u, v, 2});
    };
    constexpr int kTri[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
    for (int j = 0; j < kUp; ++j)
        for (int i = 0; i < kAlong; ++i)
            for (const auto& d : kTri) vertex(i + d[0], j + d[1]);
}
}  // namespace

Mesh banner() {
    Builder b;
    turnedPole(b);
    bannerCells(b, 0.6f, 0.93f);
    return b.mesh;
}

Mesh bannerPole() {
    Builder b;
    turnedPole(b);
    return b.mesh;
}

Mesh bannerCloth() {
    Builder b;
    bannerCells(b, -kBannerClothDrop, 0);
    return b.mesh;
}

// The sites' platforms: a step a soldier walks over, at a battle's scale. The
// battle's stones (engine/sim/src/region.cpp siteStones) follow the temple's
// and the sage's tower's models here: keep them together. A ruin is drawn
// with its 3D model (tools/models, renderer_models.cpp), which its stones are
// derived from; this box ruin only stands in until that model is fetched.
constexpr float kSitePlinth = 0.005f;

Mesh ruin() {
    Builder b;
    const V3 old{0.55f, 0.54f, 0.48f}, moss{0.38f, 0.45f, 0.30f};
    const float p = kSitePlinth;
    // A broken ring of columns around a collapsed hall, a dark stair going down.
    b.box({-0.5f, -0.4f, 0}, {0.5f, 0.4f, p}, old * 0.9f);
    const float heights[8] = {0.55f, 0.2f, 0.42f, 0.08f, 0.5f, 0.32f, 0.12f, 0.46f};
    for (int i = 0; i < 8; ++i) {
        const float a = 2 * kPi * i / 8;
        const V3 c{std::cos(a) * 0.42f, std::sin(a) * 0.34f, 0};
        b.box({c.x - 0.04f, c.y - 0.04f, p}, {c.x + 0.04f, c.y + 0.04f, p + heights[i]}, i % 3 ? old : moss);
    }
    b.box({-0.5f, 0.3f, 0}, {0.1f, 0.38f, 0.3f}, old);
    b.box({-0.5f, -0.38f, 0}, {-0.42f, 0.1f, 0.22f}, moss);
    b.quad({-0.12f, -0.12f, p + 0.001f}, {0.12f, -0.12f, p + 0.001f}, {0.12f, 0.12f, p + 0.001f}, {-0.12f, 0.12f, p + 0.001f}, {0.05f, 0.05f, 0.06f});
    b.blob({0.3f, -0.15f, p}, {0.12f, 0.1f, 0.05f}, old * 0.8f, 0.3f, 9);
    b.blob({-0.2f, 0.15f, p}, {0.1f, 0.12f, 0.05f}, old * 0.75f, 0.3f, 10);
    return b.mesh;
}

Mesh temple() {
    // A hall between two colonnades on a stepped base (three shallow treads up to the floor the battle walks: the
    // floor's height is the battle's, kSitePlinth). A fluted column to each of the battle's stones (square there,
    // round here within the square), each on a base under a capital. The hall is the battle's hall: its east end is
    // an open porch between the side walls' ends, two columns standing in it before the bronze doors, which are set
    // back in the wall behind. Over all an architrave, a frieze of triglyphs and a cornice; a pediment at either end,
    // its field set back under the raking cornice; a tiled roof with gilt finials.
    Builder b;
    const V3 marble{0.92f, 0.9f, 0.84f}, roof{0.7f, 0.42f, 0.28f}, bronze{0.42f, 0.3f, 0.16f}, gilt{1.0f, 0.9f, 0.5f};
    const float p = kSitePlinth;
    b.box({-0.524f, -0.344f, 0}, {0.524f, 0.344f, p / 3}, marble * 0.86f);
    b.box({-0.512f, -0.332f, 0}, {0.512f, 0.332f, p * 2 / 3}, marble * 0.9f);
    b.box({-0.5f, -0.32f, 0}, {0.5f, 0.32f, p}, marble * 0.94f);
    // A column: base, fluted shaft swelling a little below its middle, necking, echinus, abacus.
    const auto column = [&](float x, float y, float r, float top) {
        const size_t from = b.mark();
        const float foot = p + 0.012f, neck = top - 0.038f, belly = foot + (neck - foot) * 0.4f;
        b.frustum(p, r * 1.07f, p + 0.007f, r * 1.07f, 12, marble * 0.9f);
        b.frustum(p + 0.007f, r * 1.07f, foot, r, 12, marble * 0.97f);
        const size_t shaft = b.mark();
        b.fluted(foot, r, belly, r * 0.955f, 10, marble);
        b.fluted(belly, r * 0.955f, neck, r * 0.82f, 10, marble);
        b.turned(shaft);
        b.frustum(neck, r * 0.86f, neck + 0.005f, r * 0.86f, 12, marble * 0.9f);
        b.frustum(neck + 0.005f, r * 0.84f, top - 0.014f, r * 1.12f, 12, marble * 0.86f);
        b.box({-r * 1.14f, -r * 1.14f, top - 0.014f}, {r * 1.14f, r * 1.14f, top}, marble * 0.97f);
        b.shift(from, {x, y, 0});
    };
    for (int i = 0; i < 6; ++i)
        for (int side = -1; side <= 1; side += 2) column(-0.42f + i * 0.168f, side * 0.25f, 0.0295f, 0.5f);
    // The hall: its walls shaded toward the ceiling and the floor; the porch at the east end.
    const float back = -0.3f, front = 0.3f, wall = 0.18f, door = 0.2f, anta = 0.03f, head = 0.5f;
    const V3 lit = marble * 0.93f, dim = marble * 0.72f, low = marble * 0.84f;
    const auto face = [&](V3 a, V3 c, V3 want) {  // a wall from a (at the floor) to c (at the same height), up to the head
        const V3 mid{0, 0, (head - p) * 0.55f}, top{0, 0, head - p};
        if ((c - a).cross(mid).dot(want) < 0) std::swap(a, c);
        b.quad2(a, c, c + mid, a + mid, low, lit);
        b.quad2(a + mid, c + mid, c + top, a + top, lit, dim);
    };
    face({back, -wall, p}, {front, -wall, p}, {0, -1, 0});
    face({back, wall, p}, {front, wall, p}, {0, 1, 0});
    face({back, -wall, p}, {back, wall, p}, {-1, 0, 0});
    for (float side : {-1.0f, 1.0f}) {
        face({front, side * wall, p}, {front, side * (wall - anta), p}, {1, 0, 0});            // the anta's end
        face({door, side * (wall - anta), p}, {front, side * (wall - anta), p}, {0, -side, 0});  // the porch's side
        column(0.268f, side * 0.062f, 0.021f, head);
    }
    // The door wall, the doors let into it between jambs under a lintel.
    b.pierced({door, -(wall - anta), p}, {0, 1, 0}, 2 * (wall - anta), head - p, {{wall - anta - 0.05f, wall - anta + 0.05f, 0, 0.3f, bronze}}, 0.014f, marble * 0.7f);
    b.block({door + 0.004f, 0, p + 0.312f}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0.066f, 0.006f, 0.012f}, marble * 0.95f);
    for (float side : {-1.0f, 1.0f}) b.block({door + 0.003f, side * 0.056f, p + 0.15f}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0.006f, 0.004f, 0.15f}, marble * 0.92f);
    b.facing({door - 0.0135f, -0.001f, p}, {door - 0.0135f, 0.001f, p}, {door - 0.0135f, 0.001f, p + 0.3f}, {door - 0.0135f, -0.001f, p + 0.3f}, {1, 0, 0}, marble * 0.1f);
    b.facing({door, -(wall - anta), p + 0.0005f}, {front, -(wall - anta), p + 0.0005f}, {front, wall - anta, p + 0.0005f}, {door, wall - anta, p + 0.0005f}, {0, 0, 1}, marble * 0.7f);  // the porch's floor, in shade
    // The ceiling over the colonnades and the porch: the architrave's underside, in its own shade.
    b.facing({-0.5f, -0.3f, head}, {0.5f, -0.3f, head}, {0.5f, 0.3f, head}, {-0.5f, 0.3f, head}, {0, 0, -1}, marble * 0.42f);
    // Architrave, frieze (set back, darker, a triglyph over each column and between) and cornice (thrown out).
    b.box({-0.5f, -0.3f, 0.5f}, {0.5f, 0.3f, 0.522f}, marble * 0.97f);
    b.box({-0.49f, -0.29f, 0.522f}, {0.49f, 0.29f, 0.548f}, marble * 0.84f);
    for (int i = 0; i <= 10; ++i)
        for (float side : {-1.0f, 1.0f}) b.block({-0.42f + i * 0.084f, side * 0.293f, 0.535f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.011f, 0.004f, 0.013f}, marble * 0.93f);
    for (int i = 0; i <= 6; ++i)
        for (float end : {-1.0f, 1.0f}) b.block({end * 0.493f, -0.25f + i * 0.0833f, 0.535f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.004f, 0.011f, 0.013f}, marble * 0.93f);
    b.box({-0.528f, -0.328f, 0.548f}, {0.528f, 0.328f, 0.562f}, marble);
    b.facing({-0.528f, -0.328f, 0.548f}, {0.528f, -0.328f, 0.548f}, {0.528f, 0.328f, 0.548f}, {-0.528f, 0.328f, 0.548f}, {0, 0, -1}, marble * 0.5f);
    // The roof: tiles on a slab whose edge is the raking cornice of each pediment; the pediment's field stands back
    // under it, in its shade, and the slab's underside shows over that.
    const float eave = 0.345f, verge = 0.54f, ridge = 0.762f, foot = 0.562f, field = 0.492f;
    const V3 up{0, 0, 0.022f};
    for (float side : {-1.0f, 1.0f}) {
        const V3 e0{-verge, side * eave, foot}, e1{verge, side * eave, foot}, r0{-verge, 0, ridge}, r1{verge, 0, ridge};
        b.facing(e0 + up, e1 + up, r1 + up, r0 + up, {0, side, 1}, roof * (side < 0 ? 1.0f : 0.85f));
        b.facing(e0, e1, e1 + up, e0 + up, {0, side, 0}, marble * 0.95f);  // the eave's edge
        b.facing(e0, e1, r1, r0, {0, -side, -1}, marble * 0.45f);          // the underside
        for (float end : {-1.0f, 1.0f}) {
            const V3 e{end * verge, side * eave, foot}, r{end * verge, 0, ridge};
            b.facing(e, r, r + up, e + up, {end, 0, 0}, marble);
        }
    }
    for (float end : {-1.0f, 1.0f}) {
        const V3 a{end * field, -0.3f, foot}, c{end * field, 0.3f, foot}, top{end * field, 0, foot + 0.3f * (ridge - foot) / eave};
        if (end > 0) b.tri(c, a, top, marble * 0.8f);
        else b.tri(a, c, top, marble * 0.8f);
        b.blob({end * (verge - 0.006f), 0, ridge + 0.04f}, {0.014f, 0.014f, 0.024f}, gilt, 0, 11);
        for (float side : {-1.0f, 1.0f}) b.blob({end * (verge - 0.008f), side * (eave - 0.008f), foot + 0.034f}, {0.01f, 0.01f, 0.016f}, gilt, 0, 7);
    }
    return b.mesh;
}

Mesh sageTower() {
    // A round tower of twelve faces (no wider than the battle's stone for it: 0.3 to its corners at the foot, the
    // circle engine/sim/src/region.cpp siteStones gives it): a
    // battered foot, two string courses, loops up its height and a lit oriel, a corbelled head under a steep slated
    // roof with a gilt ball; a plank door at its foot.
    Builder b;
    const V3 stone{0.6f, 0.58f, 0.62f}, roof{0.42f, 0.22f, 0.6f}, dark{0.1f, 0.09f, 0.1f}, oak{0.3f, 0.2f, 0.12f};
    const int n = 12;
    const float twist = kPi / n;
    const auto wallAt = [](float z) { return 0.275f - (z - 0.05f) * 0.0821f; };  // the wall's radius at a height
    b.frustum(0, 0.3f, 0.05f, 0.275f, n, stone * 0.9f, twist);
    b.frustum(0.05f, 0.275f, 0.72f, 0.22f, n, stone, twist);
    for (float z : {0.28f, 0.5f}) {
        const float r = wallAt(z) + 0.008f;
        b.frustum(z, r, z + 0.012f, r, n, stone * 1.08f, twist);
        b.frustum(z + 0.012f, r, z + 0.018f, r - 0.008f, n, stone * 1.1f, twist);
    }
    b.frustum(0.72f, 0.22f, 0.76f, 0.275f, n, stone * 0.8f, twist);  // the corbelling
    b.frustum(0.76f, 0.275f, 0.8f, 0.275f, n, stone * 0.92f, twist);
    b.frustum(0.8f, 0.31f, 0.86f, 0.25f, n, roof * 0.9f, twist);     // the roof's flared eave, then its spire
    b.frustum(0.86f, 0.25f, 1.15f, 0, n, roof, twist);
    b.blob({0, 0, 1.17f}, {0.04f, 0.04f, 0.04f}, {1.0f, 0.9f, 0.5f}, 0, 11);
    // On the face at angle a, a panel w to either side from z0 to z1, lying on the tapering wall.
    const auto panel = [&](float a, float w, float z0, float z1, V3 colour, float proud) {
        const float flat = std::cos(kPi / n);
        const V3 out{std::cos(a), std::sin(a), 0}, along{-std::sin(a), std::cos(a), 0};
        const V3 lo = out * (wallAt(z0) * flat + proud) + V3{0, 0, z0}, hi = out * (wallAt(z1) * flat + proud) + V3{0, 0, z1};
        b.quad(lo - along * w, lo + along * w, hi + along * w, hi - along * w, colour);
    };
    const float face = 2 * kPi / n;
    panel(face * 6, 0.05f, 0.05f, 0.17f, stone * 1.12f, 0.002f);  // the door's surround, and the door
    panel(face * 6, 0.036f, 0.05f, 0.155f, oak, 0.004f);
    for (int i = 0; i < 5; ++i) {
        const float a = face * static_cast<float>(1 + i * 2 + (i > 2 ? 1 : 0)), z = 0.2f + 0.1f * static_cast<float>((i * 3) % 5);
        panel(a, 0.022f, z - 0.012f, z + 0.075f, stone * 1.12f, 0.002f);
        panel(a, 0.008f, z, z + 0.06f, dark, 0.004f);
    }
    b.box({0.2f, -0.05f, 0.4f}, {0.285f, 0.05f, 0.52f}, {0.95f, 0.8f, 0.4f});  // the oriel, lit, between its sill and its head
    b.box({0.195f, -0.058f, 0.52f}, {0.292f, 0.058f, 0.535f}, stone * 0.85f);
    b.box({0.2f, -0.055f, 0.385f}, {0.29f, 0.055f, 0.4f}, stone * 0.85f);
    return b.mesh;
}

// What stands about a house or on a green (each in metres, its own origin on the ground): drawn, not solid.
namespace {
const V3 kPlank{0.4f, 0.28f, 0.15f}, kEndGrain{0.52f, 0.42f, 0.27f};
// A log or a pole from a to c with its cut ends.
void pole(Builder& b, V3 a, V3 c, float r, int n, V3 bark) {
    b.limb(a, c, r, r, n, bark);
    const V3 axis = (c - a).normalized(), helper = std::fabs(axis.z) < 0.9f ? V3{0, 0, 1} : V3{1, 0, 0};
    const V3 u = axis.cross(helper).normalized(), v = axis.cross(u);
    for (int i = 0; i < n; ++i) {
        const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        const V3 d0 = (u * std::cos(a0) + v * std::sin(a0)) * r, d1 = (u * std::cos(a1) + v * std::sin(a1)) * r;
        b.tri(a, a + d1, a + d0, kEndGrain);
        b.tri(c, c + d0, c + d1, kEndGrain);
    }
}
}  // namespace

Mesh well() {
    Builder b;
    const V3 stone = kStone * 0.95f;
    b.frustum(0, 1.0f, 0.85f, 0.92f, 10, stone);
    for (int i = 0; i < 10; ++i) {  // the kerb's top, and the dark of the shaft within
        const float a0 = 2 * kPi * i / 10, a1 = 2 * kPi * (i + 1) / 10;
        const V3 o0{std::cos(a0), std::sin(a0), 0}, o1{std::cos(a1), std::sin(a1), 0}, z{0, 0, 0.85f};
        b.quad(o0 * 0.92f + z, o1 * 0.92f + z, o1 * 0.62f + z, o0 * 0.62f + z, stone * 1.08f);
        b.quad(o1 * 0.62f + z, o0 * 0.62f + z, o0 * 0.62f + V3{0, 0, 0.2f}, o1 * 0.62f + V3{0, 0, 0.2f}, stone * 0.3f);
    }
    b.disc({0, 0, 0.2f}, 0.62f, 10, {0.05f, 0.08f, 0.1f});
    for (float side : {-1.0f, 1.0f}) b.block({side * 0.8f, 0, 1.55f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.07f, 0.07f, 0.75f}, kPlank);
    pole(b, {-0.9f, 0, 1.75f}, {0.9f, 0, 1.75f}, 0.07f, 6, kPlank * 0.85f);
    b.limb({0, 0, 1.7f}, {0, 0, 1.15f}, 0.012f, 0.012f, 4, {0.3f, 0.27f, 0.2f});
    b.frustum(0.95f, 0.13f, 1.18f, 0.16f, 8, kPlank * 0.9f);  // the bucket
    b.facing({-1.05f, -0.6f, 2.25f}, {1.05f, -0.6f, 2.25f}, {1.05f, 0, 2.65f}, {-1.05f, 0, 2.65f}, {0, -1, 1}, kPlank * 0.8f);
    b.facing({-1.05f, 0.6f, 2.25f}, {1.05f, 0.6f, 2.25f}, {1.05f, 0, 2.65f}, {-1.05f, 0, 2.65f}, {0, 1, 1}, kPlank * 0.7f);
    return b.mesh;
}

Mesh woodpile() {
    // Split logs stacked along y against a wall at x = 0, three high, between two stakes.
    Builder b;
    const V3 bark{0.33f, 0.25f, 0.17f};
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < 4 - row; ++i) {
            const float x = 0.16f + 0.3f * static_cast<float>(i) + 0.15f * static_cast<float>(row), z = 0.14f + 0.26f * static_cast<float>(row);
            pole(b, {x, -0.55f, z}, {x, 0.55f + 0.06f * static_cast<float>((i + row) % 3), z}, 0.14f, 5, bark * (0.85f + 0.1f * static_cast<float>((i * 3 + row) % 4)));
        }
    return b.mesh;
}

Mesh cart() {
    // A two-wheeled cart along x, tipped onto its shafts.
    Builder b;
    b.block({0, 0, 0.8f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1.1f, 0.55f, 0.05f}, kPlank);
    for (float side : {-1.0f, 1.0f}) {
        b.block({0, side * 0.55f, 1.05f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1.1f, 0.03f, 0.22f}, kPlank * 0.9f);
        pole(b, {0, side * 0.62f, 0.55f}, {0, side * 0.72f, 0.55f}, 0.55f, 10, kPlank * 0.7f);
        pole(b, {0.9f, side * 0.4f, 0.8f}, {2.5f, side * 0.4f, 0.06f}, 0.04f, 5, kPlank * 0.85f);
    }
    b.block({-1.1f, 0, 1.05f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.03f, 0.55f, 0.22f}, kPlank * 0.85f);
    return b.mesh;
}

Mesh stall() {
    // A market stall along x: a board on trestles under a canvas awning on four poles; baskets on the board.
    Builder b;
    const V3 canvas{0.86f, 0.8f, 0.66f};
    for (float x : {-1.2f, 1.2f})
        for (float y : {-0.6f, 0.6f}) b.block({x, y, y < 0 ? 1.0f : 1.2f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.04f, 0.04f, y < 0 ? 1.0f : 1.2f}, kPlank * 0.85f);
    b.block({0, 0, 0.85f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1.15f, 0.5f, 0.035f}, kPlank);
    for (int i = 0; i < 6; ++i) {  // the awning, in stripes
        const float x0 = -1.35f + 0.45f * static_cast<float>(i), x1 = x0 + 0.45f;
        b.facing({x0, -0.8f, 1.95f}, {x1, -0.8f, 1.95f}, {x1, 0.7f, 2.45f}, {x0, 0.7f, 2.45f}, {0, -0.3f, 1}, i % 2 ? canvas : canvas * 0.8f);
    }
    for (int i = 0; i < 3; ++i) {
        const size_t from = b.mark();
        b.frustum(0.885f, 0.16f, 1.05f, 0.2f, 7, V3{0.62f, 0.5f, 0.3f} * (0.85f + 0.1f * static_cast<float>(i)));
        b.disc({0, 0, 1.03f}, 0.19f, 7, i == 1 ? V3{0.75f, 0.2f, 0.12f} : V3{0.45f, 0.5f, 0.16f});
        b.shift(from, {-0.7f + 0.7f * static_cast<float>(i), 0.1f, 0});
    }
    return b.mesh;
}

Mesh fence() {
    // A run of post and rail along x from -1 to 1 (scaled by its half length), a metre high.
    Builder b;
    for (float x : {-1.0f, 0.0f, 1.0f}) b.block({x, 0, 0.5f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.03f, 0.05f, 0.5f}, kPlank * 0.8f);
    for (float z : {0.4f, 0.8f}) b.block({0, 0.03f, z}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1.0f, 0.02f, 0.045f}, kPlank * 0.95f);
    return b.mesh;
}

Mesh barrel() {
    Builder b;
    b.frustum(0, 0.3f, 0.45f, 0.38f, 9, kPlank * 0.95f);
    b.frustum(0.45f, 0.38f, 0.9f, 0.3f, 9, kPlank);
    b.disc({0, 0, 0.86f}, 0.29f, 9, kPlank * 0.7f);
    for (float z : {0.14f, 0.76f}) b.frustum(z, 0.345f, z + 0.05f, 0.345f, 9, kIron);
    return b.mesh;
}

Mesh ship() {
    Builder b;
    const V3 wood{0.45f, 0.3f, 0.17f}, dark{0.3f, 0.2f, 0.12f}, deck{0.62f, 0.5f, 0.34f};
    // The hull: a section at each station, from a pointed stern to a pointed
    // bow, the sides flaring out from a narrow keel.
    struct Station { float x, half, keel, rail; };
    const Station st[] = {{-1.0f, 0.02f, -0.02f, 0.34f}, {-0.75f, 0.2f, -0.12f, 0.28f}, {-0.3f, 0.27f, -0.16f, 0.24f},
                          {0.3f, 0.27f, -0.16f, 0.24f},   {0.75f, 0.18f, -0.12f, 0.3f},  {1.05f, 0.02f, -0.02f, 0.42f}};
    constexpr int kStations = static_cast<int>(sizeof st / sizeof st[0]);
    for (int i = 0; i + 1 < kStations; ++i) {
        const Station& a = st[i];
        const Station& c = st[i + 1];
        for (int side = -1; side <= 1; side += 2) {
            const float s = static_cast<float>(side);
            const V3 ak{a.x, 0, a.keel}, ck{c.x, 0, c.keel};
            const V3 aw{a.x, s * a.half, 0.05f}, cw{c.x, s * c.half, 0.05f};  // the widest, just above the water
            const V3 ar{a.x, s * a.half * 0.85f, a.rail}, cr{c.x, s * c.half * 0.85f, c.rail};
            if (side > 0) {
                b.quad(ak, ck, cw, aw, dark);
                b.quad(aw, cw, cr, ar, wood);
            } else {
                b.quad(ak, aw, cw, ck, dark);
                b.quad(aw, ar, cr, cw, wood);
            }
        }
        // The deck, a little below the rail.
        const float az = std::min(a.rail, c.rail) - 0.05f;
        b.quad({a.x, -a.half * 0.8f, az}, {c.x, -c.half * 0.8f, az}, {c.x, c.half * 0.8f, az}, {a.x, a.half * 0.8f, az}, deck);
    }
    // Mast and yard.
    b.frustum(0.15f, 0.035f, 1.5f, 0.02f, 5, dark);
    b.limb({0.12f, -0.45f, 1.3f}, {0.12f, 0.45f, 1.3f}, 0.02f, 0.02f, 4, dark);
    // A raised stern deck, and the steering oar.
    b.box({-0.85f, -0.15f, 0.18f}, {-0.55f, 0.15f, 0.3f}, wood);
    b.limb({-0.9f, 0.2f, 0.3f}, {-1.05f, 0.24f, -0.12f}, 0.025f, 0.02f, 4, dark);
    return b.mesh;
}

Mesh sail() {
    Builder b;
    // A square sail hanging from the yard, bellied forward, two-sided.
    const V3 cloth{1, 1, 1};
    const V3 tl{0.12f, -0.43f, 1.29f}, tr{0.12f, 0.43f, 1.29f}, ml{0.2f, -0.42f, 0.85f}, mr{0.2f, 0.42f, 0.85f}, bl{0.14f, -0.4f, 0.42f},
        br{0.14f, 0.4f, 0.42f};
    b.quad(tl, ml, mr, tr, cloth);
    b.quad(ml, bl, br, mr, cloth * 0.92f);
    b.quad(tl, tr, mr, ml, cloth * 0.85f);
    b.quad(ml, mr, br, bl, cloth * 0.8f);
    return b.mesh;
}

Mesh pier() {
    Builder b;
    const V3 plank{0.55f, 0.43f, 0.3f}, pile{0.3f, 0.22f, 0.15f};
    b.box({0, -1, -0.25f}, {1, 1, 0}, plank);
    for (int i = 0; i <= 4; ++i) {
        const float x = 0.04f + 0.23f * static_cast<float>(i);
        for (int side = -1; side <= 1; side += 2) {
            const float y = 0.8f * static_cast<float>(side);
            b.box({x - 0.012f, y - 0.12f, -1}, {x + 0.012f, y + 0.12f, 0.15f}, pile);
        }
    }
    return b.mesh;
}

Mesh log() {
    Builder b;
    const V3 bark{0.34f, 0.25f, 0.17f}, wood{0.62f, 0.5f, 0.34f}, moss{0.32f, 0.4f, 0.2f};
    // An eight-sided trunk lying along x, resting on the ground.
    const int n = 8;
    for (int i = 0; i < n; ++i) {
        const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        const V3 p0{-1, std::cos(a0), 1 + std::sin(a0)}, p1{-1, std::cos(a1), 1 + std::sin(a1)};
        const V3 q0{1, std::cos(a0) * 0.85f, 1 + std::sin(a0) * 0.85f}, q1{1, std::cos(a1) * 0.85f, 1 + std::sin(a1) * 0.85f};
        b.quad(p0, q0, q1, p1, (i == 2 || i == 3) ? moss : bark * (0.85f + 0.05f * static_cast<float>(i % 3)));
        b.tri({-1, 0, 1}, p0, p1, wood);  // sawn or broken ends show pale wood
        b.tri({1, 0, 1}, q1, q0, wood);
    }
    // A snapped branch stub.
    b.frustum(0, 0.3f, 1.4f, 0.12f, 5, bark);
    return b.mesh;
}

namespace {
// A flat-lit triangle (normal up), for ground cover.
void flatTri(Mesh& m, V3 a, V3 b, V3 c, V3 ca, V3 cb, V3 cc) {
    const V3 pts[3] = {a, b, c}, cols[3] = {ca, cb, cc};
    for (int i = 0; i < 3; ++i)
        m.data.insert(m.data.end(), {pts[i].x, pts[i].y, pts[i].z, 0, 0, 1, cols[i].x, cols[i].y, cols[i].z});
}
float frand(int i) { return 0.5f + 0.5f * std::sin(static_cast<float>(i) * 12.9898f + 78.233f); }
}  // namespace

Mesh grassTuft() {
    Mesh m;
    const V3 base{0.62f, 0.66f, 0.55f}, tip{1.04f, 1.06f, 0.9f};
    for (int i = 0; i < 9; ++i) {
        const float a = 2 * kPi * (static_cast<float>(i) / 9 + frand(i) * 0.2f);
        const float r = 0.03f + frand(i + 20) * 0.07f, h = 0.16f + frand(i + 40) * 0.26f, lean = 0.05f + frand(i + 60) * 0.1f;
        const V3 dir{std::cos(a), std::sin(a), 0}, side{-std::sin(a) * 0.022f, std::cos(a) * 0.022f, 0};
        const V3 root = dir * r;
        flatTri(m, root - side, root + side, root + dir * lean + V3{0, 0, h}, base, base, tip);
    }
    return m;
}

Mesh flowers() {
    Mesh m;
    const V3 stem{0.5f, 0.75f, 0.4f}, petal{2, 2, 2};
    for (int i = 0; i < 3; ++i) {
        const float a = 2 * kPi * i / 3 + frand(i) * 0.6f;
        const V3 root{std::cos(a) * 0.05f, std::sin(a) * 0.05f, 0};
        const float h = 0.2f + frand(i + 7) * 0.16f;
        const V3 top = root + V3{0.02f, 0.01f, h};
        flatTri(m, root + V3{-0.01f, 0, 0}, root + V3{0.01f, 0, 0}, top, stem * 0.7f, stem * 0.7f, stem);
        const float s = 0.035f;
        flatTri(m, top + V3{-s, 0, 0}, top + V3{s, 0, 0}, top + V3{0, s, 0.01f}, petal, petal, petal);
        flatTri(m, top + V3{-s, 0, 0}, top + V3{s, 0, 0}, top + V3{0, -s, 0.01f}, petal, petal, petal);
        flatTri(m, top + V3{0, -s, 0}, top + V3{0, s, 0}, top + V3{s, 0, 0.012f}, petal, petal, petal);
    }
    return m;
}

Mesh pebbles() {
    Mesh m;
    const V3 stone{0.62f, 0.6f, 0.56f};
    for (int k = 0; k < 3; ++k) {
        const V3 c{(frand(k) - 0.5f) * 0.4f, (frand(k + 5) - 0.5f) * 0.4f, 0};
        const float r = 0.03f + frand(k + 9) * 0.05f;
        const V3 top = c + V3{0, 0, r * 0.7f};
        for (int i = 0; i < 5; ++i) {
            const float a0 = 2 * kPi * i / 5, a1 = 2 * kPi * (i + 1) / 5;
            const V3 p0 = c + V3{std::cos(a0) * r, std::sin(a0) * r, 0}, p1 = c + V3{std::cos(a1) * r, std::sin(a1) * r, 0};
            const float shade = 0.8f + 0.25f * frand(i + k * 5);
            flatTri(m, p0, p1, top, stone * shade * 0.8f, stone * shade * 0.8f, stone * shade * 1.1f);
        }
    }
    return m;
}

Mesh twigs() {
    Mesh m;
    const V3 wood{0.42f, 0.32f, 0.2f}, leaf{0.6f, 0.38f, 0.16f};
    for (int k = 0; k < 2; ++k) {
        const float a = frand(k) * kPi;
        const V3 d{std::cos(a) * 0.35f, std::sin(a) * 0.35f, 0}, s{-std::sin(a) * 0.012f, std::cos(a) * 0.012f, 0};
        const V3 z{0, 0, 0.015f};
        flatTri(m, z - d - s, d - s + z, d + s + z, wood, wood, wood * 0.8f);
        flatTri(m, z - d - s, d + s + z, z - d + s, wood, wood * 0.8f, wood);
    }
    // A few fallen leaves.
    for (int i = 0; i < 5; ++i) {
        const V3 c{(frand(i + 3) - 0.5f) * 0.6f, (frand(i + 13) - 0.5f) * 0.6f, 0.01f};
        const float r = 0.04f;
        const V3 col = leaf * (0.7f + 0.5f * frand(i + 23));
        flatTri(m, c + V3{-r, 0, 0}, c + V3{r, 0, 0}, c + V3{0, r * 1.4f, 0.005f}, col, col, col);
        flatTri(m, c + V3{-r, 0, 0}, c + V3{r, 0, 0}, c + V3{0, -r * 1.2f, 0.005f}, col, col, col);
    }
    return m;
}

Mesh fern() {
    Mesh m;
    const V3 dark{0.35f, 0.55f, 0.3f}, light{0.8f, 1.0f, 0.6f};
    for (int i = 0; i < 6; ++i) {
        const float a = 2 * kPi * i / 6 + frand(i) * 0.4f;
        const V3 dir{std::cos(a), std::sin(a), 0}, side{-std::sin(a) * 0.06f, std::cos(a) * 0.06f, 0};
        const V3 mid = dir * 0.22f + V3{0, 0, 0.22f}, tip = dir * 0.45f + V3{0, 0, 0.12f};
        flatTri(m, {0, 0, 0}, mid - side, mid + side, dark, dark * 1.1f, dark * 1.1f);
        flatTri(m, mid - side, tip, mid + side, dark * 1.1f, light, dark * 1.1f);
    }
    return m;
}

}  // namespace meshes

namespace meshes {

void bridge(Mesh& out, V3 centre, V3 dir, float halfSpan, float halfWidth, float deck, float rampBehind, float rampAhead, float endBehind, float endAhead, bool foot,
            const std::function<float(float, float)>& ground) {
    Builder b;
    const V3 u = V3{dir.x, dir.y, 0}.normalized(), v{-u.y, u.x, 0};
    // Local frame: s along the span, t across it, z up.
    const auto at = [&](float s, float t, float z) { return V3{centre.x + u.x * s + v.x * t, centre.y + u.y * s + v.y * t, z}; };
    const auto bed = [&](float s, float t) { const V3 p = at(s, t, 0); return ground(p.x, p.y); };
    // The deck's top at s: level over the water, a ramp down to the ground at each end (Continent::Crossing::pieces).
    const auto deckAt = [&](float s) {
        if (rampBehind > 0 && s < -halfSpan) return deck + (endBehind - deck) * std::min(1.0f, (-halfSpan - s) / rampBehind);
        if (rampAhead > 0 && s > halfSpan) return deck + (endAhead - deck) * std::min(1.0f, (s - halfSpan) / rampAhead);
        return deck;
    };
    const float s0 = -halfSpan - rampBehind, s1 = halfSpan + rampAhead;
    // An oriented box in the local frame; a sloping one has its bottom and top at each end.
    const auto slope = [&](float a, float c, float t0, float t1, float lo0, float lo1, float hi0, float hi1, V3 col) {
        const V3 p[8] = {at(a, t0, lo0), at(c, t0, lo1), at(c, t1, lo1), at(a, t1, lo0), at(a, t0, hi0), at(c, t0, hi1), at(c, t1, hi1), at(a, t1, hi0)};
        b.quad(p[0], p[1], p[5], p[4], col * 0.92f);
        b.quad(p[1], p[2], p[6], p[5], col * 0.85f);
        b.quad(p[2], p[3], p[7], p[6], col * 0.92f);
        b.quad(p[3], p[0], p[4], p[7], col * 0.85f);
        b.quad(p[4], p[5], p[6], p[7], col * 1.05f);
    };
    const auto block = [&](float a, float c, float t0, float t1, float z0, float z1, V3 col) { slope(a, c, t0, t1, z0, z0, z1, z1, col); };
    if (foot) {
        const V3 plank{0.46f, 0.34f, 0.22f}, post{0.36f, 0.26f, 0.17f};
        const float W = halfWidth;
        // Planks laid across, a little uneven.
        int k = 0;
        for (float s = s0; s < s1; s += 0.35f, ++k) {
            const float top = deckAt(s + 0.16f);
            block(s, s + 0.32f, -W, W, top - 0.12f, top - (k % 3 == 1 ? 0.02f : 0.0f), plank * (k % 4 == 0 ? 0.88f : 1.0f));
        }
        // Stringers under the planks, piles down to the bed, and hand rails.
        for (float t : {-W, W - 0.18f}) slope(s0, s1, t, t + 0.18f, deckAt(s0) - 0.4f, deckAt(s1) - 0.4f, deckAt(s0) - 0.12f, deckAt(s1) - 0.12f, post);
        for (float s = s0; s <= s1 + 0.01f; s += 4.0f) {
            for (float t : {-W, W - 0.16f}) {
                const float floor = std::min(bed(s, t), deckAt(s) - 0.4f) - 0.5f;
                block(s - 0.08f, s + 0.08f, t, t + 0.16f, floor, deckAt(s) + 1.0f, post);
            }
        }
        for (float t : {-W, W - 0.1f}) {
            slope(s0, s1, t, t + 0.1f, deckAt(s0) + 0.9f, deckAt(s1) + 0.9f, deckAt(s0) + 1.0f, deckAt(s1) + 1.0f, post * 1.1f);
            slope(s0, s1, t, t + 0.06f, deckAt(s0) + 0.45f, deckAt(s1) + 0.45f, deckAt(s0) + 0.5f, deckAt(s1) + 0.5f, post);
        }
        out.data.insert(out.data.end(), b.mesh.data.begin(), b.mesh.data.end());
        return;
    }
    // Stone: arches between piers, spandrel walls up to the parapets; a ramp's retaining walls and parapets at each end.
    const V3 stone{0.58f, 0.55f, 0.50f}, dark{0.46f, 0.44f, 0.40f}, coping{0.66f, 0.63f, 0.58f}, road{0.50f, 0.45f, 0.36f};
    const float W = halfWidth, thick = 0.9f, parapet = 1.05f;
    const int bays = std::max(1, static_cast<int>(std::round(halfSpan * 2 / 11.0f)));
    const float pier = 1.8f, bay = (halfSpan * 2 - pier * (bays - 1)) / bays;
    // Where the soffit is at s: the intrados of an arch, or the ground under a pier.
    const auto soffit = [&](float s) {
        float x = s + halfSpan;
        for (int i = 0; i < bays; ++i) {
            if (x <= bay) {
                const float half = bay / 2, c = x - half;
                const float low = std::min(bed(s, 0), deck - thick - 0.5f);
                const float rise = std::min(half, deck - thick - low);
                const float spring = deck - thick - rise;
                return spring + rise * std::sqrt(std::max(0.0f, 1 - (c / half) * (c / half)));
            }
            x -= bay;
            if (x <= pier) return std::min(bed(s, 0), deck - thick) - 1.0f;
            x -= pier;
        }
        return deck - thick;
    };
    const float step = 0.6f;
    for (float s = -halfSpan; s < halfSpan - 1e-3f; s += step) {
        const float c = std::min(halfSpan, s + step);
        const float z0 = soffit(s), z1 = soffit(c);
        // Faces either side, from the soffit to the parapet top.
        b.quad(at(s, -W, z0), at(c, -W, z1), at(c, -W, deck + parapet), at(s, -W, deck + parapet), stone * 0.9f);
        b.quad(at(c, W, z1), at(s, W, z0), at(s, W, deck + parapet), at(c, W, deck + parapet), stone * 0.9f);
        // The underside of the arch.
        b.quad(at(c, -W, z1), at(s, -W, z0), at(s, W, z0), at(c, W, z1), dark);
    }
    // The level span's deck, parapets (inner faces and coping), and each end: a ramp down to the ground (the road is carried
    // on a causeway between retaining walls that taper to the parapets), or 3 m of deck into the bank if it needs none.
    block(-halfSpan, halfSpan, -W + 0.45f, W - 0.45f, deck - 0.2f, deck, road);
    for (float t : {-W, W - 0.45f}) {
        block(-halfSpan, halfSpan, t, t + 0.45f, deck - 0.2f, deck + parapet, stone);
        block(-halfSpan - 0.1f, halfSpan + 0.1f, t - 0.05f, t + 0.5f, deck + parapet, deck + parapet + 0.14f, coping);
    }
    for (float sgn : {-1.0f, 1.0f}) {
        const float ramp = sgn < 0 ? rampBehind : rampAhead;
        if (ramp <= 0) {
            const float a = sgn < 0 ? -halfSpan - 3 : halfSpan, c = sgn < 0 ? -halfSpan : halfSpan + 3;
            block(a, c, -W + 0.45f, W - 0.45f, deck - 0.2f, deck, road);
            for (float t : {-W, W - 0.45f}) {
                block(a, c, t, t + 0.45f, deck - 0.2f, deck + parapet, stone);
                block(a, c, t - 0.05f, t + 0.5f, deck + parapet, deck + parapet + 0.14f, coping);
            }
            block(a, c, -W, W, std::min(bed(a, 0), bed(c, 0)) - 1.0f, deck - 0.2f, stone * 0.95f);
            continue;
        }
        for (float s = 0; s < ramp - 1e-3f; s += 1.0f) {
            const float e = std::min(ramp, s + 1.0f);
            const float a = sgn * (halfSpan + s), c = sgn * (halfSpan + e);
            const float za = deckAt(a), zc = deckAt(c);
            // Fill under the roadway to the ground, and the walls either side of it.
            slope(std::min(a, c), std::max(a, c), -W + 0.45f, W - 0.45f, std::min(bed(a, 0), bed(c, 0)) - 1.0f, std::min(bed(a, 0), bed(c, 0)) - 1.0f,
                  sgn < 0 ? zc - 0.2f : za - 0.2f, sgn < 0 ? za - 0.2f : zc - 0.2f, stone * 0.95f);
            const float lo = std::min(a, c), hi = std::max(a, c);
            const float zlo = sgn < 0 ? zc : za, zhi = sgn < 0 ? za : zc;
            slope(lo, hi, -W + 0.45f, W - 0.45f, zlo - 0.2f, zhi - 0.2f, zlo, zhi, road);
            for (float t : {-W, W - 0.45f}) {
                const float floor = std::min(bed(lo, t), bed(hi, t)) - 0.8f;
                slope(lo, hi, t, t + 0.45f, floor, floor, zlo + parapet, zhi + parapet, stone);
                slope(lo, hi, t - 0.05f, t + 0.5f, zlo + parapet, zhi + parapet, zlo + parapet + 0.14f, zhi + parapet + 0.14f, coping);
            }
        }
    }
    // Cutwaters on the piers, pointing upstream and down.
    float x = -halfSpan + bay;
    for (int i = 1; i < bays; ++i, x += bay + pier) {
        const float low = std::min(bed(x + pier / 2, 0), deck - thick) - 1.0f, top = deck - thick - 0.6f;
        for (float sgn : {-1.0f, 1.0f}) {
            const V3 tip = at(x + pier / 2, sgn * (W + 1.4f), low), tipTop = at(x + pier / 2, sgn * (W + 1.4f), top);
            const V3 a0 = at(x, sgn * W, low), a1 = at(x, sgn * W, top), b0 = at(x + pier, sgn * W, low), b1 = at(x + pier, sgn * W, top);
            if (sgn > 0) {
                b.quad(a0, tip, tipTop, a1, dark);
                b.quad(tip, b0, b1, tipTop, dark * 0.9f);
                b.tri(a1, tipTop, b1, coping);
            } else {
                b.quad(tip, a0, a1, tipTop, dark);
                b.quad(b0, tip, tipTop, b1, dark * 0.9f);
                b.tri(tipTop, a1, b1, coping);
            }
        }
    }
    out.data.insert(out.data.end(), b.mesh.data.begin(), b.mesh.data.end());
}

}  // namespace meshes

}  // namespace illyria::game
