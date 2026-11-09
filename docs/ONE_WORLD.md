# One World

*A proposal (2026-11-01), built in steps. Steps 1 to 3 are in the game (see "Building it,
in steps" below); the rest is not. It replaces the Warlords stack model on the campaign map
(GAME_DESIGN.md §4.2.4 to §4.2.9) and keeps the promise of §4.1: one continent, one clock,
the camera from the gods to the mud. The campaign is not two modes, a map and a battle: it is
always the tactical game, seen from as high or as low as you like.*

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

| | Now (after steps 1 to 3) | One world |
|---|---|---|
| Clock | The campaign runs on the battle's 30 ticks a second, a day is 2 minutes, and fights are stepped on that same clock | One 30 Hz clock for everything |
| Armies | Squads of real men on the land. A stack is a group of squads that march together (up to eight); any number may stand in a place. Drawn as points far out, sprites near (step 2), and stepped fully where they meet (step 3) | Squads of soldiers on the land, in formation, fully stepped where they meet, with orders to the squads themselves |
| Moving | The battle's gaits and speeds over the 44 m campaign grid (step 1), at the pace of the slowest army | The battle's own route planner, gaits and terrain speeds, refined on the 4 m grid as they go |
| Fights | Fought where the armies meet, always physically (step 3): the battle's own rules, on the whole island's ground. No prompt, no window, no dice | The same, with the computer lords in real time |
| Zoomed out | Every soldier a point in his side's colour, flashing while he fights; no flags over armies. Towns' banners and outposts still fly flags | Points in side colours; no flags |
| Overview | A minimap: every town and site, soldiers as points and heat in side colours, fights glowing, the camera's view; tap to go there. No fog yet | A minimap: every place, every soldier you can see, a heat map where they pile up |
| Map | 2.8 × 3.6 km (a third each way), all 80 towns and 40 sites kept, walls at three quarters (step 1) | The same |

## How long things take

Measured in the game on the map at a third (2.8 × 3.6 km, 44 m cells), with the campaign's
own march (`engine/sim/src/campaign_march.cpp`): the battle's speeds and gait table, roads
×2, hills climbed at their grade, mountains and water closed to walkers, and fliers alone
at the long-run pace of their flight. Nearest: from each town, the nearest other town or
site, averaged over the 80. Longest: the longest of all the town-to-town marches. In
minutes at 1× and in 2-minute days; the full table, every kind, is in GAME_DESIGN.md
§4.2.4.

| Unit | Pace | Nearest town or site | Longest town-to-town march (at 4×) |
|---|---|---|---|
| Heavy infantry | 1.9 m/s | 1.5 min (0.74 days) | 26 min, 12.9 days (6.5 min) |
| Light infantry | 2.4 m/s | 1.2 min (0.59 days) | 20 min, 10.2 days (5 min) |
| Light cavalry | 7.0 m/s | 0.4 min (0.21 days) | 7 min, 3.6 days (1.8 min) |
| Wolf riders | 7.5 m/s | 0.4 min (0.18 days) | 6 min, 3.2 days (1.6 min) |
| Pegasi | 4.71 m/s flying | 0.6 min (0.29 days) | 13 min, 6.6 days (3.3 min) |
| Griffins | 4.02 m/s flying | 0.7 min (0.34 days) | 15 min, 7.7 days (3.9 min) |
| Dragons | 4.88 m/s flying | 0.6 min (0.28 days) | 13 min, 6.3 days (3.2 min) |
| Hero | 2.8 m/s | 1.0 min (0.50 days) | 18 min, 8.8 days (4.4 min) |
| Catapults | 0.8 m/s | 3.7 min (1.84 days) | 64 min, 31.8 days (16 min) |

A stack of fliers alone goes straight over mountains and water at the long-run pace of its
flight (a take-off run, a flight as long as its endurance, then twice as long resting),
which nothing below changes: quicker than any foot, slower than horse. A stack with any
walker in it walks, and a hero walks with his stack. Every site can be reached on foot from
some town, the Seer's Tower from the two towns on its isle; only boats (6 m/s for now)
reach those two.

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
- **Roads double a walker's speed,** every gait alike, horse and wheel included (it was
  ×1.15 to ×1.35 by gait). With one set of rules this is the battle's gait table changing
  (UNITS.md), so set battles feel it too. Fliers in the air ignore it. Bridges stay at the
  pace of the fields: in battle a castle's walks and ramps are Bridge ground too, and men
  must not run along the walls, so a road bridge's deck is crossed at field pace. (On the
  campaign grid a road that crosses a river is road all the way.)
- **Roads should reach every town.** On the map before it shrank they covered 8% of the
  land and passed within two cells of only 35 of the 80 towns, so doubling them shortened
  the longest march by just 8% (84 to 77 min there). For the road to be the fast way, the continent has to
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
  and make the longest march in about 13 days (measured: 0.74 and 12.9; under movement
  points on the old map, 0.7 and 13.6). So the Warlords tables for gold
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

*Built in steps 2 and 3 (`engine/sim/src/campaign_world.cpp`, `game/src/campaign_soldiers.cpp`,
`game/render/renderer_map.cpp`): every squad is its real men, each with a place on the ground
(`Man`, in `Army::men`). The campaign's stacks stay as the groups that march together, and as
the unit the computer lords plan with; they are no longer a place where armies are fought.*

- **An army is a squad,** as in battle: its men, its formation, its facing, its orders. A
  stack becomes the player's group of squads (GAME_DESIGN.md §4.1.2): select them, give
  them a formation, send them.
- **No caps on a place.** Any number of stacks may stand in a cell and a town holds whatever
  fits inside its walls (the 8 to a cell and 32 to a city are gone, step 3). A stack is still
  a *group* of up to eight squads that march together, the unit the computer lords plan with
  and the player selects; splitting one into squads to give each its own order is step 4.
  Packing men tight is its own risk: a clump is what a dwarf's grenade is for.
- **Orders are the battle's orders, at any distance.** Tap a town across the island: the
  route planner finds the fastest way and they set off. Long routes are planned on the
  44 m campaign grid with the gaits' speeds, then refined on the 4 m grid over the next few hundred
  metres as the men go. (Planning a 3 km route on the 4 m grid in one go would be far too
  slow: a 100-man order across one battlefield already costs about 20 ms.)
- **Formations hold on the march.** A squad narrows into a column where its road narrows
  (bridges, gates, passes) and opens out again after.
- **One way to step a man (built).** There is no sleeping and no waking. Every man on the land is a unit of the
  one World from the moment he exists, and the World steps him every tick with the same navigation, steering,
  ramps, gates, formations, orders and fights as a skirmish, whether he marches, rests or fights. A stack is
  only the group that marches together and the unit the computer lords plan with: it looks where its men are,
  sends them on to the next waypoint of its route and meets whoever comes near, every half second
  (`Campaign::kThinkTicks`). The coarse route on the 44 m grid supplies waypoints; every step between them, round
  walls, through gates, up ramps, is the World's. The one exception is a man aboard a boat: he is out of the World
  while the ship carries him (the ship is not ground), and is put ashore as a unit where it lands.
- **What it costs, and the budget (measured, see "What it costs" below).** The World is not made cheaper for
  Conquest: a man costs what a man costs in a skirmish. If that is too much for the whole island at 4x or 8x,
  the answer is a decision about the game, not a second way of moving men.

## How the campaign holds the world (step 3)

A short note on how it works, and what it costs.

- **One ground.** The campaign owns one `sim::World` on the ground of the whole island:
  `Region::fromContinent` cut 900 cells (3.6 km) wide, with all 80 castles' walls, gates,
  ramps and walks, every tree and house (about 65,000 obstacles), and the navigation grid
  over it. It is made once per process, when a campaign starts (the page says so, a few
  seconds), so that no fight waits for it.
- **Every man is a unit of the World.** The World holds all the men of the island, 1,800 at the start of an
  all-computer campaign and more as lords train them (`World::nearby` looks men up through a grid by place, so the
  scans that make a battle cost what it does stay local). A man's place, wounds, kills, rank and morale are the
  unit's; the stack's `Man` records mirror them each tick for the computer lords, saves and the drawing.
- **Saving.** A save writes every man where he stands (`Man::ax, ay, fx, fy`) with his wounds. Nothing of a moment
  is kept (arrows in the air, cooldowns): `Campaign::save` first takes every man out of the World and puts him
  back (`reseatAll`), clearing the air, so that the game saved and the game loaded from the save carry on as one.
- **Gates open to their holder's men.** A shut gate swings open (three seconds) for the men of the side that holds it
  when they are bound through it (`World::updateGates`), and shuts behind them; everyone else hacks at it
  (`GateState::mode`). The holder's hand can hold it open or shut (`Command::SetGate`), and a garrisoned gate is kept
  by its garrison (`World::keepGates`: archers on the walls, foot in a horseshoe behind the doors). These are rules of
  the World, so a skirmish has them too (`tests/test_castle.cpp`, `tests/test_conquest_nav.cpp`).
- **Orders in a fight.** Stacks, the player's and the computer lords' alike, march their
  route with the battle's initiative (they fight whoever strikes them or crosses their path) and
  go on when it is done; standing, they meet whoever comes within 45 m of their post: foot go at
  the nearest foe, missile troops close to a good bowshot and shoot on their own
  (`Campaign::fightAi`, a small version of the battle AI's habits). A town's garrison holds its
  post. A stack held up for ten seconds with nothing struck goes deaf to the enemy for ten
  seconds (a force march) and, held up again, gives up the cell of its route the ground does not
  allow. A cell is reached when the middle of the men is at its anchor or most of them are about it. Gates are held
  by their town's owner (`World::setGateHolder`): everyone else hacks at them.
- **Towns fall by banner.** The banner (§4.2.8) rises while the men of one other side stand in the
  courtyard and none of the town's own or any third side's do: men are what is counted
  (`Campaign::armiesInTown`), in the courtyard, standing or fighting.
- **What it costs** (measured, Apple M4 Pro; the page in headless Chrome):
  - *Making the ground:* the island's `Region` takes 6.3 s natively (it took 124 s before the
    castle, deck and house loops were boxed) and its navigation grid 0.4 s (11 s before); the page
    makes it when a campaign starts and says so ("Laying out the island"), about 15 s in the browser,
    once. The 1 km windows of the set battles are as quick as they were.
  - *Every man a unit (step 3e, measured by `the_whole_population_in_one_world_costs_what_a_skirmish_man_costs`, the
    machine shared with other work, towns' garrisons packed 8 armies to a town for the larger two):*

    | men in the world | native, ms a tick | wasm (node), ms a tick |
    |---|---|---|
    | 1,819 (the start) | 1.5 (0.85 us a man) | 3.1 |
    | 3,355 (about day 40) | 6.0 | 13.4 |
    | 7,070 (about day 120) | 19.0 (2.7 us a man) | 41.4 |

    A tick is 33 ms at 1x. The budget of "an ordinary tick about 0.005 ms" no longer holds in any case (it was 3
    microseconds with the men asleep). The cost per man rose with the crowd because the tick looked at far more than
    was near a man: every man's gates, ruin floors and foes were looked for among all of them, the nearest-foe search
    of every standing stack was men x foes, and the soldiers' grids were hash tables of lists.

  - *After the tick was made to ask only what is near* (`illyria_tickbench`, which prints the same numbers and the
    world's checksum; the same ticks give the same checksum before and after, native and wasm, and every golden is
    unchanged). Measured on a quiet machine, original and new interleaved:

    | men in the world | native, ms a tick (was) | native, now | wasm (node), ms a tick (was) | wasm, now |
    |---|---|---|---|---|
    | 1,819 | 1.5 | 0.45 (0.25 us a man) | 3.0 | 0.95 |
    | 3,355 | 5.8 | 2.0 | 13.2 | 3.2 |
    | 7,070 | 18.3 | 6.5 (0.92 us a man) | 43.9 | 11.8 |

    So native fits the full population at 1x with room to spare and at 4x to the end, and the browser fits 7,000 men
    at 1x (and 2x on a fast machine). Nothing was made cheaper or skipped for Conquest, and nothing here is a second way of moving men: the same
    steps over the same men, asked of fewer of them. If it is too slow at 4x and beyond, or in the browser on a slow
    machine late in a game, that is a decision for the game (fewer, larger squads; a smaller island; a speed cap that
    follows the population), not a reason to put the men to sleep again.
  - *The second round* (same checksums, same goldens; the first round's code and the new, interleaved on a quiet
    machine; each cell is the mean over 200 ticks, and the quickest 20-tick batch by CPU time after the slash, because
    the mean includes the ticks just after 4,000 men were raised, when many plan routes):

    | men in the world | native, ms a tick (was) | native, now | wasm (node) (was) | wasm, now |
    |---|---|---|---|---|
    | 1,819 | 0.43 / 0.42 | 0.43 / 0.42 | 0.81 / 0.65 | 0.78 / 0.65 |
    | 3,355 | 1.91 / 1.49 | 1.54 / 1.18 | 3.17 / 2.25 | 2.44 / 1.96 |
    | 7,070 | 6.28 / 4.86 (0.89 us a man) | 4.79 / 3.90 (0.68 us a man; 0.55 best) | 11.4 / 7.9 | 7.5 / 6.2 |

    What was left to take out, by profile: the pair loop of `separateBodies` guessed which of 32 neighbours
    a man touches with a coin-toss branch and read each from a scattered array (now one run of numbers in the grid's
    order, one test; the visit order is unchanged, which is what keeps every push the same); every route filled two
    810,000-cell arrays before it began (`NavGrid::route` puts back the cells it touched); the square root
    (`flee` and `targetDown` take one per foe near) took six divisions and now takes one and a squaring from a table;
    `Terrain::heightAt` divided by the cell size, which is a shift. `walk` asks the height under a man once, not
    twice, and compares slopes without the root. A bucket of four cells to a lookup in the soldiers' grid; a compact
    box per deck for the first test of `Deck::contains`. In the page: the first-contact check of the voices tested
    every man of one side against every man of the other (12 million tests in a frame with 3,500 a side apart; now the
    cells round each man, `point_grid.h`, shared with the war drums), and `presentConquest` looks an army up when
    it changes, not for every man. Tried and not kept: `-flto` (2-3% in wasm, nothing native), `-O2` (the same as
    `-O3`), `-Os` (30% slower), `-msimd128` (no change; the step is integer scalar code).
    What is still hot (native, 7k men): the unit loop is about half of the tick, mostly `walk` (a step asks for the
    height, deck and slope at five or six places), `flee`'s root per foe and `nearestEnemy`; the pair loop is 14%
    and its neighbour lookups are most of that; the obstacle, gate and wall tests of `separateBodies` are 12%.
    The goal of 0.5 us a man at 7,000 men is not met (0.55 in the quickest batch, 0.68 on average); the next step
    is caching what a standing man's `walk` asks (his height and slope under him do not change while he stands), which
    is exact only with a stamp that every writer of a position bumps.
  - *An all-computer campaign* holds about 1,800 soldiers at the start and keeps small stacks
    besieging walls they cannot take for days; the computer lords no longer send a force a third the
    size of the men it would meet (`attackOdds`), a stack held up ten seconds goes deaf to what it
    cannot reach and gives up a cell of the route the ground does not allow.
- **What is not kept.** A save writes every man where he stands and the gates' wounds, and
  nothing of a fight in the middle (arrows in the air, cooldowns, morale in the moment): saving
  settles the fight, and the game carries on from there (a save is exact between fights).
  Fights the computer lords plan are still reckoned with Warlords' odds (`Campaign::winChance`,
  `attackOdds`): that is an *estimate* of whether to attack, never what happens.

## Fights

*Built in step 3, as described above.*

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
  plan. The numbers on a town's walls are that estimate's too (Warlords' defence), not what
  the real walls do: the gate's hit points are the region's own, and the old *Raise the
  walls* purchase is gone, since it would have changed nothing a man can see.
- **Heroes at ruins** fight the guardian in the ruin itself.

## Queued orders

*Built after the one world, for the one world: a man holds a queue of orders (`Unit::queue`), Skirmish and Conquest alike.*

- **A man's own state, in the sim.** `sim::Command::queue` ("add after the order in hand") is carried by the one command
  stream, so a queue is lockstep like any order. In `World::applyCommand`: a queued command is appended when the man
  has an order in hand or a queue already; a man with neither begins it at once. Any other command (a plain
  right-click, a Stop, the computer's) clears the queue before it runs.
- **When the next begins.** `World::advanceQueue`, once a tick before the man is stepped: when his order is over
  (arrived; shut out by a gate; a quarry dead or let go; a raid ended), the next begins within that tick, and one that
  turns out impossible (a quarry already dead, a target he cannot strike) is passed over in the same tick, so a queue
  never stalls on one. A hunt whose quarry falls, with orders waiting, does not choose another foe (`targetDown` is
  skipped): the next order is what he does. A standing attack (missile troops on the ground, a wall, a house or a tree)
  never ends of itself, so with a queue behind it it ends after ten seconds, or when its gate is broken.
- **Bounded.** At most `World::kMaxQueuedOrders` (8) wait behind a man's own: every man carries, hashes and saves
  his queue, and a long script is a long line of numbers on the screen. The ninth is dropped.
- **Hashed only when non-empty**, so no battle without a queue changed its checksum (no golden moved).
- **Saved.** Campaign saves are `ILC7.`: after each man, the orders waiting behind his own. The order he was walking is
  a moment and, like arrows in the air, is not kept (`reseatAll` puts every man back standing, in the live game too),
  so the first of his queue is what he does next. An attack on a soldier names a unit of the world, whose ids are made
  anew when the men are put back, so it is not kept. A man aboard a boat keeps his queue on the `Man`.
- **Not in Conquest: a march beyond the men's reach.** That is a *stack's* march on the coarse grid, the campaign's own
  order and not a man's; it has no queue. With Shift it replaces, and says so.
- **Fog-safe.** The queue is drawn (`Battle::queuedRoutes`) for the player's own men only, and a leg after a quarry the
  player cannot see is not drawn; nothing about another side's queue leaves the sim.

## One battle system, one renderer

There is one game. A fight on the campaign is the Battle's fight, shown by the Battle's own presentation, drawn by
the one renderer pass over the men. Nothing the battle has is copied for the campaign: it is the same code.

- **The field presents the conquest.** `Battle::borrow(Campaign*)` makes a Battle present the conquest's world as
  it presents its own. The conquest's field (`app->battle`) is a Battle made on the island's own origin (the
  field's 1 km window sits at the island's south-west corner), so the world's places are the battle's places and
  nothing is translated. The conquest tells it round every step of its world (`Campaign::onWorldStep`:
  `beforeWorldStep`, `afterWorldStep`) and when a fight is cleared away (`forgetMen`); `Battle::step` is the same
  two calls round its own world's step. Everything after that is the battle's: `collectEvents` (blood, dust,
  sparks, splinters off gates, craters, fire), `presentDeaths` and the sounds (`Sfx::observe`), the shouts
  (`Voice::observe`), the per-man ticks that drive flinches, blows and death frames, the remains and gibs, the
  stuck arrows, the health bars, the gates drawn broken or battered (from the presented world's own region,
  `Battle::worldRegion`), and the tap on a gate that shows its hit points (`Battle::gateAt`).
  `tests/test_campaign_soldiers.cpp` (`the_battle_presents_the_campaigns_fight_as_its_own`) runs a conquest fight
  with no code of the conquest's in the way and finds the battle's blood, deaths, sounds and shouts in it.
- **The men are drawn once.** `Renderer::drawMan` draws every man of every battle at every distance: a dot in his
  side's colour from far off (pulsing, rapidly and at random, while he fights; a town's own garrison hollow), the
  battle's sprite near, the two cross-fading between 380 m and 170 m; his shadow, ring, frame, picking box and
  place in the sheet's group are the same code. Every man is a unit of the world, and goes through that one function.
  One side-colour scheme serves every battle (`factions.h` `sideFactions`: a skirmish's seats are the Moonward blue,
  the Ironhold red, gold and green; a conquest's seats play their lords).
- **Looking.** `Battle::manAt` picks the man under a finger, at any distance;
  `Battle::gateAt` the gate; `Battle::inspect` says who is looked at (a man not the player's, or a
  gate: grey, view only, one card for a skirmish's foe and a conquest's alike, `Battle::glance`).
- **Commanding: one selection, one command bar, one order path.** (`game/src/battle_conquest.cpp`; tests in
  `tests/test_campaign_battle.cpp`.) Each frame `Battle::presentConquest` makes the Battle's squads from the armies
  of the stacks (a squad is an army: its men are its members, and the card's numbers are the army's own). The selection is of men,
  by their ids, so that it holds through a man's boarding a boat and landing, and is made again each frame into the
  world's units. A tap, a box, a press with its facing line, the cards, Charge, Hold, Find, a
  formation, As they stand and Scatter are the battle's own functions (`Battle::tap`, `pressEnd`,
  `selectSquad`, `chargeAll`...). Only where an order *goes* differs, and it is decided in
  one place (`Battle::marchOrder`): an order for men within `Battle::kLocalReachM` (200 m) is the
  world's (commands to units, which `Battle::flushOrders` submits as `Order::Command`: the conquest applies them
  at the start of the step it falls in and the stack's men stand to them: `Stack::commanded`, which stops
  `steer` and `fightAi`), and an order further off is a march of their squads (`Order::Move`, naming the squads
  when they are only some of a stack): the coarse route's cells are waypoints for the world's own steps.
- **Sight.** There is one seam, `Campaign::canSee(viewer, owner, x, y)` (everything is seen until fog is built in
  step 4); the viewer is a parameter (`Campaign::localSide()`), never a literal. Everything shown of another side goes
  through it: the men drawn, picked and looked at (`visibleSoldiers`, `Battle::sees`, which asks the conquest the
  Battle presents), the stacks listed, the minimap (`game/src/minimap.cpp`: its towns, men, heaps and fights), the
  towns' holders as the map and the minimap colour them, the banner going up over a town and the news of a town
  changing hands (`Campaign::seesCity`, `knownOwner`), and the fight the Fight! button takes the camera to
  (`Battle::hotSpot`, from the blows the viewer may see). Always visible by decision, until step 4 chooses: the
  land's colour and the outposts (the hexes' holders are map knowledge, as the map's roads and towns are), and a
  side's own news lines (a hero's deed is its own side's). `tests/test_campaign_battle.cpp` hides sides through the
  seam and finds none of them on the minimap, under a finger, in the hot spot or as a town's holder, and measures what
  the seam costs (about 15 ns a man in `visibleSoldiers` with a stub, 1.5 ns a call to `canSee`).
- **One record for a man.** A man is a `sim::Unit` in the world, and is presented as a `Campaign::Soldier` (his place,
  his stride, his side and kind, and his unit): the renderer, the picking and the minimap read that. There is no
  third record (the renderer's `ManView` is the arguments of one draw call).
- **Player intent.** The page submits `Campaign::Order`s (stamped, ordered, serializable); they are applied inside
  `step()`. `tests/test_campaign_save.cpp` replays the same Orders over the same save for the same checksum.
- **Gone.** `MapSoldier`, `collectMapSoldiers`, `Renderer::setMapSoldiers`, `placeMapSoldiers`, `pushMapSprites`,
  `pushMapWounds`, `drawMapDots`, `setMapArrows`, the campaign's own mirroring of gates and its effects, its own
  picking by dots and by gates, the separate sleeper drawing loop, and the sleeping and waking of stacks with the body-stepped march.

**What still differs, and why.** The conquest's clock and its speeds (1, 2, 4 and 8) are the conquest's own, not the
battle's (1, 2, 4 and slow motion): the page keeps both (`illyria_campaign_pause`, `illyria_toggle_pause`). Saved groups (keys 1 to 9) are of men: in a skirmish their unit ids, in a conquest their man ids (a unit is a place in the world that a man leaves when he boards a boat), found again among the men each time.
The renderer's one fork on whether the world is the conquest's is that a skirmish's field bakes sprite sheets for
the kinds of soldier it has at once, and a conquest's only for those near the camera (`Renderer::draw`). The town
screens (what a town trains, its boats, where its new armies go) are the conquest's own, and so is the tap that
opens them (`townTap` in the page): everything else is the battle's.
