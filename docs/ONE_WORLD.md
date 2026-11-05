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
- **Two ways to step a squad, one look (built).** Away from any enemy (beyond a long bowshot,
  about 150 m) a squad on the march, or a garrison at rest, is stepped as one body: its
  route, its pace, its formation, and every man placed in his slot on the ground. Towns stand closer
  than that to one another, so an enemy at rest behind its own walls doesn't count. When an
  enemy comes within that distance, every man is stepped on his own, exactly as in a battle
  today, until nothing hostile has been near for ten seconds. (The doc's first idea, "or a man is
  hurt", is not what was built: a wounded man in a squad at peace needs no stepping.) Which way a
  squad is stepped depends only on the world, never on the camera, so every machine and every zoom
  sees the same thing. Drawn, the two are the same: every man in his place, walking.
- **Why it has to be:** an all-computer campaign under today's rules holds about 1,800
  soldiers at the start (1,650 of them in neutral town garrisons), 3,800 by day 40 and
  6,800 by day 120. A 450-soldier fight costs about 0.8 ms a tick natively (§5.2), slower
  in the browser. Stepping all 6,800 men in full would take about 12 ms every tick, over a
  third of a core at 1×, and impossible at 4×.

## How the campaign holds the world (step 3)

A short note on how it works, and what it costs.

- **One ground.** The campaign owns one `sim::World` on the ground of the whole island:
  `Region::fromContinent` cut 900 cells (3.6 km) wide, with all 80 castles' walls, gates,
  ramps and walks, every tree and house (about 65,000 obstacles), and the navigation grid
  over it. It is made once per process, when a campaign starts (the page says so, a few
  seconds), so that no fight waits for it.
- **Men are in the world only while they fight.** The world holds the men who are *awake*,
  not all 6,000: the scans that make a battle cost what it does (the nearest enemy, the morale
  census, who is in a man's way) are looked up through a grid by place, and a world of
  thousands would otherwise cost thousands squared (`World::nearby`, which finds exactly what
  the old loops found). Everyone else is *asleep*: a stack is one body, marching its route on
  the 44 m campaign grid at its pace (`moveStacks`, a handful of integer operations), and every
  man stands in his slot of its formation (a column on the march, a block standing), or is
  walking back to it from where a fight left him. Every man has a real place at all times;
  `Campaign::soldiers` says where.
- **When a stack wakes.** Every half second (`kContactTicks`) the campaign looks at the stacks'
  bodies. Two stacks of different sides within 150 m of each other (their edges, a long
  bowshot) wake both, if either is on the move or fighting (an awake stack that has not struck or been struck
  for five seconds is neither). An enemy *at rest behind
  his own walls* does not count (towns stand closer than that to one another): only one that
  comes up to the walls does. Which stacks wake depends only on where everyone stands, never
  on the camera.
- **When it sleeps.** After `kCalmTicks` (10 s) with nothing hostile within reach, each man
  where the fight left him, with his wounds, kills, rank and morale (`Man`), eased back to his
  slot at walking pace. The dead lie 20 s and are taken away; the world is cleared of arrows and
  remains when the last stack sleeps, so that it is a function of the gates' wounds alone
  between fights (and the same on every machine, whether or not it has seen one).
- **Orders in a fight.** Awake stacks, the player's and the computer lords' alike, march their
  route with the battle's initiative (they fight whoever strikes them or crosses their path) and
  go on when it is done; standing, they meet whoever comes within 45 m of their post: foot go at
  the nearest foe, missile troops close to a good bowshot and shoot on their own
  (`Campaign::fightAi`, a small version of the battle AI's habits). A town's garrison holds its
  post. A stack held up for ten seconds with nothing struck goes deaf to the enemy for ten
  seconds (a force march) and, held up again, gives up the cell of its route the ground does not
  allow. Gates are held by their town's owner (`World::setGateHolder`): everyone else hacks at them.
- **Towns fall by banner.** The banner (§4.2.8) rises while the men of one other side stand in the
  courtyard and none of the town's own or any third side's do: men are what is counted
  (`Campaign::armiesInTown`), in the courtyard whether asleep there or awake fighting.
- **What it costs** (measured, Apple M4 Pro; the page in headless Chrome):
  - *Making the ground:* the island's `Region` takes 6.3 s natively (it took 124 s before the
    castle, deck and house loops were boxed) and its navigation grid 0.4 s (11 s before); the page
    makes it when a campaign starts and says so ("Laying out the island"), about 15 s in the browser,
    once. The 1 km windows of the set battles are as quick as they were.
  - *Asleep:* the ordinary tick is about 3 microseconds natively (0.1 us, and a 43 us look for
    contact every 15th tick) and 0.006 ms in the browser, against 0.005 ms before. The dawn planning
    spike, the computer lords' daily plan, is unchanged (28 to 33 ms natively on day 1).
  - *Awake:* a man costs about 2 to 4 microseconds a tick natively (5 in the browser), so
    cost follows the men in fights, not the soldiers on the island. Four fights at once of 360 men
    each (1,440 to 1,536 awake): native mean 5.9 ms a tick, median 4.9, 99th percentile 13, worst 38;
    browser mean 8.0 ms, median 5.8, 99th percentile 31. A tick is 33 ms at 1x, so that fits at 1x
    and does not at 4x or 8x: the page runs as many ticks a frame as the clock owes within 14 ms of
    it and lets the game fall behind the clock after that. The speed control does not say so yet
    (step 4). The world is not capped: a cap would have to leave men in a fight asleep, passing
    through each other.
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
  place in the sheet's group are the same code. The world's units and the sleepers (the conquest's soldiers asleep
  in their squads on the march, who are not units of any world: a `Campaign::Soldier` with no unit, made from the slot
  the campaign says they stand in) go through that one function; they differ only in whether a sim unit is to hand.
  One side-colour scheme serves every battle (`factions.h` `sideFactions`: a skirmish's seats are the Moonward blue,
  the Ironhold red, gold and green; a conquest's seats play their lords).
- **Looking.** `Battle::manAt` picks the man under a finger, a world's or a sleeper's, at any distance;
  `Battle::gateAt` the gate; `Battle::inspect` says who is looked at (a man not the player's, awake or asleep, or a
  gate: grey, view only, one card for a skirmish's foe and a conquest's alike, `Battle::glance`).
- **Commanding: one selection, one command bar, one order path.** (`game/src/battle_conquest.cpp`; tests in
  `tests/test_campaign_battle.cpp`.) Each frame `Battle::presentConquest` makes the Battle's squads from the armies
  of the stacks (a squad is an army: its men awake in the world are its members, those asleep are its `sleeping`
  men, and the card's numbers are the army's own) and its sleepers from the men asleep. The selection is of men,
  by their ids, so that it holds through a man's waking and sleeping, and is made again each frame into the
  world's units and the sleepers. A tap, a box, a press with its facing line, the cards, Charge, Hold, Find, a
  formation, As they stand and Scatter are the battle's own functions (`Battle::tap`, `pressEnd`,
  `selectSquad`, `chargeAll`...), for men awake and asleep. Only where an order *goes* differs, and it is decided in
  one place (`Battle::marchOrder`): an order for men awake and within `Battle::kLocalReachM` (200 m) is the
  world's (commands to units, which `Battle::flushOrders` submits as `Order::Command`: the conquest applies them
  at the start of the step it falls in and the stack's men stand to them: `Stack::commanded`, which stops
  `steer` and `fightAi`), and an order for men asleep or further off is a march of their squads
  (`Order::Move`, naming the squads when they are only some of a stack; `Order::Halt` for Hold). Neither selecting nor ordering men asleep wakes them (what wakes a stack is
  what always did, an enemy come near and a march); what a sleeping man cannot do (a formation, Scatter) is kept as
  the squad's choice for when he is on his feet.
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
- **One record for a man.** A man is a `sim::Unit` in the world while he is awake, and is presented as a
  `Campaign::Soldier` always (his place, his stride, his side and kind; the unit to hand when there is one): the
  renderer, the picking and the minimap read that, and the sleepers are the Soldiers with no unit. There is no
  third record (the renderer's `ManView` is the arguments of one draw call, made from either).
- **Player intent.** The page submits `Campaign::Order`s (stamped, ordered, serializable); they are applied inside
  `step()`. `tests/test_campaign_save.cpp` replays the same Orders over the same save for the same checksum.
- **Gone.** `MapSoldier`, `collectMapSoldiers`, `Renderer::setMapSoldiers`, `placeMapSoldiers`, `pushMapSprites`,
  `pushMapWounds`, `drawMapDots`, `setMapArrows`, the campaign's own mirroring of gates and its effects, its own
  picking by dots and by gates, and the separate sleeper drawing loop.

**What still differs, and why.** The conquest's clock and its speeds (1, 2, 4 and 8) are the conquest's own, not the
battle's (1, 2, 4 and slow motion): the page keeps both (`illyria_campaign_pause`, `illyria_toggle_pause`). A squad
of men asleep cannot be ordered a formation or to scatter (it has no bodies to), nor a facing on arrival; the
squad's march is the stack's grid march. Saved groups (keys 1 to 9) are of men: in a skirmish their unit ids, in a conquest their man ids (a unit is a place in the world that men come to and leave as stacks wake and sleep), found again among the men awake and asleep each time.
The renderer's one fork on whether the world is the conquest's is that a skirmish's field bakes sprite sheets for
the kinds of soldier it has at once, and a conquest's only for those near the camera (`Renderer::draw`). The town
screens (what a town trains, its boats, where its new armies go) are the conquest's own, and so is the tap that
opens them (`townTap` in the page): everything else is the battle's.
