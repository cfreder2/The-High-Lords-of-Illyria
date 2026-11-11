// The film look (shaders.h, "the film look"): with Renderer::kFilm on, a frame is drawn into a texture of its own
// (multisampled, as the window's own framebuffer is), and put on the screen through two small passes: the bright parts
// bloomed at a quarter and an eighth of the size, then the whole graded, its corners darkened and a little grain laid
// over it. Off, the frame goes straight to the screen as it always did. Cosmetic: nothing here touches the simulation.
#include <algorithm>

#include "renderer.h"

namespace illyria::game {

namespace {

void colourTarget(GLuint& fbo, GLuint& tex, int w, int h) {
    if (!fbo) glGenFramebuffers(1, &fbo);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
}

}  // namespace

GLuint Renderer::beginPost(int w, int h) {
    if (!(features_ & kFilm) || w <= 0 || h <= 0 || !bloom_.id || !grade_.id) return screenFbo_;
    if (w != postW_ || h != postH_) {
        postW_ = w;
        postH_ = h;
        GLint most = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &most);
        const GLsizei samples = std::min<GLint>(4, most);
        if (!postFbo_) glGenFramebuffers(1, &postFbo_);
        if (!postColour_) glGenRenderbuffers(1, &postColour_);
        if (!postDepth_) glGenRenderbuffers(1, &postDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, postColour_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, postDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, postFbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, postColour_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, postDepth_);
        postOk_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        colourTarget(sceneFbo_, sceneTex_, w, h);
        postOk_ = postOk_ && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        for (int level = 0; level < 2; ++level)
            for (int k = 0; k < 2; ++k) {
                const int bw = std::max(1, w >> (2 + level)), bh = std::max(1, h >> (2 + level));
                colourTarget(bloomFbo_[level][k], bloomTex_[level][k], bw, bh);
            }
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    return postOk_ ? postFbo_ : screenFbo_;
}

void Renderer::endPost(double timeSeconds) {
    if (frameFbo_ != postFbo_ || !postFbo_) return;
    const int w = postW_, h = postH_;
    // The samples down to one texture.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, postFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_);
    glActiveTexture(GL_TEXTURE0);
    // The bloom: the bright parts at a quarter of the size, blurred across and down; the same again at an eighth.
    glUseProgram(bloom_.id);
    glUniform1i(glGetUniformLocation(bloom_.id, "uScene"), 0);
    const GLint info = glGetUniformLocation(bloom_.id, "uPostInfo"), dir = glGetUniformLocation(bloom_.id, "uPostDir");
    const auto pass = [&](GLuint fbo, GLuint source, int sw, int sh, int tw, int th, float mode, float dx, float dy) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, tw, th);
        glBindTexture(GL_TEXTURE_2D, source);
        glUniform4f(info, 1.0f / static_cast<float>(sw), 1.0f / static_cast<float>(sh), mode, kBloomFrom);
        glUniform2f(dir, dx, dy);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    const int w4 = std::max(1, w >> 2), h4 = std::max(1, h >> 2), w8 = std::max(1, w >> 3), h8 = std::max(1, h >> 3);
    pass(bloomFbo_[0][0], sceneTex_, w, h, w4, h4, 0, 0, 0);
    pass(bloomFbo_[0][1], bloomTex_[0][0], w4, h4, w4, h4, 1, 1, 0);
    pass(bloomFbo_[0][0], bloomTex_[0][1], w4, h4, w4, h4, 1, 0, 1);
    pass(bloomFbo_[1][0], bloomTex_[0][0], w4, h4, w8, h8, 2, 0, 0);
    pass(bloomFbo_[1][1], bloomTex_[1][0], w8, h8, w8, h8, 1, 1, 0);
    pass(bloomFbo_[1][0], bloomTex_[1][1], w8, h8, w8, h8, 1, 0, 1);
    // The grade, onto the screen.
    glBindFramebuffer(GL_FRAMEBUFFER, screenFbo_);
    glViewport(0, 0, w, h);
    glUseProgram(grade_.id);
    glUniform1i(glGetUniformLocation(grade_.id, "uScene"), 0);
    glUniform1i(glGetUniformLocation(grade_.id, "uBloomNear"), 1);
    glUniform1i(glGetUniformLocation(grade_.id, "uBloomWide"), 2);
    glUniform1f(glGetUniformLocation(grade_.id, "uTime"), static_cast<float>(timeSeconds));
    glUniform4f(glGetUniformLocation(grade_.id, "uPostInfo"), kBloomStrength, kVignette, kGrain, kSaturation);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0][0]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[1][0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    frameFbo_ = screenFbo_;
}

}  // namespace illyria::game
