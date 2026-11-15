# The High Lords of Illyria — Devlog

The making of the game, as it happened: screenshots, videos, logs and notes from every build, good and broken alike. This branch holds media only and shares no history with the code (`master`), so cloning the game never downloads it.

- **`builds/vX.Y.Z/`**: the builds deployed on 27 and 28 Oct 2026 (v0.1.2 to v0.1.13), filmed automatically by CI (`tools/devlog/capture.js` on `master`): opening view, god view, strategic view, the battle twice, the aftermath, a phone screenshot, and `flythrough.webm`, a ~20 s dive from the gods into the fighting. Filming stopped on 30 Oct; later builds have no folder here.
- **`timeline/`**: milestones. `00` to `02` are from before there was anything to look at (design doc, simulation logs); `03` onward hold the pictures for the entries after 28 Oct, taken from the screenshots made while working.
- **`work-in-progress/`**: screenshots taken while building, including the bad ones.
- **`audio/`**: no sound files here; [`audio/README.md`](audio/README.md) says what the game's sound is and where it lives.
- **[`how-we-work/`](how-we-work/README.md)**: how the game is made: the asset pipeline, how battles are tested, how the land's look is improved, sound, and how the work is organised.

Captures run in headless Chromium with software rendering on a machine with no GPU. The frame rates printed in them say nothing about real hardware.

---

## Timeline

### 27 Oct 2026 · The design document (`260e56d`)
The starter design doc arrives: *Myth*'s physics combat meets *Warlords*' campaign. The roadmap was empty; it was drafted the same day. → [`timeline/00-design-doc`](timeline/00-design-doc/GAME_DESIGN-v1.0.0.md)

Same day, decided in conversation:
- **Custom C++ engine, web first** (no Unity or Unreal licences; built like the Avara port), then Steam, then iOS.
- **Locked 60 fps on a laptop**, then **fully playable on phones**, then **phone vs phone** as the reference multiplayer case.
- **One seamless continent**, from a god's view of ant-like armies down to physics in the mud.
- **Everything persists** with a very long half-life, **blood stains** included.

### 27 Oct 2026 · Deterministic core (`4daefce`)
No pictures yet, just numbers. Fixed-point math, a seeded RNG and arrows flying real arcs. A 20-second test battle produces a checksum trace that must match on every machine. → [`timeline/01-deterministic-core`](timeline/01-deterministic-core)

- First CI run: Linux GCC, Linux Clang, macOS ARM and WebAssembly all produced **byte-identical** traces.
- First timing tests: one 450-soldier battle took 0.8 ms per tick, but nine battles crammed into one world took **69 ms**. That's why every battle gets its own space.
- 200,000 trees, rocks and houses plus 4,300 soldiers and 3,000 arrows: 1.5 ms per tick. Scenery is nearly free; moving things cost.

### 27 Oct 2026 · Hills and hand-to-hand (`bf7ffb4`)
Terrain generated from the match seed, and melee: footmen finally reach the archers. → [`timeline/02-terrain-and-melee`](timeline/02-terrain-and-melee)

Broken along the way:
- The sandbox refused to install the Emscripten SDK, so the old Debian package stood in.
- That old compiler silently produced an **empty program** for any `main(argc, argv)`: a 718-byte `.wasm` that did nothing.

### 27 Oct 2026 · First playable battle (`1968f76`, deployed as **v0.1.2**)
The first thing you could actually play: a WebGL 2 battlefield with pinch zoom from god view to the ground, sprite soldiers that become ant-like dots, and persistent blood and arrows. → [`builds/v0.1.2`](builds/v0.1.2)

| Opening | God view | Strategic | Phone |
|---|---|---|---|
| ![](builds/v0.1.2/01-opening.png) | ![](builds/v0.1.2/02-god-view.png) | ![](builds/v0.1.2/03-strategic.png) | ![](builds/v0.1.2/07-phone.png) |

Work in progress (taken minutes before the commit): [`work-in-progress/first-playable`](work-in-progress/first-playable)

| First light | Ground level | Phone, squad selected |
|---|---|---|
| ![](work-in-progress/first-playable/1-desktop-start.png) | ![](work-in-progress/first-playable/5-ground.png) | ![](work-in-progress/first-playable/7-phone-moved.png) |

Broken along the way:
- The first deploy failed with **404** because GitHub Pages wasn't switched on yet.
- The camera started so far out that soldiers were specks, and the phone HUD covered a quarter of the screen. Both fixed before the commit.
- After playing it, the player's verdict: **"I was not seeing arrows fly or stick in ground correctly."** The cause was that the simulation's ground and the drawn ground disagreed by up to half a metre on hills, and arrows in flight were one pixel wide.

### 27 Oct 2026 · Every unit type, explosions, remains (`47ffa7b`, **v0.1.3**)
All 26 unit types: the Warlords armies and allies, plus Myth's dwarven grenadiers, journeymen, warriors and berserkers. Also added:
- Shields that only block from the front.
- Fliers that swoop down to fight.
- Satchel charges that bounce, explode and set each other off.
- Corpses, body parts and stuck arrows thrown back into the air by explosions.
- Arrows that are finally visible in flight and stand up properly when stuck in the ground.

→ [`builds/v0.1.3`](builds/v0.1.3)

| Battle | Aftermath | God view |
|---|---|---|
| ![](builds/v0.1.3/04-battle.png) | ![](builds/v0.1.3/06-aftermath.png) | ![](builds/v0.1.3/02-god-view.png) |

Fly-through: [`builds/v0.1.3/flythrough.webm`](builds/v0.1.3/flythrough.webm). Frames from it:

| From the gods… | …down… | …into the mud |
|---|---|---|
| ![](work-in-progress/flythrough-frames/frame3.png) | ![](work-in-progress/flythrough-frames/frame9.png) | ![](work-in-progress/flythrough-frames/frame14.png) |

Work in progress: [`work-in-progress/full-roster`](work-in-progress/full-roster)

| The Red Hand's line-up | Smoke, craters, demons | Phone: the dragon |
|---|---|---|
| ![](work-in-progress/full-roster/g3-enemy.png) | ![](work-in-progress/full-roster/h3.png) | ![](work-in-progress/full-roster/p1.png) |

Broken along the way:
- **GCC and Clang disagreed** about the order of random-number draws inside one expression. The all-units battle desynced 7 seconds in, and the cross-compiler check caught it before it shipped.
- The scripted camera kept filming empty grass, which is why the **Fight!** button exists.

---

*From v0.1.4 to v0.1.13 every deploy added a `builds/vX.Y.Z/` folder automatically. v0.1.13 (28 Oct) is the last. On 30 Oct (`4ffa460`) the deploy workflow was cut down to build and publish only, with no filming and no CI on every push: tests run locally before a push, and Actions minutes are limited. The entries after v0.1.13 were written on 15 Nov from the commit history, the docs and the screenshots taken along the way.*

### 27 Oct 2026 · v0.1.4 (`8c2c9d3`)
Devlog: film every deployed build into the devlog branch

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.4/04-battle.png) | ![](builds/v0.1.4/02-god-view.png) | ![](builds/v0.1.4/07-phone.png) |

Fly-through: [`builds/v0.1.4/flythrough.webm`](builds/v0.1.4/flythrough.webm)

### 27 Oct 2026 · v0.1.5 (`301964a`)
The Warlords map comes to life: 3D scenery, water, roads, 3D bodies

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.5/04-battle.png) | ![](builds/v0.1.5/02-god-view.png) | ![](builds/v0.1.5/07-phone.png) |

Fly-through: [`builds/v0.1.5/flythrough.webm`](builds/v0.1.5/flythrough.webm)

### 27 Oct 2026 · v0.1.6 (`ecc8bdb`)
Move the Eastreach label off Moonhaven; match far plains to the battlefield

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.6/04-battle.png) | ![](builds/v0.1.6/02-god-view.png) | ![](builds/v0.1.6/07-phone.png) |

Fly-through: [`builds/v0.1.6/flythrough.webm`](builds/v0.1.6/flythrough.webm)

### 27 Oct 2026 · v0.1.7 (`fa28078`)
Closer to the reference structure; variety everywhere instead of open grass

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.7/04-battle.png) | ![](builds/v0.1.7/02-god-view.png) | ![](builds/v0.1.7/07-phone.png) |

Fly-through: [`builds/v0.1.7/flythrough.webm`](builds/v0.1.7/flythrough.webm)

### 27 Oct 2026 · v0.1.8 (`9748caf`)
Rivers that wind and flow

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.8/04-battle.png) | ![](builds/v0.1.8/02-god-view.png) | ![](builds/v0.1.8/07-phone.png) |

Fly-through: [`builds/v0.1.8/flythrough.webm`](builds/v0.1.8/flythrough.webm)

### 28 Oct 2026 · v0.1.9 (`e899c64`)
The lie of the land: climbing, cliffs, high ground, mud

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.9/04-battle.png) | ![](builds/v0.1.9/02-god-view.png) | ![](builds/v0.1.9/07-phone.png) |

Fly-through: [`builds/v0.1.9/flythrough.webm`](builds/v0.1.9/flythrough.webm)

### 28 Oct 2026 · v0.1.13 (`61fc662`)
Myth-style soldiers: articulated figures baked into animated 8-direction sprite sheets

| Battle | God view | Phone |
|---|---|---|
| ![](builds/v0.1.13/04-battle.png) | ![](builds/v0.1.13/02-god-view.png) | ![](builds/v0.1.13/07-phone.png) |

Fly-through: [`builds/v0.1.13/flythrough.webm`](builds/v0.1.13/flythrough.webm)

---

## After the filming stopped

*Hashes are on `master`. Pictures are in `timeline/03` onward. Most are frames the game shot of itself (`--shot`, later `--play`) while a change was being checked, before and after.*

### 28 Oct 2026 · An island, castles that can be besieged, a campaign (`683d695` to `fde4616`)
The same day as v0.1.13, 52 commits.
- **Land.** Illyria became an island with sea all round (`683d695`), a lake with an abbey on an isle (`ce8fe06`), bridges, beaches and waterfalls (`4ed63c3`). The relief is worn by simulated rain once, offline, and compiled into the game so every platform has the same land (`3de88f8`).
- **Castles.** Varied plans, wall-walks, stairs and sieges (`575c3d3`), a parapet with embrasures (`8459b5e`), catapults and gates that break (`9f7ebc0`).
- **Command.** Selection and orders as in *Myth* (`27f9df4`); facing, charges, veterans, height for archers and morale (`e925799`).
- **The campaign.** Computer lords and *Warlords* rules (`7b8cd0a`), one capital and a hero each (`0d527de`), ports and boats (`51eb194`), campaign battles fought in 3D, with a choice to fight one or have it resolved (`fde4616`).
- **Deaths and sound.** Damage types, severed limbs and the first sound effects (`ed7acf0`); a recording script for the soldiers' voices (`8a5503c`).
- **Soldiers.** The articulated figures of v0.1.13 were replaced the same day by models built and rendered in Blender: a pilot of three types (`60321ea`), then sheets for all 27 (`76b3e83`).

| Runtime figures (left) against the Blender pilot | All 27 types | Catapults before a wall |
|---|---|---|
| ![](timeline/03-castles-and-campaign/blender-pilot-knights.jpg) | ![](timeline/03-castles-and-campaign/roster-27-types.jpg) | ![](timeline/03-castles-and-campaign/siege-catapults.jpg) |

Broken along the way:
- The owner, after playing: **"Why are my archers shooting the wall? They would shoot over it, at bad guys inside."** Arrows also came out of the wall at his own men. Archers now lob over walls (`934d6cf`).
- On a phone the campaign ran at about 14 frames a second, and menus could be opened but not closed.

### 29 and 30 Oct 2026 · A smaller map, painted soldiers, a title screen (`3ee8e59`, `147503d`, `e4d43bd`)
- The map shrank to two thirds, castles with it; walls got thick with a two-lane walk and **ramps instead of stairs** (`3ee8e59`). This undid the stairs of the day before.
- **Painted sprites.** A spec, templates and an importer for drawn art (`8a2adcc`), then cells drawn by an image-generation model, first one picture a cell (`293c3b4`), then a whole clip to a plate so the character stays the same from frame to frame (`c17d216`). Heavy infantry was painted in full: every clip from all five angles (`147503d`).
- A manual as a paged book in the browser, after the 1990 *Warlords* booklet (`2d4f3ea`). A title screen, three save slots, settings and controls (`e4d43bd`). Horde and Capture the Flag (`4a7d0a8`), a Sandbox (`860731d`).
- The deploy workflow became build-and-publish only (`4ffa460`). This is where the filmed builds end.

| Heavy infantry, painted | A close-up sheet: before and after |
|---|---|
| ![](timeline/04-painted-sprites/heavy-infantry-painted-sheet.jpg) | ![](timeline/04-painted-sprites/closeup-sheet-before-after.jpg) |

Decided in conversation:
- The same day the painted knight shipped, the owner compared it with a render of a 3D model: **"no question, the 3d render is better."** Painted details changed from frame to frame. The painted route stopped at one unit.

### 31 Oct 2026 · Input, and archers from a 3D model (`d144c18`, `399d73d`, `0cd2ecd`)
- A trackpad pans and pinches; arrow keys, slow motion, right-click orders (`d144c18`). The owner's rule from here on: no function keys.
- Archers drawn from a rigged 3D model: a draw to the jaw, a run, a cheer (`399d73d`). Hyena-folk drawn as themselves instead of as light infantry (`0cd2ecd`). The models come from Tripo, an image-to-3D service; the clips are keyed by script in Blender.
- Grenadiers throw Molotovs and lay satchels that wait for fire (`21e6e07`).

| The archers' sheet, rendered from the model |
|---|
| ![](timeline/05-archers-from-a-model/archers-sheet-from-3d.jpg) |

Broken along the way:
- The service's own animations mixed up left and right arms: three attack takes swung the shield arm. The owner judged them not good enough, so every clip is keyed by hand in numbers (where the feet stand, where the sword hand is, which way the blade points).
- On 1 Nov a restart wiped the scratch folder that held the Blender environment and the work files. They were rebuilt and moved somewhere that survives a restart.

### 1 to 4 Nov 2026 · One World (`7ce12e7`, `ba9e468`, `899b772`, `d5113de`, `da3787d`)
The campaign map showed flags for armies and ran on its own clock. The owner zoomed in on a marching flag and found no soldiers.

Decided in conversation (1 Nov):
- **"The macro to micro promise is not compatible with the flags."**
- **"This is a one clock game."** Units march across the map in real time. Time does not pass differently when zoomed out. No flags over armies, no separate battle zones, no dice.
- The map is too big: **"I literally think that we just cut the physical size of the map"**, the same towns in less land. First a half, then a third each way.

What was built:
- A written proposal first (`7ce12e7`), then the continent at a third the size (`ba9e468`), the campaign on the battle's 30 Hz clock with two-minute days and real marches (`899b772`), soldiers on the campaign map with no flags and a minimap (`d5113de`), and on 4 Nov fights in place with the men the campaign holds (`da3787d`).
- Galleries round the towers, so men can walk the whole way round a wall (`8efc097`). Formations cut to a line, a double line and a hollow box (`5ee4a0e`).

| The continent as it is now, 2.8 km by 3.6 km |
|---|
| ![](timeline/06-one-world/continent-map.jpg) |

Broken along the way:
- A test that failed on WebAssembly after the map change was dropped at the owner's request, not fixed; the commit says so (`4b432a2`).
- Step 3 kept most men "asleep", marching as one body, and woke them only to fight. That is the next entry.

### 5 Nov 2026 · The reversal (`c46a215`, `3c84fe2`, `ce28935`, `d1f0002`)
The owner ordered an army to a spot and it walked straight through a castle's walls. In a skirmish, men went round walls and archers climbed ramps. The sleeping armies of step 3 never used the battle's pathfinding: a second, thinner way of moving men had been built beside the first, because joining the two had been called too large.

The owner: **"The campaign turned out to be a major regression in features."** And the evening before: **"We are ALWAYS in micro mode. This is a tactical game, at its core."**

What followed, 35 commits in the day:
- Rules every change is checked against (`c46a215`, `docs/ARCHITECTURE_RULES.md` on `master`): Skirmish is the baseline; the Conquest is the same world with the fence taken away; one battle system, one selection, one order path; "too large" is not a reason to leave a rule broken.
- Failing tests first: a march must go round walls and archers must climb ramps, as in a skirmish (`e9c1182`).
- Every man is a unit of the one world, with no sleeping or waking (`3c84fe2`). The record for a sleeping man is gone (`ce28935`). One tap, one command bar, one order path (`d1f0002`).

### 6 to 8 Nov 2026 · Speed, a desktop app, gates that swing (`50f4687`, `610df3e`, `8330c98`)
- With every man awake, the tick had to get cheaper. A benchmark of the whole Conquest at 1,800, 3,400 and 7,000 men (`50f4687`), then integer work on square roots, divisions and grid lookups. Trees became cards so every tree can be drawn at every distance (`456c99e`).
- The platform code was split from the web build and an SDL desktop app added (`610df3e`). The same C++ now runs in a browser and in a window.
- Zoom down to 2 m (`25ef241`). Reference drawings for every unit, front, side and back with the weapon apart, to make 3D models from (`4c7b22e`).
- Gates swing: shut, opening, open, closing. The holder keeps them by hand and garrisons keep them (`8330c98`). One flag to a pole, lowered and then raised on capture (`8f795d0`). Houses, roofs and rocks get textures (`80567b8`).

| A gate open | The old flag coming down | The new one up |
|---|---|---|
| ![](timeline/07-speed-app-gates/gate-open.jpg) | ![](timeline/07-speed-app-gates/flag-1-old-halfway-down.jpg) | ![](timeline/07-speed-app-gates/flag-4-new-raised.jpg) |

| A house before textures | Walls and ground at 12 m after | Zoomed to the boots |
|---|---|---|
| ![](timeline/07-speed-app-gates/textures-before-12m.jpg) | ![](timeline/07-speed-app-gates/textures-after-12m.jpg) | ![](timeline/07-speed-app-gates/zoom-0.jpg) |

| Reference drawings for the models (part of the sheet) |
|---|
| ![](timeline/07-speed-app-gates/tripo-refs-contact-sheet.jpg) |

Tried and not kept (recorded in `docs/ONE_WORLD.md`): link-time optimisation (2 to 3% in the browser, nothing native), `-O2` (the same as `-O3`), `-Os` (30% slower), WebAssembly SIMD (no change).

### 9 Nov 2026 · Roads, bridges and water (`31a37d7`, `e6def94`, `2514e53`, `830fab7`, `2d8d983`)
55 commits, most of them answers to screenshots the owner sent. **"The roads look terrible."** There were bridges nothing could cross, water standing in walls, rivers that did not meet the sea.

- **Roads.** Worn dirt without ruts, width by traffic, puddles, bends round boulders (`31a37d7`). A terrain audit tool walked the continent and found that **0 of 80** castle gates had a road reaching them; now every gate has one (`e6def94`).
- **Bridges.** Every bridge and ford on the continent is walked across by a man, a rider and an engine in a test (`2514e53`).
- **Water.** Rivers below their banks, natural lakes and isles, one water colour (`830fab7`); rivers end at the shore (`18cb13f`); under water the view is tinted, fogged and lit by caustics, and the sound is muffled (`2d8d983`).
- Towns wear their holder's colours (`45273b7`). An order queue on Shift and right-click (`7449d62`). Fliers no longer get stuck on wall-walks (`63394e2`).

| A junction before | After |
|---|---|
| ![](timeline/08-roads-and-water/road-junction-before.jpg) | ![](timeline/08-roads-and-water/road-junction-after.jpg) |

| A bridge under a castle wall | The same place: a ford and a road |
|---|---|
| ![](timeline/08-roads-and-water/bridge-under-castle-wall-before.jpg) | ![](timeline/08-roads-and-water/bridge-under-castle-wall-after-a-ford.jpg) |

| A lake before | After | A river mouth | Under the sea |
|---|---|---|---|
| ![](timeline/08-roads-and-water/lake-before.jpg) | ![](timeline/08-roads-and-water/lake-after.jpg) | ![](timeline/08-roads-and-water/river-mouth-after.jpg) | ![](timeline/08-roads-and-water/underwater-sea-2m.jpg) |

Broken along the way:
- The browser build trapped on a division by a zero length that the native build let by (`de10115`).
- Merging the lakes onto the roads conflicted in eight files; the merge was abandoned and done again as a task of its own (`28c8bea`).
- The view from the middle heights was started and stopped the same day: the owner looked at it and said it was not good enough to keep.

### 10 and 11 Nov 2026 · The manual, parchment menus, garrisons, the middle heights (`ea45214`, `661a46b`, `88b01ca`, `4255bed`, `7992fad`)
- The manual set like the booklet out of a game's box, its chapters checked against the code (`ea45214`). The check found four things wrong in the game, not in the manual; they were put right.
- The land painted more as *Myth*'s is: relief shading, moor, peat-dark water (`661a46b`). Every realm laid out alike: three walled towns and six villages (`178ece4`).
- Menus on parchment (`88b01ca`). A garrison at rest stands as one: bowmen at the embrasures, foot in ranks behind the gate (`4255bed`).
- The middle heights came back and were merged (`7992fad`) after ten rounds of the same camera stations shot again and again.
- **Clumping.** Men in a melee piled onto each other. A harness that draws a fight as circles made it measurable; a cap on how many can strike one foe, and men who wrap round him by the free places, followed (`ffae3be`, `f7d4e9c`).

| The title | A garrison at rest |
|---|---|
| ![](timeline/09-menus-garrisons-mid-zoom/menu-title.jpg) | ![](timeline/09-menus-garrisons-mid-zoom/garrison-capital.jpg) |

| The middle heights on 9 Nov | On 11 Nov |
|---|---|
| ![](timeline/09-menus-garrisons-mid-zoom/mid-zoom-before.jpg) | ![](timeline/09-menus-garrisons-mid-zoom/mid-zoom-after.jpg) |

| 24 heavy foot against 24, 16 s in, before | After |
|---|---|
| ![](timeline/09-menus-garrisons-mid-zoom/clumping-before.jpg) | ![](timeline/09-menus-garrisons-mid-zoom/clumping-after.jpg) |

No commits on 12 or 13 Nov.

### 14 Nov 2026 · A scripted player, and a reviewer who can play (`edae15e`, `9f71698`, `35ef007`, `870cbed`)
The owner asked for the work to run as a loop: **"make this move from a 5/10 to a 10/10, you are responsible for making that judgement call."** 115 commits that day.

- **`--play`** (`edae15e`): the desktop app plays a text script of timed keys, clicks and drags through its own input path, with shots, stats and a log. A first review, by an AI agent briefed as a games-press critic, had not been able to play the game. With `--play` the reviewer could fight battles. Review 2, played by script: **6.5 out of 10**, and "a first hour that does not yet teach you how to win a siege or a first town".
- The campaign renamed **Conquest** in files, code, UI and docs (`9f71698`). Old saves and links keep working.
- HUD lines in the player's words, a card for every squad, a right drag that turns the view even with men selected (`7548d04`, `4ed866d`).
- Armies move as blocks: a capped front with ranks, each man on his own line, engines no longer setting the pace (`35ef007`).
- A balance harness and a first balance pass (`df926ab`, `df84858`). One battle AI for every mode (`f07a420`). Castles with two doors, where attackers choose the weaker (`870cbed`). Engines stand off at their own reach (`58118ba`). A skirmish setup screen: map, side, gold, squads, difficulty (`e9eb89f`).

| The HUD before | After |
|---|---|
| ![](timeline/10-play-harness-and-first-review/hud-before.jpg) | ![](timeline/10-play-harness-and-first-review/hud-after.jpg) |

| A march before | As blocks | Review 2: the Ford |
|---|---|---|
| ![](timeline/10-play-harness-and-first-review/march-before.jpg) | ![](timeline/10-play-harness-and-first-review/march-after.jpg) | ![](timeline/10-play-harness-and-first-review/review2-crossing.jpg) |

Decided in conversation:
- The owner read every line of text in the game in one table and cut the wordy ones: "As they stand", the notes under each map, the explanations on the setup screens (`d8a103d`, `d6d24de`, `29964aa`). Short and literal from here on.

### 14 Nov 2026 · "Fun is too low" (`7331ef2`, `f8b5b35`, `e6b059c`, `98b9150`, `daf9aa0`)
Review 3 gave **Fun 5.5**, Overall 7.0: fights "a pleasure to watch for ninety seconds" that "then will not end", and "my best plan bought about the same result as not touching the mouse". The owner: **"Fun is too low. We need to understand why a 5."**

- **Battles end** (`f8b5b35`): a beaten host quits the field, a last blow, a results sheet; a skirmish opens on a deployment. **A Conquest ends** (`e6b059c`).
- **Tactics** (`98b9150`): position that counts and a word on screen for it, a Hold stance, the lord's Rally.
- **Deaths** (`7331ef2`). The owner suspected some deaths were not drawn from sprites. They were not: parts cut or blown off flew as coloured capsules. Now every body part is a piece of the soldier's own sheet.
- **Towns** (`daf9aa0`, `61e6e51`). The owner sent a screenshot of a grey temple crossed by hex lines: it had to look as good as the castles. Temple, sage's tower, keep, houses and stairs now share one stone-and-tile material, with soft shadows.

| A blast before: capsules | After: his own parts | The parts cut from each sheet |
|---|---|---|
| ![](timeline/11-endings-deaths-town-look/deaths-before-blast.jpg) | ![](timeline/11-endings-deaths-town-look/deaths-after-blast.jpg) | ![](timeline/11-endings-deaths-town-look/parts-men.jpg) |

| A town before | After | The temple |
|---|---|---|
| ![](timeline/11-endings-deaths-town-look/town-before.jpg) | ![](timeline/11-endings-deaths-town-look/town-after.jpg) | ![](timeline/11-endings-deaths-town-look/temple-after.jpg) |

Review 4, that evening: **Fun 6.5**, Overall 7.5. "The battle now has a beginning, an end and a score sheet, and it still does not need me": the open field was won every time, for 51 men lost with no orders and 40 to 57 with the best plans.

### 14 and 15 Nov 2026 · Soldiers from models (`84d5c17`, `0ac2584`, `fe800fb`, `3801e38`, `8e81c41`)
Nine more unit types were drawn from 3D models in a day: dwarven grenadiers, light infantry, the hero, the berserker, giants, journeymen, light and heavy cavalry, minotaurs. One command takes a unit from its model to the game's sheets (`84d5c17`). A rider and his horse are two models on two rigs, so a rider can later be unhorsed (`2c3f32c`).

The owner's worries, and what was done:
- **Sprites looked poor up close.** Sprites are now blended when magnified, with a close-up sheet at up to 144 pixels a metre (`3801e38`).
- **"When he runs, the sword is through his body."** A check on every frame of every clip finds any weapon inside its bearer, and a solver moves it out (`0ac2584`). The berserker was re-keyed as a longsword fighter.
- **An arm inside his own chest.** A second check measures each arm against the model's own trunk, not a bone line (`fe800fb`). Every unit was then cleared under both checks, one commit each, with the worst depth in the message.
- **"I need to see all the animated sprites."** An audit page shows every unit's sheet as the game draws it, and which units are still the old script-built figures (`8e81c41`). The first version showed an all-black page; it was fixed.

| A reference drawing | The run, before and after the check | The guard, before and after |
|---|---|---|
| ![](timeline/12-soldiers-from-models/ref-berserker-front.jpg) | ![](timeline/12-soldiers-from-models/berserker-run-before-after.jpg) | ![](timeline/12-soldiers-from-models/berserker-guard-before-after.jpg) |

| The audit page | In the game: foot | Cavalry |
|---|---|---|
| ![](timeline/12-soldiers-from-models/sprite-audit-grid.jpg) | ![](timeline/12-soldiers-from-models/in-game-foot.jpg) | ![](timeline/12-soldiers-from-models/in-game-cavalry.jpg) |

Broken along the way:
- The power went out on the evening of 14 Nov with several agents mid-task. Work was resumed from the branches.
- Most of the 27 types are still the script-built figures. The owner has more models to make.

### 14 and 15 Nov 2026 · Explosives that are counted, water, boulders, a fair Ford (`efd04a9`, `a15fa55`, `69b6b9b`, `50bf535`, `5ee45b3`)
Decided in conversation:
- **Dwarves are the play of the battle.** A dwarven unit is a pair; a wizard is alone and very rare. **"You have to protect the dwarfs!"**: unguarded they die, and the answer is a Guard order, not tougher dwarves (`efd04a9`, `fd8cd0b`).
- **Nothing is unlimited.** A wizard has four fireballs. A dwarf has three Molotovs and three satchels. A catapult had 16 stones. **"16 is crazy."** A train of two now carries eight between them.
- **The trap.** Lay satchels, move on, and when the enemy crosses them throw a Molotov or a fireball (`bdd8107`). A hyena scout can pick up a satchel and carry it in, or light it and run (`bfdd0b9`).
- **A Molotov does not explode: "that is a satchel's job."** It burns the ground and the wood of an engine (`14905af`).
- **Water.** Molotovs and satchels are dead in it; a fireball keeps a third. Fighting from water is a penalty. Blood in the water, bodies that float downriver, splashes (`a15fa55`).
- **The Ford was unfair**: one side had to cross. It became a river with a bridge up and down stream and a flag on an isle in the middle (`50bf535`).
- **Boulders.** A catapult's stone bounces and rolls on over the ground (`69b6b9b`): about 30 m on level grass, through a file of men.
- Two rows of squad cards on a wide window (`5ee45b3`).

| The satchel run | Bodies adrift | A fireball on water: steam |
|---|---|---|
| ![](timeline/13-explosives-water-boulders/hyena-satchel-run.jpg) | ![](timeline/13-explosives-water-boulders/water-bodies-adrift.jpg) | ![](timeline/13-explosives-water-boulders/water-steam.jpg) |

| A boulder and its furrow | The isle and its flag | Squad cards before | After |
|---|---|---|---|
| ![](timeline/13-explosives-water-boulders/boulder-line.jpg) | ![](timeline/13-explosives-water-boulders/ford-isle-flag.jpg) | ![](timeline/13-explosives-water-boulders/cards-before.jpg) | ![](timeline/13-explosives-water-boulders/cards-after.jpg) |

Review 5, in the small hours of 15 Nov: **Fun 7.0**, Overall 7.5. The battle "starts at once, can be lost, and has a tomorrow" (a March of five battles with one army that carries over, `415072b`). And: "what I do inside it still cannot be told apart from noise". Doing nothing cost 113 men and won every time; the most careful play cost between 67 and 156 and lost three times in seven.

### 15 Nov 2026 · Why formations did not matter; outworks (`32cc4dd`, `8f69cc7`, `74ea667`, `3677ea6`, `947a7f6`)
The owner: **"Flanking needs to actually work... formations need to matter. In Myth, they really did. So something is wrong with our engine."**

The orders pass measured before it changed anything (`32cc4dd`). What it found:
- On the open field **44 to 67% of every side's arrow hits landed on its own men.** A bowman behind his own foot shot it in the back.
- A charging horse was the easiest mark on the field: 49% of arrows hit it, against 14% for a man standing still.
- Morale was decided almost wholly by a head count and by friends falling. Where a blow landed, flank or rear, hardly counted.
- Depth was worth nothing: 24 heavy foot in a single rank beat a double line twelve times in twelve, and every deeper shape did worse.
- In a melee 57 to 93% of the men were touching a friend.

What changed: arrows are lofted over a bowman's own line and lead a moving man, and a fast mark is harder to hit (`8f69cc7`); half a metre of room between men, ranks that hold, flanks that break a squad, braced foot that stops a charge from the front (`74ea667`). Friendly arrow hits fell to 12 to 20%, and no man in the measured melees touches a friend.

The same night, the siege defence the owner asked for (`3677ea6`, `3810769`, `947a7f6`): ditches, banks, palisades, abatis and wooden watchtowers round 16 of the 29 walled places, bowmen forward on the bank, tower engines, and sorties of light horse with one Molotov each. Timber that is hit breaks up and flies.

| A flank charge | Horse at bowmen |
|---|---|
| ![](timeline/14-outworks-and-orders/flank.jpg) | ![](timeline/14-outworks-and-orders/horse-at-archers.jpg) |

| Ditch, bank and palisade | A watchtower | After one train's stones | A sortie's work |
|---|---|---|---|
| ![](timeline/14-outworks-and-orders/palisade.jpg) | ![](timeline/14-outworks-and-orders/watchtower-standing.jpg) | ![](timeline/14-outworks-and-orders/watchtower-after.jpg) | ![](timeline/14-outworks-and-orders/sortie-alight.jpg) |

Not met, and said so in the record: a lit hyena is always shot down before he arrives; bowmen lose to foot at equal gold; the line plan still does not beat giving no orders.

### 15 Nov 2026 · Review 6, a crash, and sound (`616a04c`, `6250d6a`, `b221f38`)
Everything was merged and deployed (`616a04c`) and reviewed again. Review 6, 106 scripted runs: **Fun 7.0, unchanged. Overall 7.5, unchanged.**
- Better: the outworks, the redrawn soldiers, the dwarves' trap (18 dead in one detonation). The March was carried to its fifth battle for the first time. Hard beat the reviewer ten times in ten.
- The core finding stands. On the battle the menu gives first, **doing nothing lost 55 men, three runs alike; the obvious line plan lost 109.** Scripts that gave orders during the fight swung by 80 to 91 men between runs that differed only in when the input landed.
- Three modes stall: the Ford played by the bridges was undecided after 13 minutes; a capital's defence was undecided at 8:24 with 124 attackers standing outside open gates.
- The outworks changed nothing in a whole siege: the capital fell for 3 men lost, its palisade untouched, because the gates fell inside a minute.
- Rally never turned a man back.
- One crash in 106 runs, on leaving a Conquest with men selected. Found, fixed with two tests that reproduce it, and deployed the same day (`6250d6a`).

| No orders: a win for 84 lost on another seed | The capital's defence, stalled |
|---|---|
| ![](timeline/15-review-six-and-sound/review6-no-orders-result.jpg) | ![](timeline/15-review-six-and-sound/review6-capital-stall.jpg) |

**Sound.** The owner had no list of what was wrong with it. An audit made one without ears, by reading the code and measuring every file: all 168 effect files were synthesis, a third of them carried most of their energy below 300 Hz where a phone's speaker has nothing, nothing that moved made a sound, and the Conquest was close to silent. Fixes followed (`0e8bd8f`, `6f95b7e`, `6ea717a`), a Sounds page beside the sprite audit (`0934f30`), and a Sound Editor that auditions candidates, records the owner's own effects and voice lines, and installs a choice into the game at once (`b221f38`). None of the 1,055 voice lines is recorded yet.

| The Sounds audit page | One effect, measured |
|---|---|
| ![](timeline/15-review-six-and-sound/sound-audit-page.jpg) | ![](timeline/15-review-six-and-sound/spectrogram-sword-flesh.jpg) |

---

## Where it stands (15 Nov 2026)

Play it: **https://cfreder2.github.io/The-High-Lords-of-Illyria/**

Twenty days in. The reviewer's score for fun went 5.5, 6.5, 7.0, 7.0. It has stopped moving, and the reason is the same in every review: on the open field a plan is not yet worth more than no plan.

The owner, on 15 Nov: **"We need this to be a tactical game."** Imagine smaller battles: six against six on level ground should be even, and how they meet should decide it. Every formation needs a purpose. A few fast riders should be something bowmen fear. High ground and woods should matter. And the result of each tactic should be shown as a spread over many runs, not as one number.

In progress, not finished, not merged:
- A **tactics lab**: named small battles run over many seeds, both ways round, with a page that shows every run. Its runner exists on a branch (`9c18fdf`); the rules it will judge are still being changed.
- Arrows against fast units and shield walls, high ground, woods, and more varied ground on the open fields.
- Why doing nothing wins the menu's battle, why input timing swings a result by 80 men, and a Rally that works.
- An ending for every mode, and outworks that cost an attacker something.

Open, and known:
- Most unit types still use the older script-built figures.
- No voice line is recorded. The effects are still synthesis.
- The frame rates printed in the older captures were made on a machine with no GPU and mean nothing.
- Multiplayer is designed for (a deterministic simulation that every platform steps identically) and not built.
