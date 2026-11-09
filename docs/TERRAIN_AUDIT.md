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
