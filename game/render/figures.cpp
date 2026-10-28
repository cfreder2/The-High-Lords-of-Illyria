#include "figures.h"

#include <algorithm>
#include <cmath>

#include "illyria/game/gmath.h"

namespace illyria::game::figures {

using sim::UnitType;

namespace {

constexpr float kPi = 3.14159265f;

V3 rot(V3 v, V3 k, float a) {  // v turned by a about the unit axis k
    const float c = std::cos(a), s = std::sin(a);
    return v * c + k.cross(v) * s + k * (k.dot(v) * (1 - c));
}
V3 grey(float g) { return {g, g, g}; }
float ease(float t) { return t * t * (3 - 2 * t); }
// Something that glows: the sheet shader draws colours above 1.5 unlit (less 2).
V3 glow(V3 c) { return c + grey(2.0f); }
V3 perpTo(V3 v, V3 axis, V3 fallback) {
    V3 p = v - axis * v.dot(axis);
    if (p.length() < 1e-3f) p = fallback - axis * fallback.dot(axis);
    return p.normalized();
}

// Muted colours, Myth's grey-green world: worn cloth, dull steel.
const V3 kSkin{0.74f, 0.56f, 0.43f};
const V3 kSteel{0.58f, 0.6f, 0.64f};
const V3 kDarkSteel{0.36f, 0.37f, 0.4f};
const V3 kLeather{0.44f, 0.32f, 0.2f};
const V3 kDarkLeather{0.3f, 0.22f, 0.15f};
const V3 kWood{0.43f, 0.29f, 0.16f};
const V3 kGold{0.9f, 0.7f, 0.26f};
const V3 kBone{0.8f, 0.76f, 0.64f};
const V3 kHair{0.24f, 0.17f, 0.11f};
const V3 kCloth{0.38f, 0.34f, 0.28f};

// Triangles into a Mesh, through a whole-figure transform (for falls).
struct Builder {
    Mesh& m;
    V3 rx{1, 0, 0}, ry{0, 1, 0}, rz{0, 0, 1}, t{};
    uint32_t seed = 12345;

    float jitter() {
        seed = seed * 1664525u + 1013904223u;
        return 0.93f + 0.14f * static_cast<float>(seed >> 8) / 16777216.0f;
    }
    V3 xf(V3 p) const { return rx * p.x + ry * p.y + rz * p.z + t; }
    V3 xn(V3 n) const { return rx * n.x + ry * n.y + rz * n.z; }
    void vert(V3 p, V3 n, V3 c) {
        p = xf(p);
        n = xn(n);
        m.data.insert(m.data.end(), {p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z});
    }
    void tri(V3 a, V3 b, V3 c, V3 col) {
        const V3 n = (b - a).cross(c - a).normalized();
        vert(a, n, col);
        vert(b, n, col);
        vert(c, n, col);
    }
    // Both faces: wings, cloaks, ears.
    void plate(V3 a, V3 b, V3 c, V3 d, V3 col) {
        col = col * jitter();
        tri(a, b, c, col);
        tri(a, c, d, col);
        tri(a, c, b, col * 0.85f);
        tri(a, d, c, col * 0.85f);
    }
    void flat(V3 a, V3 b, V3 c, V3 col) {
        tri(a, b, c, col);
        tri(a, c, b, col * 0.85f);
    }
    // An ellipsoid with axes ax, ay, az (unit) and radii r.
    void ellipsoid(V3 c, V3 ax, V3 ay, V3 az, V3 r, V3 col, int seg = 8, int rings = 5) {
        col = col * jitter();
        const auto at = [&](int i, int j, V3& p, V3& n) {
            const float th = kPi * j / rings, ph = 2 * kPi * i / seg;
            const float x = std::sin(th) * std::cos(ph), y = std::sin(th) * std::sin(ph), z = std::cos(th);
            p = c + ax * (x * r.x) + ay * (y * r.y) + az * (z * r.z);
            n = (ax * (x / r.x) + ay * (y / r.y) + az * (z / r.z)).normalized();
        };
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < seg; ++i) {
                V3 p[4], n[4];
                at(i, j, p[0], n[0]);
                at(i + 1, j, p[1], n[1]);
                at(i + 1, j + 1, p[2], n[2]);
                at(i, j + 1, p[3], n[3]);
                if (j > 0) {
                    vert(p[0], n[0], col);
                    vert(p[2], n[2], col);
                    vert(p[1], n[1], col);
                }
                if (j < rings - 1) {
                    vert(p[0], n[0], col);
                    vert(p[3], n[3], col);
                    vert(p[2], n[2], col);
                }
            }
    }
    void ball(V3 c, float r, V3 col, int seg = 7) { ellipsoid(c, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {r, r, r}, col, seg, 4); }
    // A tapered limb from a to b, capped at both ends.
    void limb(V3 a, V3 b, float ra, float rb, V3 col, int sides = 6) {
        col = col * jitter();
        const V3 d = (b - a).normalized();
        const V3 u = perpTo({0, 0, 1}, d, {1, 0, 0}), v = d.cross(u);
        const float slope = (ra - rb) / std::max(1e-4f, (b - a).length());
        for (int i = 0; i < sides; ++i) {
            const float a0 = 2 * kPi * i / sides, a1 = 2 * kPi * (i + 1) / sides;
            const V3 e0 = u * std::cos(a0) + v * std::sin(a0), e1 = u * std::cos(a1) + v * std::sin(a1);
            const V3 n0 = (e0 + d * slope).normalized(), n1 = (e1 + d * slope).normalized();
            vert(a + e0 * ra, n0, col);
            vert(a + e1 * ra, n1, col);
            vert(b + e1 * rb, n1, col);
            vert(a + e0 * ra, n0, col);
            vert(b + e1 * rb, n1, col);
            vert(b + e0 * rb, n0, col);
            if (ra > 0.002f) tri(a, a + e1 * ra, a + e0 * ra, col);
            if (rb > 0.002f) tri(b, b + e0 * rb, b + e1 * rb, col);
        }
    }
    // A limb with a round joint at its far end.
    void bone(V3 a, V3 b, float ra, float rb, V3 col) {
        limb(a, b, ra, rb, col);
        ball(b, rb * 1.05f, col, 6);
    }
    void box(V3 c, V3 ax, V3 ay, V3 az, V3 h, V3 col) {
        col = col * jitter();
        const V3 X = ax * h.x, Y = ay * h.y, Z = az * h.z;
        const V3 p[8] = {c - X - Y - Z, c + X - Y - Z, c + X + Y - Z, c - X + Y - Z,
                         c - X - Y + Z, c + X - Y + Z, c + X + Y + Z, c - X + Y + Z};
        const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
        for (const auto& q : f) {
            tri(p[q[0]], p[q[1]], p[q[2]], col);
            tri(p[q[0]], p[q[2]], p[q[3]], col);
        }
    }
    void cone(V3 base, V3 tip, float r, V3 col, int sides = 6) { limb(base, tip, r, 0.0f, col, sides); }
};

// ------------------------------------------------------------------ people

enum class Head : uint8_t { Bare, Helmet, Hood, Bull, WizardHat, Skull, Halo, Horns, DwarfHelm, Crown, Goblin, Wild };
enum class Weapon : uint8_t { None, Sword, Axe, Pike, Bow, Staff, Sickle, Satchel, Club, Trident, Greatsword, Lance, Spear };
enum class Shield : uint8_t { None, Round, Large };
enum class Strike : uint8_t { Swing, Overhead, Thrust, Shoot, Throw, Cast };

struct Kit {
    float height = 1.8f;  // metres, top of the head
    float bulk = 1.0f;    // breadth
    float legs = 1.0f;    // leg length relative to a man's
    float headSize = 1.12f;
    V3 skin = kSkin, tunic = kCloth, trousers = kLeather, boots = kDarkLeather, belt = kDarkLeather;
    V3 hair = kHair, wing = grey(0.9f), team = grey(0.5f);
    Head head = Head::Bare;
    Weapon weapon = Weapon::Sword;
    Shield shield = Shield::None;
    Strike strike = Strike::Swing;
    bool robe = false, wings = false, batWings = false, tail = false, bareChest = false, bones = false;
    bool armour = false, mail = false, cape = false, quiver = false, kilt = false, glowEyes = false;
};

// One frame of a person: every angle in radians.
#define POSE_FIELDS(X)                                                                                         \
    X(lean) X(twist) X(side) X(nod) X(rSh) X(rOut) X(rEl) X(rWr) X(lSh) X(lOut) X(lEl) X(rHip) X(rKnee) X(lHip) \
        X(lKnee) X(hipOut) X(fall) X(draw) X(wing) X(cape) X(orb) X(held)
struct Pose {
#define DECLARE(f) float f = 0;
    POSE_FIELDS(DECLARE)
#undef DECLARE
};
Pose mix(const Pose& a, const Pose& b, float t) {
    Pose r;
#define LERP(f) r.f = a.f + (b.f - a.f) * t;
    POSE_FIELDS(LERP)
#undef LERP
    return r;
}
// Through a list of (time, pose) keys at time t.
Pose keyed(const std::pair<float, Pose>* keys, int n, float t) {
    if (t <= keys[0].first) return keys[0].second;
    for (int i = 1; i < n; ++i)
        if (t <= keys[i].first) return mix(keys[i - 1].second, keys[i].second, ease((t - keys[i - 1].first) / (keys[i].first - keys[i - 1].first)));
    return keys[n - 1].second;
}

bool twoHanded(Weapon w) { return w == Weapon::Pike || w == Weapon::Greatsword || w == Weapon::Staff || w == Weapon::Trident || w == Weapon::Spear; }

Pose stance(const Kit& k, bool mounted) {
    Pose p;
    p.held = 1;
    p.rSh = 0.35f, p.rOut = 0.15f, p.rEl = 1.0f, p.rWr = 0.25f;
    p.lSh = 0.4f, p.lOut = 0.15f, p.lEl = 1.0f;
    p.hipOut = 0.07f;
    p.rKnee = p.lKnee = 0.08f;
    p.wing = 1.1f;
    switch (k.weapon) {
        case Weapon::Pike: case Weapon::Spear: case Weapon::Trident:
            // Shaft upright at the side, butt on the ground.
            p.rSh = 0.25f, p.rEl = 0.9f, p.rWr = -0.25f;
            p.lSh = 0.2f, p.lEl = 0.4f;
            break;
        case Weapon::Lance: p.rSh = 0.4f, p.rEl = 1.1f, p.rWr = 0.25f; break;
        case Weapon::Bow: p.lSh = 0.45f, p.lEl = 0.5f, p.rSh = 0.15f, p.rEl = 0.4f; break;
        case Weapon::Greatsword: p.rSh = 0.5f, p.rEl = 1.1f, p.rWr = 0.0f, p.lSh = 0.55f, p.lEl = 1.0f, p.lOut = -0.2f; break;
        case Weapon::Staff: p.rSh = 0.3f, p.rEl = 1.1f, p.rWr = -0.15f, p.lSh = 0.1f, p.lEl = 0.3f; break;
        case Weapon::Club: p.rSh = 0.25f, p.rEl = 0.6f, p.rWr = 0.6f; break;
        default: break;
    }
    if (k.shield == Shield::Large) p.lSh = 0.55f, p.lEl = 1.25f, p.lOut = 0.05f;
    if (mounted) p.rHip = p.lHip = 1.35f, p.rKnee = p.lKnee = 1.45f, p.hipOut = 0.42f;
    return p;
}

Pose idle(const Kit& k, bool mounted, float t) {
    Pose p = stance(k, mounted);
    const float s = std::sin(2 * kPi * t);
    p.lean += 0.02f * s;
    p.nod += 0.05f * std::sin(2 * kPi * t + 1.0f);
    p.rSh += 0.04f * s;
    p.lSh -= 0.03f * s;
    p.wing = 1.1f + 0.08f * s;
    p.cape = 0.05f * s;
    p.orb = 0.5f + 0.5f * s;
    return p;
}

Pose walk(const Kit& k, bool mounted, float t) {
    Pose p = stance(k, mounted);
    const float c = 2 * kPi * t, s = std::sin(c);
    if (!mounted) {
        p.rHip = 0.5f * s, p.lHip = -0.5f * s;
        p.rKnee = 0.12f + 0.6f * std::max(0.0f, std::cos(c - 0.5f)), p.lKnee = 0.12f + 0.6f * std::max(0.0f, -std::cos(c - 0.5f));
        p.lean = 0.1f;
        // Arms swing against the legs; a weapon arm less.
        const float arm = twoHanded(k.weapon) || k.shield == Shield::Large ? 0.08f : 0.3f;
        p.lSh += -arm * s * (k.shield != Shield::None ? 0.4f : 1.0f);
        p.rSh += arm * s * 0.5f;
        if (k.robe) p.rHip *= 0.7f, p.lHip *= 0.7f;
    } else {
        p.lean = 0.05f + 0.06f * std::sin(2 * c);
    }
    p.twist = 0.06f * s;
    p.wing = 0.35f + 0.85f * std::sin(c + 0.4f);
    p.cape = 0.25f + 0.1f * std::sin(2 * c);
    p.orb = 0.7f;
    return p;
}

Pose attack(const Kit& k, bool mounted, float t) {
    const Pose base = stance(k, mounted);
    Pose wind = base, hit = base, follow = base;
    switch (k.strike) {
        case Strike::Swing:
            wind.rSh = 2.8f, wind.rOut = 0.35f, wind.rEl = 1.0f, wind.rWr = -0.4f, wind.twist = -0.45f, wind.lean = -0.05f;
            hit.rSh = 1.0f, hit.rOut = 0.0f, hit.rEl = 0.1f, hit.rWr = 1.4f, hit.twist = 0.4f, hit.lean = 0.22f;
            follow = mix(hit, base, 0.4f);
            follow.rSh = 0.2f;
            break;
        case Strike::Overhead:
            wind.rSh = wind.lSh = 2.9f, wind.rEl = wind.lEl = 1.3f, wind.rWr = 0.0f, wind.lean = -0.12f, wind.lOut = -0.25f;
            hit.rSh = hit.lSh = 0.85f, hit.rEl = hit.lEl = 0.2f, hit.rWr = 0.9f, hit.lean = 0.35f, hit.lOut = -0.3f;
            hit.rKnee = hit.lKnee = 0.35f;
            follow = mix(hit, base, 0.3f);
            break;
        case Strike::Thrust:
            wind.rSh = 0.9f, wind.rEl = 1.9f, wind.rWr = 1.0f, wind.lSh = 1.0f, wind.lEl = 0.9f, wind.lean = -0.1f, wind.twist = -0.3f;
            hit.rSh = 1.35f, hit.rEl = 0.3f, hit.rWr = 1.25f, hit.lSh = 1.4f, hit.lEl = 0.25f, hit.lean = 0.3f, hit.twist = 0.15f;
            hit.rHip = 0.45f, hit.lHip = -0.3f, hit.rKnee = 0.45f;
            follow = mix(hit, base, 0.25f);
            break;
        case Strike::Shoot:
            // Raise the bow, draw to the cheek, loose.
            wind.lSh = 1.5f, wind.lEl = 0.05f, wind.lOut = 0.1f, wind.rSh = 1.45f, wind.rEl = 0.4f, wind.twist = -0.35f, wind.draw = 0.15f;
            hit = wind;
            hit.rSh = 1.35f, hit.rEl = 2.4f, hit.rOut = 0.45f, hit.draw = 1.0f;
            follow = hit;
            follow.rEl = 1.3f, follow.rOut = 0.75f, follow.draw = 0.0f, follow.held = 0;
            break;
        case Strike::Throw:
            wind.rSh = -0.7f, wind.rEl = 0.6f, wind.lSh = 1.1f, wind.twist = -0.5f, wind.lean = -0.15f;
            hit.rSh = 2.6f, hit.rEl = 0.4f, hit.lSh = 0.2f, hit.twist = 0.5f, hit.lean = 0.25f, hit.rHip = 0.4f, hit.lHip = -0.25f;
            follow = mix(hit, base, 0.3f);
            follow.rSh = 1.1f;
            follow.held = 0;
            break;
        case Strike::Cast:
            wind.rSh = 2.6f, wind.rEl = 0.4f, wind.rWr = 0.2f, wind.lSh = 2.0f, wind.lOut = 0.5f, wind.lean = -0.15f, wind.orb = 1.5f;
            hit.rSh = 1.45f, hit.rEl = 0.1f, hit.rWr = 1.3f, hit.lSh = 0.8f, hit.lOut = 0.4f, hit.lean = 0.15f, hit.orb = 2.5f;
            follow = mix(hit, base, 0.4f);
            break;
    }
    if (mounted)
        for (Pose* q : {&wind, &hit, &follow}) q->rHip = base.rHip, q->lHip = base.lHip, q->rKnee = base.rKnee, q->lKnee = base.lKnee, q->hipOut = base.hipOut;
    wind.wing = 1.5f, hit.wing = 0.2f;
    const std::pair<float, Pose> keys[] = {{0.0f, base}, {0.35f, wind}, {0.6f, hit}, {0.8f, follow}, {1.0f, base}};
    Pose p = keyed(keys, 5, t);
    if (k.strike == Strike::Shoot || k.strike == Strike::Throw) p.held = t < 0.62f ? 1.0f : (t > 0.95f ? 1.0f : 0.0f);
    return p;
}

Pose flinch(const Kit& k, bool mounted, float t) {
    Pose p = stance(k, mounted);
    const float a = std::sin(kPi * (0.35f + 0.65f * t));
    p.lean -= 0.3f * a;
    p.nod -= 0.4f * a;
    p.rOut += 0.4f * a, p.lOut += 0.4f * a;
    p.rSh += 0.4f * a, p.lSh += 0.5f * a;
    if (!mounted) p.rKnee += 0.3f * a, p.lKnee += 0.2f * a;
    p.wing = 1.6f;
    return p;
}

Pose die(const Kit& k, bool mounted, float t) {
    Pose p = stance(k, mounted);
    p.held = t < 0.5f ? 1.0f : 0.0f;
    const float knees = std::sin(kPi * std::min(1.0f, t * 1.4f));
    p.rKnee += 0.9f * knees, p.lKnee += 0.7f * knees;
    p.rHip += 0.4f * knees, p.lHip += 0.25f * knees;
    p.fall = ease(std::clamp((t - 0.1f) / 0.85f, 0.0f, 1.0f)) * (kPi * 0.5f);
    p.nod = -0.5f * t;
    p.rSh = 0.35f + 2.0f * t, p.rOut = 0.7f * t, p.rEl = 1.0f - 0.7f * t;
    p.lSh = 0.4f + 1.6f * t, p.lOut = 0.6f * t, p.lEl = 1.0f - 0.6f * t;
    p.wing = 0.3f + 0.6f * (1 - t);
    p.orb = 0;
    return p;
}

Pose personPose(const Kit& k, bool mounted, Anim anim, int frame) {
    const float n = static_cast<float>(kFrames[static_cast<int>(anim)]);
    switch (anim) {
        case Anim::Idle: return idle(k, mounted, frame / n);
        case Anim::Walk: return walk(k, mounted, frame / n);
        case Anim::Attack: return attack(k, mounted, (frame + 0.5f) / n);
        case Anim::Flinch: return flinch(k, mounted, (frame + 0.5f) / n);
        case Anim::Die: return die(k, mounted, frame / (n - 1));
    }
    return stance(k, mounted);
}

// Where a person's joints are, for the pose.
struct Skeleton {
    V3 pelvis, chest, neck, head;
    V3 f, l, u;        // torso axes (forward, left, up)
    V3 hf, hu;         // head forward, up
    V3 hip[2], knee[2], ankle[2];
    V3 shoulder[2], elbow[2], hand[2], fore[2];  // fore: forearm direction
    float s = 1;       // metres per man-unit
};

void weaponMesh(Builder& b, const Kit& k, const Pose& p, const Skeleton& sk) {
    const float s = sk.s;
    const V3 g = sk.hand[0], fa = sk.fore[0];
    const V3 thumb = perpTo(sk.u, fa, sk.f * -1.0f);
    const V3 w = (thumb * std::cos(p.rWr) + fa * std::sin(p.rWr)).normalized();
    const V3 side = w.cross(fa).normalized().length() > 0.1f ? w.cross(fa).normalized() : sk.l;
    const V3 edge = side.cross(w).normalized();
    switch (k.weapon) {
        case Weapon::None: break;
        case Weapon::Sword:
            b.limb(g - w * (0.07f * s), g + w * (0.05f * s), 0.016f * s, 0.016f * s, kDarkLeather, 4);
            b.box(g + w * (0.06f * s), w, side, edge, V3{0.012f, 0.1f, 0.02f} * s, kGold);
            b.box(g + w * (0.42f * s), w, edge, side, V3{0.36f, 0.032f, 0.008f} * s, kSteel * 1.1f);
            b.ball(g - w * (0.08f * s), 0.025f * s, kGold, 5);
            break;
        case Weapon::Axe:
            b.limb(g - w * (0.15f * s), g + w * (0.7f * s), 0.02f * s, 0.02f * s, kWood, 5);
            b.box(g + w * (0.6f * s) + edge * (0.08f * s), w, edge, side, V3{0.1f, 0.09f, 0.014f} * s, kSteel);
            break;
        case Weapon::Pike: case Weapon::Spear: case Weapon::Trident: {
            const float len = k.weapon == Weapon::Pike ? 3.3f : 2.1f;
            b.limb(g - w * (0.9f * s), g + w * (len * s - 0.9f * s), 0.02f * s, 0.017f * s, kWood, 5);
            const V3 tip = g + w * (len * s - 0.9f * s);
            if (k.weapon == Weapon::Trident) {
                b.box(tip, w, side, edge, V3{0.015f, 0.14f, 0.015f} * s, kDarkSteel);
                for (int i = -1; i <= 1; ++i) b.cone(tip + side * (0.12f * s * i), tip + side * (0.13f * s * i) + w * (0.32f * s), 0.018f * s, kDarkSteel, 4);
            } else {
                b.cone(tip, tip + w * (0.3f * s), 0.04f * s, kSteel, 4);
            }
            break;
        }
        case Weapon::Lance:
            b.limb(g - w * (0.7f * s), g + w * (2.3f * s), 0.05f * s, 0.014f * s, k.team * 0.8f, 6);
            b.cone(g + w * (0.25f * s), g - w * (0.05f * s), 0.11f * s, kSteel, 7);
            break;
        case Weapon::Bow: break;  // in the other hand, below
        case Weapon::Staff: {
            b.limb(g - w * (0.85f * s), g + w * (0.95f * s), 0.022f * s, 0.018f * s, kWood, 5);
            const V3 orb = g + w * (1.02f * s);
            b.ball(orb, (0.07f + 0.02f * p.orb) * s, p.orb > 0.6f ? glow(V3{1.0f, 0.55f + 0.1f * p.orb, 0.15f}) : V3{0.95f, 0.5f, 0.15f}, 6);
            break;
        }
        case Weapon::Sickle: {
            b.limb(g - w * (0.05f * s), g + w * (0.18f * s), 0.015f * s, 0.015f * s, kWood, 4);
            V3 prev = g + w * (0.18f * s);
            for (int i = 1; i <= 4; ++i) {
                const float a = 0.5f * i;
                const V3 q = g + w * ((0.18f + 0.12f * std::sin(a)) * s) + edge * ((0.12f * (1 - std::cos(a))) * s);
                b.limb(prev, q, 0.012f * s, 0.009f * s, kSteel, 4);
                prev = q;
            }
            break;
        }
        case Weapon::Satchel:
            if (p.held > 0.5f) {
                b.ellipsoid(g - sk.u * (0.06f * s), sk.f, sk.l, sk.u, V3{0.09f, 0.08f, 0.08f} * s, V3{0.42f, 0.33f, 0.18f});
                b.ball(g + sk.u * (0.04f * s), 0.025f * s, glow(V3{1.0f, 0.8f, 0.3f}), 4);
            }
            break;
        case Weapon::Club:
            b.limb(g - w * (0.12f * s), g + w * (0.95f * s), 0.045f * s, 0.11f * s, V3{0.38f, 0.26f, 0.14f}, 6);
            break;
        case Weapon::Greatsword:
            b.limb(g - w * (0.22f * s), g + w * (0.06f * s), 0.018f * s, 0.018f * s, kDarkLeather, 4);
            b.box(g + w * (0.07f * s), w, side, edge, V3{0.015f, 0.16f, 0.025f} * s, kDarkSteel);
            b.box(g + w * (0.72f * s), w, edge, side, V3{0.64f, 0.045f, 0.01f} * s, kSteel * 1.1f);
            break;
    }
}

void bowMesh(Builder& b, const Pose& p, const Skeleton& sk) {
    const float s = sk.s;
    const V3 g = sk.hand[1];
    // The bow stands in the plane of the arm and the torso's up, bellying away from the archer.
    const V3 away = sk.fore[1];
    const V3 up = perpTo(sk.u, away, sk.f);
    const float bend = 0.12f + 0.12f * p.draw;
    V3 prev{};
    V3 tips[2];
    for (int i = 0; i <= 6; ++i) {
        const float y = -1.0f + 2.0f * i / 6.0f;
        const V3 q = g + up * (0.62f * s * y) + away * ((bend * (1 - y * y) - bend * 0.7f) * s);
        if (i > 0) b.limb(prev, q, 0.016f * s, 0.016f * s, V3{0.34f, 0.22f, 0.12f}, 4);
        prev = q;
        if (i == 0) tips[0] = q;
        if (i == 6) tips[1] = q;
    }
    const V3 nock = p.draw > 0.05f ? sk.hand[0] : (tips[0] + tips[1]) * 0.5f - away * (0.05f * s);
    b.limb(tips[0], nock, 0.004f * s, 0.004f * s, grey(0.85f), 3);
    b.limb(nock, tips[1], 0.004f * s, 0.004f * s, grey(0.85f), 3);
    if (p.draw > 0.05f && p.held > 0.5f) {
        const V3 dir = (g - nock).normalized();
        b.limb(nock, g + dir * (0.12f * s), 0.008f * s, 0.008f * s, kWood, 3);
        b.cone(g + dir * (0.12f * s), g + dir * (0.2f * s), 0.018f * s, kSteel, 3);
    }
}

void headMesh(Builder& b, const Kit& k, const Skeleton& sk) {
    const float s = sk.s, r = 0.115f * s * k.headSize;
    const V3 c = sk.head, f = sk.hf, u = sk.hu, l = u.cross(f).normalized();
    const auto face = [&](V3 col) {
        b.ellipsoid(c + f * (0.035f * s) - u * (0.015f * s), f, l, u, V3{0.8f, 0.85f, 0.85f} * r, col, 7, 4);
        b.ball(c + f * (r * 0.95f), r * 0.17f, col * 0.9f, 4);  // nose
    };
    switch (k.head) {
        case Head::Bare: case Head::Wild: case Head::Crown: case Head::Horns:
            b.ellipsoid(c, f, l, u, {r, r * 0.9f, r}, k.skin);
            b.ellipsoid(c - f * (0.02f * s) + u * (0.025f * s), f, l, u, V3{0.95f, 0.95f, 0.85f} * r, k.hair);
            face(k.skin);
            if (k.head == Head::Wild) {
                for (int i = 0; i < 5; ++i) {
                    const V3 d = rot(u, f, -0.9f + 0.45f * i);
                    b.cone(c + d * (r * 0.7f), c + d * (r * 1.7f) - f * (r * 0.5f), r * 0.3f, k.hair, 4);
                }
            }
            if (k.head == Head::Crown) {
                b.limb(c + u * (r * 0.55f), c + u * (r * 0.95f), r * 0.98f, r * 1.02f, kGold, 8);
                for (int i = 0; i < 5; ++i) {
                    const float a = 2 * kPi * i / 5;
                    const V3 e = f * std::cos(a) + l * std::sin(a);
                    b.cone(c + u * (r * 0.9f) + e * r, c + u * (r * 1.4f) + e * r, r * 0.18f, kGold, 4);
                }
            }
            if (k.head == Head::Horns) {
                for (int sd : {-1, 1}) {
                    const V3 root = c + u * (r * 0.6f) + l * (r * 0.55f * sd);
                    const V3 mid = root + u * (r * 0.6f) + l * (r * 0.35f * sd) - f * (r * 0.15f);
                    b.limb(root, mid, r * 0.2f, r * 0.12f, grey(0.12f), 5);
                    b.cone(mid, mid + u * (r * 0.4f) - f * (r * 0.4f), r * 0.12f, grey(0.1f), 5);
                }
            }
            break;
        case Head::Helmet:
            face(k.skin);
            b.ellipsoid(c + u * (0.02f * s) - f * (0.01f * s), f, l, u, V3{1.07f, 1.05f, 1.0f} * r, kSteel);
            b.box(c + f * (r * 1.02f) + u * (r * 0.05f), f, l, u, V3{0.012f, 0.012f, 0.045f} * s, kSteel);
            b.limb(c - u * (r * 0.05f) - f * (r * 0.1f), c + u * (r * 0.1f) - f * (r * 0.1f), r * 1.1f, r * 1.1f, kDarkSteel, 8);
            break;
        case Head::Hood:
            face(k.skin);
            b.ellipsoid(c - f * (0.025f * s) + u * (0.01f * s), f, l, u, V3{1.1f, 1.12f, 1.12f} * r, k.team * 0.55f);
            b.ellipsoid(c - u * (r * 0.95f) - f * (r * 0.15f), f, l, u, V3{1.3f, 1.8f, 0.45f} * r, k.team * 0.5f);
            break;
        case Head::Bull: {
            b.ellipsoid(c, f, l, u, {r * 1.15f, r * 1.0f, r * 1.05f}, k.skin);
            b.ellipsoid(c + f * (r * 0.95f) - u * (r * 0.35f), f, l, u, {r * 0.75f, r * 0.62f, r * 0.55f}, k.skin * 0.75f);
            b.ball(c + f * (r * 0.35f) + u * (r * 0.3f) + l * (r * 0.5f), r * 0.12f, glow(V3{0.6f, 0.1f, 0.05f}) , 4);
            b.ball(c + f * (r * 0.35f) + u * (r * 0.3f) - l * (r * 0.5f), r * 0.12f, glow(V3{0.6f, 0.1f, 0.05f}), 4);
            for (int sd : {-1, 1}) {
                const V3 root = c + u * (r * 0.55f) + l * (r * 0.7f * sd);
                const V3 mid = root + l * (r * 0.75f * sd) + u * (r * 0.1f);
                b.limb(root, mid, r * 0.22f, r * 0.15f, kBone, 5);
                b.cone(mid, mid + u * (r * 0.7f) + f * (r * 0.2f) + l * (r * 0.1f * sd), r * 0.15f, kBone * 1.05f, 5);
                b.plate(c + l * (r * 0.9f * sd) + u * (r * 0.2f), c + l * (r * 1.4f * sd) + u * (r * 0.3f) - f * (r * 0.2f),
                        c + l * (r * 1.3f * sd), c + l * (r * 0.9f * sd), k.skin * 0.8f);
            }
            break;
        }
        case Head::WizardHat:
            b.ellipsoid(c, f, l, u, {r, r * 0.9f, r}, k.skin);
            face(k.skin);
            b.ellipsoid(c + f * (r * 0.6f) - u * (r * 0.75f), f, l, u, V3{0.45f, 0.6f, 0.8f} * r, grey(0.82f));  // beard
            b.limb(c + u * (r * 0.55f), c + u * (r * 0.62f), r * 2.0f, r * 1.9f, k.team * 0.45f, 9);
            b.cone(c + u * (r * 0.6f), c + u * (r * 3.4f) - f * (r * 0.8f), r * 1.0f, k.team * 0.5f, 8);
            break;
        case Head::Skull:
            b.ellipsoid(c, f, l, u, {r, r * 0.88f, r * 1.02f}, kBone);
            b.ellipsoid(c + f * (r * 0.45f) - u * (r * 0.55f), f, l, u, V3{0.5f, 0.6f, 0.35f} * r, kBone * 0.9f);
            for (int sd : {-1, 1})
                b.ball(c + f * (r * 0.8f) + l * (r * 0.38f * sd) + u * (r * 0.05f), r * 0.24f,
                       k.glowEyes ? glow(V3{0.3f, 0.9f, 0.4f}) : grey(0.05f), 4);
            break;
        case Head::Halo:
            b.ellipsoid(c, f, l, u, {r, r * 0.9f, r}, k.skin);
            b.ellipsoid(c - f * (0.02f * s) + u * (0.03f * s), f, l, u, V3{1.0f, 1.0f, 0.9f} * r, V3{0.85f, 0.72f, 0.4f});
            face(k.skin);
            for (int i = 0; i < 10; ++i) {
                const float a0 = 2 * kPi * i / 10, a1 = 2 * kPi * (i + 1) / 10;
                const V3 hc = c + u * (r * 1.6f) - f * (r * 0.3f);
                b.limb(hc + (f * std::cos(a0) + l * std::sin(a0)) * (r * 0.95f), hc + (f * std::cos(a1) + l * std::sin(a1)) * (r * 0.95f),
                       r * 0.1f, r * 0.1f, glow(kGold), 3);
            }
            break;
        case Head::DwarfHelm:
            face(k.skin);
            b.ellipsoid(c + u * (0.025f * s), f, l, u, V3{1.08f, 1.08f, 1.0f} * r, kSteel);
            b.limb(c - u * (r * 0.02f), c + u * (r * 0.12f), r * 1.12f, r * 1.12f, kGold * 0.8f, 8);
            b.ellipsoid(c + f * (r * 0.7f) - u * (r * 0.85f), f, l, u, V3{0.55f, 0.85f, 1.05f} * r, V3{0.62f, 0.32f, 0.12f});
            break;
        case Head::Goblin:
            b.ellipsoid(c, f, l, u, {r * 1.05f, r * 0.95f, r * 0.9f}, k.skin);
            b.cone(c + f * (r * 0.7f), c + f * (r * 1.6f) - u * (r * 0.2f), r * 0.25f, k.skin * 0.9f, 4);
            for (int sd : {-1, 1}) b.cone(c + l * (r * 0.8f * sd), c + l * (r * 2.0f * sd) + u * (r * 0.5f) - f * (r * 0.3f), r * 0.3f, k.skin * 0.85f, 4);
            for (int sd : {-1, 1}) b.ball(c + f * (r * 0.85f) + l * (r * 0.35f * sd) + u * (r * 0.15f), r * 0.14f, glow(V3{0.9f, 0.8f, 0.2f}), 4);
            break;
    }
}

void wing(Builder& b, V3 root, V3 f, V3 l, V3 u, float sd, float span, float flap, V3 col, bool bat) {
    // Two segments rising and spreading from the shoulder blades; flap lifts both.
    const V3 out = (l * sd * std::cos(flap) + u * std::sin(flap)).normalized();
    const V3 elbow = root + out * (span * 0.45f) - f * (span * 0.08f);
    const V3 out2 = (l * sd * std::cos(flap * 1.4f - 0.2f) + u * std::sin(flap * 1.4f - 0.2f)).normalized();
    const V3 tip = elbow + out2 * (span * 0.55f) - f * (span * 0.2f);
    const float chord = span * 0.32f;
    const V3 back = (f * -1.0f - u * 0.35f).normalized();
    if (bat) {
        b.limb(root, elbow, span * 0.025f, span * 0.02f, col * 0.6f, 4);
        b.limb(elbow, tip, span * 0.02f, span * 0.01f, col * 0.6f, 4);
        const V3 f1 = elbow + back * (chord * 1.1f) - out2 * (chord * 0.2f);
        const V3 f2 = tip + back * (chord * 0.6f) - out2 * (chord * 0.3f);
        b.plate(root, elbow, f1, root + back * (chord * 0.9f), col);
        b.plate(elbow, tip, f2, f1, col * 0.9f);
    } else {
        b.plate(root, elbow, elbow + back * chord, root + back * (chord * 0.8f), col);
        b.plate(elbow, tip, tip + back * (chord * 0.5f), elbow + back * chord, col * 0.95f);
        // Primaries: a ragged trailing edge.
        for (int i = 0; i < 4; ++i) {
            const float t = 0.25f + 0.25f * i;
            const V3 a = lerp(elbow, tip, t), c = a + back * (chord * (0.9f - 0.35f * t));
            b.flat(a - out2 * (span * 0.05f), a + out2 * (span * 0.05f), c + back * (chord * 0.25f), col * 0.88f);
        }
    }
}

// A person, posed. With `seat` set they sit a mount there (legs astride).
Skeleton person(Builder& b, const Kit& k, const Pose& p, const V3* seat) {
    // Natural proportions in man-units (a 1.8 m man is 1.8), scaled to the kit's height.
    const float footH = 0.07f, shin = 0.4f * k.legs, thigh = 0.41f * k.legs, torso = 0.5f, neck = 0.08f;
    const float headR = 0.115f * k.headSize;
    const float natural = footH + shin + thigh + torso + neck + headR * 2.05f;
    Skeleton sk;
    const float s = sk.s = (seat ? k.height : k.height) / natural;
    const float bw = k.bulk;
    const V3 F{1, 0, 0}, L{0, 1, 0}, U{0, 0, 1};
    // Torso frame.
    const V3 f1 = rot(F, U, p.twist), l1 = rot(L, U, p.twist);
    const V3 up1 = rot(U, l1, p.lean), f2 = rot(f1, l1, p.lean);
    sk.u = rot(up1, f2, p.side);
    sk.l = rot(l1, f2, p.side);
    sk.f = f2;
    V3 P = seat ? *seat : V3{0, 0, (footH + shin + thigh) * s};
    // Legs (they follow the hips, not the shoulders).
    float lowest = 1e9f;
    for (int i = 0; i < 2; ++i) {
        const float sd = i == 0 ? -1.0f : 1.0f;  // 0 right, 1 left
        const float hipA = i == 0 ? p.rHip : p.lHip, kneeA = i == 0 ? p.rKnee : p.lKnee;
        sk.hip[i] = P + L * (sd * 0.1f * s * bw);
        V3 d = (U * -std::cos(hipA) + F * std::sin(hipA)).normalized();
        d = (d + L * (sd * std::sin(p.hipOut))).normalized();
        sk.knee[i] = sk.hip[i] + d * (thigh * s);
        const V3 pf = perpTo(F, d, U);
        const V3 sd2 = (d * std::cos(kneeA) - pf * std::sin(kneeA)).normalized();
        sk.ankle[i] = sk.knee[i] + sd2 * (shin * s);
        lowest = std::min(lowest, sk.ankle[i].z - footH * s);
    }
    if (!seat) {
        const V3 drop{0, 0, -lowest};
        P = P + drop;
        for (int i = 0; i < 2; ++i) sk.hip[i] = sk.hip[i] + drop, sk.knee[i] = sk.knee[i] + drop, sk.ankle[i] = sk.ankle[i] + drop;
    }
    sk.pelvis = P;
    sk.chest = P + sk.u * (torso * s);
    sk.neck = sk.chest + sk.u * (neck * s);
    sk.hu = rot(sk.u, sk.l, p.nod);
    sk.hf = rot(sk.f, sk.l, p.nod);
    sk.head = sk.neck + sk.hu * (headR * s * 0.95f);
    for (int i = 0; i < 2; ++i) {
        const float sd = i == 0 ? -1.0f : 1.0f;
        const float sh = i == 0 ? p.rSh : p.lSh, out = i == 0 ? p.rOut : p.lOut, el = i == 0 ? p.rEl : p.lEl;
        sk.shoulder[i] = sk.chest + sk.l * (sd * 0.2f * s * bw) - sk.u * (0.04f * s);
        V3 ua = (sk.u * -std::cos(sh) + sk.f * std::sin(sh)).normalized();
        ua = (ua * std::cos(out) + sk.l * (sd * std::sin(out))).normalized();
        sk.elbow[i] = sk.shoulder[i] + ua * (0.29f * s);
        const V3 perp = perpTo(sk.f, ua, sk.u);
        sk.fore[i] = (ua * std::cos(el) + perp * std::sin(el)).normalized();
        sk.hand[i] = sk.elbow[i] + sk.fore[i] * (0.27f * s);
    }

    // Behind first: wings, cape, tail, quiver.
    const V3 back = sk.chest - sk.f * (0.1f * s * bw) - sk.u * (0.06f * s);
    if (k.wings)
        for (float sd : {-1.0f, 1.0f}) wing(b, back + sk.l * (0.07f * s * sd), sk.f, sk.l, sk.u, sd, 1.25f * s, p.wing * 0.8f, k.wing, k.batWings);
    if (k.cape) {
        const V3 c0 = sk.chest - sk.f * (0.13f * s * bw), sway = sk.f * (-p.cape * 0.5f * s);
        const V3 low = V3{P.x, P.y, P.z * 0.35f} - sk.f * (0.25f * s) + sway;
        b.plate(c0 + sk.l * (0.2f * s * bw), c0 - sk.l * (0.2f * s * bw), low - sk.l * (0.3f * s), low + sk.l * (0.3f * s), k.team * 0.7f);
    }
    if (k.tail) {
        V3 prev = P - sk.f * (0.12f * s);
        for (int i = 1; i <= 5; ++i) {
            const float a = 0.5f * i + p.cape * 2.0f;
            const V3 q = P - sk.f * ((0.12f + 0.16f * i) * s) + V3{0, 0, (-0.3f + 0.12f * std::cos(a) * i) * s} + sk.l * (0.05f * std::sin(a) * s);
            b.limb(prev, q, (0.045f - 0.006f * i) * s, (0.04f - 0.006f * i) * s, k.skin * 0.8f, 4);
            prev = q;
        }
        b.cone(prev, prev - sk.f * (0.1f * s) + V3{0, 0, 0.06f * s}, 0.05f * s, k.skin * 0.6f, 4);
    }
    if (k.quiver) {
        const V3 q0 = sk.chest - sk.f * (0.13f * s) + sk.l * (0.08f * s), q1 = P - sk.f * (0.12f * s) - sk.l * (0.1f * s) + sk.u * (0.1f * s);
        b.limb(q1, q0, 0.05f * s, 0.055f * s, kLeather, 5);
        for (int i = 0; i < 3; ++i) b.limb(q0, q0 + (q0 - q1).normalized() * (0.12f * s) + sk.l * (0.02f * s * (i - 1)), 0.006f * s, 0.006f * s, grey(0.85f), 3);
    }

    // Legs and feet.
    const V3 legC = k.bones ? kBone : (k.bareChest && k.kilt ? k.skin : k.trousers);
    const float limbR = (k.bones ? 0.035f : 0.076f) * s * bw;
    for (int i = 0; i < 2; ++i) {
        if (!k.robe || seat) {
            b.limb(sk.hip[i], sk.knee[i], limbR * 1.15f, limbR * 0.9f, legC);
            b.ball(sk.knee[i], limbR * 0.9f, legC, 5);
            b.limb(sk.knee[i], sk.ankle[i], limbR * 0.9f, limbR * 0.75f, k.bones ? kBone : k.boots);
        }
        const V3 toe = rot(F, U, (i == 0 ? -0.15f : 0.15f));
        b.box(sk.ankle[i] + toe * (0.06f * s) - U * (footH * s * 0.5f), toe, L, U, V3{0.12f, 0.05f * bw, footH * 0.5f} * s, k.bones ? kBone : k.boots);
    }
    // Body.
    const V3 waist = P + sk.u * (0.08f * s);
    if (k.robe && !seat) {
        const V3 hem = (sk.ankle[0] + sk.ankle[1]) * 0.5f - V3{0, 0, footH * s * 0.5f};
        b.limb(waist, hem + V3{0, 0, 0.02f * s}, 0.17f * s * bw, 0.34f * s * bw, k.tunic * 0.9f, 9);
    }
    if (k.kilt) {
        // Bare-chested troops wear their side's colours: a kilt and a sash.
        b.limb(P + sk.u * (0.06f * s), P - sk.u * (0.2f * s), 0.15f * s * bw, 0.2f * s * bw, k.team * 0.9f, 8);
        if (k.bareChest) b.limb(sk.shoulder[1] + sk.u * (0.02f * s), P - sk.l * (0.14f * s * bw) + sk.u * (0.05f * s), 0.05f * s * bw, 0.05f * s * bw, k.team, 5);
    }
    if (k.bones) {
        b.limb(P, sk.chest, 0.025f * s, 0.025f * s, kBone, 4);
        for (int i = 0; i < 4; ++i) {
            const V3 rc = P + sk.u * ((0.22f + 0.07f * i) * s);
            b.ellipsoid(rc, sk.f, sk.l, sk.u, V3{0.09f, 0.14f - 0.01f * i, 0.018f} * s, kBone);
        }
        b.ellipsoid(P, sk.f, sk.l, sk.u, V3{0.08f, 0.13f, 0.05f} * s, kBone * 0.9f);
    } else {
        const V3 torsoC = k.bareChest ? k.skin : (k.armour ? kSteel : (k.mail ? kDarkSteel * 1.3f : k.tunic));
        b.ellipsoid(P + sk.u * (0.04f * s), sk.f, sk.l, sk.u, V3{0.145f, 0.185f, 0.13f} * (s * bw), k.robe ? k.tunic * 0.9f : legC);
        b.ellipsoid(P + sk.u * (torso * s * 0.58f), sk.f, sk.l, sk.u, V3{0.16f, 0.235f, torso * 0.52f} * (s * bw), torsoC);
        if (k.mail || k.armour) b.limb(waist, waist - sk.u * (0.18f * s), 0.18f * s * bw, 0.21f * s * bw, k.team * 0.85f, 8);  // tabard skirt
        if (k.armour) b.ellipsoid(sk.chest - sk.u * (0.12f * s) + sk.f * (0.06f * s), sk.f, sk.l, sk.u, V3{0.1f, 0.16f, 0.12f} * (s * bw), k.team * 0.8f);
        if (!k.bareChest) b.limb(waist - sk.u * (0.01f * s), waist + sk.u * (0.04f * s), 0.155f * s * bw, 0.155f * s * bw, k.belt, 8);
    }
    b.limb(sk.chest, sk.neck + sk.u * (0.02f * s), 0.05f * s * bw, 0.045f * s, k.bones ? kBone : k.skin, 5);
    if (k.armour || k.mail)
        for (int i = 0; i < 2; ++i) b.ellipsoid(sk.shoulder[i] + sk.u * (0.02f * s), sk.f, sk.l, sk.u, V3{0.09f, 0.1f, 0.07f} * (s * bw), k.armour ? kSteel : kDarkSteel);
    // Arms.
    for (int i = 0; i < 2; ++i) {
        const V3 sleeve = k.bones ? kBone : (k.bareChest || k.armour ? k.skin : k.tunic * 0.92f);
        const float ar = (k.bones ? 0.03f : 0.058f) * s * bw;
        b.limb(sk.shoulder[i], sk.elbow[i], ar, ar * 0.85f, sleeve);
        b.ball(sk.elbow[i], ar * 0.85f, sleeve, 5);
        b.limb(sk.elbow[i], sk.hand[i], ar * 0.85f, ar * 0.7f, k.bones ? kBone : (k.armour ? kSteel : k.skin));
        b.ball(sk.hand[i], ar * 0.95f, k.bones ? kBone : (k.armour ? kDarkSteel : k.skin * 0.95f), 5);
    }
    headMesh(b, k, sk);
    if (p.held > 0.5f || k.weapon == Weapon::Bow) weaponMesh(b, k, p, sk);
    if (k.weapon == Weapon::Bow) bowMesh(b, p, sk);
    if (k.shield != Shield::None) {
        const V3 mid = lerp(sk.elbow[1], sk.hand[1], 0.55f);
        const V3 n = (sk.l * 0.75f + sk.f * 0.65f).normalized();
        const V3 su = perpTo(sk.u, n, sk.f), sl = n.cross(su);
        const V3 c = mid + n * (0.05f * s);
        if (k.shield == Shield::Round) {
            b.limb(c - n * (0.015f * s), c + n * (0.015f * s), 0.27f * s, 0.27f * s, kDarkSteel, 10);
            b.limb(c + n * (0.012f * s), c + n * (0.022f * s), 0.24f * s, 0.24f * s, k.team * 0.85f, 10);
            b.ball(c + n * (0.03f * s), 0.05f * s, kSteel, 5);
        } else {
            b.ellipsoid(c, n, sl, su, V3{0.025f, 0.27f, 0.46f} * s, kDarkSteel, 9, 5);
            b.ellipsoid(c + n * (0.012f * s), n, sl, su, V3{0.02f, 0.245f, 0.42f} * s, k.team * 0.85f, 9, 5);
            b.box(c + n * (0.032f * s), n, sl, su, V3{0.01f, 0.025f, 0.3f} * s, kGold * 0.8f);
        }
    }
    return sk;
}

// ------------------------------------------------------------------ beasts

enum class Beast : uint8_t { Horse, Wolf, Elephant, Griffin, Dragon, Spider };

struct Hide {
    Beast kind = Beast::Horse;
    float height = 1.6f;  // withers (back) height, metres
    V3 coat{0.45f, 0.3f, 0.18f}, mane = kHair, team = grey(0.5f);
    bool horn = false, wings = false, barding = false, saddle = false, cloth = false, collar = false;
    V3 wingCol = grey(0.9f);
};

struct BeastPose {
    float t = 0;          // gait phase 0-1
    float gait = 0;       // how far the legs swing (0 standing)
    float pitch = 0;      // body nose-up (rearing)
    float lunge = 0;      // body forward, metres per metre of height
    float neck = 0;       // neck raise
    float head = 0;       // head nod down
    float jaw = 0;        // mouth open
    float roll = 0;       // falls onto its side
    float wing = 1.1f;    // wings up
    float tail = 0;
    float front = 0;      // front legs raised (rear, strike)
};

BeastPose beastPose(Anim anim, int frame) {
    BeastPose p;
    const float n = static_cast<float>(kFrames[static_cast<int>(anim)]);
    switch (anim) {
        case Anim::Idle: {
            const float t = frame / n, s = std::sin(2 * kPi * t);
            p.neck = 0.05f * s, p.head = 0.08f * std::sin(2 * kPi * t + 1), p.tail = 0.3f * s, p.wing = 0.55f + 0.08f * s;
            break;
        }
        case Anim::Walk:
            p.t = frame / n, p.gait = 1.0f, p.wing = 0.6f + 0.75f * std::sin(2 * kPi * p.t), p.tail = 0.25f * std::sin(2 * kPi * p.t);
            p.pitch = 0.04f * std::sin(4 * kPi * p.t);
            break;
        case Anim::Attack: {
            const float t = (frame + 0.5f) / n;
            const float a = std::sin(kPi * std::min(1.0f, t / 0.55f)), b = t > 0.45f ? std::sin(kPi * std::min(1.0f, (t - 0.45f) / 0.55f)) : 0.0f;
            p.pitch = 0.32f * a - 0.12f * b, p.front = a, p.lunge = 0.25f * b, p.head = -0.3f * a + 0.6f * b, p.jaw = b, p.neck = 0.2f * a - 0.15f * b;
            p.wing = 1.4f * a + 0.2f;
            break;
        }
        case Anim::Flinch: {
            const float a = std::sin(kPi * (0.35f + 0.65f * (frame + 0.5f) / n));
            p.pitch = 0.12f * a, p.neck = 0.35f * a, p.head = -0.3f * a, p.jaw = 0.6f * a, p.wing = 1.6f;
            break;
        }
        case Anim::Die: {
            const float t = frame / (n - 1);
            p.roll = ease(std::clamp((t - 0.1f) / 0.85f, 0.0f, 1.0f)) * (kPi * 0.5f);
            p.neck = -0.5f * t, p.head = 0.4f * t, p.jaw = 0.5f, p.wing = 0.2f;
            p.front = 0.3f * std::sin(kPi * t);
            break;
        }
    }
    return p;
}

// Builds a four-legged beast (or the spider); returns where a rider sits.
V3 beast(Builder& b, const Hide& h, const BeastPose& p, const Kit* riderKit, const Pose* riderPose) {
    const V3 F{1, 0, 0}, L{0, 1, 0}, U{0, 0, 1};
    const float s = h.height;  // metres per withers-height
    struct Dims {
        float bodyZ, bodyX, bodyY, bodyH, shoulderX, hipX, legY, upper, lower, legR, neck, headLen, headR;
    };
    Dims d{};
    switch (h.kind) {
        case Beast::Horse: d = {0.74f, 0.5f, 0.2f, 0.25f, 0.32f, -0.34f, 0.12f, 0.31f, 0.37f, 0.052f, 0.42f, 0.36f, 0.085f}; break;
        case Beast::Wolf: d = {0.7f, 0.52f, 0.2f, 0.22f, 0.34f, -0.36f, 0.12f, 0.3f, 0.34f, 0.055f, 0.25f, 0.33f, 0.11f}; break;
        case Beast::Elephant: d = {0.66f, 0.48f, 0.27f, 0.32f, 0.3f, -0.3f, 0.16f, 0.27f, 0.3f, 0.09f, 0.12f, 0.3f, 0.2f}; break;
        case Beast::Griffin: d = {0.66f, 0.52f, 0.22f, 0.23f, 0.32f, -0.36f, 0.13f, 0.28f, 0.33f, 0.06f, 0.3f, 0.2f, 0.11f}; break;
        case Beast::Dragon: d = {0.6f, 0.75f, 0.28f, 0.27f, 0.42f, -0.42f, 0.2f, 0.28f, 0.3f, 0.07f, 0.75f, 0.42f, 0.11f}; break;
        case Beast::Spider: d = {0.6f, 0.3f, 0.26f, 0.2f, 0.25f, -0.4f, 0.2f, 0.5f, 0.6f, 0.03f, 0, 0, 0}; break;
    }
    // Body frame, pitched about the hips when rearing.
    const V3 fb = rot(F, L, -p.pitch), ub = rot(U, L, -p.pitch);
    V3 centre{p.lunge * s, 0, d.bodyZ * s};
    const auto at = [&](float x, float y, float z) { return centre + fb * (x * s) + L * (y * s) + ub * (z * s); };

    if (h.kind == Beast::Spider) {
        // Eight legs from the cephalothorax, in two alternating sets of four.
        const V3 ceph = at(0.25f, 0, 0), abd = at(-0.35f, 0, 0.12f);
        float lowest = 1e9f;
        struct Leg { V3 root, knee, foot; };
        Leg legs[8];
        for (int i = 0; i < 8; ++i) {
            const int pair = i % 4;
            const float sd = i < 4 ? 1.0f : -1.0f;
            const float spread = 0.95f - 0.55f * pair;
            const V3 out = (F * std::sin(spread) + L * (sd * std::cos(spread))).normalized();
            const float phase = 2 * kPi * (p.t + ((pair + (i < 4 ? 0 : 1)) % 2) * 0.5f);
            const float swing = 0.18f * p.gait * std::sin(phase), lift = 0.18f * p.gait * std::max(0.0f, std::cos(phase));
            float raise = (pair == 0 ? p.front : 0.0f);
            legs[i].root = ceph + out * (0.18f * s);
            legs[i].foot = V3{ceph.x, ceph.y, 0} + out * (1.05f * s) + F * (swing * s) + U * ((lift + raise * 0.6f) * s);
            if (p.roll > 0) legs[i].foot.z += 0.4f * s * std::sin(p.roll * 2.0f + i);
            legs[i].knee = (legs[i].root + legs[i].foot) * 0.5f + out * (0.05f * s) + U * ((0.45f + raise * 0.3f) * s);
            lowest = std::min(lowest, legs[i].foot.z);
        }
        for (const Leg& g : legs) {
            b.bone(g.root, g.knee, 0.045f * s, 0.035f * s, h.coat * 0.9f);
            b.limb(g.knee, g.foot, 0.035f * s, 0.012f * s, h.coat * 0.75f);
        }
        b.ellipsoid(ceph, fb, L, ub, V3{0.3f, 0.26f, 0.2f} * s, h.coat);
        b.ellipsoid(abd, fb, L, ub, V3{0.48f, 0.42f, 0.38f} * s, h.coat * 1.1f);
        b.ellipsoid(abd + ub * (0.3f * s) - fb * (0.05f * s), fb, L, ub, V3{0.2f, 0.1f, 0.12f} * s, h.team * 0.8f);  // markings
        for (int i = 0; i < 4; ++i)
            b.ball(ceph + fb * (0.27f * s) + ub * (0.07f * s) + L * ((i - 1.5f) * 0.05f * s), 0.03f * s, glow(V3{0.9f, 0.15f, 0.1f}), 4);
        for (float sd : {-1.0f, 1.0f})
            b.cone(ceph + fb * (0.25f * s) + L * (0.06f * s * sd) - ub * (0.05f * s), ceph + fb * (0.38f * s) + L * (0.04f * s * sd) - ub * ((0.18f - 0.1f * p.jaw) * s), 0.03f * s, grey(0.1f), 4);
        return centre;
    }

    // Legs: front pair and hind pair, a trot or gallop.
    struct Leg { V3 hip, knee, foot; };
    Leg legs[4];
    float lowest = 1e9f;
    const float phases[4] = {0.0f, 0.5f, 0.55f, 0.05f};  // left fore, right fore, left hind, right hind
    for (int i = 0; i < 4; ++i) {
        const bool fore = i < 2;
        const float sd = (i % 2 == 0) ? 1.0f : -1.0f;
        const float c = 2 * kPi * (p.t + phases[i]);
        float swing = 0.45f * p.gait * std::sin(c), bend = (fore ? 0.05f : 0.6f) + 0.9f * p.gait * std::max(0.0f, std::cos(c));
        float upper0 = fore ? 0.0f : -0.3f;
        if (fore && p.front > 0) swing += 1.1f * p.front, bend += 1.0f * p.front;
        if (h.kind == Beast::Elephant) bend *= 0.4f, swing *= 0.6f;
        legs[i].hip = at(fore ? d.shoulderX : d.hipX, sd * d.legY, -d.bodyH * 0.3f);
        const V3 ud = rot(U * -1.0f, L, -(swing + upper0));
        legs[i].knee = legs[i].hip + ud * (d.upper * s);
        const V3 ld = rot(ud, L, fore ? bend : -bend + 0.9f);
        legs[i].foot = legs[i].knee + ld * (d.lower * s);
        lowest = std::min(lowest, legs[i].foot.z);
    }
    // Stand on the ground (unless falling).
    const float drop = p.roll > 0 ? 0.0f : -lowest + 0.02f * s;
    centre.z += drop;
    for (Leg& g : legs) g.hip.z += drop, g.knee.z += drop, g.foot.z += drop;
    const V3 legCol = h.kind == Beast::Griffin ? h.coat * 0.85f : h.coat * 0.9f;
    for (int i = 0; i < 4; ++i) {
        const Leg& g = legs[i];
        const bool talon = h.kind == Beast::Griffin && i < 2;
        const float r = d.legR * s;
        b.limb(g.hip, g.knee, r * 1.5f, r, i % 2 == 0 ? legCol : legCol * 0.85f);
        b.ball(g.knee, r, legCol, 5);
        b.limb(g.knee, g.foot, r, r * (h.kind == Beast::Elephant ? 1.1f : 0.75f), talon ? V3{0.8f, 0.65f, 0.25f} : legCol * 0.9f);
        if (h.kind == Beast::Horse) b.limb(g.foot, g.foot - U * (0.03f * s), r * 0.95f, r * 1.05f, grey(0.15f), 5);
        if (talon || h.kind == Beast::Dragon)
            for (int t = -1; t <= 1; ++t) b.cone(g.foot, g.foot + rot(F, U, 0.4f * t) * (0.08f * s) - U * (0.02f * s), r * 0.35f, grey(0.15f), 3);
    }
    // Body.
    const V3 body = at(0, 0, 0);
    if (h.kind == Beast::Griffin) {
        b.ellipsoid(at(-0.12f, 0, 0), fb, L, ub, V3{d.bodyX * 0.75f, d.bodyY, d.bodyH} * s, h.coat);
        b.ellipsoid(at(0.22f, 0, 0.04f), fb, L, ub, V3{d.bodyX * 0.5f, d.bodyY * 1.05f, d.bodyH * 1.1f} * s, h.mane);
    } else {
        b.ellipsoid(body, fb, L, ub, V3{d.bodyX, d.bodyY, d.bodyH} * s, h.coat, 10, 6);
    }
    if (h.barding) b.ellipsoid(at(0, 0, -0.02f), fb, L, ub, V3{d.bodyX * 1.02f, d.bodyY * 1.06f, d.bodyH * 0.92f} * s, h.team * 0.75f, 10, 4);
    if (h.collar) {
        // The side's colours: a collar at the withers and a streamer.
        b.limb(at(d.bodyX * 0.62f, 0, -d.bodyH * 0.1f), at(d.bodyX * 0.8f, 0, d.bodyH * 0.9f), d.bodyY * 0.95f * s, d.bodyY * 0.75f * s, h.team, 8);
        b.plate(at(d.bodyX * 0.7f, 0, d.bodyH * 1.0f), at(d.bodyX * 0.55f, 0, d.bodyH * 1.05f), at(d.bodyX * 0.1f, d.bodyY * 0.3f, d.bodyH * 1.1f), at(d.bodyX * 0.2f, d.bodyY * 0.25f, d.bodyH * 0.95f), h.team);
    }
    if (h.saddle) b.ellipsoid(at(0.02f, 0, d.bodyH * 0.8f), fb, L, ub, V3{0.12f, 0.16f, 0.05f} * s, kLeather);
    if (h.cloth) {
        b.ellipsoid(at(0.0f, 0, d.bodyH * 0.55f), fb, L, ub, V3{d.bodyX * 0.55f, d.bodyY * 1.08f, d.bodyH * 0.55f} * s, h.team * 0.8f, 10, 4);
        b.box(at(0.0f, 0, d.bodyH * 1.05f), fb, L, ub, V3{0.15f, 0.12f, 0.06f} * s, kWood);
    }
    // Tail.
    {
        V3 prev = at(-d.bodyX * 0.95f, 0, d.bodyH * 0.4f);
        const int n = h.kind == Beast::Dragon ? 7 : 4;
        const float len = h.kind == Beast::Dragon ? 0.28f : (h.kind == Beast::Elephant ? 0.12f : 0.14f);
        for (int i = 1; i <= n; ++i) {
            const float a = p.tail * std::sin(0.8f * i + p.t * 2 * kPi);
            const V3 dir = rot(rot(F * -1.0f, L, h.kind == Beast::Horse ? -0.9f : (h.kind == Beast::Dragon ? -0.15f + 0.05f * i : -0.5f)), U, a);
            const V3 q = prev + dir * (len * s);
            const float r0 = h.kind == Beast::Dragon ? (0.13f - 0.016f * i) * s : (h.kind == Beast::Horse ? 0.035f * s : 0.025f * s);
            b.limb(prev, q, r0, r0 * 0.85f, h.kind == Beast::Horse ? h.mane : h.coat * 0.9f, 5);
            if (h.kind == Beast::Dragon && i < n) b.cone(q + U * (r0 * 0.6f), q + U * (r0 * 1.8f) - F * (r0 * 0.6f), r0 * 0.35f, h.mane, 3);
            prev = q;
        }
        if (h.kind == Beast::Griffin || h.kind == Beast::Elephant) b.ball(prev, 0.04f * s, h.mane, 4);
        if (h.kind == Beast::Dragon) b.cone(prev, prev + (prev - at(-d.bodyX, 0, 0)).normalized() * (0.15f * s), 0.06f * s, h.mane, 3);
    }
    // Wings.
    if (h.wings) {
        const float span = h.kind == Beast::Dragon ? 1.9f * s : 1.5f * s;
        for (float sd : {-1.0f, 1.0f})
            wing(b, at(0.15f, sd * d.bodyY * 0.6f, d.bodyH * 0.75f), fb, L, ub, sd, span, p.wing, h.wingCol, h.kind == Beast::Dragon);
    }
    // Neck and head.
    const V3 base = at(d.bodyX * 0.75f, 0, d.bodyH * 0.45f);
    V3 headAt, hf, hu;
    if (h.kind == Beast::Elephant) {
        headAt = at(d.bodyX + 0.12f, 0, d.bodyH * 0.6f + p.neck * 0.2f);
        hf = rot(fb, L, 0.3f + p.head * 0.5f);
        hu = rot(ub, L, 0.3f + p.head * 0.5f);
        b.ellipsoid(headAt, hf, L, hu, V3{0.22f, 0.2f, 0.24f} * s, h.coat * 1.02f);
        for (float sd : {-1.0f, 1.0f}) {  // ears
            const V3 e0 = headAt + L * (0.16f * s * sd) - hf * (0.06f * s);
            b.plate(e0 + hu * (0.15f * s), e0 + hu * (0.18f * s) + L * (0.22f * s * sd) - hf * (0.12f * s),
                    e0 - hu * (0.18f * s) + L * (0.2f * s * sd) - hf * (0.1f * s), e0 - hu * (0.15f * s), h.coat * 0.85f);
            b.cone(headAt + hf * (0.16f * s) + L * (0.09f * s * sd) - hu * (0.12f * s),
                   headAt + hf * (0.42f * s) + L * (0.11f * s * sd) - hu * (0.05f * s), 0.03f * s, kBone * 1.1f, 5);  // tusks
        }
        V3 prev = headAt + hf * (0.18f * s) - hu * (0.06f * s);
        V3 dir = rot(hf, L, 0.9f - p.front * 2.2f);
        for (int i = 0; i < 6; ++i) {
            const V3 q = prev + dir * (0.11f * s);
            b.limb(prev, q, (0.07f - 0.008f * i) * s, (0.065f - 0.008f * i) * s, h.coat * 0.95f, 6);
            prev = q;
            dir = rot(dir, L, 0.22f - p.front * 0.5f);
        }
        for (float sd : {-1.0f, 1.0f}) b.ball(headAt + hf * (0.15f * s) + L * (0.14f * s * sd) + hu * (0.05f * s), 0.025f * s, grey(0.05f), 4);
        return at(0, 0, d.bodyH * 1.1f);
    }
    const int segs = h.kind == Beast::Dragon ? 4 : 1;
    const float neckRise = (h.kind == Beast::Horse || h.kind == Beast::Griffin) ? 0.95f : (h.kind == Beast::Dragon ? 0.7f : 0.45f);
    V3 dir = rot(fb, L, -(neckRise + p.neck));
    V3 prev = base;
    for (int i = 0; i < segs; ++i) {
        const V3 q = prev + dir * (d.neck * s / segs);
        const float r0 = (h.kind == Beast::Dragon ? 0.13f - 0.015f * i : 0.11f) * s, r1 = r0 * 0.8f;
        b.limb(prev, q, r0, r1, h.kind == Beast::Griffin ? h.mane : h.coat, 6);
        if (h.kind == Beast::Dragon) b.cone(q + ub * (r1 * 0.5f), q + ub * (r1 * 1.8f) - fb * (r1 * 0.5f), r1 * 0.4f, h.mane, 3);
        prev = q;
        if (h.kind == Beast::Dragon) dir = rot(dir, L, 0.12f + 0.15f * p.head);
    }
    if (h.kind == Beast::Horse && segs == 1) {
        // Mane along the crest.
        b.limb(base + ub * (0.08f * s) - fb * (0.04f * s), prev + ub * (0.05f * s), 0.035f * s, 0.03f * s, h.mane, 4);
    }
    headAt = prev;
    const float headPitch = (h.kind == Beast::Horse ? 1.1f : (h.kind == Beast::Wolf ? 0.15f : (h.kind == Beast::Dragon ? 0.0f : 0.25f))) + p.head - p.pitch;
    hf = rot(F, L, headPitch);
    hu = rot(U, L, headPitch);
    const float hl = d.headLen * s, hr = d.headR * s;
    switch (h.kind) {
        case Beast::Horse:
            b.limb(headAt, headAt + hf * hl, hr * 1.25f, hr * 0.8f, h.coat, 6);
            b.ball(headAt + hf * hl, hr * 0.85f, h.coat * 0.85f, 5);
            for (float sd : {-1.0f, 1.0f}) b.cone(headAt + L * (0.035f * s * sd) - hf * (0.02f * s), headAt + L * (0.04f * s * sd) - hf * (0.05f * s) + rot(hu, L, -0.4f) * (0.09f * s), 0.02f * s, h.coat * 0.8f, 3);
            if (h.horn) b.cone(headAt + hf * (0.05f * s) + hu * (0.06f * s), headAt + hf * (0.07f * s) + rot(hu, L, -0.9f) * (0.32f * s), 0.022f * s, glow(V3{0.95f, 0.85f, 0.55f}), 5);
            if (h.barding) b.limb(headAt + hu * (0.01f * s), headAt + hf * (hl * 0.7f) + hu * (0.01f * s), hr * 1.32f, hr * 0.95f, kSteel, 6);
            break;
        case Beast::Wolf: {
            b.ellipsoid(headAt + hf * (0.04f * s), hf, L, hu, V3{0.1f, 0.09f, 0.085f} * s, h.coat);
            const V3 snout = headAt + hf * (0.15f * s) - hu * (0.02f * s);
            b.limb(snout - hf * (0.04f * s), snout + hf * (0.1f * s), 0.05f * s, 0.03f * s, h.coat * 0.9f, 5);
            b.limb(snout - hu * (0.035f * s), snout + rot(hf, L, 0.5f * p.jaw + 0.1f) * (0.11f * s) - hu * (0.035f * s), 0.03f * s, 0.02f * s, h.coat * 0.75f, 4);
            for (float sd : {-1.0f, 1.0f}) b.cone(headAt + L * (0.045f * s * sd) + hu * (0.05f * s), headAt + L * (0.05f * s * sd) + hu * (0.13f * s) - hf * (0.02f * s), 0.025f * s, h.coat * 0.85f, 3);
            for (float sd : {-1.0f, 1.0f}) b.ball(headAt + hf * (0.1f * s) + L * (0.04f * s * sd) + hu * (0.025f * s), 0.012f * s, glow(V3{0.95f, 0.75f, 0.2f}), 3);
            break;
        }
        case Beast::Griffin:
            b.ellipsoid(headAt + hf * (0.03f * s), hf, L, hu, V3{0.11f, 0.09f, 0.1f} * s, h.mane);
            b.cone(headAt + hf * (0.1f * s), headAt + hf * (0.22f * s) - hu * (0.07f * s), 0.045f * s, V3{0.85f, 0.65f, 0.2f}, 5);
            for (float sd : {-1.0f, 1.0f}) b.ball(headAt + hf * (0.07f * s) + L * (0.05f * s * sd) + hu * (0.03f * s), 0.014f * s, glow(V3{0.95f, 0.7f, 0.15f}), 3);
            break;
        case Beast::Dragon: {
            b.ellipsoid(headAt + hf * (0.06f * s), hf, L, hu, V3{0.13f, 0.1f, 0.09f} * s, h.coat);
            b.limb(headAt + hf * (0.1f * s), headAt + hf * (hl), 0.07f * s, 0.045f * s, h.coat, 6);
            b.limb(headAt + hf * (0.08f * s) - hu * (0.05f * s), headAt + rot(hf, L, 0.55f * p.jaw + 0.08f) * hl - hu * (0.05f * s), 0.05f * s, 0.03f * s, h.coat * 0.75f, 5);
            if (p.jaw > 0.3f) b.ball(headAt + hf * (hl * 1.05f), 0.06f * s * p.jaw, glow(V3{1.0f, 0.6f, 0.15f}), 5);
            for (float sd : {-1.0f, 1.0f}) {
                b.cone(headAt + L * (0.06f * s * sd) + hu * (0.05f * s), headAt + L * (0.09f * s * sd) + hu * (0.16f * s) - hf * (0.22f * s), 0.03f * s, kBone, 4);
                b.ball(headAt + hf * (0.13f * s) + L * (0.055f * s * sd) + hu * (0.04f * s), 0.018f * s, glow(V3{1.0f, 0.85f, 0.2f}), 3);
            }
            break;
        }
        default: break;
    }
    (void)riderKit;
    (void)riderPose;
    return at(0.02f, 0, d.bodyH * 0.95f);
}

// ------------------------------------------------------------------ the roster

struct Figure {
    bool person = true;
    Kit kit;
    bool mounted = false;
    Hide hide;
};

V3 teamCloth(const float team[3]) {
    // Team colours dulled toward the land's greys, as Myth's were.
    return cloth(team) * 0.85f;
}

Figure figureOf(UnitType type, const float team[3]) {
    Figure fg;
    Kit& k = fg.kit;
    Hide& h = fg.hide;
    k.team = teamCloth(team);
    k.tunic = k.team;
    h.team = k.team;
    k.height = static_cast<float>(sim::unitStats(type).height.toDouble());
    switch (type) {
        case UnitType::LightInfantry:
            k.head = Head::Hood, k.weapon = Weapon::Sword, k.shield = Shield::Round;
            break;
        case UnitType::HeavyInfantry:
            k.head = Head::Helmet, k.weapon = Weapon::Sword, k.shield = Shield::Large, k.mail = true, k.bulk = 1.1f;
            break;
        case UnitType::Archers:
            k.head = Head::Hood, k.weapon = Weapon::Bow, k.strike = Strike::Shoot, k.quiver = true, k.bulk = 0.92f;
            k.trousers = V3{0.28f, 0.3f, 0.2f};
            break;
        case UnitType::Pikemen:
            k.head = Head::Helmet, k.weapon = Weapon::Pike, k.strike = Strike::Thrust, k.mail = true;
            break;
        case UnitType::LightCavalry:
        case UnitType::HeavyCavalry:
        case UnitType::Pegasi: {
            fg.mounted = true;
            const bool heavy = type == UnitType::HeavyCavalry;
            k.head = Head::Helmet;
            k.weapon = heavy ? Weapon::Lance : Weapon::Sword;
            k.strike = heavy ? Strike::Thrust : Strike::Swing;
            k.shield = heavy ? Shield::Round : Shield::None;
            k.mail = true, k.armour = heavy;
            k.height = 1.8f;
            h.kind = Beast::Horse;
            h.height = 1.55f;
            h.saddle = true;
            h.barding = heavy;
            h.coat = heavy ? V3{0.22f, 0.2f, 0.2f} : V3{0.45f, 0.3f, 0.18f};
            h.mane = heavy ? grey(0.1f) : V3{0.2f, 0.13f, 0.08f};
            if (type == UnitType::Pegasi) {
                h.coat = grey(0.88f), h.mane = grey(0.78f), h.wings = true, h.wingCol = grey(0.93f), h.height = 1.45f;
                k.armour = false;
            }
            fg.person = false;
            break;
        }
        case UnitType::Dwarves:
            k.head = Head::DwarfHelm, k.weapon = Weapon::Axe, k.shield = Shield::Round, k.bulk = 1.45f, k.legs = 0.62f, k.headSize = 1.25f, k.mail = true;
            break;
        case UnitType::DwarfGrenadier:
            k.head = Head::DwarfHelm, k.weapon = Weapon::Satchel, k.strike = Strike::Throw, k.bulk = 1.4f, k.legs = 0.62f, k.headSize = 1.25f;
            k.trousers = V3{0.3f, 0.26f, 0.2f};
            break;
        case UnitType::WolfRiders:
            fg.person = false, fg.mounted = true;
            k.head = Head::Goblin, k.skin = V3{0.38f, 0.48f, 0.22f}, k.weapon = Weapon::Sword, k.height = 1.15f, k.bulk = 0.9f;
            h.kind = Beast::Wolf, h.height = 1.05f, h.coat = V3{0.42f, 0.41f, 0.4f}, h.mane = grey(0.3f), h.saddle = true;
            break;
        case UnitType::Giants:
            k.head = Head::Wild, k.weapon = Weapon::Club, k.strike = Strike::Overhead, k.bulk = 1.3f, k.bareChest = true, k.kilt = true;
            k.skin = V3{0.7f, 0.55f, 0.42f}, k.hair = V3{0.35f, 0.25f, 0.15f};
            break;
        case UnitType::Minotaurs:
            k.head = Head::Bull, k.weapon = Weapon::Axe, k.strike = Strike::Overhead, k.bulk = 1.45f, k.bareChest = true, k.kilt = true, k.tail = true;
            k.skin = V3{0.36f, 0.24f, 0.15f}, k.boots = grey(0.12f), k.legs = 0.95f;
            break;
        case UnitType::Spiders:
            fg.person = false, h.kind = Beast::Spider, h.height = 1.3f, h.coat = V3{0.13f, 0.11f, 0.12f};
            break;
        case UnitType::Griffins:
            fg.person = false, h.kind = Beast::Griffin, h.height = 1.5f, h.coat = V3{0.66f, 0.5f, 0.28f}, h.mane = V3{0.9f, 0.86f, 0.78f};
            h.wings = true, h.wingCol = V3{0.55f, 0.4f, 0.22f}, h.collar = true;
            break;
        case UnitType::Elephants:
            fg.person = false, h.kind = Beast::Elephant, h.height = 3.6f, h.coat = V3{0.46f, 0.44f, 0.43f}, h.mane = grey(0.25f), h.cloth = true;
            break;
        case UnitType::Unicorns:
            fg.person = false, h.kind = Beast::Horse, h.height = 1.6f, h.coat = grey(0.92f), h.mane = V3{0.95f, 0.85f, 0.6f}, h.horn = true, h.collar = true;
            break;
        case UnitType::Wizards:
            k.head = Head::WizardHat, k.weapon = Weapon::Staff, k.strike = Strike::Cast, k.robe = true;
            break;
        case UnitType::Dragons:
            fg.person = false, h.kind = Beast::Dragon, h.height = 3.0f, h.coat = V3{0.3f, 0.38f, 0.16f}, h.mane = V3{0.55f, 0.5f, 0.3f};
            h.wings = true, h.wingCol = V3{0.32f, 0.2f, 0.14f}, h.collar = true;
            break;
        case UnitType::Devils:
            k.head = Head::Horns, k.weapon = Weapon::Trident, k.strike = Strike::Thrust, k.bulk = 1.3f, k.bareChest = true, k.tail = true;
            k.skin = V3{0.55f, 0.12f, 0.08f}, k.hair = grey(0.08f), k.trousers = grey(0.12f), k.boots = grey(0.08f), k.kilt = true;
            break;
        case UnitType::Demons:
            k.head = Head::Horns, k.weapon = Weapon::Greatsword, k.strike = Strike::Overhead, k.bulk = 1.35f, k.bareChest = true, k.tail = true;
            k.wings = true, k.batWings = true, k.wing = V3{0.22f, 0.08f, 0.16f};
            k.skin = V3{0.3f, 0.12f, 0.25f}, k.hair = grey(0.06f), k.kilt = true, k.boots = grey(0.08f);
            break;
        case UnitType::Undead:
            k.head = Head::Skull, k.weapon = Weapon::Sword, k.shield = Shield::Round, k.bones = true, k.glowEyes = true;
            break;
        case UnitType::Archons:
            k.head = Head::Halo, k.weapon = Weapon::Sword, k.robe = true, k.wings = true, k.wing = grey(0.95f);
            k.tunic = grey(0.88f), k.belt = k.team;
            break;
        case UnitType::Hero:
            k.head = Head::Crown, k.weapon = Weapon::Sword, k.shield = Shield::Large, k.armour = true, k.cape = true, k.bulk = 1.08f;
            break;
        case UnitType::Journeyman:
            k.head = Head::Hood, k.weapon = Weapon::Sickle, k.robe = true, k.tunic = V3{0.42f, 0.34f, 0.22f};
            break;
        case UnitType::Warrior:
            k.head = Head::Helmet, k.weapon = Weapon::Sword, k.shield = Shield::Large, k.mail = true;
            break;
        case UnitType::Berserker:
            k.head = Head::Wild, k.weapon = Weapon::Greatsword, k.strike = Strike::Overhead, k.bareChest = true, k.kilt = true, k.bulk = 1.12f;
            k.hair = V3{0.55f, 0.32f, 0.12f};
            break;
        case UnitType::Count: break;
    }
    if (twoHanded(k.weapon) && k.strike == Strike::Swing) k.strike = Strike::Overhead;
    return fg;
}

}  // namespace

Mesh build(UnitType type, const float team[3], Anim anim, int frame) {
    Mesh mesh;
    Builder b{mesh};
    const Figure fg = figureOf(type, team);
    if (fg.person) {
        const Pose p = personPose(fg.kit, false, anim, frame);
        // A fall turns the whole figure backward about its heels.
        if (p.fall > 0) {
            const float c = std::cos(p.fall), s = std::sin(p.fall);
            b.rx = {c, 0, s};
            b.rz = {-s, 0, c};
            b.t = {0, 0, 0.16f * fg.kit.height / 1.8f * fg.kit.bulk * (p.fall / (kPi * 0.5f))};
        }
        person(b, fg.kit, p, nullptr);
        return mesh;
    }
    BeastPose bp = beastPose(anim, frame);
    if (bp.roll > 0) {
        // Onto its side.
        const float c = std::cos(bp.roll), s = std::sin(bp.roll);
        b.ry = {0, c, s};
        b.rz = {0, -s, c};
        const float half = fg.hide.kind == Beast::Elephant ? 0.3f : (fg.hide.kind == Beast::Dragon ? 0.3f : 0.2f);
        b.t = {0, 0, half * fg.hide.height * (bp.roll / (kPi * 0.5f))};
    }
    Pose rp{};
    if (fg.mounted) {
        // The rider rides with the beast; their own animation is the deed (a
        // swing or a couched lance), the beast's the gait.
        rp = personPose(fg.kit, true, anim == Anim::Walk ? Anim::Idle : anim, anim == Anim::Walk ? frame % 4 : frame);
        if (anim == Anim::Walk) rp = walk(fg.kit, true, frame / 8.0f);
        if (anim == Anim::Die) {
            rp = stance(fg.kit, true);
            rp.lean = -0.6f * frame / 5.0f;
            rp.rSh = rp.lSh = 1.5f * frame / 5.0f;
        }
        rp.fall = 0;
    }
    if (anim == Anim::Attack && fg.mounted) bp.front *= 0.35f, bp.pitch *= 0.35f, bp.lunge *= 0.3f;
    const V3 seat = beast(b, fg.hide, bp, fg.mounted ? &fg.kit : nullptr, fg.mounted ? &rp : nullptr);
    if (fg.mounted) person(b, fg.kit, rp, &seat);
    return mesh;
}

View view(int v) {
    const float az = -static_cast<float>(v) * (kPi / 4);
    const V3 c{std::cos(az), std::sin(az), 0};
    View r;
    r.toward = c * std::cos(kElevation) + V3{0, 0, std::sin(kElevation)};
    r.right = (r.toward * -1.0f).cross({0, 0, 1}).normalized();
    r.up = r.right.cross(r.toward * -1.0f);
    return r;
}

V3 cloth(const float team[3]) {
    const V3 t{team[0], team[1], team[2]};
    return lerp(t, grey((t.x + t.y + t.z) / 3), 0.25f);
}

Plan plan(UnitType type) {
    Plan p;
    const float white[3] = {1, 1, 1}, black[3] = {0, 0, 0};
    for (int a = 0; a < kAnimCount; ++a)
        for (int f = 0; f < kFrames[a]; ++f) {
            const int i = kFirstFrame[a] + f;
            // Built twice, in white and in black: what changes is the team's colour.
            const Mesh lit = build(type, white, static_cast<Anim>(a), f);
            const Mesh dark = build(type, black, static_cast<Anim>(a), f);
            std::vector<float>& out = p.frames[i];
            const size_t n = lit.data.size() / 9;
            out.resize(n * 10);
            for (size_t v = 0; v < n; ++v) {
                const float* q = &lit.data[v * 9];
                const float* r = &dark.data[v * 9];
                std::copy(q, q + 9, &out[v * 10]);
                out[v * 10 + 9] = std::fabs(q[6] - r[6]) + std::fabs(q[7] - r[7]) + std::fabs(q[8] - r[8]) > 1e-3f ? 1.0f : 0.0f;
            }
            for (int w = 0; w < kViews; ++w) {
                const View vw = view(w);
                Extent e{1e9f, 1e9f, -1e9f, -1e9f};
                for (size_t v = 0; v < n; ++v) {
                    const V3 pos{out[v * 10], out[v * 10 + 1], out[v * 10 + 2]};
                    const float x = pos.dot(vw.right), y = pos.dot(vw.up);
                    e.u0 = std::min(e.u0, x), e.u1 = std::max(e.u1, x);
                    e.v0 = std::min(e.v0, y), e.v1 = std::max(e.v1, y);
                }
                if (n == 0) e = {-0.1f, 0, 0.1f, 0.1f};
                // A little room for the outline.
                const float pad = 0.03f * std::max(e.u1 - e.u0, e.v1 - e.v0) + 0.02f;
                p.extents[w][i] = {e.u0 - pad, e.v0 - pad, e.u1 + pad, e.v1 + pad};
            }
        }
    return p;
}

float stride(UnitType type) {
    const float h = static_cast<float>(sim::unitStats(type).height.toDouble());
    switch (type) {
        case UnitType::LightCavalry: case UnitType::HeavyCavalry: case UnitType::Unicorns: case UnitType::Pegasi: return 3.2f;
        case UnitType::WolfRiders: return 2.6f;
        case UnitType::Spiders: return 1.6f;
        case UnitType::Elephants: return 4.0f;
        case UnitType::Dragons: return 5.0f;
        case UnitType::Griffins: return 2.8f;
        case UnitType::Dwarves: case UnitType::DwarfGrenadier: return 1.1f;
        default: return h * 0.85f;
    }
}

float attackSeconds(UnitType type) {
    const sim::UnitStats st = sim::unitStats(type);
    if (st.ranged.muzzleSpeed > sim::Fixed::zero()) return type == UnitType::Archers ? 0.9f : 0.8f;
    const float cooldown = static_cast<float>(st.melee.cooldownTicks) / sim::kTicksPerSecond;
    return std::clamp(cooldown * 0.6f, 0.45f, 0.9f);
}

}  // namespace illyria::game::figures
