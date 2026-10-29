# The High Lords of Illyria — Technical & Game Design Starter Document

| | |
|---|---|
| **Version** | 1.0.0 |
| **Project Codename** | Project Illyria |
| **Engine Target** | Custom C++20 engine, deterministic lockstep simulation — see §5.1 |
| **Target Platform** | Web browser first (itch.io, via WebAssembly), **fully playable on phones**; then native PC (Steam / GOG) and a native iOS app if successful |

---

## 1. Executive Summary & Vision

*The High Lords of Illyria* is a real-time tactical strategy game (RTT) with deep grand-strategy map mechanics. It bridges two iconic classic paradigms:

1. **The Spatial Physics & Tactical Momentum of *Myth: The Fallen Lords*** — projectile ballistics, physical debris, elevation-based physics, dynamic mutilation, spatial directional combat.
2. **The Macro-Strategy, Named Attrition, and Probability Progression of *Warlords*** — continent campaign map, named unit identity, slow production cycles, temple blessings, high unit scarcity.

## 2. Graphic & Rendering Engine Architecture (Hybrid 3D / Billboards)

To replicate the iconic aesthetic of late-90s tactical games while leveraging modern lighting and performance, the game uses a **Hybrid 3D-Sprite Rendering Architecture**.

### 2.1 Visual Categorization

- **Terrain & Architecture (Full 3D):** Fully modeled 3D heightfield mesh terrain, physical cliffs, dynamic water, destructible stone walls, and trees with individual collision capsules.
- **Soldiers and creatures (2.5D sprites, made the way Myth's were):** every unit type is a small articulated 3D figure built and posed in code (`game/render/figures.cpp`): a person with hips, knees, shoulders, elbows, a head and a kit of helmets, hoods, weapons, shields, robes, capes, wings and tails; or a beast (horse, wolf, elephant, griffin, dragon, spider) with a gait, and a rider astride when it has one. At battle load the GPU photographs each figure's poses from five directions 45° apart, 30° above the horizon. The other three directions are mirror images. The pictures are rendered at twice their size, resolved to hard edges with a dark one-pixel outline, and packed into one sprite sheet per type.
  - **Animations (26 frames a view):** idle 4 (breathing, wings and tails stirring), walk 8 (tied to distance walked, so feet do not slide; fliers flap), attack 6 (sword swing, two-handed overhead blow, pike or lance thrust, bow drawn and loosed, satchel thrown, spell cast, beasts rearing and biting), flinch 2 (on every wound), death 6 (people fall backwards, beasts roll onto their sides; the last frame stays where they lie).
  - **In battle:** each soldier is a billboard showing its sheet's frame for the view the camera has of it. The view comes from the camera's bearing against the soldier's facing. The frame comes from its deeds: when it last struck or loosed, when it was last hurt, when it died, and how far it has walked. The side's colours are a mask in the sheet, tinted when drawn, so both sides share one sheet. Close up the pixels stay crisp; far out, soldiers become coloured specks. The same sprites, turned to the sun, cast the soldiers' shadows.
  - **Budget:** about 28 pixels a metre, no picture more than 160 pixels across (a dragon is coarser than a man); every frame cropped to its outline. The whole roster for both sides is about 56 MB, and a battle bakes only the types it has. `tools/spritesheet` draws the same sheets in software for review.
  - **Pre-rendered sheets:** every unit type has a sheet made offline with Blender, the way Myth's sprites were (`tools/blender/`, one command regenerates them all: `tools/blender/build_all.sh`; the roster at a glance in `docs/images/sprites/roster.png`). People share one body, rig and set of motions by how they fight (a swing, an overhead blow, a pike thrust, a bow drawn to the cheek, a satchel thrown, a spell gathering as light), with per-type proportions (a dwarf's short legs and big head, a giant's bulk) and outfits; two-handed weapons keep the second hand on the haft by IK. Mounts and monsters are a horse, a table-driven four-legged body (wolf, elephant with howdah and crew, griffin, dragon), a spider and a catapult. Every figure carries the side's colour where it reads at a sprite's size (tunic, tabard, kilt, cloak, shield face, plume, caparison, collar, the spider's mark). Fliers' walk frames hold two cycles, 0-3 on foot and 4-7 a wingbeat, played by whether the unit is in the air. Figures are modelled from scratch by script with smooth subdivision-surface bodies (anatomical cross-sections, faces, hands, a horse's muscles, joints and hooves), clothes with folds, mail, plate, leather and layered feathers (a bone per feather, so wings fan open and fold shut), rigged and keyed for the same five animations, and rendered with Cycles from the same five views with key, fill and rim lights, the team's cloth rendered a second time as a matte. They are packed into the runtime bake's format (alpha codes for team colour and outline, the same cell table) at 48 pixels a metre (less for the largest, so no picture is over 256 px and an atlas fits 2048 x 2048), a WebP atlas and a JSON table per type in `game/web/sprites/`. The page fetches the sheets only for the types a battle has; the bake shows meanwhile, and stays if a fetch fails.
  - **Close-up sheets and the art pipeline:** a type may also have a close-up sheet at twice the pixels a metre (`<unit>_hd.webp`, listed in `sprites/index_hd.json`). It is fetched only while the camera is near that type's soldiers (their normal sheet would be magnified more than 1.15x), within a budget of 8 M texels, and dropped when the camera pulls back (`game/render/closeup.h`, `renderer_sheets.cpp`). The camera may not come so close that any sprite on screen is magnified past 1.5x the best sheet its type has. Hand-made art comes in through `tools/sprites/import.py` (one PNG a cell, named and laid out as `docs/SPRITE_SPEC.md` says; templates and the manifest in `docs/sprite_templates/`), which packs a normal and a close-up sheet in a new layout of 44 frames a view: the old five clips plus three more deaths (die_front, die_crumple, die_burn; the old death is die_back) and 20 gib-part slots (`game/render/sprite_layout.h`). The renderer picks the death by how the soldier died (the death matrix, §5.5: burned, cut, struck from behind or in front); old 26-frame sheets play their one fall for all of them.
- **Large Creatures, Siege Engines & Heroes:** the original plan was full 3D skeletal models for giants, dragons and siege engines. The sprite figures above now cover every creature; siege engines remain to be designed.

### 2.2 Shadow & Lighting Integration

- Billboard quads write to a custom **Depth Pre-Pass** buffer, allowing 2.5D units to cast real-time directional shadows onto 3D terrain and receive dynamic point lighting from fire, spells, and torchlight.

## 3. Core Combat & Physics Mechanics

### 3.1 Dispersion Circle & Ballistic Projectiles (Ranged Combat)

Ranged attacks do not use guaranteed hit animations. Projectiles follow real Newtonian trajectories governed by gravity, wind, and initial velocity vectors.

- Each shot's aim point is sampled inside a **dispersion circle** around the target. The circle's radius grows with range and shrinks with unit veterancy (§4.2).
- Missed projectiles stay physical: they can strike friendly units, terrain, or debris along their path.
- *All randomness must come from the seeded, deterministic simulation RNG (§5).*

### 3.2 Spatial Melee Physics (Sword vs. Shield / Flesh)

Melee combat is dictated by physical bounding volumes and weapon trace envelopes rather than percentage-based hit rolls.

1. **Physical Shield Collision**
   - Shields are active 3D/2.5D colliders covering a frontal cone (*default **120°**, tunable per shield type — value TBD, see Q4*).
   - Weapon swing traces that hit the shield collider calculate **Deflection**: damage is mitigated by shield rating, and the attacker experiences poise drain or recoil.
2. **Spatial Dodge Envelopes**
   - High-Agility units trigger a physical hurtbox displacement frame during an incoming attack trace. The unit performs a quick evasive step, causing the enemy weapon to slice through open air.
3. **Flanking & Rear Exploits**
   - Strikes originating outside the defender's frontal arc ignore shield collision entirely, applying unmitigated damage directly to flesh or armor.

### 3.3 Facing, Charges, Veterans and Morale (built)

The battle simulation's rules for the tactical guide (`engine/sim/src/world_combat.cpp`, `world_morale.cpp`; tests in `tests/test_combat.cpp`):

- **Facing:** a blow lands on the front (the 120 degree cone the target faces, as its shield covers), a flank, or the rear (the 120 degrees behind). A blow in the flank does **+50%**, in the rear **x2** (a berserker: 25 front, 37 flank, 50 rear). Missiles do +30% in the flank and +60% in the back (an elven arrow: 16, 20, 25). Shields only ever block from the front, never from behind.
- **Off guard:** a soldier trading blows keeps to that foe while it is within reach; anyone else coming at him then finds him off guard, and a flank blow counts as one in the back.
- **Turning:** soldiers turn on the spot at 270 degrees a second, riders and anything as heavy as a minotaur (mass 4) at 120, and strike only within 45 degrees of facing their foe. Walking they face their way at once. So a man struck from behind loses a blow or two turning round, and a rider caught side on loses its charge.
- **Charges:** riders (the mounted types: cavalry, wolf riders, pegasi, elephants) build **momentum** at speed: toward their actual speed by 1/16 a tick (most of the way in about a second), shed at 8 m/s a second when slowing, lost at once below 1 m/s, when thrown, or once in melee. With momentum of at least 60% of their speed (and 2.5 m/s), the first blow on contact adds **impact = momentum x mass x 1.2** to the blow, whatever its cooldown, and throws the foe by momentum x mass / 3. The run in sets a multiplier: downhill +200% per unit of grade (up to x2), uphill -250% per unit of grade (down to x0.25). Flank and rear multiply the whole blow. A shield turns the blow but not the weight: half the impact still lands. Heavy cavalry over 28 m against a still heavy infantryman: a standing blow 28, a charge on the flat 54, up a 1 in 4 slope 17, down it 64; light cavalry 40 into the front, 80 into the back.
- **Braced pikes:** a charge into the front of pikemen who are standing (not marching) runs onto their points: the rider deals nothing, takes the pikeman's blow with his +150% against riders plus half his own impact (heavy cavalry: about 49 of 130), and is thrown back. Into their flank or rear the charge goes home as usual (106 to the pikeman).
- **Veterans:** a soldier's rank is now one of the small edges (§3.4): +3% damage a rank (an elite +9%: his blow 27 where a recruit's is 25), with +3% shield block a rank, the 15% tighter aim per rank, and back on his feet after a throw 15% sooner a step (a recruit takes 0.67 s). Each blessing (and the hero's artifacts the campaign counts with blessings) keeps its old weight: +10% damage, blows and reloads 7% quicker, 6% less damage taken. A hero's levels are his strength: +6% damage and +8% hit points a level. Ranks carry over the campaign as before. (Before the small edges a rank was worth +10% damage, 7% quicker blows and 6% less damage taken: an elite was a different soldier, not a slightly better one.)
- **Shield pips:** each soldier's health bar (shown over a hurt, ranked, shaken or selected soldier when close enough to read) carries a gold shield per veteran grade (`sim::veteranGrade`, 1 to 3); an amber frame marks a wavering soldier, a white flag a broken one.
- **Height for archers:** missile reach grows by a metre for each metre the shooter stands above the target (up to half again) and shrinks as much shooting up (a quarter at most), for both ordered shots and idle shooting. Archers 16 m up a 1 in 4 slope hit troops 64 m away (flat reach 55 m), who cannot answer. A plunging arrow does +1% per 0.01 of the sine of its fall beyond 0.25, at most +25% (shot down from a 20 m hill at 70 m: about +18%).
- **Morale:** every soldier has morale (0 to 1000). Shocks, at a man's resolve: a hero of his side falling within 30 m 850 (within 60 m 425), a friend killed within 12 m 70, a blow in the flank 25 or the rear 50 (missiles half), being charged 150 (more in the flank or rear), and, every third of a second, 10 for each foe within 15 m beyond the friends there (counting himself). Each type's **morale** in docs/UNITS.md scales the shocks (light infantry 110%, heavy infantry 85%, berserkers 35%, devils 20%, elephants 120%); undead and heroes never break. Each veterancy step takes 3% off a shock (8% before the small edges, §3.4). After 2 s without a shock and not outnumbered, morale returns at 45 a second, +45 near a living hero of his side (30 m), +6 for each friend near (up to 6). Below 450 he **wavers**: blows and reloads a quarter slower and aim half again as wide. Below 200 he **breaks**: he drops his orders, ignores new ones and runs from the foes within 40 m; he rallies, standing and ready for orders, once morale is back to 550 and no foe is within 25 m. A hero's fall in front of green light infantry breaks them all; elite heavy infantry are badly shaken but hold; they rally in about 8 s once the danger is past. The computer does not order a squad whose men have all broken. Morale is part of the simulation state (and its checksum); the death of a hero is also published as an event (`World::heroDeaths()`, with where and whose) for the HUD.
- **Veterans and rallying** (`world_morale.cpp`; tests in `tests/test_morale_rally.cpp`): veterans come back first. Each veterancy step adds **25%** to a soldier's recovery each census and lowers his rally bar by **50** (recruit 550, regular 500, veteran 450, elite 400) and the foe-free reach he needs by **3 m** (25, 22, 19, 16 m). Alone and safe, a broken recruit rallies in about 14 s, a veteran in 8.7 s, an elite in 7 s.
- **Steadied by veterans:** at each census a soldier counts the veterans and heroes near him who are not shaken themselves: each veteran of a higher grade than his within **12 m** takes **4%** off his shocks (an elite **6%**), a hero of his side within his 30 m leadership reach **15%**, together at most **25%**; and half that again in morale points goes into each census's recovery. A hero's fall cancels the steadiness at once (it is counted afresh at the next census), so his death still breaks green troops. Recruits under the same hail of shocks hold about half again as long beside one elite.
- **"Hold the line!":** a veteran or elite (not wavering or broken himself) who sees at least **2** friends within **20 m** wavering or broken, or a hero who sees **1**, calls out. Every friend in earshot (20 m) gains **150** morale from a veteran, **200** from an elite, **300** from a hero; a broken man whose morale so reaches the waver line (450, or his own lower rally bar) turns back at once, even with foes near. Then the caller is quiet for **15 s** (a hero **12 s**). Each call is published as an event (`World::rallyCalls()`: caller, side, where, veteran or hero, how many heard and how many turned back), like `World::heroDeaths()`, for the voices and the HUD. Steadiness and the call cooldown are part of the simulation state and its checksum.
- **The computer's riders** look for the nearest player squad (within 120 m) that is locked in melee and ride round its nearer flank, 28 m out, to come at its back; close behind it they charge home.

### 3.4 The Small Edges (built)

Like the attachments on a rifle: each a few percent, they add up, and none decides a fight alone. A weaker army with every edge is still the weaker army. The same edges, with the same numbers, count in the Warlords dice (`engine/sim/src/campaign_rules.cpp`, `Campaign::armyEdges`) and in the 3D battle (`engine/sim/src/world_edges.cpp`, `World::edgesOf`); the constants are in `engine/sim/include/illyria/sim/edges.h`.

| Edge | When | Worth |
|---|---|---|
| Home land | the hex the battle is fought in is your side's (§4.2.9) | +3% |
| Familiar ground | the dice: the type likes the ground (Warlords' army-type table, its pluses: elves in forest, dwarves and griffins in hills, pegasi in forest, the dead in marsh) +4%, and the empire's liking +2% a point; 3D: the soldier's gait is clearly quicker there than a man on foot (elves and spiders in the woods, wolves in the woods, dwarves on rock, giants in mud and fords, the dead in swamp) +4%, or the type's own liking (pikemen and light horse +3% in the open, knights +5% in the open, dwarves +4% in hills) | at most +6% |
| Veterans | +3% a rank: regular 3, veteran 6, elite 9 (soldiers; a hero's levels are his strength) | at most +9% |
| A hero leads | the best hero in the stack (dice) or within 30 m (3D), by his strength (3 + level): 4-6 +2%, 7-8 +4%, 9 +6% | at most +6% |
| High ground | dice: 15 m or more above the other side's cell (the defenders above the approach, or attackers coming down on them); 3D: striking down at a foe below, +1% for each 0.05 of grade between them | at most +4% |
| Holding | dice only, a defender holding a temple, a ruin's stones or an outpost tower (Warlords' +1 and +2) | +4% |
| **All together** | | **at most +15%** |

- **In the dice** an army's AS (DS) is now (strength + the whole Warlords points: fliers +1, special armies +1, command artifacts, a town's walls, catapults against walls) x (1 + its edges), in hundredths, between 1 and 9; the die has a thousand faces (a roll of 1-1000 against 100-900, the same odds as a d10 against whole strengths). A hero's leadership, the empire's and the type's liking for the ground, and holding a temple were whole points (+1 to +3); they are edges now. The ground's minuses of the Warlords tables are gone: the ground already costs movement on the map and speed on the battlefield, and fighting away from home costs nothing: you just lack the edge. A soldier's rank no longer adds to his strength (heroes' levels and blessings still do): Campaign::strength and the banners' numbers show it.
- **In 3D** the edges add to the damage of every blow and missile (with a hero's levels and blessings: `World::gracePct`); fractional damage is rounded up by chance in proportion (`World::scaledDamage`), so +3% is +3% on average rather than nothing until it crosses a whole point. Veterans also lose less morale to a shock: 3% a step (it was 8%).
- **What it does** (tests in `tests/test_territory.cpp`): four strength-4 armies against four are a coin toss (500/1000); home land alone 532, veterans 564, a hero 2-4%: just over half; every edge together (+15%) 685, rolled with the campaign's die 693; three and a half against four with every edge still loses. In 3D, six light infantry against six, scattered a little, each seed fought both ways round: at home and veteran (+9%) they won 39 of 40 (37 before the orders and the death matrix were merged in); home land alone (19 of 40) and veterans alone (23) hardly show. The two together count for more than the sum because light infantry sit on a kill breakpoint: 11 damage x 109% is nearly always 12, five blows to kill a 60 hit-point man instead of six. (A tuning question left open: the edges are small, but a breakpoint can make them decisive.)
- **Shown to the player:** the battle prompt lists each side's edges ("Your edges: home land +3% · veterans +6% · hero leads +4%"); in 3D the selection line adds the selected soldier's ("12 Heavy Infantry · edges: home land +3% · veterans +6%"), and a tag under the title says whose land the battle is fought on.

### 3.5 Taking a Town in 3D: the Capture Point (built)

`game/src/battle_capture.cpp` (`CapturePoint`, hooked into `Battle::step` and `Battle::outcome`): in a siege (the **Siege** scenario and every campaign battle for a town) a flagpole stands in the town's courtyard. Every tick it counts the men of each side standing within **14 m** of it (alive, on their feet, not routed). If the attackers have more there, their flag climbs by **2 + one point for each man they are ahead by (at most 8)** of **1000**: one man more takes 33 s, eight more 10 s. If the defenders have more it comes down as fast; even numbers hold it where it is; an empty courtyard lets it sink a point a tick. When the attackers' flag reaches the top **the town is taken and the battle ends** (a victory, whoever is still standing elsewhere): a siege is won by taking the heart of the town, not by killing every last man.
- On screen: the pole in the courtyard, the defenders' red flag coming down as the attackers' blue one goes up; a bar at the top of the battle ("Take the courtyard: your flag 45% · 12 of yours there, 3 of theirs (rising)").
- Back on the campaign map a town taken at its capture point is the player's at once (the banner rose in the battle: `Campaign::applyBattle(outcomes, won, bannerRaised)`); the defenders still standing fall back to an open cell next to the town, or, for a neutral garrison (or with nowhere to go), lay down their arms. A town won by killing or routing every defender is held and its banner raised on the map, as after the dice (§4.2.8).
- Tests (`tests/test_capture.cpp`): six against two in the courtyard take it in 16.7 s exactly; even numbers hold it, more defenders pull it down; the Siege battle has its point in the courtyard, held by its defenders at the start; the Ford battle has none.

## 4. Macro Campaign Map & Unit Mechanics

### 4.1 The Seamless Continent ("God to Ground")

*Built so far:* the whole continent, 12.8 × 16 km ([MAP.md](MAP.md), `engine/sim/src/continent.cpp`):
- **Landscape:** a western sea, a great river that forks to divide the realms, mountain chains whose passes are chokepoints, and an unbroken eastern range. Swamps, forests, lakes, and roads crossing the rivers at four fords and four bridges.
- **An island realm (built):** Illyria is an island, the sea all round it; its coast is a rounded, wandering shore with bays and headlands, never a drawn square, and rivers and roads end at the shore. At the heart of the land, where the great rivers meet, lies the **Heartmere**, a broad lake with wandering shores. **Abbey Isle** rises in it, reached only by **the Spit**, a narrow causeway from the eastern shore, and on the isle stand the ruins of **the Drowned Abbey**, one of the ruins a hero may search. Lake and isle shores shelve gently into the water.
- **Dirt roads:** roads are dirt tracks that curve naturally (long sweeps and gentle bends, straight only through towns, junctions, bridges and passes) and widen and narrow along their length; rivers too vary in width as they run.
- **Settlements:** 80 walled cities, 8 of them faction capitals (castles).
- **Places to explore (§4.4):** 34 ruins, 4 temples and 2 sages.
- **The battle window:** today's battle is fought at Oakhaven Ford, a full-detail 1 km window into the continent (`Region::fromContinent`). Wagon roads have ruts, an earthwork berm stands above the ford, a hamlet sits along the road, and rivers slope downhill.
- **Ground and movement:** ground types follow Warlords (road, plains, forest, swamp, shallows, deep water, rock, bridge) and set movement speed. Armies path around deep water and cross at fords and bridges.
- **Zoom:** the camera zooms from the whole continent down to a single soldier.
- **Close up anywhere (built):** wherever the camera looks closer than about 850 m, on the campaign map too, the land around it is drawn at the battlefield's own detail: 4 m heights, the same ground kinds, roads with ruts, forest floor, rock, snow, banks and water, and the same 3D grass, flowers and ferns (`game::DetailLand`). It is sampled exactly as `Region::fromContinent` cuts a battlefield out (a test checks they match), so a battle fought there later looks the same. It is built in 64 m chunks around the camera, nearest first, a couple of milliseconds a frame (all 144 chunks of the 768 m window take about 0.27 s of WebAssembly time); each new chunk fades in, and the window's edge and the zoom-out cross-fade into the coarse land by a screen dither, so nothing pops. The battlefield keeps priority where both exist, and the two meet without a step (the fine detail fades in from the battlefield's edge just as the battlefield fades it out). Off with ground detail and ground cover (Low). Its trees, undergrowth and rocks are the battlefield's own: scenery grows on one 5 m grid over the whole continent, each point drawn from its own hash (`Region::sceneryPoint`), so the trees a battle would have stand there already. Nothing marks the battlefield at any zoom: the hex grids are drawn on all land alike, individual trees give way to the woods' clumps by the same distance on and off the field (within about 250 m of where the camera looks, below 600-850 m of zoom), the battlefield's ground takes on the coarse land's look as the camera rises, and the coarse land's rivers, lakes and roads take the battlefield's colours.


- There are no separate battle maps. Illyria is **one continuous 3D continent** (*original geography — see Q5*), and the camera zooms smoothly from a god's-eye view, where armies are ant-like columns and banners, down to ground level, where individual arrows, shields and bodies are physically simulated.
- Strategy still happens on a hex grid laid over the terrain (Roads, Plains, Forests, Swamps, Mountain Passes): hexes govern movement cost, supply, ownership and the 8-squad stack limit.
- When hostile armies meet, combat starts **in place**, on the real terrain where they met. Several battles can rage across the continent at once; the player watches whichever they choose and AI generals command the rest (§4.3).
- **Real-time-with-pause** in single-player; real-time in multiplayer.

| Zoom level | What the player sees | How it is drawn |
|---|---|---|
| God (continent) | Faction territories, army banners, battle "hot spots" | Terrain far-LOD, one icon per army |
| Strategic (region) | Army columns as moving clusters of dots, cities, roads | Point sprites per soldier, simplified terrain |
| Tactical (battlefield) | Formations, individual soldiers, projectile arcs | Billboard sprites, full terrain detail |
| Ground (close-up) | Shields deflecting blades, ragdolls, debris, gore | Full animation + cosmetic GPU physics |

### 4.1.1 The Lie of the Land

Topography is strategy. The rules the battle simulation already applies:
- **Terrain is tactical (built):** a unit moves at its speed times what the ground under it does for its kind, and takes the fastest way, not the shortest: a squad in the fields marches to the nearby road and along it to the town rather than straight over the plough. Each unit type has a gait (`engine/sim/include/illyria/sim/gait.h`, a small table of gait x ground in docs/UNITS.md "Ground"), after Warlords' terrain costs and Myth's feel: cavalry are fast on roads and open ground but slow in woods and hopeless in bog, so they ride round a wood that infantry cut through; wolf riders and elven archers move well among the trees; dwarves are least slowed by rock and slopes; giants, minotaurs and elephants stride through mud and fords; the dead do not mind the swamp; engines want roads; fliers on the ground walk like men. The same table sets walking speed and path cost, so the two always agree.
- **Fastest paths:** the pathfinder (`NavGrid`, 8 m cells, 4 m in castle windows) costs each cell by travel time over the best ground in it for that gait (a road through a field makes the cell road), plus the climb into it. A* with an octile heuristic at the gait's fastest ground finds the quickest route; smoothing only cuts a corner when the straight line is no slower and crosses no slower ground (never a road across a field, never a corner through a swamp). An open straight line is taken when no detour beats it. A route starting or ending on a bridge deck goes by the deck's ends. Each gait's cost grid is built on first use (2 bytes a cell); a 1 km route takes about 2 ms natively.
- **Marching together:** a squad's soldiers all follow the squad's one route (planned once a tick), each keeping his file up to 2 m to the side of it where the ground there is the same, so on a road they go in a column of march rather than some on the road and some in the field beside it; they spread into their formation at the end. The computer re-plans a chase only when its goal has moved well away.
- **Climbing is slow:** walking speed divides by 1 + 2.5 x the grade uphill (a 1 in 4 slope takes about 1.6 times as long), and is up to 15% quicker downhill. An army charging uphill arrives late and tired; one holding a crest picks its moment.
- **Cliffs are walls:** no one walks a face steeper than 45 degrees, and pathfinding routes around them. High mountains are impassable except where a pass or a road is carved through, so passes, fords and bridges are the chokepoints.
- **The high ground:** a soldier striking down the slope deals up to 35% more damage (about +20% on a 1 in 3 slope) and throws the foe harder; striking up the slope deals up to 25% less. Arrows from a height fly further on their own (the ballistics are real), and archers on a height reach and hit harder (§3.3).
- **Mud:** the trodden margins of rivers and ponds and the churned approaches to fords are mud, at a little over half speed: a ford is slow to cross and slower to climb out of.
- **Bridges:** stone road bridges on arches carry the roads over rivers (and small ones over every creek a road meets); narrow timber footbridges away from the roads let infantry over in single file. Bridge decks are real ground: soldiers walk on them, arrows land on them, and the pathfinder routes end to end along them.
- **Coasts and water:** beaches of sand along the sea; surf rolling in over the shallows; waterfalls where creeks drop steeply off the mountains.
- **Strategic consequences:** hills make the set-piece battles; fortresses on heights and at passes are hard to take; ruins and shrines in the mountains are reached only through the passes (§4.4).

### 4.1.2 Commanding the Battle (built)

As in Myth, every soldier is worth commanding on his own (`game/src/battle_command.cpp`). Every order becomes ordinary `sim::Command`s, so it reaches the simulation, and every lockstep peer, the same way; a move may carry the way to face on arrival (`Command::face`, in the unit's state and checksum only when given).

**Selecting.** A tap on one of your soldiers selects him alone; a second tap on him (or a double-tap or double-click) selects every soldier of his type the camera can see. A squad card selects that squad. A held press drawn into a box selects all of yours inside it. Shift-click adds or removes one man; shift-drag draws a box at once. Selected soldiers wear a blue ring, and the cards of squads wholly selected light up (dashed when only partly).

**Groups.** Up to ten saved groups (keys 1-9 and 0). On a phone, whenever the selection is not already a card, a **+ Save group** card appears at the start of the row; a saved group gets its own card (its key, its kind, how many live). A long press on a group card puts the current selection in it instead, or forgets the group when it is already selected. On the desktop **Alt** (or Ctrl, where the browser allows) + number saves, the number recalls, the number twice moves the camera there. Heroes and special troops (wizards, grenadiers, healers, engines) have their own cards at the front of the row, heroes in gold; **`** selects the heroes.

**Moving and facing.** Tap the ground: the selection marches there in its formation and halts facing the way it marched. Press and hold at the destination (0.3 s), and a gold ghost of the formation appears there; drag, and a dotted line follows the finger: the formation turns to face along the line, laid out across it (Myth's gesture rotation). Let go to send them; they arrive facing that way, wherever their road took them. A selection of several squads moves as one formation, spaced for its biggest soldier and paced to its slowest. A held press that starts on your own soldiers, or with nothing selected, draws a selection box instead. Tapping a gate sends engines and bombers to batter it and the rest through it.

**Where they are going.** Every move puts a **yellow ring** on the ground where each soldier will stand, with a tick the way he will face there (the formation's slots: exactly where they halt). The rings show for a few seconds after the order, and whenever those soldiers, or anyone of their squad or saved group, are selected, until each man is there. An attack shows a **red ring** under the soldier they are after (it follows him, and breathes) with a dotted red line creeping to it from each attacker; shooting at a spot or a gate rings the spot (`Battle::orderMarks`, drawn by `game/render/renderer_orders.cpp`).

**Marching with initiative** (`engine/sim/src/world_orders.cpp`). A marching soldier is not deaf to the enemy. Struck at, he turns on whoever struck him (within 10 m, remembered for 2 s); a fighter (not missile troops, wizards or healers, who only defend themselves) also takes on any enemy who crosses within **5 m** of him. But the mission comes first: he follows no farther than **15 m** from the leg of the route he was on (the leash), lets a foe go once it is more than 10 m off, and lets a broken, fleeing foe go once it is 2 m beyond his reach; then he marches on to his place (re-routing if the way back to the road is blocked). A soldier ordered away out of a melee has **2 s** to break off before he answers blows again. A **force march** (below) ignores the enemy altogether, as Myth's moves did: to pull a squad out of a fight, or run it past one. The computer's squads march the same way, so its columns fight back too.

**Attacking.**
- **A soldier**: tap an enemy with soldiers selected and all of them go after him (`Command::AttackUnit`). Missile troops close to 90% of their reach and shoot, leading a walking man by his pace times the arrow's flight; if no arc gets there (a wall, a house) they move closer. The rest run him down, re-routing every half second as he moves. To break two riders off to hit the archers, select the two (tap, shift-tap or a box) and tap an archer. Zoomed far out, a tap on an enemy squad sends each attacker after the nearest man of it.
- **A friend** (force attack): only when ordered, with the attack mode or Ctrl-click on him; he does not strike back and the kill earns no rank. Otherwise friends never fight each other.
- **The ground**: in attack mode, tap the ground: missile troops shoot at the spot; the foot march there with initiative (an attack-move).
- **A gate, wall, house or tree** (`Command::AttackObject`): missile troops shoot a gate, the rest go at it and hack it down (gates are the only thing that breaks). At a wall, a house or a tree only what flies and bursts does any harm (catapults, grenadiers, wizards: `World::canBatter`): they bombard the wall-walk or the ground beside the house or tree, hitting whoever stands or shelters there; the stone and timber stand. Ordering anyone else at them says so on screen ("Only engines, grenadiers and wizards can strike at a wall"). Burning woods and breaching walls are left for later.

**When the target falls**, each attacker decides for himself (`World::targetDown`, an `OrderDecision` event). His candidate is the nearest foe still standing within 15 m (missile troops: their reach); the alternative is going back to his old place beside the squad-mate he left (a friend named in the order: the nearest of his squad not sent with him, his place kept at most 20 m off), or, with no one to go back to, standing his ground. His **odds** against the candidate, per mille, are his fighting power over the total: power = hit points x blows (or shots) a second x (100 + 10 per rank and blessing)%; his side counts him plus half of each friend within 10 m of the candidate, theirs every unbroken foe within 10 m (the candidate too). Going back is worth the troop's own odds there (all within 12 m of his place) when it is fighting, else **600**. Then the **dice**: he sees his odds give or take a uniform error of **700** per mille for a recruit, 150 less for each step of veterancy (regular 550, veteran 400, elite 250, never under 100), and presses on if what he sees beats going back. So an elite always leaves a demon alone (true odds about 8%) and always finishes a lone wounded archer (94%); a recruit goes at the demon about one time in six and lets the archer go about one time in three (tests: elites made the better choice 400 times of 400, recruits 308). A foe he picked himself he lets go if it breaks and runs. A fighter on the march makes the same choice when his foe falls, with only foes within his leash as candidates and the march worth **700**. Pressing on, he cries out (the Charge voice).

**Desktop and phone.**

| Order | Phone | Desktop |
|---|---|---|
| Move (with initiative) | tap the ground | click the ground |
| Move and face | hold at the spot, drag the way to face | the same (left button) |
| Attack a soldier | tap the enemy | click the enemy (a red sword cursor) |
| Force attack a friend | ⚔ Attack, then tap him | Ctrl (or Cmd) + click him, or A then click |
| Attack the ground | ⚔ Attack, then tap the ground | A, then click (a crosshair cursor); or Ctrl + click the ground |
| Attack a gate | tap the gate (or ⚔ Attack, then tap it) | click it (a target cursor) |
| Attack a wall, house or tree | ⚔ Attack, then tap it | A, then click; or Ctrl + click (a target cursor, or "not allowed" if nobody selected can) |
| Force march | hold ⚔ Attack (it turns gold, "» March"), then tap the ground | Alt + click (a gold double chevron), or M then click |

The **⚔ Attack** button sits in the selection's action row: one tap lights it red and a line above it says what the next tap does ("Attack: tap a soldier (ours too), a gate, a wall, house or tree, or the ground"); held, it turns gold for a force march. Either lasts one order; tap the button again (or Esc) to cancel. On the desktop the cursor always shows what a click would do (`Battle::intentAt`): a hand over your own soldiers, a sword over an enemy (or over a friend with Ctrl), a target over a gate, wall, house or tree in attack mode, a crosshair on the ground in attack mode, a double chevron for a force march.

**Formations** (the Formation button opens a picker of nine, each sketched as it stands; desktop keypad 1-9 or shift + 1-9):

| Key | Formation | Layout | Pace | Use |
|---|---|---|---|---|
| 1 | Line | two ranks | 90% | the shield wall; the default for foot |
| 2 | Short line | three ranks, narrower | 90% | a narrow front: a ford, a gate, a gap in a wall |
| 3 | Staggered | a checkerboard of two ranks, nobody within 1.8 gaps of another | 95% | missile troops and grenadiers (their default): one stray bomb or fireball takes one man |
| 4 | Box | a near-square block | 85% | all-round, slowest |
| 5 | Wedge | one at the point, two behind, three behind them | 90% | horse and wolf riders (their default) |
| 6 | Column | two abreast | 100% | the column of march |
| 7 | Loose | a wide, ragged skirmish line | 100% | wizards, healers, fliers, berserkers (their default) |
| 8 | Encircle | a wide crescent (220 degrees) around the point itself, every man facing outward | 90% | pinning a lone champion, or ringing a hero |
| 9 | V | a V with its point at the back and its arms reaching forward at 45 degrees (each man 0.8 gaps forward and out of the last, 1.13 gaps apart): the opposite of the Wedge | 90% | drawing a charge onto the point and closing on its flanks; a wide front from few men |
| 0 | **Scatter** | not a formation: everyone selected runs flat out 7-10 m directly away from the group's centre (the man at the centre picks his own way) | full speed | the moment a bomb, stone or fireball is in the air |

Spacing is the biggest selected soldier's size (2.6 x his radius, at least 1.8 m); Staggered and Loose open it wider, so the loose formations are also the safe ones against blasts. A squad marches at its slowest member's pace times the formation's pace above: the tight formations a little slower than the loose ones. The computer forms its squads the same way (archers and grenadiers Staggered, foot in Line, horse in a Wedge, wizards and healers Loose).

**Other orders.** **Scatter** (button, S, or keypad 0), **Hold** (H) stops, **Find** (F) brings the camera to the selection, **A** attack mode, **M** force-march mode, **Esc** cancels the mode or lets go of the selection, **Space** pauses.

### 4.2 Unit Identity, Progression & Attrition

- **Stack Limit (from *Warlords*):** At most **8 squads** may occupy a map hex, and an attack commits at most 8. A defended city (2×2 hexes) fights with everything inside it, up to **32 squads**. This caps the largest possible battle at **40 squads** (~480 soldiers at ~12 per squad), which is what keeps several simultaneous full-physics battles inside the frame budget (§5.2).
- **Named Units:** Every unit batch recruited receives a unique name (e.g., *1st Oakhaven Footmen*, *The Grey Watch*).
- **Slow Recruitment Cycle:** Units take multiple turns/minutes to produce. Replacing a wiped-out veteran squad is extremely costly, encouraging strategic retreats and tactical preservation. Rare troops are never trained at all (§4.2.4, "Every soldier counts").
- **One number, as in Warlords.** Every squad shows a single **strength from 1 to 9** on its banner (the HUD card's shield badge), and that is all a player needs to read. A fresh squad has its type's strength (light infantry 3, heavy cavalry 6, dragons 9: docs/UNITS.md); everything below adds one to it, never past 9.
- **Ranks (soldiers):** kills promote a soldier from recruit to **regular** (2 kills), **veteran** (5) and **elite** (10), shown as one to three chevrons under the strength. Each rank: +10% damage, +3% chance to turn a blow with a shield, a 15% tighter arrow spread. A squad shows its survivors' average rank, so losses cost rank as well as men.
- **Levels (heroes):** every side starts the campaign with its capital and **one hero**, free. Heroes level from 1 to 9 by kills (3, 7, 12, 18, 25, 33, 42, 52); the badge reads "Lv". Each level: +6% damage and +8% hit points. A hero's Warlords strength is 3 + level, and it **leads** the soldiers within 30 m exactly as in Warlords: strength 4-6 gives 1 leadership point, 7-8 two, 9 three, each +5% damage.
- **Blessings:** each of the four temples blesses a stack once (§4.4), shown as a gold star; each counts like one more rank. As in Warlords, blessings stack to the cap of 9.
- **Artifacts:** battle artifacts add to a hero's own strength; command artifacts add to the leadership of the stack (§4.4).
- **Hover a card** for the full story: type, rank or level, blessings and the tally of kills.

### 4.2.1 Unit Roster

Every army type from *Warlords* (with Warlords II's terrain bonuses) plus Myth's signature units. Stats live in `engine/sim/src/units.cpp`.

| Unit | Origin | Character |
|---|---|---|
| Light Infantry, Heavy Infantry | Warlords | Line troops; heavy infantry carry big shields |
| Elven Archers | Warlords | Longbows; +1 in woods in Warlords |
| Pikemen | Warlords | Long reach, big bonus against mounted troops; +1 in the open |
| Light Cavalry, Heavy Cavalry | Warlords | Fast; heavy cavalry lance, barding, knockback; +1 / +2 in the open |
| Dwarven Legion | Warlords | Axes and shields; +1 in the hills |
| Wolf Riders | Warlords | Fast goblin riders |
| Giants, Minotaurs | Warlords | Huge, slow, knock soldiers flying |
| Giant Spiders | Warlords | Fast, many-legged |
| Griffins, Pegasi | Warlords | Fliers: cruise high, swoop down to fight |
| Elephants | Warlords | Enormous, trample and toss |
| Unicorns | Warlords | Fast, piercing horn |
| Wizards | Warlords allies | Fireballs that explode on impact |
| Dragons | Warlords allies | Flying, fireballs, devastating melee |
| Devils, Demons | Warlords allies | Hulking melee monsters |
| Undead | Warlords allies | Relentless, no blood, bones instead |
| Archons | Warlords allies | Winged angels |
| Hero | Warlords | A named champion |
| Dwarven Grenadiers | Myth | Satchel charges that bounce, roll and explode on a fuse; chain reactions |
| Journeymen | Myth | Healers with a limited number of charges |
| Warriors | Myth | Sword and shield; shields block from the front only |
| Berserkers | Myth | Fast, no shield, hit hard |
| Catapults | Siege engine | A wheeled mangonel: slow, no melee, throws a stone 150 m that smashes gates, walls' defenders and anyone near where it lands |

Exact numbers (hit points, speeds, damage, attack rates, ranges, flight) are locked in [`UNITS.md`](UNITS.md), generated from the code and pinned by a test.

**Fliers** walk by default. Only moves longer than about 50 m make them take off, and they need a ground run first. They cruise over the terrain low enough for giants to swat and archers to shoot, tire, and must land to recover. They never attack from the air; flight is purely a movement advantage.

**Missile troops** have a real maximum range (archers 70 m, grenadiers 19 m). Ordered to shoot farther, they walk into range first.

**Catapults** (siege engines) roll at 0.8 m/s, cannot fight hand to hand, and reload for 14 s. The arm always slams the same padded crossbar, so the stone leaves at a fixed 53 degrees and the crew wind the skein for the range: out to 150 m, never nearer than 30 m (it would come down on them). The wind that carries arrows does not move a 15 kg stone. A stone lands in a plunging arc and smashes everything within 3 m: 100 damage at the centre, falling off to the edge, throwing men off their feet and off the wall-walks (the parapet is no shelter from above). It does five times that to a gate. Idle catapults throw at any foe within 140 m. Tap a catapult, then a gate, and it batters the gate until it falls, then stands down. Their figure is a timber bed on four spoked wheels whose wheels turn as it rolls, the arm whipping up to the crossbar as it throws, and a heap of timbers when it is destroyed.

**Bodies have space and mass.** Soldiers cannot overlap, the lighter one gives way, corpses are shoved aside, and trees, rocks, houses and deep water block movement. Loose body parts are physical objects that walkers nudge aside. Arrows are different: stuck or lying, they stay exactly where they fell until an explosion throws them.

**Explosions, as in Myth.** A blast throws body parts long and low. Each one strikes the ground, hops on and skids to a stop, leaving a splat of blood at every strike and a dotted smear along the skid. The result is the red trails radiating out from the blast, each ending where the part came to rest. The scorch is a ragged star of blackened spokes. All of it stays (§5.5).

### 4.2.2 Cities & Castles

Following Warlords: **80 cities**, and whoever holds two thirds of them (or all of them) wins.
- **Starting cities:** each of the eight sides starts with only its capital, a castle with heavier walls, a tall keep and more towers. The other 72 cities start neutral, held by their own garrisons.
- **What a city gives:** it produces armies (a few unit types per city, each with a build time) and pays gold to its owner every turn. Owners can raze it, which turns it into a ruin forever (§5.5).
- **Siege:** an attack commits at most 8 squads, but a city defends with everything inside it, up to 32 (§4.2). In the 3D battle the walls, towers and gates are real obstacles. Fliers clear walls, soldiers hack at gates, grenadiers' satchels and catapult stones breach them.
- **Siege engines in the campaign:** catapults are trained at the capitals of the Aurelian Throne, the Ironhold Dwarves and Lord Malvoren (6 days, 40 gold, 2 engines to an army; the one kind of special army a city still builds, §4.2.4; wheels want roads: 6 movement points a day, slow in woods and hills, no marshes). They are weak in the field (strength 3), but in a Warlords dice battle against a city each catapult in the attacking stack takes a point off the defenders' bonus for the walls (down to nothing), and catapults fight two points stronger there. In the 3D battle the attackers' catapults are brought along to the siege (the Siege scenario gives the besiegers two); the enemy's battle AI never marches its engines at the foe, but turns them on the player's gate when it besieges a town of the player's.

- **Every castle is different:** square, long, many-sided or shield-shaped curtain walls; capitals add an inner bailey round a great keep. Drum towers with battlements stand at the corners and either side of the gate. Inside are a keep, streets of houses, and a courtyard behind the gate kept open for mustering troops and siege engines.
- **Gates are shut:** every gate is two leaves of oak banded with iron, closed to everyone (the defenders are already inside, as in Myth). While it stands it blocks walkers and missiles like the wall, and the pathfinder treats it as closed; a march ordered into the town leads up to its face. It has 250 hit points per metre of width (an 8 m town gate: 2000). Attackers at a shut gate hack at it when their orders lead through it, or when they stand idle there with no foe on their side: a blow does a quarter of its damage to the timbers, so a legion of dwarves needs well over a minute. Blasts do far more: satchels three times their damage, fireballs one and a half, catapult stones five times (three or four good stones break a gate). A broken gate stays broken: the leaves hang shattered from their hinges, the timbers lie in the passage, and everyone on the march finds the new way in. The town's own side never hacks at its gate. Gate hit points are part of the simulation state and its checksum.
- **Walls are real in battle:** they stop soldiers and missiles; the only way in on foot is the gate, once it is broken (or over the top, for fliers). Stairs against the inner faces lead up to a wall-walk 1.3 m under the wall's nominal height. Walls themselves do not break (stones kill the men on them, and break gates).
- **The wall-walk and its parapet** (`castle_walls.cpp`, `world_walls.cpp`): along the walk's outer edge stands a parapet 0.6 m deep: a low breastwork 0.5 m high (about knee high) the whole way, and merlons up to 2 m (higher than a man) between embrasures 1 m wide every 4 m, kept clear of the towers. The sim's missiles strike exactly this stone (an exact box test of each tick's flight against the slab, the breastwork and the merlons, open at the embrasures), and the renderer draws the merlons from the same list, so what is drawn is what stops an arrow.
  - **One archer to an embrasure:** a soldier on the walk shoots out over the wall only from an embrasure (he leans into it, his arrow leaving past the wall's face); behind a merlon he cannot. A town of ~560 m of wall has about 120 embrasures. Garrison archers are placed one to an embrasure, spread round the walls; the rest wait at the foot of a stair, and the battle AI sends them up to any embrasure left empty (by the dead, or never manned).
  - **Exposure by the angle:** a defender at an embrasure stands against the breastwork, so about three quarters of him shows to a flat shot from far out; shots from close under the wall rise steeply and meet the parapet before him. Archers shooting up at a defender aim at his chest, shoot direct (never lobbed over), and only when the flight clears the parapet up to him. The defenders' advantages are height (a metre more reach per metre above the target, and plunging shots hit harder: world_combat.cpp) and so shooting first.
  - **Standing and falling on the walk:** soldiers keep inside the parapet (never in its stone), the walk being the ground there. A body thrown against the parapet stops there; one thrown above it goes over and falls to the wall's foot. The dead lie along the walk, never across it into the stone, and so does the way the death matrix laid them down (§5.5.1: a defender shot dead on the walk lies along it with the arrow pinned in him; a limb cut off there lands on the walk, and body parts on a deck never roll away down the hillside under the castle).
- **Broken gates and missiles:** once a gate is broken, missiles and soldiers pass freely through the passage; only the stone over it (from the doors' lintel to the walk) still stops them. Arrows stuck in the doors fall when they break. Missiles flying through a gate passage, or under any deck, are not stopped by the walk overhead (they land on what lies below them). Arrows that strike stone (walls, towers, houses, rocks) glance and fall to its foot; they stick only in wood (trees, timbers, gates).
- **Neutral towns resist:** each neutral town keeps a small garrison (in the siege battle: a watch of archers along the walls, pikes at the gate, militia in the courtyard and the castellan's guard at the keep). A garrison holds its town: it fights whoever comes near its post and never marches out. Every side starts with only its capital and must take the rest.

| Side | Capital | After (Warlords archetype) | Banner |
|---|---|---|---|
| The Aurelian Throne | Kingshold | Knights of the central realm | White |
| The Highcrag Giants | Highcrag | Giants of the north-west heights | Gold |
| The Ironhold Dwarves | Deepforge | Dwarves of the southern mountains | Red |
| The Orcs of Grath | Grathmaw | Orc hordes of the eastern spurs | Orange |
| The Sylvaran Elves | Sylvaris | Elves of Northwood | Green |
| The Sunreach Riders | Sunreach | Horse lords of the Valdor plains | Tan |
| The Moonward Order | Moonhaven | Mystic order of Eastreach | Blue |
| Lord Malvoren | Dreadspire | The dark lord of the south-east | Black |

Names, factions and the map are original (Q5); only the structure follows Warlords.

### 4.2.3 Gold (as in Warlords)

Money is gold only, as in Warlords:
- **Income:** every city pays its owner gold each campaign day; bigger and richer cities pay more. Capturing a city takes over its income; sacking it pays a one-off haul but leaves it poorer; razing it ends it.
- **Production:** a city can build a few unit types. Each type costs gold to set up in that city, and each unit costs gold to build. Barracks in a city decide which types it can train.
- **Upkeep:** every unit costs gold each day. A side that cannot pay sees units desert, so a large army needs a large realm behind it.
- **Other gold:** heroes find it in ruins; heroes for hire cost it (§4.4).

### 4.2.4 The Campaign (built)

*Built so far* (`engine/sim/src/campaign.cpp`, the **Campaign** button or `?campaign`):
- **The start:** each of the eight sides holds its capital, one hero (with an escort of light infantry) and a war chest (the Dwarves richest, the Giants poorest, after Warlords). The other 72 cities are neutral, each with a garrison of one to four armies, more where the walls are stronger.
- **The map:** a 200 m grid over the continent, each cell road, bridge, water, forest, hill, mountain, plain, marsh or city (road cells are every cell a road passes through). Movement costs and movement points per day come from the Warlords tables; mountains and water are closed to all but fliers, rivers are crossed only at fords and bridges, roads are cheapest, so a stack sent to a far town keeps to the road where that is quicker (a test checks the road cells follow the roads and that stacks use them). A hero goes where the stack goes (on a griffin, over the peaks).
- **Time:** real time with pause; a campaign day is 30 seconds at normal speed (2×, 4×, 8× faster).
- **Gold:** each city pays every day (capitals 30, towns 21 to 33 by their walls); each common army costs a quarter of its training price a day in upkeep (rare ones what Warlords asked: giants 4, griffins 8...; allies nothing); a side that cannot pay loses its newest armies, green common troops before veterans and rare ones. Taking a city takes the loser's share of the treasury in proportion to its cities, half of it to the victor.
- **Training:** a city trains one army at a time, common troops only (light infantry everywhere; capitals their side's specialties; towns what the land around them raises: dwarves, heavy infantry and pikemen in the hills, archers, wolf riders and warriors in the woods, horse and foot on the plains). It is paid for when training starts; open a city's panel by tapping it.
- **Every soldier counts** (the hybrid of Warlords' production and the tactical guide's "every soldier is an irreplaceable asset"; `engine/sim/src/campaign_rare.cpp`):
  - **Common troops are slow and dear.** Light infantry 2 days and 12 gold (Warlords: 1 day, 4), heavy and pike infantry and archers 4 days and 20, dwarves and warriors 4 and 24, light cavalry and wolf riders 5 and 28, heavy cavalry 6 and 36. An army lost is weeks of a city's work.
  - **Rare troops are never trained or replaced:** heroes; the beasts and monsters (giants, minotaurs, giant spiders, griffins, pegasi, elephants, unicorns); Warlords' allies (wizards, dragons, devils, demons, undead, archons); and Myth's specialists (dwarven grenadiers, journeymen, berserkers), the guide's "Special & Rare Units". Wolf riders and Myth's warriors stay common: Warlords trains wolf riders in its towns, and warriors are plain sword and shield.
  - **Where rare troops come from:** each realm's hero marches with its own at the start (the Aurelian Throne a flight of pegasi, the Highcrag Giants two giants, the Ironhold Dwarves griffins and grenadiers, the Orcs a minotaur and berserkers, the Elves unicorns and pegasi, the Sunreach Riders elephants, the Moonward Order wizards and journeymen, Lord Malvoren spiders and a minotaur); every capital holds a **one-off levy** of one or two more, and so do a few towns (about one in twelve: giants, griffins or minotaurs in the hills, spiders, unicorns or berserkers in the woods, elephants, pegasi or grenadiers on the plains), for whoever holds them, at 3 days and 5 gold a point of strength each, once; the **temples**' guards (archons, journeymen, unicorns, pegasi) follow the first hero to kneel there, blessed and for no pay; a **hero for hire** brings a band of griffins, pegasi, unicorns, wizards, berserkers or grenadiers one time in three (50 gold more); and the **ruins** give allies as before (undead, wizards, devils, archons, dragons, demons).
  - **Siege engines stay buildable** at the three capitals that had them (6 days, 40 gold): walls are worth more than any army, so without engines no capital would ever fall.
  - **Veterans carry over.** Ranks come from kills (§4.2), in the dice and in 3D, and stay with the army from battle to battle. The army cards show them as pips (◇◇◇ recruit, ◆◇◇ regular, ◆◆◇ veteran, ◆◆◆ elite) with the tally of kills, gold for veterans; rare armies have a violet border and say so; a banner on the map shows ◆ and the number of its veterans; disbanding a veteran or rare army warns that it cannot be replaced.
  - **The city panel** lists only what the city can train, marks a levy (violet, with how many are left), and folds out the list of rare types and where each can be had (realm, levy, ruins, temples, heroes).
- **Fighting:** stacks of up to eight armies fight Warlords' dice battles where they meet: weakest first, one on one, ten-sided dice against the opponent's strength, two hits destroy an army. Fliers, special armies and command artifacts add Warlords' whole points, city walls help defenders, and the small edges (home land, familiar ground, veterans, a hero leading, high ground, holding a temple or tower: §3.4) a few percent each. Kills raise ranks and hero levels exactly as in the 3D battle. Beating a town's defenders does not take the town: the victors must hold it while their banner rises (§4.2.8).
- **The computer lords:** a strategic AI with two levels, picked when the campaign starts; see §4.2.6.
- **Victory:** as in Warlords, the last side standing; and, as in its §14 "Surrender", the others give in to a side holding more than half the cities (41 of 80) when no rival is within 15 of it.
- **The player's battles:** when the player's stack marches into an enemy, the campaign stops and asks: **Fight it** or **Resolve**. Resolve rolls the dice. Fight it opens the real 3D battle on that exact ground with those exact armies: their numbers, ranks and heroes. A town is held from its walls, with archers on the walk and the rest at the gate, courtyard and keep. In the field the two lines form up a bowshot apart. **Charge** sends every squad at the nearest enemy, with archers stopping at a long bowshot. When it is won, lost or withdrawn from, the survivors go back on the map with their kills and new ranks. Winning a town by taking its courtyard (§3.5) captures it at once; winning it by killing every defender leaves the army in the town to raise its banner (§4.2.8); withdrawing counts as being thrown back. The prompt shows whose land it is and each side's edges.
- **Heroes, ruins, temples, sages and heroes for hire:** see §4.4.

### 4.2.5 Ports & Boats (built)

After Warlords' navies, "the only units that can travel the seas and rivers ... a navy can carry a maximum of seven other armies" and "ignores the movement points of the armies it carries" (`engine/sim/src/campaign_sea.cpp`):
- **Navigable water:** every water cell of the campaign grid in a body of at least 24 cells (about 1 km²): on this continent the Sunderwave Sea along the west coast and round the north. The lakes and rivers are too narrow on the 200 m grid to sail; rivers stay edges crossed at fords and bridges. A road reaching into the sea does not dam it.
- **Ports:** cities with navigable water within two cells (nine of the eighty, all on the sea). Each port's **harbour** is its nearest water cell, where its boats put out and come in. A port has a pier below its walls on the map.
- **Building:** a port's panel has **Build a boat**: 20 gold, ready in 4 days, 2 gold a day upkeep; it runs beside the city's army training. Boats are named (the *Grey Swan*, the *Sea Hawk*...).
- **Capacity:** 7 armies, a Warlords navy's load, one short of a full stack. A stack of eight boarding leaves its weakest army (never a hero) on the quay.
- **Sailing:** a stack in a port with a free boat of its side boards it by stepping onto the water (**Embark**, which also takes friends idle in the port aboard up to seven). Afloat it moves 40 points a day at 2 a water cell: 20 cells, 4 km, a day, whatever it carries (light infantry walk 5 cells a day on the plain, 10 on a road). It lands on any coast it can walk (**Land**, or by being ordered inland), and sails straight into a friendly port from its harbour.
- **Route finding:** an order plans by land and, when the side has a free boat, also by a search over (cell, afloat) that may walk to a port, board, sail and land; it takes the boat only when that is quicker (from the northern port of Thistledown to Whitecliff in the far south: 4 days by sea, 11 on foot).
- **After landing:** the boat ties up if it landed in a port of its side, else rides at anchor off that coast, where a stack of its side next to it can board it again.
- **Losses:** a boat whose armies all fall goes down with them. Armies fighting from a deck fight a point weaker (both sides, at sea). An empty boat in a port that falls changes hands; one at anchor is burnt by an enemy boat that reaches it.
- **The 3D battle:** a battle fought from or against a boat has no ground to fight on, so it is settled with the dice (the prompt offers only **Resolve**). A stack that lands first and then attacks fights its 3D battle as usual.
- **The computer lords** build a boat at a port when some city they could take is out of reach on foot or much quicker by water (60% of the walking time or less), keep at most three, and sail stacks of three or more from a port with a boat (leaving a guard) at the best such city; a stack aboard sails for the nearest city it can take. `Campaign::runSeaAi`, called from the first line of `runAi`.
- **Determinism:** boats, ports, the harbour cells, navigable water and the voyage count are all in `Campaign::checksum()`.

### 4.2.6 The Computer Lords (built)

`engine/sim/src/campaign_ai.cpp` (`Campaign::runAi`, once a day for each computer side, after the heroes' quests and the boats). Like Warlords' computer players it sees the whole map and plays by the player's rules, with no secret bonuses.
- **What it knows:** for each of its cities, the enemy stacks marching on it or idle in the field within reach, and the exact chance (`Campaign::winChance`, the Warlords dice worked out state by state, with every modifier of the manual's appendix: `Campaign::lineup`) that they would take it. For each city it does not hold: its worth (income, a neutral town early, an enemy one later, a capital, the leader's cities, and how close it lies to its own), its defenders, and the chance a given stack has of taking it. Distances are in days of marching (a Dijkstra field per city over the movement table, computed once from the map; boats through `planRoute`).
- **Defence:** a city keeps as many guards as it takes to hold off the worst threat at the odds it accepts (one when nothing threatens it), green common troops first. A city that cannot be held even so keeps everything, unless all but one guard could march out and destroy the threat itself, which they do. It trains its quickest army; stacks that can get there before the enemy are pulled back, as many as it takes, unless the city is lost anyway. Stacks in the field strike enemy stacks near its cities when the odds are good. Gold left over (400 beyond the cost) raises the walls of frontier towns (BUILD).
- **Attack:** the armies not on guard join into strike stacks of up to eight, strongest first (veterans, rare troops and the hero lead), siege engines in the first. A stack attacks the best prize for the risk within four and a half days that it takes with good odds; a small stack joins a bigger one nearby when together they could take a prize neither can alone (the bigger one waits a day for it). Late in the war it gangs up on a runaway leader. Rear cities send what they train to the city nearest the next prize (LOC).
- **Veterans and rare troops** (`Campaign::precious`: rank veteran or elite, or a rare type) are what the Warlord cannot replace: a stack needs 6% better odds for each it would risk (to 85% at most); one setting out from a city leaves them behind, the best first, whenever the attack would still have the odds without them; the guards are green troops; disbanding and desertion take green common troops first. It takes every levy it holds (quick, strong and once only), sends its heroes after ruins, temple guards and sages, and hires heroes for hire.
- **Production and gold:** it trains what fights best for the days it takes, the days counted twice now that training is slow (an army's worth in the dice is s(10-5) / 5(10-s) against a strength-5 foe; the score is that × 20 / days² - cost: in the race for the towns an army today beats a better one next week), the quickest under threat, a few catapults for the walled towns of the later war; it stops training when the armies would cost more of the income than it can bear, and disbands its weakest armies rather than see them desert.
- **Heroes:** `questAi` (§4.4) sends them exploring when it is safe; otherwise they lead the first strike stack and need better odds (a hero is worth more than a battle) before they attack.
- **Easy and Hard** (`Difficulty`, per side, in the checksum; `setComputerDifficulty`): the player picks with the two buttons shown when **Campaign** is tapped (`?campaign=easy` for Easy, `?campaign` Hard). Easy is Warlords' Knight, which "deliberately wastes money and sometimes forgets what it is doing": it loses a fifth of its income, forgets a stack one day in four, picks a random target one time in three, attacks at 30% odds, sees an attack coming only at the gates, trains whatever takes its fancy (the quickest with an enemy at the gates; once the quickest army was the dumb choice, but with slow training it is the best one), never gathers stacks, sends recruits to the front, builds walls or minds its veterans. Hard is the Warlord: attacks at 40% (45% with a hero, more for veterans and rare troops; with armies slow to train, bolder pays better than it did, measured over many seeded all-computer games), reacts two days out to a stack marching on it but does not count an enemy sitting in its own walls as a threat (that kept every army home), and uses everything above; no gold bonus, as the manual promises. Computer sides default to Hard.
- **Tests** (`tests/test_campaign_ai.cpp`): the dice odds are exact; seat for seat over seeds 2 and 3 after 40 days, Hard lords hold at least half as many cities again as Easy ones and are ahead in at least 11 of the 16 seats, and lose fewer veterans and rare troops per battle (now 290 cities to 143, ahead in 12 of 16, 78 lost in 1169 fights against 81 in 680; with banners to raise and the finer odds of the small edges, 35% threw armies away at the walls, so the Warlord now wants 40%); a capital with a stack half a day out is saved from an attack that would otherwise take it; nobody's armies desert while their side holds a realm. `tests/test_campaign_rare.cpp`: cities train only common troops (and levies); every rare type has a source; each realm starts with its rare troops; a levy is mustered once; a temple's guard follows the first hero.

### 4.2.7 Warlords Rules: How We Follow Them

From the Warlords (1990) manual, rule by rule. "Ours" says where we differ and why.

| Rule (manual section) | Warlords | Ours |
|---|---|---|
| Combat order (App. 5) | Weakest on the left fight first, one on one; heroes, the most valuable, last | Same (`Campaign::order`) |
| Dice (App. 5) | Both roll d10; low (≤ the other's strength) against high is a hit on the low roller; two hits destroy | Same; odds computed exactly for the AI |
| AFCM / DFCM (App. 1-2) | Hero 4-6: +1, 7-8: +2, 9: +3; flier +1; special army +1; command items; empire's terrain likes | Fliers, special armies and command items as the manual; a hero's leadership (+2/+4/+6%) and the empire's likes (+2% a point, by side: knights, giants, dwarves, orcs, elves, horse lords, the Selentine-like order, the dark lord) are small edges (§3.4), not whole points |
| Army type and terrain (App. 3) | Forest, hill and marsh modifiers per type, AS/DS never past 9 | The pluses are a familiar-ground edge (+4%); the minuses are dropped (the ground already costs movement); AS/DS never past 9 nor below 1, in hundredths |
| City defence (App. 2h) | Defence 0-1: +0, 2-6: +1, 7-8: +2, 9: +3 | Same; capitals 6, towns 3-6 |
| Towers (App. 2f), temple or ruin (App. 2g) | +2, +1 | Outpost towers at the river crossings (§4.2.9); a defender holding a tower, temple or ruin has a +4% edge |
| Siege lowers walls (§12) | Each successful siege: defence -1 | Same (to 1 at least) |
| BUILD (§4.3.2) | Raise a city's defence for gold | 25 × (defence + 1) gold, to 9; city panel button |
| Attack cost (§10.1) | Two movement points | Same, whatever the ground |
| Movement table (App.) | Per type and terrain, P = prohibited | Same table; wizards walk as light infantry, demons and devils as the manual; no shore column (boats, §4.2.5) |
| Stack movement (§9.3) | Slowest army's points, dearest army's cost; a hero uses the stack's costs, not its points | Same |
| Stacks (§5.2, §9.2) | 8 a square; no moving through a square that would hold more; 32 in a city | Same: 8 a cell, 32 in a city; a full city trains no more |
| Production (§8) | One army at a time, paid when it starts; LOC sends it to another city two turns later, four cities at most to one | Same (`vectorProduction`; the AI uses it); only common troops, about twice as slow and three to five times as dear; rare troops never (§4.2.4) |
| Allies (§11) | Join a hero from a ruin, or with a hero for hire | Also each realm's start, one-off levies and the temples' guards (§4.2.4) |
| Upkeep (§7.1) | Half the training cost a turn | A quarter of the (dearer) training cost; rare troops half their old Warlords price |
| Lack of money (§7.2) | No training; armies cannot be paid | Armies desert, newest first; DISBAND (`Campaign::disband`) |
| Capture (§7.1) | The loser loses its treasury's share for that city, half to the victor | Same, when the victors' banner has risen over the town (§4.2.8) |
| Victory (§2, §14) | All 80 cities; surrender when the leader has more than 40 and no rival within 15 | Last side standing, or that surrender |
| Computer players (§4.1) | Knight to Warlord, no secret bonuses; Knights waste money and forget | Easy and Hard, §4.2.6 |
| Strengths and speeds (App. city table) | Per city, e.g. heavy infantry 5, pegasi 4-5 | Per type (docs/UNITS.md), heavy infantry 4, pegasi 6: kept to match the 3D battle's units |
| Hatreds (§4.3.4) | Computer players' attitudes to each side | Not built; the AI gangs up on the leader instead |
| RAZE (§4.3.2) | Destroy a city for good | Not built (the state has `razed`) |
| Capital lost (§4.3.2) | A new capital is chosen | Not built: no rule depends on the capital after the start |

### 4.2.8 Raising a Banner (built)

A town changes hands only by being held (`engine/sim/src/campaign_capture.cpp`; tests in `tests/test_capture.cpp`). As a player reads it:
- **Beat the defenders, then hold the town.** When you win the battle for a town (the dice, or the 3D battle won by killing or routing its defenders), or march into a town nobody defends, your army stands in it and **raises its banner**. The town is not yours yet: it is yours when the banner reaches the top.
- **More troops raise it faster.** Each tick it climbs 2 + one point for each army in the town (eight at most) of 480: a lone army takes 160 ticks (about 13 hours of campaign time, 16 s at normal speed), four armies 80 (about 6 hours), a full stack 48 (about 4 hours).
- **Enemies near stop it.** Armies of any other side in the town or the eight cells around it contest it: while they are as many as yours or fewer the banner stops where it is; while they are more it comes down as fast as it would rise for them.
- **Leaving gives it up.** If your armies march out (or fall), the attempt is abandoned and its progress lost; march back in and it starts again from nothing.
- **At the top** the town is yours, with the Warlords capture rules: the loser's share of the treasury (half of it to you), the walls one lower, its training stopped, and the town's own hex and the six round it are yours (§4.2.9). A 3D battle won at the courtyard's capture point (§3.5) raises the banner there and then.
- **A town being taken trains nothing** and takes in no new armies (LOC deliveries, heroes for hire) until the raisers are gone.
- **Shown:** the stack's own pennant climbs its pole as its banner rises; under the town's name a bar in the raiser's colour says "Your banner rising · 45%", "held up" (amber) or "coming down" (red); over the town's keep a tall pole carries the old colours down and the new ones up; the news says "our banner goes up over Westmarch", "our banner flies over Westmarch" (or "the Orcs of Grath raise their banner over Grathmaw!" and "Grathmaw fell to the Orcs of Grath" when it is yours being taken); the town's panel (open it with the stack in the town) shows how far and who holds it up; the town's tower banners change to the new owner's colour.
- **The computer lords** hold a town they have won until the banner is up (a stack raising a banner takes no other orders); a Hard lord whose town an enemy starts raising its banner in answers at once (`Campaign::bannerAlarm`): the nearest stack that could beat the raisers marches on them, or, if none could, the nearest that can reach the walls stands next to the town to hold the banner up; otherwise the daily defence treats an enemy in one of its towns as a threat (it is: the odds are 1000) and sends what it takes. An Easy lord hears of it at dawn.
- **Determinism:** each town's raiser and progress are in `Campaign::checksum()`.

### 4.2.9 Territory and Outposts (built)

After the little towers with flags that Warlords sets by its river crossings (`engine/sim/src/campaign_territory.cpp`; tests in `tests/test_territory.cpp`):
- **The hexes** are the ones the campaign map draws: pointy-top, 400 m between centres (about three and a half of the 200 m cells), odd rows half a hex east, 33 columns by 47 rows over the continent (1551, of them 1120 with land). Each cell belongs to the hex its centre falls in.
- **Claiming:** ten times a day (every 30 ticks) each land hex is reckoned. Where one side's armies stand and nobody else's, it is claimed for that side: 10 points, and 5 more for each army (four at most), to 100; another side's claim is worn away first, then the hex changes colour. Where two sides stand, nothing changes. Where nobody stands, it loses a point (a full claim fades in ten days) and is nobody's at zero.
- **Towns** claim their own hex and the six round it as two armies would (so a town's land is its lord's unless an enemy stands on it); taking a town hands you that ring at half a claim at once.
- **Outpost towers** stand at the 24 river crossings (fords and bridges; one to a hex): an outpost's hex is claimed twice as fast, and once held (half a claim or more) it claims the six hexes round it as one army would. A defender in an outpost's hex fights with the +4% holding edge. On the map each shows as ♜ with its name, in its holder's colour, and as a flag on a pole.
- **Worth:** the home-land edge (+3%) in battle (§3.4), on the map and in 3D (the battlefield's hex decides); and a little gold: one a day for every five hexes held (a realm of 30 hexes: 6 gold, against 20-30 for each town). The computer lords count it: the gold is in their income, and a town is worth a point more to them for every two hexes round it not yet theirs.
- **On the map:** each hex is washed faintly with its holder's colour, stronger along its rim and the firmer the claim (a texel a hex in a 33×47 texture, looked up in the land shaders: nothing to draw per hex, cheap on a phone); the top bar shows "⚑ 12", the hexes you hold.
- **Determinism:** every hex's holder and claim are in `Campaign::checksum()`.

### 4.4 Heroes, Ruins, Temples & Sages

Heroes are what make the campaign more than a war of production. A hero is a single named champion who leads a stack (adding to every squad's strength, as in Warlords). Above all, a hero **explores**.

- **Ruins (34):** a hero alone may search a ruin. Inside waits a guardian, fought as a small real-time battle in the ruin's own 3D interior: the hero, maybe a companion, against a monster or a crypt full of undead. If the hero dies, they are gone, along with everything they carried. A hero who wins finds one of these rewards:
  - gold;
  - an artifact, which gives the hero's stack a bonus or a power such as flight, a strength bonus, or a ward against fireballs;
  - allies that join the hero's stack: dragons, wizards, devils, archons or undead (§4.2.1 already has them all).

  Each ruin is searched once; after that it lies empty for good (the land remembers, §5.5).
- **Temples (4):** blessings. A blessed stack gains strength for its next battles, which in the 3D battle means block arc, poise and morale (Q4). A temple may also hand out a quest whose reward is an artifact.
- **Sages (2):** the Seer's Tower on its island and the Hermit of the Peaks. Each tells the first hero to reach them where artifacts or unsearched ruins lie, so it is first come, first served.
- **New heroes** offer their service now and then, for gold, to sides that hold enough cities.

*Built so far* (`engine/sim/src/campaign_quests.cpp`, in the campaign with the dice; the guardian fight in a 3D ruin interior is still to come):
- **Searching:** a stack standing still on a ruin, temple or sage shows a **Search** (ruin), **Visit** (temple) or **Consult** (sage) button. Ruins and sages need a hero in the stack; the strongest hero goes in.
- **Ruins:** the hero fights the guardian alone with Warlords' dice (a d10 against the other's strength, two hits kill). Guardians run from a pack of wolves (strength 2) to an ancient wyrm (8), fiercer the further the ruin lies from the nearest capital, so a young hero starts near home. A hero who falls is gone; what they carried lies in the ruin for the next hero, and the guardian waits on. A hero who wins gains the kills (and maybe a level) and finds one reward: gold (100 to 300, more behind a fiercer guardian), an artifact, or allies (undead or wizards behind weak guardians, devils or archons, then dragons) who join the stack and draw no pay. Each ruin is searched once.
- **Artifacts (15, each unique):** battle items add to the hero's own strength (+1: Firebrand, Frostblade, Spear of Ashkar; +2: Sunblade, Shadowblade, Bow of the Morning Star; +3: Staff of Thunders); command items add to every army fighting beside the hero, as Warlords' command modifier (+1: Crown of Kings, Sceptre of Dawn, Scarlet Banner; +2: Ring of Dominion, Horn of the North; +3: Rod of the High Lords); the Winged Sandals add 4 movement a day to the stack and the Purse of Plenty pays 10 gold a day. Fiercer guardians guard greater artifacts. They show on the hero's card and carry into the 3D battle as extra strength. A hero who dies in battle drops them where they fell; the next hero to enter that cell takes them up.
- **Temples (4):** as in Warlords, any stack (no hero needed) is blessed: every army in it gains +1 strength, once per temple, so an army can carry up to four blessings (never past strength 9). Blessings carry into the 3D battle.
- **Sages (2):** the first hero to consult one is given 100 to 150 gold and told the name of the nearest unexplored ruin.
- **Heroes for hire:** at dawn a side with more than two cities for each hero it has may be offered a hero (one chance in twelve a day), waiting in one of its cities for 150 gold plus 100 for every hero already serving. The player sees **Hire for N gold** / **Decline**; the offer lasts until the next dawn. A side whose heroes have all fallen is offered another in time.
- **The computer lords** search whatever their heroes stand on, then send each hero (alone, if it is in a city, leaving the rest on guard) to the nearest ruin within 24 cells whose guardian is weaker than the hero, an unvisited sage, or (outside cities) a temple that has not yet blessed the stack, as long as no enemy stack is within 3 cells of it. They hire a hero when they can pay and keep 100 gold for training.
- **News:** what the player's heroes did is in the news ("Sir Aldric found the Crown of Kings in Sunken Halls"); explored ruins are greyed on the map.

- **Temples' guards and heroes' companions:** the first hero to visit each temple is followed by its guard (rare troops, blessed there, no pay); a hero for hire sometimes brings rare troops along (§4.2.4).

Still to settle: temple quests.

### 4.3 AI Generals & Attention

- Nobody can micromanage nine fronts. Any army can be delegated to an AI general (with a stance: hold, skirmish, assault, retreat at X% losses).
- Attention is the scarce resource: a "hot spot" control jumps the camera to the most dramatic fight in progress.

## 5. Technical Network Architecture

To keep hundreds of physical projectiles, dynamic ragdolls, and multi-unit interactions perfectly synchronized across multiplayer:

- **Deterministic Lockstep Engine:** All combat simulation logic runs at a fixed tick rate (e.g., 30 Hz). Player inputs are transmitted over the network and executed synchronously on all client simulation states.
- **Client-Side Interpolation:** Sprite animations, particle effects, and visual rendering run at native client framerates (60+ FPS) while interpolating between fixed simulation ticks.
- **Determinism requirements (implied by lockstep):**
  - Simulation uses **fixed-point math** (or strictly controlled floats) — never engine-native PhysX/Chaos for gameplay-relevant state.
  - A single seeded RNG stream owned by the simulation; render/VFX code uses a separate non-authoritative RNG.
  - Per-tick **state checksums** exchanged between peers for desync detection, plus input-log replays for debugging.
  - Ragdolls and cosmetic debris that don't affect gameplay may run in non-deterministic engine physics on each client.

### 5.1 Technology Decisions

| Area | Decision | Why |
|---|---|---|
| Engine | **Custom engine in C++20** | No licences or vendor tools needed; a deterministic lockstep simulation must be custom anyway, since neither Unity's nor Unreal's physics is bit-reproducible across machines. |
| Web build | **Emscripten → WebAssembly + WebGL 2**, hosted on itch.io | Lowest-friction release: playable in a browser with no download (the approach used by the Avara community port). |
| Native build | Same codebase compiled with Clang/GCC for Windows, macOS, Linux | Path to a Steam/GOG release without a rewrite. Windows builds use clang-cl or MinGW (the simulation needs 128-bit integers, which MSVC lacks). |
| Platform layer | SDL2 (window, input, audio) + **WebGPU** (via Dawn/wgpu natively), WebGL 2 fallback | WebGPU gives compute shaders for cosmetic physics and instancing for tens of thousands of soldiers; one API for browser and native. |
| Simulation math | 64-bit fixed point (16 fractional bits), PCG32 RNG, FNV-1a state checksums | Identical results on x86-64, ARM and WebAssembly. Enforced by CI, which compares a full simulation trace from every platform. |
| Networking | Lockstep inputs over WebSockets through a small relay server (WebRTC data channels later) | Browsers cannot open raw UDP sockets; lockstep sends only inputs, so bandwidth is tiny. |
| Threading | Single-threaded simulation at first | Web threads need cross-origin isolation (itch.io supports it, but it adds friction); the 30 Hz simulation fits comfortably on one core. |

### 5.2 Performance Budget (60 fps on laptop integrated graphics and mid-range phones)

- **Rendering is always 60 fps**, interpolated between 30 Hz simulation ticks. Only the battle the player is viewing is rendered; the minimap draws dots from simulation state.
- **One world, many independent battle zones.** When armies engage, the soldiers involved form a battle zone with its own spatial grid for hit tests; zones never test against each other. Measured on the first core (native, before optimisation): one 450-soldier battle costs 0.8 ms/tick, while nine such battles sharing one unpartitioned world cost 69 ms/tick, so partitioning is mandatory.
- **Marching armies are single entities.** An army that isn't fighting moves as one stack on the hex grid; its soldiers are only expanded into individually simulated bodies (deterministically, on every peer) when it engages, and collapsed again when the battle ends. This keeps tens of thousands of soldiers on the continent cheap.
- **Simulation tiers:**

| Tier | Applies to | Runs |
|---|---|---|
| Full + visible | Battles inside the camera view | Deterministic sim + cosmetic GPU physics (ragdolls, debris, gore) + rendering |
| Full, headless | Every other battle involving a human player | Deterministic sim only |
| Auto-resolve | AI-vs-AI battles away from human players | Warlords-style combat rolls on the campaign layer |

- **Worst case target:** 9 simultaneous 40-squad battles in under 4 ms/tick in the WebAssembly build **on the minimum-spec phone** (§5.6). In lockstep the slowest peer sets the pace, so the deterministic simulation's caps are sized for that phone, not for a laptop. CI will fail the build when a benchmark exceeds the budget scaled to that device.

### 5.3 GPU Physics: Cosmetic Only

GPU physics can't be used for gameplay outcomes. GPUs from different vendors, and even different drivers, produce slightly different floating-point results, and parallel work completes in a nondeterministic order, so lockstep peers would desync within seconds. The split is:

- **Gameplay physics (CPU, fixed-point, deterministic):** projectiles, hit detection, shield blocks, dodges, knockback, unit movement, wall collapse *states*. Kept deliberately simple (capsules, cylinders, heightfield) so the CPU can afford thousands of soldiers.
- **Cosmetic physics (GPU, float, per-client):** ragdolls, flying debris chunks, blood and gore particles, dust, grass, cloth banners, water splashes. Only simulated for the area near the camera, and free to differ between players because they never affect the outcome.
- **API:** WebGPU compute shaders (available in current Chrome, Edge and Safari, with Firefox catching up) with a CPU fallback at reduced particle counts for browsers on WebGL 2, which has no compute shaders. Native builds use the same WebGPU code through Dawn or wgpu.
- **Budget:** a laptop integrated GPU comfortably handles ~100k cosmetic particles and a few hundred simplified ragdolls per frame, alongside instanced sprite rendering for 20k–50k soldiers. The low-poly, sprite-heavy art style is what makes this possible.

### 5.4 Object Tiers (tens of thousands of things on the field)

Every object in the world belongs to exactly one tier. The tier decides who simulates it and what it may touch.

| Tier | Examples | Simulated by | Per-tick cost | Budget (whole continent) |
|---|---|---|---|---|
| **A. Gameplay, moving** | Soldiers, arrows and bolts in flight, satchel charges, thrown molotovs, rolling boulders, siege engines | CPU, fixed-point, every peer | Full | ~5k soldiers engaged + ~3k projectiles at once |
| **B. Gameplay, static** | Trees, rocks, boulders at rest, houses, walls, barrels, fences, bridges | CPU, fixed-point, every peer | **~Zero** until hit; lives in the static spatial grid | 200k+ |
| **C. Cosmetic, moving** | Severed limbs, heads, dropped shields and swords, debris chunks, splinters, sparks, blood, smoke, dust, fire | GPU compute, float, per client, near the camera only | GPU only | ~5–10k rigid pieces + ~100–250k particles in view |
| **D. Cosmetic, settled** | Corpses, arrows stuck in the ground, blood decals, craters, scorched grass, rubble | Nothing: baked into instanced meshes/decals once at rest | None (rendering only) | Hundreds of thousands; merged/faded with distance and time |

**Rules**

1. **Coupling is one-way: gameplay drives cosmetics, never the reverse.** A satchel explosion (tier A) emits an event; every client's GPU blasts nearby limbs, shields and debris (tier C) outward. Those pieces can never block an arrow or trigger anything, so they may differ between players without desyncing.
2. **Destruction is a state change.** When a house or wall (tier B) takes enough damage, the deterministic sim switches it to its "collapsed" collider (rubble you can walk over, a breach in the wall) and emits an event. The flying bricks are tier C.
3. **Things that come to rest go to sleep.** A boulder that stops rolling drops from A to B; a limb that settles drops from C to D. Most objects on a battlefield spend most of their life asleep, which is what makes "tens of thousands" affordable.
4. **If it must affect gameplay, it must be tier A or B.** Example design calls: a barrel of oil that can be shot to explode is tier B; a dropped sword is tier C, unless picking up weapons becomes a mechanic.
5. **Everything persists, with a very long half-life** (§5.5).

**Measured (first broadphase prototype, native, single core, unoptimised):**

| Static colliders (B) | Soldiers (A) | Arrows in flight (A) | Tick |
|---|---|---|---|
| 50,000 | 500 | 500 | 0.15 ms |
| 50,000 | 4,300 | 3,000 | 0.7 ms |
| 200,000 | 4,300 | 3,000 | 1.5 ms |
| 200,000 | 10,000 | 10,000 | 6.2 ms (over budget once WebAssembly overhead is added) |

The middle rows cover the 9-sieges worst case from §5.2 with room to spare. Static object count barely matters; the active (tier A) count is what has to be capped.

**Rendering:** GPU-driven culling (a compute pass culls all instances and builds indirect draw lists) so 100k+ visible objects cost a handful of draw calls; trees, houses and rocks switch to impostors at strategic zoom.

### 5.5 Persistence: The Land Remembers

Nothing that happens on Illyria is cleaned up. Every corpse, arrow, crater, shattered shield and burnt house stays where it fell and **weathers slowly** over game time, so old battlefields become landmarks (*the Bone Field at Oakhaven Ford*).

**Two kinds of remains**

| Kind | Examples | Where it comes from | Same for every player? |
|---|---|---|---|
| **Major remains** | Corpses, arrows and bolts stuck in the ground, craters, rubble, wrecked siege engines, burnt buildings, felled trees | Already known exactly by the deterministic sim (death position, impact point, explosion centre, destruction event) | **Yes**: stored in the simulation state, included in saves, replays and late-join transfers, and usable by gameplay later |
| **Minor remains** | Severed limbs, splinters, dropped swords and shields | Where the GPU cosmetic physics happened to drop them | No: local to each client, saved per client, rebuilt approximately for late joiners |

**Blood stains**

Blood is not a pile of objects; it is **paint on the land**. Every wound, death, mutilation and explosion is already a deterministic sim event with a position, direction and force. Each one paints a stain pattern (pool, spray, drag trail) derived from that event and its seed, so every player sees the same stains, even though the flying droplets themselves are GPU-only.

- **Storage:** a sparse stain layer over the terrain at ~0.5 m per texel, allocated only in 256 m tiles where blood has actually been spilled. Each texel holds amount and the tick it was last stained. Tiles near the camera live on the GPU; the rest are kept compressed (mostly empty, so roughly tens of KB each) and streamed in.
- **Weathering:** bright red → dark brown → black → soaked into the soil over a very long half-life. Fresh blood is splashed on top of old, so a site fought over twice shows both. Rain speeds fading, snow covers it and thaws to reveal it again, and blood in a river or ford dilutes and drifts downstream.
- **On units and objects:** a squad's soldiers, shields and walls they fought against carry stains too; veterans look like veterans.
- **Wounded trails:** a wounded soldier walking away leaves drips. Because stains are deterministic, this can become gameplay (tracking a retreating enemy).
- **At god zoom:** stain tiles feed the per-hex scar texture, so the bloodiest fields of the war are visible from the sky.

**Weathering instead of deleting**

Each remain records the tick it was created and moves through visual stages on a very long, deterministic schedule. For corpses: fresh → bloated → rotted → skeleton → scattered bones → grass mound. Craters soften and green over; arrows topple, then rot to shafts and arrowheads; rubble moss over. The stage is a pure function of age, so all players see the same thing.

Nothing vanishes. Old remains **consolidate**: when a region's count passes its budget, the oldest items in a hex are merged into aggregate decals and meshes (a dense scatter of bones, a field of broken shafts). Individual objects become fewer, the scene looks the same, and the memory stays bounded.

**Budget arithmetic**

With remains created at rate *R* and half-life *H*, the steady-state population is *R × H / ln 2*. Example campaign: 200 battles × (400 dead + 5,000 arrows + 50 craters + rubble) ≈ 1.1 M major remains created.

| Representation | Size | 1.1 M remains |
|---|---|---|
| Compact record (type, quantised position and yaw, creation tick, stage seed) | 16 bytes | ~18 MB of RAM, ~5 MB compressed in a save |

That fits comfortably even in a browser. Consolidation keeps it bounded across arbitrarily long campaigns.

**Drawing it**

| Zoom | Remains drawn as |
|---|---|
| Ground / tactical | Individual instanced meshes and sprites near the camera, culled on the GPU |
| Strategic | Clustered impostors per hex (one "bone field" or "broken shafts" scatter) |
| God | A per-hex "scar" texture blended into the terrain: dark earth, craters, ruined settlements |

**Optional gameplay hooks** (major remains are deterministic, so these are possible later): morale penalty marching past fresh mass graves; carrion birds revealing where a battle happened; looting battlefields for arrows and gear; old fortifications and rubble giving cover in the next war; temples gaining or losing blessing power from bloodshed nearby.

### 5.5.1 The Death Matrix (built)

How a soldier dies decides what is left of him and how it sounds (`engine/sim/src/world_death.cpp`, tests in
`tests/test_death.cpp`). Every weapon has a damage type (`UnitStats`: melee and missile, in `docs/UNITS.md`):
**slash** (swords, sabres, claws), **cleave** (axes, great blades, a fiend's or dragon's claws), **blunt** (clubs,
staves, an elephant's weight, a catapult's stone), **pierce** (pikes, lances, fangs, horns, arrows), **explosive**
(satchel charges) and **fire** (fireballs). The killing blow's type, its direction and its **overkill** (how far below
zero health it went) choose the body:

| Killing blow | Body | Left behind |
|---|---|---|
| Slash past 12 overkill, cleave past 7 (man-sized bodies, mass under 4) | lies whole but for a limb, or past twice the threshold the head | the part, thrown on along the blow, bleeding as it skids; a pool where he fell |
| Explosive past 5 overkill | torn apart | head, trunk, limbs and gobbets (more for heavier bodies, thrown less far), flung from the blast centre, rolling down slopes too steep to grip, leaving blood trails; they can be kicked |
| Any blow but fire and arrows past 50 overkill | torn apart (a catapult stone landing on a man) | as above, burst from the side the blow came from |
| An arrow | whole, fallen the way the arrow flew | the arrow, pinned in him where it struck |
| Fire | whole, burned black | smoke rising from it for 25 s (presentation) |
| Anything else | whole, fallen the way the blow drove him | |

Arrows a shield turns stick in it half the time (up to 5), drawn on the shield. On a castle's wall-walk the body lies along the walk (§4.2.2), the matrix's direction turned to it. The death (kind, cause, direction,
overkill, the part cut off, the pinned arrow) is part of each unit's state and the checksum, and each death is
published as `World::deathEvents()` (unit, side, type, kind, cause, where, the blow's direction, overkill, killer) for
the sounds and voices.

### 5.5.2 Sound Effects (built)

No music in battle, as in Myth: every sound tells the player what is happening (`game/src/sfx.cpp`,
`game/web/sfx.js`; the list of every sound with how to record or source it is `docs/SOUND_EFFECTS.md`, generated from
`game/web/sfx/sounds.json`; tests in `tests/test_sfx.cpp`).

- **Cues from the simulation:** bowstrings, satchel throws and fuses, fireballs cast, catapults loosing (new missiles);
  arrows landing by surface (earth, wood, stone, water) and on what they hit (a shield's splintering crack, armour's
  ring, flesh's wet thwack, bone); blows by damage type and armour (a swing, then a wet slash, a metallic clash, a crush,
  a point going in, a shield's thud, bone); blasts, stones, gates struck and broken; and the death matrix (a
  dismembering snap, a gib burst then blood rain, a body's fall, armour's clatter, bones collapsing, burning flesh).
  Arrows passing within 7 m of a low camera whistle, pitched by their closing speed. Victory gets a fanfare, defeat a
  drone. Each sound plays at most once a tick, the loudest of its kind standing for a volley, with its own least
  gap; at most 6 cues a tick, the gravest and nearest first. They fade over 160 m (420 m for blasts, stones, gates and
  engines) with the camera's height counting against them; beyond hearing they cost nothing.
- **Ambience** (every half second): wind by the continent under the view (the high passes' howl by its mountain value,
  the lowlands' sweep, louder as the camera rises); swamp insects and the odd frog near marsh and mud, panned towards
  it; carrion flies over the dead near the view, 12 s after they fall, louder with more bodies; the enemy's war drums
  while he comes on beyond bowshot (and in held land: a siege), silent once the armies meet; rain and thunder by a
  weather hook (`Sfx::setRain`, `Module._illyria_set_rain`) until there is weather.
- **The voices:** cries of pain, death screams and the choking moan of the shot (by the death matrix), a grenadier's
  cackle when his satchel kills, the undead's groans, and the herald's "Casualties", "Severe casualties" and "Total
  casualties" at a quarter, half and four fifths of the player's soldiers lost (`docs/VOICE_LINES.md`). Cries have their
  own throttle (a third of a second apart) so they never crowd out the talk.
- **Playback:** Web Audio on the AudioContext shared with the voices (`game/web/audio.js`, unlocked on the first touch
  for iOS): 12 reused channel strips (filter, gain, pan, reverb send), the gravest cue taking the slightest strip;
  distance dulls and reverberates a sound; a compressor guards the master. Files load lazily, a sound's variants the
  first time it plays (a core set when audio starts). The ⚙ panel has sound effects on or off, their volume, ambience on
  or off (kept per device), and a link to `sfx-test.html`, where every sound and cry can be auditioned through the same
  chain, placed left, right, near or far, and rated with a note to copy back. It plays the game's own `sfx/` and
  `voice/` files (one source of sounds); the older address `sfx-test/` forwards to it.
- **The files:** rendered offline by `python3 tools/sfx/synth/render.py` (numpy, scipy, ffmpeg; deterministic seeds),
  layered as a sound designer would (transient, body, tail): Karplus-Strong strings, modal resonator banks for metal
  and wood, filtered noise for cuts, blood, wind and rain, membrane drums in a synthetic room, a convolution reverb for
  the outdoors; one-shots mono at -18 LUFS, loops stereo at -24 LUFS, made seamless; about 4 MB in all with the
  placeholder cries. `stats.json` records each file's length, peak, loudness and DC, checked by the tests. A
  recording dropped in by `tools/sfx/convert.sh` replaces any rendered file and is never overwritten.

### 5.6 Phones: Fully Playable

The complete game (campaign, battles, multiplayer) must be playable in a phone browser. A cut-down mobile companion doesn't count.

**Minimum spec (proposed):** iPhone 12 (A14, iOS 26 Safari) and Android phones of Snapdragon 7-series / Pixel 6a class (Chrome) from 2021 onward, ~4 GB RAM. WebGPU on both (Safari since iOS 26, Chrome on Android 12+), WebGL 2 fallback with reduced cosmetics.

**Graphics settings (built).** Everything that only changes the look can be switched off: the ⚙ panel, `Renderer::Feature`. Every setting simulates the same battle, so players on different settings stay in lockstep. Phones start on Medium; anything else starts on High.

| Feature | High | Medium | Low |
|---|---|---|---|
| Ground detail: grass, soil, flowers and stones painted into the ground, shade under trees | ✓ | ✓ | (meadow colour variation only) |
| Ground cover: grass tufts, flowers, pebbles, twigs and ferns around the camera | dense, ~42 m around the camera | ~33 m | off |
| Sun shadows from trees, buildings and soldiers | 2048², up to 900 m | 1024², up to 550 m | blob shadows |
| Detailed distant land: 16 m ring around the battlefield, ragged ground edges, crags, canopy | ✓ | ✓ | coarse colours |
| Close-up land off the battlefield at its full detail (with ground detail or cover on) | ✓ | ✓ | coarse land |
| Sky gradient, haze, drifting cloud shadows | ✓ | ✓ | flat sky |
| Resolution | up to 2× | up to 1.25× | up to 1.25× |

Tree, rock and bush variety is always on: it costs draw calls, not frame time.

**Performance on a phone**

| Concern | Response |
|---|---|
| Deterministic sim must fit the weakest peer | Tier A caps (§5.4) are set by min-spec phone measurements. Scenery, remains and blood (tiers B and D) don't affect this. |
| Thermal throttling (phones sustain ~50–70% of peak after a few minutes) | Budgets are measured *after* 10 minutes of sustained load, not on a cold device. |
| Locked 60 fps | Dynamic render resolution, per-device cosmetic budgets (particles, ragdolls, remains density), and impostors at closer range. The sim never changes; only the cosmetics scale. |
| Memory (iOS Safari kills tabs at roughly 1–1.5 GB) | Total budget under 700 MB; remains and blood tiles stream from compressed storage (§5.5). |
| Download size on mobile data | First playable under ~40 MB; regions, sounds and art stream in as the camera approaches. |
| Battery | 60 fps only while something moves; drop to a low-power idle when paused or on the strategic map with nothing happening. |

**Touch controls.** Pinch-to-zoom *is* the god-to-ground camera, which makes this game a natural fit for touch.

| Gesture | Action |
|---|---|
| Pinch | Zoom (god ↔ ground) |
| Two-finger drag / twist | Pan / rotate camera |
| One-finger drag | Pan |
| Tap a soldier / tap him again | Select him alone / every soldier of his type in view (§4.1.2) |
| Tap a card | Select that squad or saved group (again: the camera goes to it) |
| Tap ground / an enemy | March there / attack |
| Hold at the destination, then drag | Move with facing: the ghost formation turns along the line |
| Hold on your own men (or with nothing selected), then drag | Box-select |
| **+ Save group** card; long-press a group card | Save the selection as a card; put the selection in that card, or forget it |
| Formation button; Scatter button | Formation picker; everyone selected runs outward |
| Edge buttons (thumb reach) | Pause, hot-spot jump, charge, squad cards |

Pause (single-player) and AI generals (§4.3) carry most of the weight: touch is slower than a mouse, so the design rewards decisions over clicks per minute. All UI text scales and is readable at phone size.

**Phones in multiplayer.** Mobile browsers suspend background tabs, and phones drop between Wi-Fi and cellular. Lockstep can't stall a whole match for one phone, so:
- A player who drops is covered by their AI generals until they return.
- Reconnecting players resync from a state snapshot plus fast-forward simulation of the missed ticks.
- Input delay adapts to the worst current connection.

**Phone vs phone is the reference match.** The hardest supported case, two min-spec phones on cellular data playing each other, is what every multiplayer feature is designed and tested against. If it works there, PC players are covered.

| Problem | Response |
|---|---|
| Neither player can host (phones sit behind carrier NAT and get suspended in the background) | A small relay server owns the match: it orders and forwards inputs, keeps the full input log, and stores periodic state snapshots. No peer is the host. |
| Cellular latency (50–150 ms) and jitter | Adaptive input delay (~100–250 ms). Fine for this genre: orders are tactical, not twitch, and Myth ran on worse. The local player's orders get instant visual and audio acknowledgement so it never feels laggy. |
| Mobile data cost | Lockstep sends only inputs: about 1–5 KB/s, roughly 5–20 MB per hour. |
| One or both phones drop (tunnel, app switch, call) | The match keeps running on the relay; AI generals command a missing player's armies. In a 1v1 where *both* drop, the relay pauses the match. Rejoin = latest snapshot + fast-forward through the logged inputs. |
| Heat and battery over a long match | Thermal-sustained budgets (above); matches can be saved and resumed later by both players. |

**Testing.** Automated: CI runs two headless simulation peers through the relay with simulated cellular latency, jitter, packet loss and disconnects, and checks their checksums every tick. Manual: two real min-spec phones (one iPhone, one Android) playing each other over cellular data are the release gate for every milestone.

**Distribution.** itch.io HTML builds marked mobile-friendly; installable as a home-screen web app (PWA) for fullscreen on iPhone, where Safari has no fullscreen API. **Native iOS app if the web version succeeds.** To keep that cheap, the codebase stays native-ready from day one:
- The simulation, renderer and platform layer are plain C++ with no browser-only dependencies. SDL runs on iOS, and the WebGPU rendering code runs on Apple's Metal through Dawn or wgpu, so the iOS app is a new build target, not a port.
- Determinism across web and native is already enforced: CI's macOS ARM64 build uses the same CPU architecture and compiler family as iPhones. An iOS build job gets added once the platform layer exists, and native iPhone players can play web players.
- A native app (rather than a wrapped web page) avoids App Store rejection for "minimum functionality" web wrappers and gets full-screen display, better thermals and offline play.
- Any purchases sold inside the iOS app must go through Apple's in-app purchase system. The business model should be decided with that in mind.

## 6. Development Roadmap

> *Proposed — durations assume a small core team and are placeholders for planning.*

| Phase | Goal | Key Deliverables | Exit Criteria |
|---|---|---|---|
| **0. Pre-Production** (≈2 mo) | Lock tech decisions | ✅ Engine choice (§5.1); ✅ fixed-point math + deterministic RNG library; ✅ cross-platform determinism CI; ✅ platform layer running in desktop **and phone** browsers (WebGL 2; WebGPU later); min-spec phone benchmark; ✅ touch-control prototype; ✅ playable build auto-deployed to GitHub Pages with a visible version; art style test (one billboard unit, one 3D unit, one terrain tile) | Written tech decision record; style test approved; a build playable on itch.io |
| **1. Deterministic Core** (≈3 mo) | Headless simulation | Fixed 30 Hz tick loop; input command queue; checksums; replay recorder; heightfield terrain collision | Same replay produces identical checksums across two machines/OSes |
| **2. Combat Vertical Slice** (≈4 mo) | Prove the "Myth feel" | Ballistic projectiles + dispersion circle; shield cone deflection; dodge envelopes; flanking; 2 infantry types, 1 archer type, 1 large 3D creature; billboard depth pre-pass & shadows | 10 v 10 skirmish is fun and readable; 200 projectiles in flight at 30 Hz sim without frame drops |
| **3. Campaign Map Prototype** (≈3 mo) | Prove the "Warlords loop" | ✅ continent with terrain types, 80 cities, 8 capitals, ruins, temples, sages; armies, city production queues, gold; heroes searching ruins; named unit generator; veterancy; shrine blessings; map → battle → map transition with persistent casualties | One full campaign playable vs. simple AI with meaningful attrition decisions |
| **4. Multiplayer Alpha** (≈3 mo) | Lockstep over the wire | Peer-to-peer/relay lockstep; input delay tuning; desync reporting; lobby | 2–4 players complete battles over internet with zero unrecovered desyncs in test suite |
| **5. Content & AI** (≈6 mo) | Fill Illyria | Full faction rosters; full continent map; tactical + strategic AI; heroes & siege; mutilation/gore pass; audio | Feature complete; campaign completable for every faction |
| **6. Beta & Launch** (≈3 mo) | Ship | Balance, performance, Steam/GOG/itch.io builds, achievements, mod/replay sharing | Release candidate passes cert/QA; launch |

## 7. Open Questions

- ~~**Q1 — Engine:**~~ *Resolved:* custom C++ engine, web-first via WebAssembly (§5.1).
- ~~**Q2 — Campaign timing:**~~ *Resolved:* seamless real-time continent, with pause in single-player (§4.1). Open sub-question: the time scale. How fast armies march across the continent relative to how long a battle lasts.
- **Q3 — Recruitment time units:** "Turns" on the campaign map vs. "minutes" inside battles — define whether reinforcements can arrive mid-battle.
- **Q4 — Shield arc value:** Confirm default frontal shield cone (placeholder 120°) and how Temple Blessings (+15%) scale it.
- **Q5 — IP:** The campaign map and naming must be original to Illyria; avoid reproducing *Warlords* or *Myth* assets, maps, or names.

## Voices (built)

Soldiers speak, as in Myth (`game/src/voice.cpp`, `game/web/voice.js`; tests in `tests/test_voice.cpp`). Every line is listed once, in `game/web/voice/lines.json`: 14 voices (soldier, archer, dwarf, rider, hero, wizard, berserker, archon, engine crew, the wordless brute, undead, fiend and beast, and the herald who reports casualties), 36 situations (with the cries of pain and death of §5.5.2), 983 lines; the cries have rendered placeholders (`voice/placeholder/`) until they are recorded. The recording script `docs/VOICE_LINES.md` and its checklist are generated from it (`python3 tools/voice/script.py`); `tools/voice/convert.sh` turns recorded WAV takes into the game's MP3s.

- **What is said:** orders answered (selected, move, attack, charge; a hero answers for those he leads, else the most senior), the simulation's events (rally calls, a hero's fall on either side, breaking, wavering, rallying, kills and a veteran's taunts, promotions, bad wounds, arrows falling, blows in the flank or back, friendly fire, a grenadier's satchel, healing, the catapult throwing, a gate broken, first contact, riders breaking into the gallop and the men they ride at, victory and defeat) and rare idle chatter after 20 s without a blow. The enemy plays the same lines a little lower.
- **Throttles:** each situation has a cooldown per side (0.3 s for orders up to 12 s for arrows, 45 s for idle chatter); a speaker keeps quiet 5 s after he spoke; chatter (priority 2 and below) keeps 1.5 s apart and at most 5 in any 10 s; orders skip that gap; grave news (hold the line, a hero's rally, a fallen hero, a broken gate, victory, defeat) skips all but its own cooldown. Cues fade over 140 m from the view centre, the camera's height counting against them; beyond hearing they are dropped before they spend any throttle. Orders, victory, defeat and our own hero's fall are always heard. In the page at most 3 voices sound at once, the graver cutting in; a Ford battle says about 15-20 lines a minute out of a few hundred events.
- **Playback:** Web Audio, panned and scaled by distance; a voice's recordings are fetched the first time it speaks. Lines not yet recorded are read by the browser's speech synthesis (pitch and rate per voice), or for the wordless voices a short made-up growl, until the real file exists (`voice/available.json`). The ⚙ panel switches voices and the stand-ins on or off and sets the volume (kept per device). Audio starts on the first touch, as iOS requires.
