#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"

namespace illyria::game {

// OpenGL ES 3.0 / WebGL 2 renderer for a battle. Everything is drawn with a
// handful of instanced draw calls so tens of thousands of soldiers, arrows,
// remains and stains stay cheap on phones.
class Renderer {
public:
    bool init(Battle& battle);
    // Forget uploaded remains and marks (call after the battle is replaced).
    void reset(Battle& battle);
    void draw(Battle& battle, const Camera& camera, double timeSeconds);

private:
    struct Program {
        GLuint id = 0;
        GLint viewProj = -1, eye = -1, heights = -1, cells = -1, cellSize = -1;
        GLint time = -1, gameSeconds = -1, camDist = -1, pxScale = -1, camRight = -1;
    };
    // An instanced batch: a VAO with its per-instance buffer bound.
    struct Batch {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        size_t capacity = 0;
        int floats = 0;
    };

    bool buildPrograms();
    void makeBatch(Batch& b, std::initializer_list<int> layout, size_t capacity);
    void zeroBatch(Batch& b, size_t count);
    void uploadDynamic(Batch& b, const std::vector<float>& data);
    void uploadTerrain(const Battle& battle);
    void uploadMarks(const Battle& battle);
    void uploadRemains(Battle& battle);
    void setCommon(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds);

    Program terrain_, decal_, arrow_, sprite_, particle_;
    GLuint terrainVao_ = 0, terrainVbo_ = 0, terrainIbo_ = 0, heightTex_ = 0;
    GLsizei terrainIndexCount_ = 0;
    int cells_ = 0;
    float cellSize_ = 1;

    Batch marks_, remainArrows_, remainParts_, dynamicDecals_, flyingArrows_, sprites_, particles_;
    uint64_t marksUploaded_ = 0;
};

}  // namespace illyria::game
