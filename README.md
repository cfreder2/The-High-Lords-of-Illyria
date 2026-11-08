# The High Lords of Illyria

A real-time tactical strategy game with grand-strategy campaign mechanics: the spatial physics combat of *Myth: The Fallen Lords* meets the named-army attrition campaign of *Warlords*. Built on a custom C++ engine, web-first (itch.io via WebAssembly), native later (Steam).

- 📜 [Technical & Game Design Starter Document](docs/GAME_DESIGN.md)
- 👥 [Target Audience Personas](docs/AUDIENCE_PERSONAS.md)
- ⚔️ [Unit Sheet](docs/UNITS.md) (locked stats for every unit type)
- 🎞️ [Devlog: screenshots and videos of every build](https://github.com/cfreder2/The-High-Lords-of-Illyria/tree/devlog) (`devlog` branch, filled in automatically on each deploy)

**Play the latest build:** https://cfreder2.github.io/The-High-Lords-of-Illyria/ (desktop or phone browser; the version is shown top-left and goes up with every deploy)

**Status:** Phase 0 (pre-production): deterministic simulation core and first playable battle.

### Controls

| | Touch | Mouse |
|---|---|---|
| Zoom (god ↔ ground) | Pinch | Wheel |
| Pan | Drag | Left-drag |
| Rotate | Two-finger twist | Right-drag |
| Select a squad | Tap a blue soldier or a squad card | Click |
| March / attack | Tap ground / tap an enemy | Click |

Settings (on the title screen, or ⚙ Settings in the in-game ☰ menu) hold the graphics (Low, Medium or High, or each effect on and off: they change only the look, every setting plays the same battle), the sound and gameplay settings, and Controls lists every gesture, click and key. The campaign keeps three save slots in the browser.

## Layout

| Path | Contents |
|---|---|
| `engine/sim/` | Deterministic gameplay simulation: fixed-point math, RNG, region generation, pathfinding, ballistics, melee, flight, collisions, remains. No engine, platform or floating-point dependencies. |
| `game/` | Client logic shared by all platforms: camera, touch/mouse gestures, battle flow, remains. |
| `game/render/` | OpenGL ES 3 / WebGL 2 renderer. |
| `game/web/` | Browser entry point and page (HUD, input forwarding). |
| `tests/` | Unit tests, including a pinned "golden" checksum every platform must reproduce. |
| `tools/simcheck` | Headless runner that prints a simulation checksum trace; CI diffs it across platforms. |
| `tools/models` | Turns a set piece's 3D model into what the game loads (`game/web/models/`) and derives its battle stones from the same mesh (`engine/sim/src/site_ruin.inc`): `<bpy-python> -I tools/models/ruin.py -- <ruin.fbx>`. |

## Building

Requires CMake 3.20+ and a C++20 compiler (GCC 11+, Clang 14+, Apple Clang 14+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/tools/illyria_simcheck
```

WebAssembly (requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)):

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
node build-wasm/tools/illyria_simcheck.js
# The playable site is assembled in build-wasm/site:
python3 -m http.server -d build-wasm/site 8000
```

Desktop (SDL 3, OpenGL 4.1 core; macOS first, built to compile on Linux and Windows). SDL 3.4.20 and libwebp 1.6.0 are fetched at configure time (pinned tags) unless installed:

```sh
cmake -S . -B build-app -DCMAKE_BUILD_TYPE=Release -DILLYRIA_BUILD_APP=ON
cmake --build build-app --target illyria_app
open build-app/game/native/Illyria.app            # macOS bundle, files in Contents/Resources
build-app/game/native/Illyria.app/Contents/MacOS/Illyria --conquest   # or from a shell, with options
```

With no option the program opens its start menu (lord, computer lords, Easy/Hard, skirmish at the Ford or the siege, continue). `--conquest [--lord N --foes MASK --easy]`, `--continue`, `--skirmish`, `--siege`, `--stats`, `--size WxH`, `--no-sound`, `--shot FILE.tga:N`, `--help`. On Linux and Windows the build puts an `assets/` folder beside the program. The game loop, renderer and asset files are the web build's; `game/app/platform.h` is the one seam (`game/web/platform_web.cpp`, `game/native/`). In a battle, `/` lists the keys (the page's own). Not there yet: voices, the town and settings screens, the minimap, touch.

Deploys: every push to `master` runs `.github/workflows/deploy.yml`, which builds the site and publishes it to GitHub Pages as version `VERSION.<run number>`.

## Determinism rules for `engine/sim`

Lockstep multiplayer requires every machine to compute identical results, so code in `engine/sim` must:

- use `Fixed` / `Vec3`, never `float` or `double` (except `toDouble()` for display);
- draw randomness only from the world's `Rng`, **at most one draw per expression** (C++ leaves the order of `a() + b()` unspecified, and GCC and Clang really do differ);
- iterate containers in a defined order (no `unordered_map` iteration, no pointer-keyed ordering);
- include any new state in `World::checksum()`.
