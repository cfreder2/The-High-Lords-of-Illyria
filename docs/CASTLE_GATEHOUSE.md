# Castle gatehouse: archers that shoot down onto the gate

Status: proposal. Nothing here is built. It waits for the `gates-and-garrisons` branch to merge into `sandbox`, because
that branch owns the code the gatehouse has to feed (`world_keep.cpp`, `world_gates.cpp`, `gate_geometry.h`, the gate
rendering), and this proposal touches none of them.

## What is wrong now (measured: `illyria_terrainaudit`, docs/TERRAIN_AUDIT.md)

A gate today is an 8 m gap in a straight curtain wall (4 m thick, 7 to 10 m high; a capital's 4.5 m and 11 m). The two
flanking towers stand in line with the wall, their faces flush with the passage (middle 8.8 m from the gate's middle, radius
4.8 m, `Ring::gateTowerAt`). The 8 m of stone over the passage has no room for an embrasure between the towers
(`Ring::crenels` clears 5.1 m either side of each tower), so **no archer stands over the gate**, and the garrison's archer
stations (`Rampart::embrasures`, taken nearest the doors first within 60 m, `World::keepGates`) are the curtain embrasures
beyond the towers: 14 m or more to either side of the doors, shooting along the wall face at a grazing angle, and the
towers are shut (nobody stands on them).

Counted on the 80 castles (outer ring, the ground 10 m in front of the gate, bow range 55 m, a shot within 60 degrees of
straight out of its embrasure, no wall or tower in the line):

| embrasures that can shoot at the ground 10 m in front of the gate | castles |
|---|---|
| 2 | 18 |
| 3 | 4 |
| 4 | 58 |

Two to four archers, none able to shoot straight down at men at the doors (the nearest is 14 m to the side). The foot
behind the doors have their horseshoe (`kHoldDepth`); the archers have nothing to match it.

## The shape: a projecting gatehouse

Everything in the gate's own frame: `u` along the wall from the gate's middle, `v` outward from the curtain's outer face,
`z` up from the pad. `t` is the ring's wall thickness (4 m, a capital's 4.5 m), `H` its height, `W = 8 m` the gate width
(`Ring::gateWidth`, unchanged, so the doors and `Gate` are as they are).

```
                 v (out)
          +-----+       +-----+   v = +8     front of the towers
          | T-  |       |  -T |
          |  L  |       |  R  |   machicolated hourd:  u in [-W/2, +W/2], v in [+5.5, +8]
          +--+--+=======+--+--+   v = +5.5
             |   forecourt  |
   ==========+  (open sky)  +==========  v = 0   curtain's outer face, doors flush with it (as now)
   curtain   |   passage    |   curtain
   ==========+              +==========  v = -t   inner face
```

1. **Two forward towers.** Round, radius `R = 5 m`, their centres at `u = +-(W/2 + R) = +-9 m` (so the passage keeps its 8 m)
   and `v = +3 m`: they project 8 m ahead of the curtain's face (`P = 8`), their backs buried in the wall. Height
   `H + 3 m` (now `5/4 H`; a 7 to 10 m wall gives towers of 10 to 15.5 m, a capital's 16.75 m). They are open-topped
   fighting platforms at the walk's height, not roofed, with a parapet all round (the walk's own: `kParapetSill`,
   `kMerlonTop`, `kCrenelWidth`).
2. **The forecourt.** The 8 m x 8 m open ground between the towers' inner faces, in front of the doors. Open to the sky,
   so a man at the doors is in the middle of three faces of embrasures: it is the gate's kill zone, the way the
   horseshoe is behind it.
3. **The hourd.** A stone gallery bridging the two towers at the walk's height, `u` in `[-4, +4]`, `v` in `[+5.5, +8]`
   (2.5 m deep, as deep as two men pass on `kGalleryWidth` 3.4 m less the parapet). Its front parapet has 4 embrasures, 2 m
   apart (`u` = -3, -1, +1, +3); behind them the floor has a machicolation slot 0.6 m wide the whole 8 m, through which
   archers shoot straight down at the ground at `v` 5.5 to 8 (the mouth of the forecourt); the slot is no hindrance to
   walking (it is `Deck`-open, not a hole in the nav: it is only a missile aperture, so the sim treats the floor as
   solid for movement and open for shots that pass `z` down through `u`, `v` inside the slot).
4. **The walk over the doors.** The curtain over the passage carries a walk (it already does: "the walk runs the whole
   edge, over the gate too"); now it joins the hourd by a 1.5 m-wide ramp-step on each side, so the walk is
   continuous round the gate: tower, hourd, tower, curtain.

### Embrasures, and what each covers

Stations (what `Rampart::embrasures` holds, so `World::keepGates` takes them unchanged, nearest the doors first):

| where | count | pitch | covers |
|---|---|---|---|
| hourd front parapet | 4 | 2 m | the ground straight out, and down onto the forecourt through the slot |
| each tower, outer arc (front 180 degrees) | 5 | 2.0 m of arc (R + parapet: 6.0 m, 18.8 m for the 180) | the approach: a fan 120 degrees across, 55 m |
| each tower, side facing the forecourt | 2 | 2.5 m | across the forecourt at the doors, 8 m, and along the passage |
| each tower, side toward the curtain | 2 | 2.5 m | along the wall's outer face, the foot of the curtain either side of the gatehouse (dead ground today) |
| curtain beyond the towers | as now (`crenels`) | 4 m | the ground along the wall |

Per gate: 4 + 2 x (5 + 2 + 2) = **22 stations** at the gatehouse (from 2 to 4 today), plus the curtain's as now.
`kMannedPct` 50 means 11 archers in a normal garrison. Each tower's 9 embrasures fit its platform: 9 men at 1.5 m
frontage on the circle of radius 3.4 m behind the parapet (21 m round; 12 m of that is the front and sides).

Every one of these has the gate's mouth inside bow range (at most 14 m) and a straight line to it above the pad's
level ground: no further line-of-sight machinery is needed beyond the existing wall slabs, and a shot from 12 to 16 m
up at a man 8 m away is steep: `World` gives archers the high-ground range bonus already.

### Reaching them: ramps and stairs

Archers climb the existing ramps (`Castle::Ramp`: 1 in 2.5 to a 3 m landing on the walk, three on the outer ring, either
side of the gate and the back wall). The ramps beside the gate stay as placed (the edges either side of the gate edge)
and the gate's edge keeps its walk, so a man gets from the ramp's landing along the curtain's walk to the tower. The
towers are the new thing to reach: each tower's platform is entered from the curtain's walk through a door 1.2 m wide at
the walk's height in its back (`u` = +-(W/2 + R), `v` = -1.5), a level step (towers are `Deck` arcs at the walk's height
now, "round each tower on its gallery"; the gatehouse towers take the full circle, not the inner half). No new stair
geometry: the towers' platforms are galleries (`Castle::Gallery`: `from`, `mid`, `to`), widened to
`radius + kGalleryWidth` all the way round, joined at their backs to the curtain's walk.

## What changes in the continent code (all in `Continent::planCastles`, `castle_walls.cpp`, `Region::fromContinent`)

- **`Castle::Ring`** gains `Fixed gatehouseProjection = 0` (8 m for the outer ring, 0 for an inner bailey: keep the
  bailey's gate as it is, a capital's second gate need not shoot down) and `Fixed gatehouseRadius = 5 m`. The existing
  `gateTowerAt()` (`gateWidth / 2 + thick * 6 / 5`) becomes `gateWidth / 2 + gatehouseRadius` (9 m: 0.2 m more than now).
- **`Castle::Tower`** gains `Vec3`'s `v` offset through its `pos` (the forward towers' `pos` is the middle of the circle at
  `v = +3`). `planCastles` (the "A pair of towers flanking each gate" block) pushes those two instead of the flush pair. The
  corner-tower rule (`Ring::towered`, `kSharpCorner`, `kTowerSpacing`) is unchanged; its `apart` test measures from the
  new tower centres. Nothing else about towers changes.
- **`Ring::galleries()`** returns the gate's pair as full galleries (`from` = back along the wall one way, `to` = the
  other, `mid` = straight out), radius `R`. **`Ring::way()`** follows them (the walk now goes out round the towers and
  across the hourd: the way from tower to tower is a short chord `v` = +7: `way()` already adds the chord where two
  galleries run into each other).
- **`Ring::crenels(edge)`** on the gate's edge clears `R + kTowerClear` at the new tower centres (as now), the hourd's 4
  go in a new `Ring::hourdCrenels()` (pairs of `u`, `v`), and `Region::fromContinent` turns the towers' outer-arc
  embrasures, the hourd's and the forecourt faces' into `Rampart::embrasures` (position behind the parapet, `out`)
  exactly as it does the curtain's (`p = a + along * c + outward * (thick / 2 - kParapetThick - 0.45)`), and walls
  for the hourd (a slab `u` in `[-4, 4]`, `v` in `[5.5, 8]`, to `walk + kMerlonTop`, `walk` set, parapet outward) with
  `within()` giving its embrasures, as for any wall. The machicolation is the one genuinely new sim fact: a `WallSeg`
  slot `{u0, u1, v0, v1}` through which `Region::sceneryAt` lets a missile pass; call it `WallSeg::slot`, a single
  rectangle checked after the slab test, and not drawn by the sim.
- **The gate's road end** (`Continent::layApproaches`, continent_roads.cpp) ends at the front of the forecourt instead of
  the doors' face: `mouth = mid + out * (thick / 2 + gatehouseProjection)`. The forecourt is paved (`Ground::Road`
  over its 8 m x 8 m, through the `roads_` spur's own width and the apron: the spur's first control point moves out, the
  forecourt is the first 8 m of it).
- **The pad.** The outer wall's reach at the gate grows by `P` (8 m): `kCapitalRadius` and `kTownRadius` already have
  slack (the wall is 0.8 R, a gatehouse 8 m out reaches 0.87 R for a town, 0.89 R for a capital); the spacing
  (`kMinWallGap` 30 m) and `routeApproaches`' own-ring zone (0.9 R) still hold.
- **Houses and the keep** are untouched (they keep clear of the "street to the gate" and of tower galleries; the gate's
  galleries are now larger: `clear` in the houses' loop already measures from `t.radius + kGalleryWidth`).

Both game modes get it by construction: Skirmish's castle and Conquest's town are the same `Continent::Castle` (one
`Region::fromContinent`), so the gatehouse, its stations and its road are one code path; `World::keepGates` is
already the one garrison rule for both.

## Cost

- Walls: +2 slabs (the hourd) and 2 circular towers moved, per gated ring; the nav cells they close are the same order.
  Tick cost: the garrison thinks every 10 ticks over `Rampart::embrasures` within 60 m of the gate: 22 more stations
  (about 44 in all with the curtain's) inside the sort: negligible against the unit grid query.
- Determinism: all geometry is `Fixed` from the continent's fixed-point generator; the goldens move once (`continent
  checksum`, the region and Conquest goldens), and the gate hashes (`GateState`) do not change.
- Renderer: a gate tower is a taller cylinder and the hourd is one more box on the gate's mesh: the gate meshes are the
  other worker's (`meshes.cpp`, `renderer.cpp`), so that part waits for the merge.

## Order of work after the merge

1. `Castle::Ring` fields, `planCastles` towers, `galleries()`, `way()`, `crenels()`, `hourdCrenels()`; castle tests
   (every gate has at least 20 stations, `tests/test_terrain_audit.cpp`'s `overlook` count for the ground 10 m ahead
   rises from 2 to 4 to at least 14).
2. `Region::fromContinent`: the hourd wall, the towers' embrasures, `WallSeg::slot`.
3. The road's end at the forecourt (`layApproaches`), the paved apron.
4. The renderer's towers and hourd, with the gate mesh's owner.
5. `docs/GAME_DESIGN.md`'s castle section, `docs/MAP.md`.
