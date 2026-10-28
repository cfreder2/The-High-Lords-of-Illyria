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

## 4. Macro Campaign Map & Unit Mechanics

### 4.1 The Seamless Continent ("God to Ground")

*Built so far:* the whole continent, 12.8 × 16 km ([MAP.md](MAP.md), `engine/sim/src/continent.cpp`):
- **Landscape:** a western sea, a great river that forks to divide the realms, mountain chains whose passes are chokepoints, and an unbroken eastern range. Swamps, forests, lakes, and roads crossing the rivers at four fords and four bridges.
- **Settlements:** 80 walled cities, 8 of them faction capitals (castles).
- **Places to explore (§4.4):** 34 ruins, 4 temples and 2 sages.
- **The battle window:** today's battle is fought at Oakhaven Ford, a full-detail 1 km window into the continent (`Region::fromContinent`). Wagon roads have ruts, an earthwork berm stands above the ford, a hamlet sits along the road, and rivers slope downhill.
- **Ground and movement:** ground types follow Warlords (road, plains, forest, swamp, shallows, deep water, rock, bridge) and set movement speed. Armies path around deep water and cross at fords and bridges.
- **Zoom:** the camera zooms from the whole continent down to a single soldier.
- **Close up anywhere (built):** wherever the camera looks closer than about 850 m, on the campaign map too, the land around it is drawn at the battlefield's own detail: 4 m heights, the same ground kinds, roads with ruts, forest floor, rock, snow, banks and water, and the same 3D grass, flowers and ferns (`game::DetailLand`). It is sampled exactly as `Region::fromContinent` cuts a battlefield out (a test checks they match), so a battle fought there later looks the same. It is built in 64 m chunks around the camera, nearest first, a couple of milliseconds a frame (all 144 chunks of the 768 m window take about 0.27 s of WebAssembly time); each new chunk fades in, and the window's edge and the zoom-out cross-fade into the coarse land by a screen dither, so nothing pops. The battlefield keeps priority where both exist, and the two meet without a step (the fine detail fades in from the battlefield's edge just as the battlefield fades it out). Off with ground detail and ground cover (Low).


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
- **The high ground:** a soldier striking down the slope deals up to 35% more damage (about +20% on a 1 in 3 slope) and throws the foe harder; striking up the slope deals up to 25% less. Arrows from a height fly further on their own (the ballistics are real).
- **Mud:** the trodden margins of rivers and ponds and the churned approaches to fords are mud, at a little over half speed: a ford is slow to cross and slower to climb out of.
- **Bridges:** stone road bridges on arches carry the roads over rivers (and small ones over every creek a road meets); narrow timber footbridges away from the roads let infantry over in single file. Bridge decks are real ground: soldiers walk on them, arrows land on them, and the pathfinder routes end to end along them.
- **Coasts and water:** beaches of sand along the sea; surf rolling in over the shallows; waterfalls where creeks drop steeply off the mountains.
- **Strategic consequences:** hills make the set-piece battles; fortresses on heights and at passes are hard to take; ruins and shrines in the mountains are reached only through the passes (§4.4).

### 4.2 Unit Identity, Progression & Attrition

- **Stack Limit (from *Warlords*):** At most **8 squads** may occupy a map hex, and an attack commits at most 8. A defended city (2×2 hexes) fights with everything inside it, up to **32 squads**. This caps the largest possible battle at **40 squads** (~480 soldiers at ~12 per squad), which is what keeps several simultaneous full-physics battles inside the frame budget (§5.2).
- **Named Units:** Every unit batch recruited receives a unique name (e.g., *1st Oakhaven Footmen*, *The Grey Watch*).
- **Slow Recruitment Cycle:** Units take multiple turns/minutes to produce. Replacing a wiped-out veteran squad is extremely costly, encouraging strategic retreats and tactical preservation.
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

**Catapults** (siege engines) roll at 0.8 m/s, cannot fight hand to hand, and reload for 14 s. The arm always slams the same padded crossbar, so the stone leaves at a fixed 53 degrees and the crew wind the skein for the range: out to 150 m, never nearer than 30 m (it would come down on them). The wind that carries arrows does not move a 15 kg stone. A stone lands in a plunging arc and smashes everything within 3 m: 100 damage at the centre, falling off to the edge, throwing men off their feet and off the wall-walks (the merlons are no shelter from above). It does five times that to a gate. Idle catapults throw at any foe within 140 m. Tap a catapult, then a gate, and it batters the gate until it falls, then stands down. Their figure is a timber bed on four spoked wheels whose wheels turn as it rolls, the arm whipping up to the crossbar as it throws, and a heap of timbers when it is destroyed.

**Bodies have space and mass.** Soldiers cannot overlap, the lighter one gives way, corpses are shoved aside, and trees, rocks, houses and deep water block movement. Loose body parts are physical objects that walkers nudge aside. Arrows are different: stuck or lying, they stay exactly where they fell until an explosion throws them.

**Explosions, as in Myth.** A blast throws body parts long and low. Each one strikes the ground, hops on and skids to a stop, leaving a splat of blood at every strike and a dotted smear along the skid. The result is the red trails radiating out from the blast, each ending where the part came to rest. The scorch is a ragged star of blackened spokes. All of it stays (§5.5).

### 4.2.2 Cities & Castles

Following Warlords: **80 cities**, and whoever holds two thirds of them (or all of them) wins.
- **Starting cities:** each of the eight sides starts with only its capital, a castle with heavier walls, a tall keep and more towers. The other 72 cities start neutral, held by their own garrisons.
- **What a city gives:** it produces armies (a few unit types per city, each with a build time) and pays gold to its owner every turn. Owners can raze it, which turns it into a ruin forever (§5.5).
- **Siege:** an attack commits at most 8 squads, but a city defends with everything inside it, up to 32 (§4.2). In the 3D battle the walls, towers and gates are real obstacles. Fliers clear walls, soldiers hack at gates, grenadiers' satchels and catapult stones breach them.
- **Siege engines in the campaign:** catapults are trained at the capitals of the Aurelian Throne, the Ironhold Dwarves and Lord Malvoren (5 days, 10 gold, 2 engines to an army; wheels want roads: 6 movement points a day, slow in woods and hills, no marshes). They are weak in the field (strength 3), but in a Warlords dice battle against a city each catapult in the attacking stack takes a point off the defenders' bonus for the walls (down to nothing), and catapults fight two points stronger there. In the 3D battle the attackers' catapults are brought along to the siege (the Siege scenario gives the besiegers two); the enemy's battle AI never marches its engines at the foe, but turns them on the player's gate when it besieges a town of the player's.

- **Every castle is different:** square, long, many-sided or shield-shaped curtain walls; capitals add an inner bailey round a great keep. Drum towers with battlements stand at the corners and either side of the gate. Inside are a keep, streets of houses, and a courtyard behind the gate kept open for mustering troops and siege engines.
- **Gates are shut:** every gate is two leaves of oak banded with iron, closed to everyone (the defenders are already inside, as in Myth). While it stands it blocks walkers and missiles like the wall, and the pathfinder treats it as closed; a march ordered into the town leads up to its face. It has 250 hit points per metre of width (an 8 m town gate: 2000). Attackers at a shut gate hack at it when their orders lead through it, or when they stand idle there with no foe on their side: a blow does a quarter of its damage to the timbers, so a legion of dwarves needs well over a minute. Blasts do far more: satchels three times their damage, fireballs one and a half, catapult stones five times (three or four good stones break a gate). A broken gate stays broken: the leaves hang shattered from their hinges, the timbers lie in the passage, and everyone on the march finds the new way in. The town's own side never hacks at its gate. Gate hit points are part of the simulation state and its checksum.
- **Walls are real in battle:** they stop soldiers and missiles; the only way in on foot is the gate, once it is broken (or over the top, for fliers). Stairs against the inner faces lead up to a wall-walk where archers stand and shoot over the merlons. The merlons stop half the missiles shot up at defenders from outside.
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
- **Gold:** each city pays every day (capitals 30, towns 21 to 33 by their walls); each army costs half its training price a day in upkeep; a side that cannot pay loses its newest armies. Taking a city takes the loser's share of the treasury in proportion to its cities, half of it to the victor.
- **Training:** a city trains one army at a time from up to four types (light infantry everywhere; capitals their side's specialties; towns what the land around them raises: dwarves and giants in the hills, archers and wolf riders in the woods, horse on the plains). It is paid for when training starts; open a city's panel by tapping it.
- **Fighting:** stacks of up to eight armies fight Warlords' dice battles where they meet: weakest first, one on one, ten-sided dice against the opponent's strength, two hits destroy an army. Heroes, fliers and special armies add Warlords' modifiers; city walls help defenders. Kills raise ranks and hero levels exactly as in the 3D battle.
- **The computer lords:** a strategic AI with two levels, picked when the campaign starts; see §4.2.6.
- **Victory:** as in Warlords, the last side standing; and, as in its §14 "Surrender", the others give in to a side holding more than half the cities (41 of 80) when no rival is within 15 of it.
- **The player's battles:** when the player's stack marches into an enemy, the campaign stops and asks: **Fight it** or **Resolve**. Resolve rolls the dice. Fight it opens the real 3D battle on that exact ground with those exact armies: their numbers, ranks and heroes. A town is held from its walls, with archers on the walk and the rest at the gate, courtyard and keep. In the field the two lines form up a bowshot apart. **Charge** sends every squad at the nearest enemy, with archers stopping at a long bowshot. When it is won, lost or withdrawn from, the survivors go back on the map with their kills and new ranks. Winning a town captures it; withdrawing counts as being thrown back.
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
- **Defence:** a threatened city keeps everything inside it and trains its quickest army; stacks that can get there before the enemy are pulled back, as many as it takes, unless the city is lost anyway. Stacks in the field strike enemy stacks near its cities when the odds are good. Gold left over raises the walls of frontier towns (BUILD).
- **Attack:** each city keeps one army (the weakest) as its garrison; the rest join into strike stacks of up to eight, strongest first, hero and siege engines in the first. A stack attacks the best prize for the risk within four and a half days that it takes with good odds; a small stack joins a bigger one nearby when together they could take a prize neither can alone (the bigger one waits a day for it). Late in the war it gangs up on a runaway leader. Rear cities send what they train to the city nearest the next prize (LOC).
- **Production and gold:** it trains what fights best for the days it takes (an army's worth in the dice is s(10-5) / 5(10-s) against a strength-5 foe), the quickest under threat, a few catapults for the walled towns of the later war; it stops training when the armies would cost more of the income than it can bear, and disbands its weakest armies rather than see them desert.
- **Heroes:** `questAi` (§4.4) sends them exploring when it is safe; otherwise they lead the first strike stack and need better odds (a hero is worth more than a battle) before they attack.
- **Easy and Hard** (`Difficulty`, per side, in the checksum; `setComputerDifficulty`): the player picks with the two buttons shown when **Campaign** is tapped (`?campaign=easy` for Easy, `?campaign` Hard). Easy is Warlords' Knight, which "deliberately wastes money and sometimes forgets what it is doing": it loses a fifth of its income, forgets a stack one day in four, picks a random target one time in three, attacks at 30% odds, sees an attack coming only at the gates, trains whatever is quickest, never gathers stacks, sends recruits to the front or builds walls. Hard is the Warlord: attacks at 45% (50% with a hero: bolder loses armies, more careful loses the race for the neutral towns, measured over many seeded all-computer games), reacts two days out, and uses everything above; no gold bonus, as the manual promises. Computer sides default to Hard.
- **Tests** (`tests/test_campaign_ai.cpp`): the dice odds are exact; Hard lords hold more cities than Easy ones after 40 days from the same seats swapped; a capital with a stack half a day out is saved from an attack that would otherwise take it; nobody's armies desert while their side holds a realm.

### 4.2.7 Warlords Rules: How We Follow Them

From the Warlords (1990) manual, rule by rule. "Ours" says where we differ and why.

| Rule (manual section) | Warlords | Ours |
|---|---|---|
| Combat order (App. 5) | Weakest on the left fight first, one on one; heroes, the most valuable, last | Same (`Campaign::order`) |
| Dice (App. 5) | Both roll d10; low (≤ the other's strength) against high is a hit on the low roller; two hits destroy | Same; odds computed exactly for the AI |
| AFCM / DFCM (App. 1-2) | Hero 4-6: +1, 7-8: +2, 9: +3; flier +1; special army +1; command items; empire's terrain likes | Same, empire likes by side (knights, giants, dwarves, orcs, elves, horse lords, the Selentine-like order, the dark lord) |
| Army type and terrain (App. 3) | Forest, hill and marsh modifiers per type, AS/DS never past 9 | Same (and never below 1) |
| City defence (App. 2h) | Defence 0-1: +0, 2-6: +1, 7-8: +2, 9: +3 | Same; capitals 6, towns 3-6 |
| Towers (App. 2f), temple or ruin (App. 2g) | +2, +1 | Not built: battles are never fought at sites; no towers |
| Siege lowers walls (§12) | Each successful siege: defence -1 | Same (to 1 at least) |
| BUILD (§4.3.2) | Raise a city's defence for gold | 25 × (defence + 1) gold, to 9; city panel button |
| Attack cost (§10.1) | Two movement points | Same, whatever the ground |
| Movement table (App.) | Per type and terrain, P = prohibited | Same table; wizards walk as light infantry, demons and devils as the manual; no shore column (boats, §4.2.5) |
| Stack movement (§9.3) | Slowest army's points, dearest army's cost; a hero uses the stack's costs, not its points | Same |
| Stacks (§5.2, §9.2) | 8 a square; no moving through a square that would hold more; 32 in a city | Same: 8 a cell, 32 in a city; a full city trains no more |
| Production (§8) | One army at a time, paid when it starts; LOC sends it to another city two turns later, four cities at most to one | Same (`vectorProduction`; the AI uses it, no player button yet) |
| Upkeep (§7.1) | Half the training cost a turn | Same; heavy infantry 4 gold as in the city table |
| Lack of money (§7.2) | No training; armies cannot be paid | Armies desert, newest first; DISBAND (`Campaign::disband`) |
| Capture (§7.1) | The loser loses its treasury's share for that city, half to the victor | Same |
| Victory (§2, §14) | All 80 cities; surrender when the leader has more than 40 and no rival within 15 | Last side standing, or that surrender |
| Computer players (§4.1) | Knight to Warlord, no secret bonuses; Knights waste money and forget | Easy and Hard, §4.2.6 |
| Strengths and speeds (App. city table) | Per city, e.g. heavy infantry 5, pegasi 4-5 | Per type (docs/UNITS.md), heavy infantry 4, pegasi 6: kept to match the 3D battle's units |
| Hatreds (§4.3.4) | Computer players' attitudes to each side | Not built; the AI gangs up on the leader instead |
| RAZE (§4.3.2) | Destroy a city for good | Not built (the state has `razed`) |
| Capital lost (§4.3.2) | A new capital is chosen | Not built: no rule depends on the capital after the start |

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

Still to settle: whether allies can later be recruited in cities (a Warlords II option); temple quests; heroes offering with allies of their own.

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
| Tap | Select squad or army; tap ground to move |
| One-finger drag from a selected squad | Draw a path or formation line |
| One-finger drag on empty ground | Box-select |
| Long-press | Context radial menu (attack ground, formation, stance, special ability) |
| Edge buttons (thumb reach) | Pause, hot-spot jump, AI general toggle, squad cards |

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
