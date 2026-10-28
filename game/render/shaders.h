#pragma once

// GLSL ES 3.00 (WebGL 2) shader sources for the battle renderer. Every program
// is prefixed with kCommon.

namespace illyria::game::shaders {

inline const char* kCommon = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DShadow;
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
uniform float uWater;         // water level

// Graphics settings (Renderer::Feature bits): cosmetic only, never gameplay.
uniform int uFeatures;
const int kFeatGroundDetail = 1, kFeatCover = 2, kFeatShadows = 4, kFeatFarDetail = 16, kFeatAtmosphere = 32;
bool feature(int f) { return (uFeatures & f) != 0; }
// Sun shadows: a depth map over the area around the camera target.
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightViewProj;
uniform vec4 uShadowInfo;  // texel size (uv), fade start, fade end (metres from centre), depth bias
uniform vec2 uFocus;       // camera target on the ground

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

// 1 in full sun, toward 0 in shadow. Five taps of hardware 2x2 PCF.
float shadowAt(vec3 world, vec3 n) {
    if (!feature(kFeatShadows)) return 1.0;
    float fromFocus = distance(world.xy, uFocus);
    if (fromFocus > uShadowInfo.z) return 1.0;
    // Offset along the normal to keep surfaces from shadowing themselves.
    vec4 l = uLightViewProj * vec4(world + n * 0.08 + kSun * 0.05, 1.0);
    vec3 c = l.xyz / l.w * 0.5 + 0.5;
    if (any(lessThan(c.xy, vec2(0.0))) || any(greaterThan(c.xy, vec2(1.0)))) return 1.0;
    float t = uShadowInfo.x, z = c.z - uShadowInfo.w;
    float s = texture(uShadowMap, vec3(c.xy, z)) * 0.4;
    s += texture(uShadowMap, vec3(c.xy + vec2(t, t), z)) * 0.15;
    s += texture(uShadowMap, vec3(c.xy + vec2(-t, t), z)) * 0.15;
    s += texture(uShadowMap, vec3(c.xy + vec2(t, -t), z)) * 0.15;
    s += texture(uShadowMap, vec3(c.xy + vec2(-t, -t), z)) * 0.15;
    return mix(s, 1.0, smoothstep(uShadowInfo.y, uShadowInfo.z, fromFocus));
}
float noise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
// Drifting cloud shadows over the land: 1 in sun, ~0.62 under a cloud.
float cloudShade(vec2 xy) {
    if (!feature(kFeatAtmosphere)) return 1.0;
    vec2 p = xy / 1400.0 + vec2(uTime * 0.004, uTime * 0.0025);
    float c = noise2(p) * 0.6 + noise2(p * 2.3 + 7.1) * 0.3 + noise2(p * 5.1 + 3.7) * 0.1;
    return 1.0 - 0.38 * smoothstep(0.5, 0.72, c);
}
// Aerial perspective: distant land fades toward the horizon, warmer toward the sun.
vec3 hazeColor(vec3 world) {
    if (!feature(kFeatAtmosphere)) return kSky;
    vec3 v = normalize(world - uEye);
    float sun = pow(max(dot(v, kSun), 0.0), 6.0);
    return mix(kSky * 1.02, vec3(0.92, 0.84, 0.72), sun * 0.35);
}
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
uniform sampler2D uGround;  // R8: ground type / 8, one texel per terrain cell
uniform sampler2D uRoad;    // RG8, linear: R = 0.5 + lateral offset / 16 m, G = road weight

// Plains, Road, Forest, Swamp, Shallows, DeepWater, Rock.
vec3 groundColor(int id, float broad, float fine) {
    vec3 grass = mix(vec3(0.30, 0.42, 0.17), vec3(0.42, 0.50, 0.22), broad) * (0.9 + 0.2 * fine);
    if (id == 1) return vec3(0.46, 0.38, 0.26) * (0.85 + 0.3 * fine);
    if (id == 2) return mix(vec3(0.16, 0.24, 0.10), vec3(0.24, 0.20, 0.12), fine) * (0.9 + 0.2 * broad);
    if (id == 3) return mix(vec3(0.24, 0.30, 0.16), vec3(0.20, 0.24, 0.18), fine);
    if (id == 4) return vec3(0.52, 0.46, 0.34) * (0.85 + 0.3 * fine);
    if (id == 5) return vec3(0.30, 0.28, 0.24);
    if (id == 6) return vec3(0.48, 0.46, 0.43) * (0.8 + 0.4 * fine);
    return grass;
}
int groundAt(ivec2 c) {
    c = clamp(c, ivec2(0), ivec2(int(uCells) - 1));
    return int(texelFetch(uGround, c, 0).r * 8.0 + 0.5);
}

uniform sampler2D uCanopy;  // R8: tree crown cover over the battlefield, 2 m texels
uniform sampler2D uWaterTex;  // R32F water surface per sample; very negative where dry

// Fades a pattern of feature size `size` metres out before it shimmers.
float detailFade(float size, float px) { return 1.0 - smoothstep(size * 0.35, size * 1.2, px); }

void main() {
    vec3 n = normalize(vNormal);
    float slope = 1.0 - n.z;
    vec2 p = vWorld.xy;
    float dist = distance(uEye, vWorld);
    float px = max(fwidth(p.x), fwidth(p.y));  // metres per pixel here
    float broad = valueNoise(p * 0.02);
    float fine = valueNoise(p * 0.35);
    // Blend the ground colours of the four nearest cells, and their kinds.
    vec2 c = p / uCellSize - 0.5;
    ivec2 i0 = ivec2(floor(c));
    vec2 f = fract(c);
    int g00 = groundAt(i0), g10 = groundAt(i0 + ivec2(1, 0)), g01 = groundAt(i0 + ivec2(0, 1)), g11 = groundAt(i0 + ivec2(1, 1));
    vec3 albedo = mix(mix(groundColor(g00, broad, fine), groundColor(g10, broad, fine), f.x),
                      mix(groundColor(g01, broad, fine), groundColor(g11, broad, fine), f.x), f.y);
    vec4 w00 = vec4(g00 == 0, g00 == 2, g00 == 6, g00 == 3), w10 = vec4(g10 == 0, g10 == 2, g10 == 6, g10 == 3);
    vec4 w01 = vec4(g01 == 0, g01 == 2, g01 == 6, g01 == 3), w11 = vec4(g11 == 0, g11 == 2, g11 == 6, g11 == 3);
    vec4 kind = mix(mix(w00, w10, f.x), mix(w01, w11, f.x), f.y);  // plains, forest, rock, swamp
    float canopy = texture(uCanopy, p / (uCells * uCellSize)).r;

    // Meadows are not one green: lush and dry patches at a few scales (always
    // on; it is what keeps mid zoom from looking like felt).
    float lush = valueNoise(p * 0.045 + 3.1) * 0.65 + valueNoise(p * 0.13 + 8.7) * 0.35;
    vec3 dry = vec3(0.50, 0.50, 0.25), wet = vec3(0.24, 0.40, 0.15);
    albedo = mix(albedo, mix(albedo, mix(dry, wet, lush), 0.55), kind.x);

    if (feature(kFeatGroundDetail)) {
        float near = 1.0 - smoothstep(60.0, 220.0, dist);
        // Bare soil and trampled patches in the grass.
        float soilMask = smoothstep(0.7, 0.84, valueNoise(p * 0.085 + 41.0) * 0.6 + valueNoise(p * 0.5 + 5.0) * 0.4);
        vec3 soil = mix(vec3(0.42, 0.33, 0.22), vec3(0.52, 0.43, 0.30), valueNoise(p * 1.7));
        albedo = mix(albedo, soil, soilMask * kind.x * 0.85 * (1.0 - canopy));
        // Grass: clumps, then blades (streaky noise), fading before they alias.
        float clump = valueNoise(p * 0.75 + 13.0);
        float blades = valueNoise(p * vec2(6.3, 2.2) + 1.7) * 0.5 + valueNoise(p * vec2(2.1, 6.9) + 9.3) * 0.5;
        float grass = kind.x * (1.0 - soilMask);
        albedo *= mix(1.0, 0.8 + 0.4 * clump, grass * detailFade(1.3, px));
        albedo *= mix(1.0, 0.78 + 0.44 * blades, grass * detailFade(0.25, px));
        // Wildflowers: sparse dots of white, yellow, violet and red.
        vec2 fc = floor(p / 0.45);
        float fh = hash12(fc + 0.37);
        if (fh > 0.955 && grass > 0.5) {
            vec2 centre = (fc + 0.25 + 0.5 * vec2(hash12(fc + 3.1), hash12(fc + 7.7))) * 0.45;
            float d = length(p - centre);
            float k = hash12(fc + 11.0);
            vec3 petal = k < 0.4 ? vec3(0.95, 0.94, 0.88) : k < 0.7 ? vec3(0.95, 0.82, 0.25) : k < 0.9 ? vec3(0.62, 0.45, 0.85) : vec3(0.85, 0.22, 0.2);
            float spot = (1.0 - smoothstep(0.035, 0.07, d)) * detailFade(0.1, px);
            albedo = mix(albedo, petal, spot * smoothstep(0.4, 0.8, lush));
        }
        // Forest floor: leaf litter, needles and moss.
        vec3 litter = mix(vec3(0.30, 0.22, 0.12), vec3(0.42, 0.30, 0.16), valueNoise(p * 2.7));
        litter = mix(litter, vec3(0.22, 0.30, 0.12), smoothstep(0.55, 0.8, valueNoise(p * 0.6 + 2.0)));
        // From high up the trees are too small to see: show the canopy's green, not the floor.
        // Same crowns-and-gaps pattern as the distant canopy, so the two meet seamlessly.
        float crowns = noise2(p / 9.0) * 0.55 + noise2(p / 9.0 * 2.1 + 3.7) * 0.3 + noise2(p / 9.0 * 4.3 + 9.1) * 0.15;
        litter = mix(litter, mix(vec3(0.10, 0.17, 0.07), vec3(0.24, 0.34, 0.13), crowns), smoothstep(150.0, 600.0, dist));
        albedo = mix(albedo, litter, clamp(kind.y * 0.8 + canopy * 0.7, 0.0, 1.0));
        // Rock: strata and lichen.
        float strata = 0.5 + 0.5 * sin(vWorld.z * 2.6 + valueNoise(p * 0.3) * 6.0);
        vec3 stone = vec3(0.48, 0.46, 0.43) * (0.8 + 0.3 * strata) * (0.85 + 0.3 * valueNoise(p * 3.1));
        stone = mix(stone, vec3(0.62, 0.62, 0.42), smoothstep(0.7, 0.85, valueNoise(p * 1.9 + 4.0)) * 0.6);
        albedo = mix(albedo, stone, max(kind.z, smoothstep(0.3, 0.45, slope)) * 0.8);
        // Swamp: dark wet mud and standing puddles.
        float puddles = smoothstep(0.6, 0.7, valueNoise(p * 0.5 + 20.0));
        albedo = mix(albedo, mix(vec3(0.20, 0.22, 0.14), vec3(0.18, 0.24, 0.26), puddles), kind.w * 0.7);
        // Stones lying in the grass and soil.
        vec2 sc = floor(p / 0.8);
        if (hash12(sc + 5.5) > 0.93) {
            vec2 centre = (sc + 0.3 + 0.4 * vec2(hash12(sc + 1.3), hash12(sc + 2.9))) * 0.8;
            float d = length((p - centre) * vec2(1.0, 1.4));
            albedo = mix(albedo, vec3(0.55, 0.53, 0.5) * (0.8 + 0.4 * hash12(sc)), (1.0 - smoothstep(0.06, 0.11, d)) * detailFade(0.15, px));
        }
        // Bumps that catch the light: tussocks, clods and roots.
        float e = 0.15;
        float h0 = valueNoise(p * 1.6) + 0.5 * valueNoise(p * 4.3 + 2.0);
        float hx = valueNoise((p + vec2(e, 0.0)) * 1.6) + 0.5 * valueNoise((p + vec2(e, 0.0)) * 4.3 + 2.0);
        float hy = valueNoise((p + vec2(0.0, e)) * 1.6) + 0.5 * valueNoise((p + vec2(0.0, e)) * 4.3 + 2.0);
        float bump = 0.06 * near * detailFade(0.4, px);
        n = normalize(n + vec3(-(hx - h0) / e, -(hy - h0) / e, 0.0) * bump);
        slope = 1.0 - n.z;
    }
    // Bare earth and scree on steep slopes.
    albedo = mix(albedo, vec3(0.45, 0.40, 0.33) * (0.85 + 0.3 * fine), smoothstep(0.18, 0.3, slope) * 0.6);
    albedo = mix(albedo, vec3(0.47, 0.45, 0.42) * (0.8 + 0.4 * fine), smoothstep(0.3, 0.45, slope));
    // Wagon-worn roads: packed dirt, two ruts, a grassy hump, puddles in the ruts.
    vec2 road = texture(uRoad, vWorld.xy / (uCells * uCellSize)).rg;
    if (road.g > 0.02) {
        float lateral = (road.r - 0.5) * 16.0;
        float d = abs(lateral);
        float w = fwidth(lateral) + 0.02;
        vec3 dirt = vec3(0.50, 0.41, 0.28) * (0.88 + 0.24 * fine);
        float rut = 1.0 - smoothstep(0.12, 0.12 + w * 2.0, abs(d - 0.85));
        float hump = 1.0 - smoothstep(0.25, 0.45, d);
        dirt = mix(dirt, vec3(0.33, 0.26, 0.17), rut * 0.8);
        dirt = mix(dirt, vec3(0.38, 0.44, 0.22), hump * 0.5);
        float puddle = rut * step(0.78, valueNoise(vWorld.xy * vec2(0.6, 0.6) + 17.0));
        dirt = mix(dirt, vec3(0.35, 0.42, 0.48), puddle * 0.8);
        float edge = 1.0 - smoothstep(2.4, 3.6 + fine * 0.6, d);
        albedo = mix(albedo, dirt, edge * clamp(road.g * 1.5, 0.0, 1.0));
    }
    // Snow on the high peaks.
    albedo = mix(albedo, vec3(0.93, 0.94, 0.96), smoothstep(310.0, 350.0, vWorld.z + fine * 6.0) * smoothstep(0.55, 0.3, slope));
    // Banks: the riverbed under the water, gravel and mud at the waterline,
    // a dark wet band above it (the nearest water surface, not one level).
    {
        ivec2 cc = ivec2(floor(p / uCellSize));
        float surf = -1e9;
        for (int k = 0; k < 4; ++k) surf = max(surf, texelFetch(uWaterTex, clamp(cc + ivec2(k & 1, k >> 1), ivec2(0), ivec2(int(uCells))), 0).r);
        if (feature(kFeatGroundDetail)) {
            for (int k = 0; k < 4; ++k) {
                ivec2 o = ivec2(k == 0 ? 2 : k == 1 ? -1 : 0, k == 2 ? 2 : k == 3 ? -1 : 0);
                surf = max(surf, texelFetch(uWaterTex, clamp(cc + o, ivec2(0), ivec2(int(uCells))), 0).r);
            }
        }
        if (surf > -1000.0) {
            float above = vWorld.z - surf;
            float wet = (1.0 - smoothstep(0.05, 0.8 + fine * 0.5, above)) * step(-0.05, above);
            albedo = mix(albedo, albedo * vec3(0.6, 0.56, 0.5) + vec3(0.03, 0.02, 0.0), wet);
            float line = 1.0 - smoothstep(0.0, 0.22 + fine * 0.1, abs(above - 0.04));
            vec3 gravel = mix(vec3(0.50, 0.46, 0.39), vec3(0.36, 0.30, 0.22), valueNoise(p * 3.3));
            albedo = mix(albedo, gravel, line * 0.75);
            if (above < 0.0) albedo = mix(albedo, mix(vec3(0.40, 0.36, 0.27), vec3(0.30, 0.30, 0.22), valueNoise(p * 0.8)), 0.75);
        }
    }

    // Low sun so the hills read clearly from above; groves shade the ground.
    float sun = max(dot(n, kSun), 0.0) * shadowAt(vWorld, n) * cloudShade(p);
    float ambient = 0.30 * (1.0 - canopy * 0.45);
    if (!feature(kFeatShadows)) sun *= 1.0 - canopy * 0.55;  // no shadow map: fake the groves' shade
    vec3 color = albedo * (ambient + 0.95 * sun);

    // The tactical hex grid fades in as the camera rises, then hands over to
    // the continent's campaign grid (the same one the far terrain draws).
    float gridFade = smoothstep(120.0, 400.0, uCamDist) * (1.0 - smoothstep(900.0, 1800.0, uCamDist)) * 0.35;
    if (gridFade > 0.0) {
        float e = hexEdge(vWorld.xy, 48.0);
        float w = fwidth(e) * 1.5;
        color = mix(color, vec3(0.95, 0.92, 0.8), gridFade * (1.0 - smoothstep(0.0, w, e)));
    }
    float bigFade = smoothstep(1800.0, 4000.0, uCamDist) * 0.3;
    if (bigFade > 0.0) {
        float e = hexEdge(vWorld.xy, 400.0);
        float w = fwidth(e) * 1.5;
        color = mix(color, vec3(0.95, 0.9, 0.78), bigFade * (1.0 - smoothstep(0.0, w, e)));
    }
    oColor = vec4(mix(color, hazeColor(vWorld), fogFactor(dist)), 1.0);
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
    vec3 fresh = vec3(0.42, 0.03, 0.02);
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
        // Scorched crater: a dark pit and a ragged star of blackened spokes
        // thrown out by the blast, slowly greening over months.
        float a = atan(vUv.y, vUv.x);
        float spokes = 0.5 + 0.5 * sin(a * 7.0 + seed * 9.0);
        spokes = spokes * 0.6 + (0.5 + 0.5 * sin(a * 11.0 + seed * 3.7)) * 0.4;
        float reach = 0.42 + 0.56 * pow(spokes, 3.0);
        float pit = 1.0 - smoothstep(0.18, 0.32, r);
        float scorch = (1.0 - smoothstep(reach - 0.2, reach, r)) * (1.0 - smoothstep(0.55, 1.0, r) * 0.45);
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
    if ((state > 2.5 && state < 3.5) || dot(iDir, iDir) < 1e-6) { gl_Position = kCulled; return; }
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
        if (state > 3.5) {
            // Embedded in a tree or wall where it struck.
            tip = iPos + dir * 0.12;
            tail = iPos - dir * 0.7;
        } else if (state < 1.5) {
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
flat out float vShade;
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
    vShade = 0.72 + 0.28 * shadowAt(iPos + vec3(0.0, 0.0, height * 0.6), vec3(0.0, 0.0, 1.0)) * cloudShade(iPos.xy);
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
flat in float vShade;
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
    oColor = vec4(mix(gCol * vShade, kSky, vFog), 1.0);
}
)";

// ---------------------------------------------------------------- meshes
// Instanced lit meshes: scenery and bodies. Per vertex: pos, normal, colour.
// Per instance: position, rotation quaternion, scale, tint.
inline const char* kMeshVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec3 iPos;
layout(location = 4) in vec4 iQuat;
layout(location = 5) in vec3 iScale;
layout(location = 6) in vec3 iTint;
uniform float uSway;
out vec3 vColor;
out vec3 vNormal;
out vec3 vWorld;
vec3 rotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
void main() {
    if (iScale.x <= 0.0) { gl_Position = kCulled; return; }
    vec3 local = aPos * iScale;
    if (uSway > 0.0) {
        // Crowns sway in the wind; trunks stay put.
        float h = aPos.z * aPos.z;
        local.xy += vec2(sin(uTime * 1.3 + iPos.x * 0.31), cos(uTime * 1.1 + iPos.y * 0.27)) * h * uSway * iScale.z;
    }
    vec3 world = iPos + rotate(iQuat, local);
    vNormal = normalize(rotate(iQuat, aNormal / iScale));
    vColor = aColor * iTint;
    vWorld = world;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kMeshFs = R"(
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorld;
out vec4 oColor;
void main() {
    vec3 n = normalize(vNormal);
    float sun = max(dot(n, kSun), 0.0) * shadowAt(vWorld, n) * cloudShade(vWorld.xy);
    float light = 0.36 + 0.8 * sun + 0.12 * max(-dot(n, kSun), 0.0);
    vec3 c = vColor * light;
    // Under the battlefield's river (the rest of the continent has no water level).
    vec2 cell = vWorld.xy / uCellSize;
    bool onField = all(greaterThanEqual(cell, vec2(0.0))) && all(lessThanEqual(cell, vec2(uCells)));
    if (onField && vWorld.z < uWater) c = mix(c, vec3(0.1, 0.25, 0.32), clamp((uWater - vWorld.z) * 0.6, 0.0, 0.8));
    oColor = vec4(mix(c, hazeColor(vWorld), fogFactor(distance(uEye, vWorld))), 1.0);
}
)";

// ---------------------------------------------------------------- ground cover
// Grass tufts, flowers, pebbles, twigs and ferns on a grid that follows the
// camera (snapped to the ground so nothing swims). No per-frame CPU work:
// gl_InstanceID picks the grid cell, hashes pick place, size and spin.
// uCover: spacing (m), grid size, seed, kind (0 grass 1 flowers 2 pebbles 3 twigs 4 ferns).
inline const char* kCoverVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform vec4 uCover;
uniform sampler2D uGround;
uniform sampler2D uRoad;
uniform sampler2D uCanopy;
out vec3 vColor;
out vec3 vWorld;
int coverGround(vec2 xy) {
    ivec2 c = clamp(ivec2(xy / uCellSize), ivec2(0), ivec2(int(uCells) - 1));
    return int(texelFetch(uGround, c, 0).r * 8.0 + 0.5);
}
void main() {
    float spacing = uCover.x;
    int n = int(uCover.y);
    float kind = uCover.w;
    vec2 cf = vec2(ivec2(gl_InstanceID % n, gl_InstanceID / n) + ivec2(floor(uFocus / spacing)) - n / 2);
    float h1 = hash12(cf * 1.13 + uCover.z), h2 = hash12(cf * 0.71 + uCover.z + 3.3);
    float h3 = hash12(cf + uCover.z * 1.7 + 9.1), h4 = hash12(cf * 1.9 + uCover.z + 5.2);
    vec2 xy = (cf + vec2(h1, h2)) * spacing;
    float size = uCells * uCellSize;
    float radius = float(n) * spacing * 0.5;
    // Fades out at the grid's edge, and as the camera rises (by then the
    // painted ground carries the look).
    float fade = (1.0 - smoothstep(radius * 0.55, radius * 0.95, distance(xy, uFocus))) * (1.0 - smoothstep(55.0, 110.0, uCamDist));
    if (fade <= 0.0 || xy.x < 1.0 || xy.y < 1.0 || xy.x > size - 1.0 || xy.y > size - 1.0) { gl_Position = kCulled; return; }
    int g = coverGround(xy);
    float canopy = texture(uCanopy, xy / size).r;
    vec2 road = texture(uRoad, xy / size).rg;
    float onRoad = road.g > 0.3 && abs((road.r - 0.5) * 16.0) < 3.0 ? 1.0 : 0.0;
    // Same patches the ground shader paints, so tufts avoid the bare soil.
    float soil = smoothstep(0.7, 0.84, noise2(xy * 0.085 + 41.0) * 0.6 + noise2(xy * 0.5 + 5.0) * 0.4);
    float lush = noise2(xy * 0.045 + 3.1) * 0.65 + noise2(xy * 0.13 + 8.7) * 0.35;
    bool plains = g == 0, forest = g == 2, swamp = g == 3, rock = g == 6, wet = g == 4 || g == 5;
    float density = 0.0;
    if (kind < 0.5) density = plains ? 0.95 * (1.0 - soil) * (1.0 - canopy * 0.75) : forest ? 0.2 : swamp ? 0.8 : rock ? 0.12 : 0.0;
    else if (kind < 1.5) density = plains ? smoothstep(0.45, 0.85, lush) * (1.0 - soil) * (1.0 - canopy) * 0.8 : 0.0;
    else if (kind < 2.5) density = wet ? 0.0 : plains ? 0.25 + soil * 0.6 : rock ? 0.9 : 0.3;
    else if (kind < 3.5) density = forest ? 0.8 : plains ? canopy * 0.9 + 0.05 : 0.0;
    else density = forest ? 0.65 * (0.4 + canopy) : plains ? canopy * 0.5 : swamp ? 0.3 : 0.0;
    density *= 1.0 - onRoad * (kind < 2.5 && kind > 1.5 ? 0.0 : 0.95);
    if (wet || h3 >= density) { gl_Position = kCulled; return; }
    float scale = (0.7 + 0.6 * h4) * (0.35 + 0.65 * fade);
    if (kind < 0.5 && swamp) scale *= 1.8;  // sedge
    float a = h4 * 6.2831853 + h1 * 3.0;
    vec2 cs = vec2(cos(a), sin(a));
    vec3 local = aPos * scale;
    local.xy = vec2(cs.x * local.x - cs.y * local.y, cs.y * local.x + cs.x * local.y);
    // Wind through the grass.
    float sway = sin(uTime * 2.1 + xy.x * 0.45 + xy.y * 0.3) * 0.5 + sin(uTime * 3.7 + xy.y * 0.9) * 0.25;
    local.xy += vec2(0.8, 0.5) * sway * aPos.z * aPos.z * 1.6;
    vec3 world = vec3(xy, groundZ(xy) - 0.01) + local;
    // Colour: the meadow's own green, drier or lusher; petals per plant.
    vec3 tint = kind < 0.5 ? mix(vec3(0.55, 0.56, 0.27), vec3(0.26, 0.45, 0.16), lush) : vec3(1.0);
    if (kind < 0.5 && forest) tint = vec3(0.28, 0.38, 0.17);
    vec3 col = aColor * tint;
    if (aColor.r > 1.5) {
        float k = hash12(cf + 17.0);
        col = k < 0.4 ? vec3(0.95, 0.94, 0.88) : k < 0.7 ? vec3(0.95, 0.8, 0.22) : k < 0.9 ? vec3(0.6, 0.42, 0.85) : vec3(0.85, 0.2, 0.18);
    }
    vColor = col * (0.9 + 0.2 * h2);
    vWorld = world;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kCoverFs = R"(
in vec3 vColor;
in vec3 vWorld;
out vec4 oColor;
void main() {
    float sun = max(kSun.z, 0.0) * shadowAt(vWorld, vec3(0.0, 0.0, 1.0)) * cloudShade(vWorld.xy);
    vec3 c = vColor * (0.32 + 0.95 * sun);
    oColor = vec4(mix(c, hazeColor(vWorld), fogFactor(distance(uEye, vWorld))), 1.0);
}
)";

// ---------------------------------------------------------------- sky
// A full-screen gradient behind everything: deep blue overhead, the haze
// colour at the horizon (matching the fog), a glow around the sun.
inline const char* kSkyVs = R"(
out vec2 vNdc;
void main() {
    vec2 c = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = c;
    gl_Position = vec4(c, 0.9999, 1.0);
}
)";
inline const char* kSkyFs = R"(
in vec2 vNdc;
uniform vec3 uCamFwd;
uniform vec3 uCamUp;
uniform vec2 uProj;  // tan(fov / 2), aspect
out vec4 oColor;
void main() {
    vec3 d = normalize(uCamFwd + uCamRight * vNdc.x * uProj.x * uProj.y + uCamUp * vNdc.y * uProj.x);
    float up = clamp(d.z, 0.0, 1.0);
    vec3 zenith = vec3(0.30, 0.47, 0.74);
    vec3 c = mix(kSky, zenith, pow(up, 0.55));
    float sun = max(dot(d, kSun), 0.0);
    c += vec3(1.0, 0.86, 0.6) * (pow(sun, 8.0) * 0.25 + pow(sun, 400.0) * 1.5);
    // Faint high cloud.
    vec2 q = d.xy / max(d.z, 0.08) * 1.6 + vec2(uTime * 0.003, 0.0);
    float cl = smoothstep(0.55, 0.85, noise2(q) * 0.6 + noise2(q * 2.7) * 0.4) * smoothstep(0.02, 0.25, d.z);
    c = mix(c, vec3(0.97, 0.97, 0.98), cl * 0.55);
    oColor = vec4(c, 1.0);
}
)";

// Depth only, for the sun's shadow map.
inline const char* kDepthFs = R"(
void main() {}
)";

// ---------------------------------------------------------------- water
// The water surface per terrain sample (rivers slope downhill); a mesh of
// the wet cells only.
inline const char* kWaterVs = R"(
layout(location = 0) in vec2 aPos;
uniform sampler2D uWaterTex;  // R32F per sample; very negative where dry
out vec3 vWorld;
float waterTexel(vec2 c) {
    ivec2 i = ivec2(clamp(c, vec2(0.0), vec2(uCells)));
    return texelFetch(uWaterTex, i, 0).r;
}
void main() {
    // The cell's wet corners decide its surface; dry corners borrow it.
    vec2 c = aPos / uCellSize;
    float w = waterTexel(c);
    if (w < -1000.0) {
        float best = -1e9;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) best = max(best, waterTexel(c + vec2(dx, dy)));
        w = best;
    }
    vWorld = vec3(aPos, w);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";
inline const char* kWaterFs = R"(
in vec3 vWorld;
uniform sampler2D uFlow;  // RG8: downstream direction per terrain sample (0.5 = still)
out vec4 oColor;
// Ripples drawn along the current: stretched noise drifting downstream.
float current(vec2 along, float t) {
    return noise2(along * vec2(0.35, 1.6) - vec2(t, 0.0)) * 0.55 + noise2(along * vec2(0.9, 3.1) - vec2(t * 1.7, 0.0) + 7.3) * 0.3 +
           noise2(along * vec2(2.3, 6.5) - vec2(t * 2.6, 0.0) + 3.1) * 0.15;
}
void main() {
    vec2 p = vWorld.xy;
    float depth = vWorld.z - groundZ(p);
    if (depth <= -0.02) discard;
    float dist = distance(uEye, vWorld);
    vec2 flow = texture(uFlow, p / (uCells * uCellSize)).rg * 2.0 - 1.0;
    float speed = length(flow);
    vec2 dir = speed > 0.05 ? flow / speed : vec2(1.0, 0.0);
    vec2 along = vec2(dot(p, dir), dot(p, vec2(-dir.y, dir.x)));
    // Faster in the middle and the shallows' riffles, slack at the edges.
    float t = uTime * (0.8 + 1.6 * smoothstep(0.1, 1.2, depth)) * step(0.05, speed) + uTime * 0.15;
    float e = 0.06;
    float h0 = current(along, t), hx = current(along + vec2(e, 0.0), t), hy = current(along + vec2(0.0, e), t);
    vec2 g = vec2(hx - h0, hy - h0) / e;
    vec2 gw = dir * g.x + vec2(-dir.y, dir.x) * g.y;  // back to world axes
    float calm = 1.0 - smoothstep(30.0, 300.0, dist) * 0.7;
    vec3 n = normalize(vec3(-gw * 0.06 * calm, 1.0));
    // Colour: the bed shows through the shallows; silt-green to deep blue.
    vec3 bed = mix(vec3(0.46, 0.40, 0.30), vec3(0.36, 0.34, 0.26), noise2(p * 0.7));
    vec3 water = mix(vec3(0.16, 0.34, 0.32), vec3(0.05, 0.16, 0.26), smoothstep(0.5, 3.0, depth));
    vec3 c = mix(bed, water, 1.0 - exp(-max(depth, 0.0) * 1.6));
    // Sky in the surface, more at grazing angles.
    vec3 v = normalize(uEye - vWorld);
    vec3 refl = reflect(-v, n);
    vec3 sky = mix(kSky, vec3(0.30, 0.47, 0.74), clamp(refl.z, 0.0, 1.0));
    float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    c = mix(c, sky, clamp(fresnel * 1.3, 0.0, 0.85));
    float lit = shadowAt(vWorld, vec3(0.0, 0.0, 1.0)) * cloudShade(p);
    c *= 0.75 + 0.25 * lit;
    c += vec3(1.0, 0.95, 0.85) * pow(max(dot(reflect(-kSun, n), v), 0.0), 120.0) * 1.2 * lit;
    // Foam: a thin wet line at the edge, and white water over the riffles.
    float riffle = smoothstep(0.72, 0.9, h0) * (1.0 - smoothstep(0.1, 0.45, depth)) * step(0.05, speed);
    float edge = 1.0 - smoothstep(0.0, 0.08, depth);
    c = mix(c, vec3(0.88, 0.91, 0.92), clamp(riffle * 0.7 + edge * 0.35, 0.0, 0.8));
    // Soft edges: no hard cut where the water meets the bank.
    float alpha = mix(0.0, 1.0, smoothstep(-0.02, 0.12, depth)) * clamp(0.55 + depth * 0.3, 0.55, 0.95);
    oColor = vec4(mix(c, hazeColor(vWorld), fogFactor(dist)), alpha);
}
)";

// ---------------------------------------------------------------- far continent
// Everything beyond the battlefield: a coarse mesh coloured by ground type.
inline const char* kFarVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec4 aKinds;  // forest, rock, swamp, water
uniform vec4 uRing;   // the fine ring's extent; coarse land sinks beneath it
uniform float uSink;
uniform vec4 uPatch;  // the close-up patch's extent; everything else sinks beneath it
uniform float uPatchSink;
out vec3 vWorld;
out vec3 vNormal;
out vec3 vColor;
out vec4 vKinds;
void main() {
    vec3 p = aPos;
    if (uSink > 0.0 && p.x > uRing.x && p.y > uRing.y && p.x < uRing.z && p.y < uRing.w) p.z -= uSink;
    if (uPatchSink > 0.0 && p.x > uPatch.x && p.y > uPatch.y && p.x < uPatch.z && p.y < uPatch.w) p.z -= uPatchSink;
    vWorld = p;
    vNormal = aNormal;
    vColor = aColor;
    vKinds = aKinds;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";
inline const char* kFarFs = R"(
in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;
in vec4 vKinds;
out vec4 oColor;
float hexEdge(vec2 p, float s) {
    vec2 r = vec2(1.0, 1.7320508);
    vec2 h = r * 0.5;
    vec2 a = mod(p / s, r) - h;
    vec2 b = mod(p / s - h, r) - h;
    vec2 g = dot(a, a) < dot(b, b) ? a : b;
    g = abs(g);
    return 0.5 - max(dot(g, normalize(vec2(1.0, 1.7320508))), g.x);
}
float fbm3(vec2 p) { return noise2(p) * 0.55 + noise2(p * 2.1 + 3.7) * 0.3 + noise2(p * 4.3 + 9.1) * 0.15; }
void main() {
    vec3 n = normalize(vNormal);
    vec3 albedo = vColor;
    vec2 p = vWorld.xy;
    if (feature(kFeatFarDetail)) {
        // Ground kinds meet along ragged, natural edges instead of the grid.
        float edge = (fbm3(p / 140.0) - 0.5) * 0.9 + (noise2(p / 28.0) - 0.5) * 0.35;
        float forest = smoothstep(0.4, 0.6, vKinds.x + edge);
        float rock = smoothstep(0.35, 0.65, vKinds.y + edge * 0.8);
        float swamp = smoothstep(0.4, 0.6, vKinds.z + edge);
        float water = vKinds.w;
        float slope = 1.0 - n.z;
        // Meadows: lush and dry patches, as on the battlefield.
        float lush = noise2(p * 0.045 + 3.1) * 0.65 + noise2(p * 0.13 + 8.7) * 0.35;
        vec3 meadow = mix(vec3(0.50, 0.50, 0.25), vec3(0.26, 0.42, 0.16), lush) * (0.9 + 0.2 * noise2(p * 0.4));
        float land = 1.0 - water;
        albedo = mix(albedo, mix(albedo, meadow, 0.6), land * (1.0 - max(max(forest, rock), swamp)) * step(vColor.r, 0.55));
        // Forest canopy: crowns and gaps, lit as a rough surface.
        float crowns = fbm3(p / 9.0);
        vec3 canopy = mix(vec3(0.10, 0.17, 0.07), vec3(0.24, 0.34, 0.13), crowns);
        canopy = mix(canopy, vec3(0.42, 0.34, 0.14), smoothstep(0.78, 0.9, noise2(p / 160.0 + 5.0)) * 0.5);  // turning leaves
        albedo = mix(albedo, canopy, forest * land);
        n = normalize(n + vec3(noise2(p / 6.0) - 0.5, noise2(p / 6.0 + 4.0) - 0.5, 0.0) * 0.5 * forest);
        // Rock: strata, crags and scree.
        // Broken bands, warped hard so they read as rock, not contour lines.
        float strata = 0.5 + 0.5 * sin(vWorld.z * 0.22 + fbm3(p / 55.0) * 14.0 + noise2(p / 9.0) * 3.0);
        float tone = fbm3(p / 18.0) * 0.6 + 0.2;
        vec3 stone = mix(vec3(0.40, 0.38, 0.36), vec3(0.56, 0.53, 0.49), tone) * (0.9 + 0.14 * strata) * (0.94 + 0.12 * noise2(p / 5.0));
        // Cliffs dark, ledges and scree pale: the shape of the range reads from afar.
        stone *= mix(1.08, 0.72, smoothstep(0.3, 0.75, slope));
        stone = mix(stone, vec3(0.36, 0.38, 0.28), smoothstep(0.62, 0.8, noise2(p / 35.0 + 2.0)) * 0.35);  // lichen and scrub
        // Bare rock on the steeps and the heights; alpine turf holds on the
        // gentler mountain ground and climbs the ravines, so the cover follows the land.
        float high = smoothstep(190.0, 320.0, vWorld.z + (fbm3(p / 80.0) - 0.5) * 70.0);
        float bare = smoothstep(0.12, 0.32, slope + high * 0.3 + (fbm3(p / 45.0) - 0.5) * 0.18);
        float rocky = max(rock * bare, smoothstep(0.3, 0.55, slope)) * land;
        vec3 turf = mix(vec3(0.36, 0.40, 0.20), vec3(0.47, 0.45, 0.27), fbm3(p / 30.0)) * (0.92 + 0.16 * noise2(p / 7.0));
        albedo = mix(albedo, turf, rock * (1.0 - bare) * land);
        albedo = mix(albedo, stone, rocky);
        vec2 crag = vec2(fbm3(p / 22.0) - 0.5, fbm3(p / 22.0 + 7.0) - 0.5);
        n = normalize(n + vec3(crag, 0.0) * 0.35 * rocky);
        // Snow lies on the heights, but not on cliffs.
        float snow = smoothstep(300.0, 360.0, vWorld.z + (fbm3(p / 60.0) - 0.5) * 60.0) * smoothstep(0.55, 0.3, slope) * land;
        albedo = mix(albedo, vec3(0.93, 0.94, 0.97), snow);
        albedo = mix(albedo, mix(vec3(0.20, 0.24, 0.15), vec3(0.18, 0.23, 0.24), smoothstep(0.55, 0.7, noise2(p / 30.0))), swamp * land);
    }
    // Sky light falls less into steep ground, so ridges and ravines model even out of the sun.
    float light = 0.18 + 0.14 * n.z + 0.95 * max(dot(n, kSun), 0.0) * cloudShade(vWorld.xy);
    vec3 c = albedo * light;
    // The strategic hex grid of the campaign map, at continent scale.
    float gridFade = smoothstep(1800.0, 4000.0, uCamDist) * 0.3;
    if (gridFade > 0.0) {
        float e = hexEdge(vWorld.xy, 400.0);
        float w = fwidth(e) * 1.5;
        c = mix(c, vec3(0.95, 0.9, 0.78), gridFade * (1.0 - smoothstep(0.0, w, e)));
    }
    oColor = vec4(mix(c, hazeColor(vWorld), fogFactor(distance(uEye, vWorld)) * 0.6), 1.0);
}
)";

// Rivers and roads drawn as ribbons on the far map, never thinner than a few pixels.
// Per instance: a, b (segment ends), info (half width, kind: 0 river 1 road 2 creek).
inline const char* kRibbonVs = R"(
layout(location = 0) in vec3 iA;
layout(location = 1) in vec3 iB;
layout(location = 2) in vec2 iInfo;
uniform vec4 uHole;  // battlefield (min x, min y, max x, max y): drawn in full detail there
flat out float vKind;
out float vFog;
void main() {
    vec3 mid = (iA + iB) * 0.5;
    if (mid.x > uHole.x && mid.y > uHole.y && mid.x < uHole.z && mid.y < uHole.w) { gl_Position = kCulled; return; }
    float dist = distance(uEye, mid);
    float px = uPxScale * dist;
    float hw = max(iInfo.x, px * (iInfo.y > 1.5 ? 0.6 : iInfo.y > 0.5 ? 0.9 : 1.6));
    vec3 d = iB - iA;
    vec3 side = normalize(vec3(-d.y, d.x, 0.0) + vec3(1e-5, 0.0, 0.0)) * hw;
    float t = float(gl_VertexID & 1);
    float s = float((gl_VertexID >> 1) & 1) * 2.0 - 1.0;
    vec3 world = mix(iA, iB, t) + side * s;
    world.z += max(1.5, dist * 0.003);  // ride above the coarse mesh
    vKind = iInfo.y;
    vFog = fogFactor(dist);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kRibbonFs = R"(
flat in float vKind;
in float vFog;
out vec4 oColor;
void main() {
    vec3 c = abs(vKind - 1.0) > 0.5 ? vec3(0.10, 0.24, 0.36) : vec3(0.48, 0.36, 0.22);
    oColor = vec4(mix(c, kSky, vFog * 0.6), 1.0);
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
