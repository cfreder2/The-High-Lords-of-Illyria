#pragma once

// The graphics API, one include for the renderer: OpenGL ES 3.0 (WebGL 2) in the browser, and on a desktop
// OpenGL 4.1 core (the newest macOS has), loaded by glad (game/native/third_party/glad). The renderer uses only
// what the two share; the shaders differ in their first line alone (shaders.h kVersion).

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif
