# Netcode: Lockstep Matches for up to Four Players

The battle simulation (`engine/sim`) is deterministic: integers and fixed point, one seeded RNG,
no clocks, no unordered iteration, a checksum of the whole state (`World::checksum`). The same
starting state and the same commands, tick by tick, give the same battle on every machine; GCC,
Clang and WebAssembly agree (the golden tests). So multiplayer is **deterministic lockstep**: peers
exchange only their players' inputs, and every peer runs the whole battle.

No networking is built yet. What is built is the seam: the matches (Horde and capture the flag,
`docs/GAME_DESIGN.md` §4.5) are played by **seats**, and everything a seat does travels as its
input stream. Offline, one machine fills every stream; online, the streams come from the peers.

## Seats, teams and inputs (`engine/sim/include/illyria/sim/match.h`)

- A match has up to four **seats** (`sim::Seat`): a person, the computer or nobody. Each sits on a
  **team**, which is the simulation's `PlayerId`: soldiers of one team are friends, of different
  teams enemies. Several seats can share a team (Horde: all four hold the hill; capture the flag:
  two seats on one team share its flag and its budget). Horde adds a fifth seat, the horde itself,
  always played by the computer.
- Each soldier belongs to one seat (`sim::SeatRoster`, by `EntityId`). A seat may order only its own
  soldiers: `sim::mergeOrders` drops any order for another seat's men, and stamps each order with
  the seat's team, so a peer can neither command an ally's army nor the enemy's.
- A seat's **input** for a tick (`sim::SeatInput`) is its orders (`sim::Command`: move, attack a
  soldier, a spot, a gate or a wall, stop, force march, with formation places and facing) and its
  **match actions** (`sim::MatchAction`: Horde's Recruit an offer, Ready, Recall the fallen hero). Ticks without input are
  sent as empty (or not at all; see below).
- `sim::InputLog` keeps every seat's inputs in (tick, seat) order. It is the match's replay: a
  battle fed the same log from the same setup reproduces the match bit for bit (`SeatedMode::replay`;
  `horde_four_seats_deterministic`, `ctf_four_teams_deterministic`). Its checksum, with the world's
  and the mode's (`BattleMode::checksum`), is what peers compare.

## A tick

Each tick of 1/30 s, on every peer, in this order (`Battle::step`, `SeatedMode::gather`):

1. **Gather** the tick's inputs of every seat (the local player's from the UI, the computer seats'
   from their AI, the remote players' from the network).
2. **Log** them, **merge** their orders in seat order (each seat's own soldiers only), and apply
   their match actions in seat order (a reinforcement bought spawns its squad, a seat is ready).
3. **Step** the world with the merged orders.
4. **Rules** (`engine/sim/src/mode_rules.cpp`, read from the simulation only, integers and fixed
   point): the hill's clock, the flags, a beaten wave, the break's mending and levels and points,
   the next wave's spawn, a team put out (`World::breakSide`), the verdict.

Steps 2 to 4 depend on nothing but the inputs and the state, so they agree everywhere. The
computer's thinking (`HordeMode::think`, `CtfMode::think`, `Battle::aiFight`) uses the battle's
floats and its presentation-side memory; it does **not** have to agree between machines, because
it only ever produces inputs, and those are sent like a person's.

## The protocol

**Lobby (before tick 0).** The host gathers the seats and agrees the **match setup** with every
peer: the mode, the place (an index into `battlePlaces`, which every peer derives from the same
continent), the seed, the seats (control and team), the difficulty and wave count (Horde), the
teams, budget, time limit and each seat's picked army (capture the flag), and the build's version
(the simulation must be the same code). In the options JSON this is exactly what `makeMatch` takes
(`game/include/illyria/game/mode.h`). Every peer builds the same field and armies from it.

**Turns.** Inputs are scheduled `D` ticks ahead (input delay; 3-6 ticks, 100-200 ms, adapted to the
worst round trip): what a player does at tick `t` is sent as his input for tick `t + D`. A peer
steps tick `t` only once it holds every playing seat's input for `t` (an empty input counts; a
peer sends one for each tick, or one "nothing until tick n" message, so silence never stalls the
others). The local player's orders show at once in the UI (markers, voices), and take effect D ticks
later on every machine alike.

**The computer's seats** (Horde's allies and its horde, capture the flag's computer teams) are
played by **the host**: its machine runs their AI and sends their inputs like its own player's.
If the host leaves, another peer takes them over from the next tick (the AI's memory is soft: it
re-plans from the state within two seconds).

**Desync checks.** Every `N` ticks (30: once a second) each peer sends the checksum of the world,
the mode and the input log. A mismatch means a bug (or a cheat): log both peers' states and inputs,
stop the match, and report. The replay test is the guard against such bugs shipping.

**Joining late, dropping out.** A peer can join a match in progress only by replaying the log from
the setup (fast: the simulation runs far quicker than real time) or from a state snapshot (not
built). A player who drops is replaced by the computer (the host plays his seat; his soldiers stay
his seat's); the others keep playing.

## The relay

Browsers cannot accept connections, so the peers talk through a **relay** (a small server: a
WebSocket per peer, or WebRTC data channels it brokers). It needs to:

- host lobbies (create, list, join by code; at most four players a match), and pass the match setup;
- forward each peer's input messages to the others, in order, with the tick they are for (the relay
  does not run the simulation and need not understand the inputs; it can be a few hundred lines);
- stamp nothing and decide nothing: every peer still checks every input (a seat's own soldiers
  only), so a lying relay or peer can only desync, not cheat silently;
- forward the periodic checksums, and tell peers when one leaves (so the host takes over its seat);
- optionally keep each match's log for replays and for settling desync reports.

Bandwidth is tiny: an order is about 60 bytes, a player gives a few a second; four players need well
under 1 KB/s each. Latency matters more than bandwidth: a relay near the players, and the input
delay above.

## How the modes map onto it

| | Horde | Capture the flag |
|---|---|---|
| Seats | 1-4 players' seats on team 0 (the holders), and seat 4, the horde, team 1 | 1-4 seats on 2-4 teams |
| Played by the host | the horde, and empty seats given to the computer | the computer's teams and allies |
| Orders | each seat its own squads (its hero, army and recruits) | each seat its own squads |
| Match actions | Recruit (an offer, in a break), Ready | none (the armies are bought in the lobby) |
| Rules from the state | waves (`hordeWave`: wave number, difficulty, seats, seed), the hill (`HillRule`), the break (mending, levels, points), the end | the flags (`FlagRule`), a team out (`breakSide`), the time limit, the end |
| Setup in the lobby | place, seed, seats, difficulty, waves, break and muster lengths | place, seed, seats and teams, budget, each seat's army, time limit |

## What is left to build

- The network transport and the relay; the lobby screens; input delay and the stall/catch-up loop
  (today `Battle::advance` steps as fast as wall time allows).
- (Done) The local seat is a setting: `SeatedMode::setLocalSeat` (the options' `localSeat`, `"local"`)
  sets the battle's local side (`Battle::localSide`): selection, orders, cards, voices, alerts and the
  HUD follow it; nothing in the simulation reads it, so a peer sits down by setting it and nothing else
  (`tests/test_match_seats.cpp` replays one match from two seats and gets the same hashes).
- `Battle::runEnemyAi` (the non-match battles) still issues orders outside any seat; the matches do not
  use it.
- State snapshots for joining late, and a host migration message.
