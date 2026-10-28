// Sprite sheets pre-rendered offline (tools/blender: modelled, rigged and
// animated figures rendered with Cycles) in place of the runtime bake. A
// sheet is the same thing either way: one RGBA texture whose alpha codes
// opaque, team-coloured and outline pixels (kResolveFs), and the table of
// where each of the 130 cells lies and what it covers in metres (kUnitVs).

#include <algorithm>
#include <cstdio>

#include "renderer.h"

namespace illyria::game {

bool Renderer::loadSheet(int type, const uint8_t* rgba, int width, int height, const float* cells) {
    constexpr int kCells = figures::kViews * figures::kFramesPerView;
    if (type < 0 || type >= sim::kUnitTypeCount || !rgba || !cells || width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    for (int c = 0; c < kCells; ++c) {
        const float* r = &cells[c * 8];
        if (r[0] < 0 || r[1] < 0 || r[2] < 1 || r[3] < 1 || r[0] + r[2] > width || r[1] + r[3] > height || r[6] <= 0 || r[7] <= 0) return false;
    }
    Sheet& sh = sheets_[type];
    if (sh.tex) {
        glDeleteTextures(1, &sh.tex);
        glDeleteTextures(1, &sh.cells);
        sheetTexels_ -= static_cast<double>(sh.texW) * sh.texH;
    }
    sh.texW = width, sh.texH = height;
    sh.lift = 0.5f + static_cast<float>(sim::unitStats(static_cast<sim::UnitType>(type)).radius.toDouble()) * 1.2f;
    sh.prerendered = true;
    // GL keeps the bottom row first.
    std::vector<uint8_t> flipped(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        std::copy(rgba + static_cast<size_t>(height - 1 - y) * width * 4, rgba + static_cast<size_t>(height - y) * width * 4, flipped.begin() + static_cast<size_t>(y) * width * 4);
    std::vector<float> table(static_cast<size_t>(kCells) * 2 * 4);
    for (int c = 0; c < kCells; ++c)
        for (int k = 0; k < 4; ++k) {
            table[static_cast<size_t>(c) * 4 + k] = cells[c * 8 + k];
            table[(static_cast<size_t>(kCells) + c) * 4 + k] = cells[c * 8 + 4 + k];
        }
    uploadSheet(sh, flipped.data(), table.data());
    std::printf("sprite sheet %s: pre-rendered, %dx%d\n", sim::unitTypeName(static_cast<sim::UnitType>(type)), width, height);
    return true;
}

void Renderer::uploadSheet(Sheet& sh, const uint8_t* rgba, const float* table) {
    constexpr int kCells = figures::kViews * figures::kFramesPerView;
    glGenTextures(1, &sh.tex);
    glBindTexture(GL_TEXTURE_2D, sh.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sh.texW, sh.texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    // As baked sheets: crisp close up, averaged far off.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &sh.cells);
    glBindTexture(GL_TEXTURE_2D, sh.cells);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kCells, 2, 0, GL_RGBA, GL_FLOAT, table);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    sheetTexels_ += static_cast<double>(sh.texW) * sh.texH;
    sh.table.assign(table, table + kCells * 8);
}

bool Renderer::readSheet(int type, std::vector<uint8_t>& rgba, int& width, int& height, std::vector<float>& table) {
    if (type < 0 || type >= sim::kUnitTypeCount) return false;
    Sheet& sh = sheets_[type];
    if (!sh.tex) bakeSheet(sh, static_cast<sim::UnitType>(type), figures::plan(static_cast<sim::UnitType>(type)), 1.0f);
    width = sh.texW, height = sh.texH;
    rgba.resize(static_cast<size_t>(width) * height * 4);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sh.tex, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    table = sh.table;
    return true;
}

}  // namespace illyria::game
