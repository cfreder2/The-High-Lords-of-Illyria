// The close-up land off the battlefield (see DetailLand): its textures, built
// a chunk at a time around the camera, drawn with the battlefield's own
// terrain, cover and water programs in their repeating-texture mode.
#include <algorithm>
#include <chrono>

#include "renderer.h"

namespace illyria::game {

namespace {

constexpr int kN = DetailLand::kSamples, kF = DetailLand::kFine, kC = DetailLand::kChunk;
// Off the battlefield the close-up land is drawn below these camera distances,
// cross-fading into the coarse land between them (by then the cover is long
// gone and the coarse land's look carries).
constexpr float kDetailFull = 600.0f, kDetailBelow = 850.0f;
constexpr double kChunkFadeSeconds = 0.6;  // a newly built chunk fades in, never pops

void texture2d(GLuint& tex, GLint internal, int size, GLenum format, GLenum type, const void* data, bool linear) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, size, size, 0, format, type, data);
}

// Uploads the square [x0, x0 + w) x [y0, y0 + w) of a toroidal array of
// period `n` into the same place of its texture, in up to four pieces where it
// wraps.
void uploadWrapped(GLuint tex, int n, int x0, int y0, int w, GLenum format, GLenum type, const void* data) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, n);
    const int sx = DetailLand::wrap(x0, n), sy = DetailLand::wrap(y0, n);
    const int wx[2] = {std::min(w, n - sx), w - std::min(w, n - sx)}, wy[2] = {std::min(w, n - sy), w - std::min(w, n - sy)};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            if (wx[i] <= 0 || wy[j] <= 0) continue;
            const int px = i == 0 ? sx : 0, py = j == 0 ? sy : 0;
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, px);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, py);
            glTexSubImage2D(GL_TEXTURE_2D, 0, px, py, wx[i], wy[j], format, type, data);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
}

}  // namespace

void Renderer::resetDetail(const Battle& battle) {
    detail_.reset(battle.continentShared(), battle.origin(), Battle::kTerrainCells);
    detailGen_ = UINT32_MAX;
    detailChunks_ = detailWetChunks_ = 0;
    detailOn_ = false;
    detailRect_[0] = detailRect_[1] = 1e9f;
    detailRect_[2] = detailRect_[3] = -1e9f;
    texture2d(detailTex_[0], GL_R32F, kN, GL_RED, GL_FLOAT, detail_.heights().data(), false);
    texture2d(detailTex_[1], GL_R8, kN, GL_RED, GL_UNSIGNED_BYTE, detail_.ground().data(), false);
    texture2d(detailTex_[2], GL_R32F, kN, GL_RED, GL_FLOAT, detail_.water().data(), false);
    texture2d(detailTex_[3], GL_RG8, kN, GL_RG, GL_UNSIGNED_BYTE, detail_.flow().data(), true);
    texture2d(detailTex_[4], GL_RG8, kF, GL_RG, GL_UNSIGNED_BYTE, detail_.road().data(), true);
    texture2d(detailTex_[5], GL_R8, kF, GL_RED, GL_UNSIGNED_BYTE, detail_.canopy().data(), true);
    const std::vector<uint8_t> none(static_cast<size_t>(DetailLand::kSlots) * DetailLand::kSlots, 0);
    texture2d(detailMask_, GL_R8, DetailLand::kSlots, GL_RED, GL_UNSIGNED_BYTE, none.data(), false);
    if (detailVao_) return;

    // One chunk's grid, 17 x 17 samples, and a skirt around its edge.
    std::vector<float> verts;
    for (int j = 0; j <= kC; ++j)
        for (int i = 0; i <= kC; ++i) verts.insert(verts.end(), {static_cast<float>(i), static_cast<float>(j), 0});
    std::vector<uint32_t> idx;
    for (int j = 0; j < kC; ++j) {
        for (int i = 0; i < kC; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * (kC + 1) + i), b = a + 1, c = a + kC + 1, d = c + 1;
            idx.insert(idx.end(), {a, b, d, a, d, c});  // the same diagonal as the battlefield
        }
    }
    const size_t gridIndices = idx.size();
    std::vector<std::pair<int, int>> ring;
    for (int i = 0; i < kC; ++i) ring.push_back({i, 0});
    for (int j = 0; j < kC; ++j) ring.push_back({kC, j});
    for (int i = kC; i > 0; --i) ring.push_back({i, kC});
    for (int j = kC; j > 0; --j) ring.push_back({0, j});
    ring.push_back({0, 0});
    for (size_t k = 0; k + 1 < ring.size(); ++k) {
        const uint32_t t0 = static_cast<uint32_t>(ring[k].second * (kC + 1) + ring[k].first);
        const uint32_t t1 = static_cast<uint32_t>(ring[k + 1].second * (kC + 1) + ring[k + 1].first);
        const uint32_t b0 = static_cast<uint32_t>(verts.size() / 3);
        verts.insert(verts.end(), {static_cast<float>(ring[k].first), static_cast<float>(ring[k].second), 1,
                                   static_cast<float>(ring[k + 1].first), static_cast<float>(ring[k + 1].second), 1});
        idx.insert(idx.end(), {t0, t1, b0 + 1, t0, b0 + 1, b0});
    }
    detailIndices_ = static_cast<GLsizei>(idx.size());
    detailWaterIndices_ = static_cast<GLsizei>(gridIndices);
    glGenVertexArrays(1, &detailVao_);
    glGenBuffers(1, &detailVbo_);
    glGenBuffers(1, &detailIbo_);
    glGenBuffers(1, &detailInst_);
    glBindVertexArray(detailVao_);
    glBindBuffer(GL_ARRAY_BUFFER, detailVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * 4), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, detailIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, detailInst_);
    glBufferData(GL_ARRAY_BUFFER, 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glVertexAttribDivisor(1, 1);
    // Water over whole chunks that hold any (the shader keeps only the wet cells).
    std::vector<float> flat;
    for (int j = 0; j <= kC; ++j)
        for (int i = 0; i <= kC; ++i) flat.insert(flat.end(), {i * DetailLand::kCell, j * DetailLand::kCell});
    glGenVertexArrays(1, &detailWaterVao_);
    glGenBuffers(1, &detailWaterVbo_);
    glGenBuffers(1, &detailWetInst_);
    glBindVertexArray(detailWaterVao_);
    glBindBuffer(GL_ARRAY_BUFFER, detailWaterVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(flat.size() * 4), flat.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, detailIbo_);
    glBindBuffer(GL_ARRAY_BUFFER, detailWetInst_);
    glBufferData(GL_ARRAY_BUFFER, 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glVertexAttribDivisor(1, 1);
    glBindVertexArray(0);
}

void Renderer::updateDetail(const Battle& battle, const Camera& camera, double budgetMs, bool instant) {
    (void)battle;
    detailOn_ = false;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double dt = detailClock_ < 0 ? 0 : std::min(now - detailClock_, 0.25);
    detailClock_ = now;
    const auto hide = [&] {
        detailRect_[0] = detailRect_[1] = 1e9f;
        detailRect_[2] = detailRect_[3] = -1e9f;
        detailFade_ = 0;
    };
    if (!(features_ & (kGroundDetail | kGroundCover)) || camera.distance() > kDetailBelow || !detail_.active()) {
        hide();
        return;
    }
    const V3 t = camera.target();
    detail_.recentre(t.x, t.y);
    for (int s : detail_.work(budgetMs)) {
        const DetailLand::Slot& sl = detail_.slot(s);
        const int bx = sl.cx * kC, by = sl.cy * kC;
        uploadWrapped(detailTex_[0], kN, bx - 1, by - 1, kC + 3, GL_RED, GL_FLOAT, detail_.heights().data());
        uploadWrapped(detailTex_[1], kN, bx - 1, by - 1, kC + 2, GL_RED, GL_UNSIGNED_BYTE, detail_.ground().data());
        uploadWrapped(detailTex_[2], kN, bx - 1, by - 1, kC + 3, GL_RED, GL_FLOAT, detail_.water().data());
        uploadWrapped(detailTex_[3], kN, bx - 1, by - 1, kC + 3, GL_RG, GL_UNSIGNED_BYTE, detail_.flow().data());
        uploadWrapped(detailTex_[4], kF, bx * 2 - 2, by * 2 - 2, kC * 2 + 4, GL_RG, GL_UNSIGNED_BYTE, detail_.road().data());
        uploadWrapped(detailTex_[5], kF, bx * 2 - 2, by * 2 - 2, kC * 2 + 4, GL_RED, GL_UNSIGNED_BYTE, detail_.canopy().data());
    }
    constexpr int kS = DetailLand::kSlots;
    // Each shown chunk fades in from the moment it is finished; where the
    // coarse land gives way follows (one byte per chunk).
    chunkFade_.resize(static_cast<size_t>(kS) * kS, 0.0f);
    bool fading = false;
    for (int s = 0; s < kS * kS; ++s) {
        float& f = chunkFade_[static_cast<size_t>(s)];
        const float was = f;
        f = !detail_.shown(s) ? 0.0f : instant ? 1.0f : std::min(1.0f, f + static_cast<float>(dt / kChunkFadeSeconds));
        fading = fading || f != was;
    }
    if (fading) {
        std::vector<uint8_t> mask(chunkFade_.size());
        for (size_t s = 0; s < mask.size(); ++s) mask[s] = static_cast<uint8_t>(chunkFade_[s] * 255.0f + 0.5f);
        glBindTexture(GL_TEXTURE_2D, detailMask_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kS, kS, GL_RED, GL_UNSIGNED_BYTE, mask.data());
    }
    if (detail_.generation() != detailGen_) {
        // What is shown changed: the chunks to draw.
        detailGen_ = detail_.generation();
        std::vector<float> chunks, wet;
        for (int s = 0; s < kS * kS; ++s) {
            if (!detail_.shown(s)) continue;
            const DetailLand::Slot& sl = detail_.slot(s);
            const float x = sl.cx * DetailLand::kChunkSize, y = sl.cy * DetailLand::kChunkSize;
            chunks.insert(chunks.end(), {x, y});
            if (sl.wet) wet.insert(wet.end(), {x, y});
        }
        detailChunks_ = static_cast<GLsizei>(chunks.size() / 2);
        detailWetChunks_ = static_cast<GLsizei>(wet.size() / 2);
        glBindBuffer(GL_ARRAY_BUFFER, detailInst_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max<size_t>(chunks.size(), 2) * 4), chunks.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, detailWetInst_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max<size_t>(wet.size(), 2) * 4), wet.data(), GL_DYNAMIC_DRAW);
    }
    // The chunks that can be shown: the window less its margin. The land fades out toward its edge.
    detailRect_[0] = (detail_.windowX() + 1) * DetailLand::kChunkSize;
    detailRect_[1] = (detail_.windowY() + 1) * DetailLand::kChunkSize;
    detailRect_[2] = (detail_.windowX() + kS - 1) * DetailLand::kChunkSize;
    detailRect_[3] = (detail_.windowY() + kS - 1) * DetailLand::kChunkSize;
    const float d = camera.distance();
    detailFade_ = d <= kDetailFull ? 1.0f : 1.0f - (d - kDetailFull) / (kDetailBelow - kDetailFull);
    detailOn_ = detailChunks_ > 0 && detailFade_ > 0;
    if (!detailOn_) hide();
}

void Renderer::bindDetail(const Program& p) {
    if (p.wrap >= 0) glUniform1f(p.wrap, 1.0f);
    glUniform1f(p.cells, static_cast<float>(kN));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, detailTex_[0]);
    if (p.ground >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, detailTex_[1]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, detailTex_[4]);
    }
    if (p.waterTex >= 0) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, detailTex_[2]);
    }
    if (p.canopy >= 0) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, detailTex_[5]);
    }
    if (p.flow >= 0) {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, detailTex_[3]);
    }
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::drawDetailTerrain(const Program& p, const Camera& camera, double timeSeconds, float gameSeconds) {
    if (!detailOn_) return;
    setCommon(p, camera, timeSeconds, gameSeconds);
    bindDetail(p);
    glBindVertexArray(detailVao_);
    glDrawElementsInstanced(GL_TRIANGLES, detailIndices_, GL_UNSIGNED_INT, nullptr, detailChunks_);
}

void Renderer::drawDetailWater(const Camera& camera, double timeSeconds, float gameSeconds) {
    if (!detailOn_ || detailWetChunks_ == 0) return;
    setCommon(water_, camera, timeSeconds, gameSeconds);
    bindDetail(water_);
    glBindVertexArray(detailWaterVao_);
    glDrawElementsInstanced(GL_TRIANGLES, detailWaterIndices_, GL_UNSIGNED_INT, nullptr, detailWetChunks_);
}

}  // namespace illyria::game
