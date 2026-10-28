// Sprite sheets pre-rendered offline (tools/blender: modelled, rigged and
// animated figures rendered with Cycles) in place of the runtime bake. A
// sheet is the same thing either way: one RGBA texture whose alpha codes
// opaque, team-coloured and outline pixels (kResolveFs), and the table of
// where each of the 130 cells lies and what it covers in metres (kUnitVs).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "renderer.h"

namespace illyria::game {

namespace {

// A sheet at half its size, keeping the alpha codes whole: each 2x2 block
// becomes the figure's colour where any of it is figure (the side's colour if
// most of that is), else outline, else empty. The cells' pixel rectangles
// halve with it; their extents in metres stay.
void halveSheet(std::vector<uint8_t>& px, int& width, int& height, std::vector<float>& table, int cells) {
    const int w2 = (width + 1) / 2, h2 = (height + 1) / 2;
    std::vector<uint8_t> out(static_cast<size_t>(w2) * h2 * 4, 0);
    for (int y = 0; y < h2; ++y)
        for (int x = 0; x < w2; ++x) {
            int rgb[3] = {0, 0, 0}, solid = 0, team = 0, outline = -1;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(width - 1, 2 * x + dx), sy = std::min(height - 1, 2 * y + dy);
                    const uint8_t* p = &px[(static_cast<size_t>(sy) * width + sx) * 4];
                    if (p[3] > 170) {
                        for (int k = 0; k < 3; ++k) rgb[k] += p[k];
                        ++solid;
                        if (p[3] < 223) ++team;
                    } else if (p[3] > 100) {
                        outline = static_cast<int>((static_cast<size_t>(sy) * width + sx) * 4);
                    }
                }
            uint8_t* o = &out[(static_cast<size_t>(y) * w2 + x) * 4];
            if (solid) {
                for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(rgb[k] / solid);
                o[3] = 2 * team > solid ? 191 : 255;
            } else if (outline >= 0) {
                std::copy(&px[static_cast<size_t>(outline)], &px[static_cast<size_t>(outline)] + 4, o);
            }
        }
    for (int c = 0; c < cells; ++c) {
        float* r = &table[static_cast<size_t>(c) * 4];
        const float x0 = std::floor(r[0] / 2), y0 = std::floor(r[1] / 2);
        const float x1 = std::ceil((r[0] + r[2]) / 2), y1 = std::ceil((r[1] + r[3]) / 2);
        r[0] = x0, r[1] = y0, r[2] = std::max(1.0f, x1 - x0), r[3] = std::max(1.0f, y1 - y0);
    }
    px.swap(out);
    width = w2, height = h2;
}

}  // namespace

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
        if (sh.prerendered) prerenderedTexels_ -= static_cast<double>(sh.texW) * sh.texH;
    }
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
    // The same texture budget as the bake's (the baked stand-ins are replaced
    // as the sheets come, so only these count): in a battle of many kinds (the
    // Ford has every one) the sheets that would not fit come at half size, as
    // the bake would have drawn them coarser. Never less than half: a little
    // over the budget is better than a blur.
    int w = width, h = height;
    const bool halved = prerenderedTexels_ + static_cast<double>(w) * h > kSheetBudget && w > 256 && h > 64;
    if (halved) halveSheet(flipped, w, h, table, kCells);
    sh.texW = w, sh.texH = h;
    uploadSheet(sh, flipped.data(), table.data());
    prerenderedTexels_ += static_cast<double>(w) * h;
    std::printf("sprite sheet %s: pre-rendered, %dx%d%s\n", sim::unitTypeName(static_cast<sim::UnitType>(type)), w, h, halved ? " (halved to fit)" : "");
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

std::string Renderer::sheetReport() const {
    std::string out;
    for (const auto& [type, sh] : sheets_) {
        if (!sh.tex) continue;
        if (!out.empty()) out += ',';
        out += sim::unitTypeName(static_cast<sim::UnitType>(type));
        out += sh.prerendered ? "=pre" : "=baked";
    }
    return out;
}

int Renderer::sheetSource(int type) const {
    const auto it = sheets_.find(type);
    if (it == sheets_.end() || !it->second.tex) return -1;
    return it->second.prerendered ? 1 : 0;
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
