# Architecture rules

*The rules every change to The High Lords of Illyria is checked against (2026-11-05). They hold for people and for agents. A change that breaks one is not merged, and "too large" is not a reason to leave a rule broken: split the work into commits and do it.*

Vocabulary: **Conquest** is the whole island with many computer lords (once called the campaign). **Skirmish** is a single battle, a small taste of Conquest. They are one game.

## 0. Skirmish is the baseline

- Skirmish is the game world on a small ground, about an 80 m perimeter, with a small battle. Conquest is the same world with the fence taken away: the whole island is the battlefield, and there is no further information on Conquest. The rules and the way the world works are identical; only the size of the ground and the amount in it differ, so a skirmish takes less time to play.
- Whatever can happen in Conquest can also happen in a skirmish: put a ruin on the ground and a hero can enter it; put a castle in the middle and the player can enter it and produce a unit. Such features belong to the world, not to the Conquest layer. A feature written only for Conquest is a bug in the design.
- Everything else builds on the skirmish world. Nothing in Conquest may replace a skirmish rule (movement, pathfinding, walls, ramps, gates, formations, orders) with a second implementation, and no man is ever stepped by anything but the world's step.

## 1. One game, one code

- One battle system: one `sim::World`, one `Battle` presenting it, one renderer pass over the men, one selection and inspection, one order and command UI, one effects, sound and voice path, one level of detail (dots when far, sprites near, dots pulse in a fight).
- Conquest and Skirmish differ only in what is truly different: the size of the ground, the economy, the town screens and the win conditions. Nothing else may fork.
- No `if (conquest) ... else ...` for selection, orders, HUD, camera, effects, audio or drawing, unless the reason is written in a comment beside it and in the doc.
- The command bar (As they stand / Attack / Scatter / Hold / Find, Charge, formations, squad and hero cards) appears for the player's men in Conquest exactly as in Skirmish. A sleeping man is selectable and orderable like an awake one.
- A second loop over the men, a second pick, a second ring or shadow, a second frame choice is a parallel path. Make the one function serve both.

## 2. DRY

- Before adding a function, search for one that already does it (`conquest_*`, `battle_*`, `mode_*`, `renderer_map`, `renderer`, `main.cpp`, `title.js`). Extend it; do not copy it.
- Two bodies that differ by a constant or a branch are one function with a parameter.
- When a path is replaced, delete the old one in the same commit: no shim, no dead flag, no kept-for-now helper, no stale comment, no stale doc or manual text.

## 3. Network-ready (lockstep)

- The simulation is deterministic and independent of camera, UI and rendering.
- All player intent is a serializable, ordered command applied at a tick. The UI never mutates sim state directly. Conquest orders (march, produce, hire, embark, search, pick) use the same kind of command list as battle orders.
- The step reads no wall clock, no float, no platform math (`sqrt`, `hypot`, `sin`, libm), no unseeded random, no pointer order, no hash-map or `std::function` iteration order. Integers, `Fixed` and the seeded `Rng` only. Floats belong to presentation.
- Save and restore reproduce the state exactly, and `checksum()` covers all of it.
- A test replays a command list from a save and compares the checksum.

## 4. Fog-ready

- There is ONE visibility question (`Conquest::canSee(viewer, owner, x, y)`, taking the viewer's sight from own men, towns and heroes). The simulation stays whole-world; fog is in what is drawn, told and inspectable.
- Everything shown of another side calls it: the men drawn (dots and sprites), picks, inspection, tooltips, the minimap, the Fight! button, event messages, unit and town lists, and the computer lords' difficulty (Easy sees its own, Medium points, Hard all).
- The viewer is a parameter, never a literal `0`.
- A test hides a side through the seam and checks that no surface mentions it.

## 5. Quality

- No dead code, no compatibility shims, no stale docs or manual text.
- Tests pin the rule, not just a golden: a change that fixes a bug or builds a rule adds the test that would have caught it.
- Budgets hold: an ordinary tick about 0.005 ms in wasm (the dawn planning spike is known), a fight about 4 microseconds per awake man per tick native.

## Checklist for every change

- [ ] Does it add a second way to select, order, draw, inspect or hear a man? (it must not)
- [ ] Is there an `if conquest / if skirmish` left? Is its reason written?
- [ ] Did I search for an existing function before writing one? Is anything now duplicated?
- [ ] Was the old path deleted, with its docs, comments and tests?
- [ ] Does player intent go through the ordered command list, applied at a tick?
- [ ] Does the step use only integers, `Fixed`, the seeded `Rng`? Any iteration-order dependence?
- [ ] Does a save restore the same checksum?
- [ ] Does everything shown of an enemy go through the one visibility question, with the viewer a parameter?
- [ ] Is there a test for the rule? Are the budgets measured?
- [ ] Did I decline anything as "too large"? (then split it and do it)
