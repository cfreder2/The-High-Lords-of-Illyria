#include "overlay.h"

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
uniform vec2 uSize;
out vec4 vColour;
void main() {
    vColour = aColour;
    gl_Position = vec4(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0, 0.0, 1.0);
})";

const char* kFragment = R"(#version 410 core
in vec4 vColour;
out vec4 outColour;
void main() { outColour = vColour; })";

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
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);
    return true;
}

void Overlay::begin(int pixelsW, int pixelsH, float scale) {
    w_ = pixelsW;
    h_ = pixelsH;
    scale_ = scale;
    verts_.clear();
}

void Overlay::rect(float x, float y, float w, float h, Colour c) {
    const float x0 = x * scale_, y0 = y * scale_, x1 = (x + w) * scale_, y1 = (y + h) * scale_;
    const Vertex a{x0, y0, c.r, c.g, c.b, c.a}, b{x1, y0, c.r, c.g, c.b, c.a}, d{x1, y1, c.r, c.g, c.b, c.a}, e{x0, y1, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, d, a, d, e});
}

void Overlay::frame(float x, float y, float w, float h, float line, Colour c) {
    rect(x, y, w, line, c);
    rect(x, y + h - line, w, line, c);
    rect(x, y + line, line, h - 2 * line, c);
    rect(x + w - line, y + line, line, h - 2 * line, c);
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
        for (int i = 0; i < 4; ++i) v[i] = {(x + raw[q * 4 + i].x * k) * scale_, (y + raw[q * 4 + i].y * k) * scale_, c.r, c.g, c.b, c.a};
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
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

}  // namespace illyria::native
