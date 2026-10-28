#include "meshes.h"

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
    Builder b;
    const V3 wall{0.82f, 0.76f, 0.62f}, roof{0.55f, 0.22f, 0.14f}, door{0.3f, 0.2f, 0.12f}, beam{0.35f, 0.25f, 0.15f};
    const V3 c[8] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    b.quad(c[0], c[1], c[5], c[4], wall);
    b.quad(c[1], c[2], c[6], c[5], wall * 0.92f);
    b.quad(c[2], c[3], c[7], c[6], wall);
    b.quad(c[3], c[0], c[4], c[7], wall * 0.92f);
    // Gables and a pitched roof along y.
    const V3 ridge0{0, -1.1f, 1.7f}, ridge1{0, 1.1f, 1.7f};
    b.tri({-1, -1, 1}, {1, -1, 1}, {0, -1, 1.7f}, wall * 0.95f);
    b.tri({1, 1, 1}, {-1, 1, 1}, {0, 1, 1.7f}, wall * 0.95f);
    b.quad({-1.15f, -1.1f, 0.9f}, ridge0, ridge1, {-1.15f, 1.1f, 0.9f}, roof);
    b.quad({1.15f, 1.1f, 0.9f}, ridge1, ridge0, {1.15f, -1.1f, 0.9f}, roof * 0.85f);
    // Door and timber beams on the long side.
    b.quad({1.002f, -0.2f, 0}, {1.002f, 0.2f, 0}, {1.002f, 0.2f, 0.6f}, {1.002f, -0.2f, 0.6f}, door);
    b.quad({1.003f, -1, 0.95f}, {1.003f, 1, 0.95f}, {1.003f, 1, 1.0f}, {1.003f, -1, 1.0f}, beam);
    b.quad({-1.003f, 1, 0.95f}, {-1.003f, -1, 0.95f}, {-1.003f, -1, 1.0f}, {-1.003f, 1, 1.0f}, beam);
    return b.mesh;
}

Mesh pillar() {
    Builder b;
    const V3 stone{0.78f, 0.75f, 0.68f};
    b.frustum(0, 1.3f, 0.06f, 1.3f, 8, stone * 0.9f);
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
const V3 kStone{0.66f, 0.63f, 0.57f}, kSlate{0.30f, 0.32f, 0.38f};
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
    b.frustum(0, 1.0f, 1.0f, 0.92f, 10, kStone);
    b.frustum(1.0f, 1.08f, 1.06f, 1.08f, 10, kStone * 0.9f);
    b.frustum(1.06f, 1.12f, 1.75f, 0, 10, kSlate);
    return b.mesh;
}

Mesh curtain() {
    Builder b;
    b.box({-1, -1, 0}, {1, 1, 1}, kStone);
    // Outer parapet with merlons and crenels between them.
    b.box({-1, 0.62f, 1}, {1, 1, 1.12f}, kStone * 0.97f);
    for (int i = 0; i < 8; ++i) {
        const float x0 = -1 + i * 0.25f + 0.03f;
        b.box({x0, 0.62f, 1.12f}, {x0 + 0.13f, 1, 1.26f}, kStone * 0.95f);
    }
    // A low inner parapet.
    b.box({-1, -1, 1}, {1, -0.82f, 1.06f}, kStone * 0.92f);
    // The walk itself, worn darker.
    b.quad({-1, -0.82f, 1.002f}, {1, -0.82f, 1.002f}, {1, 0.62f, 1.002f}, {-1, 0.62f, 1.002f}, kStone * 0.8f);
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
    Builder b;
    b.box({-1, -1, 0}, {1, 1, 1}, kStone);
    b.box({-1.05f, -1.05f, 0.94f}, {1.05f, 1.05f, 1.0f}, kStone * 0.9f);
    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < 7; ++i) {
            const float t0 = -1 + i * 0.3f + 0.05f, t1 = t0 + 0.15f;
            const V3 lo = side == 0 ? V3{t0, -1.05f, 1} : side == 1 ? V3{t0, 0.95f, 1} : side == 2 ? V3{-1.05f, t0, 1} : V3{0.95f, t0, 1};
            const V3 hi = side == 0 ? V3{t1, -0.95f, 1.08f} : side == 1 ? V3{t1, 1.05f, 1.08f} : side == 2 ? V3{-0.95f, t1, 1.08f} : V3{1.05f, t1, 1.08f};
            b.box(lo, hi, kStone * 0.95f);
        }
    }
    // Corner turrets rise above the roof, with slate caps.
    for (int k = 0; k < 4; ++k) {
        const float x = k & 1 ? 0.92f : -0.92f, y = k & 2 ? 0.92f : -0.92f;
        Builder t;
        t.frustum(0.6f, 0.2f, 1.18f, 0.2f, 8, kStone * 0.95f);
        t.frustum(1.18f, 0.24f, 1.42f, 0.0f, 8, kSlate);
        for (size_t v = 0; v < t.mesh.data.size(); v += 9) {
            t.mesh.data[v] += x;
            t.mesh.data[v + 1] += y;
        }
        b.mesh.data.insert(b.mesh.data.end(), t.mesh.data.begin(), t.mesh.data.end());
    }
    // A door and windows.
    b.quad({-0.15f, -1.002f, 0}, {0.15f, -1.002f, 0}, {0.15f, -1.002f, 0.12f}, {-0.15f, -1.002f, 0.12f}, V3{0.25f, 0.17f, 0.1f});
    for (int side = 0; side < 4; ++side)
        for (float z : {0.4f, 0.65f}) {
            const float w = 0.05f;
            if (side == 0) b.quad({-w, -1.003f, z}, {w, -1.003f, z}, {w, -1.003f, z + 0.08f}, {-w, -1.003f, z + 0.08f}, V3{0.1f, 0.09f, 0.08f});
            if (side == 1) b.quad({w, 1.003f, z}, {-w, 1.003f, z}, {-w, 1.003f, z + 0.08f}, {w, 1.003f, z + 0.08f}, V3{0.1f, 0.09f, 0.08f});
            if (side == 2) b.quad({-1.003f, w, z}, {-1.003f, -w, z}, {-1.003f, -w, z + 0.08f}, {-1.003f, w, z + 0.08f}, V3{0.1f, 0.09f, 0.08f});
            if (side == 3) b.quad({1.003f, -w, z}, {1.003f, w, z}, {1.003f, w, z + 0.08f}, {1.003f, -w, z + 0.08f}, V3{0.1f, 0.09f, 0.08f});
        }
    return b.mesh;
}

Mesh stairs() {
    Builder b;
    const int steps = 10;
    for (int i = 0; i < steps; ++i) {
        const float x0 = -1 + 2.0f * i / steps, x1 = -1 + 2.0f * (i + 1) / steps, z = static_cast<float>(i + 1) / steps;
        b.box({x0, -1, 0}, {x1, 1, z}, kStone * (i % 2 ? 0.93f : 1.0f));
    }
    return b.mesh;
}

Mesh banner() {
    Builder b;
    b.frustum(0, 0.03f, 1.0f, 0.02f, 4, {0.35f, 0.3f, 0.25f});
    // White so the instance tint shows the owner's colour; two-sided.
    const V3 cloth{1, 1, 1};
    b.quad({0, 0, 0.95f}, {0.7f, 0, 0.93f}, {0.7f, 0, 0.63f}, {0, 0, 0.6f}, cloth);
    b.quad({0, 0, 0.6f}, {0.7f, 0, 0.63f}, {0.7f, 0, 0.93f}, {0, 0, 0.95f}, cloth * 0.85f);
    return b.mesh;
}

Mesh ruin() {
    Builder b;
    const V3 old{0.55f, 0.54f, 0.48f}, moss{0.38f, 0.45f, 0.30f};
    // A broken ring of columns around a collapsed hall, a dark stair going down.
    b.box({-0.5f, -0.4f, 0}, {0.5f, 0.4f, 0.06f}, old * 0.9f);
    const float heights[8] = {0.55f, 0.2f, 0.42f, 0.08f, 0.5f, 0.32f, 0.12f, 0.46f};
    for (int i = 0; i < 8; ++i) {
        const float a = 2 * kPi * i / 8;
        const V3 c{std::cos(a) * 0.42f, std::sin(a) * 0.34f, 0};
        b.box({c.x - 0.04f, c.y - 0.04f, 0.06f}, {c.x + 0.04f, c.y + 0.04f, 0.06f + heights[i]}, i % 3 ? old : moss);
    }
    b.box({-0.5f, 0.3f, 0}, {0.1f, 0.38f, 0.3f}, old);
    b.box({-0.5f, -0.38f, 0}, {-0.42f, 0.1f, 0.22f}, moss);
    b.quad({-0.12f, -0.12f, 0.061f}, {0.12f, -0.12f, 0.061f}, {0.12f, 0.12f, 0.061f}, {-0.12f, 0.12f, 0.061f}, {0.05f, 0.05f, 0.06f});
    b.blob({0.3f, -0.15f, 0.06f}, {0.12f, 0.1f, 0.06f}, old * 0.8f, 0.3f, 9);
    b.blob({-0.2f, 0.15f, 0.06f}, {0.1f, 0.12f, 0.05f}, old * 0.75f, 0.3f, 10);
    return b.mesh;
}

Mesh temple() {
    Builder b;
    const V3 marble{0.92f, 0.9f, 0.84f}, roof{0.7f, 0.42f, 0.28f};
    b.box({-0.5f, -0.32f, 0}, {0.5f, 0.32f, 0.08f}, marble * 0.92f);
    for (int i = 0; i < 6; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            const float x = -0.42f + i * 0.168f, y = side * 0.25f;
            b.box({x - 0.03f, y - 0.03f, 0.08f}, {x + 0.03f, y + 0.03f, 0.5f}, marble);
        }
    }
    b.box({-0.3f, -0.18f, 0.08f}, {0.3f, 0.18f, 0.45f}, marble * 0.85f);
    b.box({-0.5f, -0.3f, 0.5f}, {0.5f, 0.3f, 0.56f}, marble * 0.95f);
    b.quad({-0.52f, -0.32f, 0.56f}, {0.52f, -0.32f, 0.56f}, {0.52f, 0, 0.74f}, {-0.52f, 0, 0.74f}, roof);
    b.quad({0.52f, 0.32f, 0.56f}, {-0.52f, 0.32f, 0.56f}, {-0.52f, 0, 0.74f}, {0.52f, 0, 0.74f}, roof * 0.85f);
    b.tri({-0.5f, -0.3f, 0.56f}, {-0.5f, 0.3f, 0.56f}, {-0.5f, 0, 0.74f}, marble);
    b.tri({0.5f, 0.3f, 0.56f}, {0.5f, -0.3f, 0.56f}, {0.5f, 0, 0.74f}, marble);
    return b.mesh;
}

Mesh sageTower() {
    Builder b;
    const V3 stone{0.6f, 0.58f, 0.62f}, roof{0.42f, 0.22f, 0.6f};
    b.frustum(0, 0.3f, 0.75f, 0.22f, 8, stone);
    b.frustum(0.75f, 0.28f, 0.8f, 0.28f, 8, stone * 0.85f);
    b.frustum(0.8f, 0.3f, 1.15f, 0, 8, roof);
    b.blob({0, 0, 1.17f}, {0.04f, 0.04f, 0.04f}, {1.0f, 0.9f, 0.5f}, 0, 11);
    b.box({0.22f, -0.05f, 0.4f}, {0.3f, 0.05f, 0.52f}, {0.95f, 0.8f, 0.4f});
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

Mesh copse() {
    Builder b;
    const V3 leaves{0.2f, 0.32f, 0.13f};
    b.blob({0, 0, 0.5f}, {0.5f, 0.45f, 0.45f}, leaves, 0.2f, 21);
    b.blob({0.35f, 0.2f, 0.4f}, {0.35f, 0.35f, 0.35f}, leaves * 1.12f, 0.2f, 22);
    b.blob({-0.3f, 0.25f, 0.35f}, {0.32f, 0.3f, 0.3f}, leaves * 0.88f, 0.2f, 23);
    return b.mesh;
}

std::vector<Mesh> copseVariants() {
    std::vector<Mesh> out;
    out.push_back(copse());
    {   // Pines: a cluster of dark cones.
        Builder b;
        const V3 needles{0.12f, 0.26f, 0.15f};
        const float spots[5][3] = {{0, 0, 1.0f}, {0.3f, 0.15f, 0.8f}, {-0.28f, 0.2f, 0.85f}, {0.1f, -0.32f, 0.75f}, {-0.2f, -0.25f, 0.9f}};
        for (int i = 0; i < 5; ++i) {
            const V3 c{spots[i][0], spots[i][1], 0};
            const float h = spots[i][2];
            for (const auto& tier : {std::pair<float, float>{0.15f, 0.2f}, {0.45f, 0.15f}}) {
                const float z0 = h * tier.first;
                for (int k = 0; k < 6; ++k) {
                    const float a0 = 2 * kPi * k / 6, a1 = 2 * kPi * (k + 1) / 6;
                    b.tri(c + V3{std::cos(a0) * tier.second, std::sin(a0) * tier.second, z0},
                          c + V3{std::cos(a1) * tier.second, std::sin(a1) * tier.second, z0}, c + V3{0, 0, h}, needles * (0.9f + 0.1f * i));
                }
            }
        }
        out.push_back(b.mesh);
    }
    {   // Autumn: gold and rust crowns.
        Builder b;
        b.blob({0, 0, 0.5f}, {0.5f, 0.45f, 0.45f}, {0.62f, 0.42f, 0.12f}, 0.2f, 24);
        b.blob({0.35f, 0.2f, 0.4f}, {0.35f, 0.35f, 0.35f}, {0.58f, 0.25f, 0.08f}, 0.2f, 25);
        b.blob({-0.3f, 0.25f, 0.35f}, {0.32f, 0.3f, 0.3f}, {0.5f, 0.45f, 0.15f}, 0.2f, 26);
        out.push_back(b.mesh);
    }
    return out;
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

void bridge(Mesh& out, V3 centre, V3 dir, float halfSpan, float halfWidth, float deck, bool foot,
            const std::function<float(float, float)>& ground) {
    Builder b;
    const V3 u = V3{dir.x, dir.y, 0}.normalized(), v{-u.y, u.x, 0};
    // Local frame: s along the span, t across it, z up.
    const auto at = [&](float s, float t, float z) { return V3{centre.x + u.x * s + v.x * t, centre.y + u.y * s + v.y * t, z}; };
    const auto bed = [&](float s, float t) { const V3 p = at(s, t, 0); return ground(p.x, p.y); };
    // An oriented box in the local frame.
    const auto block = [&](float s0, float s1, float t0, float t1, float z0, float z1, V3 col) {
        const V3 c[8] = {at(s0, t0, z0), at(s1, t0, z0), at(s1, t1, z0), at(s0, t1, z0),
                         at(s0, t0, z1), at(s1, t0, z1), at(s1, t1, z1), at(s0, t1, z1)};
        b.quad(c[0], c[1], c[5], c[4], col * 0.92f);
        b.quad(c[1], c[2], c[6], c[5], col * 0.85f);
        b.quad(c[2], c[3], c[7], c[6], col * 0.92f);
        b.quad(c[3], c[0], c[4], c[7], col * 0.85f);
        b.quad(c[4], c[5], c[6], c[7], col * 1.05f);
    };
    if (foot) {
        const V3 plank{0.46f, 0.34f, 0.22f}, post{0.36f, 0.26f, 0.17f};
        const float W = halfWidth;
        // Planks laid across, a little uneven.
        int k = 0;
        for (float s = -halfSpan; s < halfSpan; s += 0.35f, ++k)
            block(s, s + 0.32f, -W, W, deck - 0.12f, deck - (k % 3 == 1 ? 0.02f : 0.0f), plank * (k % 4 == 0 ? 0.88f : 1.0f));
        // Stringers under the planks, piles down to the bed, and hand rails.
        block(-halfSpan, halfSpan, -W, -W + 0.18f, deck - 0.4f, deck - 0.12f, post);
        block(-halfSpan, halfSpan, W - 0.18f, W, deck - 0.4f, deck - 0.12f, post);
        for (float s = -halfSpan; s <= halfSpan + 0.01f; s += 4.0f) {
            for (float t : {-W, W - 0.16f}) {
                const float floor = std::min(bed(s, t), deck - 0.4f) - 0.5f;
                block(s - 0.08f, s + 0.08f, t, t + 0.16f, floor, deck + 1.0f, post);
            }
        }
        for (float t : {-W, W - 0.1f}) {
            block(-halfSpan, halfSpan, t, t + 0.1f, deck + 0.9f, deck + 1.0f, post * 1.1f);
            block(-halfSpan, halfSpan, t, t + 0.06f, deck + 0.45f, deck + 0.5f, post);
        }
        out.data.insert(out.data.end(), b.mesh.data.begin(), b.mesh.data.end());
        return;
    }
    // Stone: arches between piers, spandrel walls up to the parapets.
    const V3 stone{0.58f, 0.55f, 0.50f}, dark{0.46f, 0.44f, 0.40f}, coping{0.66f, 0.63f, 0.58f};
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
        const float s1 = std::min(halfSpan, s + step);
        const float z0 = soffit(s), z1 = soffit(s1);
        // Faces either side, from the soffit to the parapet top.
        b.quad(at(s, -W, z0), at(s1, -W, z1), at(s1, -W, deck + parapet), at(s, -W, deck + parapet), stone * 0.9f);
        b.quad(at(s1, W, z1), at(s, W, z0), at(s, W, deck + parapet), at(s1, W, deck + parapet), stone * 0.9f);
        // The underside of the arch.
        b.quad(at(s1, -W, z1), at(s, -W, z0), at(s, W, z0), at(s1, W, z1), dark);
    }
    // Deck, parapets (inner faces and coping) and abutment ends into the banks.
    block(-halfSpan - 3, halfSpan + 3, -W + 0.45f, W - 0.45f, deck - 0.2f, deck, V3{0.50f, 0.45f, 0.36f});
    for (float t : {-W, W - 0.45f}) {
        block(-halfSpan - 3, halfSpan + 3, t, t + 0.45f, deck - 0.2f, deck + parapet, stone);
        block(-halfSpan - 3.1f, halfSpan + 3.1f, t - 0.05f, t + 0.5f, deck + parapet, deck + parapet + 0.14f, coping);
    }
    for (float sgn : {-1.0f, 1.0f}) {
        const float s0 = sgn < 0 ? -halfSpan - 3 : halfSpan, s1 = sgn < 0 ? -halfSpan : halfSpan + 3;
        block(s0, s1, -W, W, std::min(bed(s0, 0), bed(s1, 0)) - 1.0f, deck - 0.2f, stone * 0.95f);
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
