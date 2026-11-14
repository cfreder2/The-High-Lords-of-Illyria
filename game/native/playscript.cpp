#include "playscript.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace illyria::native::play {

int scancodeOf(const std::string& raw) {
    std::string n;
    for (char c : raw) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n.size() == 1) {
        const char c = n[0];
        if (c >= 'a' && c <= 'z') return 4 + (c - 'a');
        if (c >= '1' && c <= '9') return 30 + (c - '1');
        if (c == '0') return 39;
        if (c == '`') return 53;
        if (c == '/') return 56;
        if (c == '-') return 45;
        if (c == '=') return 46;
    }
    static const struct {
        const char* name;
        int code;
    } kNames[] = {{"return", 40}, {"enter", 40}, {"escape", 41}, {"esc", 41}, {"backspace", 42}, {"tab", 43},  {"space", 44},
                  {"minus", 45},  {"equals", 46}, {"backtick", 53}, {"grave", 53}, {"slash", 56}, {"right", 79}, {"left", 80},
                  {"down", 81},   {"up", 82},     {"ctrl", 224},   {"shift", 225}, {"alt", 226},  {"cmd", 227}};
    for (const auto& k : kNames)
        if (n == k.name) return k.code;
    return 0;
}

namespace {

bool number(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != nullptr && *end == 0;
}

int buttonOf(const std::string& s) {
    if (s == "left") return 1;
    if (s == "middle") return 2;
    if (s == "right") return 3;
    return 0;
}

// One line, read: false (with `why` set) if it is not a command of the grammar.
bool command(const std::vector<std::string>& w, Command& c, std::string& why) {
    const std::string& verb = w[0];
    const size_t n = w.size() - 1;  // arguments
    double v[4] = {};
    // Arguments from..from+count-1 as numbers.
    const auto nums = [&](size_t from, size_t count) {
        if (n < from + count - 1) return why = verb + " needs more arguments", false;
        for (size_t i = 0; i < count; ++i)
            if (!number(w[from + i], v[i])) return why = "'" + w[from + i] + "' is not a number", false;
        return true;
    };
    const auto usage = [&](const char* text) { return why = std::string("usage: ") + text, false; };

    if (verb == "size") {
        c.op = Op::Size;
        if (n != 2) return usage("size W H");
        if (!nums(1, 2)) return false;
        if (v[0] < 320 || v[1] < 200 || v[0] > 8000 || v[1] > 8000) return why = "size is out of range", false;
        c.a = v[0], c.b = v[1];
    } else if (verb == "start") {
        c.op = Op::Start;
        static const char* kUsage = "start conquest | skirmish ford | skirmish siege | horde | ctf [lord 0-7] [easy]";
        if (n < 1) return usage(kUsage);
        size_t i = 1;
        std::string mode = w[i++];
        if (mode == "skirmish") {
            if (i > n || (w[i] != "ford" && w[i] != "siege")) return usage(kUsage);
            mode += " " + w[i++];
        } else if (mode == "ford" || mode == "siege") {
            mode = "skirmish " + mode;
        } else if (mode == "conquest") {
            mode = "conquest";
        } else if (mode != "conquest" && mode != "horde" && mode != "ctf") {
            return why = "unknown mode '" + mode + "'", false;
        }
        c.text = mode;
        for (; i <= n; ++i) {
            if (w[i] == "easy") c.easy = true;
            else if (w[i] == "lord" && i < n && number(w[i + 1], v[0]) && v[0] >= 0 && v[0] <= 7) c.lord = static_cast<int>(v[0]), ++i;
            else return why = "start: what is '" + w[i] + "'? (lord 0-7, easy)", false;
        }
    } else if (verb == "key") {
        c.op = Op::Key;
        if (n < 1 || n > 2) return usage("key NAME [down|up|SECONDS]");
        c.text = w[1];
        c.scancode = scancodeOf(w[1]);
        if (c.scancode == 0) return why = "no key called '" + w[1] + "'", false;
        if (n == 2) {
            if (w[2] == "down") c.action = KeyAction::Down;
            else if (w[2] == "up") c.action = KeyAction::Up;
            else if (!number(w[2], c.seconds) || c.seconds < 0) return usage("key NAME [down|up|SECONDS]");
        }
    } else if (verb == "mouse") {
        c.op = Op::Mouse;
        if (n != 2) return usage("mouse X Y");
        if (!nums(1, 2)) return false;
        c.a = v[0], c.b = v[1];
    } else if (verb == "click") {
        c.op = Op::Click;
        if (n != 1 && n != 3) return usage("click left|right|middle [X Y]");
        if (!(c.button = buttonOf(w[1]))) return why = "button is left, right or middle", false;
        if (n == 3) {
            if (!nums(2, 2)) return false;
            c.a = v[0], c.b = v[1], c.hasPoint = true;
        }
    } else if (verb == "drag") {
        c.op = Op::Drag;
        if (n < 5 || n > 7) return usage("drag left|right|middle X1 Y1 X2 Y2 [SECONDS [HOLD]]");
        if (!(c.button = buttonOf(w[1]))) return why = "button is left, right or middle", false;
        if (!nums(2, 4)) return false;
        c.a = v[0], c.b = v[1], c.c = v[2], c.d = v[3];
        c.seconds = 0.4;
        c.hold = c.button == 3 ? 0.4 : 0;  // (a right press held still 0.3 s is a facing line; sooner, it turns the view)
        if (n >= 6 && (!number(w[6], c.seconds) || c.seconds < 0)) return why = "'" + w[6] + "' is not a time", false;
        if (n == 7 && (!number(w[7], c.hold) || c.hold < 0)) return why = "'" + w[7] + "' is not a time", false;
    } else if (verb == "wheel") {
        c.op = Op::Wheel;
        if (n != 1) return usage("wheel DY");
        if (!nums(1, 1)) return false;
        c.a = v[0];
    } else if (verb == "select") {
        c.op = Op::Select;
        if (n != 1) return usage("select all | N");
        if (w[1] == "all") c.all = true;
        else if (!number(w[1], c.a) || c.a < 0) return usage("select all | N");
    } else if (verb == "shot") {
        c.op = Op::Shot;
        if (n < 1 || n > 2 || (n == 2 && w[2] != "full")) return usage("shot NAME [full]");
        c.text = w[1];
        c.full = n == 2;
    } else if (verb == "stats") {
        c.op = Op::Stats;
        if (n) return usage("stats");
    } else if (verb == "cards") {
        c.op = Op::Cards;
        if (n) return usage("cards");
    } else if (verb == "wait" || verb == "wait_ticks") {
        c.op = verb == "wait" ? Op::Wait : Op::WaitTicks;
        if (n != 1) return usage((verb + " N").c_str());
        if (!nums(1, 1)) return false;
        if (v[0] < 0) return why = verb + " cannot be negative", false;
        c.seconds = c.a = v[0];
    } else if (verb == "speed") {
        c.op = Op::Speed;
        if (n != 1) return usage("speed N");
        if (!nums(1, 1)) return false;
        if (v[0] != 0.25 && v[0] != 0.5 && v[0] != 1 && v[0] != 2 && v[0] != 4 && v[0] != 8) return why = "speed is 0.25, 0.5, 1, 2, 4 (8 in the conquest)", false;
        c.a = v[0];
    } else if (verb == "camera") {
        c.op = Op::Camera;
        if (n != 4) return usage("camera X Y DISTANCE YAW");
        if (!nums(1, 4)) return false;
        c.a = v[0], c.b = v[1], c.c = v[2], c.d = v[3];
    } else if (verb == "print") {
        c.op = Op::Print;
        for (size_t i = 1; i <= n; ++i) c.text += (i > 1 ? " " : "") + w[i];
    } else if (verb == "quit") {
        c.op = Op::Quit;
    } else {
        return why = "unknown command '" + verb + "'", false;
    }
    return true;
}

}  // namespace

Script parse(const std::string& text) {
    Script script;
    std::istringstream in(text);
    std::string raw;
    int line = 0;
    while (std::getline(in, raw)) {
        ++line;
        if (const size_t hash = raw.find('#'); hash != std::string::npos) raw.resize(hash);
        std::istringstream words(raw);
        std::vector<std::string> w;
        for (std::string t; words >> t;) w.push_back(t);
        if (w.empty()) continue;
        Command c;
        c.line = line;
        std::string why;
        if (command(w, c, why)) script.commands.push_back(c);
        else script.errors.push_back("line " + std::to_string(line) + ": " + why);
    }
    return script;
}

}  // namespace illyria::native::play
