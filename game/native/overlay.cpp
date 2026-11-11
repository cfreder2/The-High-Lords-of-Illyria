#include "overlay.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>

#include "../render/gl.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wunused-function"
#include "third_party/stb_easy_font.h"
#pragma clang diagnostic pop

namespace illyria::native {

namespace {

const char* kVertex = R"(#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColour;
layout(location = 2) in vec2 aUV;
uniform vec2 uSize;
out vec4 vColour;
out vec2 vUV;
void main() {
    vColour = aColour;
    vUV = aUV;
    gl_Position = vec4(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0, 0.0, 1.0);
})";

const char* kFragment = R"(#version 410 core
in vec4 vColour;
in vec2 vUV;
uniform sampler2D uTexture;
out vec4 outColour;
void main() { outColour = vColour * texture(uTexture, vUV); })";

GLuint compile(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "overlay shader: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

bool Overlay::init() {
    const GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    uSize_ = glGetUniformLocation(program_, "uSize");
    uTexture_ = glGetUniformLocation(program_, "uTexture");
    {
        const uint8_t pixel[4] = {255, 255, 255, 255};
        glGenTextures(1, &white_);
        glBindTexture(GL_TEXTURE_2D, white_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glBindVertexArray(0);
    return true;
}

void Overlay::begin(int pixelsW, int pixelsH, float scale) {
    w_ = pixelsW;
    h_ = pixelsH;
    scale_ = scale;
    verts_.clear();
    batches_.clear();
}

void Overlay::rect(float x, float y, float w, float h, Colour c) {
    const float x0 = x * scale_, y0 = y * scale_, x1 = (x + w) * scale_, y1 = (y + h) * scale_;
    const Vertex a{x0, y0, 0, 0, c.r, c.g, c.b, c.a}, b{x1, y0, 0, 0, c.r, c.g, c.b, c.a}, d{x1, y1, 0, 0, c.r, c.g, c.b, c.a}, e{x0, y1, 0, 0, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, d, a, d, e});
}

void Overlay::line(float x0, float y0, float x1, float y1, float thickness, Colour c) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    const float nx = -dy / len * thickness * 0.5f * scale_, ny = dx / len * thickness * 0.5f * scale_;
    const float ax = x0 * scale_, ay = y0 * scale_, bx = x1 * scale_, by = y1 * scale_;
    const Vertex a{ax + nx, ay + ny, 0, 0, c.r, c.g, c.b, c.a}, b{bx + nx, by + ny, 0, 0, c.r, c.g, c.b, c.a}, d{bx - nx, by - ny, 0, 0, c.r, c.g, c.b, c.a},
        e{ax - nx, ay - ny, 0, 0, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, d, a, d, e});
}

unsigned Overlay::makeImage(const uint8_t* rgba, int w, int h) {
    unsigned id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    updateImage(id, rgba, w, h);
    return id;
}

void Overlay::updateImage(unsigned id, const uint8_t* rgba, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Overlay::image(unsigned id, float x, float y, float w, float h, Colour c) {
    batches_.push_back({verts_.size(), 0});  // what came before: plain
    const float x0 = x * scale_, y0 = y * scale_, x1 = (x + w) * scale_, y1 = (y + h) * scale_;
    const Vertex a{x0, y0, 0, 0, c.r, c.g, c.b, c.a}, b{x1, y0, 1, 0, c.r, c.g, c.b, c.a}, d{x1, y1, 1, 1, c.r, c.g, c.b, c.a}, e{x0, y1, 0, 1, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, d, a, d, e});
    batches_.push_back({verts_.size(), id});
}

void Overlay::frame(float x, float y, float w, float h, float line, Colour c) {
    rect(x, y, w, line, c);
    rect(x, y + h - line, w, line, c);
    rect(x, y + line, line, h - 2 * line, c);
    rect(x + w - line, y + line, line, h - 2 * line, c);
}

namespace {
// A small stateless hash, so a sheet's tear and specks never change from frame to frame.
float noise01(uint32_t seed, uint32_t i) {
    uint32_t v = seed * 747796405u + i * 2891336453u + 12345u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    v = (v >> 22u) ^ v;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}
}  // namespace

void Overlay::paper(float x, float y, float w, float h, uint32_t seed) {
    const Colour sheet = rgba(0xE9D9B0, 244), rim = rgba(0x7A5A2C, 255), brown = rgba(0x6E4A1E);
    constexpr float kBand = 5, kStep = 3, kRim = 1.6f;
    rect(x + 4, y + 5, w, h, rgba(0x000000, 80));  // the shadow on the table
    rect(x + kBand, y + kBand, w - 2 * kBand, h - 2 * kBand, sheet);
    // The deckled ring: per step, a dark rim to the torn depth and paper behind it.
    uint32_t n = 0;
    for (float t = 0; t < w; t += kStep, ++n) {
        const float sw = std::min(kStep, w - t), dT = noise01(seed, n) * 3.2f, dB = noise01(seed + 7, n) * 3.2f;
        rect(x + t, y + dT, sw, kBand - dT + 1, sheet);
        rect(x + t, y + dT, sw, kRim, rim);
        rect(x + t, y + h - kBand - 1, sw, kBand - dB + 1, sheet);
        rect(x + t, y + h - dB - kRim, sw, kRim, rim);
    }
    for (float t = 0; t < h; t += kStep, ++n) {
        const float sh = std::min(kStep, h - t), dL = noise01(seed + 13, n) * 3.2f, dR = noise01(seed + 29, n) * 3.2f;
        rect(x + dL, y + t, kBand - dL + 1, sh, sheet);
        rect(x + dL, y + t, kRim, sh, rim);
        rect(x + w - kBand - 1, y + t, kBand - dR + 1, sh, sheet);
        rect(x + w - dR - kRim, y + t, kRim, sh, rim);
    }
    // The edges darken inward, as old paper does.
    for (int i = 0; i < 7; ++i) frame(x + kBand + static_cast<float>(i) * 3, y + kBand + static_cast<float>(i) * 3, w - 2 * (kBand + static_cast<float>(i) * 3), h - 2 * (kBand + static_cast<float>(i) * 3), 3, Colour{brown.r, brown.g, brown.b, static_cast<uint8_t>(34 - i * 4)});
    // Fibre specks.
    const int specks = static_cast<int>(w * h / 700.0f);
    for (int i = 0; i < specks; ++i) {
        const float sx = x + kBand + noise01(seed + 3, static_cast<uint32_t>(i) * 2) * (w - 2 * kBand - 2);
        const float sy = y + kBand + noise01(seed + 5, static_cast<uint32_t>(i) * 2 + 1) * (h - 2 * kBand - 2);
        const bool dash = noise01(seed + 9, static_cast<uint32_t>(i)) > 0.8f;
        rect(sx, sy, dash ? 3.0f : 1.2f, 1.1f, Colour{brown.r, brown.g, brown.b, static_cast<uint8_t>(14 + noise01(seed + 11, static_cast<uint32_t>(i)) * 22)});
    }
}

// The font has ASCII only: anything else (the page's middle dots) becomes a space.
static std::string plain(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (static_cast<unsigned char>(c) > 126) c = ' ';
    return out;
}

float Overlay::width(const std::string& s, float size) const {
    const std::string p = plain(s);
    return static_cast<float>(stb_easy_font_width(const_cast<char*>(p.c_str()))) * size / 7.0f;
}

float Overlay::text(float x, float y, const std::string& s, Colour c, float size) {
    static char buffer[1 << 16];  // 4 vertices of 16 bytes a stroke
    const std::string p = plain(s);
    const int quads = stb_easy_font_print(0, 0, const_cast<char*>(p.c_str()), nullptr, buffer, sizeof buffer);
    const float k = size / 7.0f;
    struct Raw {
        float x, y, z;
        uint8_t colour[4];
    };
    const Raw* raw = reinterpret_cast<const Raw*>(buffer);
    for (int q = 0; q < quads; ++q) {
        Vertex v[4];
        for (int i = 0; i < 4; ++i) v[i] = {(x + raw[q * 4 + i].x * k) * scale_, (y + raw[q * 4 + i].y * k) * scale_, 0, 0, c.r, c.g, c.b, c.a};
        verts_.insert(verts_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
    return width(s, size);
}

void Overlay::end() {
    if (verts_.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, w_, h_);
    glUseProgram(program_);
    glUniform2f(uSize_, static_cast<float>(w_), static_cast<float>(h_));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)), verts_.data(), GL_STREAM_DRAW);
    glUniform1i(uTexture_, 0);
    glActiveTexture(GL_TEXTURE0);
    batches_.push_back({verts_.size(), 0});
    size_t from = 0;
    for (const Batch& b : batches_) {
        if (b.end > from) {
            glBindTexture(GL_TEXTURE_2D, b.texture ? b.texture : white_);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(from), static_cast<GLsizei>(b.end - from));
        }
        from = b.end;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

}  // namespace illyria::native
