# Sprite art spec (for scripted image generation)

What to draw, how to name it, and how the game takes it. Everything here is
also machine-readable in `docs/sprite_templates/manifest.json` (loop over its
`cells`). The numbers come from `tools/sprites/layout.py` (the art pipeline) and
`game/render/sprite_layout.h` (the renderer); a test keeps them in step.

## 1. What exists today

- 27 unit types (`engine/sim/include/illyria/sim/units.h`), each with one sprite
  sheet rendered in Blender (`tools/blender/`, see its README), in
  `game/web/sprites/<unit>.webp` + `.json`.
- A sheet is **5 views x 26 frames** today: idle 4, walk 8, attack 6, flinch 2,
  die 6. Views are 45 degrees apart round the figure: **0 front, 1 front-right,
  2 right side, 3 back-right, 4 back**. Views 5-7 (the figure's left side) are
  views 3, 2, 1 mirrored by the renderer: **never draw them**.
- Fliers (griffins, pegasi, dragons, archons) split the walk: frames 0-3 walking
  on the ground, 4-7 one wingbeat in the air.
- Camera: orthographic, **30 degrees above the horizon**, the figure facing the
  view's direction (view 0: facing the camera; view 2: facing right).
- Light turns with the camera: key light from **upper left, in front** (over
  the viewer's left shoulder), soft fill from the right, a rim light from behind
  upper right. Every view is lit the same way relative to the picture.
- Sheet resolution: 48 px a metre (less for giants 34, elephants 40, demons 39,
  dragons 28). Close-up sheets (new): twice that.
- Team colour: in the sheet the side's cloth is drawn white-shaded and marked by
  an alpha code (255 opaque, **191 = team colour**, **153 = the 1 px dark outline**,
  0 empty); the game dyes it blue or red.

## 2. What you deliver

**One PNG per cell**, in one folder per unit type:

    <unit_type_id>/<clip>/v<view>_f<frame>.png      e.g. heavy_infantry/walk/v2_f03.png
    <unit_type_id>/<clip>/v<view>_f<frame>_team.png optional team mask (white = team)
    <unit_type_id>/gibs/<part>_<a|b>.png             gib parts, e.g. heavy_infantry/gibs/arm_a.png

`view` is 0-4, `frame` two digits from 00. The full list is
`docs/sprite_templates/files.txt` (one name a line, marked required/optional).

### Canvas, scale, anchor (fixed per size class)

| class | canvas | px a metre | anchor (feet), px from top-left | types |
|---|---|---|---|---|
| man | 1024 x 1024 | 192 | 512, 560 | light_infantry, heavy_infantry, archers, dwarves, wizards, undead, hero, dwarf_grenadier, journeyman, warrior, berserker |
| large | 1536 x 1024 | 128 | 768, 560 | pikemen, light_cavalry, heavy_cavalry, wolf_riders, minotaurs, spiders, griffins, pegasi, unicorns, archons, catapult |
| huge | 1536 x 1024 | 80 | 768, 600 | giants, elephants, dragons, devils, demons |
| gib part | 512 x 512 | the unit's class | 256, 256 (part centred) | all |

- The **anchor** is the point on the ground between the feet (hooves, claws,
  wheels). Standing figures put their feet there in every frame; walking figures
  keep the body over it (the game moves the sprite, not the picture). Dead bodies
  lie with their feet at the anchor and the rest of the body where it fell.
- **Scale is fixed**: a 1.8 m man is 1.8 x cos 30 deg x 192 = about 300 px tall in
  the picture. Unit heights are in the manifest (`height_m`).
- Background: **transparent** (preferred). If the tool cannot write alpha, a flat
  **#00FF00** (use **#FF00FF** for green units: dragons); the importer keys out
  whatever flat colour fills the corners. No shadows on the ground, no ground,
  no scenery, nothing touching the canvas edge.
- Blank canvases with the anchor, ground line, 1 m grid in 30 degree perspective
  and head height: `docs/sprite_templates/_blank/{man,large,huge,gib}.png`.

### Team colour

Paint everything that shows the side's colour (tabard, shield field, banner,
plume, caparison, saddle cloth, the spider's mark) in **saturated key blue
#0050FF**, shaded naturally (darker blue in shadow). The importer turns every
pixel within 18 degrees of that hue and at least 55% saturation into the team
mask and keeps its brightness as the cloth's shading. So **use no other
saturated blue anywhere else**. If the tool cannot hold a key colour, deliver a
`_team.png` mask beside the picture (white = team, black = not) and paint the
cloth any colour.

### Clips, frames and timing (44 frames a view)

| clip | frames | plays | notes |
|---|---|---|---|
| idle | 4 | loop, ~2.2 s | breathing, small weight shift; frame 3 leads back into 0 |
| walk | 8 | loop, one stride (both feet) | evenly spaced; fliers: 0-3 on foot, 4-7 one wingbeat airborne |
| attack | 6 | `attack_seconds` (0.56-1.0 s, manifest) | melee: **frame 3 the blow lands** (0-2 wind-up, 4-5 recover); ranged: **frame 4 the missile has left** |
| flinch | 2 | 0.3 s | recoil from a hit, half recovered |
| die_back | 6 | 0.9 s | struck from the front (arrows in the chest, blunt blows): staggers, falls on his back. **Frame 5 is the corpse, lying flat**; it stays on the field |
| die_front | 6 | 0.9 s | **new.** struck from behind: pitches forward, lands face down; frame 5 the corpse |
| die_crumple | 6 | 0.9 s | **new.** cut down by a blade or a limb taken off: knees buckle first, slumps sideways; frame 5 the corpse |
| die_burn | 6 | 1.5 s | **new, optional.** on fire: writhes, falls; frame 5 a charred corpse (or skip it: the game uses die_crumple) |

Frames are evenly spaced in time. All death clips end in a body lying flat on the
ground (no part higher than ~0.4 m), the feet still at the anchor. Mounts fall
with their riders (the rider may be thrown clear, within the canvas). The
catapult's deaths are a collapse into timber.

Which death plays (the sim's death matrix, `world_death.cpp`): burned -> die_burn;
limb or head cut off -> die_crumple; any death struck from behind -> die_front;
other blade deaths -> die_crumple; the rest (arrows, blunt, blasts) -> die_back.
Bodies blown apart use the gib parts instead.

### Gib parts

Separate small pictures for severed limbs and bodies blown apart, two looks each
(`a`, `b`: e.g. seen from two sides), the part lying on its side as the 30 degree
camera sees it, centred on the 512 x 512 canvas at the class scale. The game
spins them in the picture's plane, so no rotations are needed. Parts per type are
in the manifest (`gib_parts`): head, torso (a torn trunk), arm, leg, chunk (a
bloody gobbet), weapon, shield, mount_head, mount_leg, wing (only those the type
has). Severed ends bloody; the team colour key works here too.

### How many pictures

Per unit type: 5 views x 44 frames = **220** (190 required, 30 optional die_burn)
plus 2 per gib part (8-18). In total **6282** pictures, **5130** required
(`files.txt`). A partial delivery works: whatever is missing is taken from
today's sheet (and missing new deaths borrow die_back), so deliver in this
order: idle, walk, attack, die_back (120 a type), then flinch, die_front,
die_crumple, then gibs and die_burn.

## 3. References to give the generator

- `docs/sprite_templates/<unit>/<clip>.webp`: today's figure for every view and
  frame of that clip (views down, frames across, labelled `v2_f03`), on the
  canvas at a quarter scale with the anchor (red cross), ground line, 1 m bar and
  head height; team areas shown in key blue. The new deaths show die_back's
  silhouette turned round as a guide only.
- Full-size pose guides, one per cell, named as the delivery files (today's
  figure on the delivery canvas at the delivery scale, for image-to-image):

      python3 tools/sprites/templates.py guides heavy_infantry /tmp/guides [--clip walk]

## 4. Example prompts

Shared style line (prepend to every prompt):

> Game sprite, single figure, full body, orthographic view from 30 degrees above
> the horizon, no perspective, fantasy medieval, painterly realistic, crisp
> edges, lit from the upper left front with a soft fill from the right, on a
> plain transparent background (or flat #00FF00), no ground, no shadow, no text.
> Everything that shows the army's colour (tabard, shield field, plume) painted
> in saturated pure blue #0050FF. Match the reference image's pose, size and
> position exactly; the feet on the red cross.

Per cell (from the manifest: `look`, the view and the clip):

> heavy infantry: mail and plate swordsman, great helm with plume, kite shield
> with a cross. Seen from his right side (he faces right). Walk cycle frame 3 of
> 8: right foot forward, passing the left. 1024x1024 canvas, feet at x=512,
> y=560, 1.9 m tall = 316 px.

> heavy infantry, seen from the front (facing the viewer), death falling forward,
> frame 5 of 6: lying face down flat on the ground, head toward the viewer, feet
> at x=512, y=560, sword fallen beside him.

> giant spider, seen from the back-right (45 degrees behind its right side),
> attack frame 3 of 6: the bite lands, front legs raised. 1536x1024, the body's
> centre over x=768, y=560, 128 px a metre.

> gib part, heavy infantry severed arm in mail with a plate gauntlet, lying on
> its side, bloody stump, seen from 30 degrees above, centred on a 512x512
> canvas, 0.7 m long = 134 px.

A script loops over `manifest.json` `cells` (each has `file`, `unit`, `anim`,
`view`, `frame`, `canvas`, `ppm`, `anchor`, `required`) and the unit's `look`,
`height_m`, `attack_seconds`, `flier` and `ranged` under `units`.

## 5. How we use it

    python3 tools/sprites/import.py ART_DIR --check     # names, canvases, what is missing, feet on the anchor
    python3 tools/sprites/import.py ART_DIR             # writes game/web/sprites/<unit>.webp/.json and <unit>_hd.*

The importer keys the background, builds the team mask, checks the feet (with
`--snap-feet` it moves each clip so they stand on the anchor), scales down to the
game's resolution (48 px a metre: the normal sheet) and to twice that (the
close-up sheet), trims, draws the 1 px outline and packs the atlases in the game's
format (`game/web/sprites/`, see `tools/blender/sprites/pack.py`), the new
layout of 44 frames a view plus 20 gib slots (`sprite_layout.h`). Views 5-7 are
mirrored by the renderer. Missing cells come from today's sheet (kept the first
time in `tools/sprites/baseline/`). Running it again changes nothing.

In the game the normal sheet is always loaded (halved in battles with many types
on a phone); a type's close-up sheet is fetched only while the camera is close to
its soldiers (within a texture budget of 8 M texels) and dropped when it pulls
back, and the camera may not come so close that any sprite on screen is magnified
past 1.5 times its sheet's resolution (`game/render/closeup.h`).

Today's Blender sheets get close-up versions with

    HD=1 tools/blender/build_all.sh /tmp/bpyvenv/bin/python build/sprites <unit> ...

(about 21 minutes a man-sized type on four cores; done for heavy_infantry,
archers, light_infantry, warrior, pikemen and undead).
