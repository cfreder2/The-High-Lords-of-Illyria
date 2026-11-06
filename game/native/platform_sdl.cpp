// The desktop's side of platform.h: a window and an OpenGL 4.1 core context from SDL 3, the sound mixer
// (audio.cpp), the files on disk (assets.cpp), and the entry point. The game itself is game/app/app.cpp.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../app/platform.h"
#include "../render/gl.h"
#include "api.h"
#include "assets.h"
#include "audio.h"
#include "hud.h"
#include "input.h"
#include "overlay.h"

namespace {

SDL_Window* window = nullptr;
SDL_GLContext context = nullptr;

struct Options {
    std::string assets, mode, options, shot;
    std::vector<std::string> clicks;
    int width = 1440, height = 900, shotFrame = 0, quitFrame = 0;
    bool fullscreen = false, sound = true, stats = false, fresh = false;
    int lord = 6, foes = 0xFF & ~(1 << 6);
    bool hard = true;
};

void usage() {
    std::puts(
        "Illyria [options]\n"
        "  (no option)           the start menu\n"
        "  --conquest            start the conquest at once (also --campaign)\n"
        "    --lord N            the lord you lead, 0-7 (default 6, the Moonward Order)\n"
        "    --foes MASK         the computer lords in the war, a bit each (default all the others)\n"
        "    --easy              the computer lords play Easy (default Hard)\n"
        "  --continue            carry on the saved conquest\n"
        "  --skirmish            a skirmish at the Ford at once;  --siege  the siege\n"
        "  --assets DIR          the folder holding sprites/, models/, sfx/\n"
        "  --size WxH, --fullscreen, --no-sound\n"
        "  --stats               print the frame rate every two seconds\n"
        "  --shot FILE.tga[:N]   write the frame N (default 300) to FILE and quit\n"
        "  --quit-after N        quit after N frames\n"
        "  --click X,Y,FRAME[,r] a click (right if r) at a frame, for testing");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--conquest" || a == "--campaign") o.mode = "campaign";
        else if (a == "--continue") o.mode = "continue";
        else if (a == "--skirmish") o.mode = "skirmish";
        else if (a == "--siege") o.mode = "siege";
        else if (a == "--easy") o.hard = false;
        else if (a == "--lord") o.lord = std::atoi(next());
        else if (a == "--foes") o.foes = std::atoi(next());
        else if (a == "--assets") o.assets = next();
        else if (a == "--size") std::sscanf(next(), "%dx%d", &o.width, &o.height);
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--no-sound") o.sound = false;
        else if (a == "--stats") o.stats = true;
        else if (a == "--shot") {
            o.shot = next();
            o.shotFrame = 300;
            const size_t colon = o.shot.rfind(':');
            if (colon != std::string::npos) {
                o.shotFrame = std::atoi(o.shot.c_str() + colon + 1);
                o.shot.resize(colon);
            }
        } else if (a == "--quit-after") o.quitFrame = std::atoi(next());
        else if (a == "--click") o.clicks.push_back(next());
        else if (a == "--help" || a == "-h") return usage(), false;
        else if (a.rfind("-psn_", 0) == 0) continue;  // (macOS passes this when a bundle is opened from the Finder)
        else return std::fprintf(stderr, "unknown option %s\n", a.c_str()), usage(), false;
    }
    o.lord = o.lord < 0 ? 0 : o.lord > 7 ? 7 : o.lord;
    return true;
}

// The frame as it stands, as an uncompressed TGA (bottom row first, which is how GL reads it).
void writeShot(const std::string& path, int w, int h) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const unsigned char head[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<unsigned char>(w & 255), static_cast<unsigned char>(w >> 8),
                                    static_cast<unsigned char>(h & 255), static_cast<unsigned char>(h >> 8), 24, 0};
    std::fwrite(head, 1, sizeof head, f);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
}

}  // namespace

namespace illyria::platform {

double nowMs() { return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()); }

Canvas canvas() {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    return {static_cast<double>(w), static_cast<double>(h), static_cast<double>(SDL_GetWindowPixelDensity(window))};
}

// The window's drawable is the system's to size.
bool resizeCanvas(int, int) { return false; }

void voiceCue(const char*, const char*, bool, float, float, int, int, int) {}  // (the desktop has no voices yet)
void sfxCue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority, int, int) {
    native::audio::sfxCue(id, gain, pan, dist, delay, doppler, priority);
}
void sfxAmbience(float wm, float wv, float sw, float swp, float fl, float flp, float dr, float drp, float rain) {
    native::audio::ambience(wm, wv, sw, swp, fl, flp, dr, drp, rain);
}
void minimapLand(const uint8_t*, int, int) {}  // (no minimap yet)
void modeStarted(const char*) {}
void requestSheet(int type, bool closeup, int standIn) { native::requestSheet(type, closeup, standIn); }
void requestModel(int kind) { native::requestModel(kind); }

}  // namespace illyria::platform

namespace {
int runApp(int argc, char** argv) {
    using namespace illyria;
    Options opt;
    if (!parse(argc, argv, opt)) return 2;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetAppMetadata("The High Lords of Illyria", ILLYRIA_VERSION, "io.github.cfreder2.illyria");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // (required by macOS)
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);  // (the page asks for antialiasing too)
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    window = SDL_CreateWindow("The High Lords of Illyria", opt.width, opt.height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (opt.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!window) {
        std::fprintf(stderr, "No window: %s\n", SDL_GetError());
        return 1;
    }
    context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "OpenGL 4.1 is not available: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "OpenGL functions could not be loaded\n");
        return 1;
    }
    std::printf("OpenGL %s on %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // Where the sprite sheets, models and sounds are: the bundle's Resources, an assets/ beside the program, or
    // the source tree the program was built from.
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    if (!native::findAssets({opt.assets, base + "../Resources", base + "assets", ILLYRIA_SOURCE_ASSETS})) {
        std::fprintf(stderr, "The game's files (sprites, models, sfx) are not found; name them with --assets DIR\n");
        return 1;
    }
    std::printf("Files from %s\n", native::assetRoot().c_str());
    if (opt.sound && !native::audio::open(native::assetRoot().c_str())) std::fprintf(stderr, "Playing without sound\n");

    if (!run::start()) {
        std::fprintf(stderr, "Graphics initialisation failed\n");
        return 1;
    }
    native::Overlay overlay;
    if (!overlay.init()) {
        std::fprintf(stderr, "HUD initialisation failed\n");
        return 1;
    }
    native::Hud hud;
    {
        char* pref = SDL_GetPrefPath("cfreder2", "Illyria");
        if (pref) hud.setSaveFile(std::string(pref) + "conquest.save");
        SDL_free(pref);
    }
    native::Input input(hud);

    if (opt.mode.empty()) {
        illyria_start_mode("title", "{}");
        hud.openMenu();
    } else if (opt.mode == "campaign") {
        char buf[160];
        std::snprintf(buf, sizeof buf, "{\"hard\":%d,\"player\":%d,\"enemies\":%d,\"speed\":1}", opt.hard ? 1 : 0, opt.lord, opt.foes & ~(1 << opt.lord) & 0xFF);
        hud.start("campaign", buf);
    } else if (opt.mode == "continue") {
        hud.startSaved();
    } else {
        hud.start("skirmish", opt.mode == "siege" ? "{\"scenario\":1}" : "{\"scenario\":0}");
    }

    int frame = 0;
    double statsAt = platform::nowMs();
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else input.handle(e);
        }
        for (const std::string& c : opt.clicks) {
            float x = 0, y = 0;
            int at = 0;
            char button = 'l';
            if (std::sscanf(c.c_str(), "%f,%f,%d,%c", &x, &y, &at, &button) < 3 || at != frame) continue;
            SDL_Event click{};
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            click.button.button = button == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            click.button.down = true;
            click.button.x = x;
            click.button.y = y;
            input.handle(click);
            click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            click.button.down = false;
            input.handle(click);
        }
        native::pumpAssets();
        hud.tick();
        const bool drew = run::frame();
        const platform::Canvas cv = platform::canvas();
        if (drew) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            overlay.begin(pw, ph, static_cast<float>(cv.pixelRatio));
            hud.draw(overlay, static_cast<float>(cv.cssW), static_cast<float>(cv.cssH));
            overlay.end();
            if (!opt.shot.empty() && frame == opt.shotFrame) {
                writeShot(opt.shot, pw, ph);
                running = false;
            }
            SDL_GL_SwapWindow(window);
        }
        if (const GLenum err = glGetError(); err != GL_NO_ERROR && frame < 600) std::fprintf(stderr, "GL error 0x%x at frame %d\n", err, frame);
        ++frame;
        if (opt.quitFrame && frame >= opt.quitFrame) running = false;
        if (opt.stats && platform::nowMs() - statsAt > 2000) {
            statsAt = platform::nowMs();
            std::printf("frame %d: %s\n", frame, illyria_perf());
            std::fflush(stdout);
        }
    }
    hud.saveConquest();
    return 0;
}
}  // namespace

// Whatever the way out, the threads are stopped before the process starts to exit (a joinable std::thread
// destroyed by exit() is std::terminate), then the sound, the GL context and window, SDL, in that order.
int main(int argc, char** argv) {
    const int code = runApp(argc, argv);
    illyria::native::stopAssets();
    illyria::native::audio::close();
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
