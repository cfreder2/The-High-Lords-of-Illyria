// Trees as cards: a few upright quads standing on each tree, wearing a picture of it. The pictures are baked once,
// on the GPU, from the same meshes the trees are drawn with (meshes.cpp), side-on, colour and normal, into one
// atlas for every kind (pines, then oaks); the cards are lit as the meshes are (kBoardFs), so a card is the tree's
// own silhouette and shading. The cards ride on the trees' own instance buffers, so which trees stand where, and
// how they fade between near and far (sceneryNear), is the same for cards and for meshes.
#include <algorithm>
#include <cmath>

#include "renderer.h"

namespace illyria::game {

namespace {

constexpr int kCell = 256, kCols = 4;  // a picture's size in texels, and how many make a row of the atlas
constexpr float kPi = 3.14159265f;

struct Card {
    float turn, offset;  // about the tree (radians; below zero: faces the camera), and off its axis in half widths
};

std::vector<Card> cardsOf(Renderer::TreeStyle style) {
    switch (style) {
        case Renderer::TreeStyle::Face1: return {{-1, 0}};
        case Renderer::TreeStyle::Cross2: return {{0, 0}, {kPi / 2, 0}};
        case Renderer::TreeStyle::Star3: return {{0, 0}, {kPi / 3, 0}, {2 * kPi / 3, 0}};
        case Renderer::TreeStyle::Grid4: return {{0, -0.4f}, {0, 0.4f}, {kPi / 2, -0.4f}, {kPi / 2, 0.4f}};
        default: return {};
    }
}

}  // namespace

void Renderer::setTreeStyle(TreeStyle style) {
    treeStyle_ = style;
    layOutBoards();
}

// The cards of the style, as one buffer all the trees share: six vertices a card (across, up, turn, offset).
void Renderer::layOutBoards() {
    std::vector<float> verts;
    static constexpr float kCorners[6][2] = {{-1, 0}, {1, 0}, {1, 1}, {-1, 0}, {1, 1}, {-1, 1}};
    for (const Card& c : cardsOf(treeStyle_))
        for (const auto& corner : kCorners) verts.insert(verts.end(), {corner[0], corner[1], c.turn, c.offset});
    if (!boardVbo_) glGenBuffers(1, &boardVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, boardVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max<size_t>(verts.size(), 4) * 4), verts.data(), GL_STATIC_DRAW);
    boardVertices_ = static_cast<GLsizei>(verts.size() / 4);
}

// The batch's cards: the card buffer over the batch's own instances.
void Renderer::makeBoardVao(MeshBatch& b) {
    glGenVertexArrays(1, &b.boardVao);
    glBindVertexArray(b.boardVao);
    glBindBuffer(GL_ARRAY_BUFFER, boardVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceVbo);
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

void Renderer::bakeTreeBoards() {
    // Every tree kind's mesh, in the order of its cell: the pines, then the oaks (batches share them with the close-up land's).
    const std::vector<Mesh> pines = meshes::pineVariants(), oaks = meshes::oakVariants();
    std::vector<const Mesh*> kinds;
    for (const Mesh& m : pines) kinds.push_back(&m);
    for (const Mesh& m : oaks) kinds.push_back(&m);
    const int rows = (static_cast<int>(kinds.size()) + kCols - 1) / kCols, width = kCols * kCell, height = rows * kCell;

    GLuint fbo = 0, depth = 0;
    for (GLuint* tex : {&boardAtlas_, &boardNormals_}) {
        glGenTextures(1, tex);
        glBindTexture(GL_TEXTURE_2D, *tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glGenFramebuffers(1, &fbo);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, boardAtlas_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, boardNormals_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    const GLenum targets[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, targets);
    // Where there is no tree the colour is a leaf green with no alpha, so the edges the filter blends are not dark.
    const float none[4] = {0.18f, 0.26f, 0.10f, 0.0f}, flat[4] = {0.5f, 0.5f, 1.0f, 0.0f}, far = 1.0f;
    glClearBufferfv(GL_COLOR, 0, none);
    glClearBufferfv(GL_COLOR, 1, flat);
    glClearBufferfv(GL_DEPTH, 0, &far);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(boardBake_.id);

    std::vector<MeshBatch*> batches[2] = {{}, {}};
    for (MeshBatch& b : pines_) batches[0].push_back(&b);
    for (MeshBatch& b : oaks_) batches[0].push_back(&b);
    for (MeshBatch& b : detailPines_) batches[1].push_back(&b);
    for (MeshBatch& b : detailOaks_) batches[1].push_back(&b);
    for (size_t k = 0; k < kinds.size(); ++k) {
        float half = 0.01f, top = 0.01f;
        for (size_t i = 0; i + 9 <= kinds[k]->data.size(); i += 9) {
            half = std::max({half, std::fabs(kinds[k]->data[i]), std::fabs(kinds[k]->data[i + 1])});
            top = std::max(top, kinds[k]->data[i + 2]);
        }
        half *= 1.04f;  // a margin the filter's blur may not cross
        top *= 1.03f;
        const int col = static_cast<int>(k) % kCols, row = static_cast<int>(k) / kCols;
        glViewport(col * kCell, row * kCell, kCell, kCell);
        glUniform3f(boardBake_.bakeBox, half, top, half);
        glBindVertexArray(batches[0][k]->vao);
        glDrawArrays(GL_TRIANGLES, 0, kinds[k]->vertexCount());
        for (MeshBatch* b : {batches[0][k], batches[1][k]}) {
            b->cell = static_cast<int>(k);
            b->boardHalf = half;
            b->boardTop = top;
        }
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, screenFbo_);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    for (GLuint tex : {boardAtlas_, boardNormals_}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (!boardVbo_) layOutBoards();
    for (auto& list : batches)
        for (MeshBatch* b : list) makeBoardVao(*b);
}

void Renderer::drawBoards(const std::vector<MeshBatch*>& batches, const Camera& camera, double timeSeconds, float gameSeconds) {
    const Program& p = viewProjOverride_ ? boardDepth_ : board_;
    setCommon(p, camera, timeSeconds, gameSeconds);
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, boardAtlas_);
    glUniform1i(p.albedo, 12);
    if (p.boardNormal >= 0) {
        glActiveTexture(GL_TEXTURE13);
        glBindTexture(GL_TEXTURE_2D, boardNormals_);
        glUniform1i(p.boardNormal, 13);
    }
    glActiveTexture(GL_TEXTURE0);
    const int rows = (static_cast<int>(pines_.size() + oaks_.size()) + kCols - 1) / kCols;
    for (MeshBatch* b : batches) {
        if (b->count == 0 || b->cell < 0) continue;
        const float col = static_cast<float>(b->cell % kCols), row = static_cast<float>(b->cell / kCols);
        glUniform1f(p.sway, b->sway);
        glUniform1f(p.lod, b->lod);
        glUniform2f(p.board, b->boardHalf, b->boardTop);
        glUniform4f(p.cell, col / kCols, row / static_cast<float>(rows), (col + 1) / kCols, (row + 1) / static_cast<float>(rows));
        glBindVertexArray(b->boardVao);
        if (!viewProjOverride_) part_ = b->part;  // (the sun's pass counts as shadows)
        drawArrays(GL_TRIANGLES, 0, boardVertices_, b->count);
    }
}

void Renderer::drawTrees(const Camera& camera, double timeSeconds, float gameSeconds) {
    if (treeStyle_ != TreeStyle::Mesh) drawBoards(trees(), camera, timeSeconds, gameSeconds);
    else if (!viewProjOverride_) drawMeshes(trees(), camera, timeSeconds, gameSeconds);  // (the sun's pass draws them with the casters)
}

}  // namespace illyria::game
