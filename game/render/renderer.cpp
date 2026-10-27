#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace illyria::game {

namespace {

// ---------------------------------------------------------------------------
// Shaders (GLSL ES 3.00, i.e. WebGL 2)
// ---------------------------------------------------------------------------

const char* kCommon = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
uniform mat4 uViewProj;
uniform vec3 uEye;
uniform sampler2D uHeights;   // R32F, (cells+1)^2 texels
uniform float uCells;
uniform float uCellSize;
uniform float uTime;          // wall-clock seconds, for animation
uniform float uGameSeconds;   // simulation time, for weathering
uniform float uCamDist;
uniform float uPxScale;       // world units per pixel at distance 1
uniform vec3 uCamRight;

const vec3 kSun = normalize(vec3(-0.62, 0.38, 0.52));
const vec3 kSky = vec3(0.64, 0.74, 0.84);

float heightTexel(vec2 c) {
    ivec2 i = ivec2(clamp(c, vec2(0.0), vec2(uCells)));
    return texelFetch(uHeights, i, 0).r;
}
float groundZ(vec2 p) {
    vec2 f = clamp(p / uCellSize, vec2(0.0), vec2(uCells));
    vec2 i = min(floor(f), vec2(uCells - 1.0));
    vec2 t = f - i;
    float a = heightTexel(i), b = heightTexel(i + vec2(1, 0));
    float c = heightTexel(i + vec2(0, 1)), d = heightTexel(i + vec2(1, 1));
    return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float fogFactor(float d) {
    float range = uCamDist * 5.0 + 700.0;
    float x = d / range;
    return clamp(1.0 - exp(-x * x * 2.0), 0.0, 1.0);
}
)";

// ---- Terrain ----
const char* kTerrainVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vWorld = aPos;
    vNormal = aNormal;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";
const char* kTerrainFs = R"(
in vec3 vWorld;
in vec3 vNormal;
out vec4 oColor;

float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
// Distance to the nearest hex edge for a pointy-top hex grid of circumradius s.
float hexEdge(vec2 p, float s) {
    vec2 r = vec2(1.0, 1.7320508);
    vec2 h = r * 0.5;
    vec2 a = mod(p / s, r) - h;
    vec2 b = mod(p / s - h, r) - h;
    vec2 g = dot(a, a) < dot(b, b) ? a : b;
    g = abs(g);
    return 0.5 - max(dot(g, normalize(vec2(1.0, 1.7320508))), g.x);
}
void main() {
    vec3 n = normalize(vNormal);
    float slope = 1.0 - n.z;
    float broad = valueNoise(vWorld.xy * 0.02);
    float fine = valueNoise(vWorld.xy * 0.35);
    vec3 grassA = vec3(0.28, 0.40, 0.17), grassB = vec3(0.40, 0.48, 0.22);
    vec3 grass = mix(grassA, grassB, broad) * (0.9 + 0.2 * fine);
    vec3 dirt = vec3(0.45, 0.37, 0.25) * (0.85 + 0.3 * fine);
    vec3 rock = vec3(0.47, 0.45, 0.42) * (0.8 + 0.4 * fine);
    vec3 albedo = mix(grass, dirt, smoothstep(0.08, 0.2, slope + (broad - 0.5) * 0.1));
    albedo = mix(albedo, rock, smoothstep(0.25, 0.4, slope));
    // Lowlands a little lusher and darker.
    albedo *= mix(0.85, 1.05, smoothstep(2.0, 20.0, vWorld.z));

    // Low sun so the hills read clearly from above.
    float light = 0.30 + 0.95 * max(dot(n, kSun), 0.0);
    vec3 color = albedo * light;

    // Strategic hex grid fades in as the camera rises.
    float gridFade = smoothstep(120.0, 400.0, uCamDist) * 0.35;
    if (gridFade > 0.0) {
        float e = hexEdge(vWorld.xy, 48.0);
        float w = fwidth(e) * 1.5;
        color = mix(color, vec3(0.95, 0.92, 0.8), gridFade * (1.0 - smoothstep(0.0, w, e)));
    }
    // Darken outside the playable region.
    float size = uCells * uCellSize;
    if (vWorld.x < 0.5 || vWorld.y < 0.5 || vWorld.x > size - 0.5 || vWorld.y > size - 0.5) color *= 0.6;

    oColor = vec4(mix(color, kSky, fogFactor(distance(uEye, vWorld))), 1.0);
}
)";

// ---- Ground decals: blood, corpses, shadows, selection rings ----
// Per instance: centre (xyz), params (yaw, size, spawn seconds, type), extra (rgb, seed).
const char* kDecalVs = R"(
layout(location = 0) in vec3 iCentre;
layout(location = 1) in vec4 iParams;
layout(location = 2) in vec4 iExtra;
out vec2 vUv;
flat out vec4 vParams;
flat out vec4 vExtra;
out float vFog;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    float c = cos(iParams.x), s = sin(iParams.x);
    vec2 local = corner * iParams.y;
    vec2 xy = iCentre.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    // Drape over the terrain; lift grows with distance to avoid z-fighting.
    float lift = 0.04 + uCamDist * 0.0004;
    vec3 world = vec3(xy, groundZ(xy) + lift);
    vUv = corner;
    vParams = iParams;
    vExtra = iExtra;
    vFog = fogFactor(distance(uEye, world));
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
const char* kDecalFs = R"(
in vec2 vUv;
flat in vec4 vParams;
flat in vec4 vExtra;
in float vFog;
out vec4 oColor;

vec3 bloodColor(float age) {
    // Bright red, to brown within minutes, to near-black within the hour.
    vec3 fresh = vec3(0.55, 0.02, 0.02);
    vec3 brown = vec3(0.30, 0.07, 0.03);
    vec3 old = vec3(0.13, 0.07, 0.05);
    return mix(mix(fresh, brown, smoothstep(0.0, 240.0, age)), old, smoothstep(240.0, 3600.0, age));
}

void main() {
    float type = vParams.w;
    float age = max(uGameSeconds - vParams.z, 0.0);
    float r = length(vUv);
    vec4 color;
    if (type < 0.5) {
        // Blood: an irregular blob plus satellite droplets.
        float a = atan(vUv.y, vUv.x);
        float seed = vExtra.w;
        float edge = 0.55 + 0.18 * sin(a * 3.0 + seed * 6.28) + 0.1 * sin(a * 7.0 + seed * 17.0);
        float blob = 1.0 - smoothstep(edge - 0.08, edge, r);
        vec2 cell = floor((vUv + 1.0) * 4.0);
        vec2 local = fract((vUv + 1.0) * 4.0) - 0.5;
        float drop = step(0.78, hash12(cell + seed * 31.0)) * (1.0 - smoothstep(0.12, 0.2, length(local)));
        float alpha = max(blob, drop * step(r, 0.95));
        // Soaks into the soil over a very long half-life, never quite gone.
        float soak = mix(0.85, 0.35, 1.0 - exp(-age / 21600.0));
        color = vec4(bloodColor(age), alpha * soak);
    } else if (type < 1.5) {
        // Soft contact shadow.
        color = vec4(0.0, 0.0, 0.0, 0.32 * (1.0 - smoothstep(0.3, 1.0, r)));
    } else if (type < 2.5) {
        // Selection ring in the squad's colour.
        float ring = smoothstep(0.62, 0.72, r) * (1.0 - smoothstep(0.86, 0.96, r));
        color = vec4(vExtra.rgb, ring * 0.9);
    } else {
        // Corpse seen from above, lying along +x of its local frame: body,
        // head, limbs. Fades from team colours to bone over weeks.
        vec2 p = vUv;
        float body = 1.0 - smoothstep(0.0, 0.05, length(max(abs(p - vec2(-0.05, 0.0)) - vec2(0.38, 0.13), 0.0)) - 0.05);
        float head = 1.0 - smoothstep(0.11, 0.15, length(p - vec2(0.48, 0.02)));
        float legs = 1.0 - smoothstep(0.0, 0.04, length(max(abs(p - vec2(-0.62, 0.0)) - vec2(0.22, 0.12), 0.0)) - 0.02);
        float arm = 1.0 - smoothstep(0.0, 0.03, length(max(abs(p - vec2(0.1, 0.3)) - vec2(0.2, 0.05), 0.0)));
        float decay = smoothstep(3600.0, 86400.0 * 7.0, age);
        vec3 cloth = mix(vExtra.rgb * 0.7, vec3(0.25, 0.22, 0.18), decay);
        vec3 skin = mix(vec3(0.75, 0.6, 0.5), vec3(0.85, 0.82, 0.74), decay);
        vec3 c = vec3(0.0);
        float a = 0.0;
        if (legs > 0.5) { c = mix(vec3(0.22, 0.17, 0.12), vec3(0.8, 0.77, 0.7), decay); a = legs; }
        if (body > 0.5) { c = cloth; a = body; }
        if (arm > 0.5) { c = cloth * 0.9; a = arm; }
        if (head > 0.5) { c = skin; a = head; }
        color = vec4(c, a > 0.5 ? 1.0 : 0.0);
    }
    if (color.a < 0.01) discard;
    oColor = vec4(mix(color.rgb, vec3(0.64, 0.74, 0.84), vFog), color.a);
}
)";

// ---- Arrows: in flight and stuck in the ground ----
// Per instance: pos (tip), dir, info (spawn seconds, stuck flag).
const char* kArrowVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iDir;
layout(location = 2) in vec2 iInfo;
out float vAlong;
out float vFog;
flat out float vStuck;
flat out float vAge;
void main() {
    const float len = 0.85;
    // Stuck arrows are buried a third of their length.
    vec3 tip = iPos + iDir * (iInfo.y > 0.5 ? len * 0.3 : 0.0);
    vec3 tail = tip - iDir * len;
    float dist = distance(uEye, iPos);
    float px = uPxScale * dist;
    // Too small to see: emit a degenerate vertex instead of shading a speck.
    if (len < px * 1.5 || (iInfo.y > 0.5 && dist > uCamDist * 1.5 + 150.0)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    float width = max(0.035, px * 1.1);
    vec3 side = normalize(cross(iDir, uEye - iPos)) * width;
    float t = float(gl_VertexID & 1);
    float s = float((gl_VertexID >> 1) & 1) * 2.0 - 1.0;
    vec3 world = mix(tail, tip, t) + side * s * 0.5;
    vAlong = t;
    vStuck = iInfo.y;
    vAge = max(uGameSeconds - iInfo.x, 0.0);
    vFog = fogFactor(dist);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
const char* kArrowFs = R"(
in float vAlong;
in float vFog;
flat in float vStuck;
flat in float vAge;
out vec4 oColor;
void main() {
    vec3 shaft = vec3(0.42, 0.30, 0.18);
    vec3 fletch = vec3(0.88, 0.86, 0.80);
    vec3 c = vAlong < 0.18 ? fletch : shaft;
    if (vAlong > 0.93) c = vec3(0.35);
    // Weathering: shafts grey and darken over days.
    c = mix(c, vec3(0.30, 0.28, 0.25), smoothstep(3600.0, 86400.0 * 3.0, vAge) * vStuck);
    oColor = vec4(mix(c, vec3(0.64, 0.74, 0.84), vFog), 1.0);
}
)";

// ---- Soldiers: camera-facing sprites that become dots far away ----
// Per instance: feet position, team colour, info (kind, moving, mirror, phase), flags (selected).
const char* kSpriteVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iColor;
layout(location = 2) in vec4 iInfo;
layout(location = 3) in float iSelected;
out vec2 vUv;
flat out vec3 vColor;
flat out vec4 vInfo;
flat out float vDot;
flat out float vSelected;
out float vFog;
void main() {
    const float height = 1.85;
    float dist = distance(uEye, iPos);
    float px = uPxScale * dist;
    // Never smaller than a few pixels: zoomed out, armies read as ants.
    float minSize = px * 4.5;
    float size = max(height, minSize);
    vDot = height < minSize * 1.6 ? 1.0 : 0.0;
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    float width = vDot > 0.5 ? size : size * 0.62;
    vec3 base = iPos + (vDot > 0.5 ? vec3(0.0, 0.0, size * 0.25) : vec3(0.0));
    vec3 world = base + uCamRight * ((corner.x - 0.5) * width) + vec3(0.0, 0.0, corner.y * size);
    vUv = corner;
    vColor = iColor;
    vInfo = iInfo;
    vSelected = iSelected;
    vFog = fogFactor(dist);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
const char* kSpriteFs = R"(
in vec2 vUv;
flat in vec3 vColor;
flat in vec4 vInfo;
flat in float vDot;
flat in float vSelected;
in float vFog;
out vec4 oColor;

float box(vec2 p, vec2 lo, vec2 hi) {
    return step(lo.x, p.x) * step(p.x, hi.x) * step(lo.y, p.y) * step(p.y, hi.y);
}
float disc(vec2 p, vec2 c, float r) { return step(length(p - c), r); }

void main() {
    vec3 team = vColor * (vSelected > 0.5 ? 1.35 : 1.0);
    if (vDot > 0.5) {
        float r = length(vUv - 0.5);
        if (r > 0.5) discard;
        oColor = vec4(mix(team * (r > 0.38 ? 0.45 : 1.0), vec3(0.64, 0.74, 0.84), vFog * 0.5), 1.0);
        return;
    }
    vec2 p = vUv;
    // Face the way the soldier faces (left/right relative to the camera).
    if (vInfo.z > 0.5) p.x = 1.0 - p.x;
    // Unit space: x in [0,1] across, y in [0,1] up (feet to helmet).
    float swing = vInfo.y > 0.5 ? sin(uTime * 9.0 + vInfo.w) * 0.06 : 0.0;
    vec3 c = vec3(0.0);
    float a = 0.0;
    vec3 legC = vec3(0.24, 0.19, 0.14);
    vec3 skin = vec3(0.86, 0.68, 0.55);
    vec3 steel = vec3(0.62, 0.64, 0.68);
    if (box(p, vec2(0.36 + swing, 0.0), vec2(0.47 + swing, 0.44)) > 0.5) { c = legC; a = 1.0; }
    if (box(p, vec2(0.53 - swing, 0.0), vec2(0.64 - swing, 0.44)) > 0.5) { c = legC * 0.85; a = 1.0; }
    if (box(p, vec2(0.30, 0.40), vec2(0.70, 0.76)) > 0.5) {
        c = team * (0.8 + 0.3 * p.x); a = 1.0;
    }
    if (box(p, vec2(0.30, 0.40), vec2(0.70, 0.45)) > 0.5) { c = vec3(0.3, 0.22, 0.12); a = 1.0; } // belt
    if (disc(p, vec2(0.5, 0.85), 0.11) > 0.5) { c = skin; a = 1.0; }
    if (vInfo.x < 0.5) {
        // Archer: hood, bow held forward, quiver on the back.
        if (disc(p, vec2(0.5, 0.88), 0.12) > 0.5 && p.y > 0.86) { c = team * 0.6; a = 1.0; }
        if (box(p, vec2(0.16, 0.45), vec2(0.27, 0.78)) > 0.5) { c = vec3(0.45, 0.30, 0.15); a = 1.0; }
        float bowR = length(p - vec2(0.62, 0.58));
        if (p.x > 0.74 && bowR > 0.2 && bowR < 0.245) { c = vec3(0.52, 0.34, 0.16); a = 1.0; }
        if (abs(p.x - 0.79) < 0.008 && abs(p.y - 0.58) < 0.2) { c = vec3(0.9); a = 1.0; }
    } else {
        // Footman: helmet, sword up, round shield in front.
        if (disc(p, vec2(0.5, 0.88), 0.12) > 0.5 && p.y > 0.87) { c = steel; a = 1.0; }
        if (abs(p.x - (0.78 + (p.y - 0.5) * 0.25)) < 0.025 && p.y > 0.45 && p.y < 0.92) { c = steel * 1.2; a = 1.0; }
        float sh = length(p - vec2(0.36, 0.56));
        if (sh < 0.2) { c = sh > 0.17 ? steel * 0.8 : team * 0.65; a = 1.0; }
        if (sh < 0.045) { c = steel; a = 1.0; }
    }
    if (a < 0.5) discard;
    oColor = vec4(mix(c, vec3(0.64, 0.74, 0.84), vFog), 1.0);
}
)";

GLuint compile(GLenum type, const char* body) {
    const char* sources[] = {kCommon, body};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, sources, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::printf("shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool link(const char* vs, const char* fs, GLuint& out) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return false;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::printf("program link failed: %s\n", log);
        return false;
    }
    out = p;
    return true;
}

// Instance attribute layout: list of component counts, one attribute per entry.
void setupInstanceAttribs(std::initializer_list<int> sizes) {
    int stride = 0;
    for (int s : sizes) stride += s;
    int offset = 0, loc = 0;
    for (int s : sizes) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, s, GL_FLOAT, GL_FALSE, stride * 4, reinterpret_cast<void*>(static_cast<intptr_t>(offset * 4)));
        glVertexAttribDivisor(loc, 1);
        offset += s;
        ++loc;
    }
}

constexpr int kDecalFloats = 11;   // centre 3, params 4, extra 4
constexpr int kArrowFloats = 8;    // pos 3, dir 3, info 2
constexpr int kSpriteFloats = 11;  // pos 3, colour 3, info 4, selected 1

const float kPlayerColor[3] = {0.22f, 0.42f, 0.92f};
const float kEnemyColor[3] = {0.86f, 0.18f, 0.14f};

}  // namespace

bool Renderer::buildPrograms() {
    struct Spec {
        Program& p;
        const char* vs;
        const char* fs;
    } specs[] = {{terrain_, kTerrainVs, kTerrainFs},
                 {decal_, kDecalVs, kDecalFs},
                 {arrow_, kArrowVs, kArrowFs},
                 {sprite_, kSpriteVs, kSpriteFs}};
    for (Spec& s : specs) {
        if (!link(s.vs, s.fs, s.p.id)) return false;
        s.p.viewProj = glGetUniformLocation(s.p.id, "uViewProj");
        s.p.eye = glGetUniformLocation(s.p.id, "uEye");
        s.p.heights = glGetUniformLocation(s.p.id, "uHeights");
        s.p.cells = glGetUniformLocation(s.p.id, "uCells");
        s.p.cellSize = glGetUniformLocation(s.p.id, "uCellSize");
        s.p.time = glGetUniformLocation(s.p.id, "uTime");
        s.p.gameSeconds = glGetUniformLocation(s.p.id, "uGameSeconds");
        s.p.camDist = glGetUniformLocation(s.p.id, "uCamDist");
        s.p.pxScale = glGetUniformLocation(s.p.id, "uPxScale");
        s.p.camRight = glGetUniformLocation(s.p.id, "uCamRight");
    }
    return true;
}

bool Renderer::init(const Battle& battle) {
    if (!buildPrograms()) return false;

    const auto makeBatch = [](Batch& b, std::initializer_list<int> layout, size_t capacity, int floats) {
        glGenVertexArrays(1, &b.vao);
        glGenBuffers(1, &b.vbo);
        glBindVertexArray(b.vao);
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        b.capacity = capacity;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity * floats * 4), nullptr, GL_DYNAMIC_DRAW);
        setupInstanceAttribs(layout);
    };
    makeBatch(bloodBatch_, {3, 4, 4}, Battle::kMaxBloodStains, kDecalFloats);
    makeBatch(stuckArrowBatch_, {3, 3, 2}, Battle::kMaxStuckArrows, kArrowFloats);
    makeBatch(dynamicDecals_, {3, 4, 4}, 0, kDecalFloats);
    makeBatch(flyingArrows_, {3, 3, 2}, 0, kArrowFloats);
    makeBatch(sprites_, {3, 3, 4, 1}, 0, kSpriteFloats);
    glBindVertexArray(0);

    uploadTerrain(battle);
    return true;
}

void Renderer::reset(const Battle& battle) {
    bloodUploaded_ = arrowsUploaded_ = 0;
    bloodBatch_.count = stuckArrowBatch_.count = 0;
    uploadTerrain(battle);
}

void Renderer::uploadTerrain(const Battle& battle) {
    cells_ = Battle::kTerrainCells;
    cellSize_ = Battle::kCellSize;
    const int n = cells_ + 1;
    const std::vector<float>& h = battle.heights();

    if (!heightTex_) glGenTextures(1, &heightTex_);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, n, n, 0, GL_RED, GL_FLOAT, h.data());

    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(n) * n * 6);
    const auto at = [&](int x, int y) {
        x = std::clamp(x, 0, cells_);
        y = std::clamp(y, 0, cells_);
        return h[static_cast<size_t>(y) * n + x];
    };
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float dx = (at(x + 1, y) - at(x - 1, y)) / (2 * cellSize_);
            const float dy = (at(x, y + 1) - at(x, y - 1)) / (2 * cellSize_);
            const V3 nrm = V3{-dx, -dy, 1}.normalized();
            verts.insert(verts.end(), {x * cellSize_, y * cellSize_, at(x, y), nrm.x, nrm.y, nrm.z});
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(cells_) * cells_ * 6);
    for (int y = 0; y < cells_; ++y) {
        for (int x = 0; x < cells_; ++x) {
            const uint32_t a = y * n + x, b = a + 1, c = a + n, d = c + 1;
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    }
    terrainIndexCount_ = static_cast<GLsizei>(idx.size());

    if (!terrainVao_) {
        glGenVertexArrays(1, &terrainVao_);
        glGenBuffers(1, &terrainVbo_);
        glGenBuffers(1, &terrainIbo_);
    }
    glBindVertexArray(terrainVao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrainVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * 4), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrainIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void Renderer::uploadNewRemains(const Battle& battle) {
    const float tickSeconds = 1.0f / sim::kTicksPerSecond;
    // Blood: upload only the stains added since last frame, at their ring slots.
    const uint64_t bloodTotal = battle.bloodStainsAdded();
    if (bloodTotal - bloodUploaded_ > Battle::kMaxBloodStains) bloodUploaded_ = bloodTotal - Battle::kMaxBloodStains;
    glBindBuffer(GL_ARRAY_BUFFER, bloodBatch_.vbo);
    for (; bloodUploaded_ < bloodTotal; ++bloodUploaded_) {
        const size_t slot = bloodUploaded_ % Battle::kMaxBloodStains;
        const BloodStain& b = battle.bloodStains()[slot];
        const float data[kDecalFloats] = {b.pos.x, b.pos.y, b.pos.z, b.yaw, b.size, b.tick * tickSeconds, 0,
                                          0, 0, 0, static_cast<float>(b.seed % 1000) / 1000.0f};
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof data), sizeof data, data);
    }
    bloodBatch_.count = static_cast<GLsizei>(battle.bloodStains().size());

    const uint64_t arrowTotal = battle.stuckArrowsAdded();
    if (arrowTotal - arrowsUploaded_ > Battle::kMaxStuckArrows) arrowsUploaded_ = arrowTotal - Battle::kMaxStuckArrows;
    glBindBuffer(GL_ARRAY_BUFFER, stuckArrowBatch_.vbo);
    for (; arrowsUploaded_ < arrowTotal; ++arrowsUploaded_) {
        const size_t slot = arrowsUploaded_ % Battle::kMaxStuckArrows;
        const StuckArrow& a = battle.stuckArrows()[slot];
        const float data[kArrowFloats] = {a.pos.x, a.pos.y, a.pos.z, a.dir.x, a.dir.y, a.dir.z, a.tick * tickSeconds, 1};
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof data), sizeof data, data);
    }
    stuckArrowBatch_.count = static_cast<GLsizei>(battle.stuckArrows().size());
}

void Renderer::setCommon(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds) {
    glUseProgram(p.id);
    const M4 vp = camera.viewProjection();
    glUniformMatrix4fv(p.viewProj, 1, GL_FALSE, vp.m);
    const V3 eye = camera.eye();
    glUniform3f(p.eye, eye.x, eye.y, eye.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glUniform1i(p.heights, 0);
    glUniform1f(p.cells, static_cast<float>(cells_));
    glUniform1f(p.cellSize, cellSize_);
    glUniform1f(p.time, static_cast<float>(std::fmod(timeSeconds, 3600.0)));
    glUniform1f(p.gameSeconds, gameSeconds);
    glUniform1f(p.camDist, camera.distance());
    glUniform1f(p.pxScale, 2.0f * std::tan(Camera::kFovY / 2) / camera.height());
    const V3 f = (camera.target() - eye).normalized();
    const V3 right = f.cross({0, 0, 1}).normalized();
    glUniform3f(p.camRight, right.x, right.y, right.z);
}

void Renderer::draw(const Battle& battle, const Camera& camera, double timeSeconds) {
    const sim::World& world = battle.world();
    const float gameSeconds = (static_cast<float>(world.tick()) + battle.alpha()) / sim::kTicksPerSecond;
    uploadNewRemains(battle);

    // Per-frame instances: corpses, shadows and selection rings; soldiers; arrows in flight.
    const V3 eye = camera.eye();
    const V3 fwd = (camera.target() - eye).normalized();
    const V3 camRight = fwd.cross({0, 0, 1}).normalized();
    std::vector<float>& decals = scratch_;
    decals.clear();
    std::vector<float> sprites, arrows;
    const auto& units = world.units();
    const int selected = battle.selectedSquad();
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        const V3 pos = battle.unitPosition(i);
        const float* team = u.owner == Battle::kPlayer ? kPlayerColor : kEnemyColor;
        const float yaw = std::atan2(static_cast<float>(u.facing.y.toDouble()), static_cast<float>(u.facing.x.toDouble()));
        const bool isSelected = battle.squadOf(u.id) == selected && selected >= 0;
        if (!u.alive()) {
            const float died = battle.deathTick(i) / static_cast<float>(sim::kTicksPerSecond);
            decals.insert(decals.end(), {pos.x, pos.y, pos.z, yaw + 3.14159f, 1.0f, died, 3, team[0], team[1], team[2], 0});
            continue;
        }
        decals.insert(decals.end(), {pos.x, pos.y, pos.z, 0, 0.55f, 0, 1, 0, 0, 0, 0});
        if (isSelected) decals.insert(decals.end(), {pos.x, pos.y, pos.z, 0, 0.75f, 0, 2, 0.4f, 0.9f, 1.0f, 0});
        const bool archer = u.stats.ranged.muzzleSpeed > sim::Fixed::zero();
        const float facingSide = static_cast<float>(u.facing.x.toDouble()) * camRight.x +
                                 static_cast<float>(u.facing.y.toDouble()) * camRight.y;
        sprites.insert(sprites.end(), {pos.x, pos.y, pos.z, team[0], team[1], team[2], archer ? 0.0f : 1.0f,
                                       battle.unitMoving(i) ? 1.0f : 0.0f, facingSide < 0 ? 1.0f : 0.0f,
                                       static_cast<float>(u.id % 17), isSelected ? 1.0f : 0.0f});
    }
    battle.forEachProjectile([&](const V3& pos, const V3& dir) {
        arrows.insert(arrows.end(), {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, 0, 0});
    });

    const auto uploadDynamic = [](Batch& b, const std::vector<float>& data, int floats) {
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        const size_t count = data.size() / floats;
        if (count > b.capacity) {
            b.capacity = count * 2;
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(b.capacity * floats * 4), nullptr, GL_DYNAMIC_DRAW);
        }
        if (!data.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(data.size() * 4), data.data());
        b.count = static_cast<GLsizei>(count);
    };
    uploadDynamic(dynamicDecals_, decals, kDecalFloats);
    uploadDynamic(sprites_, sprites, kSpriteFloats);
    uploadDynamic(flyingArrows_, arrows, kArrowFloats);

    glViewport(0, 0, static_cast<GLsizei>(camera.width()), static_cast<GLsizei>(camera.height()));
    glClearColor(0.64f, 0.74f, 0.84f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    setCommon(terrain_, camera, timeSeconds, gameSeconds);
    glBindVertexArray(terrainVao_);
    glDrawElements(GL_TRIANGLES, terrainIndexCount_, GL_UNSIGNED_INT, nullptr);

    // Decals: blended, no depth writes.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    setCommon(decal_, camera, timeSeconds, gameSeconds);
    for (Batch* b : {&bloodBatch_, &dynamicDecals_}) {
        if (b->count == 0) continue;
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, b->count);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    setCommon(arrow_, camera, timeSeconds, gameSeconds);
    for (Batch* b : {&stuckArrowBatch_, &flyingArrows_}) {
        if (b->count == 0) continue;
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, b->count);
    }

    if (sprites_.count > 0) {
        setCommon(sprite_, camera, timeSeconds, gameSeconds);
        glBindVertexArray(sprites_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, sprites_.count);
    }
    glBindVertexArray(0);
}

}  // namespace illyria::game
