#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

constexpr float kDecalBlood = 0, kDecalShadow = 1, kDecalRing = 2, kDecalCorpse = 3, kDecalCrater = 4,
                kDecalDust = 5, kDecalPart = 6;
constexpr float kPlanBomb = 9, kPlanOrb = 10, kPlanGib = 11;

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
                 {particle_, shaders::kParticleVs, shaders::kParticleFs}};
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

bool Renderer::init(Battle& battle) {
    if (!buildPrograms()) return false;
    makeBatch(marks_, {3, 4, 4}, Battle::kMaxGroundMarks);
    makeBatch(remainArrows_, {3, 3, 2}, Battle::kMaxRemains);
    makeBatch(remainParts_, {3, 4, 4}, Battle::kMaxRemains);
    makeBatch(dynamicDecals_, {3, 4, 4}, 0);
    makeBatch(flyingArrows_, {3, 3, 2}, 0);
    makeBatch(sprites_, {3, 3, 4, 4, 3, 2, 1}, 0);
    makeBatch(particles_, {3, 4}, 0);
    reset(battle);
    return true;
}

void Renderer::reset(Battle& battle) {
    marksUploaded_ = 0;
    marks_.count = 0;
    zeroBatch(remainArrows_, Battle::kMaxRemains);
    zeroBatch(remainParts_, Battle::kMaxRemains);
    remainArrows_.count = remainParts_.count = 0;
    battle.takeDirtyRemains();
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
    for (uint32_t slot : battle.takeDirtyRemains()) {
        if (slot >= remains.size()) continue;
        const sim::Remain& r = remains[slot];
        float arrow[kArrowFloats] = {0, 0, 0, 0, 0, 0, 0, 3};
        float part[kDecalFloats] = {};
        const V3 pos = toV3(r.pos), dir = toV3(r.dir);
        const float rest = r.tick * tickSeconds;
        if (r.kind == sim::RemainKind::Arrow) {
            const float state = dir.z < -0.2f ? 1.0f : 2.0f;
            const float a[kArrowFloats] = {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, rest, state};
            std::copy(a, a + kArrowFloats, arrow);
        } else if (r.kind != sim::RemainKind::None) {
            const float yaw = std::atan2(dir.y, dir.x);
            const float size = r.kind == sim::RemainKind::Head ? 0.17f : 0.3f;
            const float p[kDecalFloats] = {pos.x, pos.y, pos.z, yaw, size, rest, kDecalPart,
                                           static_cast<float>(r.kind), static_cast<float>(r.owner), 0,
                                           static_cast<float>(slot % 997) / 997.0f};
            std::copy(p, p + kDecalFloats, part);
        }
        glBindBuffer(GL_ARRAY_BUFFER, remainArrows_.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof arrow), sizeof arrow, arrow);
        glBindBuffer(GL_ARRAY_BUFFER, remainParts_.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof part), sizeof part, part);
    }
    remainArrows_.count = remainParts_.count = static_cast<GLsizei>(remains.size());
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

void Renderer::draw(Battle& battle, const Camera& camera, double timeSeconds) {
    const sim::World& world = battle.world();
    const float gameSeconds = (static_cast<float>(world.tick()) + battle.alpha()) / sim::kTicksPerSecond;
    uploadMarks(battle);
    uploadRemains(battle);

    const V3 eye = camera.eye();
    const V3 fwd = (camera.target() - eye).normalized();
    const V3 camRight = fwd.cross({0, 0, 1}).normalized();
    std::vector<float> decals, sprites, arrows, particles;
    const auto& units = world.units();
    const int selected = battle.selectedSquad();

    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        const V3 pos = battle.unitPosition(i);
        const float* team = u.owner == Battle::kPlayer ? kPlayerColor : kEnemyColor;
        const float fx = static_cast<float>(u.facing.x.toDouble()), fy = static_cast<float>(u.facing.y.toDouble());
        const float height = static_cast<float>(u.stats.height.toDouble());
        const bool isSelected = selected >= 0 && battle.squadOf(u.id) == selected;
        if (!u.alive() && !u.airborne) {
            const float died = battle.deathTick(i) / static_cast<float>(sim::kTicksPerSecond);
            decals.insert(decals.end(), {pos.x, pos.y, pos.z, std::atan2(fy, fx) + 3.14159f, height * 0.55f, died,
                                         kDecalCorpse, team[0], team[1], team[2], 0});
            continue;
        }
        const float radius = static_cast<float>(u.stats.radius.toDouble());
        decals.insert(decals.end(), {pos.x, pos.y, pos.z, 0, radius * 1.3f, 0, kDecalShadow, 0, 0, 0, 0});
        if (isSelected) decals.insert(decals.end(), {pos.x, pos.y, pos.z, 0, radius * 1.8f, 0, kDecalRing, 0.4f, 0.9f, 1.0f, 0});
        const Look look = lookOf(static_cast<UnitType>(u.stats.type));
        const float facingSide = fx * camRight.x + fy * camRight.y;
        sprites.insert(sprites.end(), {pos.x, pos.y, pos.z, team[0], team[1], team[2], look.plan,
                                       battle.unitMoving(i) ? 1.0f : 0.0f, facingSide < 0 ? 1.0f : 0.0f,
                                       static_cast<float>(u.id % 17), look.weapon, look.head, look.shield, look.flags,
                                       look.body[0], look.body[1], look.body[2], height, look.width,
                                       isSelected ? 1.0f : 0.0f});
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
            case sim::ProjectileKind::Debris:
                if (p.remain == sim::RemainKind::Arrow) {
                    // Tumbling end over end.
                    const float spin = static_cast<float>(timeSeconds) * 12.0f + static_cast<float>(p.id);
                    arrows.insert(arrows.end(), {pos.x, pos.y, pos.z, std::cos(spin), std::sin(spin * 0.7f),
                                                 std::sin(spin), 0, 0});
                } else {
                    sprite(kPlanGib, p.remain == sim::RemainKind::Head ? 0.35f : 0.55f, static_cast<float>(p.remain));
                }
                break;
        }
    });

    for (const Particle& p : battle.particles()) {
        particles.insert(particles.end(), {p.pos.x, p.pos.y, p.pos.z, static_cast<float>(p.kind), p.age / p.life,
                                           p.size, 0});
    }

    uploadDynamic(dynamicDecals_, decals);
    uploadDynamic(sprites_, sprites);
    uploadDynamic(flyingArrows_, arrows);
    uploadDynamic(particles_, particles);

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

    // Decals: blended, no depth writes. Ground marks first, then body parts, then per-frame.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    setCommon(decal_, camera, timeSeconds, gameSeconds);
    for (Batch* b : {&marks_, &remainParts_, &dynamicDecals_}) {
        if (b->count == 0) continue;
        glBindVertexArray(b->vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, b->count);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

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
    glBindVertexArray(0);
}

}  // namespace illyria::game
