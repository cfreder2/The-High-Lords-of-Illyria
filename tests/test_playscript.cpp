// The --play script's grammar (game/native/playscript.h, docs/PLAYTEST.md): the reader alone, no window.

#include "../game/native/playscript.h"
#include "test.h"

using namespace illyria::native::play;

TEST(playscript_reads_every_command) {
    const Script s = parse(
        "# a comment\n"
        "size 1600 900\n"
        "start skirmish ford\n"
        "key w down\n"
        "key w up\n"
        "key backtick\n"
        "key return 0.5   # held half a second\n"
        "mouse 800 450\n"
        "click right\n"
        "click left 10 20\n"
        "drag left 100 100 500 400\n"
        "drag middle 1 2 3 4 1.5\n"
        "wheel -3\n"
        "select all\n"
        "select 2\n"
        "shot ford-10 full\n"
        "stats\ncards\n"
        "wait 1.5\n"
        "wait_ticks 300\n"
        "speed 4\n"
        "camera 10 -20 150 0.5\n"
        "print the clash begins\n"
        "quit\n");
    CHECK(s.ok());
    CHECK(s.commands.size() == 23);
    if (s.commands.size() != 22) return;
    CHECK(s.commands[0].op == Op::Size && s.commands[0].a == 1600 && s.commands[0].b == 900 && s.commands[0].line == 2);
    CHECK(s.commands[1].op == Op::Start && s.commands[1].text == "skirmish ford");
    CHECK(s.commands[2].op == Op::Key && s.commands[2].action == KeyAction::Down && s.commands[2].scancode == 26);
    CHECK(s.commands[3].action == KeyAction::Up);
    CHECK(s.commands[4].action == KeyAction::Press && s.commands[4].scancode == 53);
    CHECK(s.commands[5].scancode == 40 && s.commands[5].seconds == 0.5);
    CHECK(s.commands[6].op == Op::Mouse && s.commands[6].a == 800 && s.commands[6].b == 450);
    CHECK(s.commands[7].op == Op::Click && s.commands[7].button == 3 && !s.commands[7].hasPoint);
    CHECK(s.commands[8].button == 1 && s.commands[8].hasPoint && s.commands[8].a == 10 && s.commands[8].b == 20);
    CHECK(s.commands[9].op == Op::Drag && s.commands[9].c == 500 && s.commands[9].d == 400 && s.commands[9].seconds > 0);
    CHECK(s.commands[10].button == 2 && s.commands[10].seconds == 1.5 && s.commands[10].hold == 0);
    const Script facing = parse("drag right 1 2 3 4\ndrag right 1 2 3 4 1 0\ndrag right 1 2 3 4 1 0.8\ndrag left 1 2 3 4");
    CHECK(facing.ok() && facing.commands.size() == 4);
    CHECK(facing.commands[0].hold > 0.3);  // (a right press must be held still to be a facing line)
    CHECK(facing.commands[1].hold == 0);
    CHECK(facing.commands[2].hold == 0.8);
    CHECK(facing.commands[3].hold == 0);
    CHECK(s.commands[11].op == Op::Wheel && s.commands[11].a == -3);
    CHECK(s.commands[12].op == Op::Select && s.commands[12].all);
    CHECK(s.commands[13].op == Op::Select && !s.commands[13].all && s.commands[13].a == 2);
    CHECK(s.commands[14].op == Op::Shot && s.commands[14].text == "ford-10" && s.commands[14].full);
    CHECK(s.commands[15].op == Op::Stats);
    CHECK(s.commands[16].op == Op::Cards);
    CHECK(s.commands[17].op == Op::Wait && s.commands[17].seconds == 1.5);
    CHECK(s.commands[18].op == Op::WaitTicks && s.commands[18].a == 300);
    CHECK(s.commands[19].op == Op::Speed && s.commands[19].a == 4);
    CHECK(s.commands[20].op == Op::Camera && s.commands[20].c == 150);
    CHECK(s.commands[21].op == Op::Print && s.commands[21].text == "the clash begins");
    CHECK(s.commands[22].op == Op::Quit);
}

TEST(playscript_start_modes) {
    CHECK(parse("start conquest").commands[0].text == "conquest");
    CHECK(parse("start campaign").commands[0].text == "conquest");
    CHECK(parse("start skirmish siege").commands[0].text == "skirmish siege");
    CHECK(parse("start siege").commands[0].text == "skirmish siege");
    const Script s = parse("start conquest lord 3 easy");
    CHECK(s.ok() && s.commands[0].lord == 3 && s.commands[0].easy);
    CHECK(!parse("start skirmish").ok());
    CHECK(!parse("start skirmish moat").ok());
    CHECK(!parse("start conquest lord 9").ok());
    CHECK(!parse("start tetris").ok());
    CHECK(!parse("start").ok());
}

TEST(playscript_names_the_bad_line) {
    const Script s = parse("size 1600 900\nclick sideways\nwait soon\n\nkey nosuchkey\nfrobnicate\n");
    CHECK(!s.ok());
    CHECK(s.errors.size() == 4);
    if (s.errors.size() != 4) return;
    CHECK(s.errors[0].rfind("line 2:", 0) == 0);
    CHECK(s.errors[1].rfind("line 3:", 0) == 0);
    CHECK(s.errors[2].rfind("line 5:", 0) == 0);
    CHECK(s.errors[3].rfind("line 6:", 0) == 0);
    CHECK(s.commands.size() == 1);
}

TEST(playscript_refuses_wrong_argument_counts) {
    CHECK(!parse("size 1600").ok());
    CHECK(!parse("size 10 10").ok());  // too small to be a window
    CHECK(!parse("mouse 1").ok());
    CHECK(!parse("click left 5").ok());
    CHECK(!parse("drag left 1 2 3").ok());
    CHECK(!parse("wait -1").ok());
    CHECK(!parse("speed 3").ok());
    CHECK(!parse("shot").ok());
    CHECK(!parse("shot a b").ok());
    CHECK(!parse("stats now").ok());
    CHECK(!parse("select").ok());
    CHECK(!parse("select first").ok());
}

TEST(playscript_key_names) {
    CHECK(scancodeOf("a") == 4 && scancodeOf("Z") == 29);
    CHECK(scancodeOf("1") == 30 && scancodeOf("9") == 38 && scancodeOf("0") == 39);
    CHECK(scancodeOf("space") == 44 && scancodeOf("tab") == 43 && scancodeOf("return") == 40 && scancodeOf("escape") == 41);
    CHECK(scancodeOf("backtick") == 53 && scancodeOf("`") == 53 && scancodeOf("slash") == 56);
    CHECK(scancodeOf("left") == 80 && scancodeOf("right") == 79 && scancodeOf("up") == 82 && scancodeOf("down") == 81);
    CHECK(scancodeOf("shift") == 235 && scancodeOf("ctrl") == 234);
    CHECK(scancodeOf("f1") == 0);  // (the game binds no function keys)
    CHECK(scancodeOf("") == 0);
}

TEST(playscript_comments_and_blank_lines) {
    const Script s = parse("\n   \n# only a comment\n  wait 1  # trailing\n\r\nquit\r\n");
    CHECK(s.ok());
    CHECK(s.commands.size() == 2);
    CHECK(s.commands[0].line == 4);
}
