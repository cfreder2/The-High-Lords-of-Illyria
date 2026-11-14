#pragma once

// --play (docs/PLAYTEST.md): the script of playscript.h fed into the game's own input path, so an automated player can
// fight a battle with no one at the keyboard. Every key and click is an SDL event handed to Input::handle, the function
// the window's real events go to, so the game behaves as for a hand on the mouse; the window need be neither focused nor
// visible. Real keyboard and mouse events are dropped while a script runs (they would be a second player).
//
// A build with -DILLYRIA_DEV_HOOKS=ON only. Nothing here runs unless --play is given, and nothing here touches the
// simulation: it reads the HUD's numbers and sends input, as a person does.

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

#include "hud.h"
#include "input.h"
#include "playscript.h"

namespace illyria::native::play {

struct Setup {
    std::string outDir;   // where shots go (default: beside the script)
    std::string logPath;  // --play-log
    int lord = 6;         // `start conquest` as --lord, --foes, --easy
    int foes = 0xFF & ~(1 << 6);
    bool hard = true;
};

class Runner {
public:
    Runner(Script script, SDL_Window* window, Input& input, Hud& hud, Setup setup);
    ~Runner();

    // The frame rate and where the frame went (the loop's own figures), once a frame.
    void perf(double fps, double loopMs, double cpuMs, double gpuMs);
    // Once a frame before the game updates: sends what is due. False once the script has ended (quit, or its last line).
    bool step();
    // Once a frame after the frame is drawn and before it is swapped: a `shot` waiting is written. (pw, ph: the drawable's pixels.)
    void afterDraw(int pw, int ph);
    // The window's own keyboard and mouse events are not the script's: true for the ones to drop.
    static bool isRealInput(const SDL_Event& e);
    int exitCode() const { return exit_; }

private:
    enum class Block { None, Until, WaitTicks, Drag, Start, Shot };
    enum class After { None, KeyUp, MouseUp };

    double now() const;
    void send(const SDL_Event& e);
    void key(int scancode, bool down);
    void motion(float x, float y);
    void button(int which, bool down);
    void execute(const Command& c);
    void stats();
    void cards();
    void log();
    bool writeShot(int pw, int ph);
    void finish(int code, const char* why);

    Script script_;
    SDL_Window* window_;
    Input& input_;
    Hud& hud_;
    Setup setup_;
    size_t next_ = 0;
    int exit_ = 0;
    bool ended_ = false;
    long frame_ = 0;

    Block block_ = Block::None;
    After after_ = After::None;
    double until_ = 0, lastDeadline_ = 0, blockedAt_ = 0;
    unsigned tickTarget_ = 0, ticksWaited_ = 0, lastTick_ = 0;
    int afterCode_ = 0;

    // The pointer and the modifiers, as the script has left them.
    float mx_ = 0, my_ = 0;
    unsigned buttons_ = 0;  // SDL_BUTTON_MASK bits down
    SDL_Keymod mod_ = SDL_KMOD_NONE;

    struct DragState {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        double start = 0, seconds = 0;
        int which = 1;
    } drag_;

    Command shot_;
    int shots_ = 0;
    int startFrames_ = 0;

    double fps_ = 0, loopMs_ = 0, cpuMs_ = 0, gpuMs_ = 0;

    FILE* logFile_ = nullptr;
    long logSecond_ = -1;
    int lastMine_ = -1, lastFoe_ = -1, lastOutcome_ = 0, lastDay_ = 0, lastBroken_ = 0;
    std::string lastNotice_;
    double wallStart_ = 0;
};

}  // namespace illyria::native::play
