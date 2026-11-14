# Playing a script: `--play`

`Illyria --play script.txt` plays the game from a text script, with no one at the keyboard and no need for the window
to be in front, focused or even uncovered. It exists so an automated reviewer can fight a battle and write down how it
felt, instead of only looking at `--shot` pictures.

It is a developer hook: only a build made with `-DILLYRIA_DEV_HOOKS=ON` has it (the same switch as `--paths-demo`
and `--garrison-demo`). A normal build has none of this code.

## How it works

Every key, click, drag and wheel turn in a script is an SDL event handed to `Input::handle`, the function the window's own
events go to (`game/native/input.cpp`). So the game does exactly what it would for a hand on the mouse: the HUD gets the
first look at a click, the gestures tell a tap from a box, the key map is the real one. The simulation is not touched;
the harness reads the same JSON the HUD reads (`illyria_hud`, `illyria_conquest_state`) for `stats` and the log.

- **Real input is ignored while a script runs.** A stray keystroke is not a second player.
- **The window takes no focus.** The app runs without a Dock icon and its window cannot become key, so starting a
  script does not pull typing away from whatever you were doing. The GL context still renders into the window.
- **No sound**, and **no saved game or settings are read or written** (a played conquest never overwrites yours).
- **Modifiers are the script's.** `key shift down` sets the keyboard modifier state the game reads, whatever the real
  keyboard holds.
- **Coordinates are window points, the units the game's own input uses** (a 1600 x 900 window is 1600 x 900 points,
  3200 x 1800 pixels on a Retina display). No conversion is needed, in either direction. The origin is the window's top left.
- **Shots are in the same points** (a 1600 x 900 PNG); add `full` for the drawable's own pixels.

## Running

```
cmake -S . -B build-play -DILLYRIA_BUILD_APP=ON -DILLYRIA_DEV_HOOKS=ON -DILLYRIA_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-play --target illyria_app -j 8

# macOS: the program inside the bundle
build-play/game/native/Illyria.app/Contents/MacOS/Illyria --play tools/playtest/ford.txt \
    --play-out ~/Desktop/playtest/ford --play-log ~/Desktop/playtest/ford/ford.log
```

| option | |
| --- | --- |
| `--play FILE` | the script. A bad line is named (`line 7: no key called 'f1'`) and nothing runs (exit 2). |
| `--play-out DIR` | where `shot` writes (default: the script's folder). Made if missing. |
| `--play-log FILE` | one line a second of game time, see below. |
| `--size WxH`, `--novsync`, `--lord N`, `--foes MASK`, `--easy` | as for the game; `--lord`, `--foes` and `--easy` are what `start conquest` uses unless the script says otherwise. |

The program prints `play: shot <path>` for each picture and `stats ...` lines to stdout, and exits when the script
reaches `quit` or its last line. Exit code 0 is a script that ran to its end; 2 a script that did not parse; 3 a `start` that
never began; 4 a shot that could not be written.

Give the program the whole of the run: `wait`s are wall-clock, and the game runs at the speed of the machine. For
repeatable scripts count the game's own time with `wait_ticks`.

## Grammar

One command a line. `#` starts a comment. Names are lower case. Numbers are plain (`1600`, `0.5`).

| command | does |
| --- | --- |
| `size W H` | the window, in points. Do this before `start`. (A display smaller than that gives you what fits, and says so.) |
| `start conquest [lord N] [easy]` | the menu's Conquest. `lord` 0-7 is whom you lead (default 6, the Moonward Order). Also `conquest`. |
| `start skirmish ford` / `start skirmish siege` | the menu's Skirmish at the Ford, and the Siege. (`start ford`, `start siege` too.) Also `start horde`, `start ctf`. |
| `key NAME` | a tap: down, a beat (0.06 s), up. |
| `key NAME SECONDS` | held down that long (camera keys: `key w 1.5`). |
| `key NAME down` / `key NAME up` | down, and later up: for modifiers (`shift`, `ctrl`, `alt`, `cmd`) and keys held across other commands. |
| `mouse X Y` | move the pointer (one motion event). |
| `click left\|right\|middle [X Y]` | move there (if given) and click: down, 0.06 s, up. Left selects, right orders, middle pans. |
| `drag left\|right\|middle X1 Y1 X2 Y2 [SECONDS [HOLD]]` | press at the first point, hold still HOLD seconds, move to the second over SECONDS (default 0.4) in many small motions, release. Left: a selection box. Right: HOLD defaults to 0.4, because a right press held still 0.3 s is a march with a facing (the soldiers end facing along the drag). With `HOLD` 0 a short right drag is a plain march to where it began (seen in play; the view did not turn). |
| `wheel DY` | wheel notches at the pointer; positive rolls away from you (zooms in), negative toward (out). |
| `select all` | every soldier your side commands (Ctrl+A takes only those on screen; use `key ctrl down` ... for that). |
| `select N` | the squad card with index N: what clicking that card does. `cards` lists the indices. |
| `spread` | prints one line: how many of your men are alive and their extent: `x=MIN..MAX (w width, 5-95 the middle nine in ten)`, the same for `y`, and the median position. For frontage and depth of a march. |
| `cards` | prints one line: `cards 0:Name(type)=alive/total* ...` (`*` all selected, `+` some; `(ally)` not yours to order). |
| `stats` | prints one line of `key=value`: see below. |
| `shot NAME [full]` | writes `NAME.png` (into `--play-out`). The shot is of the frame as the player sees it, HUD included. |
| `wait SECONDS` | wall-clock time. Consecutive waits do not drift: the time a `shot` takes is part of the wait before it. |
| `wait_ticks N` | N simulation ticks (30 a second) of the game, however fast or slow the machine. In a Conquest the tick starts again from 0 each day (3600 ticks); the wait counts across that. Gives up after a long wall time if the game is paused (and says so). |
| `speed N` | the game's pace: 0.25, 0.5, 1, 2, 4 (Conquest: 1, 2, 4, 8). As the `-` and `=` keys. |
| `camera X Y DISTANCE YAW` | puts the camera there, in battle metres, as `--camera`. A shortcut: a player would pan with the keys. |
| `print TEXT` | prints `play: TEXT` to stdout, to mark a place. |
| `quit` | ends the run. |

Key names: `a`-`z`, `0`-`9`, `space`, `tab`, `return` (opens the menu), `escape`, `backspace`, `backtick` (or `` ` ``: plan
orders), `slash` (help), `minus`, `equals`, `left`, `right`, `up`, `down`, `shift`, `ctrl`, `alt`, `cmd`. There are
no function keys (the game binds none). The game's keys are in the in-game help (`slash`).

There is no `text` command: the game takes no typed text.

### `stats`

```
stats t=41.2 tick=1080 fps=60 frame_ms=16.7 cpu_ms=4.1 gpu_ms=6.2 sim_ms=0.35 mine=118/150 foe=127/150 outcome=0 selected=12 paused=0 speed=1 cam_x=-8.0 cam_y=-60.0 cam_height=140 cam_yaw=0.000
```

`t` is wall seconds since the run began; `tick` the simulation's (0 until a battle has begun); `fps`, `frame_ms`,
`cpu_ms`, `gpu_ms` the HUD's own smoothed figures; `sim_ms` the cost of one simulation tick; `mine=alive/total` all the
squads on your side's cards (allies' too), `foe=alive/total` the others; `outcome` 0 while it is undecided;
`selected` the soldiers selected; `cam_*` the camera (battle metres; `cam_height` is its distance). In a Conquest
`day`, `gold`, `cities` and `winner` follow.

### The log

`--play-log FILE` writes a line each second of game time (each 30 ticks), and at once when the day, the outcome or a notice changes:

```
tick=300 t=10 fps=60 frame_ms=16.6 mine=150 foe=150
tick=330 t=11 fps=59 frame_ms=17.0 mine=150 foe=149 killed 1
tick=900 t=30 fps=41 frame_ms=24.1 mine=131 foe=120 lost 3 killed 7 broken=2 notice="Archers cannot reach there"
```

`lost` and `killed` are the men that fell on each side since the last line, `broken` the squads of yours that have
broken, `notice` what the HUD would show a player (a refused order's reason), `outcome` the result, `capture` the
town's capture point, `day=` the Conquest's day.

## Writing scripts that read well

- **Look first.** Start, `wait 1`, `shot first`, and look at the picture before choosing coordinates. A script that clicks
  where it guessed is a script that tests nothing. `cards` and `stats` say what is there.
- **Count in ticks.** `wait_ticks 300` is ten seconds of the game on any machine; `wait 10` is ten seconds of yours.
  Use `wait` for the start of a camera move, `wait_ticks` for the fight.
- **Hold a camera key, then let go**: `key w 1.2`. A held key is the real pan.
- **A box needs a drag long enough to be a box**: the default 0.4 s is, and the shape of it matters (the game picks
  the soldiers inside the screen rectangle, not the squad).
- **Facing**: a right-button `drag` is the facing line (held still 0.4 s first, as the game asks). A right `click` is a march to the point.
- **Queued orders**: `key backtick` starts planning, orders are then held (dashed), and `key backtick` again releases them.
  With `key shift down` a click queues after the last order instead.

## Examples

Under `tools/playtest/`. Each starts its own game and ends with `quit`.

| script | |
| --- | --- |
| `smoke.txt` | start the Ford, `cards`, `stats`, one shot. A check that the harness works on this machine. |
| `ford.txt` | the Ford: select all, box-select, march across the ford, wait for the clash, a shot every 10 s for 90 s, `stats` at the end. |
| `siege.txt` | the Siege: the siege engines and the foot to the gate, shots every 10 s, `stats`. |
| `conquest.txt` | Conquest: the capital, a stack, a march to the nearest neutral town, speed 4, a shot every 20 s for three game minutes. |

```
Illyria=build-play/game/native/Illyria.app/Contents/MacOS/Illyria
$Illyria --play tools/playtest/ford.txt --play-out out/ford --play-log out/ford/ford.log
$Illyria --play tools/playtest/siege.txt --play-out out/siege
$Illyria --play tools/playtest/conquest.txt --play-out out/conquest --play-log out/conquest/conquest.log
```

## What it cannot do

- **It cannot see.** The script does not know where a squad is on the screen. Read coordinates off a `shot`, with the camera
  fixed by `camera` (or the start view, which is the same each run), and expect them to be wrong the first time. There is
  no "click the Archers": `select N` is the only way to pick a squad by name; everything else is a box or a click on the map.
- **A squad card can be out of reach.** The HUD draws as many squad cards as fit the window's width and drops the rest
  (`hud.cpp`: `if (x + cw > w - 8) break`). In the Ford at 1600 wide 11 of 18 have one. `select N` still reaches the others;
  a click on a card does not.
- **Picking several squads, not all, needs a box.** The HUD's card click replaces the selection; there is no add. `select`
  does the same. A box (`drag left`, with `key shift down` to add) is how a script, like a hand, picks a few.
- **Time is the game's, not frozen.** `wait` is wall time; the simulation runs at 30 ticks a second in real time (faster
  with `speed`), so a slow machine runs the same ticks in more wall seconds, and a script is repeatable in what the game
  does, not in what the camera sees at an exact instant. The AI's choices are the game's (seeded); two runs of one script differ
  only through the real-time input timing, and small ones at that.
- **No sound**, no typed text, no touch, no pinch. The trackpad's two-finger pan and the mouse wheel's difference are not
  separately scripted (`wheel` sends whole notches: a mouse wheel).
- **Retina and the window's size.** `size` asks the window system; a window the display cannot hold is made smaller, and
  the run says so. Shots are in window points unless `full`.
- **Behind other windows, on a locked screen.** The window takes no focus and the app has no Dock icon, so a script does
  not take typing from the session; every run drew and read back frames with the window never made key. What was not
  checked: the window's place in the stack while it ran, a covered window, a locked screen or a sleeping display. macOS may
  stop drawing a window it knows is not on any display; if shots come back black or stale in that case, run with the screen on.
- **Numbers under load.** `fps`, `frame_ms` and `gpu_ms` are those of the machine the script ran on at that moment.
