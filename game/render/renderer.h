#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "meshes.h"

namespace illyria::game {

// OpenGL ES 3.0 / WebGL 2 renderer for a battle. Everything is drawn with a
// handful of instanced draw calls so tens of thousands of soldiers, arrows,
// remains and stains stay cheap on phones.
class Renderer {
public:
    // Graphics settings. Everything here is cosmetic: none of it changes the
    // simulation, so players on different settings stay in lockstep.
    enum Feature : int {
        kGroundDetail = 1,   // grass, soil, flowers and stones painted into the ground; shade under trees
        kGroundCover = 2,    // grass tufts, flowers, pebbles and twigs around the camera
        kShadows = 4,        // sun shadows
        kShadowsHigh = 8,    // sharper, wider sun shadows
        kFarDetail = 16,     // finer land around the battlefield, detailed far terrain
        kAtmosphere = 32,    // sky, haze and drifting cloud shadows
        kCoverDense = 64,    // denser, wider ground cover
    };
    static constexpr int kHigh = kGroundDetail | kGroundCover | kShadows | kShadowsHigh | kFarDetail | kAtmosphere | kCoverDense;
    static constexpr int kMedium = kGroundDetail | kGroundCover | kShadows | kFarDetail | kAtmosphere;
    static constexpr int kLow = 0;
    void setFeatures(int features);
    int features() const { return features_; }

    bool init(Battle& battle);
    // Forget uploaded remains and marks (call after the battle is replaced).
    void reset(Battle& battle);
    void draw(Battle& battle, const Camera& camera, double timeSeconds);

private:
    struct Program {
        GLuint id = 0;
        GLint viewProj = -1, eye = -1, heights = -1, cells = -1, cellSize = -1;
        GLint time = -1, gameSeconds = -1, camDist = -1, pxScale = -1, camRight = -1;
        GLint water = -1, sway = -1, ground = -1, road = -1, waterTex = -1, hole = -1;
        GLint features = -1, shadowMap = -1, lightViewProj = -1, shadowInfo = -1, focus = -1, canopy = -1, cover = -1;
        GLint ring = -1, sink = -1, patch = -1, patchSink = -1, camFwd = -1, camUp = -1, proj = -1, flow = -1;
    };
    // An instanced mesh: shared vertices plus a per-instance buffer of
    // position (3), rotation quaternion (4), scale (3), tint (3).
    struct MeshBatch {
        GLuint vao = 0, vertexVbo = 0, instanceVbo = 0;
        GLsizei vertices = 0, count = 0;
        size_t capacity = 0;
        float sway = 0;
    };
    // An instanced batch: a VAO with its per-instance buffer bound.
    struct Batch {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        size_t capacity = 0;
        int floats = 0;
    };

    bool buildPrograms();
    void ensureShadowMap();
    void uploadCanopy(const Battle& battle);
    void makeBatch(Batch& b, std::initializer_list<int> layout, size_t capacity);
    void zeroBatch(Batch& b, size_t count);
    void uploadDynamic(Batch& b, const std::vector<float>& data);
    void uploadTerrain(const Battle& battle);
    void uploadMarks(const Battle& battle);
    void uploadRemains(Battle& battle);
    void setCommon(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds);
    void makeMeshBatch(MeshBatch& b, const Mesh& mesh, float sway);
    void uploadMeshInstances(MeshBatch& b, const std::vector<float>& data);
    void uploadScenery(const Battle& battle);
    void uploadContinent(const Battle& battle);
    void uploadSettlements(const Battle& battle);
    void drawMeshes(const std::vector<MeshBatch*>& batches, const Camera& camera, double timeSeconds, float gameSeconds);

    Program sky_, terrain_, decal_, arrow_, sprite_, particle_, mesh_, water_, far_, ribbon_, cover_, meshDepth_, terrainDepth_;
    // A ground-cover mesh drawn on a camera-following grid (see kCoverVs).
    struct CoverLayer {
        GLuint vao = 0, vbo = 0;
        GLsizei vertices = 0;
        float kind = 0, seed = 0;
    };
    CoverLayer cover_layers_[5];
    void makeCoverLayer(CoverLayer& l, const Mesh& mesh, float kind, float seed);
    void drawCover(const Camera& camera, double timeSeconds, float gameSeconds);
    void renderShadowMap(const Camera& camera, double timeSeconds, float gameSeconds);
    GLuint terrainVao_ = 0, terrainVbo_ = 0, terrainIbo_ = 0, heightTex_ = 0, groundTex_ = 0, roadTex_ = 0;
    GLuint waterVao_ = 0, waterVbo_ = 0, waterTex_ = 0, flowTex_ = 0;
    GLsizei waterVertices_ = 0;
    // Land beyond the battlefield: see buildLand().
    struct LandMesh {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei count = 0;
    };
    LandMesh farLand_, nearLand_;
    // Close up anywhere off the battlefield: the land every 8 m around where
    // the camera looks, sampled a few rows a frame (the coarse land sinks beneath it).
    LandMesh patchLand_;
    FarGrid patchBuild_;
    int patchRow_ = -1;          // next row to sample, or -1 when idle
    V3 patchCentre_{1e9f, 1e9f, 0};
    float patch_[4] = {1e9f, 1e9f, -1e9f, -1e9f};  // the shown patch's extent, inside its skirt
    void updatePatch(const Battle& battle, const Camera& camera);
    GLuint skyVao_ = 0;
    float ring_[4] = {0, 0, 0, 0};  // the fine ring's extent (min x, min y, max x, max y)
    void buildLand(const Battle& battle, const FarGrid& grid, LandMesh& out, bool skirt);
    float waterLevel_ = 0;
    GLsizei terrainIndexCount_ = 0;
    int cells_ = 0;
    float cellSize_ = 1;

    Batch ribbons_, marks_, remainArrows_, dynamicDecals_, flyingArrows_, sprites_, particles_, sceneryShadows_;
    MeshBatch houses_, pillars_, capsules_, spheres_;
    // Several shapes per kind of scenery (meshes::pineVariants() and so on).
    std::vector<MeshBatch> pines_, oaks_, bushes_, rocks_;
    MeshBatch stumps_;
    std::vector<MeshBatch*> scenery(bool includeSmall);
    // Cities, castles and the places heroes explore, across the whole continent.
    MeshBatch walls_, towers_, banners_, cityHouses_, ruins_, temples_, sages_;
    MeshBatch logs_;
    std::vector<MeshBatch> copses_, nearCopses_;  // woods beyond the battlefield, by variant
    uint64_t marksUploaded_ = 0;

    int features_ = kHigh;
    GLuint shadowTex_ = 0, shadowFbo_ = 0, canopyTex_ = 0, blankShadowTex_ = 0;
    int shadowSize_ = 0;
    M4 lightViewProj_;
    float shadowInfo_[4] = {0, 0, 0, 0};
    const M4* viewProjOverride_ = nullptr;  // the shadow pass renders from the sun
};

}  // namespace illyria::game
