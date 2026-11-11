#pragma once

// A 2D layer over the frame for the desktop's HUD (hud.cpp): filled rectangles and text (stb_easy_font's
// strokes, so there is no font to ship). The browser's HUD is the page; this is its small stand-in.
// Positions are in points (CSS pixels); the layer scales them to the drawable.

#include <cstdint>
#include <string>
#include <vector>

namespace illyria::native {

struct Colour {
    uint8_t r, g, b, a;
};
constexpr Colour rgba(unsigned hex, uint8_t a = 255) { return {static_cast<uint8_t>(hex >> 16), static_cast<uint8_t>(hex >> 8), static_cast<uint8_t>(hex), a}; }

class Overlay {
public:
    bool init();
    // Starts a frame of the drawable's size in pixels; `scale` is pixels a point.
    void begin(int pixelsW, int pixelsH, float scale);
    void rect(float x, float y, float w, float h, Colour c);
    void frame(float x, float y, float w, float h, float line, Colour c);
    // A sheet of parchment: drop shadow, deckled darker rim, a soft vignette and fibre specks, all from rectangles
    // (the layer has no textures). `seed` makes each sheet's edge its own, the same every frame.
    void paper(float x, float y, float w, float h, uint32_t seed = 1);
    // Text of size `size` points tall, left edge at x; returns its width in points.
    float text(float x, float y, const std::string& s, Colour c, float size = 14);
    float width(const std::string& s, float size = 14) const;
    void end();

private:
    struct Vertex {
        float x, y;
        uint8_t r, g, b, a;
    };
    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    int uSize_ = -1;
    int w_ = 0, h_ = 0;
    float scale_ = 1;
    std::vector<Vertex> verts_;
};

}  // namespace illyria::native
