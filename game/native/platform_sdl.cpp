// The desktop's side of platform.h: a window and an OpenGL 4.1 core context from SDL 3, the sound mixer
// (audio.cpp), the files on disk (assets.cpp), and the entry point. The game itself is game/app/app.cpp.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
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
#include "json.h"
#include "overlay.h"

namespace {

// The simulation's tick count (illyria_hud): 0 until a mode is begun, and held there while the menu is the title's.
unsigned hudTick() {
    illyria::native::Json hud;
    illyria::native::Json::parse(illyria_hud(), hud);
    return static_cast<unsigned>(hud.num("tick"));
}

SDL_Window* window = nullptr;
SDL_GLContext context = nullptr;

constexpr int kAllTreeStyles = 99;
constexpr int kTourFrames = 120;  // frames a --camera tour holds each camera

struct Options {
    std::string assets, mode, options, shot;
    std::vector<std::array<float, 4>> cameras;  // --camera: x, y, distance, yaw (given again: a tour, one picture of each)
    std::vector<std::string> clicks;
    int width = 1440, height = 900, shotFrame = 0, quitFrame = 0;
    bool fullscreen = false, sound = true, stats = false, vsync = true;
    int trees = -1;  // --trees N: how trees are drawn (0 one card, 1 two crossed, 2 three, 3 four); 99: --shot takes one picture of each
    int profile = 0;  // --profile N: after N frames, time what each kind of drawing costs
    int msaa = 0;  // (the window's own antialiasing: a scaled frame is drawn without it, and the display is dense)
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
        "  --stats               print the frame rate and its split every two seconds\n"
        "  --profile N           after N frames, hide each kind of drawing in turn and print what it cost (with --novsync)\n"
        "  --trees N|all         draw trees as 0 one card, 1 two crossed cards, 2 three, 3 four (default: the game's);\n"
        "                        all: --shot FILE.tga writes FILE-0.tga to FILE-3.tga, one a style\n"
        "  --novsync, --msaa N   swap at once instead of with the display; samples of the window (default 0)\n"
        "  --shot FILE.tga[:N]   write the frame N (default 300) to FILE and quit\n"
        "  --quit-after N        quit after N frames\n"
        "  --camera X,Y,DIST,YAW put the camera there (battle metres, metres up, radians) once the game runs;\n"
        "                        given again with --shot FILE.tga: FILE-0.tga, FILE-1.tga... one a camera, the land built first\n"
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
        else if (a == "--novsync") o.vsync = false;
        else if (a == "--trees") o.trees = std::strcmp(next(), "all") == 0 ? kAllTreeStyles : std::atoi(argv[i]);
        else if (a == "--profile") o.profile = std::atoi(next()), o.stats = true;
        else if (a == "--msaa") o.msaa = std::atoi(next());
        else if (a == "--shot") {
            o.shot = next();
            o.shotFrame = 300;
            const size_t colon = o.shot.rfind(':');
            if (colon != std::string::npos) {
                o.shotFrame = std::atoi(o.shot.c_str() + colon + 1);
                o.shot.resize(colon);
            }
        } else if (a == "--camera") {
            std::array<float, 4> c{};
            if (std::sscanf(next(), "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]) == 4) o.cameras.push_back(c);
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

// The last frame's drawing, as "name draws/triangles" for each kind that drew, and the totals.
struct DrawCount {
    std::string name;
    int draws = 0;
    long triangles = 0;
};
std::vector<DrawCount> drawCounts() {
    std::vector<DrawCount> out;
    const char* text = illyria_draw_counts();
    while (*text) {
        char name[32];
        int draws = 0, used = 0;
        long triangles = 0;
        if (std::sscanf(text, "%31s %d %ld%n", name, &draws, &triangles, &used) < 3) break;
        out.push_back({name, draws, triangles});
        text += used;
        while (*text == '\n') ++text;
    }
    return out;
}

std::string drawSummary() {
    std::string out;
    int draws = 0;
    long triangles = 0;
    for (const DrawCount& c : drawCounts()) {
        draws += c.draws;
        triangles += c.triangles;
        if (c.draws) out += " " + c.name + " " + std::to_string(c.draws) + "/" + std::to_string(c.triangles / 1000) + "k";
    }
    return "draws " + std::to_string(draws) + " tris " + std::to_string(triangles / 1000) + "k:" + out;
}

// --profile: what each kind of drawing costs, found by hiding it in turn for a while and seeing what the GPU saves
// against the undimmed frames on either side (the GPU's clock drifts, so one baseline would not do).
class Profile {
public:
    Profile(int start, int parts) : start_(start), parts_(parts) {}
    // Called each frame with the GPU time of a frame lately finished (0 if none); false once the table is printed.
    bool step(int frame, double rawGpuMs) {
        if (frame < start_) return true;
        const int phase = (frame - start_) / (kWarm + kSample), within = (frame - start_) % (kWarm + kSample);
        if (within == 0 && phase > 0) finish(phase - 1);
        if (phase > 2 * parts_) return false;
        if (within == 0) {
            illyria_hide_parts(phase % 2 ? 1u << (phase / 2) : 0u);
            sum_ = 0;
            n_ = 0;
        }
        if (within >= kWarm && rawGpuMs > 0) {
            sum_ += rawGpuMs;
            ++n_;
        }
        return true;
    }

private:
    static constexpr int kWarm = 20, kSample = 90;
    void finish(int phase) {
        const double ms = n_ ? sum_ / n_ : 0;
        if (phase % 2 == 0) {
            if (phase == 0) {
                counts_ = drawCounts();
                std::printf("profile: %s\n%-10s %6s %9s %8s\n", drawSummary().c_str(), "part", "draws", "triangles", "gpu ms");
            } else {
                const DrawCount& c = counts_[static_cast<size_t>(phase / 2 - 1)];
                std::printf("%-10s %6d %9ld %8.2f\n", c.name.c_str(), c.draws, c.triangles, (before_ + ms) / 2 - hidden_);
                std::fflush(stdout);
            }
            before_ = ms;
        } else {
            hidden_ = ms;
        }
    }
    int start_, parts_, n_ = 0;
    double sum_ = 0, before_ = 0, hidden_ = 0;
    std::vector<DrawCount> counts_;
};

}  // namespace

namespace illyria::platform {

double nowMs() { return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()); }

Canvas canvas() {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    return {static_cast<double>(w), static_cast<double>(h), static_cast<double>(SDL_GetWindowPixelDensity(window))};
}

// The frame is drawn at the size asked for: straight into the window's drawable when that is as big, else into a
// framebuffer of its own that present() scales up (dynamic resolution, as the page does by sizing its canvas).
namespace {
struct Offscreen {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
} off;
bool useOffscreen = false;
}  // namespace

bool resizeCanvas(int w, int h) {
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(window, &dw, &dh);
    useOffscreen = w < dw || h < dh;
    if (!useOffscreen) return true;
    if (!off.fbo) {
        glGenFramebuffers(1, &off.fbo);
        glGenTextures(1, &off.color);
        glGenRenderbuffers(1, &off.depth);
    }
    if (off.w != w || off.h != h) {
        off.w = w;
        off.h = h;
        glBindTexture(GL_TEXTURE_2D, off.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindRenderbuffer(GL_RENDERBUFFER, off.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, off.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, off.color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, off.depth);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    return true;
}

unsigned screenFramebuffer() { return useOffscreen ? off.fbo : 0; }

void present() {
    if (!useOffscreen) return;
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(window, &dw, &dh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, off.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, off.w, off.h, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

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
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, opt.msaa > 0 ? 1 : 0);  // (the page asks for antialiasing too)
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, opt.msaa);
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
    SDL_GL_SetSwapInterval(opt.vsync ? 1 : 0);
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
    if (opt.trees >= 0 && opt.trees != kAllTreeStyles) illyria_set_tree_style(opt.trees);
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
    bool cameraSet = false;
    double statsAt = platform::nowMs(), lastLoop = statsAt;
    // Where a frame goes: the whole loop (with the wait for the display), the CPU's share up to the swap, and the
    // GPU's own time for the frame's commands (a timer query, read a frame late).
    double loopMs = 16.7, cpuMs = 0, gpuMs = 0;
    constexpr int kRing = 6;  // (the GPU runs a few frames behind: read the oldest)
    GLuint queries[kRing] = {};
    glGenQueries(kRing, queries);
    bool queried[kRing] = {};
    std::string hudPerf;
    Profile profile(opt.profile, static_cast<int>(drawCounts().size()));
    double rawGpuMs = 0;
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
        // One camera is set once the game runs; several are a tour, each held kTourFrames with the close-up land built.
        const int tourStep = opt.cameras.size() > 1 && frame >= opt.shotFrame ? (frame - opt.shotFrame) / kTourFrames : 0;
        if (!opt.cameras.empty() && hudTick() > 0 && (opt.cameras.size() == 1 ? !cameraSet : frame >= opt.shotFrame && (frame - opt.shotFrame) % kTourFrames == 0)) {
            const std::array<float, 4>& c = opt.cameras[static_cast<size_t>(std::min<int>(tourStep, static_cast<int>(opt.cameras.size()) - 1))];
            illyria_set_camera(c[0], c[1], c[2], c[3]);
            if (opt.cameras.size() > 1) illyria_finish_detail();
            cameraSet = true;
        }
        const double cpuStart = platform::nowMs();
        const int slot = frame % kRing;
        glBeginQuery(GL_TIME_ELAPSED, queries[slot]);
        const bool drew = run::frame();
        if (drew) platform::present();
        const platform::Canvas cv = platform::canvas();
        if (drew) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            overlay.begin(pw, ph, static_cast<float>(cv.pixelRatio));
            hud.draw(overlay, static_cast<float>(cv.cssW), static_cast<float>(cv.cssH));
            overlay.end();
            cpuMs = cpuMs * 0.9 + (platform::nowMs() - cpuStart) * 0.1;
            if (!opt.shot.empty() && opt.trees == kAllTreeStyles) {
                // A picture of each style: set it, give the renderer a few frames, take it.
                const int k = (frame - opt.shotFrame) / 4, step = (frame - opt.shotFrame) % 4;
                if (frame >= opt.shotFrame && step == 0) illyria_set_tree_style(k);
                if (frame >= opt.shotFrame && step == 3) {
                    writeShot(opt.shot.substr(0, opt.shot.rfind('.')) + "-" + std::to_string(k) + ".tga", pw, ph);
                    running = k < 3;
                }
            } else if (!opt.shot.empty() && opt.cameras.size() > 1) {
                if (frame >= opt.shotFrame && (frame - opt.shotFrame) % kTourFrames == kTourFrames - 1) {
                    writeShot(opt.shot.substr(0, opt.shot.rfind('.')) + "-" + std::to_string(tourStep) + ".tga", pw, ph);
                    std::printf("camera %d: %s\n         %s\n", tourStep, hudPerf.c_str(), drawSummary().c_str());
                    running = tourStep + 1 < static_cast<int>(opt.cameras.size());
                }
            } else if (!opt.shot.empty() && frame == opt.shotFrame) {
                writeShot(opt.shot, pw, ph);
                running = false;
            }
            SDL_GL_SwapWindow(window);
        }
        glEndQuery(GL_TIME_ELAPSED);
        queried[slot] = true;
        const int oldest = (slot + 1) % kRing;
        rawGpuMs = 0;
        if (queried[oldest]) {
            GLuint ready = 0;
            glGetQueryObjectuiv(queries[oldest], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready) {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(queries[oldest], GL_QUERY_RESULT, &ns);
                if (ns > 100000) {
                    rawGpuMs = static_cast<double>(ns) / 1e6;
                    gpuMs = gpuMs * 0.9 + rawGpuMs * 0.1;
                }
                queried[oldest] = false;
            }
        }
        {
            const double t = platform::nowMs();
            loopMs = loopMs * 0.95 + (t - lastLoop) * 0.05;
            lastLoop = t;
            const char* perf = illyria_perf();  // "N fps . sim S . draw D ms . Rx"
            const char* sim = std::strstr(perf, "sim ");
            char line[160];
            std::snprintf(line, sizeof line, "%.0f fps  frame %.1f ms  cpu %.1f  gpu %.1f  sim %.2f/tick  tick %u", 1000.0 / std::max(loopMs, 0.1), loopMs, cpuMs,
                          gpuMs, sim ? std::atof(sim + 4) : 0.0, hudTick());
            hudPerf = line;
            hud.setPerf(line);
        }
        if (const GLenum err = glGetError(); err != GL_NO_ERROR && frame < 600) std::fprintf(stderr, "GL error 0x%x at frame %d\n", err, frame);
        if (opt.profile > 0 && !profile.step(frame, rawGpuMs)) running = false;
        ++frame;
        if (opt.quitFrame && frame >= opt.quitFrame) running = false;
        if (opt.stats && platform::nowMs() - statsAt > 2000) {
            statsAt = platform::nowMs();
            std::printf("frame %d: %s\n         %s\n         camera %s\n", frame, hudPerf.c_str(), drawSummary().c_str(), illyria_camera_state());
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
