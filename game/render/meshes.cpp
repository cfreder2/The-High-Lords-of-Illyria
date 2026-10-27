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

}  // namespace meshes

}  // namespace illyria::game
