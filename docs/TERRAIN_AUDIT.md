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
- **Bridges** (`Continent::placeBridges`, `Crossing`, `Deck`): the deck is centred on the span of water measured along its axis to dry banks
  (five dry metres in a row, a bridge never longer than 90 m a side), level over the water and clear of it, with a ramp at each end grading
  down to the ground at 16% (`rampA`, `rampB`, `endA`, `endB`), the road drawn onto its axis, and as wide as the road there; a bridge a castle's wall stands over is
  no bridge (3 bridges that were inside walls are fords). The mesh draws the ramps as causeways between retaining walls.
  `tools/bridge_audit.h` (`illyria_terrainaudit --bridges`) walks a man, a rider and an engine across each bridge, each way, in a World with the
  real Region and NavGrid (14 m of road before one end to 40 m past the other; across when past the far end). Before: 11 of 18 walked across,
  decks 1 to 8 m above the ground at their ends (up to 8.4 m, a sheer wall); after: 10 of 15, all ends within 1.5 m of the ground.
  The outpost's pole stands at `Crossing::bridgehead()`, dry ground beside the road at a bridge's head (the campaign's own position of the
  outpost is unchanged).

Left: five bridges that the audit cannot walk across (`illyria_terrainaudit --bridges` lists them): two where the road turns hard at the end
(the unit's straight line from the road's point to the deck leaves the deck's width), the long design bridge over a flood plain, one
footbridge, and one where the pool beside the end is wet in the Region's own water. The navigation, not the bridge, is at fault in most.
