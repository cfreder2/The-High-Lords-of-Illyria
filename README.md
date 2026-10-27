# The High Lords of Illyria — Devlog

The making of the game, as it happened: screenshots, videos, logs and notes from every build, good and broken alike. This branch holds media only and shares no history with the code (`master`), so cloning the game never downloads it.

- **`builds/vX.Y.Z/`**: every deployed build, filmed automatically by CI (`tools/devlog/capture.js` on `master`): opening view, god view, strategic view, the battle twice, the aftermath, a phone screenshot, and `flythrough.webm`, a ~20 s dive from the gods into the fighting.
- **`timeline/`**: milestones from before there was anything to look at (design doc, simulation logs).
- **`work-in-progress/`**: screenshots taken while building, including the bad ones.
- **`audio/`**: nothing yet; the game is silent so far.

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

*From here on, every deploy adds a `builds/vX.Y.Z/` folder automatically.*

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
