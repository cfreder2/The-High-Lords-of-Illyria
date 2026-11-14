#include "playrun.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../render/gl.h"
#include "api.h"
#include "json.h"

namespace illyria::native::play {

namespace {

constexpr double kTap = 0.06;  // seconds a key or button is held for a tap
constexpr int kTicksPerSecond = 30;

Json parsed(const char* text) {
    Json j;
    if (!Json::parse(text, j)) j = Json();
    return j;
}

Json hudState() { return parsed(illyria_hud()); }

unsigned tickNow() { return static_cast<unsigned>(hudState().num("tick")); }

// The campaign's state when a conquest is on (it has a day), else an empty object.
Json campaignState() {
    Json j = parsed(illyria_campaign_state());
    return j.find("day") ? j : Json();
}

struct Men {
    int mineAlive = 0, mineTotal = 0, foeAlive = 0, foeTotal = 0, broken = 0;
};
Men men(const Json& hud) {
    Men m;
    for (const Json& s : hud.list("squads")) {
        m.mineAlive += static_cast<int>(s.num("alive"));
        m.mineTotal += static_cast<int>(s.num("total"));
        m.broken += static_cast<int>(s.num("broken") > 0);
    }
    m.foeAlive = static_cast<int>(hud.num("foeAlive"));
    m.foeTotal = static_cast<int>(hud.num("foeTotal"));
    return m;
}

SDL_Keymod modBit(int scancode) {
    switch (scancode) {
        case 224: return SDL_KMOD_LCTRL;
        case 225: return SDL_KMOD_LSHIFT;
        case 226: return SDL_KMOD_LALT;
        case 227: return SDL_KMOD_LGUI;
        default: return SDL_KMOD_NONE;
    }
}

}  // namespace

Runner::Runner(Script script, SDL_Window* window, Input& input, Hud& hud, Setup setup)
    : script_(std::move(script)), window_(window), input_(input), hud_(hud), setup_(std::move(setup)) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    mx_ = static_cast<float>(w) / 2;
    my_ = static_cast<float>(h) / 2;
    if (!setup_.outDir.empty()) SDL_CreateDirectory(setup_.outDir.c_str());
    if (!setup_.logPath.empty()) {
        logFile_ = std::fopen(setup_.logPath.c_str(), "w");
        if (!logFile_) std::fprintf(stderr, "play: cannot write the log %s\n", setup_.logPath.c_str());
    }
    wallStart_ = now();
    SDL_SetModState(SDL_KMOD_NONE);  // (the script's modifiers, not whatever the real keyboard holds)
}

Runner::~Runner() {
    if (logFile_) std::fclose(logFile_);
}

double Runner::now() const { return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency()); }

void Runner::perf(double fps, double loopMs, double cpuMs, double gpuMs) {
    fps_ = fps, loopMs_ = loopMs, cpuMs_ = cpuMs, gpuMs_ = gpuMs;
}

bool Runner::isRealInput(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL: case SDL_EVENT_PINCH_UPDATE: case SDL_EVENT_WINDOW_FOCUS_LOST:
            return true;
        default: return false;
    }
}

void Runner::send(const SDL_Event& e) { input_.handle(e); }

void Runner::key(int scancode, bool down) {
    if (const SDL_Keymod bit = modBit(scancode); bit != SDL_KMOD_NONE) {
        mod_ = static_cast<SDL_Keymod>(down ? (mod_ | bit) : (mod_ & ~bit));
        SDL_SetModState(mod_);
    }
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = static_cast<SDL_Scancode>(scancode);
    e.key.down = down;
    e.key.repeat = false;
    e.key.mod = mod_;
    e.key.which = 1;
    send(e);
}

void Runner::motion(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.which = 1;
    e.motion.state = buttons_;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - mx_;
    e.motion.yrel = y - my_;
    mx_ = x, my_ = y;
    send(e);
}

void Runner::button(int which, bool down) {
    const unsigned bit = SDL_BUTTON_MASK(which);
    buttons_ = down ? (buttons_ | bit) : (buttons_ & ~bit);
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.which = 1;
    e.button.button = static_cast<Uint8>(which);
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = mx_;
    e.button.y = my_;
    send(e);
}

void Runner::finish(int code, const char* why) {
    if (ended_) return;
    ended_ = true;
    if (code) exit_ = code;
    if (why && *why) std::printf("play: %s\n", why);
    std::fflush(stdout);
}

bool Runner::step() {
    if (ended_) return false;
    ++frame_;
    log();
    for (;;) {
        const double t = now();
        switch (block_) {
            case Block::None: break;
            case Block::Until:
                if (t < until_) return true;
                lastDeadline_ = until_;
                block_ = Block::None;
                if (after_ == After::KeyUp) key(afterCode_, false);
                if (after_ == After::MouseUp) button(afterCode_, false);
                after_ = After::None;
                break;
            case Block::WaitTicks: {
                // The tick starts again from 0 when a Conquest's day turns over (the world on the map is a new one): count what passed.
                const unsigned tick = tickNow();
                ticksWaited_ += tick >= lastTick_ ? tick - lastTick_ : tick;
                lastTick_ = tick;
                if (ticksWaited_ < tickTarget_) {
                    // (a paused game or an open menu never gets there: say so rather than hang)
                    if (t < until_) return true;
                    std::printf("play: line %d: wait_ticks gave up after %.0f s (is the game paused, or a menu open?)\n", script_.commands[next_ - 1].line, t - blockedAt_);
                }
                lastDeadline_ = 0;
                block_ = Block::None;
                break;
            }
            case Block::Drag: {
                const double u = drag_.seconds > 0 ? std::min(1.0, (t - drag_.start) / drag_.seconds) : 1.0;
                const double v = std::max(0.0, u);
                const float x = drag_.x1 + static_cast<float>(v) * (drag_.x2 - drag_.x1), y = drag_.y1 + static_cast<float>(v) * (drag_.y2 - drag_.y1);
                if (u < 1.0) {
                    motion(x, y);
                    return true;
                }
                motion(drag_.x2, drag_.y2);
                block_ = Block::Until;
                after_ = After::MouseUp;
                afterCode_ = drag_.which;
                until_ = t + kTap;  // (a beat at the end, then the button up)
                return true;
            }
            case Block::Start: {
                ++startFrames_;
                const bool begun = tickNow() > 0 || campaignState().find("day") != nullptr;
                if (startFrames_ >= 4 && begun) {
                    block_ = Block::None;
                    lastDeadline_ = 0;
                    break;
                }
                if (t - blockedAt_ > 180) {
                    finish(3, "the start did not begin within 180 s");
                    return false;
                }
                return true;
            }
            case Block::Shot: return true;  // (afterDraw writes it, and clears this)
        }
        if (next_ >= script_.commands.size()) {
            finish(0, "script ended");
            return false;
        }
        execute(script_.commands[next_++]);
        if (ended_) return false;
    }
}

void Runner::execute(const Command& c) {
    const double t = now();
    switch (c.op) {
        case Op::Size: {
            SDL_SetWindowSize(window_, static_cast<int>(c.a), static_cast<int>(c.b));
            int w = 0, h = 0;
            SDL_SyncWindow(window_);
            SDL_GetWindowSize(window_, &w, &h);
            if (w != static_cast<int>(c.a) || h != static_cast<int>(c.b))
                std::printf("play: line %d: the window is %dx%d points, not the %dx%d asked (the display is smaller)\n", c.line, w, h, static_cast<int>(c.a), static_cast<int>(c.b));
            block_ = Block::Until;
            until_ = t + 0.3;
            break;
        }
        case Op::Start: {
            char buf[200];
            if (c.text == "conquest") {
                const int lord = c.lord >= 0 ? c.lord : setup_.lord;
                std::snprintf(buf, sizeof buf, "{\"hard\":%d,\"player\":%d,\"enemies\":%d,\"speed\":1}", (c.easy ? false : setup_.hard) ? 1 : 0, lord,
                              setup_.foes & ~(1 << lord) & 0xFF);
                hud_.start("campaign", buf);
            } else if (c.text == "skirmish ford") {
                hud_.start("skirmish", "{\"scenario\":0}");
            } else if (c.text == "skirmish siege") {
                hud_.start("skirmish", "{\"scenario\":1}");
            } else {
                hud_.start(c.text, "{}");
            }
            block_ = Block::Start;
            blockedAt_ = t;
            startFrames_ = 0;
            break;
        }
        case Op::Key:
            if (c.action == KeyAction::Down) key(c.scancode, true);
            else if (c.action == KeyAction::Up) key(c.scancode, false);
            else {
                key(c.scancode, true);
                block_ = Block::Until;
                after_ = After::KeyUp;
                afterCode_ = c.scancode;
                until_ = t + std::max(c.seconds, kTap);
            }
            break;
        case Op::Mouse: motion(static_cast<float>(c.a), static_cast<float>(c.b)); break;
        case Op::Click:
            if (c.hasPoint) motion(static_cast<float>(c.a), static_cast<float>(c.b));
            button(c.button, true);
            block_ = Block::Until;
            after_ = After::MouseUp;
            afterCode_ = c.button;
            until_ = t + kTap;
            break;
        case Op::Drag:
            motion(static_cast<float>(c.a), static_cast<float>(c.b));
            button(c.button, true);
            drag_ = {static_cast<float>(c.a), static_cast<float>(c.b), static_cast<float>(c.c), static_cast<float>(c.d), t + std::max(kTap, c.hold), c.seconds, c.button};
            block_ = Block::Drag;
            break;
        case Op::Wheel: {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.which = 1;
            e.wheel.y = static_cast<float>(c.a);
            e.wheel.mouse_x = mx_;
            e.wheel.mouse_y = my_;
            send(e);
            break;
        }
        case Op::Select:
            if (c.all) illyria_select_all();
            else illyria_select_squad(static_cast<int>(c.a));
            break;
        case Op::Shot:
            shot_ = c;
            block_ = Block::Shot;
            break;
        case Op::Stats: stats(); break;
        case Op::Cards: cards(); break;
        case Op::Wait:
            // Waits follow on from one another without drift: a shot's own time is part of the wait before it.
            block_ = Block::Until;
            until_ = (lastDeadline_ > t - 0.5 ? lastDeadline_ : t) + c.seconds;
            break;
        case Op::WaitTicks:
            tickTarget_ = static_cast<unsigned>(c.a);
            ticksWaited_ = 0;
            lastTick_ = tickNow();
            blockedAt_ = t;
            until_ = t + 30 + c.a / kTicksPerSecond * 20;
            block_ = Block::WaitTicks;
            break;
        case Op::Speed:
            if (campaignState().find("day")) {
                for (int i = 0; i < 4 && static_cast<int>(campaignState().num("speed")) != static_cast<int>(c.a); ++i) illyria_campaign_speed();
            } else {
                illyria_set_pace(static_cast<float>(c.a));
            }
            break;
        case Op::Camera: illyria_set_camera(static_cast<float>(c.a), static_cast<float>(c.b), static_cast<float>(c.c), static_cast<float>(c.d)); break;
        case Op::Print: std::printf("play: %s\n", c.text.c_str()), std::fflush(stdout); break;
        case Op::Quit: finish(0, "quit"); break;
    }
}

void Runner::stats() {
    const Json hud = hudState(), cam = parsed(illyria_camera_state());
    const Json camp = campaignState();
    const Men m = men(hud);
    const char* perf = illyria_perf();
    const char* sim = std::strstr(perf, "sim ");
    char line[640];
    int n = std::snprintf(line, sizeof line,
                          "stats t=%.1f tick=%u fps=%.0f frame_ms=%.1f cpu_ms=%.1f gpu_ms=%.1f sim_ms=%.2f mine=%d/%d foe=%d/%d outcome=%d selected=%d paused=%d speed=%g "
                          "cam_x=%.1f cam_y=%.1f cam_height=%.0f cam_yaw=%.3f",
                          now() - wallStart_, static_cast<unsigned>(hud.num("tick")), fps_, loopMs_, cpuMs_, gpuMs_, sim ? std::atof(sim + 4) : 0.0, m.mineAlive, m.mineTotal,
                          m.foeAlive, m.foeTotal, static_cast<int>(hud.num("outcome")), static_cast<int>(hud.num("selCount")), hud.flag("paused") ? 1 : 0,
                          hud.num("slow", 1) < 0.999 ? hud.num("slow", 1) : (camp.find("day") ? camp.num("speed", 1) : hud.num("speed", 1)), cam.num("x"), cam.num("y"),
                          cam.num("distance"), cam.num("yaw"));
    if (camp.find("day"))
        n += std::snprintf(line + n, sizeof line - static_cast<size_t>(n), " day=%d gold=%d cities=%d winner=%d", static_cast<int>(camp.num("day")), static_cast<int>(camp.num("gold")),
                           static_cast<int>(camp.num("cities")), static_cast<int>(camp.num("winner")));
    std::printf("%s\n", line);
    std::fflush(stdout);
}

void Runner::cards() {
    const Json hud = hudState();
    std::string out = "cards";
    for (const Json& s : hud.list("squads"))
        out += " " + std::to_string(static_cast<int>(s.num("i"))) + ":" + s.str("name") + "(" + s.str("type") + ")=" + std::to_string(static_cast<int>(s.num("alive"))) + "/" +
               std::to_string(static_cast<int>(s.num("total"))) + (s.num("sel") == 2 ? "*" : s.num("sel") == 1 ? "+" : "") + (s.flag("mine") ? "" : "(ally)");
    std::printf("%s\n", out.c_str());
    std::fflush(stdout);
}

// One line a second of game time (the sim's 30 ticks), and one when the day, the outcome or a notice changes.
void Runner::log() {
    if (!logFile_) return;
    const Json hud = hudState();
    const unsigned tick = static_cast<unsigned>(hud.num("tick"));
    const Json camp = campaignState();
    if (tick == 0 && !camp.find("day")) return;  // (the title, or a start not yet begun)
    const long second = static_cast<long>(tick / kTicksPerSecond);  // (in a Conquest, of the day: the tick starts from 0 each day)
    const bool day = camp.find("day") != nullptr;
    const int dayNo = day ? static_cast<int>(camp.num("day")) : 0;
    const int outcome = static_cast<int>(hud.num("outcome"));
    const std::string notice = hud.str("notice");
    if (second == logSecond_ && dayNo == lastDay_ && outcome == lastOutcome_ && (notice.empty() || notice == lastNotice_)) return;
    const Men m = men(hud);
    std::string events;
    if (lastMine_ >= 0 && m.mineAlive < lastMine_) events += " lost " + std::to_string(lastMine_ - m.mineAlive);
    if (lastFoe_ >= 0 && m.foeAlive < lastFoe_) events += " killed " + std::to_string(lastFoe_ - m.foeAlive);
    if (m.broken != lastBroken_) events += " broken=" + std::to_string(m.broken);
    if (!notice.empty() && notice != lastNotice_) events += " notice=\"" + notice + "\"";
    if (outcome != lastOutcome_) events += " outcome=" + std::to_string(outcome);
    if (dayNo != lastDay_ && day) events += " day=" + std::to_string(dayNo) + " gold=" + std::to_string(static_cast<int>(camp.num("gold"))) + " cities=" + std::to_string(static_cast<int>(camp.num("cities")));
    if (const Json* cap = hud.find("capture"); cap && cap->flag("on")) events += " capture=" + std::to_string(static_cast<int>(cap->num("p"))) + "%" + (cap->flag("taken") ? "(taken)" : "");
    std::fprintf(logFile_, "tick=%u t=%ld fps=%.0f frame_ms=%.1f mine=%d foe=%d%s\n", tick, second, fps_, loopMs_, m.mineAlive, m.foeAlive, events.c_str());
    std::fflush(logFile_);
    logSecond_ = second;
    lastMine_ = m.mineAlive, lastFoe_ = m.foeAlive, lastBroken_ = m.broken, lastOutcome_ = outcome, lastDay_ = dayNo;
    lastNotice_ = notice;
}

bool Runner::writeShot(int pw, int ph) {
    std::string name = shot_.text;
    const auto ends = [&](const char* ext) { return name.size() > 4 && name.compare(name.size() - 4, 4, ext) == 0; };
    if (!ends(".png")) name += ".png";
    const std::string path = setup_.outDir.empty() ? name : setup_.outDir + "/" + name;
    std::vector<unsigned char> px(static_cast<size_t>(pw) * static_cast<size_t>(ph) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // (GL's rows run bottom to top)
    std::vector<unsigned char> flipped(px.size());
    const size_t row = static_cast<size_t>(pw) * 4;
    for (int y = 0; y < ph; ++y) std::memcpy(flipped.data() + static_cast<size_t>(y) * row, px.data() + static_cast<size_t>(ph - 1 - y) * row, row);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    SDL_Surface* s = SDL_CreateSurfaceFrom(pw, ph, SDL_PIXELFORMAT_RGBA32, flipped.data(), static_cast<int>(row));
    if (!s) return false;
    int lw = 0, lh = 0;
    SDL_GetWindowSize(window_, &lw, &lh);
    bool ok;
    if (!shot_.full && (lw != pw || lh != ph)) {
        SDL_Surface* small = SDL_ScaleSurface(s, lw, lh, SDL_SCALEMODE_LINEAR);  // (a Retina frame is twice the window's points each way)
        ok = small && SDL_SavePNG(small, path.c_str());
        SDL_DestroySurface(small);
    } else {
        ok = SDL_SavePNG(s, path.c_str());
    }
    SDL_DestroySurface(s);
    if (!ok) std::fprintf(stderr, "play: cannot write %s: %s\n", path.c_str(), SDL_GetError());
    else std::printf("play: shot %s\n", path.c_str());
    std::fflush(stdout);
    return ok;
}

void Runner::afterDraw(int pw, int ph) {
    if (block_ != Block::Shot) return;
    ++shots_;
    if (!writeShot(pw, ph) && exit_ == 0) exit_ = 4;
    block_ = Block::None;
}

}  // namespace illyria::native::play
