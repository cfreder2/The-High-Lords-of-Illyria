# The High Lords of Illyria — Technical & Game Design Starter Document

| | |
|---|---|
| **Version** | 1.0.0 |
| **Project Codename** | Project Illyria |
| **Engine Target** | Custom C++20 engine, deterministic lockstep simulation — see §5.1 |
| **Target Platform** | Web browser first (itch.io, via WebAssembly); native PC later (Steam / GOG) |

---

## 1. Executive Summary & Vision

*The High Lords of Illyria* is a real-time tactical strategy game (RTT) with deep grand-strategy map mechanics. It bridges two iconic classic paradigms:

1. **The Spatial Physics & Tactical Momentum of *Myth: The Fallen Lords*** — projectile ballistics, physical debris, elevation-based physics, dynamic mutilation, spatial directional combat.
2. **The Macro-Strategy, Named Attrition, and Probability Progression of *Warlords*** — continent campaign map, named unit identity, slow production cycles, temple blessings, high unit scarcity.

## 2. Graphic & Rendering Engine Architecture (Hybrid 3D / Billboards)

To replicate the iconic aesthetic of late-90s tactical games while leveraging modern lighting and performance, the game uses a **Hybrid 3D-Sprite Rendering Architecture**.

### 2.1 Visual Categorization

- **Terrain & Architecture (Full 3D):** Fully modeled 3D heightfield mesh terrain, physical cliffs, dynamic water, destructible stone walls, and trees with individual collision capsules.
- **Standard Infantry & Light Units (2.5D Sprites / Billboards):** Multi-directional sprite sheets rendered onto camera-facing quads in 3D space — 16 directional angles with distinct animation states: Idle, Walk, Attack, Block, Dodge, Hit, Death, Mutilation.
- **Large Creatures, Siege Engines & Heroes (Full 3D Models):** Giant units (e.g., Minotaurs, Siege Engines, Dragons, Mounted Heroes) are rendered as full 3D skeletal meshes with physical ragdolls to allow complex spatial deformation and multi-joint weapon swings.

### 2.2 Shadow & Lighting Integration

- Billboard quads write to a custom **Depth Pre-Pass** buffer, allowing 2.5D units to cast real-time directional shadows onto 3D terrain and receive dynamic point lighting from fire, spells, and torchlight.

## 3. Core Combat & Physics Mechanics

### 3.1 Dispersion Circle & Ballistic Projectiles (Ranged Combat)

Ranged attacks do not use guaranteed hit animations. Projectiles follow real Newtonian trajectories governed by gravity, wind, and initial velocity vectors.

- Each shot's aim point is sampled inside a **dispersion circle** around the target. The circle's radius grows with range and shrinks with unit veterancy (§4.2).
- Missed projectiles stay physical: they can strike friendly units, terrain, or debris along their path.
- *All randomness must come from the seeded, deterministic simulation RNG (§5).*

### 3.2 Spatial Melee Physics (Sword vs. Shield / Flesh)

Melee combat is dictated by physical bounding volumes and weapon trace envelopes rather than percentage-based hit rolls.

1. **Physical Shield Collision**
   - Shields are active 3D/2.5D colliders covering a frontal cone (*default **120°**, tunable per shield type — value TBD, see Q4*).
   - Weapon swing traces that hit the shield collider calculate **Deflection**: damage is mitigated by shield rating, and the attacker experiences poise drain or recoil.
2. **Spatial Dodge Envelopes**
   - High-Agility units trigger a physical hurtbox displacement frame during an incoming attack trace. The unit performs a quick evasive step, causing the enemy weapon to slice through open air.
3. **Flanking & Rear Exploits**
   - Strikes originating outside the defender's frontal arc ignore shield collision entirely, applying unmitigated damage directly to flesh or armor.

## 4. Macro Campaign Map & Unit Mechanics

### 4.1 3D Strategic Map

- The campaign map is a 3D recreation of the classic Warlords-style continent layout (*original Illyrian geography — see Q5*).
- Players maneuver armies across hex-based terrain tiles: Roads, Plains, Forests, Swamps, Mountain Passes.
- Moving into enemy territory or encountering hostile armies seamlessly transitions into a local real-time physics battlefield matching the regional terrain topology.

### 4.2 Unit Identity, Progression & Attrition

- **Stack Limit (from *Warlords*):** At most **8 squads** may occupy a map hex, and an attack commits at most 8. A defended city (2×2 hexes) fights with everything inside it, up to **32 squads**. This caps the largest possible battle at **40 squads** (~480 soldiers at ~12 per squad), which is what keeps several simultaneous full-physics battles inside the frame budget (§5.2).
- **Named Units:** Every unit batch recruited receives a unique name (e.g., *1st Oakhaven Footmen*, *The Grey Watch*).
- **Slow Recruitment Cycle:** Units take multiple turns/minutes to produce. Replacing a wiped-out veteran squad is extremely costly, encouraging strategic retreats and tactical preservation.
- **Veterancy & Blessings**
  - **Level Progression:** Units gain rank through battles, shrinking their dispersion circle and increasing poise/stamina reserves.
  - **Temple Blessings:** Controlling sacred shrines across Illyria unlocks probability buffs (e.g., +15% shield block arc, elemental projectile imbues, panic resistance).

## 5. Technical Network Architecture

To keep hundreds of physical projectiles, dynamic ragdolls, and multi-unit interactions perfectly synchronized across multiplayer:

- **Deterministic Lockstep Engine:** All combat simulation logic runs at a fixed tick rate (e.g., 30 Hz). Player inputs are transmitted over the network and executed synchronously on all client simulation states.
- **Client-Side Interpolation:** Sprite animations, particle effects, and visual rendering run at native client framerates (60+ FPS) while interpolating between fixed simulation ticks.
- **Determinism requirements (implied by lockstep):**
  - Simulation uses **fixed-point math** (or strictly controlled floats) — never engine-native PhysX/Chaos for gameplay-relevant state.
  - A single seeded RNG stream owned by the simulation; render/VFX code uses a separate non-authoritative RNG.
  - Per-tick **state checksums** exchanged between peers for desync detection, plus input-log replays for debugging.
  - Ragdolls and cosmetic debris that don't affect gameplay may run in non-deterministic engine physics on each client.

### 5.1 Technology Decisions

| Area | Decision | Why |
|---|---|---|
| Engine | **Custom engine in C++20** | No licences or vendor tools needed; a deterministic lockstep simulation must be custom anyway, since neither Unity's nor Unreal's physics is bit-reproducible across machines. |
| Web build | **Emscripten → WebAssembly + WebGL 2**, hosted on itch.io | Lowest-friction release: playable in a browser with no download (the approach used by the Avara community port). |
| Native build | Same codebase compiled with Clang/GCC for Windows, macOS, Linux | Path to a Steam/GOG release without a rewrite. Windows builds use clang-cl or MinGW (the simulation needs 128-bit integers, which MSVC lacks). |
| Platform layer | SDL2 (window, input, audio) + OpenGL ES 3.0 / WebGL 2 | One rendering API that works natively and in the browser. |
| Simulation math | 64-bit fixed point (16 fractional bits), PCG32 RNG, FNV-1a state checksums | Identical results on x86-64, ARM and WebAssembly. Enforced by CI, which compares a full simulation trace from every platform. |
| Networking | Lockstep inputs over WebSockets through a small relay server (WebRTC data channels later) | Browsers cannot open raw UDP sockets; lockstep sends only inputs, so bandwidth is tiny. |
| Threading | Single-threaded simulation at first | Web threads need cross-origin isolation (itch.io supports it, but it adds friction); the 30 Hz simulation fits comfortably on one core. |

### 5.2 Performance Budget (60 fps on laptop integrated graphics)

- **Rendering is always 60 fps**, interpolated between 30 Hz simulation ticks. Only the battle the player is viewing is rendered; the minimap draws dots from simulation state.
- **Each battle is its own simulation world**, with a spatial grid for hit tests. Measured on the first core (native, before optimisation): one 450-soldier battle costs 0.8 ms/tick, while nine such battles sharing one world cost 69 ms/tick. Separating them is mandatory.
- **Simulation tiers:**

| Tier | Applies to | Runs |
|---|---|---|
| Full + visible | The battle on screen | Deterministic sim + cosmetic physics (ragdolls, debris, gore) + rendering |
| Full, headless | Every other battle involving a human player | Deterministic sim only |
| Auto-resolve | AI-vs-AI battles away from human players | Warlords-style combat rolls on the campaign layer |

- **Worst case target:** 9 simultaneous 40-squad battles in under 4 ms/tick in the WebAssembly build. CI will fail the build when a benchmark exceeds this.

## 6. Development Roadmap

> *Proposed — durations assume a small core team and are placeholders for planning.*

| Phase | Goal | Key Deliverables | Exit Criteria |
|---|---|---|---|
| **0. Pre-Production** (≈2 mo) | Lock tech decisions | ✅ Engine choice (§5.1); ✅ fixed-point math + deterministic RNG library; ✅ cross-platform determinism CI; SDL2/WebGL 2 platform layer running in the browser; art style test (one billboard unit, one 3D unit, one terrain tile) | Written tech decision record; style test approved; a build playable on itch.io |
| **1. Deterministic Core** (≈3 mo) | Headless simulation | Fixed 30 Hz tick loop; input command queue; checksums; replay recorder; heightfield terrain collision | Same replay produces identical checksums across two machines/OSes |
| **2. Combat Vertical Slice** (≈4 mo) | Prove the "Myth feel" | Ballistic projectiles + dispersion circle; shield cone deflection; dodge envelopes; flanking; 2 infantry types, 1 archer type, 1 large 3D creature; billboard depth pre-pass & shadows | 10 v 10 skirmish is fun and readable; 200 projectiles in flight at 30 Hz sim without frame drops |
| **3. Campaign Map Prototype** (≈3 mo) | Prove the "Warlords loop" | Hex map with terrain types; armies, cities, production queues; named unit generator; veterancy; shrine blessings; map → battle → map transition with persistent casualties | One full campaign playable vs. simple AI with meaningful attrition decisions |
| **4. Multiplayer Alpha** (≈3 mo) | Lockstep over the wire | Peer-to-peer/relay lockstep; input delay tuning; desync reporting; lobby | 2–4 players complete battles over internet with zero unrecovered desyncs in test suite |
| **5. Content & AI** (≈6 mo) | Fill Illyria | Full faction rosters; full continent map; tactical + strategic AI; heroes & siege; mutilation/gore pass; audio | Feature complete; campaign completable for every faction |
| **6. Beta & Launch** (≈3 mo) | Ship | Balance, performance, Steam/GOG/itch.io builds, achievements, mod/replay sharing | Release candidate passes cert/QA; launch |

## 7. Open Questions

- ~~**Q1 — Engine:**~~ *Resolved:* custom C++ engine, web-first via WebAssembly (§5.1).
- **Q2 — Campaign timing:** Turn-based campaign with real-time battles (one battle at a time in single-player), or a fully real-time continent (up to one battle per faction at once)? The budget in §5.2 covers the second. In multiplayer, do other players wait, watch, or keep playing during a battle?
- **Q3 — Recruitment time units:** "Turns" on the campaign map vs. "minutes" inside battles — define whether reinforcements can arrive mid-battle.
- **Q4 — Shield arc value:** Confirm default frontal shield cone (placeholder 120°) and how Temple Blessings (+15%) scale it.
- **Q5 — IP:** The campaign map and naming must be original to Illyria; avoid reproducing *Warlords* or *Myth* assets, maps, or names.
