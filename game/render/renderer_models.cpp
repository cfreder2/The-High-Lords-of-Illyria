// 3D models made by tools/models (game/web/models): a mesh file and its
// texture, fetched by the page (game/web/models.cpp) and drawn instanced
// with the textured model shaders (kModelVs/kModelFs) wherever the sites
// stand: the ruin. Until it arrives, or if it fails, the ruins are the box
// model built in code (meshes::ruin), as before.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "renderer.h"

namespace illyria::game {

namespace {
// The mesh file (tools/models/ruin.py): "IMDL", version, vertex and index
// counts, bounds, then 20-byte vertices and 16-bit indices.
constexpr size_t kHeader = 4 + 3 * 4 + 6 * 4, kVertexBytes = 20;
constexpr GLenum kMaxAnisotropy = 0x84FE;  // EXT_texture_filter_anisotropic

// Whether the context lists an extension. (glGetString(GL_EXTENSIONS) is an error in a core profile: the names are read one at a time.)
bool hasExtension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext && std::strstr(ext, name)) return true;
    }
    return false;
}

uint32_t readU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
}  // namespace

bool Renderer::loadModel(ModelKind kind, const uint8_t* mesh, size_t meshBytes, const uint8_t* rgba, int width, int height) {
    if (kind != ModelKind::Ruin || !mesh || !rgba || width <= 0 || height <= 0) return false;
    if (meshBytes < kHeader || std::memcmp(mesh, "IMDL", 4) != 0 || readU32(mesh + 4) != 1) return false;
    const uint32_t vertices = readU32(mesh + 8), indices = readU32(mesh + 12);
    if (vertices == 0 || vertices > 65536 || indices == 0 || indices % 3 != 0) return false;
    if (meshBytes != kHeader + vertices * kVertexBytes + indices * 2) return false;
    const uint8_t* vdata = mesh + kHeader;
    const uint8_t* idata = vdata + vertices * kVertexBytes;
    for (uint32_t i = 0; i < indices; ++i) {
        uint16_t k;
        std::memcpy(&k, idata + i * 2, 2);
        if (k >= vertices) return false;
    }

    Model& m = ruinModel_;
    if (!m.vao) {
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vertexVbo);
        glGenBuffers(1, &m.indexVbo);
        glGenBuffers(1, &m.instanceVbo);
        glGenTextures(1, &m.tex);
    }
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vertexVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices * kVertexBytes), vdata, GL_STATIC_DRAW);
    // Position (3 floats), normal (3 signed bytes), uv (2 unsigned shorts).
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kVertexBytes, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, kVertexBytes, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, kVertexBytes, reinterpret_cast<void*>(16));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.indexVbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices * 2), idata, GL_STATIC_DRAW);
    // The instances, laid out as a MeshBatch's: position, quaternion, scale, tint.
    glBindBuffer(GL_ARRAY_BUFFER, m.instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, 13 * 4, nullptr, GL_DYNAMIC_DRAW);
    m.capacity = 1;
    const int sizes[] = {3, 4, 3, 3};
    int offset = 0;
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, sizes[i], GL_FLOAT, GL_FALSE, 13 * 4, reinterpret_cast<void*>(static_cast<intptr_t>(offset * 4)));
        glVertexAttribDivisor(3 + i, 1);
        offset += sizes[i];
    }
    glBindVertexArray(0);
    m.indices = static_cast<GLsizei>(indices);

    // The texture, mipmapped, sharp at a slant where the browser allows.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (hasExtension("texture_filter_anisotropic")) glTexParameterf(GL_TEXTURE_2D, kMaxAnisotropy, 4.0f);
    glBindTexture(GL_TEXTURE_2D, 0);

    uploadRuins(ruinInstances_);  // the sites already laid out now get the model
    return true;
}

void Renderer::uploadRuins(const std::vector<float>& instances) {
    ruinInstances_ = instances;
    Model& m = ruinModel_;
    const bool model = m.vao && modelsOn_;
    uploadMeshInstances(ruins_, model ? std::vector<float>{} : instances);
    m.instances = model ? instances : std::vector<float>{};
}

void Renderer::drawModel(const Program& p, Model& m, const Camera& camera, double timeSeconds, float gameSeconds) {
    if (!m.vao || m.instances.empty()) return;
    // Only the instances in this pass's view (the camera's, or the sun's box
    // for the shadows): a bounding sphere about the model (a unit across,
    // 0.6 high) against the planes of its frustum.
    const M4 vp = viewProjOverride_ ? *viewProjOverride_ : camera.viewProjection();
    float planes[6][4];
    for (int i = 0; i < 3; ++i)
        for (int s = 0; s < 2; ++s)
            for (int j = 0; j < 4; ++j) planes[i * 2 + s][j] = vp.m[j * 4 + 3] + (s ? -1.0f : 1.0f) * vp.m[j * 4 + i];
    m.visible.clear();
    for (size_t k = 0; k + 13 <= m.instances.size(); k += 13) {
        const float* in = &m.instances[k];
        const float scale = std::max(in[7], std::max(in[8], in[9]));
        const V3 c{in[0], in[1], in[2] + 0.27f * in[9]};
        const float radius = 0.8f * scale;
        bool inside = true;
        for (const auto& pl : planes) {
            const float len = std::sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
            inside = inside && pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] >= -radius * len;
        }
        if (inside) m.visible.insert(m.visible.end(), in, in + 13);
    }
    if (m.visible.empty()) return;
    const size_t count = m.visible.size() / 13;
    glBindBuffer(GL_ARRAY_BUFFER, m.instanceVbo);
    if (count > m.capacity) {
        m.capacity = count + count / 2 + 16;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.capacity * 13 * 4), nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(m.visible.size() * 4), m.visible.data());
    setCommon(p, camera, timeSeconds, gameSeconds);
    if (p.albedo >= 0) {
        glActiveTexture(GL_TEXTURE11);
        glBindTexture(GL_TEXTURE_2D, m.tex);
        glUniform1i(p.albedo, 11);
        glActiveTexture(GL_TEXTURE0);
    }
    glBindVertexArray(m.vao);
    drawElements(GL_TRIANGLES, m.indices, GL_UNSIGNED_SHORT, static_cast<GLsizei>(count));
    glBindVertexArray(0);
}

}  // namespace illyria::game
