// Writes every GLSL program of the renderer (kCommon prefix included) to
// <dir>/<name>.vert and .frag so glslangValidator can check them offline:
//   illyria_shaderdump out/ && glslangValidator out/*.vert out/*.frag
#include <cstdio>
#include <string>

#include "../../game/render/shaders.h"

namespace shaders = illyria::game::shaders;

using namespace illyria::game::shaders;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: illyria_shaderdump <dir>\n");
        return 2;
    }
    struct Program {
        const char* name;
        const char* vs;
        const char* fs;
    } programs[] = {
#define ILLYRIA_SHADER(name, vs, fs) {name, vs, fs},
#include "../../game/render/shader_list.h"
#undef ILLYRIA_SHADER
    };
    for (const Program& p : programs) {
        for (int stage = 0; stage < 2; ++stage) {
            const std::string path = std::string(argv[1]) + "/" + p.name + (stage ? ".frag" : ".vert");
            FILE* f = std::fopen(path.c_str(), "w");
            if (!f) return 1;
            std::fputs(kCommon, f);
            std::fputs(stage ? p.fs : p.vs, f);
            std::fclose(f);
        }
    }
    return 0;
}
