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

The ⚙ button opens the graphics settings: Low, Medium or High, or switch individual effects on and off. They change only the look; every setting plays the same battle.

## Layout

| Path | Contents |
|---|---|
| `engine/sim/` | Deterministic gameplay simulation: fixed-point math, RNG, region generation, pathfinding, ballistics, melee, flight, collisions, remains. No engine, platform or floating-point dependencies. |
| `game/` | Client logic shared by all platforms: camera, touch/mouse gestures, battle flow, remains. |
| `game/render/` | OpenGL ES 3 / WebGL 2 renderer. |
| `game/web/` | Browser entry point and page (HUD, input forwarding). |
| `tests/` | Unit tests, including a pinned "golden" checksum every platform must reproduce. |
| `tools/simcheck` | Headless runner that prints a simulation checksum trace; CI diffs it across platforms. |

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

Deploys: every push to `master` runs `.github/workflows/deploy.yml`, which builds the site and publishes it to GitHub Pages as version `VERSION.<run number>`.

## Determinism rules for `engine/sim`

Lockstep multiplayer requires every machine to compute identical results, so code in `engine/sim` must:

- use `Fixed` / `Vec3`, never `float` or `double` (except `toDouble()` for display);
- draw randomness only from the world's `Rng`, **at most one draw per expression** (C++ leaves the order of `a() + b()` unspecified, and GCC and Clang really do differ);
- iterate containers in a defined order (no `unordered_map` iteration, no pointer-keyed ordering);
- include any new state in `World::checksum()`.
