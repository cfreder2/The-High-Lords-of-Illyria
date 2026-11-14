# The High Lords of Illyria

A real-time strategy game in two layers. Above, a conquest of a continent after *Warlords*: eight lords, castles and towns, armies marching on roads, heroes, gold. Below, wherever two armies meet, a battle after *Myth: The Fallen Lords*: every soldier is a figure on real ground, arrows fly and can miss. The land you march over is the land you fight on. It also has skirmishes (Oakhaven Ford and a siege), Horde, Capture the Flag and a sandbox in the web build. The simulation is a custom C++ engine, deterministic for lockstep play.

**Play the live build:** https://cfreder2.github.io/The-High-Lords-of-Illyria/ (desktop or phone browser). Every push to `master` deploys it; the version is under the game's name at the top left.

**Status:** playable, and still changing. The web build is the complete one. The desktop app (SDL 3) shares its game loop and renderer, and has a start menu, the conquest, the two skirmishes, a minimap and a HUD; it does not yet have the town and settings screens, voices or touch.

## Documents

- [Manual](docs/manual/) (the same text the game's Manual page shows; the controls table is in `docs/manual/21-tables.md`)
- [Game design](docs/GAME_DESIGN.md), [units](docs/UNITS.md), [one world](docs/ONE_WORLD.md), [netcode](docs/NETCODE.md), [architecture rules](docs/ARCHITECTURE_RULES.md)
- [Devlog](https://github.com/cfreder2/The-High-Lords-of-Illyria/tree/devlog): screenshots and videos of each build (the `devlog` branch)

## Controls

Press `/` in a battle for the keys; Controls on the title screen lists every gesture, click and key. In short: click or tap to select, right-click or tap the ground to march, drag a box to select several, wheel or pinch to zoom, `M` shows the map, `Esc` pauses. The frame rate line is off until asked for: `I`, or `--stats` on the desktop, or `?stats` on the web.

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

With no option the program opens its start menu (lord, computer lords, Easy/Hard, skirmish at the Ford or the siege, continue). `--conquest [--lord N --foes MASK --easy]`, `--continue`, `--skirmish`, `--siege`, `--stats`, `--size WxH`, `--no-sound`, `--shot FILE.tga:N`, `--help`. On Linux and Windows the build puts an `assets/` folder beside the program. The game loop, renderer and asset files are the web build's; `game/app/platform.h` is the one seam (`game/web/platform_web.cpp`, `game/native/`). In play, `/` lists the keys and `I` shows the frame rate line. Not there yet: voices, the town and settings screens, touch.

Deploys: every push to `master` runs `.github/workflows/deploy.yml`, which builds the site and publishes it to GitHub Pages as version `VERSION.<run number>`.

## Determinism rules for `engine/sim`

Lockstep multiplayer requires every machine to compute identical results, so code in `engine/sim` must:

- use `Fixed` / `Vec3`, never `float` or `double` (except `toDouble()` for display);
- draw randomness only from the world's `Rng`, **at most one draw per expression** (C++ leaves the order of `a() + b()` unspecified, and GCC and Clang really do differ);
- iterate containers in a defined order (no `unordered_map` iteration, no pointer-keyed ordering);
- include any new state in `World::checksum()`.
