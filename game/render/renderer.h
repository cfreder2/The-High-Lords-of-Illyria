#pragma once

#include "gl.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/game/detail_land.h"
#include <map>

#include "figures.h"
#include "meshes.h"
#include "sprite_clips.h"
#include "sprite_layout.h"

namespace illyria::game {

// The campaign's soldiers are grouped for drawing past the battle's, by type and
// by side (a faction 0-7, or 8 for a town's own garrison), in the faction's own colour.
constexpr int kMapKeyBase = 1 << 20, kMapSides = 9;
float manPulse(uint32_t id, float seconds);  // how hard a man in a fight flashes as a dot (renderer_map.cpp)
int manSide(uint8_t owner);

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
    // The campaign map shows the land, not the battle's soldiers.
    // Builds all of the close-up land around the camera now (test hook; it is
    // otherwise built a little each frame).
    void finishDetail(const Battle& battle, const Camera& camera) { updateDetail(battle, camera, 1e9, true); }
    void setShowUnits(bool show) {
        showUnits_ = show;
        unitsChecked_ = SIZE_MAX;
    }
    // Campaign map dressing, in battle-local metres, drawn only while the map
    // is shown: boats on the water (a sail in the side's colour) and piers at
    // the ports. scale: the boat's half length, the pier's length.
    struct Prop {
        V3 pos;
        float yaw = 0, scale = 1;
        V3 tint{1, 1, 1};
    };
    void setBoats(const std::vector<Prop>& boats);
    void setPiers(const std::vector<Prop>& piers);
    // Flags on poles (renderer_flags.cpp), in battle-local metres, drawn in
    // both views: the siege's capture point, the banners rising over the
    // campaign's towns. raised: how far up the pole the cloth is (0 to 1).
    struct Flag {
        V3 base;
        float height = 10, raised = 1, size = 3;
        V3 colour{1, 1, 1};
    };
    void setFlags(const std::vector<Flag>& flags);
    // The sandbox (mode_sandbox.h): these soldiers play clip `clip`
    // (sprites::Clip) from now, in game time (so pausing, slow motion and the
    // frame step hold it too), once or over and over; -1 hands them back to
    // what they are doing. A clip played once ends where it began, except a
    // death, which stays on its last frame.
    void playClip(const std::vector<sim::EntityId>& ids, int clip, bool loop);
    // The same by name: one of the sheet's own clips (sprite_clips.h), or
    // what stands in for it (fallbackOf); "" hands them back.
    void playClip(const std::vector<sim::EntityId>& ids, const std::string& clip, bool loop);
    // The clips a type's sheet plays, by name: its own, or the fixed layout's eight.
    std::vector<std::string> clipNames(int type) const;
    // The clip a type's sheet plays for `name`: itself, else what stands in for it ("" none).
    std::string clipPlayed(int type, std::string_view name) const;
    // How many of the men were drawn as sprites and as dots in the last frame (tests).
    size_t mapSprites() const { return men_.sprites; }
    int mapDots() const { return dots_.count; }
    // The castles' tower banners in each town's holder's colour (by city index).
    void setCityColours(const std::vector<V3>& colours);
    // The campaign's territory, one RGBA texel per hex (cols x rows); strength 0 hides it.
    void setTerritory(const std::vector<uint8_t>& rgba, int cols, int rows, float strength);

private:
    bool showUnits_ = true;
    struct Program {
        GLuint id = 0;
        GLint viewProj = -1, eye = -1, heights = -1, cells = -1, cellSize = -1;
        GLint time = -1, gameSeconds = -1, camDist = -1, pxScale = -1, camRight = -1;
        GLint water = -1, sway = -1, ground = -1, road = -1, waterTex = -1, hole = -1;
        GLint features = -1, shadowMap = -1, lightViewProj = -1, shadowInfo = -1, focus = -1, canopy = -1, cover = -1;
        GLint ring = -1, sink = -1, patch = -1, patchSink = -1, masonry = -1, camFwd = -1, camUp = -1, proj = -1, flow = -1;
        GLint bakeRight = -1, bakeUp = -1, bakeToward = -1, bakeRect = -1, scratch = -1, resolve = -1;
        GLint lightDepth = -1, lightRight = -1, lightUp = -1, lightToward = -1, lightRect = -1, lightRange = -1, sheetView = -1;
        GLint cellTable = -1, cloth = -1, sheetSize = -1, toViewer = -1, lift = -1, team = -1, sheet = -1, framesPerView = -1;
        GLint wrap = -1, detailMask = -1, detailRect = -1, detailFade = -1, lod = -1, decalGrid = -1, flag = -1;
        GLint territory = -1, territoryInfo = -1;
        GLint albedo = -1, viewport = -1, dotPx = -1, rim = -1;
    };
    // An instanced mesh: shared vertices plus a per-instance buffer of
    // position (3), rotation quaternion (4), scale (3), tint (3).
    struct MeshBatch {
        GLuint vao = 0, vertexVbo = 0, instanceVbo = 0;
        GLsizei vertices = 0, count = 0;
        size_t capacity = 0;
        float sway = 0;
        bool masonry = false;
        // 1: individual scenery, drawn near the camera; -1: the woods' clumps
        // that stand in for it beyond (see sceneryNear in kMeshVs); 0: always.
        float lod = 0;
        bool cloth = false;  // a banner's waving cloth (uFlag): the castles' and the flags'
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
    void uploadGates(const Battle& battle);
    void drawMeshes(const std::vector<MeshBatch*>& batches, const Camera& camera, double timeSeconds, float gameSeconds);

    Program sky_, terrain_, decal_, arrow_, sprite_, particle_, mesh_, water_, far_, ribbon_, falls_, cover_, meshDepth_, terrainDepth_;
    Program figure_, resolve_, unit_, figureDepth_, sheetView_;
    Program bar_;  // health bars with veterans' shield pips
    Batch bars_;
    // Men at every distance (renderer_map.cpp): those too far to be sprites are drawn as dots (dot_, dots_:
    // position, colour, whether selected and how hard they flash in a fight), those near the camera as the
    // battle's sprites; the frame's level of detail is worked out once (beginMen) for the whole pass.
    Program dot_;
    Batch dots_;
    struct MenLod {
        bool near = false, looked = false;
        V3 at;
        float reach = 0, spriteDot = 1, lift = 1, seconds = 0, dt = 0.016f;
        M4 vp;
        std::vector<float> dots;
        std::vector<sim::UnitType> need;
        std::vector<V3> selected;
        std::vector<std::pair<int, V3>> seen;  // the sprites' types and places, for sizing the close-up sheets next frame
        size_t sprites = 0;
    } men_;
    void beginMen(const Battle& battle, const Camera& camera, float seconds);
    bool manOnScreen(const V3& p) const;
    bool manIsSprite(const V3& p, int type);
    void manDot(const V3& p, const V3& colour, bool owned, bool selected, bool inspected, float pulse, bool alsoSprite);
    // One man, drawn by one function whoever he is: a unit of the world presented (his sim state to hand). A dot or the
    // battle's sprite by the level of detail, his shadow, his ring, his frame, his place in the sheet's group.
    struct ManView {
        const sim::Unit* unit = nullptr;  // the world's man
        size_t index = 0;                 // his place among the world's units (for the boxes picking uses)
        uint32_t id = 0;
        V3 pos;
        float yaw = 0, speed = 0, walked = 0, moved = 0;
        float struck = 1e9f, hurt = 1e9f, died = 1e9f;  // seconds since he struck, was hurt, died
        int type = 0;
        uint8_t owner = 0;
        bool alive = true, selected = false, inspected = false, aloft = false, moving = false;
    };
    struct ManPass {
        const Battle& battle;
        std::map<int, std::vector<float>>& groups;
        std::vector<float>& decals;
        std::vector<float>& arrows;
        std::vector<float>& bars;
        std::vector<float>& boxes;
        V3 camRight;
        float toViewer = 0, gameSeconds = 0, sinceLastDraw = 0;
        std::function<uint32_t()> won;
    };
    void drawMan(const ManView& v, ManPass& pass);
    void endMen(const Battle& battle, const Camera& camera, std::vector<float>& decals);
    void drawDots(const Camera& camera, double timeSeconds, float gameSeconds);
    float mapDotPx_ = 4;  // a dot's width, in device pixels
    // The soldiers' sprite sheets, one per unit type and side, baked on the
    // GPU from figures::build when a battle first needs them.
    struct Sheet {
        GLuint tex = 0, cells = 0;  // the pictures; where each cell is (see kUnitVs)
        int texW = 0, texH = 0;
        float lift = 0.5f;
        bool prerendered = false;  // loaded from tools/blender's sheets, not baked
        std::vector<float> table;  // the cells, as uploaded (for readSheet)
        // The layout (sprite_layout.h): 26 frames a view and no gibs for the old one.
        int framesPerView = figures::kFramesPerView, cellCount = figures::kViews * figures::kFramesPerView;
        sprites::SheetClips clips;  // its own clips, if it lists them (sprite_clips.h): played by name
        float ppm = figures::kPixelsPerMetre;  // as uploaded (less if halved)
        double fullTexels = 0;                 // before any halving
    };
    std::map<int, Sheet> sheets_;  // by unit type
    GLuint scratchFbo_ = 0, scratchTex_ = 0, scratchDepth_ = 0, sheetFbo_ = 0, figureVao_ = 0, figureVbo_ = 0, emptyVao_ = 0;
    GLuint lightFbo_ = 0, lightTex_ = 0;  // the figure's own shadow map while baking
    static constexpr int kScratch = 512, kSheetMax = 2048, kGutter = 2;
    // Texels all the sheets may use (four bytes each, plus mipmaps).
    static constexpr double kSheetBudget = 26e6;
    double sheetTexels_ = 0;
    void bakeSheet(Sheet& s, sim::UnitType type, const figures::Plan& plan, float scale);
    int debugSheet_ = -1;  // a sheet drawn over the screen, for looking at it
public:
    // 3D models made by tools/models (renderer_models.cpp), fetched by the page
    // (game/web/models.cpp): the ruin. Until one arrives, or if it fails, its
    // sites are drawn with the box model built in code (meshes::ruin).
    enum class ModelKind : int { Ruin = 0 };
    // A fetched model: its mesh file (tools/models/ruin.py describes it) and
    // its texture, RGBA rows top first. False if malformed.
    bool loadModel(ModelKind kind, const uint8_t* mesh, size_t meshBytes, const uint8_t* rgba, int width, int height);
    bool modelLoaded(ModelKind kind) const { return kind == ModelKind::Ruin && ruinModel_.vao != 0; }
    // For before/after comparisons: off, the sites are drawn as before the models.
    void enableModels(bool on) {
        modelsOn_ = on;
        uploadRuins(ruinInstances_);
    }
private:
    bool modelsOn_ = true;
    // An instanced textured mesh: indexed vertices (position, normal, uv)
    // and a per-instance buffer laid out as a MeshBatch's (kModelVs), filled
    // each pass with the instances in view (the model is heavy, and drawn
    // at every site on the continent).
    struct Model {
        GLuint vao = 0, vertexVbo = 0, indexVbo = 0, instanceVbo = 0, tex = 0;
        GLsizei indices = 0;
        size_t capacity = 0;
        std::vector<float> instances, visible;  // all of them; those in the pass's view
    };
    Program model_, modelDepth_;
    Model ruinModel_;
    std::vector<float> ruinInstances_;  // the sites' ruins, for whichever draws them
    void uploadRuins(const std::vector<float>& instances);
    void drawModel(const Program& p, Model& m, const Camera& camera, double timeSeconds, float gameSeconds);
public:
    void showSheet(int type) { debugSheet_ = type; }
    // Pre-rendered sheets (tools/blender, renderer_sheets.cpp): the first time a
    // battle needs a type, request(type) is called so the page can fetch its
    // sheet; until it arrives, or if there is none, the type is baked as before.
    void setSheetRequest(std::function<void(int)> request) { sheetRequest_ = std::move(request); }
    // A fetched sheet: RGBA rows top first (as image files store them), and
    // cellCount cells of 8 floats (x, y from the bottom, w, h in pixels; u0, v0, w,
    // h in metres), framesPerView frames a view (sprite_layout.h: 26 the old
    // layout, 44 the new, gib cells after the views'; or as many as `clips`
    // list, for a sheet of its own clips). Replaces the type's baked sheet,
    // or with closeup its close-up sheet. False if malformed.
    bool loadSheet(int type, const uint8_t* rgba, int width, int height, const float* cells, int cellCount = sprites::kLegacyCells,
                   int framesPerView = sprites::kLegacyFramesPerView, bool closeup = false,
                   const sprites::SheetClips& clips = sprites::SheetClips{});
    // Close-up sheets (closeup.h, renderer_sheets.cpp): the page lists the types
    // that have one (its size), the renderer asks for it with request(type)
    // when the camera comes close to that type and drops it when it pulls back.
    void listCloseup(int type, int width, int height, float ppm);
    void setCloseupRequest(std::function<void(int)> request) { closeupRequest_ = std::move(request); }
    // The screen height, in pixels, sheets should look sharp at (the canvas's
    // CSS height times the device pixel ratio, at most two); 0: the camera's.
    void setCloseupScreen(float px) { closeupScreen_ = px; }
    // The closest the camera should come to its target now, so the sheets of
    // the soldiers there are never magnified past closeup::kMaxMagnification.
    float closeupMinDistance() const { return closeupMinDistance_; }
    // For tests: the close-up sheets held ("Name=WxH", comma separated).
    std::string closeupReport() const;
    // For before/after comparisons: off, no close-up sheets and no zoom limit (as before them).
    void enableCloseups(bool on) { closeupsOn_ = on; }
    // A type's sheet read back (baked first if need be), for tests and tools:
    // RGBA bottom row first, and the cells as uCellTable holds them.
    bool readSheet(int type, std::vector<uint8_t>& rgba, int& width, int& height, std::vector<float>& table);
    // The sheets held now, for tests: "Name=pre" (tools/blender) or "Name=baked", comma separated.
    std::string sheetReport() const;
    // One type's sheet: 1 pre-rendered, 0 baked, -1 none yet.
    int sheetSource(int type) const;
private:
    std::function<void(int)> sheetRequest_;
    std::set<int> sheetsRequested_;
    double prerenderedTexels_ = 0;  // of the loaded sheets, against kSheetBudget
    void uploadSheet(Sheet& sh, const uint8_t* rgba, const float* table);
    // Close-up sheets by type, those the page has, and their texels.
    std::map<int, Sheet> closeups_;
    struct Listed {
        double texels = 0;
        float ppm = 0;
    };
    std::map<int, Listed> closeupListed_;
    std::set<int> closeupsRequested_;
    std::function<void(int)> closeupRequest_;
    float closeupScreen_ = 0, closeupMinDistance_ = Camera::kMinDistance;
    bool closeupsOn_ = true;
    void updateCloseups(const Battle& battle, const Camera& camera);
    void dropSheet(Sheet& sh, bool closeup);
    // The sheet a type is drawn with now: its close-up one if held.
    const Sheet& sheetFor(int type);
    // Frame i of clip c in the cell table of the type's sheet now (falling back
    // to the clips an old sheet has); the dying soldier's frame by how he died.
    int clipFrame(int type, sprites::Clip c, int i);
    // The clips of the sheet a type is drawn with now, if it lists its own; else null.
    const sprites::SheetClips* clipsFor(int type) const;
    // The frame of a clip a soldier is made to play (playClip), or -1 when none is (or it ended).
    // `down`, if given: how far he has fallen, 0 standing .. 1 lying (a death clip).
    int playedFrame(int type, sim::EntityId id, float gameSeconds, float* down = nullptr);
    struct Played {
        std::string clip;  // by name: a fixed layout's clip, or one of a sheet's own
        bool loop = false;
        float start = 0;
    };
    std::map<sim::EntityId, Played> played_;
    float gameSeconds_ = 0;  // at the last draw
    int deathFrame(int type, const sim::Unit& u, float secondsDead, float* down = nullptr);
    // How long his fall takes: figures::kDieSeconds, or his sheet's own clip's.
    float deathSeconds(int type, const sim::Unit& u) const;
    void bakeMissing(const Battle& battle);
    void bakeTypes(const std::vector<sim::UnitType>& need);
    // Body parts drawn as pieces of their soldier's own picture (the sheet's
    // gib cells, sprite_layout.h; renderer_gibs.cpp), where his sheet has
    // them; the rest stay 3D (pushPart). Each flying part, by projectile id,
    // and each landed one, by remain slot, keeps the type it came from, its
    // cell, mirrored or not, and its turn in the picture's plane.
    struct GibLook {
        int type = -1, cell = -1;
        bool mirror = false, seen = false;
        float angle = 0, spin = 0;
        sim::RemainKind kind = sim::RemainKind::None;
        sim::PlayerId owner = 0;
        V3 pos;
    };
    std::map<sim::EntityId, GibLook> debrisLooks_;
    std::vector<GibLook> remainLooks_;
    std::vector<GibLook> landed_;              // parts that stopped flying this frame, for the remains they became
    std::map<sim::EntityId, int> gibChunks_;  // gobbets given out per blown-apart soldier: the first two are his weapon and shield
    bool hasGibCell(int type, int cell) const;
    // Follows the flying parts (before uploadRemains, which hands their looks to the remains they become).
    void trackDebris(const Battle& battle, float dt);
    GibLook gibLookFor(const Battle& battle, const sim::Projectile& p);
    GibLook restingLook(const Battle& battle, const sim::Remain& r, uint32_t slot);
    // The instance (units_ layout) drawing a part, into its sheet's group; false if it has no cell.
    bool pushGib(std::map<int, std::vector<float>>& groups, const GibLook& look, const V3& pos);
    // Soldiers to draw this frame, by sheet: feet (3), facing, frame, selected, down.
    // A body part: its lowest point, its turn, -(gib cell + 1), 0, mirrored (kUnitVs).
    Batch units_;
    std::vector<std::pair<int, std::pair<size_t, size_t>>> unitGroups_;  // type x 2 + side, first instance, count
    void drawUnits(const Camera& camera, double timeSeconds, float gameSeconds, V3 viewDir, V3 right, bool shadow);
    // Presentation per soldier: metres walked, for the stride; and, for a
    // sheet of its own clips, his pace, gait, stride and deeds as last seen.
    std::vector<float> walked_;
    std::vector<V3> lastSeen_;
    std::vector<sprites::ClipMemory> clipMemory_;
    std::vector<uint32_t> seenGen_;  // World::generation of each place, as last drawn
    // A living soldier's frame on a sheet of its own clips (sprite_clips.h chooseFrame):
    // `moved` metres since the last frame, `dt` game seconds; `victors` the sides that have won.
    int listedFrame(const Battle& battle, size_t i, const sprites::SheetClips& clips, float gameSeconds, float moved, float dt,
                    uint32_t victors);
    size_t unitsChecked_ = SIZE_MAX;  // the unit count sheets were last checked for
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
    LandMesh ocean_;  // open sea beyond the edges of the map
    // Close up anywhere off the battlefield: the land every 8 m around where
    // the camera looks, sampled a few rows a frame (the coarse land sinks beneath it).
    LandMesh patchLand_;
    FarGrid patchBuild_;
    int patchRow_ = -1;          // next row to sample, or -1 when idle
    V3 patchCentre_{1e9f, 1e9f, 0};
    float patch_[4] = {1e9f, 1e9f, -1e9f, -1e9f};  // the shown patch's extent, inside its skirt
    void updatePatch(const Battle& battle, const Camera& camera);
    // Close up anywhere off the battlefield, the land at the battlefield's own
    // detail (DetailLand), drawn by the terrain, cover and water programs with
    // its own repeating textures (renderer_detail.cpp); the coarse land and the
    // ribbons give way where it is shown.
    DetailLand detail_;
    Program detailTerrain_, detailDepth_, farCut_;
    GLuint detailTex_[6] = {0, 0, 0, 0, 0, 0};  // heights, ground, water, flow, road, canopy
    GLuint detailMask_ = 0, detailVao_ = 0, detailVbo_ = 0, detailIbo_ = 0, detailInst_ = 0;
    GLuint detailWaterVao_ = 0, detailWaterVbo_ = 0, detailWetInst_ = 0;
    GLsizei detailIndices_ = 0, detailWaterIndices_ = 0, detailChunks_ = 0, detailWetChunks_ = 0;
    uint32_t detailGen_ = UINT32_MAX;
    float detailRect_[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    float detailFade_ = 0;   // by the camera's height (see detailWeight in kCommon)
    std::vector<float> chunkFade_;  // per slot: fading in once finished
    double detailClock_ = -1;
    bool detailOn_ = false;  // drawn this frame
    // The close-up land's trees, undergrowth and rocks, near the camera.
    std::vector<MeshBatch> detailPines_, detailOaks_, detailBushes_, detailRocks_;
    MeshBatch detailLogs_, detailStumps_;
    uint32_t detailSceneryGen_ = UINT32_MAX;
    V3 detailSceneryAt_{1e9f, 1e9f, 0};
    double detailSceneryClock_ = 0;
    void updateDetailScenery(const Battle& battle, const Camera& camera, double now, bool instant);
    void resetDetail(const Battle& battle);
    void updateDetail(const Battle& battle, const Camera& camera, double budgetMs, bool instant = false);
    void bindDetail(const Program& p);
    void drawDetailTerrain(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds);
    void drawDetailWater(const Camera& camera, double timeSeconds, float gameSeconds);
    GLuint skyVao_ = 0;
    float ring_[4] = {0, 0, 0, 0};  // the fine ring's extent (min x, min y, max x, max y)
    void buildLand(const Battle& battle, const FarGrid& grid, LandMesh& out, bool skirt);
    float waterLevel_ = 0;
    GLsizei terrainIndexCount_ = 0;
    int cells_ = 0;
    float cellSize_ = 1;

    Batch ribbons_, waterfalls_, marks_, remainArrows_, dynamicDecals_, flyingArrows_, sprites_, particles_, sceneryShadows_;
    MeshBatch houses_, pillars_, capsules_, spheres_;
    // Several shapes per kind of scenery (meshes::pineVariants() and so on).
    std::vector<MeshBatch> pines_, oaks_, bushes_, rocks_;
    MeshBatch stumps_;
    std::vector<MeshBatch*> scenery(bool includeSmall);
    // Trees, undergrowth, rocks and logs as instances, by mesh variant.
    struct SceneryLists {
        std::vector<std::vector<float>> pine, oak, bush, rock;
        std::vector<float> logs, stumps;
    };
    static uint32_t sceneryId(const Battle& battle, const sim::Obstacle& o);
    // Adds one; returns the radius of its blob shadow.
    static float pushScenery(const sim::Obstacle& o, uint32_t id, SceneryLists& out);
    // Cities, castles and the places heroes explore, across the whole continent.
    MeshBatch walls_, parapets_, towers_, banners_, cityHouses_, ruins_, temples_, sages_;
    MeshBatch drums_, keeps_, stairs_, galleries_;
    MeshBatch gates_, brokenGates_;
    std::vector<uint8_t> gatesShown_;  // broken flags last uploaded
    MeshBatch logs_;
    // Flags (renderer_flags.cpp): poles and their cloth, drawn in both views;
    // the castles' tower banners, retinted with whoever holds each town; territory.
    MeshBatch poles_, cloths_;
    std::vector<float> bannerData_;
    std::vector<uint32_t> bannerCity_;
    GLuint territoryTex_ = 0;
    float territoryInfo_[4] = {0, 0, 0, 0};
    MeshBatch ships_, sails_, piers_;  // the campaign's boats and ports
    MeshBatch bridges_;  // every bridge on the continent, one world-space mesh
    std::vector<MeshBatch> copses_, nearCopses_;  // woods beyond the battlefield, by variant
    uint64_t marksUploaded_ = 0;

    int features_ = kHigh;
    GLuint shadowTex_ = 0, shadowFbo_ = 0, canopyTex_ = 0, blankShadowTex_ = 0;
    int shadowSize_ = 0;
    M4 lightViewProj_;
    float shadowInfo_[4] = {0, 0, 0, 0};
    const M4* viewProjOverride_ = nullptr;  // the shadow pass renders from the sun
};

// Order markers (renderer_orders.cpp): destinations, targets, attack lines.
void pushOrderMarks(const Battle& battle, float seconds, std::vector<float>& decals);

}  // namespace illyria::game
