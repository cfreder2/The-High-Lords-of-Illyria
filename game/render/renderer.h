#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"

namespace illyria::game {

// OpenGL ES 3.0 / WebGL 2 renderer for a battle. Everything is drawn with a
// handful of instanced draw calls so tens of thousands of soldiers, arrows and
// stains stay cheap on phones.
class Renderer {
public:
    bool init(const Battle& battle);
    // Forget uploaded remains (call after the battle is replaced).
    void reset(const Battle& battle);
    void draw(const Battle& battle, const Camera& camera, double timeSeconds);

private:
    struct Program {
        GLuint id = 0;
        GLint viewProj = -1, eye = -1, heights = -1, cells = -1, cellSize = -1;
        GLint time = -1, gameSeconds = -1, camDist = -1, pxScale = -1, camRight = -1;
    };

    bool buildPrograms();
    void uploadTerrain(const Battle& battle);
    void uploadNewRemains(const Battle& battle);
    void setCommon(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds);

    Program terrain_, decal_, arrow_, sprite_;
    GLuint terrainVao_ = 0, terrainVbo_ = 0, terrainIbo_ = 0, heightTex_ = 0;
    GLsizei terrainIndexCount_ = 0;
    int cells_ = 0;
    float cellSize_ = 1;

    // Instanced batches: a VAO with the per-instance buffer bound.
    struct Batch {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        size_t capacity = 0;
    };
    Batch bloodBatch_, stuckArrowBatch_, dynamicDecals_, flyingArrows_, sprites_;
    uint64_t bloodUploaded_ = 0, arrowsUploaded_ = 0;

    std::vector<float> scratch_;
};

}  // namespace illyria::game
