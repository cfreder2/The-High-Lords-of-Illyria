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

## All-computer Conquest pacing (illyria_conquestsim [days] [hard|easy] [seed ...])

Eight Hard lords, headless, seeds 1 to 3, before and after the men-aware attack gate and retaking bonus (conquest_ai.cpp). No war was decided in 60 days (70 to 75 with the later "weary" boldness): the most any lord held was 11 to 17 of 80 towns (41 are needed), nobody snowballed, and from about day 30 towns changed hands about once in five days. Men in the field stay near 2,600 because upkeep takes the income, so equal lords stand off behind their walls. The stalemate breaker is the next section.

## A Conquest that ends (2026-11-14)

A Conquest between eight equal Hard lords never ended. This section is why, what was changed, and what it measures now. The tool is `tools/conquestsim.cpp`: `illyria_conquestsim [days] [hard|easy|mixed] [seed ...]` plays the whole war headless and prints, per seed, the day the first lord fell, the day a lord first held 41 towns, the day the war was decided and the leader's towns every ten days (`ILLYRIA_SIM_REALMS=1` adds each lord's armies, gold, income and upkeep, the armies in his seat and his biggest host); `illyria_conquestsim siege [squads] [seat|town][+engines] [seed ...]` marches so many squads of light infantry at one walled place and prints the men left on each side.

### Why it stalled

1. **Nobody takes a wall.** By day 20 the fifty-odd open villages are shared out and the towns in lords' hands stop near 60 of 80. The other twenty are the neutral walled towns, each with a garrison of 36 to 60 men, and nobody ever takes one. From then on villages change hands about once in five days and nothing else happens: on seeds 1 and 2 the leader held 10 and 9 towns on day 20 and 14 and 12 on day 95, and no lord had fallen.
2. **The lords' sums were right about that.** The computer lords reckon an attack with the Warlords dice, which count armies one against one. Against a held wall the dice all but forbid it (24 squads of heavy infantry against 14 in a capital: 30 in 100). The first attempt here replaced the dice with a count of men and let a lord go at a wall with half as many men again as stood behind it. The hosts went, and bled: by day 25 lords had lost a third of their armies at neutral walls and held no more towns. The siege runs say why:

| Light infantry marched at the gate | Men | Held by | To one | After four days |
|---|---|---|---|---|
| A walled town (Brookhaven, walls 4), seed 1 | 96 | 48 | 2.0 | held: 47 of ours left, 36 of theirs |
| the same, seed 3 | 120 | 48 | 2.5 | held: 64 left, 8 of theirs |
| the same, seeds 1 and 2 | 144 | 48 | 3.0 | taken with 92 left; held with 77 left, 32 of theirs |
| the same, seed 1 | 192 | 48 | 4.0 | taken in 2.6 days, 139 left |
| the same, two of the eight squads catapults | 76 | 48 | 1.6 | held: 17 left, 21 of theirs |
| A lord's seat as it stands on day 1 (Highcrag), seeds 1 and 2 | 72 | 43 | 1.7 | held: 43 and 24 left, 19 and 15 of theirs |
| the same, seed 1 | 96 | 43 | 2.2 | the garrison dead on day 3.75, 66 left |

   A man behind a wall is worth three in front of it, and a siege takes two to four days, long enough for everything the defender has within three days' march to come home.
3. **A lord has about 240 men.** Upkeep is a quarter of the price a day, so a realm of ten towns (about 190 gold) keeps 25 to 45 squads, and most of them stand within a day of his seat: the men a host would meet at a seat on day 20 were 140 to 245. Three to one of that is more than any lord has. Equal lords cannot take each other's seats, and that is not a fault of the computer's play: a person facing an equal could not either.
4. **Gold sat in the chest.** On day 10 lords held 300 to 850 gold and trained nothing with it, because upkeep is capped by income.

So the war does not stall for want of boldness. Making the computer bolder was tried first and cost it armies for nothing. It stalls because nothing a lord can do to an equal is final, and because nothing makes two lords unequal. Raising every lord's army to the size a wall wants (halving upkeep) would have doubled the men on the island, which the frame budget of docs/ONE_WORLD.md does not have. The changes below make conquest final and make it pay, and give the computer lords the sense to know when they are beaten.

### What changed, and why

| Change | Was | Now | Why |
|---|---|---|---|
| **A seat is a realm** (`Conquest::fallOfRealm`) | "no rule depends on the capital after the start": a lord who lost his capital played on from his villages, and a lord with one hero and no town was still in the war | when a banner reaches the top over the capital a lord began in, he is out of the war; his chest, his towns and their land are the victor's; the garrisons standing in his towns swear to the victor, man for man; his armies in the field and at sea are gone, and his heroes | the one siege that matters now decides something, for the player as for the computer: seven seats is the war, and a superpower can be undone by one blow at its heart. The garrisons go over so that the victor is handed towns, not empty walls for the neighbours to walk into |
| Banner over a seat (`kSeatBannerSlow`) | as any town: 19 s to 64 s at normal speed | a third of the pace: 58 s to 3 min 12 s (half a day to a day and a half) | a realm should not be lost in the time it takes to notice. The lord (or the player) is given the time to come back and fight in his own courtyard |
| Victory (`Conquest::winner`) | last lord standing, or more than half the towns with no rival within 15 | last lord standing, or more than half the towns (41); and the war is over when every person in it has lost his seat | with 41 towns no rival can hold more than 39: "within 15" only drew out a war already won |
| **The council of the tenth day** (`Conquest::council`) | none | from day 30, at every tenth dawn, the weakest computer lord bends the knee to the strongest lord in the war if that lord holds twice his towns (an Easy lord: three times). His realm goes over as if his seat had fallen. One lord a council; never a person | the stalemate breaker. A lord with four towns facing one with ten has lost, and the only thing that kept him in the war was that nobody could afford the siege to say so. It is a decision the computer makes, not a rule that takes a person's realm: the player is never made to yield, and may be the one yielded to. One a council and every tenth day, so that the war is a story of one power rising with time to stop it, not a collapse in a morning |
| The sack (`kSackDays`) | taking a lord's town paid half his chest's share for it | that, and four days of the town's own income | conquest pays for the men it cost: a walled town's sack (84 to 120 gold) buys back six to eight squads |
| The computer's chest | pay kept within 95% of income and a twentieth of the chest | 95% of income and a twelfth of the chest | the 300 to 850 gold of the first weeks, a sack, a ruin's hoard and a fallen lord's treasury now become squads within days instead of sitting |
| Walls, in the computer's sums (`menOdds`) | the dice, and never fewer than 90% of the men to be met | for a host (and for a stack the dice would not send): even at as many men as it will meet, counting men behind a wall three times; it wants 45 in 100 (less as the war drags on) | honest in both directions. It no longer refuses a walled town held by one squad, and no longer throws eight squads at one held by five. The same sum tells a lord that a host marching on his town is a threat |
| Hosts and the muster (`host`, `muster`) | every stack chose its own target, and no two stacks went at one town | a Hard lord's idle stacks muster at his town nearest the best prize; the fewest of the stacks standing together that have the men for it, three at most, go as one at a prize half as good again as one of them could take alone | a wall wants three to one, and no single stack of eight squads has that against more than a token guard |
| What a seat is worth to a Hard lord | a capital: 10 points more than a town | and 4 points more for every town its lord holds | all of them come with it |
| The guard of a computer lord's own seat | his weakest army | one army, and one more for every four towns he holds, five at most | his realm hangs on it; the guard grows with what he stands to lose and leaves the first days' race alone |

No statistic of a computer lord's men, prices or income was touched; the sack, the seat, the slow banner and victory are the same code for the player and the computer. The council is the one thing only a computer lord does, and it is a thing he does to himself.

### Measured

Eight Hard lords, the same seeds before and after. "Leader" is the most towns any lord held on days 10, 20, 30 and so on.

Before (stopped at day 95; seeds 1 and 2):

| Seed | First lord out | A lord holds 41 | Decided | Leader's towns every ten days |
|---|---|---|---|---|
| 1 | never | never | not by day 95 | 6 10 11 11 12 12 12 14 14 |
| 2 | never | never | not by day 95 | 8 9 9 10 11 10 11 12 12 |

(The earlier runs of this section's predecessor, seeds 1 to 3 to day 60 and 75, read the same: 11 to 17 towns, nobody out.)

After:

| Seed | First lord out | A lord holds 41 | Decided | Lords out (seats stormed) | Leader's towns every ten days | How |
|---|---|---|---|---|---|---|
| 1 | day 30 | day 70 | day 70 | 6 (1) | 8 10 14 16 25 31 42 | councils on days 30 and 40 to one lord; on day 45 another stormed a neighbour's seat (11 towns with it) and overtook him; councils on 50, 60 and 70 to the new leader |
| 2 | day 30 | day 70 | day 70 | 5 (0) | 7 11 15 21 28 36 43 | a council every tenth day to the lord who led on day 30 |
| 3 | day 30 | day 70 | day 70 | 6 (1) | 8 9 15 20 25 26 46 | the leader was ganged up on and fell from 25 to 22 towns while a rival rose from 9 to 18; he stormed a seat on day 68 and the last council gave him the war |
| 4 | day 29 | day 60 | day 60 | 5 (1) | 8 9 19 27 35 45 | a seat stormed on day 29, then a council every tenth day |
| 5 | day 30 | day 80 | day 80 | 6 (0) | 7 9 14 19 26 33 40 50 | a council every tenth day; 40 towns on day 70, one short |

Men under arms stayed between 2,300 and 3,500 throughout, as before (no more men on the island). The pins moved: `conquest_is_deterministic` and `boats_are_deterministic` (re-pinned, both read twice).

All Easy lords, sixty days, seeds 1 and 2 (no person in the war, so no council sits: a Knight yields only to a person): one seat fell in each (days 27 and 31, both walked into with no guard in them), the leader held 16 and 14 towns on day 60, and nothing was decided. That is the war an Easy player walks into: seven lords who do not combine, do not kneel to each other, and leave their seats thin.

### Reading it

- **The targets are met on five seeds of five:** a first lord out on day 29 or 30 (asked: 20 to 40), a winner on day 60 to 80 (asked: 60 to 120), nobody ahead by more than two towns on day 10 and no leader above 11 on day 20.
- **The council carries most of it.** Of the 28 lords who left these five wars, 3 had their seats stormed and 25 bent the knee. The walls are as strong as they were; what changed is that being beaten now has a consequence. The storms that did happen mattered: on seed 1 one of them changed who won.
- **It is regular.** With one lord kneeling every tenth day, a war that nobody interrupts ends between day 60 and day 80, and the lord who leads on day 30 usually wins it (four seeds of five). On seeds 2 and 5 nothing interrupted it at all. That is the cost of a rule that always ends the war; the cure in play is the seat rule, which lets one siege undo a leader, and the leader being ganged up on (seed 3).
- **Position decides who leads on day 30.** The Sunreach Riders won three seeds of five; the central realm (the Aurelian Throne, the player's default) held 4 or 5 towns on day 25 in every seed and was first to kneel in four. That is the map's balance, not this change's, but the council now turns it into a result. It wants looking at before the default realm is left as it is.
- **A person on Hard** who is not the strongest on day 30 has about forty days to become it or to take the leader's seat. **On Easy** nobody else is handed anything, the Knights' seats are thin, and every realm the player earns twice over kneels to him. No person has played either; see below.

### Tried and left

- **Bolder dice** (the earlier "weary" rule, kept): two thousandths less asked for each day after day 30. It moves villages, not walls.
- **A host on the dice alone:** three stacks reckoned together by the dice. Twenty-four squads against fourteen in a capital is 30 in 100 by the dice, so no host ever went.
- **Men behind a wall counted one and a half times, and a seat's one and three quarters:** hosts went at neutral walled towns at two to one and were beaten off; lords lost a third of their armies between days 20 and 25 for no town. Three times is what the siege runs measure.
- **Catapults in the host:** two engines among eight squads did worse than eight squads of foot (17 of 76 left against 21 of 48). The patient siege of the section above is the player's; the computer's stacks do not yet play it well enough to count on, so the sums take no account of engines.

### Not done

- **Starving a town out** (a walled place that yields to a host camped round it, without an assault). It would let two to one decide a siege with time instead of blood, and would make relief armies and field battles the centre of the war. It needs a rule, a way for a stack to stand off instead of going in, and something on the screen; it is the next thing to build if sieges are to carry more of the war than the councils do.
- **Bigger armies** (cheaper upkeep): see "Why it stalled", 3.
- **A day limit.** Not needed: every war measured was decided by the towns.
- **The council between people.** In a war of several people a person is never made to yield and cannot offer to; fealty between people wants an Order of its own.

### Not verified

- **No person has played it.** "A person of average skill can win on Easy" rests on the all-Easy runs above and on the rules (Knights yield only to a person, and do not mass), not on a played game.
- `hard_lords_outplay_easy_ones` (tests/test_conquest_ai.cpp: ten 25-day wars) was not run; the Hard lord's play changed (hosts, the muster, the chest), so its tally will have moved and may want its bounds looked at.
- The other long tests that play computer lords for days (the save round trips on many seeds, the sea and quest tests) and the full suite were not run. Run here: the new tests in tests/test_conquest_realm.cpp, the banner tests in tests/test_capture.cpp, `the_computer_saves_a_threatened_capital`, `computer_lords_keep_out_of_debt`, and the two pins.
- The page and the desktop app were not built or looked at: the news lines for a fallen realm and a council, and the *Illyria is lost* overlay when the player's seat falls, go through paths that exist (`deeds`, `winner`), but nobody has seen them on the screen. Nothing on the screen yet warns that a seat is a realm, or that a council is coming.
- The before table is two seeds stopped at day 95, not five at 120 (the machine was shared and each war costs twenty to sixty minutes).
- The wars above were run before the last two edits (the council told to every lord as news; a Knight yielding only to a person). Neither is on the path of an all-Hard war, and the pins, which do not reach day 30, were read after them.
