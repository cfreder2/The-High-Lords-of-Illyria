# Pre-rendered unit sprites (Blender)

Every unit type the game knows (`engine/sim/include/illyria/sim/units.h`, `UnitType`) is
modelled, rigged and animated from scratch by the Python scripts in `sprites/`, rendered with
Cycles from the game's five views and packed into a sprite sheet the renderer loads in place
of its runtime bake (`game/render/renderer_sheets.cpp`, `game/web/sheets.cpp`). Nothing is
downloaded: every mesh is built from numbers in these files, so the output is deterministic for
a given Blender version.

## Regenerate everything (one command)

Install Blender as a Python module once (Blender 5.1 needs Python 3.13):

    python3.13 -m venv /tmp/bpyvenv
    /tmp/bpyvenv/bin/pip install bpy==5.1.2 pillow numpy

then, from the repository root:

    tools/blender/build_all.sh /tmp/bpyvenv/bin/python

This renders all 27 types into `build/sprites/<unit>/` (cells, packed sheet, contact sheets and
a log per type) and installs each sheet into `game/web/sprites/` (`<unit>.webp`, `<unit>.json`,
and `index.json`, which maps UnitType numbers to sheet names). It takes about two hours on four
CPU cores. To redo some types only:

    tools/blender/build_all.sh /tmp/bpyvenv/bin/python build/sprites dragons griffins

Close-up sheets (twice the pixels a metre, `<unit>_hd.webp/.json`, listed in
`index_hd.json`; see `docs/SPRITE_SPEC.md`): render with `HD=1`, which renders
the cells at four times the sheet's resolution (not two) and packs both sheets
from them. About 20-35 minutes a man-sized type on four shared cores:

    HD=1 tools/blender/build_all.sh /tmp/bpyvenv/bin/python build/sprites heavy_infantry archers

The contact sheet of the whole roster (`docs/images/sprites/roster.png`, idle, front view, in
both sides' colours) is made from the installed sheets by:

    /tmp/bpyvenv/bin/python tools/blender/roster_sheet.py docs/images/sprites/roster.png

## Working on a model

    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py list
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py look minotaurs /abs/path/out.png 0,0 2,14 120
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py blend minotaurs /abs/path/minotaurs.blend

`look` renders the given cells (view,frame) large; `blend` saves the rigged, keyed scene to open
in Blender. `render_sheets.py`'s docstring lists the other commands.

## The pieces

| File | What |
|---|---|
| `sprites/geo.py` | lofted tubes, panels and solids with skin weights (`Part`) |
| `sprites/rig.py` | armatures, poses as turns in the figure's frame, keys for the 26 frames, IK, bone scale (things that leave the hand) |
| `sprites/mats.py` | the materials (team cloth is white in the picture and in the team matte) |
| `sprites/human.py` | the human body and its joint angles (`Stance`); proportions by height, bulk, leg length and head size |
| `sprites/outfit.py`, `sprites/gear.py` | clothes, armour, helmets and plumes, weapons and shields |
| `sprites/people.py` | every person's rig and motions by how they fight (swing, overhead, thrust, shoot, throw, cast) |
| `sprites/horse.py`, `sprites/quad.py` | the horse; other four-legged bodies from tables (wolf, elephant, griffin, dragon) |
| `sprites/wings.py`, `sprites/batwing.py` | feathered wings (a bone a feather) and bat wings |
| `sprites/units.py` | the pilot's three: Heavy Infantry, Light Cavalry, Archons |
| `sprites/roster.py`, `sprites/roster2.py` | everyone else on foot |
| `sprites/beasts.py`, `sprites/beasts2.py` | mounts and monsters, the spider and the catapult |
| `sprites/render.py`, `sprites/pack.py` | the studio (camera, lights), cell rendering and packing |

## The sheet format

The same as the runtime bake's, so either can stand in for the other (`sprites/pack.py`): one
RGBA atlas whose alpha is a code (255 opaque, 191 opaque in the side's colour, 153 the dark
outline, 0 empty) and a table of 130 cells (5 views x 26 frames: idle 4, walk 8, attack 6,
flinch 2, die 6) giving each cell's pixels and its extent about the feet in metres. Pixels a
metre are 48, the game's, unless a picture would be more than 256 px across or the atlas would
not fit 2048 x 2048 (giants, elephants, dragons: `render.choose_ppm`).

Fliers (griffins, pegasi, dragons, archons) split the walk's eight frames: 0-3 walking on the
ground, 4-7 a wingbeat; the renderer plays the half that fits (`Renderer::draw`, by
`Unit::inAir`). The catapult's walk turns its wheels, its attack throws. The grenadier's
satchel and the archer's arrow hang on bones scaled to nothing once thrown or loosed, and the
wizard's spell gathers as light in his hand.

The page fetches only the sheets of the types in the battle (`sprites/index.json`, then
`<unit>.json` and `<unit>.webp`); until they arrive, or if a fetch fails, the type is baked as
before.
