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
| Map | 8.5 × 10.7 km | About 2.8 × 3.6 km (a third each way), all 80 towns and 40 sites kept, walls at three quarters |

## How long things take

Measured on today's map with the battle's speeds and terrain (heavy infantry 1.9 m/s on
open ground, roads ×1.25, forest ×0.7, marsh ×0.5, hills slower, mountains closed to
walkers), routed over the 133 m grid. The last two columns are on the map cut to a third,
with roads at ×2.

| Unit | Nearest town or site, today | Longest town-to-town march, today | Nearest, a third | Longest, a third (at 4×) |
|---|---|---|---|---|
| Heavy infantry | 4.7 min | 84 min | 1.5 min | 26 min (6 min) |
| Light cavalry | 1.3 min | 23 min | 0.4 min | 7 min (2 min) |
| Pegasi | 1.9 min | 39 min | 0.6 min | 13 min (3 min) |
| Griffins | 2.9 min | 59 min | 1.0 min | 20 min (5 min) |
| Dragons | 3.5 min | 71 min | 1.2 min | 24 min (6 min) |

Fliers in the air go straight over mountains and water at their own speed, which nothing
changes; only cavalry is quicker. Boats come later: they will shorten the marches over
water and reach the one islet town walkers can't.

## The map

- **A third each way:** about 2.8 × 3.6 km. All 80 towns and 40 sites stay. Everything
  drawn at map scale shrinks to a third: coasts, ranges, passes, lakes, forests, the
  spacing of towns. Everything at battle scale keeps its size (gates, ramps, houses, river
  and road widths, bridges, the ruins) except the towns' walls, which come in to three
  quarters: a town 55 m in radius instead of 73, a capital 85 m instead of 113.
- **It still fits.** Moved closer with walls at three quarters, the closest two towns stand
  20 m apart and a town's nearest neighbour is typically 62 m off; none overlap. The town
  placer should space towns by their walls rather than their centres, so the closest pairs
  open to 30 m or more. Towns then cover about an eighth of the land instead of 2.4%:
  crowded, and more action.
- **The grids shrink with the map,** as they did when the continent last shrank: 64 × 80
  cells of 44 m, hexes of 89 m. There are as many hexes as today, so territory and income
  from land work as they do now.
- **Roads double a walker's speed,** every gait alike, horse and wheel included, and a
  road's bridges count as road (today a road is ×1.15 to ×1.35 by gait). With one set of
  rules this is the battle's gait table changing (UNITS.md), so set battles feel it too.
  Fliers in the air ignore it.
- **Roads should reach every town.** Today they cover 8% of the land and pass within two
  cells of only 35 of the 80 towns, so doubling them shortens the longest march by just 8%
  (84 to 77 min on today's map). For the road to be the fast way, the continent has to
  join every town to it.

How the size was chosen, moving today's 80 towns closer:

| Each way | Walls | Map | Closest walls | Towns with another within 60 m | Longest march (today's roads), heavy infantry / cavalry |
|---|---|---|---|---|---|
| 1/2 | today's | 4.3 × 5.3 km | 54 m | 4 | 42 / 11 min |
| 0.45 | today's | 3.8 × 4.8 km | 30 m | 8 | 38 / 10 min |
| 0.40 | today's | 3.4 × 4.3 km | 6 m | 41 | 33 / 9 min |
| 1/3 | today's | 2.8 × 3.6 km | 11 towns overlap | 67 | 28 / 8 min |
| **1/3 (chosen)** | **three quarters** | 2.8 × 3.6 km | 20 m | 39 | 28 / 8 min |

## The clock and the day

- **One simulation at 30 ticks a second.** Pause, and 1×, 2×, 4× and 8× in single-player;
  real time in multiplayer.
- **Days stay as the unit of the economy**: income, upkeep, training, boats. **A day is
  2 minutes at 1×.** On the smaller map that keeps today's proportions between marching
  and the economy: heavy infantry reach the nearest place in about three quarters of a day
  (0.7 today) and make the longest march in about 13 days (13.6 today). So the Warlords tables for gold
  and training stay as they are, in days, and a 120-day campaign is four hours at 1×.
- **New squads, at 2 minutes a day.** Each town trains one squad at a time, by today's
  table:

  | Squad | Days | At 1× |
  |---|---|---|
  | Light infantry | 2 | 4 min |
  | Heavy infantry, archers, pikemen, dwarves, warriors | 4 | 8 min |
  | Light cavalry, wolf riders | 5 | 10 min |
  | Heavy cavalry, catapult | 6 | 12 min |
  | A levy of rare troops | 3 | 6 min |

  Gold is the other limit, and the day's length doesn't change it. A town pays about 25
  gold a day (a capital 30), and a squad costs a quarter of its price a day to keep (heavy
  infantry 5). So a town training heavy infantry without a break pays for about four
  squads of its own, which takes it about half an hour. After that, more soldiers need more
  towns or more land.
- **Fast-forward is limited by fighting.** Every fight is simulated in full, so at 8× with
  a big battle on, the machine may not keep up. When it can't, the speed drops to the
  highest it can hold, and the speed control says so. This is the speed setting changing,
  not time bending with the camera.

## Armies on the land

- **An army is a squad,** as in battle: its men, its formation, its facing, its orders. A
  stack becomes the player's group of squads (GAME_DESIGN.md §4.1.2): select them, give
  them a formation, send them.
- **No caps.** A group is as many squads as you select, and a town holds whatever fits
  inside its walls. Warlords' 8 to a cell goes. Packing men tight is its own risk: a
  clump is what a dwarf's grenade is for.
- **Orders are the battle's orders, at any distance.** Tap a town across the island: the
  route planner finds the fastest way and they set off. Long routes are planned on the
  44 m campaign grid with the gaits' speeds, then refined on the 4 m grid over the next few hundred
  metres as the men go. (Planning a 3 km route on the 4 m grid in one go would be far too
  slow: a 100-man order across one battlefield already costs about 20 ms.)
- **Formations hold on the march.** A squad narrows into a column where its road narrows
  (bridges, gates, passes) and opens out again after.
- **Two ways to step a squad, one look.** Away from any enemy (beyond a long bowshot,
  about 150 m) a squad on the march, or a garrison at rest, is stepped as one body: its
  route, its pace, its formation, and every man placed in his slot on the ground. Towns stand closer
  than that to one another, so an enemy at rest behind its own walls doesn't count. When an
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
  already do), the minimap shows the fight they're in, and **Fight!** takes the camera
  there.
- **No dice.** `Campaign::winChance` stays only as the computer lords' estimate when they
  plan.
- **Heroes at ruins** fight the guardian in the ruin itself.

## What you see

| Height | What you see |
|---|---|
| The whole island | The land, territory, towns; every soldier you can see a point in his side's colour |
| A kilometre or two | Columns and blocks of points: the ants on the march |
| A field | Soldiers as sprites, in formation |
| The ground | One man: his shield, his wounds, the arrows in him |

- **No flags over armies.** Zoomed far out, the points can't be told apart: an archer is
  the same dot as a knight. That is the price of the height. Zoom in to see who they are.
- **More is drawn when zoomed out,** but as points in 2D, which is cheap; sprites and
  close-up land are drawn only near the camera.
- **Fog of war, in single-player and multiplayer.** The land, every town and every site are
  always on show; enemy soldiers only where your soldiers, towns or heroes can see them.
  From high up, that means points only where you have eyes. In lockstep every machine holds
  the whole world, so the fog is in what is drawn and told, not in the simulation; as in
  any lockstep game, reading the browser's memory could lift it.

## The minimap

- Every town and site, always.
- Every soldier you can see as a point in his side's colour where the points can be told
  apart; where they can't, a heat map of each side's troops.
- Fights you can see glow. Tap anywhere on it to take the camera there.

## How hard the computer lords are

What a lord can see through the fog is the difficulty. Today's Easy and Hard (GAME_DESIGN.md
§4.2.6) keep their habits, and a Medium goes between them:

| | Sees | Plays like |
|---|---|---|
| Easy | Only what its own soldiers, towns and heroes see | Warlords' Knight, as Easy does today: wastes a fifth of its gold, forgets a stack now and then, picks targets at random |
| Medium (new) | Every soldier as a point, as from high up: where armies are, not what they are, except where its own men see | Warlords' Warlord, as Hard does today |
| Hard | Everything | Warlords' Warlord |

The design promises no secret bonuses, so the difficulty buttons say plainly what each
lord sees.

## What becomes of what is built

| Built | Becomes |
|---|---|
| Campaign stacks, movement points, the 8-to-a-cell rule | Squads on the land, the battle's movement, no caps |
| "Fight it" / "Resolve", the battle window (`Region::fromContinent`) | Fights in place, everywhere |
| Close-up land streamed around the camera | Kept, and the simulation samples the same ground |
| Towns, castles, walls, gates, ramps | Kept, all of them in the simulation at once |
| Gold, upkeep, training, rare troops, heroes, ruins, temples, sages | Kept, in days of 2 minutes |
| Banners rising, territory hexes, income from land | Kept |
| Boats | Later: carrying squads over water in real time |
| The computer lords' campaign AI | Kept, planning in real time instead of daily, its sight set by the difficulty |
| The title's set battles (Ford, Siege) and matches (Horde, CTF) | Kept as they are: a piece of the island with nothing else running |
| Saved games | A new format: the whole world's state |

## Building it, in steps

Each step leaves a playable game.

1. **The map and the clock.** Cut the continent to a third each way and the towns' walls to
   three quarters; run the campaign on the 30 Hz clock with 2-minute days; stacks move at the battle's speeds over the grid. Still flags, but you
   can feel the pace.
2. **Soldiers on the land.** Squads replace flags: points far out, sprites close in, in
   their formation, marching their routes. The minimap.
3. **Fights in place.** Contact turns men into fully stepped soldiers where they stand. The
   whole island's ground, stone and routes in the simulation; no battle window, no prompt,
   no dice; sieges at the real towns.
4. **The rest.** The computer lords in real time, saved games, the fast-forward limit,
   multiplayer, fog of war.
5. **Boats.**

## Open questions

- **The day's length.** 2 minutes for now, to be settled in play by how often new squads
  come. If they come too slowly or too fast, the day is the one number to turn: income,
  upkeep and training move together. A 5-minute day would make marching quick against the
  economy (the longest march about 5 days) and a 120-day campaign ten hours at 1×.
