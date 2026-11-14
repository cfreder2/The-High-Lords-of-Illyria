# Death audit

*How every unit type dies and what it leaves on the ground, checked in play on 2026-11-14, with what was changed.
The pictures are in `~/Desktop/death-audit/` (`before/`, `after/`, and the parts of every sheet in `parts-men.png` and
`parts-beasts.png`). To see it again: `Illyria --play tools/playtest/deaths.txt` (docs/PLAYTEST.md, the `deaths`
command).*

## The question

*"Are deaths all working correctly, with body parts? Sometimes it is not all sprite based still. Is that old?"*

It was not all sprite based, and yes, what was left was old. The bodies were always sprites: every type has a death
clip in its sheet and lies on its last frame. The **loose parts were not**: 25 of the 28 sheets (every one packed
before the gib cells existed: the 26-frame Blender sheets) had no part pictures, so whatever was cut or blown off those
soldiers flew and lay as the 3D stand-ins from before the sprite sheets: a skin-coloured ball for a head, a
team-coloured capsule for an arm, a brown capsule for a leg, a dark red ball for a gobbet, all man-sized whatever died.
A giant, a horse and a dragon each left the same six small capsules. Only Heavy Infantry, Archers and the Hyena-folk
(the three imported sheets) threw pieces of their own pictures. `before/before-blast-0-flying.png` shows both at once:
the heavy infantryman's shield and limbs as pixel art beside blue, purple and brown bars.

Now every sheet has its parts, cut from its own idle pictures, and no shipped sheet reaches the 3D path
(`after/after-blast-*.png`).

## Every type

Death clips: the falls its sheet holds and their frames a view (five views). Corpse: what lies after the fall. Parts:
what a blast or a cut throws.

| Unit | Death clips (frames) | Corpse | Parts before | Parts now | Fallbacks still hit |
|---|---|---|---|---|---|
| Light Infantry | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, shield | front, crumple and burn play the back fall; a burned body is now blackened |
| Heavy Infantry | back 6, front 6, crumple 6, burn 6 | own sprite | own sprite | own sprite (imported): head, torso, arm, leg, chunk, weapon, shield | none |
| Archers | back 8, front 8, crumple 8, burn 12 | own sprite | own sprite | own sprite (imported): head, torso, arm, leg, chunk | a blade taken from it is a gobbet |
| Pikemen | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon | front, crumple and burn play the back fall; a burned body is now blackened |
| Light Cavalry | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, arm, leg, chunk, weapon, mount head | front, crumple and burn play the back fall; a burned body is now blackened |
| Heavy Cavalry | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, arm, leg, chunk, mount head | front, crumple and burn play the back fall; a burned body is now blackened. a blade taken from it is a gobbet |
| Dwarven Legion (retired) | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, shield | front, crumple and burn play the back fall; a burned body is now blackened |
| Wolf Riders | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, arm, leg, chunk, mount head | front, crumple and burn play the back fall; a burned body is now blackened. a blade taken from it is a gobbet |
| Giants | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk | front, crumple and burn play the back fall; a burned body is now blackened. a blade taken from it is a gobbet |
| Minotaurs | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon | front, crumple and burn play the back fall; a burned body is now blackened |
| Giant Spiders | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, leg, chunk | front, crumple and burn play the back fall; a burned body is now blackened. arms are legs; a blade taken from it is a gobbet |
| Griffins | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, leg, chunk | front, crumple and burn play the back fall; a burned body is now blackened. arms are legs; a blade taken from it is a gobbet |
| Pegasi | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, arm, leg, chunk, weapon, mount head | front, crumple and burn play the back fall; a burned body is now blackened |
| Elephants | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, arm, leg, chunk, mount head | front, crumple and burn play the back fall; a burned body is now blackened. a blade taken from it is a gobbet |
| Unicorns | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, leg, chunk | front, crumple and burn play the back fall; a burned body is now blackened. arms are legs; a blade taken from it is a gobbet |
| Wizards | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon | front, crumple and burn play the back fall; a burned body is now blackened |
| Dragons | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, beast): head, torso, leg, chunk, wing | front, crumple and burn play the back fall; a burned body is now blackened. arms are wings; a blade taken from it is a gobbet |
| Devils | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon | front, crumple and burn play the back fall; a burned body is now blackened |
| Demons | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, wing | front, crumple and burn play the back fall; a burned body is now blackened |
| Undead | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, shield | front, crumple and burn play the back fall; a burned body is now blackened |
| Archons | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, wing | front, crumple and burn play the back fall; a burned body is now blackened |
| Hero | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, shield | front, crumple and burn play the back fall; a burned body is now blackened |
| Dwarven Grenadiers | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon | front, crumple and burn play the back fall; a burned body is now blackened |
| Journeymen | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk | front, crumple and burn play the back fall; a burned body is now blackened. a blade taken from it is a gobbet |
| Warriors | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon, shield | front, crumple and burn play the back fall; a burned body is now blackened |
| Berserkers | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, man): head, torso, arm, leg, chunk, weapon | front, crumple and burn play the back fall; a burned body is now blackened |
| Catapults | back 6 only | own sprite | 3D capsules and balls | own sprite (cut, engine): chunk | never torn apart or cut (the sim fells or burns an engine); burn plays the back fall, blackened |
| Hyena-folk | back 8, front 8, crumple 8, burn 8 | own sprite | own sprite | own sprite (imported): head, torso, arm, leg, chunk, weapon | none |

Seen in play for all 28: the fall plays and rests on its last frame (`after/after-fell-*.png`), an arrow stays in the
body (`after/after-shot-*.png`), fire leaves it black and smoking (`after/after-burn-*.png`), a cut throws the head or
a limb as a piece of the picture with a pool where he fell (`after/after-cut-*.png`), a blast throws the lot
(`after/after-blast-*.png`). Corpses and parts take the side's colour from the sheet's own cloth mask (red and blue
bodies in `after/battle-*.png`). Nothing lands in the river and no blood is left on water (`after/river-2-rest.png`).

## What was wrong, and what was done

1. **Parts were 3D capsules and balls for 25 sheets.** `tools/sprites/sheet_gibs.py` cuts the gib parts of every
   packed sheet that has none from the sheet's own idle pictures and packs them in new rows above the old ones (no old
   pixel or cell moves; an old sheet now has 150 cells, which the loader takes). One pipeline, three body plans chosen
   from the unit's part list, nothing placed by hand for a unit: *man* (the importer's outlines, in heights of the
   figure), *beast* (riders, spiders, griffins, unicorns, dragons: the side view is measured for its belly line, the
   legs under it, the back line, what rides above it and the head in front, and cut along those lines), *engine* (two
   baulks of timber). Close-up sheets are cut the same way.
2. **Parts did not match the body.** A horse left a man's parts and a giant man-sized ones, because the stand-ins
   were one size and shape. Parts are now pieces of the type's picture at its own scale: a rider's horse gives the
   barrel, the legs and its head, a dragon its head, barrel, legs and wings. The sim still throws the same kinds
   (head, trunk, two arms, two legs, gobbets), so no checksum moved; the renderer draws a kind the sheet lacks as the
   nearest it has (`sprite_layout.h` `gibStandIn`: a beast's "arms" are its wings or legs), and gives the first gobbets
   of a body blown apart its weapon, shield, mount's head and a wing.
3. **The undead left gobbets.** Every piece of the bloodless is a "bone" in the sim, and all were drawn as the gobbet.
   They are now skulls, ribcages, arm and leg bones of the skeleton's own picture, cut without blood.
4. **A blade a hyena took was a grey bar** for every type but the three imported ones. It is the body's own weapon
   (cut as the longest thin thing in its side view) where the sheet has one.
5. **A burned man looked freshly felled** on the old sheets, which have no burning clip: the back fall stood in for
   it. He now blackens as he falls and lies charred (`kUnitFs`, a negative value in the instance's selected slot).
6. **A body whose death was not seen could show a wrong frame.** The frame was worked out from "seconds dead", which
   is 1e9 for a body the presentation never saw fall, and that was cast to an int (undefined behaviour). Past the
   clip's length is now its last frame (`deathFrame`, `SheetClip::frameAt`).
7. **A beast's barrel stood on end.** Long parts were turned a quarter to lie down, which is right for a man's trunk
   drawn standing and wrong for a barrel cut from the side. A part wider than tall now lies as drawn.
8. **Parts outlived the bodies when zoomed out.** Corpses stop being drawn where men become dots; a dragon's trunk
   would not have. Parts follow the same level of detail.
9. **Flying parts had no shadow**, so an arc read as a slide. Each has a small shadow on the ground under it.

Already right, and left alone: the sim's arcs (a strike, a hop, a skid, a roll down a slope too steep to grip, a bounce
off a wall), the splat at every strike and the smear along the skid, the pool under every bleeding death, parts and
corpses staying for the battle (30,000 remains, 30,000 ground marks; ten minutes in the Conquest), sinking in water,
kicking by men walking through, and the hyena-folk's scavenging (their tests pass unchanged).

## Still not a sprite

| What | How it is drawn | Old? | To fix |
|---|---|---|---|
| Arrows, flying, stuck and lying | a small 3D shaft (`kArrowVs`) | by design, not a leftover | none needed |
| Molotov in flight | a green capsule (`renderer.cpp`, `ProjectileKind::Molotov`) | yes | **new art**: a bottle with a rag, 4 to 8 tumbling frames |
| Catapult stone in flight | a grey ball | yes | **new art**: a boulder, 4 tumbling frames |
| Satchel charge, laid, thrown or carried | a procedural disc (`kPlanBomb`) | yes | **new art**: a satchel, lying and tumbling |
| Fireball | a procedural glowing orb (`kPlanOrb`) with particles | an effect, not a stand-in | none needed |
| Any part, while a type's own sheet has not arrived | the 3D stand-ins, beside a figure baked from the old 3D models | yes, and brief (until the sheet loads; for good only if a sheet fails to load) | none: every shipped sheet has its parts |

## Needs new art (cannot be derived)

- **The 25 old sheets have one fall.** Struck from behind, cut down or burned, the body plays the back fall. The
  clips `die_front`, `die_crumple` and `die_burn` are wanted for: Light Infantry, Pikemen, Light Cavalry, Heavy
  Cavalry, Wolf Riders, Giants, Minotaurs, Giant Spiders, Griffins, Pegasi, Elephants, Unicorns, Wizards, Dragons,
  Devils, Demons, Undead, Archons, Hero, Dwarven Grenadiers, Journeymen, Warriors, Berserkers, Catapults (and the
  retired Dwarven Legion).
- **A body that lost its head or a limb still lies whole** (all 28): the part flies off and the corpse's picture has
  it too. Wanted: a last frame without the head, and one without a limb, for each type a cut can sever (mass under 4).
- **Drawn parts for the beasts and riders.** The cut ones are true to the picture but rough: a rider's head is a
  helmet's crown, his body carries his lance, a griffin's and a pegasus's folded wings stay on the barrel (so their
  "arms" are legs), an archon's wings seen from behind are slivers, a spider's head is a pair of forelegs, a giant's
  front head has his club beside it. Delivering `gibs/<part>_<a|b>.png` (docs/SPRITE_SPEC.md) replaces them.
- The Molotov, the stone and the satchel above.

## Not checked

- The web build: the shader change (`kUnitFs`) was compiled and seen on the desktop's GL 4.1 only. It uses nothing
  outside ES 3.0 (`clamp`, `mix`, `dot`).
- Close-up sheets' parts (cut by the same code, loaded by the same loader; not looked at closer than 22 m).
- The Conquest's battles, and a long battle near the remains budget.
- A hyena carrying and throwing one of the new parts in play (the draw path is the one the flying parts use).
- No golden or integration test was run (by the testing rule). The sim's only change is `World::testBlow`, which no
  game calls; the unit tests of the death matrix, the scavengers and the sprite layout pass.
