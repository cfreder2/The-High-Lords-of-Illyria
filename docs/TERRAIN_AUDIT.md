# Terrain audit: roads to the castles, towers on their walls

`illyria_terrainaudit [seed] [-v] [--map out.ppm]` walks the generated continent and prints what is wrong with its
castles and the roads to them; `tests/test_terrain_audit.cpp` fails if any of it comes back. The measuring is
`tools/terrain_audit.h` (doubles: it reports and never feeds the sim). The game uses one continent seed
(`Battle::kRegionSeed` = 20261027) for Skirmish and Conquest alike, so that is the one audited; Skirmish's castle is the
Conquest town's, from the same `Continent::planCastles` through `Region::fromContinent`.

## What it found (before)

| | |
|---|---|
| castles (8 capitals, 72 towns) | 80 |
| gates with a road reaching their mouth | **0** of 80 |
| castles joined to the road network | **0** of 80 (the design has 6 roads; the nearest town was 19 m from one, the farthest 632 m) |
| gate facing water (a pool 4 to 10 m out) | 1 (Southmere) |
| roads through a castle's walls away from its gate | 2 (Northgate, Rowanbury: placed "astride" a road) |
| castles cut off from every other road by water | 3 land castles (Sylvaris, Grathmaw and a town: the Sorrow's mouth and a long pool) and 2 on the Isle of the Seer |
| towers per castle | 6 (40 castles), 7 (26), 8 (6), 16 (8 capitals): 598 towers |
| towers within 2 m of each other | 4 castles (a corner tower against a gate tower on a short gate wall) |
| embrasures that can shoot at the ground 10 m before the gate (within bow range, 60 degrees of straight out, no wall or tower between) | 2 for 18 castles, 3 for 4, 4 for 58 |
| roads through water that is no bridge or ford | roads 0, 2, 3 and 5 (the design's): 144, 240, 112 and 420 m; road 5 runs along the Westrun's own bed |

Why: the design's roads are hand-placed way points; the towns are placed on a grid near them but nothing joined a town
to a road: `planCastles` only turned the gate toward the nearest one. Towers: one on every corner of every ring, plus two
at each gate; a capital's 8-gon outer wall and square bailey gave 16.

## What was done

- **Roads to every gate** (`engine/sim/src/continent_roads.cpp`, one generator, no second road system): after the towns
  are placed, a cheapest-path search over the land (a 16 m grid, integer costs: dearer over slope, forest, swamp; shut to
  rock, the sea, lakes, broad rivers and other towns' walls; creeks, narrow rivers and pools crossable at a high price)
  takes each town, nearest the roads first, from its middle to the road network or to the road an earlier town was joined
  by. The path decides which way the gate faces; once the castle stands, the road is laid from the gate's mouth along it as
  one more entry of `Continent::roads_`, smoothed like the design's, so everything that already knows the roads (the
  ground, causeways, what is placed clear of them) treats it like the rest. Creek stretches get the existing bridge
  placement (`placeBridges`, now taking where to start); stretches through a narrow river or a pool get a bridge over them.
  The design's roads that did not meet (the Sorrow's, the Westrun's) are joined to the main network by three links.
  The two towns on the Isle of the Seer have no way off it: each gets 40 m of road at its gate.
- **No town astride a road** (`placeCities`): the "astride" allowance, which put two towns' walls across a road, is gone;
  every gate has a road and a road has only one gate to lead to.
- **Design roads out of the water** (`keepRoadsDry`): their way points in water, away from the fords and bridges, move to
  the nearest bank. Road 5 from 420 m of water to 112 m.
- **Towers by a rule** (`planCastles`, `Castle::Ring::towered`): a pair flanking each gate always; a corner tower where the
  wall turns by 65 degrees or more, and at about every `kTowerSpacing` (80 m) of wall between; never against a gate tower
  (`galleries()`, `crenels()` and `way()` go round the towers there are: the walk goes past a bare corner). A capital's
  16 towers are 11, a town's 6 to 8 are 4 to 6; none within 3 m of another.

## After

| | before | after |
|---|---|---|
| gates with a road to their mouth | 0 / 80 | 80 / 80 |
| castles joined to one road network | 0 / 80 | 78 / 80 (the isle's two, which no road can reach) |
| gate mouth in water | 1 | 0 |
| road through a castle's walls | 2 | 0 |
| towers (castle total, capital / town) | 598 (16 / 6 to 8) | 485 (11 / 4 to 6) |
| castles with towers crowded within 2 m | 4 | 0 |
| roads laid | 0 | 80 (11 912 m, the longest 545 m), 3 links, 11 bridges over creeks, rivers and pools |
| design roads in water (m) | 916 | about 480 (pools baked into the relief) |

## Left

- Four design roads still cross baked drainage pools and the Heartmere's shore (road 0 near (1323, 1880), road 2 near
  (943, 3225), road 3 near (2042, 921), road 5 near (701, 1609)): fixing them means re-routing or re-baking, which is the
  terrain bake's (`tools/terrainbake`), not a mechanical change.
- **The archers' view of the gate** is not fixed here: two to four embrasures see the ground in front of a gate, none
  from over it. The reshape is proposed in docs/CASTLE_GATEHOUSE.md.


## Road look (classes, surface, puddles, rocks, bridges)

The roads were the generator's and the shader's: twin ruts and a thin outline down every road, little loops and spurs at junctions,
one width for all. What replaced them (`engine/sim/src/continent_roads.cpp`, `game/render/shaders.h`, `game/road_field.h`):

- **Junctions are clean.** A road laid from a gate is cut where it first touches a road of another network (within 4.5 m), or
  where it comes nearest one (within 64 m), and its end moved onto that road (`cutAtJoin`); before, it was snapped from its
  last point, so the path's wandering and the spline's overshoot left a spur or a loop beside the junction. A link between two
  networks is laid only if its ends are in different ones (roads that meet are one network). A design road's free end that runs on past a junction for
  under 100 m stops at the junction; points where a road turns right back on itself (more than 100 degrees) are dropped.
  `tools/terrain_audit.h roadFlaws` finds stubs (a free end with under 100 m of road to a junction) and loops (a cycle under 150 m);
  `a_junction_leaves_no_dead_end_stub_and_no_little_loop` holds them at zero on the game's continent. Before: 3 stubs (14, 25 and
  61 m) and loops of 63 m (the triangle in the screenshot), 11 m and 16 m. After: 0 and 0. (Other seeds: a few short loops where two
  paths part and meet again, kept to a handful by the test.)
- **Road classes from traffic** (`Continent::shapeRoads`). Every pair of castles takes the cheapest way between their gates over the
  road network (a graph of the roads' points every 6 m, joined where two roads' points are within 7 m); each stretch of road counts
  the journeys over it (a capital counts four, a town one; `Continent::roadTraffic`). Lanes are the logarithm of that between 800
  journeys (one lane) and 5000 (four), and a lane is 4.4, 6.6, 10 and 14 m across at 1, 2, 3 and 4 lanes (half widths 2.2, 3.3, 5, 7 m, a
  trunk road being 3 to 4 lanes). The width is continuous: the lanes are smoothed along the road (24 passes of a 1-2-1 filter on 6 m
  points), a slow seeded wobble of 20 to 50 cm each side is added (`noise::fbm` of the arc length, wavelength 70 m), it is never under 2 m
  a side, never over 2.2 m a side beside a town's walls (it narrows to the gate, one metre of half width to each 8 of road), and it
  changes no faster than 0.1 m a side to each metre (the audit's steepest is 0.125). On the game's continent: 16.6 km single lane (69%),
  2.6 km main (11%), 4.8 km trunk (20%); 6 of the 89 roads have a trunk stretch; the busiest point carries 4914 journeys.
- **One width.** `Polyline::widths` is the only width: `Continent::Sample::roadDist` (and so `landGround`'s Road, and the roads'
  doubling of a walker's pace), `Region::fromContinent`'s ground and road beds (`roadDistance`, the same formula), the close-up
  land's and the battlefield's road field (`road_field.h`: the distance from the middle line and the half width, so the drawn road is as wide
  as the ground that is Road) and the far map's ribbons all read it. A battle window's Road ground now spans the real width, not 6 m.
  The campaign's grid still has a road cell wherever a road crosses it.
- **Surface** (`ILLYRIA_TERRAIN_FS`): packed dirt and gravel in two tones with pale specks, a shade darker down the middle (no ruts, no centre
  line: on a 14 m road it did not read), and a ragged edge where the grass creeps in (the edge is displaced by two octaves of noise and
  blended into the grass colour), at every zoom; from afar the ribbons are never thinner than a lane. The thin outline of the old picture was the road field's
  edge: its linear filtering between the field's last texel and its empty one drew a hump and a rut there; the field now fades to "no
  road" (distance 255) instead.
- **Puddles** (visual only; nothing blocks movement): one chance in each 13 m cell, an ellipse 0.7 to 2.2 m long, only where the cell's middle is on
  the road, likelier low down, in swampy ground and on wide roads (8% + 30% low + 25% wet + 3% a metre of half width), never on a
  slope; a darker, wet earth round it, the sky's colour in it (Fresnel) and the sun's glint.
- **Rocks and boulders** (`Region::roadClears`, `Region::worthDodging`, one rule for `Region::scatterScenery`, `DetailLand::growScenery`
  and the road generator): a boulder or rock on a road's line is gone; one beside it stays; trees and bushes keep clear of the road's
  width and 2.5 m (4.5 m at least). The road bends round the stones worth the trouble (a boulder, or a rock 0.9 m across or more, found by
  `sceneryAt` on the ground without the road) by a smooth bump of up to the stone's size and its 0.6 m margin over 28 m or more, not near a junction,
  bridge or town and never into water. Test `no_rock_stands_on_a_road_and_roads_go_round_the_big_ones`: over three seeds no rock stands on a road's
  middle line in the windows tested, and 18 stones on the game's continent have a road's edge within a metre. (A stone the road cannot go
  round is just cleared from it.)
- **Bridges** (`Continent::placeBridges`, `spanCrossing`, `Crossing`, `Deck`): a bridge spans the water measured along its axis to dry banks (five dry
  metres in a row, ground a hand's breadth above the water counted as water, then ten metres more so the ramp starts well inland of the shore the battle's
  finer land draws), never more than 100 m a side. It is centred on that span, level over the water and clear of it, with a ramp at each end grading
  down to the ground at 16% (`rampA`, `rampB`, `endA`, `endB`; the mesh draws a causeway between retaining walls), and as wide as the road there.
  Roads are fitted to it (`shapeRoads`): the road that crosses is drawn onto the axis over the deck and 32 m beyond each end, the pull fading
  out over 32 m more, so the way on is a straight line along the deck's width and not a turn at its foot (a junction on the pulled road is kept on
  it, and a road laid from a gate stays at its gate). A bridge the road no longer reaches is moved onto the road where it is in water (the design's were
  set before the roads were drawn); one with no road 25 m beyond an end, or a castle's wall over it, is a ford.
  Water too wide for a bridge (no bank within 100 m: the Westrun's mouth, the estuary of the Sorrow) is a **ford** the road wades: Old Bridge is one
  now, the broad water Southbridge crossed (which no road reaches, and lies in a lake) is gone, and the bridge at the Westrun's mouth by (344, 3240) is a
  ford. `NavGrid` pins a point every 10 m along a deck on a route (a long deck is not left to the cells' middles), and `World::followPath` reaches a deck's
  end instead of cutting the corner to the next point (a footbridge is 1.8 m wide).
  `tools/bridge_audit.h` (`illyria_terrainaudit --bridges`) walks a man, a rider and an engine across each bridge, and across each ford a road
  runs through, in a World with the real Region, NavGrid and walking rules, each way: three orders as a player would give them (to the deck's
  near end, along it, on 40 m beyond; 16 m out on its axis, further if that is water or marsh). Before (sandbox, the same audit): 9 of 18 bridges
  (decks up to 8.4 m above the ground at their ends) and 5 of 5 fords. After: 9 of 9 bridges, ends within 1.5 m of the ground, and 7 of 7 fords on a
  road. `every_bridge_meets_the_ground_and_can_be_walked_across_and_so_can_every_ford` asserts all of them on three continents (the game's, 1 and 6): 25
  bridges and 30 fords. Seeds 7, 99 and others still have a crossing or two that fails, a design ford in deep water or a road that turns hard at a
  footbridge's end (up to three of ten crossings): the game's continent is clean.
  The outpost's pole stands at `Crossing::bridgehead()`, dry ground beside the road at a bridge's head, or on the nearest dry ground to a ford
  (the campaign's own position of the outpost is unchanged).

## Water: rivers below their banks, lakes and islands that are not ovals

`illyria_terrainaudit` also prints a section for every river and for every lake and island (`tools/water_audit.h`,
`tools/shape_audit.h`; the tests in `tests/test_terrain_audit.cpp` hold the numbers on three seeds).

### Rivers: walls and curtains of water (before)

A river's surface was the ground at its centreline less 1.5 m, held to the lowest of the course so far; the banks were
whatever the land did beside it. Where the land fell away across the course the water stood above the bank and ended at
the cutoff (half width + 6 m) in a cliff of water: the "wall of water" with a vertically streaked side. Where the land
fell along the course the surface fell with it, 12 m between two vertices 20 m apart: the "curtain" beside the bridge.
Two more things cut a bank below the water: a town's pad (levelled to the town, not to the river) and the drainage
bake's creeks running along a river. The Coldbrook ran from the northern shore *up* into the Aldwater, 21 m uphill.

| rivers, game seed (a section every vertex, 1 m steps across) | before | after |
|---|---|---|
| sections ending in a cliff of water (water over 0.75 m deep at its edge) | 48 of 292 | 6 of 265 |
| tallest such wall | 69.5 m | 3.1 m |
| course steps falling faster than 1 in 9 | 12 | 0 |
| steepest fall / largest step between two vertices | 1 in 1.5 / 12.3 m | 1 in 10 / 2.6 m |
| the four sea mouths: largest step in the last 100 m | 12.1, 16.7, 13.1, 17.9 m (1 in 1.3 to 1 in 2.6) | 0.1, 1.0, 1.4, 2.6 m (1 in 10 to 1 in 125) |
| surface at the mouth / half width at the mouth over the middle of the river | 0.1 m / about 1x | 0 (the sea's) / 1.7x to 3.9x |

What was done (`Continent::computeRiverSurfaces`, `carve`):

- **The surface follows the course's ground**: the ground along the middle less 1.5 m (as before), but no lower than the
  lowest bank of the section (less 0.35 m) unless that cuts a gorge deeper than 3 m below the middle; the lowest of the
  course so far (it never climbs downstream); no steeper in its fall than 1 in 10 (the surface upstream of a drop is
  lowered: a gorge, which the valley already blends into the land, not a curtain); the level of the river it flows into at
  a junction; zero (the sea's level) at a mouth, which opens out over its last 140 m (`kMouthFlare`).
- **A bank lower than the water is built up** (the levee, the end of `carve`): to the surface plus 0.35 m at the water's
  edge, tapering off into the land at 1 in 4 however far that takes, so there is no cliff where it ends (a first version
  that ended after 24 m left a wall of ground a metre or two high, and soldiers stood below it).
- **A river rises where the land is above where it ends, clear of the shore**: the head of a course below that is dropped
  (the Aldwater rose on the beach). The Coldbrook is gone (it could not run down to the sea in the room it had).
- **Nothing else cuts a bank below the water**: a creek's gully and a town's pad fade out over 24 m from a river's edge
  (`riverGround` in `carve`), so that the river's own ground stands there.
- **The same sim feeds both paths**: the battle's Region (`Region::fromContinent`) and the Conquest's close-up land
  (`DetailLand`) both take water from `waterSurfaceOf` on `sample`, so they cannot disagree about the surface; the
  far map's ribbons are drawn at the same vertices.

### Lakes and islands: circles and straight causeways (before)

Lakes were ellipses with a gentle fbm on the radius (0.8 to 1.2), islands and the Spit plain ellipses (the Spit 330 x 16,
straight), the Isle of the Seer a capsule. The measure (`shape_audit.h`): rasterise the body from the sim's own samples,
compare it with the ellipse of the same area, centre and second moments (deviation = the area where they differ over
the body's area: an oval scores 0), plus solidity (area over convex hull) and the spread of the radius.

| body | deviation before | after | solidity before | after |
|---|---|---|---|---|
| Mirrormere | 0.064 | 0.253 | 1.000 | 0.872 |
| Silverlake | 0.047 | 0.201 | 0.994 | 0.888 |
| Longmere | 0.060 | 0.352 | 0.964 | 0.713 |
| Heartmere | 0.201 | 0.231 | 0.873 | 0.845 |
| Abbey Isle | 0.105 | 0.200 | 0.974 | 0.881 |
| the Spit | 0.118 | 0.993 | 0.933 | 0.460 |
| Isle of the Seer | 0.040 | 0.209 | 1.000 | 0.866 |

- `shoreDistance`: the ellipse domain-warped at two scales (lobes and headlands over the whole shape, bays and points a
  third of it), faded in from the middle so what stands at an isle's middle (the abbey) still does. Lakes, isles, the
  Heartmere's rocks and the Seer's isle (its capsule is warped in `landDistance`) all use it.
- The Spit (`Isle::wander`): its middle line swings up to 38 m sideways, its width swells and thins between 6 and 16 m
  either side of the line, and it fans out where it meets the isle and the shore; `isleWalk` marks every dry cell of it
  for the campaign's grid. Three rocks and shoals stand about Abbey Isle (`Isle::walked` false: no way on foot).
- Lakes shelve from the waterline (no step) to a deep middle as deep as a river (5 m and a thirtieth of the reach, up to
  14 m), so the one water-colour function, `inlandWater(depth)` in `kCommon`, gives a lake and a river the same blue:
  the battlefield's water, the far land and the far river ribbons all use it.
- The mud at the water's margin (`muddyShore`, shared by Region and DetailLand) follows the shore with a ragged reach,
  not a 5 x 5 box of cells: no staircase of brown blocks along a bank.

Kept true: every castle's gate has its road (80 of 80), the castles are joined to the road network (80 of 80 linked; the
test's own count: all but the Isle of the Seer's one), no gate in water, no road through walls; the Drowned Abbey is
reached on foot by the Spit (test). The towns moved (their placement reads the terrain beside the water: 17 of 80 are
elsewhere, and the order of them, and so their names and castle shapes, changed), which is why six goldens moved and a
few scenario tests that pick a town or a place by a rule (`town()` in test_castle, the horde's hill, the campaign fight
the battle shows) were re-seated: see the commit message. Design road 4 now ends on road 3 (it ended 14 m short, and a
town's approach road used to bridge the gap).

### Left (water)

- The coarse land (16 m) and the close-up land's water mesh still draw a shore at the grid's resolution; the far shore is
  roughened (`farShoreWobble`) so that it is ragged, not a stair, but a very gentle shelf shows its cells.
- The drainage bake's streams and pools were baked against the old lake shapes: a stream may end a few metres short of
  or inside a lake's shore. Re-baking (`tools/terrainbake`) would settle them.
- Roads 0, 2, 3 and 5 still wade (as above): road 0 now 34 points (it was 30) near (1306, 1880), by the Heartmere's shore.

## Roads and lakes together

The road look (classes, junctions, bridges that meet the ground) and the water (levelled rivers, natural lakes, one water
colour) were made apart, and met on the same land. What the merge needed (both branches' audits hold on the result: 80 of 80
gates with their road, 80 castles linked bar the Isle of the Seer's, no cliff of water, every bridge and ford passable on the
game's continent and on seeds 1, 6 and 7, the Spit reaches the Drowned Abbey):

- **A creek's water stood above the ground** (`Continent::carve`): the drainage bake's stream surfaces were made against the old
  ground, and on a seed the bake was not made for, or where the rivers have since lowered the land, a creek stood metres over its
  bank as a pool, and the bridge over it was made a "ford" over deep water that nothing could cross. A creek's surface is now at
  most 0.3 m above the ground it runs in.
- **A bridge over water that is gone** (`layApproaches`): a town's pad, laid after the first bridges, levels the creek it was
  made for. Each unnamed road bridge is re-spanned from the water nearest along its axis within 60 m; with none, or water too wide
  to bridge, it is no crossing.
- **A short road beside another did not stay joined** (`shapeRoads`): the junction ends put back on the road they joined were
  only those of roads of eight points or more; a town whose gate is a few metres from a road (its road is four points) lost its link
  when a boulder's bend moved the road beside it. Every junction end is put back.
- **The walk of a ford stopped at a gate** (tools/bridge_audit.h): a gate road ends at the (shut) doors, and a ford within 30 m of
  one was "walked" into them. The walk stops short of the town's pad.

### Men stuck in a courtyard

After the towns moved, scenarios on some other towns left men standing in a courtyard (a man who came down a ramp, a squad sent
in at a broken gate, giants through the gap). Two causes, neither in the new land:

1. The navigation grid closes every 4 m cell a house touches. Houses on a jittered 11 m grid, 4 to 7 m across, leave gaps a cell
   cannot hold; with the keep, the ramps along the walls and the towers, a row of them sealed a ramp's foot, or the whole back of the
   yard, off from the courtyard and the gate (30 of 240 ramp feet in the 80 castles; a route from there found nothing and the man
   stopped at the foot). `unsealCastle` (continent.cpp) closes the same cells from the castle's geometry and takes down the house
   nearest the way between a cut-off foot and the yard until it is open, and, where no house is to blame, moves the ramp along its
   wall by up to 3 m. 7 of 240 remain (a ramp so close to a wall and a tower that the grid has no cell between); no castle is left
   without a way (`a_ramps_foot_has_a_way_to_the_courtyard_in_every_castle`). `town()` in test_castle is the most open town again
   (the lakes branch took the fourth), and every castle test but the giants' passes on each of the twelve most open towns.
2. A gate used to be 31/50 of the wall's height, and walls were 7 to 10 m: a quarter of the towns had a gate 4.4 m high, which a
   giant (5.5 m) could not pass under. Fixed by design: the stone over the doors starts at 31/50 of the wall or 5.8 m, whichever
   is higher (`Region`, where the gates are made), and neutral walls are 8 to 11 m (they were 7 to 10), so the lintel always keeps
   0.7 m under the wall-walk. Every gate now passes a giant, and `town()` no longer has to pick one.
2. A gate is 31/50 of the wall's height, and a town's walls are 7 to 10 m: a quarter of the towns have a gate 4.4 m high, which a
   giant (5.5 m) cannot pass under. That is a design fact, not a navigation fault; the giants' test needs a town whose gate stands
   taller than a giant, and `town()` says so.

## Rocks in the water and waves on the shore

Water had no scenery and a flat edge. Two additions, one in the sim and one in the shaders.

### Rocks and boulders in the water

`Region::waterRockAt` is the water's half of the scenery rule: the same 5 m position-seeded grid as the land's trees and rocks
(`Region::sceneryPoint`), taken at points whose ground is Shallows or DeepWater. Region (`scatterScenery`) and the close-up land
(`DetailLand::growScenery`) both apply it, with their own ground and water lookups, so they stand in the same places (a test cuts
the Ford's window both ways: 31 of 31 match). Inputs are the water's depth (to a tenth of a metre, so the two beds' differences of
a millimetre do not change a rock) and the shore band (`shoreBandOf`: dry land within 10 m, within 26 m, or open water, from eight
probes of each ring). The rate falls with the band (12%, 5%, 1.2% of grid points before clusters), clusters where a patch noise
runs high (x3) and thins where it runs low (x1/5), so there are singles and small groups, mostly near shores, isles and bends, a
few further out where it is shoal. A seventh of them are boulders (1.1 to 2.3 m across), the rest rocks (0.35 to 0.85 m);
about two in five lie just under the surface (0.15 to 0.55 m below it, a dark shape through the water: `Obstacle::kSunkenRock`,
not solid), the rest break it (`kWaterRock`, solid, up to 2.2 m of water for a boulder and 0.8 m for a rock). `Obstacle::halfX` of a water
rock is the depth over its foot, so the renderer finds the surface without asking the sim again.

What a rock that breaks the surface keeps clear of (all tested on three seeds, `test_water_rocks.cpp`):

- every road's line by its half width, its own radius and three metres (`Region::roadClears`, which fords and bridges ride on: a
  ford's whole width is a clear way), and the deck of every bridge and 8 m past its ends (the scatter's existing deck rule);
- a river's mouth (170 m) and the water about a town (100 m, its pier and harbour), and every fourth point of the strand of an isle
  that is walked to (the Spit to the Drowned Abbey, 14 m): `Continent::waterKeepouts`, built when the land is dressed;
- the middle of a campaign cell (10 m; Campaign::centre, 44 m cells), where a boat sits.

The campaign sails from cell to cell, never between metres, so no route is blocked by a rock: a boat is only kept off one at the cell
middle, at a pier and at a river's mouth. The Ford scenario's own hand-made river has none (it is a set piece). The Region
checksum, hence the goldens, moved with the rocks.

Drawn as rock meshes with the land's rocks (before the water, which is blended over them: the sunk ones show through it), and a
ring of foam and a dark wet patch on the water round each that breaks the surface (the waterfalls' batch, kind 2). Beyond the
close-up land's reach no water rock is drawn (the far land has no scenery but its trees).

### Waves on the shore

One function, `shoreSwash` in `kCommon` (shaders.h), takes the height of the ground over the water at a point, its steepness, an
energy and the pixel size, and returns the foam, the wet sand and the water lying over the beach. The water's own program
(`kWaterFs`, over the shallows), the battlefield's and close-up land's terrain program (over the beach) and the far land all call
it, so the line of foam crosses from water to sand unbroken. A wave is born offshore, runs in as a bright thin line, runs up the
beach to a run-up height, and slides back with a lace of foam behind its front, leaving the sand dark, which dries over several
seconds. Phase varies with position (value noise), so the waves do not pulse in step; the size of each wave comes from noise at its
own wave number, and a slow swell (a minute or two) modulates the set; on a sea shore a second, smaller wave sometimes trails the
first. Pure time-and-position shader: nothing in the sim.

`shoreEnergy(surface, flowSpeed)` is the water body kind: the sea's level (under 0.5 to 1.1 m: the far land's inland/sea split) is
1.0 (run-up 0.1 to 0.3 m of height, so 1 to 3 m along a gentle beach, wave 7.5 to 14 s apart), a lake's 0.13 (a few cm of height, slow),
and the current kills it: a river, which runs, has none, and a river's mouth fades out up the estuary as the flow takes over.
