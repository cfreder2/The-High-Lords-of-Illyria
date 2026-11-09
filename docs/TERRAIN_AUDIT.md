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
