#pragma once

// GLSL ES 3.00 (WebGL 2) shader sources for the battle renderer. Every program
// is prefixed with kVersion and kCommon. A desktop (OpenGL 4.1 core) takes the
// same source under its own version line: it accepts the precision statements
// and everything else used here.

namespace illyria::game::shaders {

inline const char* kVersionES = "#version 300 es\n";
inline const char* kVersionGL = "#version 410 core\n";
#ifdef __EMSCRIPTEN__
inline const char* kVersion = kVersionES;
#else
inline const char* kVersion = kVersionGL;
#endif

inline const char* kCommon = R"(precision highp float;
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

// Which land the ground textures hold. 0: the battlefield, from (0, 0),
// clamped at its edges. 1: the close-up land around the camera off the
// battlefield (game::DetailLand), whose textures repeat every uCells samples.
uniform float uWrap;
ivec2 texCell(vec2 c, float last) {
    if (uWrap > 0.5) return ivec2(clamp(mod(c, uCells), vec2(0.0), vec2(uCells - 1.0)));
    return ivec2(clamp(c, vec2(0.0), vec2(last)));
}
float heightTexel(vec2 c) { return texelFetch(uHeights, texCell(c, uCells), 0).r; }
// Same two-triangle interpolation as the simulation and the terrain mesh.
float groundZ(vec2 p) {
    vec2 f = p / uCellSize;
    if (uWrap < 0.5) f = clamp(f, vec2(0.0), vec2(uCells));
    vec2 i = floor(f);
    if (uWrap < 0.5) i = min(i, vec2(uCells - 1.0));
    vec2 t = f - i;
    float a = heightTexel(i), b = heightTexel(i + vec2(1, 0));
    float c = heightTexel(i + vec2(0, 1)), d = heightTexel(i + vec2(1, 1));
    return t.x >= t.y ? a + (b - a) * t.x + (d - b) * t.y : a + (c - a) * t.y + (d - c) * t.x;
}
// How much the close-up land shows here, 0..1 (the coarse land and the
// ribbons give way to it): inside uDetailRect, in the 64 m chunks uDetailMask
// marks (one texel per chunk, repeating like the land's textures, rising as a
// chunk fades in), fading out toward the rect's edge and as the camera rises
// (uDetailFade). Where it is between 0 and 1 the two cross-fade by a screen
// dither, each drawing the pixels the other leaves (overlapping a little, so
// that where their surfaces part no pixel is left to the sky).
uniform sampler2D uDetailMask;
uniform vec4 uDetailRect;
uniform float uDetailFade;
float detailMask(vec2 p) {  // whether its chunk is built (and how far faded in)
    if (any(lessThan(p, uDetailRect.xy)) || any(greaterThanEqual(p, uDetailRect.zw))) return 0.0;
    vec2 n = vec2(textureSize(uDetailMask, 0));
    ivec2 c = ivec2(clamp(mod(floor(p / 64.0), n), vec2(0.0), n - 1.0));
    return texelFetch(uDetailMask, c, 0).r;
}
float detailWeight(vec2 p) {
    vec2 e = min(p - uDetailRect.xy, uDetailRect.zw - p);
    return detailMask(p) * clamp(min(e.x, e.y) / 128.0, 0.0, 1.0) * uDetailFade;
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
float valueNoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
// Drifting cloud shadows over the land: 1 in sun, ~0.62 under a cloud.
float cloudShade(vec2 xy) {
    if (!feature(kFeatAtmosphere)) return 1.0;
    vec2 p = xy / 1400.0 + vec2(uTime * 0.004, uTime * 0.0025);
    float c = valueNoise2(p) * 0.6 + valueNoise2(p * 2.3 + 7.1) * 0.3 + valueNoise2(p * 5.1 + 3.7) * 0.1;
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
// What every land fragment shader shares (the terrain's, the close-up
// land's and the coarse land's): prefixed to their bodies.
#define ILLYRIA_LAND_FS R"(
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
float fbm3(vec2 p) { return valueNoise2(p) * 0.55 + valueNoise2(p * 2.1 + 3.7) * 0.3 + valueNoise2(p * 4.3 + 9.1) * 0.15; }
// Territory (sim campaign_territory.cpp): one texel per campaign hex, the
// holder's colour with alpha for how firmly; drawn on the campaign map only.
uniform sampler2D uTerritory;
uniform vec4 uTerritoryInfo;  // xy: the battlefield's origin on the continent (m); z: tint strength, 0 off
// The campaign hex (column, row) nearest p in continent metres: 89 m apart,
// rows 77.08 m apart, odd rows half a hex east (as Campaign::hexOfMetres).
ivec2 campaignHex(vec2 p) {
    const float rowH = 77.08;
    float r0 = floor(p.y / rowH);
    vec2 best = vec2(0.0);
    float bd = 1e20;
    for (int k = 0; k < 2; ++k) {
        float r = r0 + float(k);
        float shift = mod(r, 2.0) * 44.5;
        float col = floor((p.x - shift + 44.5) / 89.0);
        vec2 d = p - vec2(col * 89.0 + shift, r * rowH);
        float dd = dot(d, d);
        if (dd < bd) { bd = dd; best = vec2(col, r); }
    }
    return ivec2(best);
}
// The grids over all land alike, world-aligned so they run on unbroken across
// the battlefield's edge: the tactical hex grid fades in as the camera rises,
// then hands over to the campaign's (laid on the continent's own hexes, which
// are under twice as wide since the map shrank to a third).
vec3 landGrids(vec3 c, vec2 xy) {
    vec2 land = xy + uTerritoryInfo.xy;
    if (uTerritoryInfo.z > 0.0 && uCamDist > 170.0) {
        // Whose land: a faint wash of the holder's colour, stronger along each hex's rim.
        ivec2 size = textureSize(uTerritory, 0);
        vec4 t = texelFetch(uTerritory, clamp(campaignHex(land), ivec2(0), size - 1), 0);
        float e = hexEdge(land, 89.0);
        float rim = 1.0 - smoothstep(0.0, max(0.035, fwidth(e) * 2.5), e);
        float k = uTerritoryInfo.z * t.a * (0.13 + 0.42 * rim) * smoothstep(170.0, 300.0, uCamDist);
        c = mix(c, t.rgb, k);
    }
    float gridFade = smoothstep(120.0, 400.0, uCamDist) * (1.0 - smoothstep(450.0, 600.0, uCamDist)) * 0.35;
    if (gridFade > 0.0) {
        float e = hexEdge(xy, 48.0);
        float w = fwidth(e) * 1.5;
        c = mix(c, vec3(0.95, 0.92, 0.8), gridFade * (1.0 - smoothstep(0.0, w, e)));
    }
    float bigFade = smoothstep(600.0, 1350.0, uCamDist) * 0.3;
    if (bigFade > 0.0) {
        float e = hexEdge(land, 89.0);
        float w = fwidth(e) * 1.5;
        c = mix(c, vec3(0.95, 0.9, 0.78), bigFade * (1.0 - smoothstep(0.0, w, e)));
    }
    return c;
}
// The land's look from afar, from its base colour and ground kinds (weights of
// forest, rock, swamp and water): the coarse land's (kFarFs), and the
// battlefield's and the close-up land's as the camera rises, so they all look
// alike from high up.
// The far land's waterline (kinds.w = 1) wanders a little between samples.
float farShoreWobble(vec2 p) { return (valueNoise2(p / 23.0) - 0.5) * 0.03 + (valueNoise2(p / 7.0) - 0.5) * 0.012; }
// How much crown covers the ground at p: the trees of the whole continent summed (game::FarForest), the same trees
// the cards are drawn for, so the canopy the land shows from afar is under exactly the trees that stand there.
uniform sampler2D uForestMap;  // R8, linear: crown cover, 8 m texels
uniform vec4 uForestRect;      // the map's corner (metres), and 1 / its size
float forestCover(vec2 p) { return texture(uForestMap, (p - uForestRect.xy) * uForestRect.zw).r; }
vec3 farAlbedo(vec3 base, vec4 kinds, vec3 world, inout vec3 n) {
    vec3 albedo = base;
    vec2 p = world.xy;
    // Ground kinds meet along ragged, natural edges instead of the grid.
    float edge = (fbm3(p / 140.0) - 0.5) * 0.9 + (valueNoise2(p / 28.0) - 0.5) * 0.35;
    // Forest is where the trees are (not a ground kind painted in blotches): their cover, from the cards' own
    // positions, grows to a solid canopy as the cards shrink to specks.
    float canopyWeight = mix(0.55, 1.0, smoothstep(250.0, 2500.0, uCamDist));
    float forest = smoothstep(0.1, 0.4, forestCover(p) * canopyWeight * 1.6 + edge * 0.1);
    float rock = smoothstep(0.35, 0.65, kinds.y + edge * 0.8);
    float swamp = smoothstep(0.4, 0.6, kinds.z + edge);
    float water = step(1.0, kinds.w + farShoreWobble(p));
    float slope = 1.0 - n.z;
    // Meadows: lush and dry patches, as on the battlefield.
    float lush = valueNoise2(p * 0.045 + 3.1) * 0.65 + valueNoise2(p * 0.13 + 8.7) * 0.35;
    vec3 meadow = mix(vec3(0.50, 0.50, 0.25), vec3(0.26, 0.42, 0.16), lush) * (0.9 + 0.2 * valueNoise2(p * 0.4));
    float land = 1.0 - water;
    albedo = mix(albedo, mix(albedo, meadow, 0.6), land * (1.0 - max(max(forest, rock), swamp)) * step(base.r, 0.55));
    // Each kind's look is worked out only where the kind is (the noise is most of this function's cost, and
    // most land is meadow): the same picture as working all of them out and weighing them by nothing.
    // Forest canopy: crowns and gaps, lit as a rough surface.
    if (forest > 0.0) {
        float crowns = fbm3(p / 9.0);
        vec3 canopy = mix(vec3(0.07, 0.13, 0.05), vec3(0.19, 0.28, 0.10), crowns);
        canopy = mix(canopy, vec3(0.42, 0.34, 0.14), smoothstep(0.78, 0.9, valueNoise2(p / 160.0 + 5.0)) * 0.5);  // turning leaves
        albedo = mix(albedo, canopy, forest * land);
        n = normalize(n + vec3(valueNoise2(p / 6.0) - 0.5, valueNoise2(p / 6.0 + 4.0) - 0.5, 0.0) * 0.5 * forest);
    }
    // Rock: strata, crags and scree.
    if (rock > 0.0 || slope > 0.3) {
        // Broken bands, warped hard so they read as rock, not contour lines.
        float strata = 0.5 + 0.5 * sin(world.z * 0.22 + fbm3(p / 55.0) * 14.0 + valueNoise2(p / 9.0) * 3.0);
        float tone = fbm3(p / 18.0) * 0.6 + 0.2;
        vec3 stone = mix(vec3(0.40, 0.38, 0.36), vec3(0.56, 0.53, 0.49), tone) * (0.9 + 0.14 * strata) * (0.94 + 0.12 * valueNoise2(p / 5.0));
        // Cliffs dark, ledges and scree pale: the shape of the range reads from afar.
        stone *= mix(1.08, 0.72, smoothstep(0.3, 0.75, slope));
        stone = mix(stone, vec3(0.36, 0.38, 0.28), smoothstep(0.62, 0.8, valueNoise2(p / 35.0 + 2.0)) * 0.35);  // lichen and scrub
        // Bare rock on the steeps and the heights; alpine turf holds on the
        // gentler mountain ground and climbs the ravines, so the cover follows the land.
        // (Heights here are the mountains' own: a third of the first design's, Continent::peakHeight.)
        float high = smoothstep(63.0, 107.0, world.z + (fbm3(p / 80.0) - 0.5) * 23.0);
        float bare = smoothstep(0.12, 0.32, slope + high * 0.3 + (fbm3(p / 45.0) - 0.5) * 0.18);
        float rocky = max(rock * bare, smoothstep(0.3, 0.55, slope)) * land;
        vec3 turf = mix(vec3(0.36, 0.40, 0.20), vec3(0.47, 0.45, 0.27), fbm3(p / 30.0)) * (0.92 + 0.16 * valueNoise2(p / 7.0));
        albedo = mix(albedo, turf, rock * (1.0 - bare) * land);
        albedo = mix(albedo, stone, rocky);
        vec2 crag = vec2(fbm3(p / 22.0) - 0.5, fbm3(p / 22.0 + 7.0) - 0.5);
        n = normalize(n + vec3(crag, 0.0) * 0.35 * rocky);
    }
    // Snow lies on the heights, but not on cliffs.
    if (world.z > 90.0) {
        float snow = smoothstep(100.0, 120.0, world.z + (fbm3(p / 60.0) - 0.5) * 20.0) * smoothstep(0.55, 0.3, slope) * land;
        albedo = mix(albedo, vec3(0.93, 0.94, 0.97), snow);
    }
    if (swamp > 0.0) albedo = mix(albedo, mix(vec3(0.20, 0.24, 0.15), vec3(0.18, 0.23, 0.24), smoothstep(0.55, 0.7, valueNoise2(p / 30.0))), swamp * land);
    return albedo;
}
)"
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
// The close-up land off the battlefield: one 16 x 16 cell grid per chunk,
// instanced, its heights from the (repeating) height texture, with a skirt
// hanging from its edges to hide any crack with the coarser land.
inline const char* kPatchVs = R"(
layout(location = 0) in vec3 aGrid;   // sample in the chunk (0..16), and 1 on the skirt
layout(location = 1) in vec2 aChunk;  // per instance: the chunk's corner
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vec2 c = floor(aChunk / uCellSize + 0.5) + aGrid.xy;
    float z = heightTexel(c);
    float l = heightTexel(c - vec2(1, 0)), r = heightTexel(c + vec2(1, 0));
    float d = heightTexel(c - vec2(0, 1)), u = heightTexel(c + vec2(0, 1));
    vNormal = normalize(vec3(l - r, d - u, 2.0 * uCellSize));
    vWorld = vec3(c * uCellSize, z - aGrid.z * 10.0);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";
#define ILLYRIA_TERRAIN_FS R"(
in vec3 vWorld;
in vec3 vNormal;
out vec4 oColor;

float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
uniform sampler2D uGround;  // R8: ground type / 16, one texel per terrain cell
uniform sampler2D uRoad;    // RG8, linear: R = 0.5 + lateral offset / 16 m, G = road weight

// Plains, Road, Forest, Swamp, Shallows, DeepWater, Rock, Bridge, Mud.
vec3 groundColor(int id, float broad, float fine) {
    if (id == 9) return vec3(0.80, 0.73, 0.55) * (0.9 + 0.2 * fine);
    if (id == 8) return mix(vec3(0.27, 0.21, 0.14), vec3(0.36, 0.29, 0.19), broad) * (0.85 + 0.3 * fine);
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
    return int(texelFetch(uGround, texCell(vec2(c), uCells - 1.0), 0).r * 16.0 + 0.5);
}

// The coarse land's colour of a ground kind (Renderer::buildLand).
vec3 farGroundColor(int id, float z) {
    vec3 c = id == 1 ? vec3(0.48, 0.40, 0.27) : id == 2 ? vec3(0.18, 0.25, 0.11) : id == 3 ? vec3(0.24, 0.29, 0.17)
           : id == 4 ? vec3(0.30, 0.50, 0.55) : id == 5 ? vec3(0.10, 0.26, 0.42) : id == 6 ? vec3(0.48, 0.46, 0.43)
           : id == 7 ? vec3(0.45, 0.36, 0.26) : id == 8 ? vec3(0.33, 0.27, 0.18) : id == 9 ? vec3(0.80, 0.73, 0.55) : vec3(0.40, 0.49, 0.21);
    if (id != 5) c = mix(c, vec3(0.93, 0.94, 0.96), clamp((z - 107.0) / 13.0, 0.0, 1.0));
    return c;
}
uniform sampler2D uCanopy;  // R8: tree crown cover over the battlefield, 2 m texels
uniform sampler2D uWaterTex;  // R32F water surface per sample; very negative where dry

// Fades a pattern of feature size `size` metres out before it shimmers.
float detailFade(float size, float px) { return 1.0 - smoothstep(size * 0.35, size * 1.2, px); }

void main() {
#ifdef DETAIL_FADE
    if (detailWeight(vWorld.xy) <= hash12(floor(gl_FragCoord.xy)) * 0.92) discard;  // cross-fading with the coarse land
#endif
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
        float grass = kind.x * (1.0 - soilMask);
        albedo *= mix(1.0, 0.8 + 0.4 * clump, grass * detailFade(1.3, px));
        if (grass * detailFade(0.25, px) > 0.0) {  // (the noise only where it shows: the same picture, a third of the cost from afar)
            float blades = valueNoise(p * vec2(6.3, 2.2) + 1.7) * 0.5 + valueNoise(p * vec2(2.1, 6.9) + 9.3) * 0.5;
            albedo *= mix(1.0, 0.78 + 0.44 * blades, grass * detailFade(0.25, px));
        }
        // Wildflowers: sparse dots of white, yellow, violet and red.
        vec2 fc = floor(p / 0.45);
        float fh = hash12(fc + 0.37);
        if (fh > 0.955 && grass > 0.5 && detailFade(0.1, px) > 0.0) {
            vec2 centre = (fc + 0.25 + 0.5 * vec2(hash12(fc + 3.1), hash12(fc + 7.7))) * 0.45;
            float d = length(p - centre);
            float k = hash12(fc + 11.0);
            vec3 petal = k < 0.4 ? vec3(0.95, 0.94, 0.88) : k < 0.7 ? vec3(0.95, 0.82, 0.25) : k < 0.9 ? vec3(0.62, 0.45, 0.85) : vec3(0.85, 0.22, 0.2);
            float spot = (1.0 - smoothstep(0.035, 0.07, d)) * detailFade(0.1, px);
            albedo = mix(albedo, petal, spot * smoothstep(0.4, 0.8, lush));
        }
        // Forest floor: leaf litter, needles and moss.
        float floorShown = clamp(kind.y * 0.8 + canopy * 0.7, 0.0, 1.0);
        if (floorShown > 0.0) {
            vec3 litter = mix(vec3(0.30, 0.22, 0.12), vec3(0.42, 0.30, 0.16), valueNoise(p * 2.7));
            litter = mix(litter, vec3(0.22, 0.30, 0.12), smoothstep(0.55, 0.8, valueNoise(p * 0.6 + 2.0)));
            // From high up the trees are too small to see: show the canopy's green, not the floor.
            // Same crowns-and-gaps pattern as the distant canopy, so the two meet seamlessly.
            if (dist > 150.0) {
                float crowns = valueNoise2(p / 9.0) * 0.55 + valueNoise2(p / 9.0 * 2.1 + 3.7) * 0.3 + valueNoise2(p / 9.0 * 4.3 + 9.1) * 0.15;
                litter = mix(litter, mix(vec3(0.10, 0.17, 0.07), vec3(0.24, 0.34, 0.13), crowns), smoothstep(150.0, 600.0, dist));
            }
            albedo = mix(albedo, litter, floorShown);
        }
        // Rock: strata and lichen.
        float stoneShown = max(kind.z, smoothstep(0.3, 0.45, slope)) * 0.8;
        if (stoneShown > 0.0) {
            float strata = 0.5 + 0.5 * sin(vWorld.z * 2.6 + valueNoise(p * 0.3) * 6.0);
            vec3 stone = vec3(0.48, 0.46, 0.43) * (0.8 + 0.3 * strata) * (0.85 + 0.3 * valueNoise(p * 3.1));
            stone = mix(stone, vec3(0.62, 0.62, 0.42), smoothstep(0.7, 0.85, valueNoise(p * 1.9 + 4.0)) * 0.6);
            albedo = mix(albedo, stone, stoneShown);
        }
        // Swamp: dark wet mud and standing puddles.
        if (kind.w > 0.0) {
            float puddles = smoothstep(0.6, 0.7, valueNoise(p * 0.5 + 20.0));
            albedo = mix(albedo, mix(vec3(0.20, 0.22, 0.14), vec3(0.18, 0.24, 0.26), puddles), kind.w * 0.7);
        }
        // Stones lying in the grass and soil.
        vec2 sc = floor(p / 0.8);
        if (hash12(sc + 5.5) > 0.93 && detailFade(0.15, px) > 0.0) {
            vec2 centre = (sc + 0.3 + 0.4 * vec2(hash12(sc + 1.3), hash12(sc + 2.9))) * 0.8;
            float d = length((p - centre) * vec2(1.0, 1.4));
            albedo = mix(albedo, vec3(0.55, 0.53, 0.5) * (0.8 + 0.4 * hash12(sc)), (1.0 - smoothstep(0.06, 0.11, d)) * detailFade(0.15, px));
        }
        // Bumps that catch the light: tussocks, clods and roots.
        float bump = 0.06 * near * detailFade(0.4, px);
        if (bump > 0.0) {
            float e = 0.15;
            float h0 = valueNoise(p * 1.6) + 0.5 * valueNoise(p * 4.3 + 2.0);
            float hx = valueNoise((p + vec2(e, 0.0)) * 1.6) + 0.5 * valueNoise((p + vec2(e, 0.0)) * 4.3 + 2.0);
            float hy = valueNoise((p + vec2(0.0, e)) * 1.6) + 0.5 * valueNoise((p + vec2(0.0, e)) * 4.3 + 2.0);
            n = normalize(n + vec3(-(hx - h0) / e, -(hy - h0) / e, 0.0) * bump);
            slope = 1.0 - n.z;
        }
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
    albedo = mix(albedo, vec3(0.93, 0.94, 0.96), smoothstep(103.0, 117.0, vWorld.z + fine * 6.0) * smoothstep(0.55, 0.3, slope));
    // From high up, the coarse land's look, so the battlefield and the land
    // around it read as one (the coarse land beyond knows nothing finer).
    float farBlend = feature(kFeatFarDetail) ? smoothstep(450.0, 1100.0, uCamDist) : 0.0;
    if (farBlend > 0.0) {
        vec3 base = mix(mix(farGroundColor(g00, vWorld.z), farGroundColor(g10, vWorld.z), f.x),
                        mix(farGroundColor(g01, vWorld.z), farGroundColor(g11, vWorld.z), f.x), f.y);
        vec3 nFar = n;
        albedo = mix(albedo, farAlbedo(base, vec4(kind.y, kind.z, kind.w, 0.0), vWorld, nFar), farBlend);
        n = normalize(mix(n, nFar, farBlend));
    }
    // Banks: the riverbed under the water, gravel and mud at the waterline,
    // a dark wet band above it (the nearest water surface, not one level).
    {
        ivec2 cc = ivec2(floor(p / uCellSize));
        float surf = -1e9, nearest = 1e9;  // and how far off the nearest wet sample is
        for (int k = 0; k < 8; ++k) {
            if (k >= 4 && !feature(kFeatGroundDetail)) break;
            int j = k - 4;
            ivec2 o = k < 4 ? ivec2(k & 1, k >> 1) : ivec2(j == 0 ? 2 : j == 1 ? -1 : 0, j == 2 ? 2 : j == 3 ? -1 : 0);
            float w = texelFetch(uWaterTex, texCell(vec2(cc + o), uCells), 0).r;
            if (w > -1000.0) nearest = min(nearest, distance(p, vec2(cc + o) * uCellSize));
            surf = max(surf, w);
        }
        if (surf > -1000.0) {
            float above = vWorld.z - surf;
            // The wet band thins out with the distance to the water, not at a cell's edge.
            float reach = 1.0 - smoothstep(5.0, 9.5, nearest);
            float wet = (1.0 - smoothstep(0.05, 0.8 + fine * 0.5, above)) * step(-0.05, above) * reach;
            albedo = mix(albedo, albedo * vec3(0.6, 0.56, 0.5) + vec3(0.03, 0.02, 0.0), wet);
            float line = (1.0 - smoothstep(0.0, 0.22 + fine * 0.1, abs(above - 0.04))) * reach;
            vec3 gravel = mix(vec3(0.50, 0.46, 0.39), vec3(0.36, 0.30, 0.22), valueNoise(p * 3.3));
            albedo = mix(albedo, gravel, line * 0.75);
            if (above < 0.0) albedo = mix(albedo, mix(vec3(0.40, 0.36, 0.27), vec3(0.30, 0.30, 0.22), valueNoise(p * 0.8)), 0.75);
        }
    }

    // Low sun so the hills read clearly from above; groves shade the ground.
    float sun = max(dot(n, kSun), 0.0) * shadowAt(vWorld, n) * cloudShade(p);
    float grove = canopy * (1.0 - farBlend);  // from high up the coarse land's canopy carries the woods
    float ambient = 0.30 * (1.0 - grove * 0.45);
    if (!feature(kFeatShadows)) sun *= 1.0 - grove * 0.55;  // no shadow map: fake the groves' shade
    vec3 color = albedo * (ambient + 0.95 * sun);

    // The grids, as on all the land; haze as on the coarse land.
    color = landGrids(color, vWorld.xy);
    oColor = vec4(mix(color, hazeColor(vWorld), fogFactor(dist) * 0.6), 1.0);
}
)"
inline const char* kTerrainFs = ILLYRIA_LAND_FS ILLYRIA_TERRAIN_FS;
// The close-up land off the battlefield: the same, cross-fading at its edges
// (a separate program, so the battlefield's ground never pays for discard).
inline const char* kPatchFs = "#define DETAIL_FADE 1\n" ILLYRIA_LAND_FS ILLYRIA_TERRAIN_FS;
#undef ILLYRIA_TERRAIN_FS

// ---------------------------------------------------------------- decals
// Per instance: centre (xyz), params (yaw, size, spawn seconds, type), extra (rgb, seed).
// Types: 0 blood, 1 shadow, 2 selection ring, 3 corpse, 4 crater, 5 dust, 6 body part, 7 dot, 8 arrowhead, 9 bar, 10 cross-hair, 11 a numbered badge (the digit in the spawn-seconds slot).
// Drawn as triangles, uDecalGrid x uDecalGrid cells per decal, each vertex
// on the ground there: a big marker follows the land's folds (the terrain
// mesh is 4 m cells, so a single quad's chord cut into ridges and banks).
// iCentre.z is the floor the caller found (Battle::floorAt): well above the
// ground it is a deck (a wall-walk, stair or bridge) and the decal lies flat
// on it; off the battlefield (no height texture there) it lies flat too.
inline const char* kDecalVs = R"(
layout(location = 0) in vec3 iCentre;
layout(location = 1) in vec4 iParams;
layout(location = 2) in vec4 iExtra;
uniform float uDecalGrid;
out vec2 vUv;
flat out vec4 vParams;
flat out vec4 vExtra;
out float vFog;
const ivec2 kTri[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
void main() {
    if (iParams.y <= 0.0) { gl_Position = kCulled; return; }
    int n = max(int(uDecalGrid + 0.5), 1);
    int cell = gl_VertexID / 6;
    vec2 corner = vec2(ivec2(cell % n, cell / n) + kTri[gl_VertexID % 6]) / float(n) * 2.0 - 1.0;
    float c = cos(iParams.x), s = sin(iParams.x);
    vec2 local = corner * iParams.y;
    vec2 xy = iCentre.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    // Lift grows with distance to avoid z-fighting.
    float lift = 0.04 + uCamDist * 0.0004 + (iParams.w > 5.5 ? 0.04 : 0.0);
    float size = uCells * uCellSize;
    bool onField = all(greaterThanEqual(iCentre.xy, vec2(0.0))) && all(lessThanEqual(iCentre.xy, vec2(size)));
    bool onDeck = iCentre.z > groundZ(iCentre.xy) + 0.12;
    vec3 world = vec3(xy, (onField && !onDeck ? groundZ(xy) : iCentre.z) + lift);
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
        // A soldier's shadow (extra.x: how much narrower across his body than
        // along it), darker in the middle where he stands (extra.y: 1; not
        // under scenery).
        float q = length(vec2(vUv.x, vUv.y * (1.0 + vExtra.x)));
        color = vec4(0.0, 0.0, 0.0, 0.3 * (1.0 - smoothstep(0.3, 1.0, q)) + 0.25 * vExtra.y * (1.0 - smoothstep(0.0, 0.5, q)));
    } else if (type < 2.5) {
        // Rings and order markers: extra.w fades them (1 - opacity).
        float ring = smoothstep(0.62, 0.72, r) * (1.0 - smoothstep(0.86, 0.96, r));
        color = vec4(vExtra.rgb, ring * 0.9 * (1.0 - vExtra.w));
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
    } else if (type > 6.5 && type < 7.5) {
        // Order marker: a soft dot (the facing line).
        color = vec4(vExtra.rgb, 0.9 * (1.0 - smoothstep(0.55, 0.95, r)) * (1.0 - vExtra.w));
    } else if (type > 7.5 && type < 8.5) {
        // Order marker: an arrowhead pointing along +x (the way to face).
        float halfW = (0.9 - vUv.x) * 0.45;
        float inside = step(-0.5, vUv.x) * step(vUv.x, 0.9) * (1.0 - smoothstep(halfW - 0.06, halfW, abs(vUv.y)));
        color = vec4(vExtra.rgb, 0.9 * inside * (1.0 - vExtra.w));
    } else if (type > 8.5 && type < 9.5) {
        // A stretch of a path (renderer_orders.cpp): a bar along +x, the whole width of the square and a third of its
        // height, so the size is half its length.
        float bar = (1.0 - smoothstep(0.27, 0.35, abs(vUv.y))) * (1.0 - smoothstep(0.94, 1.0, abs(vUv.x)));
        color = vec4(vExtra.rgb, 0.95 * bar * (1.0 - vExtra.w));
    } else if (type > 9.5 && type < 10.5) {
        // The quarry of a hunt: a ring with a cross through it that reaches beyond.
        float ring = smoothstep(0.5, 0.58, r) * (1.0 - smoothstep(0.66, 0.74, r));
        float cross = max((1.0 - smoothstep(0.07, 0.12, abs(vUv.x))) * step(abs(vUv.y), 0.98),
                          (1.0 - smoothstep(0.07, 0.12, abs(vUv.y))) * step(abs(vUv.x), 0.98)) * step(0.3, r);
        color = vec4(vExtra.rgb, 0.95 * max(ring, cross) * (1.0 - vExtra.w));
    } else if (type > 10.5 && type < 11.5) {
        // A numbered badge (a queued order's place in the queue): a dark disc with a ring and the digit (vParams.z, 1-9) in
        // seven segments, all in the colour of the line.
        float digit = floor(vParams.z + 0.5);
        float disc = 1.0 - smoothstep(0.9, 0.98, r);
        float ring = smoothstep(0.7, 0.78, r) * (1.0 - smoothstep(0.88, 0.96, r));
        int mask = 0x3F;
        if (digit < 1.5) mask = 0x06; else if (digit < 2.5) mask = 0x5B; else if (digit < 3.5) mask = 0x4F;
        else if (digit < 4.5) mask = 0x66; else if (digit < 5.5) mask = 0x6D; else if (digit < 6.5) mask = 0x7D;
        else if (digit < 7.5) mask = 0x07; else if (digit < 8.5) mask = 0x7F; else if (digit < 9.5) mask = 0x6F;
        float lit = 0.0;
        vec2 p = vec2(-vUv.x, vUv.y);
        const float w = 0.075;
        if ((mask & 1) != 0)  lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(-0.26, -0.5), vec2(0.26, -0.5))));
        if ((mask & 2) != 0)  lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(0.28, -0.48), vec2(0.28, -0.02))));
        if ((mask & 4) != 0)  lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(0.28, 0.02), vec2(0.28, 0.48))));
        if ((mask & 8) != 0)  lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(-0.26, 0.5), vec2(0.26, 0.5))));
        if ((mask & 16) != 0) lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(-0.28, 0.02), vec2(-0.28, 0.48))));
        if ((mask & 32) != 0) lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(-0.28, -0.48), vec2(-0.28, -0.02))));
        if ((mask & 64) != 0) lit = max(lit, 1.0 - smoothstep(w, w + 0.05, seg(p, vec2(-0.26, 0.0), vec2(0.26, 0.0))));
        float ink = max(ring, lit);
        color = vec4(mix(vec3(0.03, 0.03, 0.04), vExtra.rgb, ink), max(disc * 0.78, ink) * (1.0 - vExtra.w));
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

// ---------------------------------------------------------------- soldiers' sheets
// Baking: one posed figure (figures::build) photographed orthographically
// from a sheet view, lit from over the viewer's left shoulder, into a
// scratch target twice the cell's size.
inline const char* kFigureVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aTeam;
layout(location = 4) in float aMat;
uniform vec3 uBakeRight, uBakeUp, uBakeToward;
uniform vec4 uBakeRect;  // the picture's extent: left, bottom, width, height (m)
out vec3 vNormal;
out vec3 vColor;
out vec3 vPos;
out float vTeam;
flat out int vMat;
void main() {
    vNormal = aNormal;
    vColor = aColor;
    vPos = aPos;
    vTeam = aTeam;
    vMat = int(aMat + 0.5);
    vec2 c = (vec2(dot(aPos, uBakeRight), dot(aPos, uBakeUp)) - uBakeRect.xy) / uBakeRect.zw;
    gl_Position = vec4(c * 2.0 - 1.0, -dot(aPos, uBakeToward) / 40.0, 1.0);
}
)";
// The figure seen from its key light, for its own shadows (arms across the
// chest, a shield over the body, the rider on the horse).
inline const char* kFigureDepthVs = R"(
layout(location = 0) in vec3 aPos;
uniform vec3 uLightRight, uLightUp, uLightToward;
uniform vec4 uLightRect;   // left, bottom, width, height (m) across the light
uniform vec2 uLightRange;  // nearest along the light, depth range (m)
void main() {
    vec2 c = (vec2(dot(aPos, uLightRight), dot(aPos, uLightUp)) - uLightRect.xy) / uLightRect.zw;
    float d = (uLightRange.x - dot(aPos, uLightToward)) / uLightRange.y;  // 0 nearest the light
    gl_Position = vec4(c * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
}
)";
inline const char* kFigureFs = R"(
uniform vec3 uBakeRight, uBakeUp, uBakeToward;
uniform highp sampler2DShadow uLightDepth;
uniform vec3 uLightRight, uLightUp, uLightToward;
uniform vec4 uLightRect;
uniform vec2 uLightRange;
in vec3 vNormal;
in vec3 vColor;
in vec3 vPos;
in float vTeam;
flat in int vMat;
out vec4 oColor;
// Materials, as in figures.cpp.
const int kPlain = 0, kCloth = 1, kSkin = 2, kSteel = 3, kMail = 4, kLeather = 5, kWood = 6, kHair = 7, kBone = 8;
const int kGold = 9, kFur = 10, kScale = 11, kFeather = 12, kMembrane = 13, kHide = 14, kChitin = 15;
float h3(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float n3(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(h3(i), h3(i + vec3(1, 0, 0)), f.x), mix(h3(i + vec3(0, 1, 0)), h3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(h3(i + vec3(0, 0, 1)), h3(i + vec3(1, 0, 1)), f.x), mix(h3(i + vec3(0, 1, 1)), h3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) { return (0.5 * n3(p) + 0.25 * n3(p * 2.03 + 7.1) + 0.125 * n3(p * 4.07 + 3.3) + 0.0625 * n3(p * 8.13 + 1.7)) / 0.9375; }
void main() {
    // Alpha marks what was drawn: 1, or 0.5 where the side's colour goes.
    float mark = vTeam > 0.5 ? 0.5 : 1.0;
    // Colours above 1.5 glow: drawn unlit, less 2 (see figures.cpp).
    if (vColor.r > 1.5) { oColor = vec4(vColor - 2.0, mark); return; }
    vec3 n = normalize(vNormal);
    vec3 V = uBakeToward;
    if (dot(n, V) < 0.0) n = -n;  // the inside of a cape or a wing
    vec3 L = uLightToward;
    vec3 p = vPos;
    // Self-shadow from the key light.
    vec2 luv = (vec2(dot(p, uLightRight), dot(p, uLightUp)) - uLightRect.xy) / uLightRect.zw;
    float ld = (uLightRange.x - dot(p, uLightToward)) / uLightRange.y;
    float lit = texture(uLightDepth, vec3(luv, ld - 0.012));
    // Surface: albedo variation, specular strength and tightness, per material.
    float albedo = 1.0, spec = 0.04, shine = 8.0;
    vec3 tint = vec3(1.0);
    if (vMat == kCloth) {
        albedo = 0.74 + 0.4 * fbm(p * vec3(11.0, 11.0, 4.0));          // folds hang down
        albedo *= 0.94 + 0.06 * sin((p.x + p.y) * 260.0) * sin(p.z * 260.0);  // weave
        albedo *= mix(0.72, 1.0, smoothstep(0.05, 0.55, p.z));          // mud at the hem
    } else if (vMat == kSkin) {
        albedo = 0.9 + 0.16 * fbm(p * 24.0);
        spec = 0.1, shine = 10.0;
    } else if (vMat == kSteel) {
        albedo = 0.72 + 0.3 * fbm(p * vec3(5.0, 5.0, 38.0));            // brushed, dented
        albedo *= 1.0 - 0.25 * step(0.82, n3(p * 70.0));               // nicks and rust spots
        spec = 0.95, shine = 36.0;
    } else if (vMat == kMail) {
        vec3 q = p * vec3(60.0, 60.0, 75.0);
        float ring = abs(fract(q.x + 0.5 * floor(q.z)) - 0.5) + abs(fract(q.z) - 0.5);
        albedo = 0.55 + 0.6 * smoothstep(0.25, 0.55, ring) + 0.15 * fbm(p * 9.0);
        spec = 0.7, shine = 18.0;
    } else if (vMat == kLeather) {
        albedo = 0.76 + 0.36 * fbm(p * 22.0);
        spec = 0.18, shine = 9.0;
    } else if (vMat == kWood) {
        float g = fbm(vec3(p.x * 7.0, p.y * 7.0, p.z * 44.0)) + fbm(vec3(p.x * 44.0, p.y * 7.0, p.z * 7.0));
        albedo = 0.68 + 0.5 * abs(sin(g * 9.0)) * 0.6;
        spec = 0.08;
    } else if (vMat == kHair) {
        albedo = 0.66 + 0.55 * fbm(p * vec3(70.0, 70.0, 10.0));
        spec = 0.3, shine = 22.0;
    } else if (vMat == kBone) {
        albedo = 0.82 + 0.24 * fbm(p * 18.0);
        spec = 0.12;
    } else if (vMat == kGold) {
        albedo = 0.8 + 0.25 * fbm(p * 12.0);
        spec = 1.1, shine = 26.0, tint = vec3(1.0, 0.82, 0.45);
    } else if (vMat == kFur) {
        albedo = 0.74 + 0.42 * fbm(p * vec3(30.0, 30.0, 18.0));
        spec = 0.06;
    } else if (vMat == kScale) {
        vec3 q = p * 13.0;
        vec2 c = fract(vec2(q.x + 0.5 * floor(q.z), q.z)) - 0.5;
        float edge = smoothstep(0.28, 0.48, length(c));
        albedo = (0.85 + 0.25 * fbm(p * 6.0)) * (1.0 - 0.38 * edge);
        spec = 0.45, shine = 16.0;
    } else if (vMat == kFeather) {
        albedo = 0.72 + 0.4 * fbm(p * vec3(34.0, 9.0, 34.0));
        albedo *= 0.9 + 0.1 * sin(dot(p, vec3(40.0, 40.0, 10.0)));
        spec = 0.08;
    } else if (vMat == kMembrane) {
        albedo = 0.78 + 0.3 * fbm(p * 5.0);
        albedo *= 1.0 - 0.3 * smoothstep(0.46, 0.5, abs(fract(dot(p, vec3(3.0, 3.0, 1.0))) - 0.5));  // finger bones
        spec = 0.15, shine = 12.0;
    } else if (vMat == kHide) {
        albedo = 0.66 + 0.48 * fbm(p * vec3(10.0, 10.0, 26.0));      // wrinkled
        spec = 0.05;
    } else if (vMat == kChitin) {
        albedo = 0.8 + 0.3 * fbm(p * 8.0);
        spec = 0.8, shine = 30.0;
    }
    vec3 base = vColor * albedo;
    // Light: a warm key over the viewer's shoulder (shadowed), a cool sky
    // from above, a little light thrown up from the ground, a rim.
    float key = max(0.0, dot(n, L)) * mix(0.5, 1.0, lit);
    float sky = 0.5 + 0.5 * n.z;
    float bounce = max(0.0, -n.z) * 0.12;
    float rim = pow(1.0 - max(0.0, dot(n, V)), 3.0) * 0.2;
    float ao = 0.75 + 0.25 * clamp(p.z / 1.1, 0.0, 1.0);
    vec3 light = vec3(1.0, 0.95, 0.86) * key * 1.02 + vec3(0.62, 0.68, 0.78) * sky * 0.62 * ao + vec3(0.5, 0.45, 0.35) * bounce + rim;
    vec3 c = base * light;
    if (vMat == kSkin) c += vColor * vec3(0.12, 0.03, 0.0) * (1.0 - key);  // warmth where the light is low
    // Highlights.
    vec3 h = normalize(L + V);
    float s = pow(max(0.0, dot(n, h)), shine) * spec * lit;
    c += tint * s * 0.75;
    if (vMat == kSteel || vMat == kGold || vMat == kChitin) c += tint * vColor * mix(vec3(0.16, 0.14, 0.1), vec3(0.42, 0.46, 0.5), sky) * 0.45;  // the sky in the metal
    oColor = vec4(c, mark);
}
)";
// A sheet drawn over the screen, for looking at it (?sheet=<type>).
inline const char* kSheetViewVs = R"(
out vec2 vUv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vUv = c;
    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);
}
)";
inline const char* kSheetViewFs = R"(
uniform sampler2D uSheet;
uniform vec3 uCloth;
uniform vec4 uSheetView;  // visible part of the sheet: u0, v0, u1, v1
in vec2 vUv;
out vec4 oColor;
void main() {
    vec4 t = texture(uSheet, mix(uSheetView.xy, uSheetView.zw, vUv));
    vec3 bg = vec3(0.24, 0.3, 0.18);
    vec3 c = t.a > 0.68 && t.a < 0.88 ? t.rgb * uCloth : t.rgb;
    oColor = vec4(t.a < 0.5 ? bg : c, 1.0);
}
)";
// Resolving the scratch into its sheet cell: 2x2 samples to one pixel, hard
// edges, and a dark outline one pixel wide, as Myth's sprites had. Alpha in
// the sheet: 1 plain, 0.75 the side's colour (tinted when drawn), 0.6 outline.
inline const char* kResolveVs = R"(
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);
}
)";
inline const char* kResolveFs = R"(
uniform sampler2D uScratch;
uniform vec4 uResolve;  // cell origin in the sheet (px), cell size (px)
out vec4 oColor;
float cover(ivec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= int(uResolve.z) || p.y >= int(uResolve.w)) return 0.0;
    ivec2 s = p * 2;
    return (step(0.25, texelFetch(uScratch, s, 0).a) + step(0.25, texelFetch(uScratch, s + ivec2(1, 0), 0).a) +
            step(0.25, texelFetch(uScratch, s + ivec2(0, 1), 0).a) + step(0.25, texelFetch(uScratch, s + ivec2(1, 1), 0).a)) * 0.25;
}
void main() {
    ivec2 p = ivec2(floor(gl_FragCoord.xy - uResolve.xy));
    ivec2 s = p * 2;
    vec3 col = vec3(0.0);
    float k = 0.0, team = 0.0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            vec4 t = texelFetch(uScratch, s + ivec2(i, j), 0);
            if (t.a < 0.25) continue;
            col += t.rgb;
            k += 1.0;
            team += t.a < 0.75 ? 1.0 : 0.0;
        }
    float l = cover(p + ivec2(-1, 0)), r = cover(p + ivec2(1, 0)), dn = cover(p + ivec2(0, -1)), up = cover(p + ivec2(0, 1));
    bool solidAround = l >= 0.5 && r >= 0.5 && dn >= 0.5 && up >= 0.5;
    bool anyAround = l >= 0.5 || r >= 0.5 || dn >= 0.5 || up >= 0.5;
    if (k >= 2.0) oColor = vec4(col / k * (solidAround ? 1.0 : 0.78), team * 2.0 >= k ? 0.75 : 1.0);
    else if (anyAround) oColor = vec4(0.05, 0.045, 0.04, 0.6);
    else oColor = vec4(0.0);
}
)";
// In battle: each soldier a billboard showing the frame of its sheet for its
// facing (view and mirror chosen here) and deed (frame chosen on the CPU).
// Per instance: feet, facing (radians), frame, selected, down (0 standing ..
// 1 lying). uCellTable holds, per cell (view x 26 + frame), its place in the
// sheet (px: x, y, w, h; row 0) and the picture's extent about the feet (m:
// left, bottom, w, h; row 1).
inline const char* kUnitVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in float iYaw;
layout(location = 2) in float iFrame;
layout(location = 3) in float iSelected;
layout(location = 4) in float iDown;
uniform highp sampler2D uCellTable;
uniform vec2 uSheetSize;  // px
// The viewer's bearing seen from the soldiers (radians), worked out on the
// CPU: ANGLE on Metal turns atan(y, -0.0) the wrong way, and a camera at
// exactly 90 or 270 degrees gives a view direction whose x is zero.
uniform float uToViewer;
uniform float uLift;     // metres drawn toward the viewer, against the ground
out vec2 vUv;
flat out float vSelected;
flat out float vDot;
out float vFog;
flat out float vShade;
uniform float uFramesPerView;  // 26 in the old layout, 44 in the new (sprite_layout.h), or as a sheet's own clips list
const float kElevation = 0.52;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    // A body part (renderer_gibs.cpp): iFrame -(its place among the gib
    // cells, which follow the five views' + 1), the same from every side,
    // turned by iYaw in the picture's plane, mirrored if iDown is 1.
    bool gib = iFrame < -0.5;
    // Which of the eight views: the viewer's bearing seen from the soldier.
    float d = mod(floor(-(uToViewer - iYaw) / 0.7853982 + 0.5), 8.0);
    bool mirror = gib ? iDown > 0.5 : d > 4.5;
    float view = mirror ? 8.0 - d : d;
    int cell = gib ? int(5.0 * uFramesPerView + 0.5) + int(-iFrame - 0.5) : int(view * uFramesPerView + iFrame + 0.5);
    vec4 place = texelFetch(uCellTable, ivec2(cell, 0), 0);
    vec4 extent = texelFetch(uCellTable, ivec2(cell, 1), 0);
    float dist = distance(uEye, iPos);
    // Zoomed far out, soldiers are coloured specks (as the old sprites were); parts vanish.
    float px = uPxScale * dist;
    vDot = !gib && extent.w < px * 7.0 ? 1.0 : 0.0;
    vec3 world;
    if (gib) {
        if (max(extent.z, extent.w) < px * 1.5) { gl_Position = kCulled; return; }
        float x0 = mirror ? -(extent.x + extent.z) : extent.x;
        float u = x0 + corner.x * extent.z, v = extent.y + corner.y * extent.w;
        float c = cos(iYaw), s = sin(iYaw);
        // Its lowest corner, turned, on iPos.
        float low = min(x0 * s, (x0 + extent.z) * s) + min(extent.y * c, (extent.y + extent.w) * c);
        world = iPos + uCamRight * (u * c - v * s) + vec3(0.0, 0.0, (u * s + v * c - low) / cos(kElevation));
    } else if (vDot > 0.5) {
        float size = px * 4.5;
        world = iPos + uCamRight * ((corner.x - 0.5) * size) + vec3(0.0, 0.0, corner.y * size + size * 0.25);
    } else {
        float u = mirror ? -(extent.x + extent.z) + corner.x * extent.z : extent.x + corner.x * extent.z;
        float v = extent.y + corner.y * extent.w;
        world = iPos + uCamRight * u + vec3(0.0, 0.0, v / cos(kElevation));
        // A man standing on a hillside stands upright; one lying on it lies
        // along it: each side of his picture rises or sinks with the ground
        // under it, as much as he is down. Not on a wall-walk or a bridge.
        if (iDown > 0.0) {
            float under = groundZ(iPos.xy);
            if (abs(iPos.z - under) < 0.5) world.z += (groundZ(iPos.xy + uCamRight.xy * u) - under) * iDown;
        }
    }
    vec2 inCell = vec2(mirror ? 1.0 - corner.x : corner.x, corner.y) * place.zw;
    vUv = (place.xy + inCell) / uSheetSize;
    vSelected = iSelected;
    vFog = fogFactor(dist);
    vShade = 0.72 + 0.28 * shadowAt(iPos + vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0)) * cloudShade(iPos.xy);
    // Drawn as if a little nearer the viewer, so the part of the picture
    // below the feet (the ground in front of them) is not lost in the earth;
    // moving along the line of sight leaves the picture where it is.
    // Only the feet and legs are drawn nearer: higher up the whole picture
    // pulled a metre toward the eye put a man who stands just beyond a gate
    // or wall in front of it.
    vec3 toEye = uEye - world;
    float len = length(toEye);
    float lifted = gib || vDot > 0.5 ? 1.0 : 1.0 - smoothstep(0.0, 1.0, (world.z - iPos.z) * cos(kElevation));
    // Looking along the ground there is none of it to sink into, and a man a little beyond a gate or wall would be
    // pulled through it: the pull is for the view from above.
    lifted *= smoothstep(0.12, 0.5, (uEye.z - iPos.z) / max(len, 1e-3));
    world += toEye * (min(uLift * lifted, len * 0.5) / max(len, 1e-3));
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kUnitFs = R"(
uniform sampler2D uSheet;
uniform vec3 uTeam;   // the side's colour, for specks
uniform vec3 uCloth;  // and as its cloth is dyed
in vec2 vUv;
flat in float vSelected;
flat in float vDot;
in float vFog;
flat in float vShade;
out vec4 oColor;
void main() {
    vec3 team = uTeam * (vSelected > 0.5 ? 1.35 : 1.0);
    if (vDot > 0.5) {
        oColor = vec4(mix(team, kSky, vFog * 0.5), 1.0);
        return;
    }
    vec4 t = texture(uSheet, vUv);
    if (t.a < 0.5) discard;
    vec3 c = t.a > 0.68 && t.a < 0.88 ? t.rgb * uCloth : t.rgb;
    c *= vShade;
    if (vSelected > 0.5) c = c * 1.15 + team * 0.12;
    oColor = vec4(mix(c, kSky, vFog), 1.0);
}
)";

// ---------------------------------------------------------------- meshes
// Instanced lit meshes: scenery and bodies. Per vertex: pos, normal, colour.
// Per instance: position, rotation quaternion, scale, tint.
// What the scenery's vertex shaders share (meshes and tree boards): which scenery stands where, and the instance's
// record. uLod: 1 trees of the battlefield and the close-up land; -1 the trees of the rest of the continent, which
// stand where those do not; 2 undergrowth, rocks and logs, near the camera only; 0 always drawn.
#define ILLYRIA_SCENERY_VS R"(
layout(location = 3) in vec3 iPos;
layout(location = 4) in vec4 iQuat;
layout(location = 5) in vec3 iScale;
layout(location = 6) in vec3 iTint;
uniform float uSway;
uniform float uLod;
uniform vec4 uHole;  // the battlefield
flat out float vLod;   // how much of this piece shows (the dither in dissolved()), not how big it is
vec3 rotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
// Undergrowth, rocks and logs stand near the camera's focus while the camera is low, wherever they exist (the
// battlefield, or the close-up land's built chunks); too small to see from anywhere else.
float sceneryNear(vec2 p) {
    float there = all(greaterThanEqual(p, uHole.xy)) && all(lessThan(p, uHole.zw)) ? 1.0 : detailMask(p);
    return there * (1.0 - smoothstep(600.0, 850.0, uCamDist)) * (1.0 - smoothstep(180.0, 270.0, distance(p, uFocus)));
}
// How much of the battlefield's and the close-up land's own trees stand at p, in place of the continent's: the
// battlefield's always, the close-up land's while the camera is near enough to have built it. The two sets are the
// same trees where both grow (one rule, one hash), so the cross-fade between them is of identical trees.
float treesHere(vec2 p) {
    bool field = all(greaterThanEqual(p, uHole.xy)) && all(lessThan(p, uHole.zw));
    return field ? 1.0 : detailMask(p) * (1.0 - smoothstep(600.0, 850.0, uCamDist));
}
float sceneryShown(vec2 p) { return uLod > 1.5 ? sceneryNear(p) : uLod > 0.5 ? treesHere(p) : uLod < -0.5 ? 1.0 - treesHere(p) : 1.0; }
// The wind in the crowns: where a vertex at relative height `up` (0 trunk, 1 top) of an instance at iPos moves to.
vec2 crownSway(float up, float sway) {
    return vec2(sin(uTime * 1.3 + iPos.x * 0.31), cos(uTime * 1.1 + iPos.y * 0.27)) * up * up * sway * iScale.z;
}
// Whether a thing about `radius` metres round the point is wholly outside the view.
bool outOfView(vec3 centre, float radius) {
    vec4 c = uViewProj * vec4(centre, 1.0);
    vec3 pad = radius * vec3(length(vec3(uViewProj[0][0], uViewProj[1][0], uViewProj[2][0])), length(vec3(uViewProj[0][1], uViewProj[1][1], uViewProj[2][1])), 0.0);
    return c.w < -radius || abs(c.x) > c.w + pad.x || abs(c.y) > c.w + pad.y;
}
)"
// The cross-fade between the battlefield's and the close-up land's trees and the continent's that stand where they
// do not: each shows in the pixels the other leaves (complementary screen dither), so nothing is scaled and no tree
// grows or shrinks.
#define ILLYRIA_SCENERY_FS R"(
flat in float vLod;
uniform float uLod;
bool dissolved() {
    if (vLod >= 0.999) return false;
    float h = hash12(gl_FragCoord.xy);
    return uLod < -0.5 ? (1.0 - h) >= vLod : h >= vLod;
}
// The scenery's light: the sun and its shadows and the clouds', a little from the sky, the river over it, the haze.
vec4 sceneryColor(vec3 albedo, vec3 n, vec3 world) {
    float sun = max(dot(n, kSun), 0.0) * shadowAt(world, n) * cloudShade(world.xy);
    float light = 0.36 + 0.8 * sun + 0.12 * max(-dot(n, kSun), 0.0);
    vec3 c = albedo * light;
    // Under the battlefield's river (the rest of the continent has no water level).
    vec2 cell = world.xy / uCellSize;
    bool onField = all(greaterThanEqual(cell, vec2(0.0))) && all(lessThanEqual(cell, vec2(uCells)));
    if (onField && world.z < uWater) c = mix(c, vec3(0.1, 0.25, 0.32), clamp((uWater - world.z) * 0.6, 0.0, 0.8));
    return vec4(mix(c, hazeColor(world), fogFactor(distance(uEye, world))), 1.0);
}
)"
inline const char* kMeshVs = ILLYRIA_SCENERY_VS R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform float uFlag; // 1: banners, whose cloth (colour.b = 2, colour.rg its place on it) waves
uniform float uOwned; // 1: a town's meshes: the tint is the holder's colour, worn by the roofs only (owner_look.h)
uniform float uHouse; // 1: the house mesh (houseSurface)
out vec3 vColor;
out vec3 vNormal;
out vec3 vWorld;
out vec2 vCloth;     // where on a flag's cloth (u from the pole, v up)
flat out float vIsCloth;
flat out vec2 vAxis; // the instance's local x on the ground, either way round (masonry)
out vec3 vObj;       // the vertex in the mesh's own unit space, and the instance's scale (buildings)
flat out vec3 vScale;
void main() {
    if (iScale.x <= 0.0) { gl_Position = kCulled; return; }
    float lod = sceneryShown(iPos.xy);
    if (lod < 0.02) { gl_Position = kCulled; return; }
    vLod = lod;  // (a tree fades in by dissolving, never by growing out of the ground)
    vec3 pos = aPos, nrm = aNormal;
    vIsCloth = 0.0;
    vCloth = vec2(0.0);
    if (uFlag > 0.5 && aColor.b > 1.5) {
        // Cloth in the wind, pinned at the pole: travelling waves growing
        // toward the fly, a little droop; each flag on its own beat.
        float u = aColor.r, v = aColor.g;
        float seed = iPos.x * 0.37 + iPos.y * 0.23;
        float a1 = u * 7.0 - uTime * 4.2 + seed, a2 = u * 12.0 - uTime * 6.9 + v * 2.5 + seed * 1.7;
        float amp = 0.045 * u + 0.012 * u * u;
        float wave = sin(a1) + 0.35 * sin(a2);
        pos.y += amp * wave;
        pos.x -= 0.03 * u * u * (0.6 + 0.4 * sin(uTime * 1.3 + seed));
        pos.z -= 0.025 * u * u * (1.0 - v * 0.5);
        // Its slope along the fly, for the light (0.508: the cloth's length).
        float slope = (amp * (cos(a1) * 7.0 + 0.35 * cos(a2) * 12.0) + (0.045 + 0.024 * u) * wave) / 0.508;
        nrm = normalize(vec3(-slope, 1.0, 0.0));
        vIsCloth = 1.0;
        vCloth = vec2(u, v);
    }
    vec3 local = pos * iScale;
    if (uSway > 0.0) {
        // Crowns sway in the wind; trunks stay put.
        local.xy += crownSway(aPos.z, uSway);
    }
    vec3 world = iPos + rotate(iQuat, local);
    vNormal = normalize(rotate(iQuat, nrm / iScale));
    // Pointing +x-ward (or +y), so pieces laid either way along one line (a
    // wall's sections, the ramp beside it and its landing) share their courses.
    vec2 ax = rotate(iQuat, vec3(1.0, 0.0, 0.0)).xy;
    ax = dot(ax, ax) > 1e-6 ? normalize(ax) : vec2(1.0, 0.0);
    vAxis = ax.x < -1e-4 || (ax.x <= 1e-4 && ax.y < 0.0) ? -ax : ax;
    vColor = vIsCloth > 0.5 ? iTint : aColor * iTint;
    if (uOwned > 0.5 && vIsCloth < 0.5) {
        if (uHouse > 0.5) {
            // The tiles take the holder's colour (the mesh's red gives their shading); everything else the house's own,
            // each house a little lighter or darker than the next.
            float br = 0.85 + 0.3 * hash12(iPos.xy * 0.173);
            vColor = aNormal.z > 0.25 ? iTint * (aColor.r / 0.55) * br : aColor * vec3(1.0, 0.97, 0.92) * br;
        } else if (aColor.b > 2.5) {
            vColor = iTint * aColor.r;  // a cone (meshes.cpp kCone): colour.b marks it, colour.r is its shade
            vCloth = vec2(-1.0, 0.0);   // (unused but by cloth: here it tells the fragment shader this is a slate roof, to be coursed whatever its colour)
        } else {
            vColor = aColor;  // stone and timber: their own
        }
    }
    vWorld = world;
    vObj = aPos;
    vScale = iScale;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kMeshFs = ILLYRIA_SCENERY_FS R"(
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorld;
in vec2 vCloth;
flat in float vIsCloth;
flat in vec2 vAxis;
in vec3 vObj;
flat in vec3 vScale;
out vec4 oColor;
uniform float uMasonry;  // 1: lay coursed ashlar over the surface (castles, bridges)
uniform float uHouse;    // 1: the house mesh: shingles, framed plaster, a plank door (houseSurface)
uniform float uRock;     // 1: boulders: bedding planes, cracks, grain, lichen (rockSurface)
// Courses of dressed blocks, staggered row by row, each its own tone, with
// mortar lines that fade out (px: metres a pixel spans) before they shimmer.
vec3 coursedStone(vec3 albedo, vec2 uv, vec2 size, float px) {
    float row = floor(uv.y / size.y);
    float x = uv.x / size.x + 0.5 * mod(row, 2.0) + hash12(vec2(row, 3.7)) * 0.3;
    vec2 cell = vec2(floor(x), row);
    vec2 f = vec2(fract(x), fract(uv.y / size.y));
    float fade = 1.0 - smoothstep(0.067 * size.y, 0.267 * size.y, px);
    float mortar = smoothstep(0.0, 0.05, f.x) * smoothstep(1.0, 0.95, f.x) * smoothstep(0.0, 0.09, f.y) * smoothstep(1.0, 0.91, f.y);
    float tone = 0.84 + 0.26 * hash12(cell + 0.5) + 0.08 * (valueNoise2(uv * 3.1 * (0.45 / size.y)) - 0.5);
    return albedo * mix(1.0, mix(0.6, tone, mortar), fade);
}
// Grain of wood running along `along` (metres), fading to its mean with distance.
float woodGrain(float along, float across, float fade) {
    return mix(0.9, 0.74 + 0.36 * valueNoise2(vec2(along * 1.7, across * 48.0)), fade);
}
// A boulder: sedimentary bedding planes (wavy bands, each its own tone, a dark
// seam between), cracks that follow nothing, a grain of crystals, lichen
// patches on whatever faces up, and the weather darkening the lower face. Laid
// out in world metres (faces turned to the sky by x and y, the rest by the
// horizontal along the face and z), so it is one rock face across a rock's
// triangles; fades to its mean tone as a pixel covers the grain.
void rockSurface(inout vec3 albedo, inout vec3 n) {
    bool top = abs(n.z) > 0.7;
    vec2 uv = top ? vWorld.xy : vec2(dot(vWorld.xy, normalize(vec2(-n.y, n.x) + vec2(1e-4, 0.0))), vWorld.z);
    float px = max(fwidth(uv.x), fwidth(uv.y));
    float fade = 1.0 - smoothstep(0.015, 0.1, px);
    float warp = valueNoise2(uv * 1.7 + 4.0) * 1.6;
    float band = (top ? uv.x * 0.8 : uv.y * 6.0) + warp;
    float layer = floor(band);
    float tone = 0.8 + 0.34 * hash12(vec2(layer, 7.3));
    float seamLine = smoothstep(0.0, 0.07, fract(band)) * smoothstep(1.0, 0.93, fract(band));
    // Cracks: thin ridges of a second noise, branching where two cross.
    float c1 = abs(valueNoise2(uv * 3.1 + 11.0) - 0.5), c2 = abs(valueNoise2(uv * 6.3 + 29.0) - 0.5);
    float crack = (1.0 - smoothstep(0.0, 0.018 + px * 0.5, c1)) + 0.7 * (1.0 - smoothstep(0.0, 0.015 + px * 0.5, c2));
    float grain = 0.88 + 0.24 * hash12(floor(uv * 70.0)) + 0.1 * (valueNoise2(uv * 17.0) - 0.5);
    vec3 detail = albedo * tone * (top ? 0.92 + 0.08 * seamLine : 0.78 + 0.22 * seamLine) * (1.0 - 0.4 * clamp(crack, 0.0, 1.0)) * grain;
    float lichen = smoothstep(0.58, 0.74, valueNoise2(uv * 2.3 + 3.0)) * (top ? 0.8 : 0.35 * smoothstep(0.0, 0.6, n.z + 0.2));
    detail = mix(detail, detail * vec3(0.85, 0.95, 0.5) + vec3(0.04, 0.05, 0.0), lichen);
    vec3 mean = albedo * 0.92 * mix(vec3(1.0), vec3(0.9, 0.97, 0.75), 0.3 * lichen);
    albedo = mix(mean, detail, fade);
    n = normalize(n + (vec3(valueNoise2(uv * 9.0 + 1.0), valueNoise2(uv * 9.0 + 5.0), valueNoise2(uv * 9.0 + 9.0)) - 0.5) * 0.45 * fade);
}
// The house mesh (meshes::house): clay shingles in staggered rows with
// rounded tips that shade the row below; plaster framed in timber (posts a
// bay apart, sill, plate, braces) on a stone footing; a plank door in a
// frame with two iron straps and a ring. Everything is laid out in metres on
// the mesh's own axes (vObj, vScale), so it sits the same on every house, and
// each pattern fades to its mean colour before a pixel spans it. The pattern
// also tilts the normal a little, for the light.
void houseSurface(inout vec3 albedo, inout vec3 n) {
    vec3 m = vObj * vScale;
    float px = length(fwidth(m));
    vec3 base = albedo;
    float tint = clamp(base.g / 0.76, 0.6, 1.4);  // the instance's tint (the wall's own green is 0.76)
    if (n.z > 0.25) {
        // Roof: v up the slope from the eave (the eaves are at x = +-1.15, z = 0.9; the ridge x = 0, z = 1.7).
        vec3 R = normalize(vec3(-n.y, n.x, 0.0) + vec3(1e-4, 0.0, 0.0));
        vec3 S = cross(R, n);
        if (S.z < 0.0) S = -S;
        const float rowH = 0.3, tileW = 0.26;
        vec2 uv = vec2(m.y, length(vec2((1.15 - abs(vObj.x)) * vScale.x, (vObj.z - 0.9) * vScale.z)));
        float fade = 1.0 - smoothstep(0.08 * rowH, 0.3 * rowH, px);
        float r = uv.y / rowH, row = floor(r);
        vec2 f = vec2(fract(uv.x / tileW + 0.5 * mod(row, 2.0)), fract(r));
        float tip = 0.1 * pow(abs(2.0 * f.x - 1.0), 2.0);  // rounded tips reach lowest in the middle
        bool under = f.y < tip;                              // below the tip: the row beneath, in its shade
        float rr = under ? row - 1.0 : row;
        float cx = floor(uv.x / tileW + 0.5 * mod(rr, 2.0));
        float h = hash12(vec2(cx, rr));
        float tone = 0.78 + 0.4 * h;
        vec3 tile = base * tone;
        tile = mix(tile, tile * vec3(0.7, 0.85, 0.6), smoothstep(0.88, 0.97, hash12(vec2(cx + 7.0, rr + 3.0))) * 0.7);  // a mossy one
        if (under) tile *= 0.55;
        else tile *= 1.0 + 0.2 * (1.0 - smoothstep(0.0, 0.08, f.y - tip));  // the lit edge of the tip
        tile *= 1.0 - 0.45 * (1.0 - smoothstep(0.0, 0.06, min(f.x, 1.0 - f.x))) * (under ? 0.0 : 1.0);  // the joint between tiles
        tile *= 0.94 + 0.1 * valueNoise2(uv * 4.0 + 3.0);
        albedo = mix(base * 0.9, tile, fade);
        vec3 tilt = S * (0.5 - f.y) * 0.4 + R * (f.x - 0.5) * 0.3;
        n = normalize(n + tilt * fade);
        albedo *= 0.92 + 0.12 * valueNoise2(uv * 0.6 + 11.0);  // weathering in broad patches
        return;
    }
    bool onX = abs(vObj.x) > 0.999;
    float u = onX ? m.y : m.x, v = m.z;
    float W = onX ? vScale.y : vScale.x, H = vScale.z;
    float fade = 1.0 - smoothstep(0.04, 0.25, px);
    if (abs(vObj.x) > 1.001) {
        if (vObj.z >= 0.8) {  // the beam under the eaves
            albedo *= woodGrain(u, v, fade);
            return;
        }
        // The door: planks 14 cm wide, a frame, straps from the hinge side, a ring.
        float halfW = 0.2 * vScale.y, doorH = 0.6 * vScale.z;
        float inset = min(halfW - abs(u), doorH - v);
        float pu = (u + halfW) / 0.14;
        float seam = min(fract(pu), 1.0 - fract(pu)) * 0.14;
        float plank = floor(pu);
        vec3 wood = base * (0.7 + 0.5 * hash12(vec2(plank, 1.3)));
        wood *= 0.78 + 0.34 * valueNoise2(vec2(u * 26.0 + plank * 7.0, v * 2.5));
        wood *= 1.0 - 0.6 * (1.0 - smoothstep(0.005, 0.005 + max(px, 0.008), seam)) * fade;
        wood *= 1.0 + 0.2 * (1.0 - smoothstep(0.012, 0.02, abs(seam - 0.03))) * fade;  // each plank's edge catching the light
        const float frameW = 0.09;
        vec3 framed = base * 1.18 * woodGrain(v, u, fade);
        wood = mix(framed, wood * (0.8 + 0.2 * smoothstep(frameW, frameW + 0.06, inset)), smoothstep(frameW, frameW + max(px, 0.004), inset));
        // Iron: straps at a quarter and three quarters of the height, running two thirds across from the hinges.
        vec3 iron = vec3(0.1, 0.1, 0.11) * (0.8 + 0.4 * valueNoise2(vec2(u * 30.0, v * 30.0)));
        float across = (u + halfW) / (2.0 * halfW);
        float strap = 0.0, rivet = 0.0;
        for (int i = 0; i < 2; ++i) {
            float mid = doorH * (i == 0 ? 0.24 : 0.74);
            float body = 1.0 - smoothstep(0.035 - px, 0.035 + px, abs(v - mid) - 0.0);
            float len = 1.0 - smoothstep(0.7 - px, 0.7 + px, across);
            strap = max(strap, body * len);
            rivet = max(rivet, 1.0 - smoothstep(0.012, 0.012 + px, length(vec2(fract(u / 0.3 + 0.5) - 0.5, (v - mid) / 0.3) * 0.3)));
        }
        vec2 rp = vec2(u - halfW * 0.55, v - doorH * 0.48);
        float ring = 1.0 - smoothstep(0.012, 0.012 + px, abs(length(rp) - 0.05));
        float metal = max(strap * step(frameW, inset), ring);
        albedo = mix(wood, iron, metal * fade);
        albedo = mix(albedo, iron * 2.2, rivet * strap * fade * 0.8);
        albedo = mix(base * 0.85, albedo, fade);
        return;
    }
    // Walls: stone footing, then framed plaster.
    const float foot = 0.45;
    vec3 plaster = base;
    vec3 timber = vec3(0.27, 0.18, 0.11) * tint;
    vec3 wall;
    if (v < foot) {
        wall = coursedStone(vec3(0.62, 0.6, 0.55) * tint, vec2(u, v), vec2(0.5, 0.22), px);
    } else {
        float nb = max(1.0, floor(W / 0.8 + 0.5));            // bays across (2W / 1.6 m)
        float bayW = 2.0 * W / nb;
        float bay = (u + W) / bayW;
        float bi = floor(bay);
        float fb = bay - bi;
        float postD = abs(bay - floor(bay + 0.5)) * bayW;
        float topPlate = H - 0.1;
        float hh = max(topPlate - foot - 0.14, 0.5);
        float fv = clamp((v - foot - 0.14) / hh, 0.0, 1.0);
        float diag = abs(fb - (mod(bi, 2.0) < 0.5 ? fv : 1.0 - fv)) * bayW * hh / length(vec2(bayW, hh));
        float braced = step(0.55, hash12(vec2(bi, 5.1))) * step(v, topPlate) * step(1.2, bayW);
        float sd = min(min(postD - 0.06, abs(v - (foot + 0.07)) - 0.07), min(abs(v - topPlate) - 0.1, braced > 0.5 ? diag - 0.045 : 1.0));
        float tim = 1.0 - smoothstep(-px, px, sd);
        float ao = 0.8 + 0.2 * smoothstep(0.0, 0.1, sd);
        float spot = valueNoise2(vec2(u, v) * 7.0);
        // Lime-wash: each bay its own tone, patches where it has worn through to the lath, rain streaks, and a fine speckle.
        float bayTone = 0.88 + 0.2 * hash12(vec2(bi, floor(v / 1.1) + 9.0));
        float worn = smoothstep(0.58, 0.72, valueNoise2(vec2(u, v) * 1.7 + 21.0)) * 0.3;
        float speck = 0.95 + 0.1 * hash12(floor(vec2(u, v) * 90.0)) * fade;
        vec3 pl = plaster * ao * bayTone * speck * (0.86 + 0.2 * spot) * (0.84 + 0.2 * valueNoise2(vec2(u * 1.3, v * 0.3) + 5.0));
        pl = mix(pl, pl * vec3(0.72, 0.66, 0.56), worn);
        pl *= mix(0.82, 1.0, smoothstep(foot, foot + 0.9, v));                     // splashed at the foot
        pl *= mix(0.9, 1.0, smoothstep(topPlate - 0.7, topPlate, v) * -1.0 + 1.0);  // sooty under the eaves
        bool vertical = postD - 0.06 < min(abs(v - (foot + 0.07)) - 0.07, abs(v - topPlate) - 0.1);
        vec3 tm = timber * (vertical ? woodGrain(v, u, fade) : woodGrain(u, v, fade));
        wall = mix(pl, tm, tim);
        wall = mix(mix(plaster, timber, 0.16) * 0.88, wall, fade);
    }
    albedo = wall;
}
// A flag's face: the owner's colour with a hem, and a cross pattee in white
// (or deep red on a pale field), smoothed over a pixel and blended into the
// field when it shrinks to a few pixels.
vec3 flagCloth(vec3 field, vec2 uv) {
    vec2 p = (uv - 0.5) * vec2(1.54, 1.0);  // the cloth is about 3:2
    float lum = dot(field, vec3(0.3, 0.55, 0.15));
    vec3 device = lum > 0.62 ? vec3(0.62, 0.1, 0.08) : vec3(0.96, 0.94, 0.88);
    const float kArm = 0.36;
    // Each arm narrow at the middle, flaring toward its end.
    vec2 q = abs(p);
    float wx = 0.055 + 0.11 * (q.x / kArm) * (q.x / kArm);
    float wy = 0.055 + 0.11 * (q.y / kArm) * (q.y / kArm);
    float d = min(max(q.y - wx, q.x - kArm), max(q.x - wy, q.y - kArm));
    float px = max(fwidth(p.x), fwidth(p.y));
    float arms = 1.0 - smoothstep(-px, px, d);
    // The hem: a darker band round the edge (not along the pole).
    float edge = min(min(uv.y, 1.0 - uv.y) * 0.66, 1.0 - uv.x);
    float hem = 1.0 - smoothstep(0.03 - px, 0.03 + px, edge);
    vec3 c = mix(field, field * 0.62, hem);
    // Too small to read: the average of field and device instead.
    float tiny = smoothstep(0.04, 0.1, px);
    c = mix(mix(c, device, arms), mix(field, device, 0.2), tiny);
    return c;
}
void main() {
    if (dissolved()) discard;
    vec3 n = normalize(vNormal);
    vec3 albedo = vColor;
    if (vIsCloth > 0.5) {
        // Thin cloth: lit through from either face, its own folds shading it.
        if (dot(n, kSun) < 0.0) n = -n;
        if (!gl_FrontFacing) albedo *= 0.97;
        albedo = flagCloth(albedo, vCloth);
        float sunC = (0.35 + 0.65 * dot(n, kSun)) * mix(1.0, shadowAt(vWorld, n), 0.6) * cloudShade(vWorld.xy);
        vec3 cc = albedo * (0.42 + 0.75 * sunC);
        oColor = vec4(mix(cc, hazeColor(vWorld), fogFactor(distance(uEye, vWorld))), 1.0);
        return;
    }
    if (uHouse > 0.5) houseSurface(albedo, n);
    if (uRock > 0.5) rockSurface(albedo, n);
    if (uMasonry > 0.5 && (vCloth.x < -0.5 || abs(vColor.r - vColor.b) < 0.14)) {  // stone (a cone's slates too, whatever their colour), not timber
        // Courses of dressed blocks, staggered row by row, each its own tone;
        // mortar lines fade out before they shimmer; rain streaks and lichen.
        // Faces looking up (a walk, a landing, a ramp's slope) are paved in
        // courses along the piece's own length, set by world position: one
        // pattern unbroken across every piece laid along a line, and up a
        // slope as across the flat.
        bool top = abs(n.z) > 0.7;
        vec2 uv = top ? vec2(dot(vWorld.xy, vAxis), dot(vWorld.xy, vec2(-vAxis.y, vAxis.x)))
                      : vec2(dot(vWorld.xy, normalize(vec2(-n.y, n.x) + vec2(1e-4, 0.0))), vWorld.z);
        albedo = coursedStone(albedo, uv, vec2(0.95, 0.45), max(fwidth(uv.x), fwidth(uv.y)));
        // Streaks running down the faces; on the paving, wear in patches (not
        // stripes, which would band a ramp across).
        albedo *= 0.9 + 0.14 * valueNoise2(top ? uv * 0.45 : vec2(uv.x * 0.8, uv.y * 0.07));
        float lichen = smoothstep(0.62, 0.8, valueNoise2(uv * 0.35 + 7.0)) * (top ? 0.3 : 0.55);
        albedo = mix(albedo, albedo * vec3(0.78, 0.86, 0.62), lichen);
    }
    oColor = sceneryColor(albedo, n, vWorld);
}
)";

// ---------------------------------------------------------------- models
// Instanced textured meshes made by tools/models (renderer_models.cpp): the
// ruin. Per vertex: position, normal, uv (v down the image). Per instance,
// as the meshes': position, rotation quaternion, scale, tint. Lit as the
// meshes are: sun and its shadows, cloud shadows, the river, haze and fog.
inline const char* kModelVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec3 iPos;
layout(location = 4) in vec4 iQuat;
layout(location = 5) in vec3 iScale;
layout(location = 6) in vec3 iTint;
out vec3 vNormal;
out vec3 vWorld;
out vec2 vUv;
out vec3 vTint;
vec3 rotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
void main() {
    if (iScale.x <= 0.0) { gl_Position = kCulled; return; }
    vec3 world = iPos + rotate(iQuat, aPos * iScale);
    vNormal = rotate(iQuat, aNormal / iScale);
    vUv = aUv;
    vTint = iTint;
    vWorld = world;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kModelFs = R"(
in vec3 vNormal;
in vec3 vWorld;
in vec2 vUv;
in vec3 vTint;
out vec4 oColor;
uniform sampler2D uAlbedo;
void main() {
    vec3 n = normalize(vNormal);
    vec3 albedo = texture(uAlbedo, vUv).rgb * vTint;
    float sun = max(dot(n, kSun), 0.0) * shadowAt(vWorld, n) * cloudShade(vWorld.xy);
    float light = 0.36 + 0.8 * sun + 0.12 * max(-dot(n, kSun), 0.0);
    vec3 c = albedo * light;
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
uniform vec4 uHole;  // the battlefield: the close-up land's cover stops at its edge
int coverGround(vec2 xy) {
    return int(texelFetch(uGround, texCell(floor(xy / uCellSize), uCells - 1.0), 0).r * 16.0 + 0.5);
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
    bool outside = uWrap > 0.5 ? (all(greaterThan(xy, uHole.xy + 1.0)) && all(lessThan(xy, uHole.zw - 1.0))) || h1 >= detailWeight(xy)
                               : xy.x < 1.0 || xy.y < 1.0 || xy.x > size - 1.0 || xy.y > size - 1.0;
    if (fade <= 0.0 || outside) { gl_Position = kCulled; return; }
    int g = coverGround(xy);
    float canopy = texture(uCanopy, xy / size).r;
    vec2 road = texture(uRoad, xy / size).rg;
    float onRoad = road.g > 0.3 && abs((road.r - 0.5) * 16.0) < 3.0 ? 1.0 : 0.0;
    // Same patches the ground shader paints, so tufts avoid the bare soil.
    float soil = smoothstep(0.7, 0.84, valueNoise2(xy * 0.085 + 41.0) * 0.6 + valueNoise2(xy * 0.5 + 5.0) * 0.4);
    float lush = valueNoise2(xy * 0.045 + 3.1) * 0.65 + valueNoise2(xy * 0.13 + 8.7) * 0.35;
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
    float cl = smoothstep(0.55, 0.85, valueNoise2(q) * 0.6 + valueNoise2(q * 2.7) * 0.4) * smoothstep(0.02, 0.25, d.z);
    c = mix(c, vec3(0.97, 0.97, 0.98), cl * 0.55);
    oColor = vec4(c, 1.0);
}
)";

// Depth only, for the sun's shadow map.
inline const char* kDepthFs = R"(
void main() {}
)";
// The same for the scenery's meshes, which dissolve in and out (kMeshFs).
inline const char* kMeshDepthFs = ILLYRIA_SCENERY_FS R"(
void main() {
    if (dissolved()) discard;
}
)";

// ---------------------------------------------------------------- tree boards
// Trees as a few upright cards (renderer_trees.cpp), their picture baked from the tree's own mesh: the one look of a
// tree at every distance. A card is [-1, 1] across and 0 to 1 up in the mesh's unit space, widened to the mesh's extent;
// per vertex: across, up, the card's turn about the tree (radians, below zero: always facing the camera), and
// how far it stands off the tree's axis (in half widths). Per instance, as a mesh's, and iKind: which tree's picture
// (an index into uPictures and uExtents: the picture's place in the atlas, u0 v0 u1 v1; the mesh's half width, top and
// the crowns' sway). Cards too small to see dissolve away (the land's canopy, shaded from the same trees, stands in).
inline const char* kBoardVs = ILLYRIA_SCENERY_VS R"(
layout(location = 0) in vec4 aCard;
layout(location = 7) in float iKind;
uniform vec4 uPictures[16];
uniform vec3 uExtents[16];
out vec2 vUv;
out vec3 vWorld;
out vec3 vRight;
out vec3 vToward;
out vec3 vTint;
void main() {
    int k = int(iKind + 0.5);
    vec4 cell = uPictures[k];
    vec3 board = uExtents[k];
    float lod = sceneryShown(iPos.xy);
    // Seen from the camera, how many pixels tall the tree stands.
    float px = iScale.z / (distance(uEye, iPos) * uPxScale);
    lod *= smoothstep(1.5, 4.0, px);
    if (iScale.x <= 0.0 || lod < 0.02 || outOfView(iPos + vec3(0.0, 0.0, iScale.z * 0.5), max(board.x * iScale.x, iScale.z * 0.5) * 1.2)) { gl_Position = kCulled; return; }
    vLod = lod;
    vec3 right = aCard.z < 0.0 ? normalize(vec3(uCamRight.xy, 0.0)) : rotate(iQuat, vec3(cos(aCard.z), sin(aCard.z), 0.0));
    vec3 toward = vec3(right.y, -right.x, 0.0);  // the side the picture was taken from
    vec3 world = iPos + right * (aCard.x * board.x * iScale.x) + toward * (aCard.w * board.x * iScale.x) + vec3(0.0, 0.0, aCard.y * board.y * iScale.z);
    world.xy += crownSway(aCard.y, board.z);
    vUv = mix(cell.xy, cell.zw, vec2(aCard.x * 0.5 + 0.5, aCard.y));
    vWorld = world;
    vRight = right;
    vToward = toward;
    vTint = iTint;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kBoardFs = ILLYRIA_SCENERY_FS R"(
in vec2 vUv;
in vec3 vWorld;
in vec3 vRight;
in vec3 vToward;
in vec3 vTint;
uniform sampler2D uAlbedo;  // the pictures: colour, and 1 in alpha where the tree is
uniform sampler2D uBoardNormal;  // across, toward the viewer and up, as 0..1
out vec4 oColor;
void main() {
    if (dissolved()) discard;
    vec4 t = texture(uAlbedo, vUv);
    if (t.a < 0.5) discard;
    vec3 nb = texture(uBoardNormal, vUv).xyz * 2.0 - 1.0;
    float side = gl_FrontFacing ? 1.0 : -1.0;  // a card seen from behind is lit as the far side of the tree
    vec3 n = normalize(vRight * nb.x * side + vToward * nb.y * side + vec3(0.0, 0.0, nb.z));
    oColor = sceneryColor(t.rgb * vTint, n, vWorld);
}
)";
// The same for the sun's shadow map.
inline const char* kBoardDepthFs = ILLYRIA_SCENERY_FS R"(
in vec2 vUv;
uniform sampler2D uAlbedo;
void main() {
    if (dissolved() || texture(uAlbedo, vUv).a < 0.5) discard;
}
)";
// The picture of a tree mesh seen side-on (uBakeBox: half width, top, half depth), colour and normal; the
// unlit colour, since the cards are lit as the meshes are.
inline const char* kBoardBakeVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform vec3 uBakeBox;
out vec3 vColor;
out vec3 vNormal;
out vec3 vPos;
void main() {
    vColor = aColor;
    vNormal = aNormal;
    vPos = aPos;
    gl_Position = vec4(aPos.x / uBakeBox.x, aPos.z / uBakeBox.y * 2.0 - 1.0, aPos.y / uBakeBox.z, 1.0);
}
)";
// The bake is where the texture of a tree is laid: bark furrowed along the trunk and boughs, leaves and needles as
// clumps and a speckle, with the clumps' relief in the normal (the picture is a quarter of a metre a texel at
// most, so the finest grain is a few texels and the mipmaps carry it down to a tree's average with distance).
inline const char* kBoardBakeFs = R"(
in vec3 vColor;
in vec3 vNormal;
in vec3 vPos;
layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;
void main() {
    vec3 col = vColor, n = normalize(vNormal);
    vec2 p = vec2(vPos.x + vPos.y * 0.37, vPos.z);  // the picture's own plane, turned a little so no two sides line up
    if (vColor.r > 1.28 * vColor.g) {
        // Bark: furrows running up, broken into plates now and then, ridges lighter than the crevices.
        float f = valueNoise2(vec2(p.x * 120.0, p.y * 9.0)), g = valueNoise2(vec2(p.x * 230.0 + 3.0, p.y * 22.0));
        float ridge = smoothstep(0.25, 0.75, f);
        col *= 0.62 + 0.55 * ridge + 0.18 * (g - 0.5);
        col = mix(col, col * vec3(0.9, 1.0, 0.8), smoothstep(0.7, 0.9, valueNoise2(p * 40.0 + 9.0)) * 0.5);  // moss in patches
        n = normalize(n + vec3((f - 0.5) * 0.7, 0.0, 0.0));
    } else {
        // Leaves: clumps of a few texels, each its own tone and a little off-hue, and a finer speckle of single leaves.
        float clump = valueNoise2(p * 46.0), big = valueNoise2(p * 13.0 + 5.0);
        float leaf = hash12(floor(p * 140.0));
        col *= 0.7 + 0.5 * clump + 0.2 * (big - 0.5);
        col *= 0.9 + 0.2 * leaf;
        col = mix(col, col * vec3(1.18, 1.08, 0.7), smoothstep(0.62, 0.82, valueNoise2(p * 29.0 + 17.0)) * 0.7);  // sunlit and yellowing
        n = normalize(n + vec3(valueNoise2(p * 46.0 + 7.0) - 0.5, valueNoise2(p * 46.0 + 13.0) - 0.5, clump - 0.5) * 0.7);
    }
    oColor = vec4(col, 1.0);
    oNormal = vec4(vec3(n.x, -n.y, n.z) * 0.5 + 0.5, 1.0);
}
)";
#undef ILLYRIA_SCENERY_VS
#undef ILLYRIA_SCENERY_FS

// ---------------------------------------------------------------- water
// The water surface per terrain sample (rivers slope downhill); a mesh of
// the wet cells only.
inline const char* kWaterVs = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aChunk;  // the close-up land: per instance, the chunk's corner (else 0)
uniform sampler2D uWaterTex;  // R32F per sample; very negative where dry
out vec3 vWorld;
float waterTexel(vec2 c) { return texelFetch(uWaterTex, texCell(c, uCells), 0).r; }
void main() {
    // The cell's wet corners decide its surface; dry corners borrow it.
    vec2 pos = aPos + aChunk;
    vec2 c = floor(pos / uCellSize + 0.5);
    float w = waterTexel(c);
    if (w < -1000.0) {
        float best = -1e9;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) best = max(best, waterTexel(c + vec2(dx, dy)));
        // Nowhere near water (the close-up land draws whole chunks): far below, discarded.
        w = best < -1000.0 ? -1.0e4 : best;
    }
    vWorld = vec3(pos, w);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";
inline const char* kWaterFs = R"(
in vec3 vWorld;
uniform sampler2D uFlow;  // RG8: downstream direction per terrain sample (0.5 = still)
out vec4 oColor;
// Ripples drawn along the current: stretched noise drifting downstream.
float current(vec2 along, float t) {
    return valueNoise2(along * vec2(0.35, 1.6) - vec2(t, 0.0)) * 0.55 + valueNoise2(along * vec2(0.9, 3.1) - vec2(t * 1.7, 0.0) + 7.3) * 0.3 +
           valueNoise2(along * vec2(2.3, 6.5) - vec2(t * 2.6, 0.0) + 3.1) * 0.15;
}
void main() {
    vec2 p = vWorld.xy;
    if (uWrap > 0.5 && detailWeight(p) <= hash12(floor(gl_FragCoord.xy)) * 0.92) discard;  // the close-up land's, cross-fading
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
    vec3 bed = mix(vec3(0.46, 0.40, 0.30), vec3(0.36, 0.34, 0.26), valueNoise2(p * 0.7));
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
#define ILLYRIA_FAR_FS R"(
in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;
in vec4 vKinds;
out vec4 oColor;
void main() {
#ifdef DETAIL_CUT
    if (detailWeight(vWorld.xy) > hash12(floor(gl_FragCoord.xy)) * 0.92 + 0.08) discard;  // the close-up land is drawn there
#endif
    vec3 n = normalize(vNormal);
    vec3 albedo = vColor;
    vec2 p = vWorld.xy;
    if (feature(kFeatFarDetail)) albedo = farAlbedo(vColor, vKinds, vWorld, n);
    // Open water: coloured by depth, with swells, chop, surf and glints.
    float glint = 0.0;
    vec3 inland = vec3(-1.0);
    bool wetHere = vKinds.w + farShoreWobble(p) >= 1.0;
    if (feature(kFeatFarDetail) && wetHere && vWorld.z >= 0.8) {
        // Rivers, lakes and pools: the battlefield's water (kWaterFs) over its
        // bed, seen from high up: silt-green shallows to deep blue, calm.
        float dm = clamp(vKinds.w - 1.0, 0.0, 1.0) * 12.0;
        vec3 bed = mix(vec3(0.46, 0.40, 0.30), vec3(0.36, 0.34, 0.26), valueNoise2(p * 0.7)) * 0.8;
        vec3 wc = mix(vec3(0.16, 0.34, 0.32), vec3(0.05, 0.16, 0.26), smoothstep(0.5, 3.0, dm));
        wc = mix(bed, wc, 1.0 - exp(-dm * 1.6));
        inland = mix(bed, wc, clamp(0.55 + dm * 0.3, 0.55, 0.95)) * (0.8 + 0.2 * cloudShade(p));
        n = vec3(0.0, 0.0, 1.0);
        glint = 0.25;
    } else if (feature(kFeatFarDetail) && wetHere) {
        float depth = clamp(vKinds.w - 1.0, 0.0, 1.0);  // 1 = 12 m or more
        float t = uTime;
        albedo = mix(vec3(0.22, 0.50, 0.50), vec3(0.06, 0.19, 0.34), smoothstep(0.0, 0.6, depth));
        vec2 g = vec2(0.8, 0.6) * cos(dot(p, vec2(0.8, 0.6)) * 0.09 - t * 1.1) * 0.10;
        g += vec2(-0.3, 0.95) * cos(dot(p, vec2(-0.3, 0.95)) * 0.16 - t * 1.6) * 0.06;
        g += (vec2(valueNoise2(p / 6.0 + t * 0.35), valueNoise2(p / 6.0 - t * 0.3 + 5.0)) - 0.5) * 0.3;
        n = normalize(vec3(-g, 1.0));
        {
            // The sea: lines of foam roll in over the shallows and break on the sand.
            float shore = 1.0 - smoothstep(0.0, 0.3, depth);
            float roll = sin(depth * 55.0 + t * 1.7 + valueNoise2(p / 45.0) * 7.0);
            float foam = smoothstep(0.55, 0.95, roll) * shore * (0.55 + 0.45 * valueNoise2(p / 3.0 + t * 0.4));
            foam = max(foam, (1.0 - smoothstep(0.0, 0.05, depth)) * (0.55 + 0.45 * valueNoise2(p / 2.0 + t * 0.8)));
            albedo = mix(albedo, vec3(0.93, 0.95, 0.96), foam);
            glint = 1.0 - foam;
        }
    }
    // Sky light falls less into steep ground, so ridges and ravines model even out of the sun.
    float light = 0.18 + 0.14 * n.z + 0.95 * max(dot(n, kSun), 0.0) * cloudShade(vWorld.xy);
    vec3 c = inland.r >= 0.0 ? inland : albedo * light;
    if (glint > 0.0) {
        vec3 h = normalize(kSun + normalize(uEye - vWorld));
        c += vec3(1.0, 0.97, 0.9) * pow(max(dot(n, h), 0.0), 90.0) * 0.6 * glint;
    }
    if (!wetHere) c = landGrids(c, vWorld.xy);  // the grid is the land's; the sea goes unmarked
    oColor = vec4(mix(c, hazeColor(vWorld), fogFactor(distance(uEye, vWorld)) * 0.6), 1.0);
}
)"
inline const char* kFarFs = ILLYRIA_LAND_FS ILLYRIA_FAR_FS;
// The same, giving way where the close-up land is drawn (a separate program:
// discard costs some GPUs their early depth test, so only the land near it pays).
inline const char* kFarCutFs = "#define DETAIL_CUT 1\n" ILLYRIA_LAND_FS ILLYRIA_FAR_FS;
#undef ILLYRIA_FAR_FS
#undef ILLYRIA_LAND_FS

// Rivers and roads drawn as ribbons on the far map, never thinner than a few pixels.
// Per instance: a, b (segment ends), info (half width, kind: 0 river 1 road 2 creek).
inline const char* kRibbonVs = R"(
layout(location = 0) in vec3 iA;
layout(location = 1) in vec3 iB;
layout(location = 2) in vec2 iInfo;
uniform vec4 uHole;  // battlefield (min x, min y, max x, max y): drawn in full detail there
flat out float vKind;
out float vFog;
out vec2 vXy;
void main() {
    vec3 mid = (iA + iB) * 0.5;
    if (mid.x > uHole.x && mid.y > uHole.y && mid.x < uHole.z && mid.y < uHole.w) { gl_Position = kCulled; return; }
    float dist = distance(uEye, mid);
    float px = uPxScale * dist;
    float hw = max(iInfo.x, px * (iInfo.y > 1.5 ? 0.6 : iInfo.y > 0.5 ? 0.9 : 1.6));
    if (abs(iInfo.y - 1.0) < 0.5) hw = max(hw, 3.4);  // a road: its ruts and verges, as on the battlefield
    vec3 d = iB - iA;
    vec3 side = normalize(vec3(-d.y, d.x, 0.0) + vec3(1e-5, 0.0, 0.0)) * hw;
    float t = float(gl_VertexID & 1);
    float s = float((gl_VertexID >> 1) & 1) * 2.0 - 1.0;
    vec3 world = mix(iA, iB, t) + side * s;
    world.z += max(1.5, dist * 0.003);  // ride above the coarse mesh
    vKind = iInfo.y;
    vFog = fogFactor(dist);
    vXy = world.xy;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kRibbonFs = R"(
flat in float vKind;
in float vFog;
in vec2 vXy;
out vec4 oColor;
void main() {
    if (detailWeight(vXy) > hash12(floor(gl_FragCoord.xy)) * 0.92 + 0.08) discard;  // the close-up land draws its own roads and water
    // Water as the battlefield's river looks from high up; roads as its
    // wagon-worn dirt (ruts, grassy hump, verges) does, lit like the land.
    vec3 c = abs(vKind - 1.0) > 0.5 ? vec3(0.09, 0.21, 0.27) : vec3(0.45, 0.39, 0.27) * (0.32 + 0.95 * kSun.z * cloudShade(vXy));
    oColor = vec4(mix(c, kSky, vFog * 0.6), 1.0);
}
)";

// Waterfalls: a sheet of white water down each steep drop of a creek, and a
// churned pool at its foot. Per instance: a (top), b (bottom), info (half
// width, kind: 0 sheet 1 pool, run start, run length along the falls).
inline const char* kFallsVs = R"(
layout(location = 0) in vec3 iA;
layout(location = 1) in vec3 iB;
layout(location = 2) in vec4 iInfo;
out vec2 vUv;
flat out float vKind;
out float vFog;
void main() {
    vec3 mid = (iA + iB) * 0.5;
    float dist = distance(uEye, mid);
    if (dist > 2500.0) { gl_Position = kCulled; return; }
    float t = float(gl_VertexID & 1);
    float s = float((gl_VertexID >> 1) & 1) * 2.0 - 1.0;
    vec3 d = iB - iA;
    vec3 side = normalize(vec3(-d.y, d.x, 0.0) + vec3(1e-5, 0.0, 0.0));
    vec3 world;
    if (iInfo.y < 0.5) {
        world = mix(iA, iB, t) + side * s * iInfo.x * (1.0 + 0.25 * t);
        world.z += 0.35;
        vUv = vec2(s * 0.5 + 0.5, iInfo.z + t * iInfo.w);
    } else {
        // The pool: a flat patch of foam around the foot.
        vec3 f = normalize(vec3(d.xy, 0.0) + vec3(1e-5, 0.0, 0.0));
        world = iB + side * s * iInfo.x * 2.2 + f * (t * 2.0 - 1.0) * iInfo.x * 2.2;
        world.z += 0.15;
        vUv = vec2(s * 0.5 + 0.5, t);
    }
    vKind = iInfo.y;
    vFog = fogFactor(dist);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kFallsFs = R"(
in vec2 vUv;
flat in float vKind;
in float vFog;
out vec4 oColor;
void main() {
    float edge = smoothstep(0.0, 0.18, vUv.x) * smoothstep(1.0, 0.82, vUv.x);
    float a;
    vec3 c;
    if (vKind < 0.5) {
        // Streaks pour down the sheet; it thins to spray at the edges.
        float streak = valueNoise2(vec2(vUv.x * 9.0, vUv.y * 0.6 - uTime * 2.6)) * 0.6 + valueNoise2(vec2(vUv.x * 23.0, vUv.y * 1.7 - uTime * 4.1)) * 0.4;
        c = mix(vec3(0.55, 0.68, 0.74), vec3(0.95, 0.97, 0.98), smoothstep(0.35, 0.75, streak));
        a = edge * (0.55 + 0.4 * streak);
    } else {
        vec2 q = vUv * 2.0 - 1.0;
        float r = length(q);
        float churn = valueNoise2(q * 5.0 + uTime * 0.9) * 0.6 + valueNoise2(q * 11.0 - uTime * 1.4) * 0.4;
        c = vec3(0.92, 0.95, 0.96);
        a = (1.0 - smoothstep(0.4, 1.0, r)) * smoothstep(0.3, 0.7, churn) * 0.8;
    }
    float light = 0.55 + 0.45 * max(kSun.z, 0.0);
    oColor = vec4(mix(c * light, kSky, vFog * 0.6) * a, a);
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

// ---------------------------------------------------------------- health bars
// Per instance: the top of the soldier (3); info: health 0..1, shield pips
// (0..3, the veteran's rank), morale (0 steady, 1 wavering, 2 broken), the
// soldier's height. A small bar of fixed pixel size over his head, gold
// shields above it, an amber frame when wavering, a white flag when broken.
inline const char* kBarVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec4 iInfo;
out vec2 vPx;
flat out vec4 vInfo;
const vec2 kBarPx = vec2(26.0, 13.0);
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    float px = uPxScale * distance(uEye, iPos);
    // Zoomed out, where soldiers are a few pixels tall, there are no bars.
    if (iInfo.w / px < 14.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    // A gate's bar (no rank: pips -1) three times a soldier's width.
    float wide = iInfo.y < -0.5 ? 3.0 : 1.0;
    vec3 world = iPos + vec3(0.0, 0.0, px * 4.0) + uCamRight * ((corner.x - 0.5) * kBarPx.x * wide * px) + vec3(0.0, 0.0, corner.y * kBarPx.y * px);
    vPx = corner * kBarPx;
    vInfo = iInfo;
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";
inline const char* kBarFs = R"(
in vec2 vPx;
flat in vec4 vInfo;
out vec4 oColor;
void main() {
    float hp = vInfo.x, pips = vInfo.y, morale = vInfo.z;
    vec2 p = vPx;
    // Broken: a white flag on a pole, left of the bar.
    if (morale > 1.5 && p.x < 7.0 && p.y >= 4.0) {
        if (p.x >= 1.0 && p.x < 2.0) { oColor = vec4(0.25, 0.17, 0.05, 1.0); return; }
        if (p.x >= 2.0 && p.y > 7.0) { oColor = vec4(0.96, 0.96, 0.92, 1.0); return; }
        discard;
    }
    // The bar: 4 px tall with a dark frame; green when whole, red when nearly dead.
    if (p.y < 4.0) {
        if (p.x < 1.0 || p.x > 25.0 || p.y < 1.0 || p.y > 3.0) {
            oColor = vec4(morale > 0.5 ? vec3(0.95, 0.6, 0.1) : vec3(0.05), 1.0);
            return;
        }
        bool filled = (p.x - 1.0) / 24.0 <= hp;
        vec3 c = mix(vec3(0.9, 0.15, 0.1), vec3(0.25, 0.85, 0.2), smoothstep(0.25, 0.75, hp));
        oColor = vec4(filled ? c : vec3(0.2, 0.16, 0.14), 1.0);
        return;
    }
    // Shield pips, 6 px wide with 2 px between, centred above the bar.
    float n = pips;
    if (n < 0.5) discard;
    float left = 13.0 - (n * 6.0 + (n - 1.0) * 2.0) * 0.5;
    float k = floor((p.x - left) / 8.0);
    if (k < 0.0 || k > n - 1.0) discard;
    vec2 q = vec2(p.x - left - k * 8.0 - 3.0, p.y - 5.0);  // x -3..3, y 0..8 from the shield's point
    if (abs(q.x) > 3.0 || q.y > 8.0) discard;
    // Flat top, pointed bottom.
    float hw = q.y > 4.0 ? 3.0 : 3.0 * q.y / 4.0;
    if (abs(q.x) > hw) discard;
    bool edge = abs(q.x) > hw - 1.0 || q.y > 7.0;
    oColor = vec4(edge ? vec3(0.25, 0.17, 0.05) : vec3(0.98, 0.8, 0.28), 1.0);
}
)";

// The campaign's soldiers from high up (renderer_map.cpp): a round dot in his
// side's colour, a few pixels wide whatever the distance. Drawn twice, so that
// a crowd has one dark outline and not one a man: first every dot wider by
// uRim (dark; pale for the selected stack's), then the colours.
inline const char* kDotVs = R"(
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec4 iColour;  // colour, and 1 if selected
layout(location = 2) in vec2 iFight;   // how hard he is flashing in a fight (0: not), and how solid he is
uniform vec2 uViewport;  // px
uniform float uDotPx;
uniform float uRim;  // px of outline all round (0: the colours)
out vec2 vCorner;
flat out vec4 vColour;
flat out vec2 vFight;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    // A man in a fight swells and shrinks with his flash.
    float size = uDotPx * (1.0 + 0.5 * min(iColour.a, 1.0)) * (1.0 + 0.7 * iFight.x) + 2.0 * uRim;
    // Drawn a little nearer the eye along its own line of sight (where it appears is not
    // changed): so that bumps of the ground the coarse land does not have do not bury it.
    gl_Position = uViewProj * vec4(iPos + (uEye - iPos) * 0.02, 1.0);
    gl_Position.xy += (corner - 0.5) * 2.0 * size / uViewport * gl_Position.w;
    vCorner = corner * 2.0 - 1.0;
    vColour = iColour;
    vFight = iFight;
}
)";
inline const char* kDotFs = R"(
in vec2 vCorner;
flat in vec4 vColour;
flat in vec2 vFight;
uniform float uRim;
uniform float uDotPx;
out vec4 oColor;
void main() {
    float r = length(vCorner);
    if (r > 1.0) discard;
    // A town's own garrison (no lord's): hollow, a ring, so that it is not mistaken for a lord's men.
    bool hollow = vFight.y < 0.0;
    float alpha = abs(vFight.y);
    if (hollow && uRim <= 0.0 && r < 0.5) discard;
    // In a fight the colour flares toward white-hot with the flash, and its rim goes hot too.
    vec3 flare = mix(vColour.rgb, vec3(1.0, 0.95, 0.75), 0.85 * vFight.x);
    // Two rims, so that a dot stands out on any ground and any colour reads against its own ring: the ring next to the
    // colour is the dark or the light one, whichever the colour is not (a black side's is light, a white side's dark);
    // the outer edge is the other of the two (dark against snow and pale stone, light against forest and shadow).
    float luma = dot(flare, vec3(0.2126, 0.7152, 0.0722));
    const vec3 kDark = vec3(0.02, 0.02, 0.03), kLight = vec3(0.97, 0.96, 0.9);
    bool outer = uRim > uDotPx * 0.3;
    bool darkInner = luma > 0.45 && vColour.a < 0.5;
    vec3 inner = vColour.a > 1.5 ? vec3(0.62, 0.62, 0.62) : vColour.a > 0.5 ? vec3(0.75, 0.92, 1.0) : darkInner ? kDark : kLight;
    bool innerLight = !darkInner;
    vec3 outerRim = innerLight ? mix(kDark, vec3(0.55, 0.12, 0.04), vFight.x) : kLight;
    oColor = vec4(uRim > 0.0 ? (outer ? outerRim : inner) : flare, alpha);
}
)";

}  // namespace illyria::game::shaders
