#pragma once

// The --play script (docs/PLAYTEST.md): one command a line, "#" comments. This file is only the reader, with no SDL
// or game in it, so tests/test_playscript.cpp can check the grammar on every platform. The runner that feeds the
// commands into the game's own input path is playrun.h (a build with -DILLYRIA_DEV_HOOKS=ON only).

#include <string>
#include <vector>

namespace illyria::native::play {

enum class Op {
    Size,       // size W H            (window, in points)
    Start,      // start conquest | skirmish ford | skirmish siege | horde | ctf  [lord N] [easy]
    Key,        // key NAME [down|up|SECONDS]
    Mouse,      // mouse X Y
    Click,      // click left|right|middle [X Y]
    Drag,       // drag left|right|middle X1 Y1 X2 Y2 [SECONDS [HOLD]]
    Wheel,      // wheel DY            (notches; SDL's wheel.y: positive rolls the wheel away from you, which zooms in)
    Select,     // select all | N      (all: the soldiers on screen, as Ctrl+A; N: the squad card of that index)
    Shot,       // shot NAME [full]
    Stats,      // stats
    Cards,      // cards              (one line: each squad card's index, name, men alive; the `select N` numbers)
    Wait,       // wait SECONDS        (wall clock)
    WaitTicks,  // wait_ticks N        (simulation ticks, 30 a second)
    Speed,      // speed N
    Camera,     // camera X Y DISTANCE YAW   (battle metres, as --camera)
    Print,      // print TEXT...       (a line to stdout, to mark a place in the log)
    Quit,       // quit
};

enum class KeyAction { Press, Down, Up };

struct Command {
    Op op = Op::Quit;
    int line = 0;  // in the script, from 1
    double a = 0, b = 0, c = 0, d = 0, seconds = 0, hold = 0;  // the numbers, in the order written
    int button = 1;         // 1 left, 2 middle, 3 right (SDL's numbering)
    bool hasPoint = false;  // Click: X Y given
    bool all = false;       // Select all
    bool full = false;      // Shot: the drawable's pixels, not the window's points
    bool easy = false;      // Start
    int lord = -1;          // Start
    int scancode = 0;       // Key: the USB usage id, which is SDL's scancode
    KeyAction action = KeyAction::Press;
    std::string text;  // Start: the mode ("conquest", "skirmish ford", ...); Shot: the name; Key: the name; Print: the text
};

struct Script {
    std::vector<Command> commands;
    std::vector<std::string> errors;  // "line N: what is wrong"; a script with any error is not run
    bool ok() const { return errors.empty(); }
};

Script parse(const std::string& text);

// The USB usage id (SDL's scancode) of a key name, or 0 if it has none here: a-z, 0-9, space, tab, return, escape,
// backspace, backtick, slash, minus, equals, left/right/up/down, shift, ctrl, alt, cmd.
int scancodeOf(const std::string& name);

}  // namespace illyria::native::play
