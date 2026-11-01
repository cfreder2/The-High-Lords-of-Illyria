# One World

*A proposal (2026-11-01), not yet built. It replaces the Warlords stack model on the
campaign map (GAME_DESIGN.md §4.2.4 to §4.2.9) and keeps the promise of §4.1: one
continent, one clock, the camera from the gods to the mud.*

## The promise

- **One clock.** Everything happens in real time on one 30 Hz simulation: the march, the
  fight, the town training its next army. Zooming never changes how fast time passes.
- **One world.** There are no battle maps, battle windows or battle zones. Soldiers are on
  the land all the time. Zoom in anywhere and you see whatever is happening there.
- **Ants, then units, then the man.** From high up every soldier is a point in his side's
  colour; lower down the points become columns and formations; at the bottom, one man,
  his shield and his wounds.
- **Every fight is physical.** Two armies that meet fight where they meet, with the same
  rules as any battle today, whether or not anyone is watching. Nothing is settled by dice.
- **Only what is on screen is drawn.** The simulation runs everywhere; drawing follows the
  camera. Moving towards a fight prepares it for drawing (its ground, sprites and sounds
  stream in ahead of the camera), as the close-up land already does in 64 m chunks.
- **Marching takes real time.** Tell an army to march to a town and the route planner
  finds the fastest way; the men set off in their formation, and it may legitimately take
  a while. That is why the map has to be small.

## What changes

| | Now | One world |
|---|---|---|
| Clock | Campaign: 10 ticks a second, a day is 30 s. Battles: 30 ticks a second, a separate clock | One 30 Hz clock for everything |
| Armies | A flag on a 133 m cell, hopping cell to cell | Squads of soldiers on the land, in formation |
| Moving | Warlords movement points and terrain costs | The battle's own route planner, gaits and terrain speeds |
| Fights | "Fight it" opens a battle in a 1 km window; "Resolve" rolls dice | Fought where the armies meet, always physically |
| Zoomed out | Flags with labels | Points in side colours; no flags |
| Overview | None | A minimap: every place, every soldier, a heat map where they pile up |
| Map | 8.5 × 10.7 km | About 4.3 × 5.3 km (half each way), all 80 towns and 40 sites kept |

## How long things take

Measured on today's map with the battle's speeds and terrain (heavy infantry 1.9 m/s on
open ground, roads ×1.25, forest ×0.7, marsh ×0.5, hills slower, mountains closed to
walkers), routed over the 133 m grid. The halved column is today's distances halved.

| Unit | Nearest town or site, today | Longest town-to-town march, today | Nearest, halved | Longest, halved (at 4×) |
|---|---|---|---|---|
| Heavy infantry | 4.7 min | 84 min | 2.4 min | 42 min (10 min) |
| Light cavalry | 1.3 min | 23 min | 0.6 min | 11 min (3 min) |
| Pegasi | 1.9 min | 39 min | 1.0 min | 20 min (5 min) |
| Griffins | 2.9 min | 59 min | 1.5 min | 30 min (7 min) |
| Dragons | 3.5 min | 71 min | 1.7 min | 36 min (9 min) |

Fliers go straight over mountains and water; only cavalry is quicker.

## The map

- **Half each way:** about 4.3 × 5.3 km. All 80 towns and 40 sites stay. Everything drawn
  at map scale halves: coasts, ranges, passes, lakes, forests, the spacing of towns.
  Everything at battle scale keeps its size: walls, gates, ramps, houses, river and road
  widths, bridges, the ruins.
- **It still fits.** Town centres are 472 to 518 m apart today, so halved the closest walls
  stand 54 m apart (four towns have a neighbour's walls within 60 m; none overlap). Towns
  then cover about a tenth of the land instead of 2.4%: crowded, and more action.
- **Hexes keep their size** (267 m), so there are about a quarter as many. They stay for
  territory and income.

## The clock and the day

- **One simulation at 30 ticks a second.** Pause, and 1×, 2×, 4× and 8× in single-player;
  real time in multiplayer.
- **Days stay as the unit of the economy**: income, upkeep, training, boats. **A day is
  5 minutes at 1×.** On the halved map that keeps today's proportions between marching and
  the economy: heavy infantry reach the nearest place in about half a day (0.7 today) and
  make the longest march in about 8 days (13.6 today). So the Warlords tables for gold and
  training can stay as they are, in days.
- **Fast-forward is limited by fighting.** Every fight is simulated in full, so at 8× with
  a big battle on, the machine may not keep up. When it can't, the speed drops to the
  highest it can hold, and the speed control says so. This is the speed setting changing,
  not time bending with the camera.

## Armies on the land

- **An army is a squad,** as in battle: its men, its formation, its facing, its orders. A
  stack becomes the player's group of squads (GAME_DESIGN.md §4.1.2): select them, give
  them a formation, send them.
- **Orders are the battle's orders, at any distance.** Tap a town 3 km away: the route
  planner finds the fastest way and they set off. Long routes are planned on the 133 m
  grid with the gaits' speeds, then refined on the 4 m grid over the next few hundred
  metres as the men go. (Planning a 3 km route on the 4 m grid in one go would be far too
  slow: a 100-man order across one battlefield already costs about 20 ms.)
- **Formations hold on the march.** A squad narrows into a column where its road narrows
  (bridges, gates, passes) and opens out again after.
- **Two ways to step a squad, one look.** Away from any enemy (beyond a long bowshot,
  about 150 m) a squad on the march, or a garrison at rest, is stepped as one body: its
  route, its pace, its formation, and every man placed in his slot on the ground. When an
  enemy comes within that distance, or a man is hurt, every man is stepped on his own,
  exactly as in a battle today. Which way a squad is stepped depends only on the world,
  never on the camera, so every machine and every zoom sees the same thing. Drawn, the two
  are the same: every man in his place, walking.
- **Why it has to be:** an all-computer campaign under today's rules holds about 1,800
  soldiers at the start (1,650 of them in neutral town garrisons), 3,800 by day 40 and
  6,800 by day 120. A 450-soldier fight costs about 0.8 ms a tick natively (§5.2), slower
  in the browser. Stepping all 6,800 men in full would take about 12 ms every tick, over a
  third of a core at 1×, and impossible at 4×.

## Fights

- **They start where squads meet.** The rules are today's battle rules: orders, morale,
  initiative, the drag to face, formations, walls and gates.
- **Sieges happen at the real towns.** The attackers march up to the walls and the
  defenders hold them. A town falls by its capture point (§3.5) or by raising a banner
  (§4.2.8).
- **Fights nobody is watching** are fought just the same. The computer lords command their
  squads with the battle AI. The player's squads fight back on their own when struck (they
  already do), the minimap shows the fight, and **Fight!** takes the camera there.
- **No dice.** `Campaign::winChance` stays only as the computer lords' estimate when they
  plan.
- **Heroes at ruins** fight the guardian in the ruin itself.

## What you see

| Height | What you see |
|---|---|
| The whole island | The land, territory, towns; every soldier a point in his side's colour |
| A few km | Columns and blocks of points: the ants on the march |
| A field | Soldiers as sprites, in formation |
| The ground | One man: his shield, his wounds, the arrows in him |

- **No flags over armies.** Zoomed far out, the points can't be told apart: an archer is
  the same dot as a knight. That is the price of the height. Zoom in to see who they are.
- **More is drawn when zoomed out,** but as points in 2D, which is cheap; sprites and
  close-up land are drawn only near the camera.

## The minimap

- Every town and site, always.
- Every soldier as a point in his side's colour where the points can be told apart; where
  they can't, a heat map of each side's troops.
- Fights glow. Tap anywhere on it to take the camera there.

## What becomes of what is built

| Built | Becomes |
|---|---|
| Campaign stacks, movement points, the 8-to-a-cell rule | Squads on the land, the battle's movement |
| "Fight it" / "Resolve", the battle window (`Region::fromContinent`) | Fights in place, everywhere |
| Close-up land streamed around the camera | Kept, and the simulation samples the same ground |
| Towns, castles, walls, gates, ramps | Kept, all of them in the simulation at once |
| Gold, upkeep, training, rare troops, heroes, ruins, temples, sages | Kept, in days of 5 minutes |
| Banners rising, territory hexes, income from land | Kept |
| Boats | Kept, sailing in real time |
| The computer lords' campaign AI | Kept, planning in real time instead of daily |
| The title's set battles (Ford, Siege) and matches (Horde, CTF) | Kept as they are: a piece of the island with nothing else running |
| Saved games | A new format: the whole world's state |

## Building it, in steps

Each step leaves a playable game.

1. **The map and the clock.** Halve the continent; run the campaign on the 30 Hz clock with
   5-minute days; stacks move at the battle's speeds over the grid. Still flags, but you
   can feel the pace.
2. **Soldiers on the land.** Squads replace flags: points far out, sprites close in, in
   their formation, marching their routes. The minimap.
3. **Fights in place.** Contact turns men into fully stepped soldiers where they stand. The
   whole island's ground, stone and routes in the simulation; no battle window, no prompt,
   no dice; sieges at the real towns.
4. **The rest.** The computer lords in real time, saved games, the fast-forward limit,
   multiplayer.

## Open questions

- **How much to cut.** Today's 80 towns moved closer as the map shrinks, walls kept at
  their size (town 73 m in radius, capital 113 m):

  | Each way | Map | Closest walls | Towns within 60 m of another | Longest march, heavy infantry / cavalry | Walls for 30 m between every pair |
  |---|---|---|---|---|---|
  | 0.71 (half the area) | 6.0 × 7.6 km | 155 m | 0 | 59 / 16 min | as today |
  | 1/2 (proposed) | 4.3 × 5.3 km | 54 m | 4 | 42 / 11 min | as today |
  | 0.45 | 3.8 × 4.8 km | 30 m | 8 | 38 / 10 min | as today |
  | 0.40 | 3.4 × 4.3 km | 6 m | 41 | 33 / 9 min | 87% |
  | 1/3 | 2.8 × 3.6 km | 11 towns overlap | 67 | 28 / 8 min | 70% (town 51 m, capital 79 m) |

  A third each way leaves about two 267 m hexes per town, and every garrison within
  bowshot of the next, so "away from any enemy" would have to mean away from any enemy not
  at rest behind its own walls.
- **The day's length.** 5 minutes is proposed.
- **What you can see of the enemy.** The minimap shows every soldier; is there fog of war?
- **Squads to a group, and a town's room.** Warlords' 8 to a cell has no cell left to apply
  to. Is there a cap on how many squads march together, or a town holds (32 today)?
