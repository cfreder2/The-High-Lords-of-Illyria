# Unit Sheet

The locked stats for every unit type, generated from `engine/sim/src/units.cpp` by
`tools/unitsheet.cpp`. A checksum test (`unit_sheet_is_locked`) fails on any change, so
balance changes are always deliberate: change the code, regenerate this file, update the
checksum, all in one commit.

Scale: metres and seconds. A man is 1.8 m tall; a steady run is about 2.2 m/s and a gallop 7 m/s.

## Body and melee

**Strength** is the Warlords number (1 to 9) shown on the squad's banner; each hero level and blessing adds one, to 9 (a soldier's rank is a small edge instead: +3% a rank, docs/GAME_DESIGN.md §3.4).

**Morale** is how hard shocks shake the type, in percent of a man's (lower is steadier); "never" types never break.

Retired, no longer raised anywhere (the type number is kept for old saves and sprite sheets; old saves' armies of it load as grenadiers): Dwarven Legion.

| Unit | Strength | HP | Height | Radius | Mass | Speed | Melee dmg | Blow | Every | Reach | Knockback | Shield | Morale | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Light Infantry | 3 | 60 | 1.80 | 0.40 | 1.0 | 2.4 | 11 | slash | 1.30 s | 0.6 | 0 | 25% | 110% |  |
| Heavy Infantry | 4 | 90 | 1.90 | 0.45 | 1.3 | 1.9 | 15 | slash | 1.50 s | 0.6 | 0 | 55% | 85% |  |
| Elven Archers | 4 | 45 | 1.80 | 0.40 | 1.0 | 2.4 | 5 | slash | 1.20 s | 0.5 | 0 |  | 110% |  |
| Pikemen | 4 | 65 | 1.80 | 0.40 | 1.0 | 2.0 | 14 | pierce | 1.60 s | 2.2 | 0 |  | 90% | +150% vs mounted; +3% in the open;  |
| Light Cavalry | 4 | 85 | 2.50 | 0.90 | 3.0 | 7.0 | 13 | slash | 1.20 s | 0.8 | 3 |  | 100% | mounted; +3% in the open;  |
| Heavy Cavalry | 6 | 130 | 2.70 | 0.95 | 4.0 | 5.5 | 20 | pierce | 1.50 s | 0.8 | 5 | 40% | 80% | mounted; +5% in the open;  |
| Wolf Riders | 5 | 75 | 1.90 | 0.80 | 2.0 | 7.5 | 12 | slash | 1.10 s | 0.6 | 2 |  | 100% | mounted;  |
| Giants | 6 | 350 | 5.50 | 1.20 | 8.0 | 2.4 | 45 | blunt | 2.20 s | 1.6 | 12 |  | 60% |  |
| Minotaurs | 6 | 230 | 3.00 | 0.90 | 4.0 | 3.0 | 32 | cleave | 1.70 s | 1.0 | 8 |  | 60% |  |
| Giant Spiders | 5 | 120 | 1.30 | 1.10 | 2.0 | 5.0 | 18 | pierce | 1.00 s | 0.6 | 0 |  | 70% |  |
| Griffins | 6 | 150 | 2.20 | 1.20 | 3.0 | 3.0 | 24 | slash | 1.20 s | 0.8 | 4 |  | 70% |  |
| Pegasi | 6 | 95 | 2.30 | 1.10 | 2.5 | 4.5 | 14 | slash | 1.20 s | 0.8 | 0 |  | 90% | mounted;  |
| Elephants | 7 | 450 | 4.00 | 1.80 | 12.0 | 3.2 | 38 | blunt | 2.20 s | 1.2 | 14 |  | 120% | mounted;  |
| Unicorns | 6 | 115 | 2.20 | 0.90 | 3.0 | 7.0 | 24 | pierce | 1.30 s | 1.0 | 3 |  | 70% |  |
| Wizards | 7 | 45 | 1.90 | 0.40 | 1.0 | 2.0 | 4 | blunt | 1.20 s | 0.5 | 0 |  | 100% |  |
| Dragons | 9 | 650 | 4.50 | 2.50 | 15.0 | 2.5 | 40 | cleave | 1.80 s | 1.2 | 10 |  | 25% |  |
| Devils | 8 | 270 | 3.00 | 1.00 | 4.0 | 3.2 | 36 | cleave | 1.50 s | 1.0 | 6 |  | 20% |  |
| Demons | 8 | 320 | 3.60 | 1.10 | 5.0 | 3.4 | 40 | cleave | 1.60 s | 1.2 | 8 |  | 30% |  |
| Undead | 5 | 70 | 1.80 | 0.40 | 1.0 | 1.6 | 11 | slash | 1.40 s | 0.6 | 0 |  | never | no blood;  |
| Archons | 8 | 170 | 2.40 | 0.80 | 2.0 | 2.8 | 26 | slash | 1.20 s | 0.8 | 3 |  | 30% |  |
| Hero | 4 | 260 | 2.00 | 0.50 | 2.0 | 2.8 | 30 | slash | 1.10 s | 0.8 | 3 | 55% | never |  |
| Dwarven Grenadiers | 4 | 50 | 1.30 | 0.45 | 1.0 | 1.8 | 5 | blunt | 1.20 s | 0.5 | 0 |  | 100% |  |
| Journeymen | 2 | 50 | 1.80 | 0.40 | 1.0 | 2.3 | 6 | blunt | 1.20 s | 0.5 | 0 |  | 120% | heals 35 every 2.5 s, 6 charges;  |
| Warriors | 5 | 80 | 1.90 | 0.45 | 1.2 | 2.2 | 17 | slash | 1.20 s | 0.6 | 0 | 65% | 85% |  |
| Berserkers | 5 | 85 | 1.90 | 0.45 | 1.2 | 3.6 | 25 | cleave | 0.93 s | 0.6 | 0 |  | 35% |  |
| Catapults | 3 | 380 | 2.50 | 1.60 | 25.0 | 0.8 | 0 |  | 0.00 s | 0.0 | 0 |  | 110% | no blood;  |
| Hyena-folk | 3 | 35 | 1.55 | 0.40 | 0.9 | 6.0 | 8 | slash | 1.00 s | 0.9 | 0 |  | never |  |

## Missile troops

Targets beyond **max range** are approached on foot first. Idle units fire on their own inside **auto range**,
but never at targets inside **min range**. Catapult stones fly a fixed 53 degree arc and smash what they land on;
against a castle gate they do five times their blast damage (satchels three times, fireballs and Molotovs one and a half).
A Molotov shatters where it strikes into a burst of fire, and its fire sets off satchel charges.

| Unit | Missile | Wounds | Muzzle speed | Reload | Max range | Auto range | Damage | Spread at 100 m | Min range | Blast radius | Blast damage | Fuse |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Elven Archers | arrow | pierce | 42 m/s | 4.0 s | 70 m | 55 m | 16 | 6 m |  |  |  |  |
| Wizards | fireball | fire | 24 m/s | 9.0 s | 50 m | 45 m |  | 4 m |  | 3.5 m | 40 |  |
| Dragons | fireball | fire | 30 m/s | 6.0 s | 60 m | 50 m |  | 4 m |  | 5.0 m | 50 |  |
| Dwarven Grenadiers | Molotov | fire | 14 m/s | 5.0 s | 19 m | 16 m |  | 10 m |  | 3.0 m | 40 |  |
| Catapults | stone | blunt | 40 m/s | 14.0 s | 150 m | 140 m |  | 4 m | 30 m | 3.0 m | 100 |  |

## Satchel charges

Carried into battle and laid on the ground, where a satchel lies still and never goes off by itself. A blast or
a Molotov's fire reaching it sets it off a moment later, and its blast the next: chains. Left to himself a
grenadier lays one in the path of a foe coming within 25 m (not yet within 6 m), steps back out of its blast,
and throws his Molotovs at a satchel with foes beside it (and none of his friends) before any one man. Killed by
fire or a blast with satchels still on him, he goes up with all of them at once: a blast a quarter wider and half
again as hard for each one beyond the first; any other death spills them, unlit, where he falls.

| Unit | Carried | Blast radius | Blast damage | Between lays | All of them at once |
|---|---|---|---|---|---|
| Dwarven Grenadiers | 5 | 4.0 m | 80 | 4.0 s | 8.0 m, 240 |

## Fliers

Fliers walk by default. Moves longer than the minimum flight take a ground run, then they cruise at
altitude over the terrain, tire, and must land to recover (rest restores stamina at half the rate
flight spends it). They never attack from the air. Cruise heights are within a giant's reach, and
arrows can hit them.

| Unit | Ground speed | Air speed | Cruise height | Take-off run | Min flight | Endurance |
|---|---|---|---|---|---|---|
| Griffins | 3.0 m/s | 12 m/s | 6.5 m | 14 m | 50 m | 30 s |
| Pegasi | 4.5 m/s | 14 m/s | 7.0 m | 18 m | 50 m | 40 s |
| Dragons | 2.5 m/s | 15 m/s | 10.0 m | 30 m | 50 m | 25 s |
| Archons | 2.8 m/s | 11 m/s | 6.0 m | 10 m | 50 m | 35 s |

## Scavengers

Left to themselves, scavengers run their own fight (`engine/sim/src/world_scavenge.cpp`; orders override it):
round the end of the enemy's line toward its rear, taking the best thing in **sight**, darting in to throw it at
the softest man in reach, falling back and going for the next. Prizes are worth: a satchel charge 100, a blade 60,
a head 45, a trunk 25, a limb 18, a bone or gobbet 12, less 2 for every metre of the run. Marks, by kind:
archers, healers and wizards first (x3), other missile troops (x2), then the rest, armoured men last (x1/2);
nearer is better. They keep out of reach of armoured foes (a shield of 30% or more, or a heavy body: knights,
warriors, dwarves, cavalry, heroes, beasts), and with nothing to throw cut down the soft ones near them.
Thrown, a part hurts as any flying part does (2 to 8 by its speed, a trunk half again), a blade cuts (5 to 14),
a head breaks the man it strikes (his morale to nothing; the dead and heroes never break), and a satchel bursts
where it strikes. A burning satchel is taken only with the fuse to throw it on.

| Unit | Sight | Throw speed | Throw range | Darts in to | Spread at 100 m | Wind-up |
|---|---|---|---|---|---|---|
| Hyena-folk | 30 m | 17 m/s | 24 m | 14 m | 5 m | 0.3 s |

Blades are taken from the bodies of: Light Infantry, Heavy Infantry, Pikemen, Light Cavalry, Heavy Cavalry, Wolf Riders, Pegasi, Undead, Archons, Hero, Warriors, Berserkers, Hyena-folk.

## Ground

Terrain is tactical. A unit moves at its speed times its gait's multiplier for the ground under it
(`engine/sim/src/gait.cpp`), and its paths cost travel time with the same table, so each kind takes its
own fastest way: to the road and along it rather than straight over the fields, round a wood that
slows it more than the detour does. Deep water is impassable on foot. Climbing divides speed by
1 + grade x 2.5 x the gait's climb percentage; going down is up to 15% quicker; slopes steeper than
35% slow walkers a further 20%. Fliers walk like men.

| Gait | Road | Plains | Forest | Swamp | Shallows | Rock | Mud | Sand | Bridge | Climb | Units |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Foot | 1.25 | 1.00 | 0.70 | 0.50 | 0.40 | 0.50 | 0.55 | 0.85 | 1.00 | 100% | Light Infantry, Heavy Infantry, Pikemen, Griffins, Pegasi, Wizards, Dragons, Archons, Hero, Journeymen, Warriors, Berserkers |
| Horse | 1.30 | 1.00 | 0.50 | 0.30 | 0.35 | 0.40 | 0.40 | 0.80 | 1.00 | 120% | Light Cavalry, Heavy Cavalry, Unicorns |
| Wolf | 1.25 | 1.00 | 0.85 | 0.35 | 0.35 | 0.55 | 0.45 | 0.80 | 1.00 | 90% | Wolf Riders |
| Dwarf | 1.20 | 1.00 | 0.70 | 0.50 | 0.35 | 0.80 | 0.55 | 0.85 | 1.00 | 50% | Dwarven Grenadiers |
| Woodland | 1.25 | 1.00 | 0.95 | 0.55 | 0.40 | 0.50 | 0.55 | 0.85 | 1.00 | 100% | Elven Archers, Giant Spiders |
| Giant | 1.15 | 1.00 | 0.65 | 0.60 | 0.65 | 0.60 | 0.90 | 0.90 | 1.00 | 80% | Giants, Minotaurs, Elephants, Devils, Demons |
| Undead | 1.20 | 1.00 | 0.70 | 1.00 | 0.55 | 0.50 | 0.80 | 0.85 | 1.00 | 100% | Undead |
| Wheeled | 1.35 | 1.00 | 0.40 | 0.25 | 0.30 | 0.30 | 0.30 | 0.60 | 1.00 | 150% | Catapults |
| Hyena | 1.15 | 1.00 | 0.90 | 0.65 | 0.50 | 0.80 | 0.75 | 0.90 | 1.00 | 70% | Hyena-folk |
