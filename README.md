# The High Lords of Illyria

A real-time tactical strategy game with grand-strategy campaign mechanics: the spatial physics combat of *Myth: The Fallen Lords* meets the named-army attrition campaign of *Warlords*. Built on a custom C++ engine, web-first (itch.io via WebAssembly), native later (Steam).

- 📜 [Technical & Game Design Starter Document](docs/GAME_DESIGN.md)

**Status:** Phase 0 (pre-production): deterministic simulation core.

## Layout

| Path | Contents |
|---|---|
| `engine/sim/` | Deterministic gameplay simulation: fixed-point math, RNG, ballistics, world tick. No engine, platform or floating-point dependencies. |
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
```

## Determinism rules for `engine/sim`

Lockstep multiplayer requires every machine to compute identical results, so code in `engine/sim` must:

- use `Fixed` / `Vec3`, never `float` or `double` (except `toDouble()` for display);
- draw randomness only from the world's `Rng`;
- iterate containers in a defined order (no `unordered_map` iteration, no pointer-keyed ordering);
- include any new state in `World::checksum()`.
