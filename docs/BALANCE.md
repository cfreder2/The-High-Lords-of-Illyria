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

## Sieges with a second act (2026-11-14, review3 items 5, 6, 7)

Three things were wrong: a keeper waited four minutes with nobody in reach and the battle never ended; a storm lifted the flag and walked home; a capital fell in nine seconds with most of its garrison alive. And from the pass above: the rush was cheaper than the patient siege.

| Change | Was | Now | Why |
|---|---|---|---|
| The flag (`CapturePoint`) | the side with more men within 14 m raised it, up to 10 points a tick (3.3 s) | climbs only with attackers at the pole and **no unbroken defender on the ground inside the walls**; 1 point a tick and one more for every four men there, eight at most (11 s at the least); stops, not falls, while defenders hold; sinks 1 a tick with no attacker there | a town is taken by clearing its ground, not by outnumbering two men at a pole |
| A capital | one flag in the outer ward | a second pole behind the bailey's doors, fought for after the first by the same rule | the keep must be taken too |
| An order at a foe's gate (standing or down) | through the doors, 10 m, and stop | through, then on to the courtyard (a bailey's gate: the ground before the keep), and **occupy** it: an idle man more than 6 m from his place there walks back, after a rout and a rally too (`Command::occupy`) | the storm stays on the flag's ground |
| A garrison and a host of more than two to a man of its foot | failing doors were opened to it, and the foot went out through open doors or a breach | the doors stay shut and the foot hold the horseshoe behind the breach | the rush is no longer met in the open, where its numbers told |
| Besiegers' muster | foot in front 110 m from the door, engines last (60 to 90 m to walk) | engines in front, 132 m from the door, throwing from the first tick | a defence has a clock from the start |
| A computer besieging a town of one door | one camp | a second camp (a third of the host, the second siege train) 112 m before a side wall; its engines shell the wall-walks while the first train batters the gate | two batteries to watch, and the walls are worth manning or leaving |
| Siege purses | the same for both sides | besiegers have 150% of the keepers' (`kSiegeOddsPct`), whichever the player is; a siege train to every four squads, two at most for a computer | walls are worth men; with the honest flag an attacker at parity could not take a capital |
| The end of a defence | none | the besiegers break off when 30% or fewer of their fighting men stand unbroken (`kSpentPct`): the keeper has won | a defence ends |
| On screen | a flag on a pole | each door's strength and a line on the flag beside the minimap ("Gate 54%", "Defenders hold the bailey", "Courtyard 45%") | the clock, and what blocks the flag |

Measured with the play harness on this branch merged with the battle endings (the clock stops at the decision, so `mine` is the price at the moment of victory), before on merge-master 7331ef2:

| Script | Before | After |
|---|---|---|
| `10-siege-defend-look`, `22-siege-defend-hold` (keep the walls, 300 gold) | first man lost at about 280 s; 104 v 12 at 422 s and 92 v 2 at 667 s, undecided | the gate at 54% by 8 s and down by 30 s; first man lost at 71 s; **won at 184 s**, 71 of 143 left against 59 of 182 |
| `siege-catapults-first.txt` (the patient way) | won at 136 s with 67 of 72 (the flag in 4 s, 27 of 36 defenders alive) | won at 146 s with 65 of 72 (7 lost) |
| `08-siege-all-in.txt` (the rush) | won at 91 s with 65 of 72 (7 lost) | won at 100 s with 57 of 72 (**15 lost**) |
| `13-siege-attack-patient` | flag 11% to 100% in 3 s; won at 169 s, 69 v 25 | flag in 39 s, the storm staying on it; won at 207 s, 60 v 13 |
| `21-capital-patient` | flag 4% to 100% in 6 s; "won" at 342 s with 93 against 95 | no flag; 71 v 34 at 612 s, undecided (the script's squad numbers are those of the old muster, and it gives no order after the first storm) |
| `capital-keep.txt` (everyone at the road's door, then at the keep's) | | won at 338 s with 68 of 99 against 28 of 86: the garrison quit the field with the courtyard's flag at 18% |

Engines first is now the cheaper way into a town (7 lost against the rush's 15). Each figure is one run.

What was tried and left, with why:

- **The garrison's last stand at the keep** (a capital's garrison falling back through the bailey's doors when the outer ward is lost): written and taken out. The men were sent and the bailey's gate opened to them, but thirty of them jammed at its doors and none passed in 90 s (the way planned against shut doors leads every man to the one point at their middle; planning it again when the doors opened did not free them). So the keep's gate and ground are taken after the courtyard, but the garrison fights for the outer ward where it stands, and a garrison that breaks there ends the battle as a rout before the keep is asked for.
- **A longer clock on the gate**: one siege train opens a gate in about 20 to 30 s, too short for a sortie on foot (132 m is 55 s) to save it. The second train is kept off the gate (it shells the walks) so that it is not 15 s. The gate's strength and the stone's blow were left as the pass above set them.
- **A slower flag** (33 s for fewer than eight men): the patient storm then stood on the flag's ground under the walls' bows while the garrison rallied, and lost 43 of 72; the rate above is the one that measured well.

## The boulder (2026-11-14)

The owner: *"catapults should be able to fire boulders that roll on open ground smashing troops, while crushing doors, and smashing castles."* A catapult's stone no longer ends where it comes down: it smashes there as before (3 m, 100) and then bounces once and rolls on (`engine/sim/src/world_boulder.cpp`; the rules in docs/GAME_DESIGN.md, "The boulder"; the roll distances by ground and slope in docs/UNITS.md, "The boulder", made by the stone's own steps). The catapult's own figures (14 s, 150 m, 4 m of scatter at 100 m, the smash) are unchanged.

**Tuning, and why.** Level grass holds a rolling stone back by 0.28 of its weight and takes 0.28 m/s of forward speed for each m/s it comes down with: a stone thrown 100 m comes down at 19 m/s over the ground, keeps 11.6, hops 9 m and rolls 20 more, **30 m in all** (11 m from the nearest throw, 45 m from the farthest). A man costs it a fifteenth of its speed and takes 30 + 12 a metre a second (dead above about 5 m/s; an armoured man lives through a slow one). A lane is the stone's half width and the man's: about 2 m across.

**Kills per stone**, measured without scatter (a stone thrown 100 m to come down 6 m short of the first man; light infantry 2 m apart unless said; a scratch harness on bare test ground, the sim's own `World::throwStone`):

| The men | Struck | Dead | The stone went |
|---|---:|---:|---:|
| nobody | | | 30 m |
| a file of 16 in line with the roll | 7 | 7 | 19 m |
| two files abreast | 10 | 10 (3 more hurt) | 16 m |
| a loose file, 4 m apart | 4 | 4 | 23 m |
| a rank of 16 across its way | 0 or 1 | 0 or 1 | 30 m |
| a block 12 wide and 4 deep | the file in its lane: 4 | 4 | 21 m |
| a file of heavy infantry | 6 | 5 | 18 m |
| a file of heavy cavalry | 3 | 2 | 12 m |
| a file on a road | 9 | 9 | 24 m |
| a file in mud | 0 | 0 | 3 m |
| a file standing out through a ford 12 m on | 4 (those on the bank) | 4 | 12 m (stopped at the water) |

Slope tells in **reach**, not in the first few men (a dense file under the landing saturates it). The same file beginning 16 m beyond the landing:

| Up 25 in 100 | Up 15 | Up 8 | Level | Down 8 | Down 15 | Down 25 | Down 35 | Road, level |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 | 3 | 4 | 4 | 5 | 6 | 6 | all 16 (it gathers speed and runs 300 m) | 7 |

**In the game** (`tools/playtest/boulder-*.txt`, the open ground skirmish and the village siege; pictures and frame sequences were read from `~/Desktop/boulder`):

| Script | What happened |
|---|---|
| `boulder-level.txt` (a file of 16 on level grass) | 7 dead; the stone lay 21 m from where it came down (25 m forecast with nobody there) |
| `boulder-line.txt` (a rank of 16 across the roll) | none hurt; it passed between two men and lay 25 m on, its furrow behind it |
| `boulder-downhill.txt` (a block of 60 on a 30 in 100 hillside, thrown from above) | 5 dead; it bore away down the fall of the hill and was still rolling 43 m on, ten seconds later |
| `boulder-uphill.txt` (the same block, thrown from below) | 2 dead, 3 hurt; it climbed a few metres, stopped and rolled back to lie 15 m aside |
| `boulder-ford.txt` (a file from the bank out through the ford) | 2 dead and 2 hurt on the bank; it hopped once and stopped dead at the water's edge 9 m on, the twelve in the ford untouched |
| `boulder-own.txt` (six foes, eight of your own in file 8 m beyond them) | 7 dead and 6 hurt of the fourteen: it does not know sides |
| `boulder-gate.txt` (two catapults at a village gate) | the two stones that came down short rolled into the doors for 214 each (gate 2000 to 1572); the next, straight on the doors, 900 |

**Doors: the balance is kept.** A stone from the air does what it did (900% of 100 on the doors; `catapult_stones_break_a_gate_and_the_way_is_open`: one engine, three stones, 33 s; `engines_batter_a_gate_from_their_reach_and_open_it_in_a_few_stones`: two engines, 20 s). Those two figures are the same with the rolled blow switched off (measured with `kRolledGateShare` at nothing), so rolling into a gate is a consolation for a stone that fell short (about 200 from a 10 m/s roll, half the blow of its speed), not a faster siege. Walls, towers and houses take nothing yet (`World::strikeStructure`, the hook).

**Its own men.** The aim weighs the whole roll (`World::rollToll`). A catapult shelling six foes with eight of its own in file 9 m beyond them (clear of the smash, in the roll), eight worlds, 90 s: with the roll left out of the aim its own side lost **8905** hit points; reckoned, **824** (it drops the stone a few metres aside or waits), and the foe lost more (7092 against 6068). With no friend near it throws at the same marks as before, and the pile it shells loses 8380 where the smash alone took 6156 (`tests/test_friendly_fire.cpp`, re-pinned).

**What this does to the field, and what was not measured.** A catapult is stronger against anything deep: a column marched straight at it, a block, a storming party in a gateway. Against a line it is what it was. The duel matrix below (catapult mean 12) and the siege scripts of the sections above were **not** run again for this change; the catapult's price and the computer's use of engines are as they were.

## Tactics: position, support, the Rally, Easy (2026-11-14, review3 items 2, 3, 8, 15)

The review's finding: on the open field at 450 gold a planned defence killed about as many per man lost as giving no order at all. The rules below are the World's (`engine/sim/src/world_tactics.cpp`, tests in `tests/test_tactics.cpp`), from the men's own state, `Fixed` only, the same for every side and for Skirmish and Conquest. Each tells the presentation when it happens (`World::tactics()`), which floats a word over the men (`Battle::floats`, only over men the viewer can see).

| Rule | Was | Now | Word |
|---|---|---|---|
| A man wading (shallows; not the giants' gait) | slower, no other cost | his blows 50% weaker; a man on dry ground strikes him 50% harder and each such blow shakes him 40; missiles +75% and his shield turns half as many | In the ford, Shot in the water |
| The morale census | every foe within 15 m counts | to a man on dry ground a wading foe is half a foe | |
| A man with no order, a foe in the water | walks in after him | keeps the dry ground and lets him come (an order takes him across) | |
| The high ground, melee | the small edge only (+4% at most) | also +2% for each 0.01 of grade striking down (at most +30%), as much less striking up (at most -20%) | High ground |
| A volley from above | reach, and the plunge (+25% at most) | also +5% a metre over the mark beyond the first (at most +50%) | |
| Charge, shock to the man struck | 150 front, 225 flank, 300 rear | 150 front, 400 flank, 600 rear, 950 in the back of a man busy with another; half of it to his friends within 6 m | Flanked, Hit from behind, Broken by the charge |
| A rider at the gallop | wheeled to a free place on his man's ring, and lost the charge | rides straight at his man | |
| Braced pikes, met head on | the pike's blow and half the impact | the pike's blow half again and the whole impact, and the rider shaken 250 (heavy horse loses 78 of 130, was about 48) | Braced |
| A man with no order | fights what comes within his own reach (20 to 30 m) | goes to the help of a friend fighting within 40 m; horse ride round to the flank; `Hold` (K) keeps him in his place | |
| A broken squad | comes back on its own clock, unseen | a white flag and the seconds on its card; the lord within 20 m halves it; the lord's Rally (Y, a minute's cooldown) brings back every broken man within 40 m, the computer's lords too | |
| Easy (Skirmish) | the player's purse; attacks at once, piecemeal | three quarters of the purse; stands 45 s, then piecemeal; no massing, flanking or falling back | |

Kills per man lost, the review's scripts on the play harness (`~/Desktop/review3/scripts`, field, 450 gold; the scripts take real-time input, so runs of one script differ: each run is listed). Before is merge-master's build run the same day.

| Script | Before | After |
|---|---|---|
| `03` Normal, no orders (the army starts on the bank) | 2.1 (152 for 71) | 2.6 (156 for 61) |
| `07` Normal, the planned bank defence, at 180 s | 2.2 (review), 2.7, 2.2, 2.4, 2.5 | 3.2, 3.3, 3.1 (145 to 161 for 46 to 49) |
| `07` with the Rally pressed at each look | | 3.0 (153 for 51) |
| `09` Normal, everyone across the river at once | 1.4 (169 for 124) | 1.6 (202 for 126) |
| `12` the Ford, no orders | 1.4 (97 for 70) | 1.3 (90 for 70) |
| `04` Easy, no orders | 3.1 (176 for 57) | 2.2 (146 of 166 for 66) |
| `08` Easy, the same plan as `07` | 1.4 (179 for 132) | 2.8 (135 of 166 for 49; 134 of 183 left, was 51) |

The set piece (`illyria_balance tactics`, flat open ground, 8 seeds, at 60 s): 12 heavy infantry and 8 light cavalry against 24 light infantry.

| What the horse does | Kills per loss | The foe's line broke |
|---|---|---|
| No horse | 0.0 (0 for 95) | 0 of 8 |
| Sent in at the front, through its own foot | 0.8 (79 for 100) | 0 of 8 |
| Left on the wing with no order (support: round to the flank) | 6.4 (191 for 30) | 8 of 8 |

Reading it, and what is not there yet:

- **The held bank pays more and steadier, but not 4 to 1.** The plan loses 46 to 49 men where it lost 67 to 91, and for its first 110 s it kills about nine for one; the rest is lost where the foe's main body comes ashore on one squad of twelve. First teeth of 35%, 35% and +50% gave 1.7 to 3.7 run to run and were raised.
- **No orders also gained** (2.1 to 2.6), because the field's army starts on the bank and men with no order now keep the dry ground and help each other. The plan beats it by about a quarter, not by a wide margin.
- **A flank charge pays in the set piece (6.4 against 0.8) but is rare in the field battle**: two instrumented runs of `07` landed one and three charges in the whole fight, all frontal. Horse given a march to a place fights at a walk; the computer's riders halt behind their mark before they strike. The lever left is the order, not the rule: an attack order for horse that keeps the gallop.
- **The Rally pressed blind did not help** (3.0 against 3.2): it brings men back into the fight they ran from. It is for the moment the lord is beside them and the foe is not.
- **Easy is no longer harder than Normal for the plan** (49 lost against 46 to 49, where it was 132 against 67 to 91), **but it is not yet clearly easier**: with no orders it costs 66 men where Normal costs 61, from a smaller host. Easy by one road was tried and left: every squad on the one foe nearest the army's middle is a concentration (no orders: 86 lost).

Matrix (flat field, no water, so only the charge rules show): light cavalry 71 to 77, wolf riders 61 to 70, heavy cavalry 64 to 66, elephants 36 to 44 on the mean; warriors 74 to 67, berserkers 56 to 53, archers 74 to 72. Pikes still beat all horse 100 to 0. The matrix printed below is from before these rules.

## Land, formations, calls for help, and a computer that comes on (2026-11-14, review3 item 2, review4)

The owner's direction: "land, formations, tactics, high ground, etc should factor in. Units shouldn't sit by idle and not help their peers. Teams should call for help, they should answer." And: "fighting from the water should be a penalty." Review 4 added: the outcome did not depend on the player (23 of 25 skirmishes won, with no orders on every map), and horse sent into a flank died to a man.

The rules are in `docs/GAME_DESIGN.md` §3.3 (Land, Formations, A cavalry attack, Nobody stands idle, Calls for help) and the manual's Rules of Battle; the code in `engine/sim/src/world_tactics.cpp`, `world_formation.cpp`, `world_help.cpp`, `world_combat.cpp`; tests in `tests/test_tactics.cpp`.

### What was there, and its size (the audit)

| Ground | Before | Now |
|---|---|---|
| Uphill, melee | -2% per 0.01 of grade, at most -20%; the climb slows the walk | -3%, at most **-35%**; and 25% slower to strike again |
| Downhill, melee | +2%, at most +30% | +3%, at most **+40%** |
| Volley from above | +5% a metre, at most +50%; reach +1 m a metre | the same |
| Volley shot uphill | reach shorter (a quarter at most); no loss of effect | the same reach; **-5% a metre, at most -40%** |
| Charge down a slope / up one | up to x2 / down to x0.25 | the same, and told |
| Wading | blows -50%, struck +50%, missiles +75%, shield half against arrows | the same for **every side**, and: shield half against blows too, his own missiles **-40%**, no brace, no formation |
| Mud, swamp | slow (50 to 55%), nothing else | blows **-25%**, struck **+25%**, missiles **+30%**, no brace, no formation |
| Woods | slow for horse (50%); trunks stop some arrows as obstacles | also: **40%** of arrows on a man there stopped; horse and closed ranks disordered; unseen beyond 25 m by men acting on their own and by the computer; the first blow out is an ambush (250 to the nerve) |
| Bridge | narrow: few fight at once | also: men on it count half in the census of the man at its end |
| Roads | every pace doubled | the same |
| Walls, gates | as built (§3.5) | the same |
| Formations | shapes only: no rule read them | every formation a strength and a price (the table in GAME_DESIGN §3.3), disorder, slow wheel |

### The open field was the Ford

The Skirmish "open field" was meant to stand on the first hill the continent offers, away from towns. No hill passed the test, and it fell back to Oakhaven Ford: both "maps" the reviews measured were the same river, with the player on the defender's bank. `fieldCentre` now takes the open ground nearest the ford with no water, bog or (but for an outcrop) rock where the hosts stand or between them, clear of the towns. It is level grass with a road through it and a rocky ridge along its south edge. The review scripts' clicks were read off the old ground, so their plans (`07`, `26`, `31`, `38`) no longer put anyone on a bank; they are kept as a check that nothing breaks, not as a measure of a plan.

### The computer

On the open field a host that walked in under a standing line's arrows lost to an army nobody commanded, at any quality of its own squad handling. So (`Battle::runEnemyAi`):

- **Bows first** (Normal, Hard): it comes up to a bowshot and stands, foot before bowmen, in loose order under the arrows that come back, and meets whoever comes out at it. It goes in when the foe's bows are down to a third of its own, when its own are down to half the foe's, when a fifth of it is fighting anyway, or after 60 s of shooting (Hard: 40 s).
- **One wing:** it goes in on the end of the foe's line nearer to it, all its foot together, and rolls the line up. Arrived, its men are let go and fight as men with no order do (close with what is near, help friends, answer calls, horse by the flank). The computer's hand on each squad in a melee did worse than the support stance: letting go was worth more than any target choice tried.
- **Formations by the rules:** a square when horse is coming at foot not yet fighting, loose under arrows with nobody at its throat, else its kind's own.
- **Purse (first pass; undone in the second, below):** in the open, Normal raised **125%** of the player's purse and Hard **150%**; Easy three quarters, as before. At an even purse the host that must come to the other lost to an army with no orders in every battle run (ten, over five versions of its play; the idle army at 1.4 to 2.2 kills per loss). Purse was the lever that settled it; no unit's numbers differ by setting.
- **Hard** also answers its own calls by command (`Command::Answer`: its squads always have an order, and the world only sends free men) and gives the bows less time.

### Measured

Whole battles, headless, through the game's own `Battle` and its orders (`illyria_balance tactics [set-piece seeds] [battle seeds]`, `tools/balance_battle.cpp`; `TAC_MAP`, `TAC_PLAN`, `TAC_SKILL` in the environment pick one). Open field, 450 gold, three seeds, kills for losses when the battle is decided. "Plan" is the scripted player's: the army where it stands, its foot in loose order while only arrows reach it and closed to two ranks when a foe is within 34 m. "Charge": everyone at the middle of the foe at once.

| The player | Easy (75% purse) | Normal (125%) | Hard (150%) |
|---|---|---|---|
| no orders | 1.3 (359 for 269), won 3 of 3 | 1.5 (597 for 407), won 1, lost 1, one undecided at 360 s | 1.5 (571 for 370), lost 3 of 3 |
| planned defence | 1.1 (412 for 384), won 3 of 3 | 1.4 (527 for 374), won 1, lost 2 | not run |
| plan plus micro | not run | 1.0 (374 for 375), lost 3 of 3 | not run |
| reckless charge | not run | 0.7 (246 for 378), lost 3 of 3 | not run |

(Easy and the micro row were run a few changes earlier than the rest, with the plan that marched the line to the ridge; the Hard no-orders row before the last one-line change to what counts as "flanked" for a call. Before any of this, on the old field and counted at 180 s, the same tool gave no orders 2.8, planned defence 2.2, plan plus micro 2.1, charge 2.9, all of them wins.)

The review's scripts on the play harness, one run each (real-time input: runs differ). Before is merge-master's build the same day, on the old field (the Ford's ground); after is this build, on the new field for `03`, `07`, `09`, `38`.

| Script | Before | After |
|---|---|---|
| `03` no orders | 2.7 (139 for 51), won | 1.2 (147 for 125), **lost** (the foe 260 men, was 208) |
| `07` the planned bank defence | 2.3 (145 for 62), won | 0.8 (100 for 127), lost (review4's `07`; there is no bank under it now) |
| `26` plan plus micro | 4.1 (150 for 37), won | not run |
| `09` everyone forward at once | 1.6 (155 for 98), won | 0.6 (75 for 129), lost |
| review4 `38` horse ordered at a flank | horse 0 of 8 each time (review 4) | Heavy Cavalry 8 of 8 and Light Cavalry 8 of 8 left; the battle lost (86 for 127) |
| `12` the Ford, no orders | 1.1 (71 for 64), won | 0.9 (83 for 93), won |

(Run on the build before the last one-line change: an arrow in the flank no longer makes a squad call "flanked".)

The set piece (flat ground, 8 seeds, at 60 s: 12 heavy infantry and 8 light cavalry against 24 light infantry):

| What the horse does | Before | After |
|---|---|---|
| No horse | 0.0 (0 for 95) | 0.0 (0 for 95) |
| Sent in at the front, through its own foot | 0.8 (79 for 100), the line broke 0 of 8 | 1.2 (107 for 92), 1 of 8 |
| Left on the wing with no order | 6.4 (191 for 30), 8 of 8 | 2.7 (166 for 61), 6 of 8 |

The wing lost ground: riders now come out of a flank charge and go in again, and the second time in they often meet a front that has turned, where they stay.

Reading it, and what is not there:

- **A plan does not yet double the exchange.** The target was a planned defence at twice the kills per loss of no orders. It is not met: on the new field the scripted plans (a line moved to the ridge; loose-then-closed formations; those plus pulling out mauled squads) all came out at or under no orders. An army with no orders now fights as one body (support, calls, horse by the flank), which is what was asked for, and it took most of what a simple plan used to add. What a plan can still add on this ground is not measured by any script here: where the line stands when the wing attack comes, horse held for the bowmen, a square at the right moment.
- **A bad plan is punished.** The charge is at half of no orders (0.7 against 1.5) and loses every battle.
- **No orders no longer wins on Normal, but does not yet reliably lose:** one win, one loss and one undecided of three headless, a loss in the script run; lost 3 of 3 on Hard; won 3 of 3 on Easy. The purse does most of it (see above).
- **A sound plan winning on Normal is not shown.** The scripted plans lose on Normal too. Normal at 125% may be too much for a player who plans; it has not been played by hand.
- **Flank charges happen, unevenly:** in the no-orders battles 9 of 63 of the player's charges landed in a flank or back in the last run and 23 of 95 in the one before, the computer's 93 of 251 (it was 1 to 3 charges a battle, all frontal). A rider who charged into a flank or back wheels out and comes again; one who charged a front that stood fights it out, as before.
- **Nobody watches:** see the squads that never fought, per run, in the tool's output; the Ford no-orders line is below.

The Ford, no orders (headless, three seeds): 1.0 (224 for 230), won 3 of 3. In the first seed the Lancers, the Elephants and the Pegasi fought and the Unicorns did not (they heal: healers go to the wounded within 40 m and do not answer calls); the Ferry Riders were still at their post at 150 s. In the script run (`12`) the Riders, Elephants, Pegasi, Griffins and Archons all fought and the **Lancers finished 8 of 8 again**, with the Unicorns 4 of 4. The nearest free squad answers each call; a squad that is never the nearest can still sit a battle out. Not fixed.

### Found on the way

- `World::tactics()` was never emptied between ticks: every word ever told was told again each tick (the presentation's six-second key hid it), and the list grew for the length of a battle. It is cleared with the other events.
- A square of five men had six places (`formationLayout`): the sixth march order went to a man who is not there (a crash, found when the computer began to form squares). Fixed, with a test of every formation at every size to 40.
- The results sheet's Killed column did not add up to the dead: men no squad is credited with (those who fled the field with a quitting host, blasts and fire with no hand behind them, the foe's own missiles) now have a row, "Others (fled the field, blasts, fire)".

### Second pass: orders that add, a default that only reacts, an even purse (2026-11-14)

The first pass ended with a planned defence at 1.4 against 1.5 for no orders, and Normal at 125% of the purse to make up for it. That was the wrong lever. The cause was that orders did not help:

- **A placed line did not stay placed.** Every squad, ordered or not, was in the support stance once its march ended, so a line set on a height walked down to the first fight within 40 m and answered calls from 70 to 160 m. The ground the player chose was given up by the stance. Now a squad is **posted** where an order's march ends: it fights what comes within 20 m of its post, helps a neighbour whose foe is that near, goes back after, and answers no call. Halt frees it.
- **The default army planned.** Left alone it answered calls from across the field and its horse rode for the flank unbidden: it played the commander's part. Now a squad left to itself reacts to what is near and no more: foot answer from 50 m, bowmen from 60, horse from 100, each a metre farther for every second since the side's first call (so the far wing comes late and piecemeal, but comes), and it goes straight to its friend. The ride round the foe's flank is given only by a command (`Command::Answer`: the player's full stop key, the computer's own answers on Normal and Hard).
- **The scripted plans were poor ones**: a line marched across the slope it meant to stand on, loose order re-formed as the foe arrived, mauled squads pulled out with their backs turned. The plan measured now: the line along the contour of the nearest rise, facing down it, pikes on its ends, bowmen a rank above, the lord and the rest behind, horse held on the wings. "Micro" on top of it: each call for help answered with the full stop key, and the lord's Rally.
- **Purse:** Normal is even again; Hard has 125% on the open field.

Open field after the merge with The March (the hosts 64 m apart, bowmen in front), 450 gold, headless, kills for losses when decided:

| The player | Normal, even purse, 5 seeds | Hard, 125%, 3 seeds |
|---|---|---|
| no orders | 1.0 (675 for 689), won 2, lost 3 | 0.8 (365 for 442), lost 3 |
| planned defence | 1.4 (812 for 580), won 3, lost 0, two undecided at 360 s | not run |
| plan plus micro | 1.5 (816 for 560), won 5 | 1.5 (570 for 373), won 2, lost 1 |
| reckless charge | 1.1 (778 for 715), won 2, lost 3 | not run |

Against the targets: no orders loses more often than not (met). A sound plan never lost and plan plus micro won every battle (met), at 1.4 to 1.5 times the exchange of no orders, **not the 2 times asked for**. The charge loses three in five but does **not lose badly**: with the hosts starting 64 m apart and the computer's bowmen in front of its foot, everyone forward at once reaches the bows before the foot is set. The computer does not yet pull its bows back through its line when charged at the start.

The Ford, no orders (3 seeds): 1.3 (232 for 180), won 3. The Lancers and the Ferry Riders now both fight (the far wing comes when the word reaches it). In the first seed the Ferry Pikes, the Unicorns and the Pegasi still had not fought when the battle was decided: one squad answers each call, and a battle can end before the word has gone round. Review scripts were not re-run on this pass.

Not mine: `tests/test_command.cpp` lines 613 and 818 (`a_shape_is_where_they_were_sent...`, `a_right_click_gives_the_tap_s_order...`) fail on merge-master itself, built clean beside this branch.

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
