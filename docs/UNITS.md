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
| Archers | 4 | 45 | 1.80 | 0.40 | 1.0 | 2.4 | 5 | slash | 1.20 s | 0.5 | 0 |  | 110% |  |
| Pikemen | 4 | 65 | 1.80 | 0.40 | 1.0 | 2.0 | 14 | pierce | 1.60 s | 2.2 | 0 |  | 90% | +150% vs mounted; +3% in the open;  |
| Light Cavalry | 4 | 85 | 2.50 | 0.90 | 3.0 | 7.0 | 13 | slash | 1.20 s | 0.8 | 3 |  | 100% | mounted; +3% in the open;  |
| Heavy Cavalry | 6 | 130 | 2.70 | 0.95 | 4.0 | 5.5 | 20 | pierce | 1.50 s | 0.8 | 5 | 40% | 80% | mounted; +5% in the open;  |
| Wolf Riders | 5 | 75 | 1.90 | 0.80 | 2.0 | 7.5 | 12 | slash | 1.10 s | 0.6 | 2 |  | 100% | mounted;  |
| Giants | 6 | 350 | 5.50 | 1.20 | 8.0 | 3.0 | 45 | blunt | 2.20 s | 1.6 | 12 |  | 60% |  |
| Minotaurs | 6 | 230 | 3.00 | 0.90 | 4.0 | 3.0 | 32 | cleave | 1.70 s | 1.0 | 8 |  | 60% |  |
| Giant Spiders | 5 | 120 | 1.30 | 1.10 | 2.0 | 5.0 | 18 | pierce | 1.00 s | 0.6 | 0 |  | 70% |  |
| Griffins | 6 | 150 | 2.20 | 1.20 | 3.0 | 3.0 | 24 | slash | 1.20 s | 0.8 | 4 |  | 70% |  |
| Pegasi | 6 | 95 | 2.30 | 1.10 | 2.5 | 4.5 | 14 | slash | 1.20 s | 0.8 | 0 |  | 90% | mounted;  |
| Elephants | 7 | 450 | 4.00 | 1.80 | 12.0 | 3.2 | 38 | blunt | 2.20 s | 1.2 | 14 |  | 120% | mounted;  |
| Unicorns | 6 | 115 | 2.20 | 0.90 | 3.0 | 7.0 | 24 | pierce | 1.30 s | 1.0 | 3 |  | 70% |  |
| Wizards | 7 | 40 | 1.90 | 0.40 | 1.0 | 2.0 | 4 | blunt | 1.20 s | 0.5 | 0 |  | 100% |  |
| Dragons | 9 | 650 | 4.50 | 2.50 | 15.0 | 2.5 | 40 | cleave | 1.80 s | 1.2 | 10 |  | 25% |  |
| Devils | 8 | 270 | 3.00 | 1.00 | 4.0 | 3.2 | 36 | cleave | 1.50 s | 1.0 | 6 |  | 20% |  |
| Demons | 8 | 320 | 3.60 | 1.10 | 5.0 | 3.4 | 40 | cleave | 1.60 s | 1.2 | 8 |  | 30% |  |
| Undead | 5 | 70 | 1.80 | 0.40 | 1.0 | 1.8 | 11 | slash | 1.40 s | 0.6 | 0 |  | never | no blood;  |
| Archons | 8 | 170 | 2.40 | 0.80 | 2.0 | 2.8 | 26 | slash | 1.20 s | 0.8 | 3 |  | 30% |  |
| Hero | 4 | 260 | 2.00 | 0.50 | 2.0 | 2.8 | 30 | slash | 1.10 s | 0.8 | 3 | 55% | never |  |
| Dwarven Grenadiers | 4 | 70 | 1.30 | 0.45 | 1.0 | 1.6 | 5 | blunt | 1.20 s | 0.5 | 0 |  | 60% |  |
| Journeymen | 2 | 50 | 1.80 | 0.40 | 1.0 | 2.3 | 6 | blunt | 1.20 s | 0.5 | 0 |  | 120% | heals 35 every 2.5 s, 6 charges;  |
| Warriors | 5 | 80 | 1.90 | 0.45 | 1.2 | 2.2 | 17 | slash | 1.20 s | 0.6 | 0 | 65% | 85% |  |
| Berserkers | 5 | 85 | 1.90 | 0.45 | 1.2 | 3.6 | 25 | cleave | 0.93 s | 0.6 | 0 |  | 35% |  |
| Catapults | 3 | 380 | 2.50 | 1.60 | 25.0 | 1.4 | 0 |  | 0.00 s | 0.0 | 0 |  | 110% | no blood;  |
| Hyena-folk | 3 | 35 | 1.55 | 0.40 | 0.9 | 6.0 | 8 | slash | 1.00 s | 0.9 | 0 |  | never |  |

## Missile troops

Targets beyond **max range** are approached on foot first. Idle units fire on their own inside **auto range**,
but never at targets inside **min range**. A catapult's boulder flies a fixed 53 degree arc, smashes what it lands on
(its blast, below) and then rolls on over the ground, crushing the men in its way, friend and foe (see **The boulder**);
against a castle gate its smash does nine times its blast damage (satchels twice, fireballs one and a half).
**A Molotov has no blast** ("that is a satchel's job"): it shatters into a patch of burning ground (its radius below)
that burns for its fuse, hurting everyone who stands in it or walks through it (its damage, a second), shaking
their nerve and halving their pace there. Timber on or beside it catches: an engine burns on for 15 s at 20 a
second wherever it goes (a catapult standing in the patch burns to nothing on one bottle), and a gate in the patch burns at 25 a second
while it does (200 a bottle; a town gate has 2000). Its fire sets off any satchel it reaches: that is how the line
is sprung. Any unit may carry Molotovs (a count, like any store). A bowman with a
**draw** looses his first shot at a mark only once he has drawn on it, and draws afresh whenever he leaves it
(to march, to fight hand to hand); while he holds it, the draw comes inside his reload. A bowman aims at the body, leads a
moving man by his pace and the flight (misjudged), and lofts his arrow over his own line; a man going faster than
a march is past before many land square (docs/GAME_DESIGN.md §3.1).

**Nothing that bursts is carried without end.** **Carried** is the store a man brings into battle (a bowman's
quiver has no count); none is made there, and in a Conquest a load is made up again only at dawn in a town of
his own side. A man with a store is thrifty with it on his own: he throws at three or more men in the blast
(a great beast of 200 hp or more counts as three) or at a foe within 6 m of him (but none inside his own blast), never at men running away;
a Molotov goes at a foe's engine not alight, a foe's gate not burning, three or more men standing still in its
patch, or a satchel with foes on it; and a grenadier keeps his last Molotov for his satchels. An order (attack, Detonate) spends one whatever the
mark; Hold fire (Shift+K) keeps the store for orders alone. Out of missiles he has only his hands.

**Water.** A Molotov that lands in water (where it stands a hand deep or more) goes out: no fire, and nothing
set off. A satchel is not laid in water, and one that ends there is dead. A fireball bursting on water, or on
a man standing in it, goes up in steam: a third of its blast radius and a third of its damage. Left to
themselves, grenadiers do not throw at men in a ford, and wizards weigh them by the smaller blast.

**Rare.** Those who throw a blast come few (two dwarves, one wizard, one dragon to a squad; two engines to a
train, with eight stones between them), and an army has
few: at most 2 Dwarven Grenadiers, 1 Wizards, 1 Dragons, 3 Catapults (a Conquest realm keeps no more, and a
Skirmish host buys no more). A grenadier or a wizard with no order gives ground before a foe within 10 m,
to behind the nearest of his side's fighting men (who go to his help); told to hold, he stands. One bowman in
three picks out a foe's grenadier or wizard within 30 m before anyone else. A squad ordered to guard them
(comma, then click them) rings them and strikes what comes to the ring. An engine's stones go unasked only
at a gate, at four or more men in the lane a stone rolls down, or at three in its blast on a wall-walk.

| Unit | Missile | Wounds | Carried | Muzzle speed | Reload | Draw | Max range | Auto range | Damage | Spread at 100 m | Min range | Blast radius | Blast damage | Fuse |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Archers | arrow | pierce |  | 42 m/s | 4.0 s | 1.47 s | 70 m | 55 m | 16 | 6 m |  |  |  |  |
| Wizards | fireball | fire | 4 | 24 m/s | 7.0 s |  | 50 m | 45 m |  | 4 m |  | 4.5 m | 90 |  |
| Dragons | fireball | fire | 6 | 30 m/s | 4.0 s |  | 60 m | 50 m |  | 4 m |  | 5.0 m | 70 |  |
| Dwarven Grenadiers | Molotov | fire | 3 | 14 m/s | 5.0 s |  | 19 m | 16 m |  | 10 m |  | 3.0 m of fire | 10 a second | burns 8 s |
| Catapults | stone | blunt | 4 | 40 m/s | 14.0 s |  | 150 m | 140 m |  | 4 m | 30 m | 3.0 m | 100 |  |

## Satchel charges

Carried into battle and laid on the ground, where a satchel lies still and never goes off by itself. A blast or
a Molotov's or a fireball's fire reaching it sets it off a moment later (4 to 11 ticks), and its blast the next
(8 to 15 ticks): a line goes up one after another. A blast also shakes the morale of everyone left standing
within twice its radius.

**The trap.** In the deployment pause a charge is laid at once where it is ordered (within 160 m of him, 50 m clear
of any foe). In battle, Lay satchel (T, then click; Shift+click queues a line): he walks there, lays one and goes on to his
next order, or with none steps back out of its blast. Detonate (L, then click a satchel or the ground): of the
grenadiers and wizards selected, the nearest in reach throws one missile at once at the satchel nearest the click
(within 4 m), walking into reach first if none is; the missile is lobbed onto the charge, the wind allowed for.
A Detonate order does not wait for friends to clear the blast:
the ring over a laid satchel is red while one of yours stands in it, and he dies with the foe.

**Left to himself** a grenadier lays one in the path of a foe coming within 25 m (not yet within 6 m), clear of
his own men and of water, steps back out of its blast, and throws a Molotov at a satchel of his side's with two or more foes
on it and none of his friends (waiting up to 5 s for a friend to clear) before any one man; a wizard sparks
such a satchel too. Killed by fire or a blast with satchels still on him, he goes up with all of them at once:
a blast a quarter wider and half again as hard for each one beyond the first; any other death spills them,
unlit, where he falls.

| Unit | Carried | Blast radius | Blast damage | Between lays | All of them at once |
|---|---|---|---|---|---|
| Dwarven Grenadiers | 3 | 5.0 m | 150 | 3.0 s | 7.5 m, 300 |

## The boulder

A catapult's stone is a thing on the field (`engine/sim/src/world_boulder.cpp`, docs/GAME_DESIGN.md "The boulder").
It comes down with its smash (the blast above), bounces once and rolls on, 1.1 m across, until the ground has
taken its speed: far on a road, a little way in sand, mud and bog; faster and farther down a slope, soon stopped up
one; dead in water (a splash), and all but dead against a tree, a rock, a house, a tower, a wall or a shut gate.
Every man its middle passes within its own half width and his is struck once: hurt by 30 and 12 more for each metre a
second it is going (to 250), thrown, and shaken (those of his side within 5 m a third as much); slower than 3 m/s it
hurts no one. Each body takes its share of the stone's speed (a man a fifteenth, a horseman a sixth, a giant a third,
an elephant near half, an engine two thirds), and a body of mass 4 or more that leaves it under 3 m/s has stopped it.
It knows no side. Rolled into a gate it does half the blow of its speed (a stone from the air is 24 m/s). Where it
stops it lies, in nobody's way.

How far it rolls on from where it comes down, over bare level ground (the hop included), by how far it was thrown:

| Ground | Thrown 40 m | 100 m | 150 m |
|---|---|---|---|
| Road, bridge | 17 m | 45 m | 70 m |
| Plains | 11 m | 30 m | 45 m |
| Rock | 11 m | 30 m | 46 m |
| Forest (between the trees) | 7 m | 20 m | 30 m |
| Sand | 2 m | 8 m | 12 m |
| Mud | 1 m | 3 m | 5 m |
| Swamp | 1 m | 2 m | 3 m |
| Shallows, deep water | stops | stops | stops |

On plains, thrown 100 m along a slope (metres of rise in a hundred, the way it is thrown):

| Up 25 | Up 15 | Up 8 | Level | Down 8 | Down 15 | Down 25 | Down 35 |
|---|---|---|---|---|---|---|---|
| 21 m | 24 m | 27 m | 30 m | 34 m | 41 m | 79 m | to the foot of the hill (540 m here) |

(On grass a slope steeper than about 28 in a hundred pulls it harder than the ground holds it: it gathers speed, to 32 m/s at most.)

## Fliers

Fliers walk by default. Moves longer than the minimum flight take a ground run, then they cruise at
altitude over the terrain, tire, and must land to recover (rest restores stamina at half the rate
flight spends it). They never attack from the air. Cruise heights are within a giant's reach, and
arrows can hit them. On the conquest map a stack of fliers alone moves at the **long-run pace** of
that cycle, whatever is below: the take-off run (at half again its walking pace), a flight as long as
its endurance, then twice as long resting (`Conquest::flightPace`). A stack with a walker in it walks.

| Unit | Ground speed | Air speed | Cruise height | Take-off run | Min flight | Endurance | Long-run pace |
|---|---|---|---|---|---|---|---|
| Griffins | 3.0 m/s | 12 m/s | 6.5 m | 14 m | 50 m | 30 s | 4.02 m/s |
| Pegasi | 4.5 m/s | 14 m/s | 7.0 m | 18 m | 50 m | 40 s | 4.71 m/s |
| Dragons | 2.5 m/s | 15 m/s | 10.0 m | 30 m | 50 m | 25 s | 4.88 m/s |
| Archons | 2.8 m/s | 11 m/s | 6.0 m | 10 m | 50 m | 35 s | 3.68 m/s |

## Scavengers

Scavengers are soldiers like any other until ordered to raid (`engine/sim/src/world_scavenge.cpp`): they go
where they are sent, hold there and fight what comes at them. Let loose (the Raid order), each runs his own fight
from where he stands, knowing only what is within his **sight**: he takes the best thing lying there, darts in to
throw it at a soft man or a straggler, falls back to one place well clear, and goes again. The raid ends at
another order, when his pack has lost half its men, or after 15 s with nothing worth taking in sight.
Prizes are worth: a satchel charge 100, a blade 60, a head 45, a trunk 25, a limb 18, a bone or gobbet 12, less
2 for every metre of the run. Marks, by kind: archers, healers, wizards and grenadiers first (x3), other missile
troops (x2), then the rest, armoured men last (x0.3); the wounded up to twice; the straggler far from his
friends up to x2.5; a shield facing them less its chance to block; nearer is better. He goes only when his pack
about him (each at half again his fighting power) is a match for the foes within 10 m of the man (the armoured,
those quick enough to catch him and braced pikes counting double or more) and for the missile troops whose reach
covers where he would stand (at twice their power, by how deep in it): 55 in 100 to throw, 60 to cut a man down,
breaking off below 45. Out of missile reach first. He keeps out of reach of armoured foes (a shield of 30% or more,
or a heavy body: knights, warriors, cavalry, heroes, beasts). Thrown, a part hurts as any flying part does (2 to 8
by its speed, a trunk half again), a blade cuts (5 to 14), a head breaks the man it strikes (his morale to
nothing; the dead and heroes never break), and a satchel bursts where it strikes; a shield facing the throw
catches a part or a blade. A burning satchel is taken only with the fuse to throw it on. A soldier with no order
does not chase a scavenger quicker than him; struck by a throw, he turns his shield to the thrower.

The satchel run (`Command::TakeSatchel`, `Command::LightFuse`): ordered to a satchel lying on the ground, a
scavenger takes it up and carries it; any blast or fire reaching him sets it off in his hands (a Molotov's
burning ground too). Ordered to light it, he lights a 5 s fuse and sprints (30% quicker) at the man shown, deaf to
all else, and it goes off where he is; a lit satchel is never thrown away.

| Unit | Sight | Throw speed | Throw range | Darts in to | Spread at 100 m | Wind-up |
|---|---|---|---|---|---|---|
| Hyena-folk | 35 m | 17 m/s | 24 m | 14 m | 5 m | 0.3 s |

Blades are taken from the bodies of: Light Infantry, Heavy Infantry, Pikemen, Light Cavalry, Heavy Cavalry, Wolf Riders, Pegasi, Undead, Archons, Hero, Warriors, Berserkers, Hyena-folk.

## Ground

Terrain is tactical. A unit moves at its speed times its gait's multiplier for the ground under it
(`engine/sim/src/gait.cpp`), and its paths cost travel time with the same table, so each kind takes its
own fastest way: to the road and along it rather than straight over the fields, round a wood that
slows it more than the detour does. A road doubles every gait's pace. A bridge is walked at the pace
of the fields: Bridge is the ground of every deck, a castle's walls and ramps as well as the bridges
over rivers. Deep water is impassable on foot. Climbing divides speed by 1 + grade x 2.5 x the gait's
climb percentage; going down is up to 15% quicker; slopes steeper than 35% slow walkers a further
20%. Fliers walk like men. The conquest map marches by the same table and the same climbs, cell by
cell (`engine/sim/src/conquest_march.cpp`): a stack at the pace of its slowest army, a road cell as
road, a hill as plains to be climbed, marsh as swamp, mountains and water closed.

| Gait | Road | Plains | Forest | Swamp | Shallows | Rock | Mud | Sand | Bridge | Climb | Units |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Foot | 2.00 | 1.00 | 0.70 | 0.50 | 0.40 | 0.50 | 0.55 | 0.85 | 1.00 | 100% | Light Infantry, Heavy Infantry, Pikemen, Griffins, Pegasi, Wizards, Dragons, Archons, Hero, Journeymen, Warriors, Berserkers |
| Horse | 2.00 | 1.00 | 0.50 | 0.30 | 0.35 | 0.40 | 0.40 | 0.80 | 1.00 | 120% | Light Cavalry, Heavy Cavalry, Unicorns |
| Wolf | 2.00 | 1.00 | 0.85 | 0.35 | 0.35 | 0.55 | 0.45 | 0.80 | 1.00 | 90% | Wolf Riders |
| Dwarf | 2.00 | 1.00 | 0.70 | 0.50 | 0.35 | 0.80 | 0.55 | 0.85 | 1.00 | 50% | Dwarven Grenadiers |
| Woodland | 2.00 | 1.00 | 0.95 | 0.55 | 0.40 | 0.50 | 0.55 | 0.85 | 1.00 | 100% | Archers, Giant Spiders |
| Giant | 2.00 | 1.00 | 0.65 | 0.60 | 0.65 | 0.60 | 0.90 | 0.90 | 1.00 | 80% | Giants, Minotaurs, Elephants, Devils, Demons |
| Undead | 2.00 | 1.00 | 0.70 | 1.00 | 0.55 | 0.50 | 0.80 | 0.85 | 1.00 | 100% | Undead |
| Wheeled | 2.00 | 1.00 | 0.40 | 0.25 | 0.30 | 0.30 | 0.30 | 0.60 | 1.00 | 150% | Catapults |
| Hyena | 2.00 | 1.00 | 0.90 | 0.65 | 0.50 | 0.80 | 0.75 | 0.90 | 1.00 | 70% | Hyena-folk |
