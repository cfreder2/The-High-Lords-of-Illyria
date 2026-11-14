# Balance

*A pass on pacing, prices and matchups (2026-11-14). The tool is `tools/balance.cpp` (`illyria_balance`); every number below comes from it, deterministic, the same seeds before and after.*

## How it is measured

- `illyria_balance speeds`: each type walks 100 m on flat ground; the time, and the speed against a man's 2.4 m/s.
- `illyria_balance march`: a stack of one type is ordered from each of the 80 towns to its three nearest neighbours; the time is the Conquest's own `timeLeft` (a day is 120 s).
- `illyria_balance matrix [seeds] [gold]`: equal gold a side (60), at the Conquest price per army (`Conquest::price`), 90 m apart on flat ground. Melee types march on the enemy line; shooters hold and fire. Eight fights a pair cover both ends of the field, both player numbers and both spawn orders: the world resolves player 0 and the first-spawned man first, so a side raised first strikes first, and without alternating them even a mirror match was lopsided (the clumpcheck mirror run shows the same). A cell is the share of fights the **row** won (a win is the side with more value left unbroken). `mean` is the row's average over every other type; `men` is what 60 gold buys.
- `illyria_balance duel A B [seeds]` and `trace A B` for one pairing; `BAL_MEN_A` / `BAL_MEN_B` fix the numbers, to try a price before changing it.

Fights are decided by morale in seconds (a side that loses a few men breaks wholesale), so cells are mostly 0 or 100 and a type's strength moves in steps: read rows and columns, not single cells. Columns, in order: Light Inf, Heavy Inf, Archers, Pikes, Light Cav, Heavy Cav, Wolf, Giants, Minotaurs, Spiders, Griffins, Pegasi, Elephants, Unicorns, Wizards, Dragons, Devils, Demons, Undead, Archons, Hero, Grenadiers, Journeymen, Warriors, Berserkers, Catapults, Hyena-folk. (The retired legion is left out.)

## What was found

1. **Speed.** On foot a man crosses a 90 m field in 37 to 56 s, cavalry in 12 to 16 s, fliers in 9 to 14 s. The ratios (horse about 3x foot, heavy horse 2.3x, fliers 3.5 to 4x) read right and are left alone. Three types were off: the **catapult** at 0.8 m/s took 124 s for 100 m and, on the Conquest map, a median 3.6 minutes (p90 8 minutes) to the next towns; **giants** walked at a soldier's pace though their stride is three men's; **undead** at 1.6 were the slowest thing that is not an engine.
2. **Conquest marches.** Marches use the battle's speeds (docs/ONE_WORLD.md), so they follow from the table above. Nearest towns are a median 4 cells (176 m) apart; light infantry take a median 70 s (0.6 day, p90 143 s) to the three nearest, heavy foot 88 s, cavalry 24 s, giants 70 s. That is purposeful, not tedious, and is left alone. Only engines were tedious.
3. **Terrain.** Road +25% (horse +30%, wheels +35%), forest 50 to 95% by gait, swamp and shallows 25 to 65%, climbing and descent by the gait's climb share. Roads matter, wading costs, and wheels and horses are rightly the ones that suffer in woods and bog. Nothing changed.
4. **Prices.** Per man light infantry cost 1 gold, heavy infantry 1.67, cavalry 3.5 to 4.5, and numbers win by morale more than quality does. Light infantry beat heavy infantry, pikes and every horseman at equal gold (87 mean): heavy infantry was strictly dominated. Cavalry lost to everything that was not a monster, archers included, which they should ride down. Archers beat all foot. Grenadiers (80 mean), berserkers (83) and archons (88, three men) were bargains; wizards (9), dragons (0), elephants (19) and demons (18) were useless at the levy price of five gold a point of strength.
5. **Shooters.** The dragon's fireball (6 s, 50) was too slow to matter against a mass of foot.
6. **Economy.** Upkeep is a quarter of the price a day and a town pays about 25 a day, so a town carries about eight armies of light infantry: the cap is the gold. Nothing is free: an army is paid for when training starts, and one that cannot be paid for deserts. Not broken; the one real hole was price (light infantry the best value in the game), changed below. Upkeep follows the new prices (`(price + 3) / 4`).

## The siege (2026-11-14, review2 issues 1 and 12)

A catapult (150 m throw, nearest 30 m) outranges every archer (70 m). The review's two catapults died because the click that was meant for the gate landed on the ground beside it: a plain march, and a marching engine walked on into the bowmen's reach and never threw. Measured on `07-siege-batter-long.txt`: engines dead (0/2) by 100 s, gate untouched in 320 s.

| Change | Was | Now | Why |
|---|---|---|---|
| Engine ordered to march to within 40 m of a foe's standing gate | walks up to the doors | marches until the gate is within its throw (6 m inside the 150 m), then batters it from there (`World::engineGateInReach`) | engines stand off at their own reach, out of archer range; one rule for Skirmish and Conquest |
| Engine with no order and a foe's gate in its autoRange (140 m) | does nothing (throws at men only) | batters the gate, never walking up to it | an engine left in reach does its work |
| Stone against a gate | 500% of 100 (average about 170 a stone, 12 stones for a 2000 hp gate) | 900% (about 300 a stone, 6 or 7 stones) | two catapults open a gate in about 40 s, not 80 |
| Selected shooters | no reach shown | a dotted ring at the weapon's furthest reach (and the engines' nearest) for each kind selected (`Battle::rangeRings`, `pushRangeRings`) | you can see what is out of the archers' reach |

Archers' own kill rate is unchanged: they cannot reach an engine that holds its stand-off, and an engine walked in still dies as before. Measured with `tools/playtest/siege-catapults-first.txt` (the two catapults alone, then foot and sappers once the doors are down): gate down by 54 s, all 72 alive at that point, won with 50/72. `08-siege-all-in.txt` (the rush, select all): won with 65/72, the engines now throwing from their reach as the army goes in. The rush is still the cheaper win against this garrison. A foot escort, the rally and the suppression rules followed (the next section). Not done: a mark for who is shooting at the selected unit.

## The siege played as one (2026-11-14, review2 issue 12, second pass)

Engines batter a gate from their own reach, but nothing cleared the wall archers and nobody stood with the engines. The rules below are the World's (`engine/sim/src/world_siege.cpp`), derived from the men's own state each step (nothing saved, `Fixed` only), so Skirmish and Conquest, the player and the computer, play one siege.

| Rule | What it does |
|---|---|
| Escort | Men with no order within 80 m of an engine at work on a standing gate hold a line 14 m in front of it (shooters 5 m, healers 3 m behind it), and a melee man meets a foe within 45 m of the engines (the sortie) and goes back to his place. Men with an order (the rush) are not taken off it. |
| Suppression | A shooter outside the walls takes a garrison shooter on a wall-walk before any foe farther than 18 m; an engine takes any man on a wall-walk, none with a friend within 10 m of him. An idle engine creeps up to 120 m of such a man (its stone scatters at its whole 150 m) and no nearer: the walls' bows reach 70. Engines now throw on their own at four fifths of their reach. |
| Rally | After a breach, men bound through it from outside gather 70 to 95 m from it (outside the garrison's bows) until 70% of their side within 220 m has come up, then go in together. A breach older than four minutes, an order older than a minute, an engine or a man already nearer than 70 m is not held. |
| Weaker door | An engine with two doors in its reach takes the weaker (a failing door counts 30 m nearer, as `routeToGate` does). |
| Computer, battle | `aiSiege`/`aiEscort`: with engines at a standing gate the computer's other squads come up to them and halt (so the escort and the sortie rules take them) until it falls, then `aiFight` as ever, and the rally gathers them; `aimEngines` takes only gates within 250 m (one siege at a time). |
| Computer, Conquest | `Conquest::siegeHeld`: a stack whose engines are at work on a standing gate stops its other men (`Stop` once, `orderCell` = hold) and marches on when it falls; the rally gathers them. |

Measured with the play harness (speed 4, the same seeds; `mine` of 72 is every squad on the player's side, the engines and the bowmen included), before and after:

| Script | Before | After |
|---|---|---|
| `siege-catapults-first.txt` (catapults alone, foot and sappers in after the doors fall; the bowmen stay at camp) | won, 34/72 (38 lost) | won, 63/72 (9 lost) |
| `08-siege-all-in.txt` (the rush: select all, click the gate) | won, 65/72 (7 lost) | won, 66/72 (6 lost) |

What was tried and left, with why:

- **The patient siege is not yet cheaper than the rush** (9 lost against 6). The rush is cheap because the whole garrison comes out to meet 72 men in the open (the doors open to lure them), where numbers tell; the patient wave meets the horseshoe behind the breach, in a choke, with the wall bowmen over it. The two catapults kill about one man in the garrison before the wave goes: the garrison stands behind the doors and only mounts the walls when a foe is within 70 m, which is beyond the engines' creep.
- **Garrison on the walls when the door is struck**: tried, no gain (the bowmen mount but the two engines are too few to thin them), and it broke `a_breach_at_one_door_pulls_reserves_from_the_other`'s set-up; not kept.
- **Faster, wider stones** (9 s, 4 m): the engines then shell the melee at the breach and kill their own side (29/72 left); not kept. Holding fire wherever a friend is near silences them at the breach, where they help more than they hurt (30/72 left); not kept (the World's own friendly-fire hold, `world_aoe.cpp`, stands).
- **Bowmen on a firing line 60 m from the breach**: 17 of 24 died to the walls' bows, which outrange them from the height (the same reason the rally stands beyond 70 m).

The lever left is the garrison, not the attackers: a gatehouse that gives the defenders more to lose by coming out (docs/CASTLE_GATEHOUSE.md), or a third and fourth engine in the train.

## What changed, and why

| Change | Was | Now | Why |
|---|---|---|---|
| Catapult speed | 0.8 m/s | 1.4 | a siege should be slow, not 8 minutes to the next town; still under 60% of a man's pace, and the wheeled gait keeps its road and wood penalties |
| Giants speed | 2.4 | 3.0 | a five-metre man's stride; now level with minotaurs |
| Undead speed | 1.6 | 1.8 | still the slowest foot (they never break, so they should be late) but not a crawl |
| Wizard fireball | 9 s, 40 | 8 s, 50 | strong and fragile: a handful should break a column, and cavalry still run them down in 14 s |
| Dragon fireball | 6 s, 50 | 4 s, 70 | one dragon lost to sixty spearmen; the breath is the dragon |
| Light infantry price | 12 | 14 | was the best value in the game |
| Heavy infantry price | 20 | 17 | a man is worth three light men in a fight, yet cost 1.7; 1.4 a man puts the two on a knife edge (at 16 heavy wins, at 17 light wins) |
| Archers price | 20 | 30 | at 1.67 a man they beat every foot type; at 2.5 they parity with light foot and still beat slow foot |
| Pikemen price | 20 | 18 | the horse's answer and little else |
| Light cavalry price | 28 | 20 | horse beat almost nothing; now rides down archers and loses to pikes |
| Heavy cavalry price | 36 | 26 | as above |
| Wolf riders price | 28 | 20 | as above |
| Grenadiers price | 24 | 32 | fifteen grenadiers beat any foot; a satchel's blast wants a price |
| Berserker levy | 25 | 40 | beat all foot at the old price |
| Archon levy | 40 | 60 | three archons beat anything that walked |
| Wizard levy | 35 | 30 | with the faster fireball, still fair |
| Elephant levy | 35 | 25 | two men of 450 hp that nothing needed to fear |
| Demon levy | 40 | 26 | as above |
| Unicorn levy | 30 | 24 | as above |
| Dragon levy | 45 | 40 | buys two at sixty gold, which is what a dragon's rarity is for |

Everything else (the other prices, hit points, blows, morale, charge, the gait table, the climb shares) is unchanged. Prices live in `trained()` in `engine/sim/src/conquest.cpp`, stats in `engine/sim/src/units.cpp` (docs/UNITS.md follows it).

## Matrix

Before (the Conquest base, melee marching on the enemy line):

```
equal gold (60) a side, 8 seeds, both ends of the field; cell = % of fights the row won
        Lig Hea Arc Pik Lig Hea Wol Gia Min Gia Gri Peg Ele Uni Wiz Dra Dev Dem Und Arc Her Dwa Jou War Ber Cat Hye  mean  men
Light     . 100   0 100 100 100 100 100 100 100 100 100 100 100  50 100 100 100  88 100 100   0 100 100 100 100  13    87  60
Heavy     0   .  13 100 100 100 100 100 100 100 100 100 100 100 100 100 100 100 100   0 100   0 100  88   0 100   0    77  36
Archer  100  88   . 100 100  38 100 100 100 100 100 100 100 100 100 100 100 100 100 100  75   0 100  50   0   0  13    79  36
Pikeme    0   0   0   . 100 100 100 100 100 100 100 100 100 100 100 100 100 100 100   0 100   0 100  13   0 100   0    70  36
Light     0   0   0   0   .   0 100 100 100 100   0   0 100 100 100 100 100 100   0   0 100 100 100   0   0 100   0    54  17
Heavy     0   0  63   0 100   . 100 100 100 100   0   0 100 100 100 100 100 100   0   0 100  25  50   0   0 100   0    55  13
Wolf R    0   0   0   0   0   0   . 100 100   0   0   0 100 100 100 100 100 100   0   0  63  75 100   0   0 100   0    44  17
Giants    0   0   0   0   0   0   0   .  50   0  50  50 100 100 100 100 100 100   0   0 100  50   0   0   0 100   0    38  6
Minota    0   0   0   0   0   0   0  50   .   0  50  50 100 100 100 100 100 100   0   0 100   0   0   0   0 100   0    37  8
Giant     0   0   0   0   0   0 100 100 100   .   0   0 100 100 100 100 100 100   0   0 100   0 100   0   0 100   0    46  14
Griffi    0   0   0   0 100 100 100  50  50 100   .  50 100  50 100 100   0  50   0   0  50   6  50   0  50 100   0    46  8
Pegasi    0   0   0   0 100 100 100  50  50 100  50   . 100  50  75 100   0  50   0   0  50   0   0   0  50 100   0    43  8
Elepha    0   0   0   0   0   0   0   0   0   0   0   0   .   0 100 100   0 100   0   0 100   0   0   0   0 100   0    19  3
Unicor    0   0   0   0   0   0   0   0   0   0  50  50 100   . 100 100   0 100   0   0 100  50   0   0   0 100   0    29  8
Wizard   50   0   0   0   0   0   0   0   0   0   0  25   0   0   . 100   0   0  13   0   0   0  38   0   0   0   0     9  5
Dragon    0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   .   0   0   0   0   0   0   0   0   0   0   0     0  1
Devils    0   0   0   0   0   0   0   0   0   0 100 100 100 100 100 100   . 100   0  50  88   0   0   0   0 100   0    36  5
Demons    0   0   0   0   0   0   0   0   0   0  50  50   0   0 100 100   0   .   0   0  63   0   0   0   0 100   0    18  3
Undead   13   0   0   0 100 100 100 100 100 100 100 100 100 100  88 100 100 100   .   0 100   0 100   0   0 100   0    65  29
Archon    0 100   0 100 100 100 100 100 100 100 100 100 100 100 100 100  50 100 100   . 100  50 100 100 100 100 100    88  5
Hero      0   0  25   0   0   0  38   0   0   0  50  50   0   0 100 100  13  38   0   0   .   0 100   0   0 100   0    24  3
Dwarve  100 100 100 100   0  75  25  50 100 100  94 100 100  50 100 100 100 100 100  50 100   .  50  75 100   0 100    80  15
Journe    0   0   0   0   0  50   0 100 100   0  50 100 100 100  63 100 100 100   0   0   0  50   .   0   0 100   0    43  24
Warrio    0  13  50  88 100 100 100 100 100 100 100 100 100 100 100 100 100 100 100   0 100  25 100   .  38 100   0    77  30
Berser    0 100 100 100 100 100 100 100 100 100  50  50 100 100 100 100 100 100 100   0 100   0 100  63   . 100 100    83  24
Catapu    0   0 100   0   0   0   0   0   0   0   0   0   0   0 100 100   0   0   0   0   0 100   0   0   0   .   0    15  3
Hyena-   88 100  88 100 100 100 100 100 100 100 100 100 100 100 100 100 100 100 100   0 100   0 100 100   0 100   .    88  48
```

After:

```
equal gold (60) a side, 8 seeds, both ends of the field; cell = % of fights the row won
        Lig Hea Arc Pik Lig Hea Wol Gia Min Gia Gri Peg Ele Uni Wiz Dra Dev Dem Und Arc Her Dwa Jou War Ber Cat Hye  mean  men
Light     . 100   0 100 100 100 100 100 100 100 100 100 100 100   0   0 100 100  88 100 100   0 100 100 100 100   0    80  51
Heavy     0   .   0 100 100 100 100 100 100 100 100 100 100 100 100   0 100 100 100 100 100 100 100 100 100 100  13    85  42
Archer  100 100   . 100  13   0  63 100 100 100 100 100 100 100  50   0 100 100 100 100  50 100 100  75  75   0   0    74  24
Pikeme    0   0   0   . 100 100 100 100 100 100 100 100 100 100  13   0 100 100 100  50 100 100 100 100 100 100   0    75  40
Light     0   0  88   0   .  13 100 100 100 100  50  50 100 100 100 100 100 100 100  50 100 100 100   0 100 100   0    71  24
Heavy     0   0 100   0  88   .  88 100 100 100   0   0 100 100 100 100 100 100  75   0 100  13 100   0 100 100   0    64  18
Wolf R    0   0  38   0   0  13   . 100 100 100  50  50 100 100 100 100 100 100 100  50 100  88 100   0   0 100   0    61  24
Giants    0   0   0   0   0   0   0   .  13   0  50  50 100 100 100 100 100 100   0  50 100 100   0   0   0 100   0    41  6
Minota    0   0   0   0   0   0   0  88   .   0  50  50 100 100 100  13 100 100   0  50 100  25   0   0   0 100   0    38  8
Giant     0   0   0   0   0   0   0 100 100   .   0   0 100 100 100 100 100 100   0   0 100   0 100   0   0 100   0    42  14
Griffi    0   0   0   0  50 100  50  50  50 100   .  50   0  50 100  25   0   0   0  50  50 100  50   0  50 100   0    39  8
Pegasi    0   0   0   0  50 100  50  50  50 100  50   .   0  50   0   0   0   0   0  50  50   0   0   0  50 100   0    29  8
Elepha    0   0   0   0   0   0   0   0   0   0 100 100   .   0 100 100 100 100   0 100 100  38   0   0   0 100   0    36  5
Unicor    0   0   0   0   0   0   0   0   0   0  50  50 100   . 100 100 100   0   0  50 100 100   0   0   0 100   0    33  10
Wizard  100   0  50  88   0   0   0   0   0   0   0 100   0   0   .   0   0   0  88  13  38 100 100  13  13   0   0    27  6
Dragon  100 100 100 100   0   0   0   0  88   0  75 100   0   0 100   . 100  88 100 100 100 100 100 100 100 100  25    68  2
Devils    0   0   0   0   0   0   0   0   0   0 100 100   0   0 100   0   .   0   0 100  88  13   0   0   0 100   0    23  5
Demons    0   0   0   0   0   0   0   0   0   0 100 100   0 100 100  13 100   .   0 100 100  38   0   0   0 100   0    33  5
Undead   13   0   0   0   0  25   0 100 100 100 100 100 100 100  13   0 100 100   . 100 100   0 100   0   0 100   0    52  29
Archon    0   0   0  50  50 100  50  50  50 100  50  50   0  50  88   0   0   0   0   .  50  69  50  50  50 100  50    43  3
Hero      0   0  50   0   0   0   0   0   0   0  50  50   0   0  63   0  13   0   0  50   .   0 100   0   0 100   0    18  3
Dwarve  100   0   0   0   0  88  13   0  75 100   0 100  63   0   0   0  88  63 100  31 100   .   0  50 100   0   0    41  11
Journe    0   0   0   0   0   0   0 100 100   0  50 100 100 100   0   0 100 100   0  50   0 100   .   0   0 100   0    38  24
Warrio    0   0  25   0 100 100 100 100 100 100 100 100 100 100  88   0 100 100 100  50 100  50 100   . 100 100   0    74  30
Berser    0   0  25   0   0   0 100 100 100 100  50  50 100 100  88   0 100 100 100  50 100   0 100   0   . 100   0    56  15
Catapu    0   0 100   0   0   0   0   0   0   0   0   0   0   0 100   0   0   0   0   0   0 100   0   0   0   .   0    12  3
Hyena-  100  88 100 100 100 100 100 100 100 100 100 100 100 100 100  75 100 100 100  50 100 100 100 100 100 100   .    97  48
```

## Reading it

- Rock, paper, scissors among the trained types. Pikes beat horse (light and heavy 100) and lose to every other foot. Light horse beat archers (88) and lose to pikes, heavy horse beat archers (100). Archers beat light foot, heavy foot and pikes (100) and lose to horse and, hand to hand, to nothing that is not close. Heavy infantry beat pikes and warriors and lose to light foot and archers. Wizards beat light foot and pikes in the open and die to anything that reaches them. The catapult beats shooters (archers, wizards, dragons, grenadiers) from 150 m and nothing in melee, which is its job; its work on walls is not in this matrix.
- Trained types sit between 61 and 85 on the mean, except the grenadier (41, a specialist who wins by blast) and the catapult (12, a siege engine).
- **Dragons** buy two at 60 gold (price 40), which is why the row reads 68. A single one is weak against a mass of foot, as before.
- **Hyena-folk** (97 for 48 men) are a field-only beast, never trained or levied: their price is the default levy and means nothing. If they are ever sold they need pricing like the rest.
- Rares that are weak per gold (hero 18, devils 23, pegasi 29) are meant to be given, not bought; left alone.

## Not covered

Walls and gates (engines against a castle), terrain in the fight (high ground, forest), hero levels and ranks, mixed armies, and fliers fighting from the air. A flat 90 m field favours shooters a little and charges less than a real battle would.
