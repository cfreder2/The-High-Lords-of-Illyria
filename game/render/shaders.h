#pragma once

// GLSL ES 3.00 (WebGL 2) shader sources for the battle renderer. Every program
// is prefixed with kCommon.

namespace illyria::game::shaders {

inline const char* kCommon = R"(#version 300 es
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
// Same two-triangle interpolation as the simulation and the terrain mesh.
float groundZ(vec2 p) {
    vec2 f = clamp(p / uCellSize, vec2(0.0), vec2(uCells));
    vec2 i = min(floor(f), vec2(uCells - 1.0));
    vec2 t = f - i;
    float a = heightTexel(i), b = heightTexel(i + vec2(1, 0));
    float c = heightTexel(i + vec2(0, 1)), d = heightTexel(i + vec2(1, 1));
    return t.x >= t.y ? a + (b - a) * t.x + (d - b) * t.y : a + (c - a) * t.y + (d - c) * t.x;
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
// Degenerate vertex: nothing is rasterised.
const vec4 kCulled = vec4(2.0, 2.0, 2.0, 1.0);
)";

// ---------------------------------------------------------------- terrain
inline const char* kTerrainVs = R"(
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
inline const char* kTerrainFs = R"(
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
    float size = uCells * uCellSize;
    if (vWorld.x < 0.5 || vWorld.y < 0.5 || vWorld.x > size - 0.5 || vWorld.y > size - 0.5) color *= 0.6;
    oColor = vec4(mix(color, kSky, fogFactor(distance(uEye, vWorld))), 1.0);
}
)";

// ---------------------------------------------------------------- decals
// Per instance: centre (xyz), params (yaw, size, spawn seconds, type), extra (rgb, seed).
// Types: 0 blood, 1 shadow, 2 selection ring, 3 corpse, 4 crater, 5 dust, 6 body part.
inline const char* kDecalVs = R"(
layout(location = 0) in vec3 iCentre;
layout(location = 1) in vec4 iParams;
layout(location = 2) in vec4 iExtra;
out vec2 vUv;
flat out vec4 vParams;
flat out vec4 vExtra;
out float vFog;
void main() {
    if (iParams.y <= 0.0) { gl_Position = kCulled; return; }
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    float c = cos(iParams.x), s = sin(iParams.x);
    vec2 local = corner * iParams.y;
    vec2 xy = iCentre.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    // Drape over the terrain; lift grows with distance to avoid z-fighting.
    float lift = 0.04 + uCamDist * 0.0004 + (iParams.w > 5.5 ? 0.04 : 0.0);
    vec3 world = vec3(xy, groundZ(xy) + lift);
    vUv = corner;
    vParams = iParams;
    vExtra = iExtra;
    vFog = fogFactor(distance(uEye, world));
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kDecalFs = R"(
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
float seg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}

void main() {
    float type = vParams.w;
    float age = max(uGameSeconds - vParams.z, 0.0);
    float r = length(vUv);
    float seed = vExtra.w;
    vec4 color;
    if (type < 0.5 || (type > 4.5 && type < 5.5)) {
        // Blood (or bone dust): an irregular blob plus droplets.
        float a = atan(vUv.y, vUv.x);
        float edge = 0.55 + 0.18 * sin(a * 3.0 + seed * 6.28) + 0.1 * sin(a * 7.0 + seed * 17.0);
        float blob = 1.0 - smoothstep(edge - 0.08, edge, r);
        vec2 cell = floor((vUv + 1.0) * 4.0);
        vec2 local = fract((vUv + 1.0) * 4.0) - 0.5;
        float drop = step(0.78, hash12(cell + seed * 31.0)) * (1.0 - smoothstep(0.12, 0.2, length(local)));
        float alpha = max(blob, drop * step(r, 0.95));
        // Soaks into the soil over a very long half-life, never quite gone.
        float soak = mix(0.85, 0.35, 1.0 - exp(-age / 21600.0));
        vec3 c = type < 0.5 ? bloodColor(age) : vec3(0.62, 0.6, 0.55);
        color = vec4(c, alpha * soak * (type < 0.5 ? 1.0 : 0.6));
    } else if (type < 1.5) {
        color = vec4(0.0, 0.0, 0.0, 0.32 * (1.0 - smoothstep(0.3, 1.0, r)));
    } else if (type < 2.5) {
        float ring = smoothstep(0.62, 0.72, r) * (1.0 - smoothstep(0.86, 0.96, r));
        color = vec4(vExtra.rgb, ring * 0.9);
    } else if (type < 3.5) {
        // Corpse seen from above, lying along +x. Fades from team colours to bone over a week.
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
        if (legs > 0.5) { c = mix(vec3(0.22, 0.17, 0.12), vec3(0.8, 0.77, 0.7), decay); a = 1.0; }
        if (body > 0.5) { c = cloth; a = 1.0; }
        if (arm > 0.5) { c = cloth * 0.9; a = 1.0; }
        if (head > 0.5) { c = skin; a = 1.0; }
        color = vec4(c, a);
    } else if (type < 4.5) {
        // Scorched crater: dark pit, blackened ring, slowly greening over months.
        float a = atan(vUv.y, vUv.x);
        float edge = 0.85 + 0.08 * sin(a * 5.0 + seed * 9.0);
        float pit = 1.0 - smoothstep(0.35, 0.55, r);
        float scorch = 1.0 - smoothstep(edge - 0.25, edge, r);
        float heal = smoothstep(86400.0, 86400.0 * 60.0, age);
        vec3 c = mix(vec3(0.08, 0.06, 0.05), vec3(0.18, 0.13, 0.08), 1.0 - pit);
        color = vec4(c, scorch * mix(0.85, 0.15, heal));
    } else {
        // Body part on the ground: extra.x = kind (2 head, 3 arm, 4 leg, 5 bone), extra.y = owner.
        float kind = vExtra.x;
        vec3 team = vExtra.y < 0.5 ? vec3(0.22, 0.42, 0.92) : vec3(0.86, 0.18, 0.14);
        float decay = smoothstep(3600.0, 86400.0 * 7.0, age);
        vec3 skin = mix(vec3(0.78, 0.6, 0.5), vec3(0.85, 0.82, 0.74), decay);
        vec3 c = vec3(0.0);
        float a = 0.0;
        if (kind < 2.5) {
            if (r < 0.55) { c = skin; a = 1.0; }
            if (r < 0.55 && vUv.x < -0.25) { c = vec3(0.45, 0.04, 0.03); }
        } else if (kind < 4.5) {
            float d = seg(vUv, vec2(-0.8, 0.0), vec2(0.8, 0.0));
            if (d < 0.22) { c = vUv.x > 0.3 ? skin : mix(team * 0.7, vec3(0.25, 0.2, 0.15), decay); a = 1.0; }
            if (d < 0.22 && vUv.x < -0.65) c = vec3(0.45, 0.04, 0.03);
        } else {
            float d = min(seg(vUv, vec2(-0.7, 0.0), vec2(0.7, 0.0)) - 0.1,
                          min(length(vUv - vec2(-0.75, 0.1)), length(vUv - vec2(0.75, -0.1))) - 0.17);
            if (d < 0.0) { c = vec3(0.86, 0.83, 0.74); a = 1.0; }
        }
        color = vec4(c, a);
    }
    if (color.a < 0.01) discard;
    oColor = vec4(mix(color.rgb, kSky, vFog), color.a);
}
)";

// ---------------------------------------------------------------- arrows
// Per instance: pos, dir, info (rest seconds, state: 0 flying, 1 stuck, 2 lying, 3 empty).
inline const char* kArrowVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iDir;
layout(location = 2) in vec2 iInfo;
out float vAlong;
out float vFog;
flat out float vStuck;
flat out float vAge;
void main() {
    float state = iInfo.y;
    if (state > 2.5 || dot(iDir, iDir) < 1e-6) { gl_Position = kCulled; return; }
    float dist = distance(uEye, iPos);
    float px = uPxScale * dist;
    vec3 dir = normalize(iDir);
    vec3 tip, tail;
    float width;
    if (state < 0.5) {
        // In flight: never shorter than a visible streak.
        float len = max(0.85, px * 14.0);
        width = max(0.045, px * 2.0);
        tip = iPos;
        tail = iPos - dir * len;
    } else {
        if (0.85 < px * 2.0 || dist > uCamDist * 2.0 + 200.0) { gl_Position = kCulled; return; }
        width = max(0.03, px * 1.4);
        vec3 base = vec3(iPos.xy, groundZ(iPos.xy));
        if (state < 1.5) {
            // Stuck: buried tip first, shaft standing out of the ground.
            vec3 d = normalize(vec3(dir.xy * 0.8, min(dir.z, -0.55)));
            tip = base + d * 0.22;
            tail = base - d * 0.62;
        } else {
            vec3 d = normalize(vec3(dir.xy, 0.0) + vec3(1e-4, 0.0, 0.0));
            tip = base + d * 0.42 + vec3(0.0, 0.0, 0.03);
            tail = base - d * 0.42 + vec3(0.0, 0.0, 0.03);
        }
    }
    vec3 side = normalize(cross(tail - tip, uEye - iPos) + vec3(0.0, 0.0, 1e-5)) * width;
    float t = float(gl_VertexID & 1);
    float s = float((gl_VertexID >> 1) & 1) * 2.0 - 1.0;
    vec3 world = mix(tail, tip, t) + side * s * 0.5;
    vAlong = t;
    vStuck = state;
    vAge = max(uGameSeconds - iInfo.x, 0.0);
    vFog = fogFactor(dist);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kArrowFs = R"(
in float vAlong;
in float vFog;
flat in float vStuck;
flat in float vAge;
out vec4 oColor;
void main() {
    vec3 shaft = vec3(0.50, 0.36, 0.20);
    vec3 fletch = vec3(0.95, 0.93, 0.86);
    vec3 c = vAlong < 0.2 ? fletch : shaft;
    if (vAlong > 0.92) c = vec3(0.35);
    // Weathering: shafts grey and darken over days.
    if (vStuck > 0.5) c = mix(c, vec3(0.30, 0.28, 0.25), smoothstep(3600.0, 86400.0 * 3.0, vAge));
    oColor = vec4(mix(c, kSky, vFog), 1.0);
}
)";

// ---------------------------------------------------------------- sprites
// Per instance: feet position, team colour, info (plan, moving, mirror, phase),
// style (weapon, head, shield, flags), body colour, size (height, width), selected.
inline const char* kSpriteVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iColor;
layout(location = 2) in vec4 iInfo;
layout(location = 3) in vec4 iStyle;
layout(location = 4) in vec3 iBody;
layout(location = 5) in vec2 iSize;
layout(location = 6) in float iSelected;
out vec2 vUv;
flat out vec3 vColor;
flat out vec4 vInfo;
flat out vec4 vStyle;
flat out vec3 vBody;
flat out float vAspect;
flat out float vDot;
flat out float vSelected;
out float vFog;
void main() {
    float height = iSize.x;
    float dist = distance(uEye, iPos);
    float px = uPxScale * dist;
    // Never smaller than a few pixels: zoomed out, armies read as ants.
    float minSize = px * 4.5;
    float size = max(height, minSize);
    bool isUnit = iInfo.x < 8.5;
    vDot = (isUnit && height < minSize * 1.6) ? 1.0 : 0.0;
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    float aspect = vDot > 0.5 ? 1.0 : iSize.y;
    float width = size * aspect;
    // Projectiles and gibs are centred on their position; units stand on it.
    vec3 base = iPos + (vDot > 0.5 ? vec3(0.0, 0.0, size * 0.25) : vec3(0.0)) - (isUnit ? vec3(0.0) : vec3(0.0, 0.0, size * 0.5));
    vec3 world = base + uCamRight * ((corner.x - 0.5) * width) + vec3(0.0, 0.0, corner.y * size);
    vUv = corner;
    vColor = iColor;
    vInfo = iInfo;
    vStyle = iStyle;
    vBody = iBody;
    vAspect = aspect;
    vSelected = iSelected;
    vFog = fogFactor(dist);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kSpriteFs = R"(
in vec2 vUv;
flat in vec3 vColor;
flat in vec4 vInfo;
flat in vec4 vStyle;
flat in vec3 vBody;
flat in float vAspect;
flat in float vDot;
flat in float vSelected;
in float vFog;
out vec4 oColor;

vec3 gCol;
float gA;
// Paint a shape (signed distance d < 0 inside) with a thin dark outline.
void paint(float d, vec3 c) {
    if (d < 0.0) { gCol = d > -0.012 ? c * 0.45 : c; gA = 1.0; }
}
float sdSeg(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0)) - r;
}
float sdBox(vec2 p, vec2 c, vec2 h) {
    vec2 d = abs(p - c) - h;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
float sdCircle(vec2 p, vec2 c, float r) { return length(p - c) - r; }
float sdEll(vec2 p, vec2 c, vec2 r) { return (length((p - c) / r) - 1.0) * min(r.x, r.y); }
float sdTri(vec2 p, vec2 a, vec2 b, vec2 c) {
    vec2 e0 = b - a, e1 = c - b, e2 = a - c;
    vec2 v0 = p - a, v1 = p - b, v2 = p - c;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}
bool flag(float flags, float bit) { return mod(floor(flags / bit), 2.0) > 0.5; }

const vec3 kSkin = vec3(0.86, 0.68, 0.55);
const vec3 kSteel = vec3(0.66, 0.68, 0.72);
const vec3 kWood = vec3(0.45, 0.30, 0.15);
const vec3 kLeg = vec3(0.24, 0.19, 0.14);
const vec3 kGold = vec3(0.95, 0.78, 0.25);

// Weapon held in the front hand at h.
void weapon(vec2 q, float w, vec2 h, float bulk) {
    if (w < 0.5) return;
    if (w < 1.5) { paint(sdSeg(q, h, h + vec2(0.1, 0.34), 0.018), kSteel); paint(sdSeg(q, h - vec2(0.03, 0.0), h + vec2(0.03, 0.0), 0.014), kGold); }
    else if (w < 2.5) { paint(sdSeg(q, h - vec2(0.02, 0.06), h + vec2(0.07, 0.3), 0.016), kWood); paint(sdEll(q, h + vec2(0.1, 0.27), vec2(0.07, 0.055) * bulk), kSteel); }
    else if (w < 3.5) { paint(sdSeg(q, h - vec2(0.09, 0.25), h + vec2(0.13, 0.43), 0.013), kWood); paint(sdTri(q, h + vec2(0.11, 0.39), h + vec2(0.16, 0.39), h + vec2(0.15, 0.47)), kSteel); }
    else if (w < 4.5) {
        float br = length(q - (h + vec2(-0.08, 0.04)));
        if (q.x > h.x - 0.02 && br > 0.2 && br < 0.235) { gCol = vec3(0.52, 0.34, 0.16); gA = 1.0; }
        if (abs(q.x - (h.x - 0.06)) < 0.007 && abs(q.y - h.y - 0.04) < 0.19) { gCol = vec3(0.92); gA = 1.0; }
    }
    else if (w < 5.5) { paint(sdSeg(q, vec2(h.x + 0.02, 0.04), h + vec2(0.04, 0.36), 0.016), kWood); paint(sdCircle(q, h + vec2(0.04, 0.39), 0.045), vec3(1.0, 0.6, 0.15)); }
    else if (w < 6.5) { paint(sdSeg(q, h, h + vec2(0.05, 0.12), 0.012), kWood); paint(sdEll(q, h + vec2(0.09, 0.13), vec2(0.05, 0.02)), kSteel); paint(sdEll(q, h + vec2(-0.12, -0.05), vec2(0.06, 0.05)), vec3(0.4, 0.3, 0.15)); }
    else if (w < 7.5) {
        paint(sdCircle(q, h + vec2(0.02, 0.02), 0.06), vec3(0.32, 0.22, 0.12));
        paint(sdSeg(q, h + vec2(0.04, 0.07), h + vec2(0.08, 0.12), 0.006), vec3(0.8));
        if (sdCircle(q, h + vec2(0.085, 0.13), 0.02 + 0.01 * sin(uTime * 30.0)) < 0.0) { gCol = vec3(1.0, 0.85, 0.3); gA = 1.0; }
    }
    else if (w < 8.5) { paint(sdSeg(q, h - vec2(0.02, 0.05), h + vec2(0.12, 0.34), 0.04 * bulk), vec3(0.38, 0.26, 0.14)); }
    else if (w < 9.5) { paint(sdSeg(q, h - vec2(0.05, 0.2), h + vec2(0.08, 0.42), 0.014), vec3(0.2)); paint(sdBox(q, h + vec2(0.08, 0.43), vec2(0.05, 0.008)), vec3(0.2)); paint(sdSeg(q, h + vec2(0.03, 0.43), h + vec2(0.03, 0.5), 0.008), vec3(0.2)); paint(sdSeg(q, h + vec2(0.13, 0.43), h + vec2(0.13, 0.5), 0.008), vec3(0.2)); }
    else if (w < 10.5) { paint(sdSeg(q, h - vec2(0.02, 0.04), h + vec2(0.16, 0.48), 0.026), kSteel * 1.1); paint(sdSeg(q, h + vec2(-0.04, 0.02), h + vec2(0.06, -0.01), 0.016), kGold); }
    else { paint(sdSeg(q, h - vec2(0.15, 0.04), h + vec2(0.45, 0.12), 0.02), kWood); paint(sdTri(q, h + vec2(0.4, 0.1), h + vec2(0.4, 0.14), h + vec2(0.52, 0.125)), kSteel); }
}

// Standing figure, facing +x, feet at y = 0, top of head near y = 1.
void humanoid(vec2 q, vec3 team, vec4 style, vec3 body, bool moving, float phase) {
    float flags = style.w;
    float bulk = flag(flags, 4.0) ? 1.35 : 1.0;
    bool robe = flag(flags, 1.0);
    bool monster = flag(flags, 4.0) && (style.y > 2.5 && style.y < 3.5 || style.y > 6.5 && style.y < 7.5 || style.x > 7.5 && style.x < 8.5);
    vec3 skin = body;
    float swing = moving ? sin(uTime * 9.0 + phase) * 0.07 : 0.0;
    if (flag(flags, 2.0)) {
        float flap = sin(uTime * 6.0 + phase) * 0.06;
        vec3 wing = style.y > 5.5 && style.y < 6.5 ? vec3(0.96, 0.95, 0.9) : vec3(0.18, 0.08, 0.12);
        paint(sdEll(q, vec2(-0.22, 0.74 + flap), vec2(0.3, 0.1)), wing);
        paint(sdEll(q, vec2(-0.1, 0.84 + flap), vec2(0.22, 0.08)), wing * 0.9);
    }
    if (flag(flags, 32.0)) paint(sdSeg(q, vec2(-0.08, 0.45), vec2(-0.28, 0.2 + swing), 0.018), skin * 0.8);
    if (!robe) {
        vec3 legC = monster || style.x > 7.5 && style.x < 8.5 ? skin * 0.8 : kLeg;
        paint(sdSeg(q, vec2(-0.05, 0.46), vec2(-0.07 + swing, 0.04), 0.045 * bulk), legC * 0.85);
        paint(sdSeg(q, vec2(0.05, 0.46), vec2(0.07 - swing, 0.04), 0.045 * bulk), legC);
    }
    // Back arm.
    paint(sdSeg(q, vec2(-0.1 * bulk, 0.72), vec2(-0.15 * bulk, 0.5), 0.035 * bulk), monster ? skin * 0.85 : team * 0.7);
    if (robe) {
        float hw = 0.11 + (0.66 - q.y) * 0.14;
        paint(max(sdBox(q, vec2(0.0, 0.34), vec2(0.3, 0.32)), abs(q.x) - hw), team * 0.75);
    }
    vec3 torso = monster ? skin : (flag(flags, 64.0) ? kSkin : team);
    paint(sdBox(q, vec2(0.0, 0.6), vec2(0.12 * bulk, 0.15)) - 0.02, torso);
    if (monster || flag(flags, 64.0)) paint(sdBox(q, vec2(0.0, 0.47), vec2(0.13 * bulk, 0.05)), team);
    else paint(sdBox(q, vec2(0.0, 0.47), vec2(0.125 * bulk, 0.02)), vec3(0.3, 0.22, 0.12));
    // Head.
    float head = style.y;
    vec3 face = (head > 4.5 && head < 5.5) ? vec3(0.86, 0.83, 0.72) : (monster ? skin : kSkin);
    vec2 hc = vec2(0.0, 0.85);
    float hr = 0.085 * (bulk > 1.0 ? 1.1 : 1.0);
    if (head > 2.5 && head < 3.5) {
        paint(sdEll(q, hc, vec2(0.1, 0.085)), skin);
        paint(sdEll(q, hc + vec2(0.08, -0.03), vec2(0.06, 0.045)), skin * 0.7);
        paint(sdSeg(q, hc + vec2(-0.04, 0.06), hc + vec2(-0.13, 0.13), 0.016), vec3(0.9, 0.86, 0.75));
        paint(sdSeg(q, hc + vec2(0.03, 0.06), hc + vec2(0.11, 0.14), 0.016), vec3(0.9, 0.86, 0.75));
    } else {
        paint(sdCircle(q, hc, hr), face);
        if (head > 4.5 && head < 5.5) {
            vec3 eye = flag(flags, 16.0) ? vec3(0.4, 1.0, 0.6) : vec3(0.1);
            if (length(q - hc - vec2(0.035, 0.01)) < 0.018) { gCol = eye; gA = 1.0; }
        }
    }
    if (head > 0.5 && head < 1.5) { paint(max(sdCircle(q, hc + vec2(0.0, 0.005), hr + 0.012), hc.y - q.y), kSteel); paint(sdBox(q, hc + vec2(0.05, -0.02), vec2(0.008, 0.04)), kSteel); }
    else if (head > 1.5 && head < 2.5) { paint(max(sdCircle(q, hc + vec2(-0.01, 0.01), hr + 0.02), (hc.y - 0.02) - q.y + (q.x - hc.x) * 0.6), team * 0.55); }
    else if (head > 3.5 && head < 4.5) { paint(sdTri(q, hc + vec2(-0.12, 0.05), hc + vec2(0.12, 0.05), hc + vec2(-0.02, 0.2)), team * 0.6); }
    else if (head > 5.5 && head < 6.5) { float d = abs(length((q - hc - vec2(0.0, 0.13)) / vec2(1.0, 0.35)) - 0.07); if (d < 0.012) { gCol = kGold; gA = 1.0; } }
    else if (head > 6.5 && head < 7.5) { paint(sdTri(q, hc + vec2(-0.06, 0.05), hc + vec2(-0.02, 0.06), hc + vec2(-0.08, 0.15)), vec3(0.15)); paint(sdTri(q, hc + vec2(0.02, 0.06), hc + vec2(0.06, 0.05), hc + vec2(0.08, 0.15)), vec3(0.15)); }
    else if (head > 7.5 && head < 8.5) { paint(max(sdCircle(q, hc, hr + 0.014), hc.y - q.y), kSteel); paint(sdEll(q, hc + vec2(0.03, -0.07), vec2(0.07, 0.07)), vec3(0.62, 0.36, 0.16)); }
    else if (head > 8.5) { paint(sdBox(q, hc + vec2(0.0, 0.07), vec2(0.07, 0.02)), kGold); paint(sdTri(q, hc + vec2(-0.07, 0.08), hc + vec2(-0.03, 0.08), hc + vec2(-0.05, 0.13)), kGold); paint(sdTri(q, hc + vec2(0.03, 0.08), hc + vec2(0.07, 0.08), hc + vec2(0.05, 0.13)), kGold); }
    // Front arm, weapon, shield.
    vec2 hand = vec2(0.16 * bulk, 0.55);
    paint(sdSeg(q, vec2(0.1 * bulk, 0.72), hand, 0.035 * bulk), monster ? skin : (flag(flags, 64.0) ? kSkin : team * 0.85));
    weapon(q, style.x, hand, bulk);
    if (style.z > 0.5) {
        vec2 sc = vec2(0.13 * bulk, 0.55);
        float d = style.z < 1.5 ? sdCircle(q, sc, 0.11) : sdEll(q, sc - vec2(0.0, 0.03), vec2(0.13, 0.19));
        if (d < 0.0) { gCol = d > -0.025 ? kSteel * 0.8 : team * 0.65; gA = 1.0; }
        paint(sdCircle(q, sc, 0.03), kSteel);
    }
}

// A horse (or unicorn) facing +x, hooves at y = 0, back near y = 0.55.
void horse(vec2 q, vec3 coat, bool moving, float phase, bool barding, vec3 team) {
    float g = moving ? sin(uTime * 12.0 + phase) : 0.0;
    paint(sdSeg(q, vec2(-0.4, 0.5), vec2(-0.55, 0.28 + g * 0.03), 0.03), coat * 0.5);
    paint(sdSeg(q, vec2(-0.3, 0.4), vec2(-0.32 - g * 0.1, 0.02), 0.04), coat * 0.8);
    paint(sdSeg(q, vec2(0.28, 0.4), vec2(0.3 + g * 0.1, 0.02), 0.04), coat * 0.8);
    paint(sdEll(q, vec2(-0.02, 0.45), vec2(0.42, 0.13)), coat);
    paint(sdSeg(q, vec2(-0.24, 0.4), vec2(-0.22 + g * 0.1, 0.02), 0.04), coat * 0.9);
    paint(sdSeg(q, vec2(0.22, 0.4), vec2(0.2 - g * 0.1, 0.02), 0.04), coat * 0.9);
    paint(sdSeg(q, vec2(0.3, 0.5), vec2(0.44, 0.69), 0.07), coat);
    paint(sdSeg(q, vec2(0.45, 0.69), vec2(0.6, 0.62), 0.055), coat);
    paint(sdTri(q, vec2(0.4, 0.74), vec2(0.45, 0.74), vec2(0.42, 0.8)), coat * 0.8);
    if (barding) paint(max(sdEll(q, vec2(-0.02, 0.45), vec2(0.43, 0.14)), q.y - 0.47), team * 0.8);
}

void rider(vec2 q, vec3 team, vec4 style, vec2 seat, bool lance) {
    paint(sdSeg(q, seat + vec2(0.0, 0.0), seat + vec2(0.08, -0.14), 0.035), kLeg);
    paint(sdBox(q, seat + vec2(0.0, 0.13), vec2(0.07, 0.12)) - 0.015, team);
    vec2 hc = seat + vec2(0.0, 0.31);
    paint(sdCircle(q, hc, 0.06), kSkin);
    if (style.y > 0.5 && style.y < 1.5) paint(max(sdCircle(q, hc, 0.07), hc.y - q.y), kSteel);
    vec2 hand = seat + vec2(0.12, 0.1);
    paint(sdSeg(q, seat + vec2(0.05, 0.2), hand, 0.028), team * 0.85);
    weapon(q, style.x, hand, 1.0);
    if (style.z > 0.5) { paint(sdCircle(q, seat + vec2(0.07, 0.1), 0.08), team * 0.65); paint(sdCircle(q, seat + vec2(0.07, 0.1), 0.02), kSteel); }
}

void wings(vec2 q, vec2 root, vec3 c, float phase, float span) {
    float flap = sin(uTime * 7.0 + phase);
    vec2 tip = root + vec2(-0.25 * span, (0.3 + flap * 0.18) * span);
    paint(sdTri(q, root + vec2(0.12, 0.0), root - vec2(0.12, 0.02), tip), c);
    paint(sdTri(q, root + vec2(0.08, 0.02), root - vec2(0.18, 0.0), tip + vec2(-0.12, -0.12 * span)), c * 0.85);
}

void main() {
    vec3 team = vColor * (vSelected > 0.5 ? 1.35 : 1.0);
    if (vDot > 0.5) {
        float r = length(vUv - 0.5);
        if (r > 0.5) discard;
        oColor = vec4(mix(team * (r > 0.38 ? 0.45 : 1.0), kSky, vFog * 0.5), 1.0);
        return;
    }
    float plan = vInfo.x;
    bool moving = vInfo.y > 0.5;
    float phase = vInfo.w;
    // Local space: x across (centred, in units of height), y up from the feet.
    vec2 q = vec2((vUv.x - 0.5) * vAspect, vUv.y);
    if (vInfo.z > 0.5) q.x = -q.x;
    gCol = vec3(0.0);
    gA = 0.0;
    vec3 body = vBody;

    if (plan < 0.5) {
        humanoid(q, team, vStyle, body, moving, phase);
    } else if (plan < 1.5) {
        horse(q, body, moving, phase, flag(vStyle.w, 8.0), team);
        rider(q, team, vStyle, vec2(-0.02, 0.6), vStyle.x > 10.5);
    } else if (plan < 2.5) {
        // Wolf rider: grey wolf, small goblin rider.
        float g = moving ? sin(uTime * 13.0 + phase) : 0.0;
        paint(sdSeg(q, vec2(-0.32, 0.4), vec2(-0.45, 0.58), 0.04), body * 0.8);
        paint(sdSeg(q, vec2(-0.22, 0.3), vec2(-0.25 - g * 0.1, 0.02), 0.04), body * 0.75);
        paint(sdSeg(q, vec2(0.2, 0.3), vec2(0.23 + g * 0.1, 0.02), 0.04), body * 0.75);
        paint(sdEll(q, vec2(0.0, 0.35), vec2(0.34, 0.11)), body);
        paint(sdSeg(q, vec2(-0.16, 0.3), vec2(-0.14 + g * 0.1, 0.02), 0.035), body * 0.9);
        paint(sdSeg(q, vec2(0.14, 0.3), vec2(0.12 - g * 0.1, 0.02), 0.035), body * 0.9);
        paint(sdEll(q, vec2(0.38, 0.44), vec2(0.11, 0.075)), body);
        paint(sdTri(q, vec2(0.44, 0.42), vec2(0.44, 0.47), vec2(0.56, 0.42)), body * 0.85);
        paint(sdTri(q, vec2(0.32, 0.5), vec2(0.37, 0.5), vec2(0.33, 0.6)), body * 0.7);
        if (length(q - vec2(0.42, 0.46)) < 0.015) { gCol = vec3(1.0, 0.8, 0.1); gA = 1.0; }
        vec4 st = vStyle;
        rider(q, team, st, vec2(-0.03, 0.47), false);
        if (sdCircle(q, vec2(-0.03, 0.78), 0.06) < 0.0) { gCol = vec3(0.45, 0.6, 0.25); gA = 1.0; }
    } else if (plan < 3.5) {
        horse(q, body, moving, phase, false, team);
        paint(sdBox(q, vec2(-0.04, 0.58), vec2(0.16, 0.035)), team);
        paint(sdSeg(q, vec2(0.58, 0.68), vec2(0.7, 0.86), 0.012), kGold);
        paint(sdSeg(q, vec2(0.36, 0.72), vec2(0.28, 0.55), 0.03), vec3(0.95, 0.9, 1.0));
    } else if (plan < 4.5) {
        wings(q, vec2(-0.05, 0.55), vec3(0.97, 0.97, 1.0), phase, 1.0);
        horse(q, body, moving, phase, false, team);
        rider(q, team, vStyle, vec2(-0.02, 0.6), false);
    } else if (plan < 5.5) {
        // Elephant with a howdah.
        vec3 c = body;
        paint(sdSeg(q, vec2(-0.36, 0.45), vec2(-0.45, 0.25), 0.02), c * 0.6);
        paint(sdSeg(q, vec2(-0.25, 0.35), vec2(-0.25, 0.02), 0.07), c * 0.8);
        paint(sdSeg(q, vec2(0.2, 0.35), vec2(0.2, 0.02), 0.07), c * 0.8);
        paint(sdEll(q, vec2(-0.02, 0.45), vec2(0.38, 0.2)), c);
        paint(sdSeg(q, vec2(-0.12, 0.35), vec2(-0.12, 0.02), 0.07), c * 0.9);
        paint(sdSeg(q, vec2(0.1, 0.35), vec2(0.1, 0.02), 0.07), c * 0.9);
        paint(sdCircle(q, vec2(0.34, 0.5), 0.14), c);
        paint(sdSeg(q, vec2(0.44, 0.45), vec2(0.5, 0.1), 0.04), c * 0.95);
        paint(sdSeg(q, vec2(0.4, 0.4), vec2(0.52, 0.3), 0.015), vec3(0.95, 0.93, 0.85));
        paint(sdEll(q, vec2(0.28, 0.52), vec2(0.08, 0.12)), c * 0.8);
        paint(sdBox(q, vec2(-0.06, 0.7), vec2(0.17, 0.07)), team);
        paint(sdBox(q, vec2(-0.06, 0.78), vec2(0.18, 0.012)), kGold);
        paint(sdCircle(q, vec2(-0.04, 0.88), 0.05), kSkin);
    } else if (plan < 6.5) {
        // Giant spider.
        for (int i = 0; i < 4; ++i) {
            float fx = -0.3 + float(i) * 0.2;
            float lift = moving ? 0.04 * sin(uTime * 14.0 + float(i) * 1.7 + phase) : 0.0;
            vec2 knee = vec2(fx * 0.9, 0.78 + lift);
            paint(sdSeg(q, vec2(fx * 0.4, 0.42), knee, 0.022), body * 1.4);
            paint(sdSeg(q, knee, vec2(fx * 1.9, 0.02), 0.018), body * 1.4);
        }
        paint(sdEll(q, vec2(-0.18, 0.46), vec2(0.24, 0.2)), body);
        paint(sdTri(q, vec2(-0.24, 0.42), vec2(-0.12, 0.42), vec2(-0.18, 0.55)), team);
        paint(sdTri(q, vec2(-0.24, 0.42), vec2(-0.12, 0.42), vec2(-0.18, 0.3)), team);
        paint(sdEll(q, vec2(0.12, 0.4), vec2(0.13, 0.11)), body * 1.2);
        if (length(q - vec2(0.2, 0.43)) < 0.02 || length(q - vec2(0.17, 0.45)) < 0.015) { gCol = vec3(0.9, 0.1, 0.05); gA = 1.0; }
    } else if (plan < 7.5) {
        // Griffin: lion body, eagle head, great wings.
        wings(q, vec2(-0.05, 0.5), vec3(0.45, 0.32, 0.18), phase, 1.2);
        horse(q, vec3(0.72, 0.58, 0.35), moving, phase, false, team);
        paint(sdCircle(q, vec2(0.5, 0.66), 0.09), vec3(0.95, 0.94, 0.9));
        paint(sdTri(q, vec2(0.56, 0.68), vec2(0.56, 0.62), vec2(0.67, 0.62)), vec3(0.95, 0.75, 0.15));
        paint(sdBox(q, vec2(0.38, 0.6), vec2(0.07, 0.025)), team);
    } else {
        // Dragon.
        wings(q, vec2(-0.05, 0.52), mix(body * 0.7, team * 0.6, 0.3), phase, 1.5);
        paint(sdSeg(q, vec2(-0.3, 0.4), vec2(-0.85, 0.22), 0.05), body * 0.9);
        paint(sdSeg(q, vec2(-0.2, 0.32), vec2(-0.22, 0.04), 0.05), body * 0.8);
        paint(sdSeg(q, vec2(0.18, 0.32), vec2(0.2, 0.04), 0.05), body * 0.8);
        paint(sdEll(q, vec2(-0.05, 0.42), vec2(0.34, 0.15)), body);
        paint(sdEll(q, vec2(-0.05, 0.36), vec2(0.28, 0.07)), team * 0.7);
        paint(sdSeg(q, vec2(0.22, 0.48), vec2(0.5, 0.74), 0.07), body);
        paint(sdEll(q, vec2(0.6, 0.76), vec2(0.13, 0.065)), body);
        paint(sdTri(q, vec2(0.52, 0.8), vec2(0.56, 0.82), vec2(0.48, 0.92)), body * 0.6);
        if (length(q - vec2(0.62, 0.79)) < 0.015) { gCol = vec3(1.0, 0.85, 0.1); gA = 1.0; }
    }

    if (plan > 8.5) {
        vec2 c = vec2(0.0, 0.5);
        float r = length(q - c);
        if (plan < 9.5) {
            // Satchel charge with a fizzing fuse.
            paint(sdCircle(q, c, 0.35), vec3(0.35, 0.24, 0.12));
            paint(sdBox(q, c, vec2(0.36, 0.06)), vec3(0.2, 0.15, 0.08));
            if (length(q - vec2(0.18, 0.92)) < 0.08 + 0.04 * sin(uTime * 40.0)) { gCol = vec3(1.0, 0.9, 0.4); gA = 1.0; }
        } else if (plan < 10.5) {
            // Fireball.
            if (r < 0.5) { gCol = mix(vec3(1.0, 0.95, 0.6), vec3(1.0, 0.35, 0.05), r * 2.0) * 1.3; gA = 1.0; }
        } else {
            // Body part tumbling through the air: style.x = kind.
            float k = vStyle.x;
            if (k < 2.5) { paint(sdCircle(q, c, 0.32), kSkin); if (q.x < -0.15 && r < 0.32) gCol = vec3(0.5, 0.03, 0.03); }
            else if (k < 4.5) { paint(sdSeg(q, c - vec2(0.38, 0.1), c + vec2(0.38, 0.1), 0.13), k < 3.5 ? team : kLeg); paint(sdCircle(q, c + vec2(0.38, 0.1), 0.12), kSkin); }
            else { paint(sdSeg(q, c - vec2(0.36, 0.0), c + vec2(0.36, 0.0), 0.07), vec3(0.88, 0.85, 0.76)); paint(sdCircle(q, c - vec2(0.38, 0.0), 0.12), vec3(0.88, 0.85, 0.76)); paint(sdCircle(q, c + vec2(0.38, 0.0), 0.12), vec3(0.88, 0.85, 0.76)); }
        }
    }
    if (gA < 0.5) discard;
    oColor = vec4(mix(gCol, kSky, vFog), 1.0);
}
)";

// ---------------------------------------------------------------- particles
// Per instance: pos, info (kind, age 0..1, size, seed). Premultiplied alpha:
// glowing kinds output zero alpha and so add light.
inline const char* kParticleVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec4 iInfo;
out vec2 vUv;
flat out vec4 vInfo;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    float dist = distance(uEye, iPos);
    float size = max(iInfo.z, uPxScale * dist * 2.5);
    vec3 up = normalize(cross(uCamRight, iPos - uEye));
    vec3 world = iPos + (uCamRight * corner.x + up * corner.y) * size;
    vUv = corner;
    vInfo = iInfo;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kParticleFs = R"(
in vec2 vUv;
flat in vec4 vInfo;
out vec4 oColor;
void main() {
    float r = length(vUv);
    if (r > 1.0) discard;
    float kind = vInfo.x, age = vInfo.y;
    float soft = 1.0 - smoothstep(0.4, 1.0, r);
    if (kind < 0.5) {         // fire
        vec3 c = mix(vec3(1.0, 0.9, 0.45), vec3(0.9, 0.2, 0.02), age);
        float a = soft * (1.0 - age);
        oColor = vec4(c * a * 1.4, 0.0);
    } else if (kind < 1.5) {  // smoke
        float a = soft * 0.55 * (1.0 - age) * smoothstep(0.0, 0.1, age);
        vec3 c = vec3(0.32, 0.3, 0.28);
        oColor = vec4(c * a, a);
    } else if (kind < 2.5) {  // spark
        float a = soft * (1.0 - age);
        oColor = vec4(vec3(1.0, 0.85, 0.4) * a * 2.0, 0.0);
    } else if (kind < 3.5) {  // dirt
        float a = step(r, 0.8) * (1.0 - smoothstep(0.7, 1.0, age));
        oColor = vec4(vec3(0.3, 0.22, 0.14) * a, a);
    } else if (kind < 4.5) {  // healing light
        float a = soft * (1.0 - age);
        oColor = vec4(vec3(0.45, 1.0, 0.55) * a * 1.5, 0.0);
    } else {                  // flash
        float a = soft * (1.0 - age);
        oColor = vec4(vec3(1.0, 0.95, 0.75) * a * 2.0, 0.0);
    }
}
)";

}  // namespace illyria::game::shaders
