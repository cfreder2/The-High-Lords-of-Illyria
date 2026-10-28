#include "renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "illyria/game/factions.h"
#include "shaders.h"

namespace illyria::game {

namespace {

using sim::UnitType;

// How each unit type is drawn by the procedural sprite shader.
struct Look {
    float plan;    // 0 humanoid, 1 horse+rider, 2 wolf+rider, 3 unicorn, 4 pegasus+rider,
                   // 5 elephant, 6 spider, 7 griffin, 8 dragon
    float weapon;  // 0 none, 1 sword, 2 axe, 3 pike, 4 bow, 5 staff, 6 sickle, 7 satchel,
                   // 8 club, 9 trident, 10 greatsword, 11 lance
    float head;    // 0 bare, 1 helmet, 2 hood, 3 bull, 4 wizard hat, 5 skull, 6 halo,
                   // 7 horns, 8 dwarf helm and beard, 9 crown
    float shield;  // 0 none, 1 round, 2 large
    float flags;   // 1 robe, 2 wings, 4 bulky, 8 barding, 16 glowing eyes, 32 tail, 64 bare chest
    float body[3];
    float width;   // sprite width / height
};

Look lookOf(UnitType t) {
    switch (t) {
        case UnitType::LightInfantry: return {0, 1, 2, 1, 0, {0.86f, 0.68f, 0.55f}, 0.7f};
        case UnitType::HeavyInfantry: return {0, 1, 1, 2, 0, {0.86f, 0.68f, 0.55f}, 0.7f};
        case UnitType::Archers: return {0, 4, 2, 0, 0, {0.86f, 0.68f, 0.55f}, 0.7f};
        case UnitType::Pikemen: return {0, 3, 1, 0, 0, {0.86f, 0.68f, 0.55f}, 0.75f};
        case UnitType::LightCavalry: return {1, 1, 1, 0, 0, {0.45f, 0.3f, 0.18f}, 1.35f};
        case UnitType::HeavyCavalry: return {1, 11, 1, 1, 8, {0.3f, 0.28f, 0.28f}, 1.45f};
        case UnitType::Dwarves: return {0, 2, 8, 1, 4, {0.86f, 0.68f, 0.55f}, 0.85f};
        case UnitType::WolfRiders: return {2, 1, 0, 0, 0, {0.5f, 0.5f, 0.5f}, 1.3f};
        case UnitType::Giants: return {0, 8, 0, 0, 4, {0.78f, 0.62f, 0.48f}, 0.8f};
        case UnitType::Minotaurs: return {0, 2, 3, 0, 4, {0.42f, 0.28f, 0.18f}, 0.85f};
        case UnitType::Spiders: return {6, 0, 0, 0, 0, {0.12f, 0.1f, 0.12f}, 1.7f};
        case UnitType::Griffins: return {7, 0, 0, 0, 0, {0.72f, 0.58f, 0.35f}, 1.6f};
        case UnitType::Pegasi: return {4, 1, 1, 0, 0, {0.95f, 0.95f, 0.97f}, 1.45f};
        case UnitType::Elephants: return {5, 0, 0, 0, 0, {0.52f, 0.5f, 0.5f}, 1.25f};
        case UnitType::Unicorns: return {3, 0, 0, 0, 0, {0.97f, 0.96f, 1.0f}, 1.35f};
        case UnitType::Wizards: return {0, 5, 4, 0, 1, {0.86f, 0.68f, 0.55f}, 0.7f};
        case UnitType::Dragons: return {8, 0, 0, 0, 0, {0.32f, 0.4f, 0.16f}, 1.9f};
        case UnitType::Devils: return {0, 9, 7, 0, 4 | 32, {0.62f, 0.1f, 0.08f}, 0.85f};
        case UnitType::Demons: return {0, 10, 7, 0, 4 | 2 | 32, {0.32f, 0.1f, 0.26f}, 1.3f};
        case UnitType::Undead: return {0, 1, 5, 1, 16, {0.86f, 0.83f, 0.72f}, 0.7f};
        case UnitType::Archons: return {0, 1, 6, 0, 1 | 2, {0.95f, 0.85f, 0.75f}, 1.2f};
        case UnitType::Hero: return {0, 1, 9, 2, 0, {0.86f, 0.68f, 0.55f}, 0.75f};
        case UnitType::DwarfGrenadier: return {0, 7, 8, 0, 4, {0.86f, 0.68f, 0.55f}, 0.85f};
        case UnitType::Journeyman: return {0, 6, 2, 0, 1, {0.86f, 0.68f, 0.55f}, 0.75f};
        case UnitType::Warrior: return {0, 1, 1, 2, 0, {0.86f, 0.68f, 0.55f}, 0.75f};
        case UnitType::Berserker: return {0, 10, 0, 0, 64, {0.86f, 0.68f, 0.55f}, 0.8f};
        case UnitType::Catapult: return {9, 0, 0, 0, 0, {0.43f, 0.29f, 0.16f}, 1.6f};
        case UnitType::Count: break;
    }
    return {};
}

GLuint compile(GLenum type, const char* body) {
    const char* sources[] = {shaders::kCommon, body};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, sources, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
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
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::printf("program link failed: %s\n", log);
        return false;
    }
    out = p;
    return true;
}

// Instance sizes in floats (sprites: 20 = pos 3, colour 3, info 4, style 4,
// body 3, size 2, selected 1; particles: 7 = pos 3, info 4).
constexpr int kDecalFloats = 11;  // centre 3, params 4, extra 4
constexpr int kArrowFloats = 8;   // pos 3, dir 3, info 2

constexpr float kDecalBlood = 0, kDecalShadow = 1, kDecalRing = 2, kDecalCrater = 4, kDecalDust = 5;
constexpr float kPlanBomb = 9, kPlanOrb = 10;

// Instance record for a mesh: position, quaternion, scale, tint.
void pushInstance(std::vector<float>& out, const V3& pos, const float q[4], const V3& scale, const V3& tint) {
    out.insert(out.end(), {pos.x, pos.y, pos.z, q[0], q[1], q[2], q[3], scale.x, scale.y, scale.z, tint.x, tint.y, tint.z});
}
void yawQuat(float yaw, float q[4]) {
    q[0] = 0;
    q[1] = 0;
    q[2] = std::sin(yaw / 2);
    q[3] = std::cos(yaw / 2);
}
// Rotation about z by yaw, then about the (rotated) x axis by roll and y axis by pitch.
void yprQuat(float yaw, float pitch, float roll, float q[4]) {
    const float cy = std::cos(yaw / 2), sy = std::sin(yaw / 2), cp = std::cos(pitch / 2), sp = std::sin(pitch / 2),
                cr = std::cos(roll / 2), sr = std::sin(roll / 2);
    q[0] = sr * cp * cy - cr * sp * sy;
    q[1] = cr * sp * cy + sr * cp * sy;
    q[2] = cr * cp * sy - sr * sp * cy;
    q[3] = cr * cp * cy + sr * sp * sy;
}
float hash01(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return static_cast<float>(v & 0xFFFF) / 65535.0f;
}


const V3 kSkinColor{0.86f, 0.68f, 0.55f};
const V3 kBoneColor{0.86f, 0.83f, 0.74f};
const V3 kLegColor{0.26f, 0.2f, 0.15f};

// A loose body part as a 3D object: heads are spheres, limbs and bones capsules.
void pushPart(std::vector<float>& capsules, std::vector<float>& spheres, sim::RemainKind kind, const V3& pos,
              const float q[4], const V3& team, float decay) {
    const V3 skin = kSkinColor * (1 - decay) + kBoneColor * decay;
    switch (kind) {
        case sim::RemainKind::Head:
            pushInstance(spheres, pos + V3{0, 0, 0.11f}, q, {0.23f, 0.23f, 0.23f}, skin);
            break;
        case sim::RemainKind::Arm:
            pushInstance(capsules, pos + V3{0, 0, 0.06f}, q, {0.55f, 0.12f, 0.12f}, team * (1 - decay) + kBoneColor * decay);
            break;
        case sim::RemainKind::Leg:
            pushInstance(capsules, pos + V3{0, 0, 0.07f}, q, {0.65f, 0.14f, 0.14f}, kLegColor * (1 - decay) + kBoneColor * decay);
            break;
        case sim::RemainKind::Bone:
            pushInstance(capsules, pos + V3{0, 0, 0.04f}, q, {0.5f, 0.07f, 0.07f}, kBoneColor);
            break;
        case sim::RemainKind::Torso:
            // The trunk in its tunic, torn and bloody.
            pushInstance(capsules, pos + V3{0, 0, 0.14f}, q, {0.62f, 0.3f, 0.26f}, team * (0.75f * (1 - decay)) + V3{0.3f, 0.04f, 0.03f} * (1 - decay) + kBoneColor * decay);
            break;
        case sim::RemainKind::Chunk:
            pushInstance(spheres, pos + V3{0, 0, 0.05f}, q, {0.17f, 0.13f, 0.11f}, V3{0.42f, 0.06f, 0.05f} * (1 - decay) + kBoneColor * decay * 0.8f);
            break;
        default:
            break;
    }
}

// A dead soldier lying where he fell: torso, head and limbs, scaled to size.
void pushCorpse(std::vector<float>& capsules, std::vector<float>& spheres, const V3& pos, float yaw, float scale,
                const V3& team, const V3& body, bool humanoid, float decay, uint32_t seed) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const auto at = [&](float x, float y, float z) { return pos + V3{c * x - s * y, s * x + c * y, z} * scale; };
    const V3 cloth = team * (1 - decay) + V3{0.25f, 0.22f, 0.18f} * decay;
    const V3 flesh = (humanoid ? kSkinColor : body) * (1 - decay) + kBoneColor * decay;
    float q[4];
    if (!humanoid) {
        yprQuat(yaw, 0, 1.4f, q);
        pushInstance(capsules, at(0, 0, 0.3f), q, V3{1.3f, 0.55f, 0.5f} * scale, flesh);
        pushInstance(spheres, at(0.75f, 0.1f, 0.2f), q, V3{0.4f, 0.4f, 0.4f} * scale, flesh);
        return;
    }
    const float twist = (hash01(seed) - 0.5f) * 0.8f;
    yprQuat(yaw, 0, 0, q);
    pushInstance(capsules, at(0, 0, 0.16f), q, V3{0.62f, 0.34f, 0.24f} * scale, cloth);
    pushInstance(spheres, at(0.45f, 0, 0.12f), q, V3{0.24f, 0.24f, 0.24f} * scale, flesh);
    yprQuat(yaw + 0.12f, 0, 0, q);
    pushInstance(capsules, at(-0.58f, 0.1f, 0.08f), q, V3{0.62f, 0.15f, 0.15f} * scale, kLegColor);
    yprQuat(yaw - 0.15f + twist * 0.3f, 0, 0, q);
    pushInstance(capsules, at(-0.58f, -0.12f, 0.08f), q, V3{0.62f, 0.15f, 0.15f} * scale, kLegColor);
    yprQuat(yaw + 0.6f + twist, 0, 0, q);
    pushInstance(capsules, at(0.15f, 0.3f, 0.07f), q, V3{0.5f, 0.12f, 0.12f} * scale, cloth * 0.9f);
    yprQuat(yaw - 0.5f - twist, 0, 0, q);
    pushInstance(capsules, at(0.15f, -0.3f, 0.07f), q, V3{0.5f, 0.12f, 0.12f} * scale, cloth * 0.9f);
}

// A soldier's health bar, with a gold shield for each rank he has earned
// (sim::veteranGrade) and a white flag when he has broken: shown when he is
// hurt, a veteran, shaken or selected.
void pushHealthBar(std::vector<float>& bars, const sim::Unit& u, const V3& pos, bool selected) {
    const int pips = sim::veteranGrade(u);
    const float morale = u.routed ? 2.0f : u.wavering() ? 1.0f : 0.0f;
    if (u.hp >= u.stats.maxHp && pips == 0 && !selected && morale == 0.0f) return;
    const float height = static_cast<float>(u.stats.height.toDouble());
    const float hp = static_cast<float>(u.hp) / static_cast<float>(std::max(1, u.stats.maxHp));
    bars.insert(bars.end(), {pos.x, pos.y, pos.z + height, hp, static_cast<float>(pips), morale, height});
}

const float kPlayerColor[3] = {0.22f, 0.42f, 0.92f};
const float kEnemyColor[3] = {0.86f, 0.18f, 0.14f};

}  // namespace

bool Renderer::buildPrograms() {
    struct Spec {
        Program& p;
        const char* vs;
        const char* fs;
    } specs[] = {{terrain_, shaders::kTerrainVs, shaders::kTerrainFs},
                 {decal_, shaders::kDecalVs, shaders::kDecalFs},
                 {arrow_, shaders::kArrowVs, shaders::kArrowFs},
                 {sprite_, shaders::kSpriteVs, shaders::kSpriteFs},
                 {particle_, shaders::kParticleVs, shaders::kParticleFs},
                 {mesh_, shaders::kMeshVs, shaders::kMeshFs},
                 {water_, shaders::kWaterVs, shaders::kWaterFs},
                 {far_, shaders::kFarVs, shaders::kFarFs},
                 {ribbon_, shaders::kRibbonVs, shaders::kRibbonFs},
                 {falls_, shaders::kFallsVs, shaders::kFallsFs},
                 {cover_, shaders::kCoverVs, shaders::kCoverFs},
                 {meshDepth_, shaders::kMeshVs, shaders::kDepthFs},
                 {terrainDepth_, shaders::kTerrainVs, shaders::kDepthFs},
                 {sky_, shaders::kSkyVs, shaders::kSkyFs},
                 {figure_, shaders::kFigureVs, shaders::kFigureFs},
                 {resolve_, shaders::kResolveVs, shaders::kResolveFs},
                 {unit_, shaders::kUnitVs, shaders::kUnitFs},
                 {figureDepth_, shaders::kFigureDepthVs, shaders::kDepthFs},
                 {sheetView_, shaders::kSheetViewVs, shaders::kSheetViewFs},
                 {detailTerrain_, shaders::kPatchVs, shaders::kPatchFs},
                 {detailDepth_, shaders::kPatchVs, shaders::kDepthFs},
                 {farCut_, shaders::kFarVs, shaders::kFarCutFs},
                 {bar_, shaders::kBarVs, shaders::kBarFs}};
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
        s.p.water = glGetUniformLocation(s.p.id, "uWater");
        s.p.sway = glGetUniformLocation(s.p.id, "uSway");
        s.p.ground = glGetUniformLocation(s.p.id, "uGround");
        s.p.road = glGetUniformLocation(s.p.id, "uRoad");
        s.p.waterTex = glGetUniformLocation(s.p.id, "uWaterTex");
        s.p.hole = glGetUniformLocation(s.p.id, "uHole");
        s.p.features = glGetUniformLocation(s.p.id, "uFeatures");
        s.p.shadowMap = glGetUniformLocation(s.p.id, "uShadowMap");
        s.p.lightViewProj = glGetUniformLocation(s.p.id, "uLightViewProj");
        s.p.shadowInfo = glGetUniformLocation(s.p.id, "uShadowInfo");
        s.p.focus = glGetUniformLocation(s.p.id, "uFocus");
        s.p.canopy = glGetUniformLocation(s.p.id, "uCanopy");
        s.p.cover = glGetUniformLocation(s.p.id, "uCover");
        s.p.ring = glGetUniformLocation(s.p.id, "uRing");
        s.p.sink = glGetUniformLocation(s.p.id, "uSink");
        s.p.patch = glGetUniformLocation(s.p.id, "uPatch");
        s.p.patchSink = glGetUniformLocation(s.p.id, "uPatchSink");
        s.p.masonry = glGetUniformLocation(s.p.id, "uMasonry");
        s.p.camFwd = glGetUniformLocation(s.p.id, "uCamFwd");
        s.p.camUp = glGetUniformLocation(s.p.id, "uCamUp");
        s.p.proj = glGetUniformLocation(s.p.id, "uProj");
        s.p.flow = glGetUniformLocation(s.p.id, "uFlow");
        s.p.bakeRight = glGetUniformLocation(s.p.id, "uBakeRight");
        s.p.bakeUp = glGetUniformLocation(s.p.id, "uBakeUp");
        s.p.bakeToward = glGetUniformLocation(s.p.id, "uBakeToward");
        s.p.bakeRect = glGetUniformLocation(s.p.id, "uBakeRect");
        s.p.scratch = glGetUniformLocation(s.p.id, "uScratch");
        s.p.resolve = glGetUniformLocation(s.p.id, "uResolve");
        s.p.cellTable = glGetUniformLocation(s.p.id, "uCellTable");
        s.p.lightDepth = glGetUniformLocation(s.p.id, "uLightDepth");
        s.p.lightRight = glGetUniformLocation(s.p.id, "uLightRight");
        s.p.lightUp = glGetUniformLocation(s.p.id, "uLightUp");
        s.p.lightToward = glGetUniformLocation(s.p.id, "uLightToward");
        s.p.lightRect = glGetUniformLocation(s.p.id, "uLightRect");
        s.p.lightRange = glGetUniformLocation(s.p.id, "uLightRange");
        s.p.sheetView = glGetUniformLocation(s.p.id, "uSheetView");
        s.p.cloth = glGetUniformLocation(s.p.id, "uCloth");
        s.p.sheetSize = glGetUniformLocation(s.p.id, "uSheetSize");
        s.p.viewDir = glGetUniformLocation(s.p.id, "uViewDir");
        s.p.lift = glGetUniformLocation(s.p.id, "uLift");
        s.p.team = glGetUniformLocation(s.p.id, "uTeam");
        s.p.sheet = glGetUniformLocation(s.p.id, "uSheet");
        s.p.wrap = glGetUniformLocation(s.p.id, "uWrap");
        s.p.detailMask = glGetUniformLocation(s.p.id, "uDetailMask");
        s.p.detailRect = glGetUniformLocation(s.p.id, "uDetailRect");
        s.p.detailFade = glGetUniformLocation(s.p.id, "uDetailFade");
        s.p.lod = glGetUniformLocation(s.p.id, "uLod");
    }
    return true;
}

void Renderer::makeBatch(Batch& b, std::initializer_list<int> layout, size_t capacity) {
    int floats = 0;
    for (int s : layout) floats += s;
    b.floats = floats;
    b.capacity = capacity;
    glGenVertexArrays(1, &b.vao);
    glGenBuffers(1, &b.vbo);
    glBindVertexArray(b.vao);
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max<size_t>(capacity, 1) * floats * 4), nullptr,
                 GL_DYNAMIC_DRAW);
    int offset = 0, loc = 0;
    for (int s : layout) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, s, GL_FLOAT, GL_FALSE, floats * 4,
                              reinterpret_cast<void*>(static_cast<intptr_t>(offset * 4)));
        glVertexAttribDivisor(loc, 1);
        offset += s;
        ++loc;
    }
    glBindVertexArray(0);
}

void Renderer::zeroBatch(Batch& b, size_t count) {
    const std::vector<float> zeros(count * b.floats, 0.0f);
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(zeros.size() * 4), zeros.data());
}

void Renderer::uploadDynamic(Batch& b, const std::vector<float>& data) {
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    const size_t count = data.size() / b.floats;
    if (count > b.capacity) {
        b.capacity = count * 2;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(b.capacity * b.floats * 4), nullptr, GL_DYNAMIC_DRAW);
    }
    if (!data.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(data.size() * 4), data.data());
    b.count = static_cast<GLsizei>(count);
}

void Renderer::makeMeshBatch(MeshBatch& b, const Mesh& mesh, float sway) {
    if (b.vao) {
        // Rebuilt (a new battlefield): free the old buffers.
        glDeleteVertexArrays(1, &b.vao);
        glDeleteBuffers(1, &b.vertexVbo);
        glDeleteBuffers(1, &b.instanceVbo);
    }
    b.sway = sway;
    b.vertices = mesh.vertexCount();
    glGenVertexArrays(1, &b.vao);
    glGenBuffers(1, &b.vertexVbo);
    glGenBuffers(1, &b.instanceVbo);
    glBindVertexArray(b.vao);
    glBindBuffer(GL_ARRAY_BUFFER, b.vertexVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.data.size() * 4), mesh.data.data(), GL_STATIC_DRAW);
    for (int i = 0; i < 3; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(static_cast<intptr_t>(i * 12)));
    }
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, 13 * 4, nullptr, GL_DYNAMIC_DRAW);
    b.capacity = 1;
    const int sizes[] = {3, 4, 3, 3};
    int offset = 0;
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, sizes[i], GL_FLOAT, GL_FALSE, 13 * 4, reinterpret_cast<void*>(static_cast<intptr_t>(offset * 4)));
        glVertexAttribDivisor(3 + i, 1);
        offset += sizes[i];
    }
    glBindVertexArray(0);
}

void Renderer::uploadMeshInstances(MeshBatch& b, const std::vector<float>& data) {
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceVbo);
    const size_t count = data.size() / 13;
    if (count > b.capacity) {
        b.capacity = count + count / 2 + 16;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(b.capacity * 13 * 4), nullptr, GL_DYNAMIC_DRAW);
    }
    if (!data.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(data.size() * 4), data.data());
    b.count = static_cast<GLsizei>(count);
}

std::vector<Renderer::MeshBatch*> Renderer::scenery(bool includeSmall) {
    std::vector<MeshBatch*> out{&houses_, &pillars_, &capsules_, &spheres_};
    for (auto* list : {&pines_, &oaks_, &detailPines_, &detailOaks_}) for (MeshBatch& b : *list) out.push_back(&b);
    if (includeSmall) {
        for (auto* list : {&bushes_, &rocks_, &detailBushes_, &detailRocks_}) for (MeshBatch& b : *list) out.push_back(&b);
        for (MeshBatch* b : {&logs_, &stumps_, &detailLogs_, &detailStumps_}) out.push_back(b);
    }
    return out;
}

void Renderer::drawMeshes(const std::vector<MeshBatch*>& batches, const Camera& camera, double timeSeconds,
                          float gameSeconds) {
    setCommon(mesh_, camera, timeSeconds, gameSeconds);
    for (MeshBatch* b : batches) {
        if (b->count == 0) continue;
        glUniform1f(mesh_.sway, b->sway);
        glUniform1f(mesh_.masonry, b->masonry ? 1.0f : 0.0f);
        glUniform1f(mesh_.lod, b->lod);
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, b->vertices, b->count);
    }
}


namespace {
// A stable pick from weights (they need not sum to one).
size_t pick(float u, std::initializer_list<float> weights) {
    float total = 0;
    for (float w : weights) total += w;
    u *= total;
    size_t k = 0;
    for (float w : weights) {
        if (u < w) return k;
        u -= w;
        ++k;
    }
    return weights.size() - 1;
}
}  // namespace

uint32_t Renderer::sceneryId(const Battle& battle, const sim::Obstacle& o) {
    // Its look comes from where it stands on the continent, so a tree looks the
    // same drawn on a battlefield or on the close-up land around it.
    const V3 at = battle.origin() + toV3(o.pos);
    return static_cast<uint32_t>(std::lround(at.x * 8.0f)) * 73856093u ^ static_cast<uint32_t>(std::lround(at.y * 8.0f)) * 19349663u;
}

float Renderer::pushScenery(const sim::Obstacle& o, uint32_t id, SceneryLists& out) {
    const V3 pos = toV3(o.pos);
    const float r = static_cast<float>(o.radius.toDouble()), h = static_cast<float>(o.height.toDouble());
    const float yaw = hash01(id * 2654435761u) * 6.283f;
    const float tintVar = 0.85f + 0.3f * hash01(id * 40503u + 7);
    float q[4];
    yawQuat(yaw, q);
    switch (o.kind) {
        case sim::Obstacle::Kind::Pine: {
            // Spruce, tall fir, young pine, dead snag.
            const size_t v = pick(hash01(id * 2246822519u + 1), {0.42f, 0.28f, 0.22f, 0.08f});
            pushInstance(out.pine[v], pos, q, {h * 0.32f, h * 0.32f, h}, {tintVar, tintVar, tintVar});
            return h * 0.3f;
        }
        case sim::Obstacle::Kind::Oak: {
            // Oak, broad oak, elm, autumn oak, birch, autumn birch, dead oak.
            const size_t v = pick(hash01(id * 2246822519u + 2), {0.26f, 0.17f, 0.14f, 0.1f, 0.17f, 0.08f, 0.08f});
            pushInstance(out.oak[v], pos, q, {h * 0.55f, h * 0.55f, h}, {tintVar, tintVar * 1.05f, tintVar * 0.9f});
            return h * 0.45f;
        }
        case sim::Obstacle::Kind::Bush: {
            // Reeds (the simulation's variant 3) or round, berry and thorn bushes.
            const size_t v = o.variant == 3 ? 3 : pick(hash01(id * 2246822519u + 3), {0.5f, 0.2f, 0.3f});
            const float reedH = v == 3 ? h * 1.2f : h;
            pushInstance(out.bush[v], pos, q, {r * 0.8f, r * 0.8f, reedH}, {tintVar, tintVar, tintVar});
            return r * 0.8f;
        }
        case sim::Obstacle::Kind::Rock:
        case sim::Obstacle::Kind::Boulder: {
            const size_t v = pick(hash01(id * 2246822519u + 4), {0.4f, 0.2f, 0.2f, 0.2f});
            pushInstance(out.rock[v], pos - V3{0, 0, h * 0.15f}, q, {r, r * 0.9f, h}, {tintVar, tintVar, tintVar * 0.95f});
            return r * 1.1f;
        }
        case sim::Obstacle::Kind::Log:
            // Radius is half the length, height the girth; sunk a little into the turf.
            if (hash01(id * 2246822519u + 5) < 0.3f) {
                const float girth = h * 1.6f;  // a stump with its fallen top beside it
                pushInstance(out.stumps, pos, q, {girth, girth, girth}, {tintVar, tintVar, tintVar});
            } else {
                pushInstance(out.logs, pos - V3{0, 0, h * 0.15f}, q, {r, h / 2, h / 2}, {tintVar, tintVar, tintVar});
            }
            return 0;
        default: return 0;
    }
}

void Renderer::uploadScenery(const Battle& battle) {
    const sim::Region& region = battle.region();
    std::vector<float> house, pillar, shadows;
    SceneryLists natural;
    natural.pine.resize(pines_.size());
    natural.oak.resize(oaks_.size());
    natural.bush.resize(bushes_.size());
    natural.rock.resize(rocks_.size());
    for (size_t i = 0; i < region.obstacles().size(); ++i) {
        const sim::Obstacle& o = region.obstacles()[i];
        const V3 pos = toV3(o.pos);
        const float h = static_cast<float>(o.height.toDouble());
        const float tintVar = 0.85f + 0.3f * hash01(static_cast<uint32_t>(i) * 40503u + 7);
        float shadow = pushScenery(o, sceneryId(battle, o), natural);
        switch (o.kind) {
            case sim::Obstacle::Kind::Tower: break;  // drawn with its castle
            case sim::Obstacle::Kind::House: {
                if (o.variant == sim::Obstacle::kTownHouse) break;  // drawn with its castle
                float aligned[4];
                yawQuat(0, aligned);
                const float hx = static_cast<float>(o.halfX.toDouble()), hy = static_cast<float>(o.halfY.toDouble());
                if (o.variant == 3) {
                    // The temple altar: a low stone block.
                    pushInstance(pillar, pos, aligned, {hx * 0.9f, hy * 0.9f, h}, {1.0f, 0.98f, 0.95f});
                } else {
                    pushInstance(house, pos, aligned, {hx, hy, h / 1.7f}, {tintVar, tintVar * 0.97f, tintVar * 0.92f});
                }
                shadow = std::max(hx, hy) * 1.3f;
                break;
            }
            case sim::Obstacle::Kind::Pillar: {
                const float r = static_cast<float>(o.radius.toDouble());
                float q[4];
                yawQuat(hash01(static_cast<uint32_t>(i) * 2654435761u) * 6.283f, q);
                pushInstance(pillar, pos, q, {r, r, h}, {1, 1, 1});
                shadow = r * 1.5f;
                break;
            }
            default: break;  // trees, undergrowth, rocks and logs: pushScenery
        }
        shadows.insert(shadows.end(), {pos.x, pos.y, pos.z, 0, shadow, 0, 1, 0, 0, 0, 0});
    }
    for (size_t v = 0; v < pines_.size(); ++v) uploadMeshInstances(pines_[v], natural.pine[v]);
    for (size_t v = 0; v < oaks_.size(); ++v) uploadMeshInstances(oaks_[v], natural.oak[v]);
    for (size_t v = 0; v < bushes_.size(); ++v) uploadMeshInstances(bushes_[v], natural.bush[v]);
    for (size_t v = 0; v < rocks_.size(); ++v) uploadMeshInstances(rocks_[v], natural.rock[v]);
    uploadMeshInstances(stumps_, natural.stumps);
    uploadMeshInstances(houses_, house);
    uploadMeshInstances(pillars_, pillar);
    uploadMeshInstances(logs_, natural.logs);
    uploadDynamic(sceneryShadows_, shadows);

    // Ground types, one texel per cell.
    const int n = Battle::kTerrainCells;
    std::vector<uint8_t> ground(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) ground[static_cast<size_t>(y) * n + x] = static_cast<uint8_t>(static_cast<int>(region.cellGround(x, y)) * 16);
    if (!groundTex_) glGenTextures(1, &groundTex_);
    glBindTexture(GL_TEXTURE_2D, groundTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, n, n, 0, GL_RED, GL_UNSIGNED_BYTE, ground.data());

    // Road field at 2 m: signed distance across the nearest road, for ruts.
    const int rn = 512;
    const float texel = Battle::kWorldSize / rn;
    std::vector<uint8_t> road(static_cast<size_t>(rn) * rn * 2, 0);
    std::vector<float> best(static_cast<size_t>(rn) * rn, 1e9f);
    for (const sim::Polyline& line : region.roads()) {
        for (size_t i = 0; i + 1 < line.points.size(); ++i) {
            const V3 a = toV3(line.points[i]), b = toV3(line.points[i + 1]);
            const V3 ab = V3{b.x - a.x, b.y - a.y, 0};
            const float len2 = ab.x * ab.x + ab.y * ab.y;
            const int x0 = std::max(0, static_cast<int>((std::min(a.x, b.x) - 8) / texel));
            const int x1 = std::min(rn - 1, static_cast<int>((std::max(a.x, b.x) + 8) / texel));
            const int y0 = std::max(0, static_cast<int>((std::min(a.y, b.y) - 8) / texel));
            const int y1 = std::min(rn - 1, static_cast<int>((std::max(a.y, b.y) + 8) / texel));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = (x + 0.5f) * texel, py = (y + 0.5f) * texel;
                    const float t = std::clamp(((px - a.x) * ab.x + (py - a.y) * ab.y) / len2, 0.0f, 1.0f);
                    const float cx = a.x + ab.x * t - px, cy = a.y + ab.y * t - py;
                    const float dist = std::sqrt(cx * cx + cy * cy);
                    const size_t idx = static_cast<size_t>(y) * rn + x;
                    if (dist >= best[idx] || dist > 8) continue;
                    best[idx] = dist;
                    const float side = (ab.x * (py - a.y) - ab.y * (px - a.x)) >= 0 ? 1.0f : -1.0f;
                    // Across the road in units of a 3 m half width, so wider stretches paint wider.
                    const float w = static_cast<float>(line.widthAt(i, toFixed(t)).toDouble());
                    road[idx * 2] = static_cast<uint8_t>(std::clamp(128.0f + side * dist * 3.0f / w * 16.0f, 0.0f, 255.0f));
                    road[idx * 2 + 1] = 255;
                }
            }
        }
    }
    if (!roadTex_) glGenTextures(1, &roadTex_);
    glBindTexture(GL_TEXTURE_2D, roadTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, rn, rn, 0, GL_RG, GL_UNSIGNED_BYTE, road.data());

    // Water: the surface per sample (rivers slope downhill), drawn over the wet cells.
    waterLevel_ = static_cast<float>(region.waterLevel().toDouble());
    const int samples = Battle::kTerrainCells + 1;
    const std::vector<sim::Fixed>& surfaces = region.waterSurfaces();
    std::vector<float> water(static_cast<size_t>(samples) * samples, -1e6f);
    for (size_t i = 0; i < water.size() && i < surfaces.size(); ++i)
        if (surfaces[i].raw != sim::Region::kDryRaw) water[i] = static_cast<float>(surfaces[i].toDouble());
    if (!waterTex_) glGenTextures(1, &waterTex_);
    glBindTexture(GL_TEXTURE_2D, waterTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, samples, samples, 0, GL_RED, GL_FLOAT, water.data());
    // Which way the water runs at each wet sample: along the nearest river
    // segment, downstream. Ponds and lakes are still.
    {
        std::vector<std::pair<V3, V3>> segs;
        for (const sim::Polyline& rv : region.rivers()) {
            for (size_t i = 0; i + 1 < rv.points.size(); ++i) {
                const V3 a = toV3(rv.points[i]), b = toV3(rv.points[i + 1]);
                if (std::max(a.x, b.x) < -100 || std::max(a.y, b.y) < -100 || std::min(a.x, b.x) > Battle::kWorldSize + 100 ||
                    std::min(a.y, b.y) > Battle::kWorldSize + 100)
                    continue;
                segs.push_back({a, b});
            }
        }
        std::vector<uint8_t> flow(static_cast<size_t>(samples) * samples * 2, 128);
        for (int y = 0; y < samples; ++y) {
            for (int x = 0; x < samples; ++x) {
                const size_t k = static_cast<size_t>(y) * samples + x;
                if (water[k] < -1e5f) continue;
                const float px = x * Battle::kCellSize, py = y * Battle::kCellSize;
                float best = 60.0f * 60.0f;
                V3 dir{0, 0, 0};
                for (const auto& sg : segs) {
                    const V3 ab = sg.second - sg.first;
                    const float len2 = ab.x * ab.x + ab.y * ab.y;
                    if (len2 <= 0) continue;
                    const float t = std::clamp(((px - sg.first.x) * ab.x + (py - sg.first.y) * ab.y) / len2, 0.0f, 1.0f);
                    const float dx = sg.first.x + ab.x * t - px, dy = sg.first.y + ab.y * t - py;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < best) {
                        best = d2;
                        dir = V3{ab.x, ab.y, 0}.normalized();
                    }
                }
                flow[k * 2] = static_cast<uint8_t>(std::clamp(128.0f + dir.x * 127.0f, 0.0f, 255.0f));
                flow[k * 2 + 1] = static_cast<uint8_t>(std::clamp(128.0f + dir.y * 127.0f, 0.0f, 255.0f));
            }
        }
        if (!flowTex_) glGenTextures(1, &flowTex_);
        glBindTexture(GL_TEXTURE_2D, flowTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, samples, samples, 0, GL_RG, GL_UNSIGNED_BYTE, flow.data());
    }
    std::vector<float> wet;
    const float cs = Battle::kCellSize;
    for (int y = 0; y < Battle::kTerrainCells; ++y) {
        for (int x = 0; x < Battle::kTerrainCells; ++x) {
            bool any = false;
            for (int k = 0; k < 4 && !any; ++k)
                any = water[static_cast<size_t>(y + (k >> 1)) * samples + x + (k & 1)] > -1e5f;
            if (!any) continue;
            const float x0 = x * cs, y0 = y * cs, x1 = x0 + cs, y1 = y0 + cs;
            wet.insert(wet.end(), {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1});
        }
    }
    waterVertices_ = static_cast<GLsizei>(wet.size() / 2);
    if (!waterVao_) {
        glGenVertexArrays(1, &waterVao_);
        glGenBuffers(1, &waterVbo_);
    }
    glBindVertexArray(waterVao_);
    glBindBuffer(GL_ARRAY_BUFFER, waterVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max<size_t>(wet.size(), 1) * 4), wet.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindVertexArray(0);
}

void Renderer::buildLand(const Battle& battle, const FarGrid& g, LandMesh& out, bool skirt) {
    // Per vertex: position, normal, colour (the whole look on Low), and the
    // ground's kind as weights (forest, rock, swamp, water) for the detailed
    // shader, which blends them with noisy, natural edges.
    const auto colourOf = [](sim::Ground ground, float h) -> V3 {
        V3 c;
        switch (ground) {
            case sim::Ground::Road: c = {0.48f, 0.40f, 0.27f}; break;
            case sim::Ground::Forest: c = {0.18f, 0.25f, 0.11f}; break;
            case sim::Ground::Swamp: c = {0.24f, 0.29f, 0.17f}; break;
            case sim::Ground::Shallows: c = {0.30f, 0.50f, 0.55f}; break;
            case sim::Ground::DeepWater: c = {0.10f, 0.26f, 0.42f}; break;
            case sim::Ground::Rock: c = {0.48f, 0.46f, 0.43f}; break;
            case sim::Ground::Bridge: c = {0.45f, 0.36f, 0.26f}; break;
            case sim::Ground::Mud: c = {0.33f, 0.27f, 0.18f}; break;
            case sim::Ground::Sand: c = {0.80f, 0.73f, 0.55f}; break;
            default: c = {0.40f, 0.49f, 0.21f}; break;
        }
        if (ground != sim::Ground::DeepWater) {
            const float snow = std::clamp((h - 320.0f) / 40.0f, 0.0f, 1.0f);
            c = c + (V3{0.93f, 0.94f, 0.96f} - c) * snow;
        }
        return c;
    };
    const auto hAt = [&](int i, int j) {
        i = std::clamp(i, 0, g.nx - 1);
        j = std::clamp(j, 0, g.ny - 1);
        return g.height[static_cast<size_t>(j) * g.nx + i];
    };
    // The water, as 1 + depth / 12 where wet. On dry land beside it, 1 less
    // its height above the water next door / 12: the shader's waterline (1)
    // falls between samples where the shore really is, not halfway.
    const auto wetAt = [&](int i, int j) {
        const auto gr = static_cast<sim::Ground>(g.ground[static_cast<size_t>(j) * g.nx + i]);
        return gr == sim::Ground::DeepWater || gr == sim::Ground::Shallows;
    };
    const auto waterOf = [&](int i, int j) {
        if (wetAt(i, j)) return 1.0f + std::clamp(g.depth[static_cast<size_t>(j) * g.nx + i] / 12.0f, 0.0f, 1.0f);
        float surface = -1e6f;
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di) {
                const int a = i + di, b = j + dj;
                if (a >= 0 && b >= 0 && a < g.nx && b < g.ny && wetAt(a, b)) surface = std::max(surface, hAt(a, b));
            }
        if (surface < -1e5f) return 0.0f;
        return std::clamp(1.0f - (hAt(i, j) - surface) / 12.0f, 0.0f, 0.999f);
    };
    const V3 o = battle.origin();
    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(g.nx) * g.ny * 13);
    const auto vertex = [&](int i, int j, float drop) {
        const float x = g.min.x + static_cast<float>(i) * g.cell, y = g.min.y + static_cast<float>(j) * g.cell;
        float z = hAt(i, j) - drop;
        // Under the full-detail battlefield, sink a little out of the way.
        if (x > 0 && y > 0 && x < Battle::kWorldSize && y < Battle::kWorldSize) z -= 3.0f;
        const V3 n = V3{hAt(i - 1, j) - hAt(i + 1, j), hAt(i, j - 1) - hAt(i, j + 1), 2 * g.cell}.normalized();
        const auto ground = static_cast<sim::Ground>(g.ground[static_cast<size_t>(j) * g.nx + i]);
        V3 c = colourOf(ground, z);
        // Inside the walls: trodden earth and lanes, not meadow.
        for (const sim::Continent::City& city : battle.continent().cities()) {
            const float dx = x - (static_cast<float>(city.pos.x.toDouble()) - o.x), dy = y - (static_cast<float>(city.pos.y.toDouble()) - o.y);
            const float r = static_cast<float>(city.radius.toDouble()) * 1.2f;
            if (dx * dx + dy * dy > r * r || city.castle.rings.empty()) continue;
            const auto& ring = city.castle.rings.front().corners;
            bool inside = true;
            for (size_t e = 0; e < ring.size() && inside; ++e) {
                const float ax = static_cast<float>(ring[e].x.toDouble()) - o.x, ay = static_cast<float>(ring[e].y.toDouble()) - o.y;
                const float bx = static_cast<float>(ring[(e + 1) % ring.size()].x.toDouble()) - o.x, by = static_cast<float>(ring[(e + 1) % ring.size()].y.toDouble()) - o.y;
                const float cx = static_cast<float>(city.pos.x.toDouble()) - o.x, cy = static_cast<float>(city.pos.y.toDouble()) - o.y;
                // Same side of the edge as the centre.
                const float sc = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), sp = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
                inside = sc * sp > 0;
            }
            if (inside) c = {0.56f, 0.48f, 0.36f};
        }
        verts.insert(verts.end(), {x, y, z, n.x, n.y, n.z, c.x, c.y, c.z, ground == sim::Ground::Forest ? 1.0f : 0.0f,
                                   ground == sim::Ground::Rock ? 1.0f : 0.0f, ground == sim::Ground::Swamp ? 1.0f : 0.0f, waterOf(i, j)});
    };
    for (int j = 0; j < g.ny; ++j)
        for (int i = 0; i < g.nx; ++i) vertex(i, j, 0);
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(g.nx - 1) * (g.ny - 1) * 6);
    for (int j = 0; j + 1 < g.ny; ++j) {
        for (int i = 0; i + 1 < g.nx; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * g.nx + i), b = a + 1, c = a + g.nx, d = c + 1;
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    }
    if (skirt) {
        // A wall hanging from the edge hides any seam with the coarser land.
        const auto edge = [&](int i, int j) {
            const uint32_t top = static_cast<uint32_t>(j * g.nx + i);
            const uint32_t bottom = static_cast<uint32_t>(verts.size() / 13);
            vertex(i, j, 10.0f);
            return std::pair<uint32_t, uint32_t>{top, bottom};
        };
        std::vector<std::pair<uint32_t, uint32_t>> ring;
        for (int i = 0; i < g.nx; ++i) ring.push_back(edge(i, 0));
        for (int j = 1; j < g.ny; ++j) ring.push_back(edge(g.nx - 1, j));
        for (int i = g.nx - 2; i >= 0; --i) ring.push_back(edge(i, g.ny - 1));
        for (int j = g.ny - 2; j >= 0; --j) ring.push_back(edge(0, j));
        for (size_t k = 0; k + 1 < ring.size(); ++k)
            idx.insert(idx.end(), {ring[k].first, ring[k + 1].first, ring[k + 1].second, ring[k].first, ring[k + 1].second, ring[k].second});
    }
    out.count = static_cast<GLsizei>(idx.size());
    if (!out.vao) {
        glGenVertexArrays(1, &out.vao);
        glGenBuffers(1, &out.vbo);
        glGenBuffers(1, &out.ibo);
    }
    glBindVertexArray(out.vao);
    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * 4), verts.data(), GL_STATIC_DRAW);
    for (int a = 0; a < 4; ++a) {
        glEnableVertexAttribArray(a);
        glVertexAttribPointer(a, a == 3 ? 4 : 3, GL_FLOAT, GL_FALSE, 52, reinterpret_cast<const void*>(static_cast<uintptr_t>(a * 12)));
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void Renderer::updatePatch(const Battle& battle, const Camera& camera) {
    constexpr float kPatchCell = 8.0f, kPatchSize = 1280.0f, kRebuildAt = 320.0f, kShowBelow = 1800.0f;
    constexpr int kRowsPerFrame = 2;  // ~320 samples a frame: no hitch on phones
    if (!(features_ & kFarDetail) || camera.distance() > kShowBelow) return;
    const V3 t = camera.target();
    if (patchRow_ < 0) {
        const float dx = t.x - patchCentre_.x, dy = t.y - patchCentre_.y;
        if (dx * dx + dy * dy < kRebuildAt * kRebuildAt) return;
        // The battlefield and the ring around it are already fine enough.
        const FarGrid& ng = battle.nearGrid();
        const float half = kPatchSize / 2;
        if (t.x - half > ng.min.x && t.y - half > ng.min.y && t.x + half < ring_[2] && t.y + half < ring_[3]) return;
        patchCentre_ = {std::round(t.x / 32.0f) * 32.0f, std::round(t.y / 32.0f) * 32.0f, 0};
        const int n = static_cast<int>(kPatchSize / kPatchCell) + 1;
        patchBuild_.reset({patchCentre_.x - half, patchCentre_.y - half, 0}, n, n, kPatchCell);
        patchRow_ = 0;
    }
    patchBuild_.fillRows(battle.continent(), battle.origin(), patchRow_, patchRow_ + kRowsPerFrame);
    patchRow_ += kRowsPerFrame;
    if (patchRow_ < patchBuild_.ny) return;
    patchRow_ = -1;
    buildLand(battle, patchBuild_, patchLand_, true);
    const float inset = kPatchCell;
    patch_[0] = patchBuild_.min.x + inset;
    patch_[1] = patchBuild_.min.y + inset;
    patch_[2] = patchBuild_.min.x + patchBuild_.cell * static_cast<float>(patchBuild_.nx - 1) - inset;
    patch_[3] = patchBuild_.min.y + patchBuild_.cell * static_cast<float>(patchBuild_.ny - 1) - inset;
}

void Renderer::uploadContinent(const Battle& battle) {
    // The whole continent every 64 m, and a finer ring every 16 m around the
    // battlefield (drawn with kFarDetail; the coarse land sinks beneath it).
    buildLand(battle, battle.farGrid(), farLand_, false);
    buildLand(battle, battle.nearGrid(), nearLand_, true);
    {
        // The sea runs on past the edges of the map, out to the haze.
        const FarGrid& fg = battle.farGrid();
        const float x0 = fg.min.x, y0 = fg.min.y, x1 = x0 + fg.cell * static_cast<float>(fg.nx - 1), y1 = y0 + fg.cell * static_cast<float>(fg.ny - 1);
        const float far = 40000.0f;
        const float xs[4] = {x0 - far, x0, x1, x1 + far}, ys[4] = {y0 - far, y0, y1, y1 + far};
        std::vector<float> verts;
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) verts.insert(verts.end(), {xs[i], ys[j], -0.05f, 0, 0, 1, 0.10f, 0.26f, 0.42f, 0, 0, 0, 2.0f});
        std::vector<uint32_t> idx;
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                if (i == 1 && j == 1) continue;  // the map itself
                const uint32_t a = static_cast<uint32_t>(j * 4 + i), b = a + 1, cc = a + 4, d = cc + 1;
                idx.insert(idx.end(), {a, b, d, a, d, cc});
            }
        ocean_.count = static_cast<GLsizei>(idx.size());
        if (!ocean_.vao) {
            glGenVertexArrays(1, &ocean_.vao);
            glGenBuffers(1, &ocean_.vbo);
            glGenBuffers(1, &ocean_.ibo);
        }
        glBindVertexArray(ocean_.vao);
        glBindBuffer(GL_ARRAY_BUFFER, ocean_.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * 4), verts.data(), GL_STATIC_DRAW);
        for (int a = 0; a < 4; ++a) {
            glEnableVertexAttribArray(a);
            glVertexAttribPointer(a, a == 3 ? 4 : 3, GL_FLOAT, GL_FALSE, 52, reinterpret_cast<const void*>(static_cast<uintptr_t>(a * 12)));
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ocean_.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
    }
    const FarGrid& ng = battle.nearGrid();
    ring_[0] = ng.min.x;
    ring_[1] = ng.min.y;
    ring_[2] = ng.min.x + ng.cell * static_cast<float>(ng.nx - 1);
    ring_[3] = ng.min.y + ng.cell * static_cast<float>(ng.ny - 1);

    // Rivers and roads as ribbons, so they read from god view.
    const sim::Continent& cont = battle.continent();
    const V3 o = battle.origin();
    std::vector<float> ribbons;
    for (const sim::Continent::River& r : cont.rivers()) {
        for (size_t i = 0; i + 1 < r.points.size(); ++i) {
            const float hw = static_cast<float>(r.widths[i].toDouble());
            const V3 a = toV3(r.points[i]) - V3{o.x, o.y, 0}, b = toV3(r.points[i + 1]) - V3{o.x, o.y, 0};
            ribbons.insert(ribbons.end(), {a.x, a.y, a.z, b.x, b.y, b.z, hw, 0});
        }
    }
    // Creeks: thinner, every other point (they are finely sampled).
    for (const sim::Continent::River& st : cont.streams()) {
        for (size_t i = 0; i + 1 < st.points.size(); i += 2) {
            const size_t j = std::min(i + 2, st.points.size() - 1);
            const float hw = static_cast<float>(st.widths[i].toDouble());
            const V3 a = toV3(st.points[i]) - V3{o.x, o.y, 0}, b = toV3(st.points[j]) - V3{o.x, o.y, 0};
            ribbons.insert(ribbons.end(), {a.x, a.y, a.z, b.x, b.y, b.z, hw, 2});
        }
    }
    for (const sim::Polyline& road : cont.roads()) {
        for (size_t i = 0; i + 1 < road.points.size(); ++i) {
            V3 a = toV3(road.points[i]) - V3{o.x, o.y, 0}, b = toV3(road.points[i + 1]) - V3{o.x, o.y, 0};
            // Long road legs follow the ground in 100 m steps.
            const int steps = std::max(1, static_cast<int>((b - a).length() / 100.0f));
            for (int s = 0; s < steps; ++s) {
                V3 p = a + (b - a) * (static_cast<float>(s) / steps), q = a + (b - a) * (static_cast<float>(s + 1) / steps);
                p.z = std::max(battle.heightAt(p.x, p.y), battle.farGrid().heightAt(p.x, p.y));
                q.z = std::max(battle.heightAt(q.x, q.y), battle.farGrid().heightAt(q.x, q.y));
                ribbons.insert(ribbons.end(), {p.x, p.y, p.z, q.x, q.y, q.z, static_cast<float>(road.widthAt(i, toFixed(static_cast<float>(s) / steps)).toDouble()), 1});
            }
        }
    }
    uploadDynamic(ribbons_, ribbons);
    // Waterfalls wherever a creek drops steeply: a sheet down each run of
    // steep segments and a foam pool at its foot.
    {
        std::vector<float> falls;
        for (const sim::Continent::River& st : cont.streams()) {
            const size_t n = st.points.size();
            size_t i = 0;
            while (i + 1 < n) {
                const auto steep = [&](size_t k) {
                    const V3 a = toV3(st.points[k]), b = toV3(st.points[k + 1]);
                    const float run = std::hypot(b.x - a.x, b.y - a.y);
                    return run > 0.1f && (a.z - b.z) / run > 0.45f;
                };
                if (!steep(i)) {
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j + 1 < n && steep(j)) ++j;
                const float drop = static_cast<float>((st.points[i].z - st.points[j].z).toDouble());
                if (drop > 3.0f) {
                    const float hw = std::max(1.2f, static_cast<float>(st.widths[i].toDouble()));
                    float along = 0;
                    for (size_t k = i; k < j; ++k) {
                        const V3 a = toV3(st.points[k]) - V3{o.x, o.y, 0}, b = toV3(st.points[k + 1]) - V3{o.x, o.y, 0};
                        const float len = (b - a).length();
                        falls.insert(falls.end(), {a.x, a.y, a.z, b.x, b.y, b.z, hw, 0, along / 4.0f, len / 4.0f});
                        along += len;
                    }
                    const V3 a = toV3(st.points[j - 1]) - V3{o.x, o.y, 0}, b = toV3(st.points[j]) - V3{o.x, o.y, 0};
                    falls.insert(falls.end(), {a.x, a.y, a.z, b.x, b.y, b.z, hw, 1, 0, 1});
                }
                i = j + 1;
            }
        }
        uploadDynamic(waterfalls_, falls);
    }
    uploadSettlements(battle);

    // Bridges: stone road bridges on arches, timber footbridges on piles.
    Mesh bridges;
    for (const sim::Continent::Crossing& cr : cont.crossings()) {
        if (!cr.bridged()) continue;
        const V3 c = toV3(cr.pos) - V3{o.x, o.y, 0};
        meshes::bridge(bridges, c, toV3(cr.dir), static_cast<float>(cr.halfSpan.toDouble()), static_cast<float>(cr.halfWidth.toDouble()),
                       static_cast<float>(cr.deck.toDouble()), cr.kind == sim::Continent::Crossing::Kind::Footbridge,
                       [&](float x, float y) { return battle.heightAt(x, y); });
    }
    makeMeshBatch(bridges_, bridges, 0);
    bridges_.masonry = true;
    std::vector<float> one;
    const float identity[4] = {0, 0, 0, 1};
    pushInstance(one, {0, 0, 0}, identity, {1, 1, 1}, {1, 1, 1});
    uploadMeshInstances(bridges_, one);
}

void Renderer::uploadGates(const Battle& battle) {
    // Every town's gate: those of the battlefield as the battle has left them
    // (shut, or broken in), the rest of the continent's shut. Uploaded again
    // only when one breaks.
    const std::vector<sim::GateState>& state = battle.world().gates();
    std::vector<uint8_t> broken(state.size());
    for (size_t i = 0; i < state.size(); ++i) broken[i] = state[i].broken();
    if (!gatesShown_.empty() && broken == gatesShown_) return;
    gatesShown_ = broken;
    gatesShown_.push_back(1);  // never empty once uploaded
    std::vector<float> shut, open;
    const auto put = [](std::vector<float>& out, V3 a, V3 b, V3 out_, float top) {
        const float half = (b - a).length() / 2;
        float q[4];
        yawQuat(std::atan2(-out_.x, out_.y), q);
        pushInstance(out, (a + b) * 0.5f, q, {half, half, top - a.z}, {1, 1, 1});
    };
    const auto& gates = battle.region().gates();
    for (size_t i = 0; i < gates.size(); ++i) {
        const sim::Gate& g = gates[i];
        put(broken[i] ? open : shut, toV3(g.a), toV3(g.b), toV3(g.out), static_cast<float>(g.top.toDouble()));
    }
    const sim::Continent& cont = battle.continent();
    const V3 o = battle.origin();
    for (const sim::Continent::City& c : cont.cities()) {
        const V3 centre = toV3(c.pos) - V3{o.x, o.y, 0};
        for (const sim::Continent::Castle::Ring& ring : c.castle.rings) {
            const size_t n = ring.corners.size();
            const V3 a = toV3(ring.corners[ring.gateEdge]) - V3{o.x, o.y, 0}, b = toV3(ring.corners[(ring.gateEdge + 1) % n]) - V3{o.x, o.y, 0};
            V3 mid = (a + b) * 0.5f;
            mid.z = centre.z;
            bool onField = false;
            for (const sim::Gate& g : gates) {
                const V3 gm = toV3(g.a + g.b) * 0.5f;
                onField = onField || (gm.x - mid.x) * (gm.x - mid.x) + (gm.y - mid.y) * (gm.y - mid.y) < 4.0f;
            }
            if (onField) continue;
            V3 along = V3{b.x - a.x, b.y - a.y, 0}.normalized();
            V3 outward = V3{mid.x - centre.x, mid.y - centre.y, 0}.normalized();
            const float half = static_cast<float>(ring.gateWidth.toDouble()) / 2;
            put(shut, mid - along * half, mid + along * half, outward, centre.z + static_cast<float>(ring.height.toDouble()) * 0.62f);
        }
    }
    uploadMeshInstances(gates_, shut);
    uploadMeshInstances(brokenGates_, open);
}

void Renderer::uploadSettlements(const Battle& battle) {
    const sim::Continent& cont = battle.continent();
    const V3 o = battle.origin();
    std::vector<float> walls, towers, banners, houses, ruins, temples, sages;
    float flat[4];
    yawQuat(0, flat);
    float side[4];
    yawQuat(1.5707963f, side);
    const V3 stone{1, 1, 1};
    std::vector<float> drums, keeps, stairs;
    const auto local = [&](const sim::Vec3& p) { return toV3(p) - V3{o.x, o.y, 0}; };
    const auto yawOf = [](V3 d) { return std::atan2(d.y, d.x); };
    for (size_t ci = 0; ci < cont.cities().size(); ++ci) {
        const sim::Continent::City& c = cont.cities()[ci];
        const sim::Continent::Castle& k = c.castle;
        const V3 centre = local(c.pos);
        const V3 colour = factionColor(c.owner);
        // Curtain walls, corner to corner, in sections; the gate edge leaves an
        // opening under a lintel. The parapet faces outward.
        for (const sim::Continent::Castle::Ring& ring : k.rings) {
            const size_t n = ring.corners.size();
            const float height = static_cast<float>(ring.height.toDouble()), thick = static_cast<float>(ring.thick.toDouble());
            const float gate = static_cast<float>(ring.gateWidth.toDouble());
            for (size_t e = 0; e < n; ++e) {
                const V3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % n]);
                V3 along = V3{b.x - a.x, b.y - a.y, 0};
                const float len = along.length();
                along = along * (1.0f / len);
                V3 out{along.y, -along.x, 0};
                if ((V3{centre.x - a.x, centre.y - a.y, 0}).dot(out) > 0) out = out * -1.0f;
                // Local +y must point out: yaw so that (cos, sin) = along and +y = out.
                const bool flip = along.x * out.y - along.y * out.x < 0;
                const V3 dir = flip ? along * -1.0f : along;
                float q[4];
                yawQuat(yawOf(dir), q);
                const auto section = [&](float s0, float s1, float z0, float h) {
                    const float mid = (s0 + s1) / 2, half = (s1 - s0) / 2;
                    pushInstance(walls, a + along * mid + V3{0, 0, z0}, q, {half, thick / 2, h}, {1, 1, 1});
                };
                std::vector<std::pair<float, float>> spans;
                if (e == ring.gateEdge) {
                    spans = {{0, len / 2 - gate / 2}, {len / 2 + gate / 2, len}};
                    // The lintel over the gate passage.
                    const float mid = len / 2;
                    pushInstance(walls, a + along * mid + V3{0, 0, height * 0.62f}, q, {gate / 2 + 0.5f, thick / 2, height * 0.38f}, {0.95f, 0.95f, 0.95f});
                    (void)mid;
                } else {
                    spans = {{0, len}};
                }
                for (const auto& [s0, s1] : spans) {
                    const int pieces = std::max(1, static_cast<int>(std::ceil((s1 - s0) / 10.0f)));
                    for (int p = 0; p < pieces; ++p) section(s0 + (s1 - s0) * p / pieces, s0 + (s1 - s0) * (p + 1) / pieces, 0, height);
                }
            }
        }
        for (const sim::Continent::Castle::Tower& t : k.towers) {
            const float r = static_cast<float>(t.radius.toDouble()), h = static_cast<float>(t.height.toDouble());
            const V3 p = local(t.pos);
            if (t.roofed) pushInstance(towers, p, flat, {r, r, h}, stone);
            else pushInstance(drums, p, flat, {r, r, h}, stone);
            pushInstance(banners, p + V3{0, 0, h + (t.roofed ? h * 0.75f : 0.4f)}, flat, {4.5f, 4.5f, 4.5f}, colour);
        }
        {
            float q[4];
            yawQuat(yawOf(toV3(k.keepAxis)), q);
            const float half = static_cast<float>(k.keepHalf.toDouble()), h = static_cast<float>(k.keepHeight.toDouble());
            const V3 p = local(k.keep);
            pushInstance(keeps, p, q, {half, half, h}, {0.97f, 0.97f, 0.97f});
            const float s = c.capital ? 14.0f : 9.0f;
            pushInstance(banners, p + V3{0, 0, h + 0.3f}, flat, {s, s, s}, colour);
        }
        for (const sim::Continent::Castle::Stair& st : k.stairs) {
            const V3 foot = local(st.foot), head = local(st.head);
            V3 run{head.x - foot.x, head.y - foot.y, 0};
            const float len = run.length();
            float q[4];
            yawQuat(yawOf(run), q);
            pushInstance(stairs, V3{(foot.x + head.x) / 2, (foot.y + head.y) / 2, foot.z}, q,
                         {len / 2, static_cast<float>(st.halfWidth.toDouble()), head.z - foot.z}, {1, 1, 1});
        }
        for (size_t hi = 0; hi < k.houses.size(); ++hi) {
            const sim::Continent::Castle::House& hs = k.houses[hi];
            const uint32_t hh = static_cast<uint32_t>(ci * 131 + hi * 17);
            const float t = 0.85f + 0.3f * hash01(hh + 3);
            const float hx = static_cast<float>(hs.halfX.toDouble()), hy = static_cast<float>(hs.halfY.toDouble());
            // The house mesh's ridge runs along y; turn it to run along the longer side.
            float q[4];
            yawQuat(hx > hy ? 1.5707963f : 0, q);
            pushInstance(houses, local(hs.pos), q, hx > hy ? V3{hy, hx, static_cast<float>(hs.height.toDouble())} : V3{hx, hy, static_cast<float>(hs.height.toDouble())},
                         {t, t * 0.97f, t * 0.92f});
        }
    }
    for (size_t si = 0; si < cont.sites().size(); ++si) {
        const sim::Continent::Site& st = cont.sites()[si];
        const V3 p = toV3(st.pos) - V3{o.x, o.y, 0};
        float q[4];
        yawQuat(hash01(static_cast<uint32_t>(si) * 7919u) * 6.283f, q);
        switch (st.kind) {
            case sim::Continent::Site::Kind::Ruin: pushInstance(ruins, p, q, {40, 40, 40}, {1, 1, 1}); break;
            case sim::Continent::Site::Kind::Temple: pushInstance(temples, p, q, {44, 44, 44}, {1, 1, 1}); break;
            case sim::Continent::Site::Kind::Sage: pushInstance(sages, p, q, {26, 26, 26}, {1, 1, 1}); break;
        }
    }
    uploadMeshInstances(walls_, walls);
    uploadMeshInstances(towers_, towers);
    uploadMeshInstances(drums_, drums);
    uploadMeshInstances(keeps_, keeps);
    uploadMeshInstances(stairs_, stairs);
    uploadMeshInstances(banners_, banners);
    uploadMeshInstances(cityHouses_, houses);
    uploadMeshInstances(ruins_, ruins);
    uploadMeshInstances(temples_, temples);
    uploadMeshInstances(sages_, sages);

    // Woods across the continent as clumps of crowns, so the land beyond the
    // battlefield is not bare from mid zoom (the battlefield has real trees).
    // Broadleaf, pine and autumn clumps; denser in the fine ring around the
    // battlefield when the detailed land is on.
    const auto plant = [&](const FarGrid& g, std::vector<std::vector<float>>& out, int stride, float forestChance, bool skipRing,
                           uint32_t salt) {
        for (int j = 0; j < g.ny; j += stride) {
            for (int i = 0; i < g.nx; i += stride) {
                const uint32_t h = static_cast<uint32_t>(j * 7919 + i * 104729) ^ salt;
                const auto ground = static_cast<sim::Ground>(g.ground[static_cast<size_t>(j) * g.nx + i]);
                const float chance = ground == sim::Ground::Forest ? forestChance : ground == sim::Ground::Plains ? 0.04f
                                     : ground == sim::Ground::Swamp ? 0.08f : 0;
                if (hash01(h) >= chance) continue;
                const float step = g.cell * static_cast<float>(stride);
                const float x = g.min.x + static_cast<float>(i) * g.cell + (hash01(h + 1) - 0.5f) * step;
                const float y = g.min.y + static_cast<float>(j) * g.cell + (hash01(h + 2) - 0.5f) * step;
                bool inTown = false;  // no woods inside the walls
                for (const sim::Continent::City& city : battle.continent().cities()) {
                    const float cx = static_cast<float>(city.pos.x.toDouble()) - battle.origin().x, cy = static_cast<float>(city.pos.y.toDouble()) - battle.origin().y;
                    const float rr = static_cast<float>(city.radius.toDouble()) * 1.25f;
                    inTown = inTown || (x - cx) * (x - cx) + (y - cy) * (y - cy) < rr * rr;
                }
                if (inTown) continue;
                if (skipRing && x > ring_[0] && y > ring_[1] && x < ring_[2] && y < ring_[3]) continue;
                float q[4];
                yawQuat(hash01(h + 3) * 6.283f, q);
                const float w = step * (0.45f + hash01(h + 4) * 0.35f), tall = 14.0f + hash01(h + 5) * 12.0f;
                const float t = 0.85f + 0.3f * hash01(h + 6);
                // Higher ground grows pines; a few stands have turned.
                const float z = battle.heightAt(x, y);
                const size_t v = z > 120.0f || hash01(h + 7) < 0.3f ? 1 : hash01(h + 8) < 0.12f ? 2 : 0;
                pushInstance(out[v], {x, y, z - 1.0f}, q, {w, w, tall}, {t, t, t});
            }
        }
    };
    std::vector<std::vector<float>> farCopses(copses_.size()), nearCopses(nearCopses_.size());
    plant(battle.farGrid(), farCopses, 1, 0.55f, false, 0);
    plant(battle.nearGrid(), nearCopses, 2, 0.7f, false, 0x9E3779B9u);
    for (size_t v = 0; v < copses_.size(); ++v) uploadMeshInstances(copses_[v], farCopses[v]);
    for (size_t v = 0; v < nearCopses_.size(); ++v) uploadMeshInstances(nearCopses_[v], nearCopses[v]);
}

void Renderer::setBoats(const std::vector<Prop>& boats) {
    std::vector<float> hulls, sails;
    for (const Prop& p : boats) {
        float q[4];
        yawQuat(p.yaw, q);
        pushInstance(hulls, p.pos, q, {p.scale, p.scale, p.scale}, {1, 1, 1});
        pushInstance(sails, p.pos, q, {p.scale, p.scale, p.scale}, p.tint);
    }
    uploadMeshInstances(ships_, hulls);
    uploadMeshInstances(sails_, sails);
}

void Renderer::setPiers(const std::vector<Prop>& piers) {
    std::vector<float> out;
    for (const Prop& p : piers) {
        float q[4];
        yawQuat(p.yaw, q);
        pushInstance(out, p.pos, q, {p.scale, p.scale * 0.09f, 5}, p.tint);
    }
    uploadMeshInstances(piers_, out);
}

bool Renderer::init(Battle& battle) {
    if (!buildPrograms()) return false;
    ensureShadowMap();
    makeCoverLayer(cover_layers_[0], meshes::grassTuft(), 0, 0.37f);
    makeCoverLayer(cover_layers_[1], meshes::flowers(), 1, 1.71f);
    makeCoverLayer(cover_layers_[2], meshes::pebbles(), 2, 2.93f);
    makeCoverLayer(cover_layers_[3], meshes::twigs(), 3, 4.19f);
    makeCoverLayer(cover_layers_[4], meshes::fern(), 4, 5.57f);
    makeBatch(marks_, {3, 4, 4}, Battle::kMaxGroundMarks);
    makeBatch(ribbons_, {3, 3, 2}, 0);
    makeBatch(waterfalls_, {3, 3, 4}, 0);
    makeBatch(remainArrows_, {3, 3, 2}, Battle::kMaxRemains);
    makeBatch(sceneryShadows_, {3, 4, 4}, 0);
    const auto makeAll = [&](std::vector<MeshBatch>& out, const std::vector<Mesh>& meshes, float sway) {
        out.resize(meshes.size());
        for (size_t v = 0; v < meshes.size(); ++v) makeMeshBatch(out[v], meshes[v], sway);
    };
    makeAll(pines_, meshes::pineVariants(), 0.03f);
    makeAll(oaks_, meshes::oakVariants(), 0.02f);
    makeAll(bushes_, meshes::bushVariants(), 0.04f);
    makeAll(rocks_, meshes::rockVariants(), 0);
    makeMeshBatch(stumps_, meshes::stump(), 0);
    makeMeshBatch(houses_, meshes::house(), 0);
    makeMeshBatch(pillars_, meshes::pillar(), 0);
    makeMeshBatch(capsules_, meshes::capsule(), 0);
    makeMeshBatch(spheres_, meshes::sphere(), 0);
    makeMeshBatch(walls_, meshes::curtain(), 0);
    makeMeshBatch(drums_, meshes::drum(), 0);
    makeMeshBatch(keeps_, meshes::keep(), 0);
    makeMeshBatch(stairs_, meshes::stairs(), 0);
    makeMeshBatch(gates_, meshes::gate(false), 0);
    makeMeshBatch(brokenGates_, meshes::gate(true), 0);
    for (MeshBatch* b : {&walls_, &towers_, &drums_, &keeps_, &stairs_}) b->masonry = true;
    makeMeshBatch(towers_, meshes::tower(), 0);
    makeMeshBatch(banners_, meshes::banner(), 0.02f);
    makeMeshBatch(cityHouses_, meshes::house(), 0);
    makeMeshBatch(ruins_, meshes::ruin(), 0);
    makeMeshBatch(temples_, meshes::temple(), 0);
    makeMeshBatch(sages_, meshes::sageTower(), 0);
    makeMeshBatch(logs_, meshes::log(), 0);
    makeMeshBatch(ships_, meshes::ship(), 0);
    makeMeshBatch(sails_, meshes::sail(), 0.01f);
    makeMeshBatch(piers_, meshes::pier(), 0);
    makeAll(copses_, meshes::copseVariants(), 0.01f);
    makeAll(nearCopses_, meshes::copseVariants(), 0.01f);
    makeAll(detailPines_, meshes::pineVariants(), 0.03f);
    makeAll(detailOaks_, meshes::oakVariants(), 0.02f);
    makeAll(detailBushes_, meshes::bushVariants(), 0.04f);
    makeAll(detailRocks_, meshes::rockVariants(), 0);
    makeMeshBatch(detailLogs_, meshes::log(), 0);
    makeMeshBatch(detailStumps_, meshes::stump(), 0);
    // Individual scenery near the camera, the woods' clumps beyond, on the
    // battlefield and off it alike.
    for (auto* list : {&pines_, &oaks_, &bushes_, &rocks_, &detailPines_, &detailOaks_, &detailBushes_, &detailRocks_})
        for (MeshBatch& b : *list) b.lod = 1;
    for (MeshBatch* b : {&logs_, &stumps_, &detailLogs_, &detailStumps_}) b->lod = 1;
    for (auto* list : {&copses_, &nearCopses_})
        for (MeshBatch& b : *list) b.lod = -1;
    makeBatch(dynamicDecals_, {3, 4, 4}, 0);
    makeBatch(flyingArrows_, {3, 3, 2}, 0);
    makeBatch(sprites_, {3, 3, 4, 4, 3, 2, 1}, 0);
    makeBatch(units_, {3, 1, 1, 1}, 0);
    makeBatch(particles_, {3, 4}, 0);
    makeBatch(bars_, {3, 4}, 0);
    reset(battle);
    return true;
}

void Renderer::reset(Battle& battle) {
    marksUploaded_ = 0;
    marks_.count = 0;
    zeroBatch(remainArrows_, Battle::kMaxRemains);
    remainArrows_.count = 0;
    battle.takeDirtyRemains();
    // The close-up patch belongs to the old battlefield's coordinates.
    patchLand_.count = 0;
    patchRow_ = -1;
    patchCentre_ = {1e9f, 1e9f, 0};
    patch_[0] = patch_[1] = 1e9f;
    patch_[2] = patch_[3] = -1e9f;
    uploadTerrain(battle);
    uploadScenery(battle);
    uploadCanopy(battle);
    uploadContinent(battle);
    resetDetail(battle);
    walked_.clear();
    lastSeen_.clear();
    gatesShown_.clear();
    unitsChecked_ = SIZE_MAX;
}

// ------------------------------------------------------------- sprite sheets

void Renderer::bakeMissing(const Battle& battle) {
    if (!showUnits_) return;
    // The types this battle needs that have no sheet yet, in order.
    std::vector<UnitType> need;
    for (const sim::Unit& u : battle.world().units()) {
        const auto t = static_cast<UnitType>(u.stats.type);
        if (!sheets_[static_cast<int>(t)].tex && std::find(need.begin(), need.end(), t) == need.end()) need.push_back(t);
    }
    if (need.empty()) return;
    std::vector<figures::Plan> plans;
    double texels = 0;
    for (UnitType t : need) {
        plans.push_back(figures::plan(t));
        float big = 0.1f;
        for (int v = 0; v < figures::kViews; ++v)
            for (const figures::Extent& e : plans.back().extents[v]) big = std::max({big, e.u1 - e.u0, e.v1 - e.v0});
        const float s = std::min(figures::kPixelsPerMetre, figures::kMaxPixels / big);
        for (int v = 0; v < figures::kViews; ++v)
            for (const figures::Extent& e : plans.back().extents[v]) texels += ((e.u1 - e.u0) * s + 4) * ((e.v1 - e.v0) * s + 4);
    }
    // A battle with many kinds of soldier shares the budget: every sheet a little coarser.
    const double room = std::max(kSheetBudget * 0.25, kSheetBudget - sheetTexels_);
    const float scale = texels > room ? static_cast<float>(std::sqrt(room / texels)) : 1.0f;
    for (size_t i = 0; i < need.size(); ++i) bakeSheet(sheets_[static_cast<int>(need[i])], need[i], plans[i], scale);
}

void Renderer::bakeSheet(Sheet& sh, UnitType type, const figures::Plan& plan, float budgetScale) {
    const auto started = std::chrono::steady_clock::now();
    sh.lift = 0.5f + static_cast<float>(sim::unitStats(type).radius.toDouble()) * 1.2f;
    constexpr int kCells = figures::kViews * figures::kFramesPerView;
    // Every picture at one scale, the finest that keeps the largest within kMaxPixels.
    float big = 0.1f;
    for (int v = 0; v < figures::kViews; ++v)
        for (const figures::Extent& e : plan.extents[v]) big = std::max({big, e.u1 - e.u0, e.v1 - e.v0});
    float scale = std::min({figures::kPixelsPerMetre, figures::kMaxPixels / big, kScratch / 2 / big}) * budgetScale;
    struct Slot {
        int w, h, x = 0, y = 0;
    };
    Slot slots[kCells];
    // Shelves, tallest first, in a sheet as narrow as keeps it roughly square.
    for (;;) {
        double area = 0;
        for (int c = 0; c < kCells; ++c) {
            const figures::Extent& e = plan.extents[c / figures::kFramesPerView][c % figures::kFramesPerView];
            slots[c].w = std::max(2, static_cast<int>(std::ceil((e.u1 - e.u0) * scale)));
            slots[c].h = std::max(2, static_cast<int>(std::ceil((e.v1 - e.v0) * scale)));
            area += static_cast<double>(slots[c].w + 2 * kGutter) * (slots[c].h + 2 * kGutter);
        }
        sh.texW = 256;
        while (sh.texW < kSheetMax && static_cast<double>(sh.texW) * sh.texW < area * 1.2) sh.texW *= 2;
        int order[kCells];
        for (int c = 0; c < kCells; ++c) order[c] = c;
        std::sort(order, order + kCells, [&](int a, int b) { return slots[a].h > slots[b].h; });
        int x = 0, y = 0, shelf = 0;
        for (int c : order) {
            const int w = slots[c].w + 2 * kGutter, h = slots[c].h + 2 * kGutter;
            if (x + w > sh.texW) x = 0, y += shelf, shelf = 0;
            slots[c].x = x + kGutter, slots[c].y = y + kGutter;
            x += w;
            shelf = std::max(shelf, h);
        }
        sh.texH = y + shelf;
        if (sh.texH <= kSheetMax) break;
        scale *= 0.9f;
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (!scratchFbo_) {
        glGenTextures(1, &scratchTex_);
        glBindTexture(GL_TEXTURE_2D, scratchTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kScratch, kScratch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenRenderbuffers(1, &scratchDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, scratchDepth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kScratch, kScratch);
        glGenFramebuffers(1, &scratchFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, scratchFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scratchTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, scratchDepth_);
        glGenFramebuffers(1, &sheetFbo_);
        glGenVertexArrays(1, &figureVao_);
        glGenBuffers(1, &figureVbo_);
        glBindVertexArray(figureVao_);
        glBindBuffer(GL_ARRAY_BUFFER, figureVbo_);
        // Position, normal, colour, team mark, material (figures::kVertexFloats).
        const int offsets[5] = {0, 3, 6, 9, 10}, sizes[5] = {3, 3, 3, 1, 1};
        for (int a = 0; a < 5; ++a) {
            glEnableVertexAttribArray(a);
            glVertexAttribPointer(a, sizes[a], GL_FLOAT, GL_FALSE, figures::kVertexFloats * 4, reinterpret_cast<void*>(static_cast<intptr_t>(offsets[a] * 4)));
        }
        glGenVertexArrays(1, &emptyVao_);
        glGenTextures(1, &lightTex_);
        glBindTexture(GL_TEXTURE_2D, lightTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kScratch, kScratch, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers(1, &lightFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, lightFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, lightTex_, 0);
    }
    glGenTextures(1, &sh.tex);
    glBindTexture(GL_TEXTURE_2D, sh.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sh.texW, sh.texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, sheetFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sh.tex, 0);
    glViewport(0, 0, sh.texW, sh.texH);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    std::vector<float> table(static_cast<size_t>(kCells) * 2 * 4);
    for (int f = 0; f < figures::kFramesPerView; ++f) {
        const std::vector<float>& mesh = plan.frames[f];
        glBindVertexArray(figureVao_);
        glBindBuffer(GL_ARRAY_BUFFER, figureVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.size() * 4), mesh.data(), GL_STREAM_DRAW);
        for (int v = 0; v < figures::kViews; ++v) {
            const figures::View view = figures::view(v);
            const figures::Extent& e = plan.extents[v][f];
            const int c = v * figures::kFramesPerView + f;
            const Slot& slot = slots[c];
            // Exactly the slot's pixels: widen the extent to whole pixels.
            const float w = slot.w / scale, h = slot.h / scale;
            float* row0 = &table[static_cast<size_t>(c) * 4];
            float* row1 = &table[(static_cast<size_t>(kCells) + c) * 4];
            row0[0] = static_cast<float>(slot.x), row0[1] = static_cast<float>(slot.y), row0[2] = static_cast<float>(slot.w), row0[3] = static_cast<float>(slot.h);
            row1[0] = e.u0, row1[1] = e.v0, row1[2] = w, row1[3] = h;
            // Its key light: over the viewer's left shoulder. First the
            // figure seen from the light, for its own shadows.
            const V3 key = (view.toward * 0.7f + view.up * 0.5f - view.right * 0.5f).normalized();
            const V3 lr = key.cross({0, 0, 1}).length() > 0.1f ? key.cross({0, 0, 1}).normalized() : V3{1, 0, 0};
            const V3 lu = lr.cross(key).normalized();
            float a0 = 1e9f, a1 = -1e9f, b0 = 1e9f, b1 = -1e9f, d0 = 1e9f, d1 = -1e9f;
            for (size_t i = 0; i + 2 < mesh.size(); i += figures::kVertexFloats) {
                const V3 q{mesh[i], mesh[i + 1], mesh[i + 2]};
                a0 = std::min(a0, q.dot(lr)), a1 = std::max(a1, q.dot(lr));
                b0 = std::min(b0, q.dot(lu)), b1 = std::max(b1, q.dot(lu));
                d0 = std::min(d0, q.dot(key)), d1 = std::max(d1, q.dot(key));
            }
            const float lrect[4] = {a0 - 0.05f, b0 - 0.05f, a1 - a0 + 0.1f, b1 - b0 + 0.1f};
            const float lrange[2] = {d1 + 0.1f, d1 - d0 + 0.2f};
            glBindFramebuffer(GL_FRAMEBUFFER, lightFbo_);
            glViewport(0, 0, kScratch, kScratch);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glUseProgram(figureDepth_.id);
            glUniform3f(figureDepth_.lightRight, lr.x, lr.y, lr.z);
            glUniform3f(figureDepth_.lightUp, lu.x, lu.y, lu.z);
            glUniform3f(figureDepth_.lightToward, key.x, key.y, key.z);
            glUniform4fv(figureDepth_.lightRect, 1, lrect);
            glUniform2fv(figureDepth_.lightRange, 1, lrange);
            glBindVertexArray(figureVao_);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.size() / figures::kVertexFloats));
            // The figure, twice the size, into the scratch.
            glBindFramebuffer(GL_FRAMEBUFFER, scratchFbo_);
            glViewport(0, 0, slot.w * 2, slot.h * 2);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glUseProgram(figure_.id);
            glUniform3f(figure_.bakeRight, view.right.x, view.right.y, view.right.z);
            glUniform3f(figure_.bakeUp, view.up.x, view.up.y, view.up.z);
            glUniform3f(figure_.bakeToward, view.toward.x, view.toward.y, view.toward.z);
            glUniform4f(figure_.bakeRect, e.u0, e.v0, w, h);
            glUniform3f(figure_.lightRight, lr.x, lr.y, lr.z);
            glUniform3f(figure_.lightUp, lu.x, lu.y, lu.z);
            glUniform3f(figure_.lightToward, key.x, key.y, key.z);
            glUniform4fv(figure_.lightRect, 1, lrect);
            glUniform2fv(figure_.lightRange, 1, lrange);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, lightTex_);
            glUniform1i(figure_.lightDepth, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(figureVao_);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.size() / figures::kVertexFloats));
            // Down into its slot.
            glBindFramebuffer(GL_FRAMEBUFFER, sheetFbo_);
            glViewport(slot.x, slot.y, slot.w, slot.h);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(resolve_.id);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, scratchTex_);
            glUniform1i(resolve_.scratch, 0);
            glUniform4f(resolve_.resolve, static_cast<float>(slot.x), static_cast<float>(slot.y), static_cast<float>(slot.w), static_cast<float>(slot.h));
            glBindVertexArray(emptyVao_);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
    glBindTexture(GL_TEXTURE_2D, sh.tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    // Close up the pixels stay crisp, as Myth's did; far off they average.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &sh.cells);
    glBindTexture(GL_TEXTURE_2D, sh.cells);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kCells, 2, 0, GL_RGBA, GL_FLOAT, table.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    sheetTexels_ += static_cast<double>(sh.texW) * sh.texH;
    std::printf("sprite sheet %s: %dx%d at %.1f px/m, %.0f ms\n", sim::unitTypeName(type), sh.texW, sh.texH, scale,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
}

void Renderer::drawUnits(const Camera& camera, double timeSeconds, float gameSeconds, V3 viewDir, V3 right, bool shadow) {
    if (unitGroups_.empty()) return;
    setCommon(unit_, camera, timeSeconds, gameSeconds);
    glUniform3f(unit_.camRight, right.x, right.y, right.z);
    glUniform2f(unit_.viewDir, viewDir.x, viewDir.y);
    glUniform1i(unit_.sheet, 7);
    glUniform1i(unit_.cellTable, 8);
    glBindVertexArray(units_.vao);
    glBindBuffer(GL_ARRAY_BUFFER, units_.vbo);
    for (const auto& group : unitGroups_) {
        const Sheet& sh = sheets_[group.first / 2];
        const auto range = group.second;
        const float* team = group.first % 2 == 0 ? kPlayerColor : kEnemyColor;
        const V3 cloth = figures::cloth(team);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, sh.tex);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, sh.cells);
        glUniform2f(unit_.sheetSize, static_cast<float>(sh.texW), static_cast<float>(sh.texH));
        glUniform1f(unit_.lift, shadow ? 0.0f : sh.lift);
        glUniform3f(unit_.team, team[0], team[1], team[2]);
        glUniform3f(unit_.cloth, cloth.x, cloth.y, cloth.z);
        // WebGL 2 has no base instance: point the attributes at this group.
        const int stride = units_.floats * 4;
        const auto at = [&](int floats) { return reinterpret_cast<void*>(static_cast<intptr_t>(range.first * stride + floats * 4)); };
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, at(0));
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, at(3));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, at(4));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, at(5));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(range.second));
    }
    glActiveTexture(GL_TEXTURE0);
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
    // Two triangles per cell split along the (x, y)-(x+1, y+1) diagonal, matching
    // Terrain::heightAt so ground contact looks exact.
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

void Renderer::uploadMarks(const Battle& battle) {
    const float tickSeconds = 1.0f / sim::kTicksPerSecond;
    const uint64_t total = battle.groundMarksAdded();
    if (total - marksUploaded_ > Battle::kMaxGroundMarks) marksUploaded_ = total - Battle::kMaxGroundMarks;
    glBindBuffer(GL_ARRAY_BUFFER, marks_.vbo);
    for (; marksUploaded_ < total; ++marksUploaded_) {
        const size_t slot = marksUploaded_ % Battle::kMaxGroundMarks;
        const GroundMark& m = battle.groundMarks()[slot];
        const float type = m.kind == GroundMark::Kind::Blood ? kDecalBlood
                           : m.kind == GroundMark::Kind::Crater ? kDecalCrater
                                                                : kDecalDust;
        const float data[kDecalFloats] = {m.pos.x, m.pos.y, m.pos.z, m.yaw, m.size, m.tick * tickSeconds, type,
                                          0, 0, 0, static_cast<float>(m.seed % 1000) / 1000.0f};
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof data), sizeof data, data);
    }
    marks_.count = static_cast<GLsizei>(battle.groundMarks().size());
}

void Renderer::uploadRemains(Battle& battle) {
    const float tickSeconds = 1.0f / sim::kTicksPerSecond;
    const auto& remains = battle.world().remains();
    glBindBuffer(GL_ARRAY_BUFFER, remainArrows_.vbo);
    for (uint32_t slot : battle.takeDirtyRemains()) {
        if (slot >= remains.size()) continue;
        const sim::Remain& r = remains[slot];
        float arrow[kArrowFloats] = {0, 0, 0, 0, 0, 0, 0, 3};
        if (r.kind == sim::RemainKind::Arrow) {
            const V3 pos = toV3(r.pos), dir = toV3(r.dir);
            // Stuck in the ground, lying loose, or embedded in a tree or wall.
            const float state = pos.z > battle.heightAt(pos.x, pos.y) + 0.3f ? 4.0f : dir.z < -0.2f ? 1.0f : 2.0f;
            const float a[kArrowFloats] = {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, r.tick * tickSeconds, state};
            std::copy(a, a + kArrowFloats, arrow);
        }
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof arrow), sizeof arrow, arrow);
    }
    remainArrows_.count = static_cast<GLsizei>(remains.size());
}

void Renderer::makeCoverLayer(CoverLayer& l, const Mesh& mesh, float kind, float seed) {
    l.kind = kind;
    l.seed = seed;
    l.vertices = mesh.vertexCount();
    glGenVertexArrays(1, &l.vao);
    glGenBuffers(1, &l.vbo);
    glBindVertexArray(l.vao);
    glBindBuffer(GL_ARRAY_BUFFER, l.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.data.size() * 4), mesh.data.data(), GL_STATIC_DRAW);
    for (int i = 0; i < 3; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<const void*>(static_cast<uintptr_t>(i * 12)));
    }
    glBindVertexArray(0);
}

void Renderer::drawCover(const Camera& camera, double timeSeconds, float gameSeconds) {
    // From high up the blades are sub-pixel and the painted ground stands in.
    if (!(features_ & kGroundCover) || camera.distance() > 110.0f) return;
    const bool dense = features_ & kCoverDense;
    // Spacing (m) and grid size per layer: grass, flowers, pebbles, twigs, ferns.
    const float spacing[5] = {dense ? 0.42f : 0.6f, dense ? 1.1f : 1.5f, dense ? 1.4f : 1.9f, dense ? 1.6f : 2.2f, dense ? 1.3f : 1.8f};
    const int grid[5] = {dense ? 200 : 110, dense ? 76 : 44, dense ? 60 : 36, dense ? 52 : 30, dense ? 64 : 38};
    // On the battlefield, and off it on the close-up land: a pass for each the grid reaches.
    const V3 t = camera.target();
    const float reach = 50.0f;
    const bool onField = t.x > -reach && t.y > -reach && t.x < Battle::kWorldSize + reach && t.y < Battle::kWorldSize + reach;
    const bool offField = detailOn_ && !(t.x > reach && t.y > reach && t.x < Battle::kWorldSize - reach && t.y < Battle::kWorldSize - reach);
    for (int pass = 0; pass < 2; ++pass) {
        if (!(pass == 0 ? onField : offField)) continue;
        setCommon(cover_, camera, timeSeconds, gameSeconds);
        if (pass == 1) bindDetail(cover_);
        for (int i = 0; i < 5; ++i) {
            const CoverLayer& l = cover_layers_[i];
            glUniform4f(cover_.cover, spacing[i], static_cast<float>(grid[i]), l.seed, l.kind);
            glBindVertexArray(l.vao);
            glDrawArraysInstanced(GL_TRIANGLES, 0, l.vertices, grid[i] * grid[i]);
        }
    }
}

void Renderer::renderShadowMap(const Camera& camera, double timeSeconds, float gameSeconds) {
    shadowInfo_[2] = 0;  // no shadows unless drawn below
    if (!(features_ & kShadows) || camera.distance() > 1400.0f) return;
    const bool high = features_ & kShadowsHigh;
    // A square around the camera target, wide enough for what is on screen.
    const float radius = std::clamp(camera.distance() * 1.7f + 40.0f, 60.0f, high ? 900.0f : 550.0f);
    const V3 sun = V3{-0.62f, 0.38f, 0.52f}.normalized();
    const V3 centre = camera.target();
    const M4 view = M4::lookAt(centre + sun * 1500.0f, centre, {0, 0, 1});
    // Snap to whole texels so the shadows do not crawl as the camera pans.
    const float texel = 2 * radius / static_cast<float>(shadowSize_);
    const float* v = view.m;
    float cx = v[0] * centre.x + v[4] * centre.y + v[8] * centre.z + v[12];
    float cy = v[1] * centre.x + v[5] * centre.y + v[9] * centre.z + v[13];
    cx = std::floor(cx / texel) * texel;
    cy = std::floor(cy / texel) * texel;
    // Depth only as deep as the area needs: the bias is a few centimetres, so
    // soldiers shadow the ground right at their feet.
    const float depthNear = 1500.0f - radius - 400.0f, depthFar = 1500.0f + radius + 400.0f;
    lightViewProj_ = M4::ortho(cx - radius, cx + radius, cy - radius, cy + radius, depthNear, depthFar) * view;
    shadowInfo_[0] = 1.0f / static_cast<float>(shadowSize_);
    shadowInfo_[1] = radius * 0.7f;
    shadowInfo_[2] = radius * 0.95f;
    shadowInfo_[3] = (0.06f + texel * 1.2f) / (depthFar - depthNear) * 2.0f * 0.5f;  // metres, in [0, 1] depth

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    viewProjOverride_ = &lightViewProj_;
    const int features = features_;
    features_ &= ~kShadows;  // casters do not sample the map they are drawn into
    setCommon(terrainDepth_, camera, timeSeconds, gameSeconds);
    glBindVertexArray(terrainVao_);
    glDrawElements(GL_TRIANGLES, terrainIndexCount_, GL_UNSIGNED_INT, nullptr);
    drawDetailTerrain(detailDepth_, camera, timeSeconds, gameSeconds);
    setCommon(meshDepth_, camera, timeSeconds, gameSeconds);
    std::vector<MeshBatch*> casters = scenery(true);
    for (MeshBatch* b : {&walls_, &towers_, &drums_, &keeps_, &stairs_, &banners_, &cityHouses_, &ruins_, &temples_, &sages_, &bridges_, &gates_, &brokenGates_}) casters.push_back(b);
    for (MeshBatch& b : copses_) casters.push_back(&b);
    if (features_ & kFarDetail)
        for (MeshBatch& b : nearCopses_) casters.push_back(&b);
    for (MeshBatch* b : casters) {
        if (b->count == 0) continue;
        glUniform1f(meshDepth_.sway, b->sway);
        glUniform1f(meshDepth_.lod, b->lod);
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, b->vertices, b->count);
    }
    // Soldiers: their billboards turned side-on to the sun, so the cut-out
    // silhouettes cast real shadows.
    if (sprites_.count > 0) {
        setCommon(sprite_, camera, timeSeconds, gameSeconds);
        const V3 right = V3{sun.y, -sun.x, 0}.normalized();
        glUniform3f(sprite_.camRight, right.x, right.y, right.z);
        glBindVertexArray(sprites_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, sprites_.count);
    }
    {
        const V3 toward = V3{-sun.x, -sun.y, 0}.normalized();
        drawUnits(camera, timeSeconds, gameSeconds, toward, V3{toward.y, -toward.x, 0}, true);
    }
    features_ = features;
    viewProjOverride_ = nullptr;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setFeatures(int features) {
    features_ = features;
    ensureShadowMap();
}

void Renderer::ensureShadowMap() {
    // A real map only when shadows are on; otherwise a 1x1 stand-in keeps
    // every program's shadow sampler bound to a depth texture.
    if (!blankShadowTex_) {
        // Bound in place of the real map while it is being drawn (a texture
        // may not be sampled and rendered to at once, even unread).
        glGenTextures(1, &blankShadowTex_);
        glBindTexture(GL_TEXTURE_2D, blankShadowTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    }
    const int size = (features_ & kShadows) ? ((features_ & kShadowsHigh) ? 2048 : 1024) : 1;
    if (size == shadowSize_) return;
    shadowSize_ = size;
    if (!shadowTex_) glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    if (!shadowFbo_) glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::uploadCanopy(const Battle& battle) {
    // How much tree crown covers each 2 m of the battlefield, softened: the
    // ground under groves is shaded and littered rather than grassy.
    const int n = 512;
    const float texel = Battle::kWorldSize / n;
    std::vector<float> cover(static_cast<size_t>(n) * n, 0.0f);
    for (const sim::Obstacle& o : battle.region().obstacles()) {
        float crown = 0;
        if (o.kind == sim::Obstacle::Kind::Pine) crown = 2.6f;
        else if (o.kind == sim::Obstacle::Kind::Oak) crown = 3.6f;
        else if (o.kind == sim::Obstacle::Kind::Bush) crown = 1.2f;
        if (crown == 0) continue;
        const V3 p = toV3(o.pos);
        const int r = static_cast<int>(std::ceil(crown * 1.6f / texel));
        const int cx = static_cast<int>(p.x / texel), cy = static_cast<int>(p.y / texel);
        for (int y = std::max(0, cy - r); y <= std::min(n - 1, cy + r); ++y) {
            for (int x = std::max(0, cx - r); x <= std::min(n - 1, cx + r); ++x) {
                const float dx = (x + 0.5f) * texel - p.x, dy = (y + 0.5f) * texel - p.y;
                const float d = std::sqrt(dx * dx + dy * dy) / (crown * 1.6f);
                if (d < 1) cover[static_cast<size_t>(y) * n + x] += (1 - d * d) * (o.kind == sim::Obstacle::Kind::Bush ? 0.3f : 0.6f);
            }
        }
    }
    std::vector<uint8_t> bytes(cover.size());
    for (size_t i = 0; i < cover.size(); ++i) bytes[i] = static_cast<uint8_t>(std::min(1.0f, cover[i]) * 255);
    if (!canopyTex_) glGenTextures(1, &canopyTex_);
    glBindTexture(GL_TEXTURE_2D, canopyTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, n, n, 0, GL_RED, GL_UNSIGNED_BYTE, bytes.data());
}

void Renderer::setCommon(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds) {
    glUseProgram(p.id);
    const M4 vp = viewProjOverride_ ? *viewProjOverride_ : camera.viewProjection();
    glUniformMatrix4fv(p.viewProj, 1, GL_FALSE, vp.m);
    const V3 eye = camera.eye();
    glUniform3f(p.eye, eye.x, eye.y, eye.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glUniform1i(p.heights, 0);
    glUniform1f(p.cells, static_cast<float>(cells_));
    if (p.wrap >= 0) glUniform1f(p.wrap, 0.0f);  // the battlefield's textures (bindDetail: the close-up land's)
    if (p.detailRect >= 0) glUniform4fv(p.detailRect, 1, detailRect_);
    if (p.detailFade >= 0) glUniform1f(p.detailFade, detailFade_);
    if (p.detailMask >= 0) {
        glActiveTexture(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D, detailMask_);
        glUniform1i(p.detailMask, 9);
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform1f(p.cellSize, cellSize_);
    glUniform1f(p.time, static_cast<float>(std::fmod(timeSeconds, 3600.0)));
    glUniform1f(p.gameSeconds, gameSeconds);
    glUniform1f(p.camDist, camera.distance());
    glUniform1f(p.pxScale, 2.0f * std::tan(Camera::kFovY / 2) / camera.height());
    const V3 f = (camera.target() - eye).normalized();
    const V3 right = f.cross({0, 0, 1}).normalized();
    glUniform3f(p.camRight, right.x, right.y, right.z);
    glUniform1f(p.water, waterLevel_);
    if (p.waterTex >= 0) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, waterTex_);
        glUniform1i(p.waterTex, 3);
        glActiveTexture(GL_TEXTURE0);
    }
    if (p.hole >= 0) glUniform4f(p.hole, 0, 0, Battle::kWorldSize, Battle::kWorldSize);
    glUniform1i(p.features, features_);
    if (p.shadowMap >= 0) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, viewProjOverride_ ? blankShadowTex_ : shadowTex_);
        glUniform1i(p.shadowMap, 4);
        glActiveTexture(GL_TEXTURE0);
    }
    if (p.lightViewProj >= 0) glUniformMatrix4fv(p.lightViewProj, 1, GL_FALSE, lightViewProj_.m);
    if (p.shadowInfo >= 0) glUniform4fv(p.shadowInfo, 1, shadowInfo_);
    if (p.focus >= 0) glUniform2f(p.focus, camera.target().x, camera.target().y);
    if (p.flow >= 0) {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, flowTex_);
        glUniform1i(p.flow, 6);
        glActiveTexture(GL_TEXTURE0);
    }
    if (p.canopy >= 0) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, canopyTex_);
        glUniform1i(p.canopy, 5);
        glActiveTexture(GL_TEXTURE0);
    }
    if (p.ground >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, groundTex_);
        glUniform1i(p.ground, 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, roadTex_);
        glUniform1i(p.road, 2);
        glActiveTexture(GL_TEXTURE0);
    }
}

void Renderer::draw(Battle& battle, const Camera& camera, double timeSeconds) {
    if (showUnits_ && unitsChecked_ != battle.world().units().size()) {
        bakeMissing(battle);
        unitsChecked_ = battle.world().units().size();
    }
    updatePatch(battle, camera);
    updateDetail(battle, camera, 2.5);  // ms a frame: a little at a time, so a phone never hitches
    const sim::World& world = battle.world();
    const float gameSeconds = (static_cast<float>(world.tick()) + battle.alpha()) / sim::kTicksPerSecond;
    uploadMarks(battle);
    uploadRemains(battle);
    uploadGates(battle);

    const V3 eye = camera.eye();
    const V3 fwd = (camera.target() - eye).normalized();
    const V3 camRight = fwd.cross({0, 0, 1}).normalized();
    std::vector<float> decals, sprites, arrows, particles, capsules, spheres, bars;
    const auto& units = world.units();
    const int selected = battle.selectedSquad();
    const float week = 86400.0f * 7.0f;

    // Each soldier's frame: dying, striking, hurt, walking or at ease.
    std::map<int, std::vector<float>> byGroup;
    if (walked_.size() != units.size()) {
        walked_.assign(units.size(), 0.0f);
        lastSeen_.resize(units.size());
        for (size_t i = 0; i < units.size(); ++i) lastSeen_[i] = battle.unitPosition(i);
    }
    const auto ago = [&](uint32_t tick) { return tick == UINT32_MAX ? 1e9f : gameSeconds - tick / static_cast<float>(sim::kTicksPerSecond); };
    for (size_t i = 0; i < (showUnits_ ? units.size() : 0); ++i) {
        const sim::Unit& u = units[i];
        const V3 pos = battle.unitPosition(i);
        const float* team = u.owner == Battle::kPlayer ? kPlayerColor : kEnemyColor;
        const float fx = static_cast<float>(u.facing.x.toDouble()), fy = static_cast<float>(u.facing.y.toDouble());
        const float height = static_cast<float>(u.stats.height.toDouble());
        const bool isSelected = selected >= 0 && battle.squadOf(u.id) == selected;
        const auto type = static_cast<UnitType>(u.stats.type);
        const V3 step = pos - lastSeen_[i];
        walked_[i] += std::min(5.0f, std::sqrt(step.x * step.x + step.y * step.y));
        lastSeen_[i] = pos;
        if (!u.alive() && u.gibbed) continue;  // blown apart: only the parts remain
        int frame;
        if (!u.alive()) {
            const float died = ago(battle.deathTick(i));
            const float decay = std::clamp((died - 3600.0f) / week, 0.0f, 1.0f);
            if (decay > 0 && type != UnitType::Catapult) {
                // Long dead: the body rots where it lies (a wrecked engine stays a heap of timber).
                const Look look = lookOf(type);
                pushCorpse(capsules, spheres, pos, std::atan2(fy, fx) + 3.14159f, height / 1.8f, {team[0], team[1], team[2]},
                           {look.body[0], look.body[1], look.body[2]}, look.plan < 0.5f, decay, u.id);
                continue;
            }
            const int f = u.airborne ? 2 : static_cast<int>(died / figures::kDieSeconds * figures::kFrames[4]);
            frame = figures::kFirstFrame[4] + std::clamp(f, 0, figures::kFrames[4] - 1);
        } else {
            const float radius = static_cast<float>(u.stats.radius.toDouble());
            decals.insert(decals.end(), {pos.x, pos.y, pos.z, 0, radius * 1.3f, 0, kDecalShadow, 0, 0, 0, 0});
            pushHealthBar(bars, u, pos, isSelected);
            if (isSelected) decals.insert(decals.end(), {pos.x, pos.y, pos.z, 0, radius * 1.8f, 0, kDecalRing, 0.4f, 0.9f, 1.0f, 0});
            const float struck = ago(battle.attackTick(i)), hurt = ago(battle.hitTick(i)), strike = figures::attackSeconds(type);
            if (struck >= 0 && struck < strike) {
                frame = figures::kFirstFrame[2] + std::min(figures::kFrames[2] - 1, static_cast<int>(struck / strike * figures::kFrames[2]));
            } else if (hurt >= 0 && hurt < figures::kFlinchSeconds) {
                frame = figures::kFirstFrame[3] + (hurt < figures::kFlinchSeconds * 0.5f ? 0 : 1);
            } else if (battle.unitMoving(i) || u.inAir()) {
                const float cycle = u.inAir() ? gameSeconds * 1.6f : walked_[i] / figures::stride(type);
                frame = figures::kFirstFrame[1] + static_cast<int>((cycle - std::floor(cycle)) * figures::kFrames[1]) % figures::kFrames[1];
            } else {
                const float cycle = gameSeconds * 0.45f + static_cast<float>(u.id % 13) * 0.37f;
                frame = static_cast<int>((cycle - std::floor(cycle)) * figures::kFrames[0]) % figures::kFrames[0];
            }
        }
        const int key = static_cast<int>(type) * 2 + (u.owner == Battle::kPlayer ? 0 : 1);
        if (!sheets_.count(key / 2) || !sheets_[key / 2].tex) continue;  // baked before the next frame (bakeMissing)
        std::vector<float>& g = byGroup[key];
        g.insert(g.end(), {pos.x, pos.y, pos.z, std::atan2(fy, fx), static_cast<float>(frame), isSelected ? 1.0f : 0.0f});
    }
    {
        std::vector<float> all;
        unitGroups_.clear();
        for (auto& [key, data] : byGroup) {
            unitGroups_.push_back({key, {all.size() / 6, data.size() / 6}});
            all.insert(all.end(), data.begin(), data.end());
        }
        uploadDynamic(units_, all);
    }

    battle.forEachProjectile([&](const sim::Projectile& p, const V3& pos, const V3& vel) {
        const float* team = p.owner == Battle::kPlayer ? kPlayerColor : kEnemyColor;
        const auto sprite = [&](float plan, float size, float style) {
            sprites.insert(sprites.end(), {pos.x, pos.y, pos.z, team[0], team[1], team[2], plan, 0, 0, 0, style, 0,
                                           0, 0, 1, 1, 1, size, 1, 0});
        };
        switch (p.kind) {
            case sim::ProjectileKind::Arrow:
                arrows.insert(arrows.end(), {pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, 0, 0});
                break;
            case sim::ProjectileKind::Bomb:
                sprite(kPlanBomb, 0.4f, 0);
                break;
            case sim::ProjectileKind::Fireball:
                sprite(kPlanOrb, 0.9f, 0);
                break;
            case sim::ProjectileKind::Stone: {
                // A dressed boulder, turning over in its flight.
                const float t = static_cast<float>(timeSeconds) * 3.0f + static_cast<float>(p.id);
                float q[4];
                yprQuat(t, t * 0.6f, 0, q);
                pushInstance(spheres, pos, q, {0.36f, 0.32f, 0.34f}, {0.62f, 0.6f, 0.56f});
                break;
            }
            case sim::ProjectileKind::Debris:
                if (p.remain == sim::RemainKind::Arrow) {
                    // Tumbling end over end.
                    const float spin = static_cast<float>(timeSeconds) * 12.0f + static_cast<float>(p.id);
                    arrows.insert(arrows.end(), {pos.x, pos.y, pos.z, std::cos(spin), std::sin(spin * 0.7f),
                                                 std::sin(spin), 0, 0});
                } else {
                    // Tumbling through the air.
                    const float t = static_cast<float>(timeSeconds) * 9.0f + static_cast<float>(p.id);
                    float q[4];
                    yprQuat(t, t * 0.7f, t * 1.3f, q);
                    pushPart(capsules, spheres, p.remain, pos, q, {team[0], team[1], team[2]}, 0);
                }
                break;
        }
    });

    // Body parts at rest on the ground.
    for (const sim::Remain& r : world.remains()) {
        if (r.kind == sim::RemainKind::None || r.kind == sim::RemainKind::Arrow) continue;
        const V3 pos = toV3(r.pos), dir = toV3(r.dir);
        const float* team = r.owner == Battle::kPlayer ? kPlayerColor : kEnemyColor;
        const float decay = std::clamp((gameSeconds - r.tick / 30.0f - 3600.0f) / week, 0.0f, 1.0f);
        float q[4];
        yprQuat(std::atan2(dir.y, dir.x), 0, (hash01(r.tick) - 0.5f) * 0.6f, q);
        pushPart(capsules, spheres, r.kind, pos, q, {team[0], team[1], team[2]}, decay);
    }

    for (const Particle& p : battle.particles()) {
        particles.insert(particles.end(), {p.pos.x, p.pos.y, p.pos.z, static_cast<float>(p.kind), p.age / p.life,
                                           p.size, 0});
    }

    uploadDynamic(dynamicDecals_, decals);
    uploadDynamic(sprites_, sprites);
    uploadDynamic(flyingArrows_, arrows);
    uploadDynamic(particles_, particles);
    uploadDynamic(bars_, bars);
    uploadMeshInstances(capsules_, capsules);
    uploadMeshInstances(spheres_, spheres);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    renderShadowMap(camera, timeSeconds, gameSeconds);
    glViewport(0, 0, static_cast<GLsizei>(camera.width()), static_cast<GLsizei>(camera.height()));
    glClearColor(0.64f, 0.74f, 0.84f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    if (features_ & kAtmosphere) {
        // The sky first, behind everything (no depth writes).
        glDepthMask(GL_FALSE);
        setCommon(sky_, camera, timeSeconds, gameSeconds);
        const V3 fwd = (camera.target() - camera.eye()).normalized();
        const V3 right = fwd.cross({0, 0, 1}).normalized();
        const V3 up = right.cross(fwd);
        glUniform3f(sky_.camFwd, fwd.x, fwd.y, fwd.z);
        glUniform3f(sky_.camUp, up.x, up.y, up.z);
        glUniform2f(sky_.proj, std::tan(Camera::kFovY / 2), camera.width() / camera.height());
        if (!skyVao_) glGenVertexArrays(1, &skyVao_);
        glBindVertexArray(skyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);
    }
    setCommon(terrain_, camera, timeSeconds, gameSeconds);
    glBindVertexArray(terrainVao_);
    glDrawElements(GL_TRIANGLES, terrainIndexCount_, GL_UNSIGNED_INT, nullptr);
    drawDetailTerrain(detailTerrain_, camera, timeSeconds, gameSeconds);
    drawCover(camera, timeSeconds, gameSeconds);

    // The rest of the continent, coarse, with its rivers and roads on top. The
    // land that meets the close-up land gives way to it, cut exactly at its edge.
    const bool farDetail = features_ & kFarDetail;
    const bool patchShown = farDetail && patchLand_.count > 0;
    const auto farProgram = [&](const Program& p, float sink, float patchSink) {
        setCommon(p, camera, timeSeconds, gameSeconds);
        glUniform4fv(p.ring, 1, ring_);
        glUniform1f(p.sink, sink);
        glUniform4fv(p.patch, 1, patch_);
        glUniform1f(p.patchSink, patchSink);
    };
    // The continent-wide land only needs the cut where no finer land sinks it out of the way.
    const auto covers = [&](const float* r) {
        return detailRect_[0] >= r[0] && detailRect_[1] >= r[1] && detailRect_[2] <= r[2] && detailRect_[3] <= r[3];
    };
    const bool farSunk = farDetail && (covers(ring_) || (patchShown && covers(patch_)));
    farProgram(detailOn_ && !farSunk ? farCut_ : far_, farDetail ? 6.0f : 0.0f, patchShown ? 6.0f : 0.0f);
    glBindVertexArray(farLand_.vao);
    glDrawElements(GL_TRIANGLES, farLand_.count, GL_UNSIGNED_INT, nullptr);
    if (ocean_.count > 0) {
        glBindVertexArray(ocean_.vao);
        glDrawElements(GL_TRIANGLES, ocean_.count, GL_UNSIGNED_INT, nullptr);
    }
    const Program& nearProgram = detailOn_ ? farCut_ : far_;
    if (farDetail) {
        farProgram(nearProgram, 0.0f, patchShown ? 6.0f : 0.0f);
        glBindVertexArray(nearLand_.vao);
        glDrawElements(GL_TRIANGLES, nearLand_.count, GL_UNSIGNED_INT, nullptr);
    }
    if (patchShown) {
        farProgram(nearProgram, 0.0f, 0.0f);
        glBindVertexArray(patchLand_.vao);
        glDrawElements(GL_TRIANGLES, patchLand_.count, GL_UNSIGNED_INT, nullptr);
    }
    if (ribbons_.count > 0) {
        setCommon(ribbon_, camera, timeSeconds, gameSeconds);
        glBindVertexArray(ribbons_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, ribbons_.count);
    }
    if (waterfalls_.count > 0 && farDetail) {
        setCommon(falls_, camera, timeSeconds, gameSeconds);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glBindVertexArray(waterfalls_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, waterfalls_.count);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Decals: blended, no depth writes. Ground marks first, then body parts, then per-frame.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    setCommon(decal_, camera, timeSeconds, gameSeconds);
    const bool realShadows = shadowInfo_[2] > 0;
    for (Batch* b : {&marks_, &sceneryShadows_, &dynamicDecals_}) {
        // Blob shadows under the battlefield's trees only while they stand (see sceneryNear in kMeshVs).
        if (b->count == 0 || (b == &sceneryShadows_ && (realShadows || camera.distance() > 850.0f))) continue;
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, b->count);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Solid 3D things: scenery, corpses and loose body parts.
    // From high up, undergrowth and small rocks are sub-pixel: skip them (phones).
    drawMeshes({&walls_, &towers_, &drums_, &keeps_, &stairs_, &banners_, &cityHouses_, &ruins_, &temples_, &sages_, &bridges_, &gates_, &brokenGates_}, camera, timeSeconds, gameSeconds);
    if (!showUnits_) drawMeshes({&ships_, &sails_, &piers_}, camera, timeSeconds, gameSeconds);
    // Distant woods: sub-pixel and covered by the terrain colour from very high up.
    if (camera.distance() < 6000.0f) {
        std::vector<MeshBatch*> woods;
        for (MeshBatch& b : copses_) woods.push_back(&b);
        if (features_ & kFarDetail)
            for (MeshBatch& b : nearCopses_) woods.push_back(&b);
        drawMeshes(woods, camera, timeSeconds, gameSeconds);
    }
    if (camera.distance() > 450.0f) {
        drawMeshes(scenery(false), camera, timeSeconds, gameSeconds);
    } else {
        drawMeshes(scenery(true), camera, timeSeconds, gameSeconds);
    }

    setCommon(arrow_, camera, timeSeconds, gameSeconds);
    for (Batch* b : {&remainArrows_, &flyingArrows_}) {
        if (b->count == 0) continue;
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, b->count);
    }

    if (sprites_.count > 0) {
        setCommon(sprite_, camera, timeSeconds, gameSeconds);
        glBindVertexArray(sprites_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, sprites_.count);
    }
    {
        const V3 toward = V3{fwd.x, fwd.y, 0}.normalized();
        drawUnits(camera, timeSeconds, gameSeconds, toward, camRight, false);
    }
    if (bars_.count > 0) {
        setCommon(bar_, camera, timeSeconds, gameSeconds);
        glBindVertexArray(bars_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, bars_.count);
    }

    // Water over everything below it: soldiers wading show through.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    setCommon(water_, camera, timeSeconds, gameSeconds);
    glBindVertexArray(waterVao_);
    glDrawArrays(GL_TRIANGLES, 0, waterVertices_);
    drawDetailWater(camera, timeSeconds, gameSeconds);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Particles last: premultiplied alpha, depth-tested but not written.
    if (particles_.count > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        setCommon(particle_, camera, timeSeconds, gameSeconds);
        glBindVertexArray(particles_.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, particles_.count);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    if (debugSheet_ >= 0) {
        // A sheet over the screen at its own pixels' shape, for looking at it.
        auto it = sheets_.find(debugSheet_);
        if (it == sheets_.end() || !it->second.tex) {
            const auto type = static_cast<UnitType>(debugSheet_);
            bakeSheet(sheets_[debugSheet_], type, figures::plan(type), 1.0f);
            it = sheets_.find(debugSheet_);
        }
        const Sheet& sh = it->second;
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        const float k = std::min(static_cast<float>(vp[2]) / sh.texW, static_cast<float>(vp[3]) / sh.texH);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(sheetView_.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sh.tex);
        glUniform1i(sheetView_.sheet, 0);
        const V3 cloth = figures::cloth(kPlayerColor);
        glUniform3f(sheetView_.cloth, cloth.x, cloth.y, cloth.z);
        glUniform4f(sheetView_.sheetView, 0, 1 - vp[3] / (k * sh.texH), vp[2] / (k * sh.texW), 1);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_DEPTH_TEST);
    }
    glBindVertexArray(0);
}

}  // namespace illyria::game
