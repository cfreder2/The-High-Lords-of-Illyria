# War on the Map {#war}

::: lore
*"There is no map and no battle. There is only the ground, and who is standing on it."*

— Brannoc the Bold
:::

## Ordering men {#war-orders}

The war is a battle seen from as high or as low as you like, so your men are ordered as in any battle (chapter [](#orders)): tap a soldier to select him, tap him again for all of his kind in view, hold and drag to box them, tap a card to find a hero; then the same row: **As they stand**, a **formation**, **⚔ Attack**, **Scatter**, **Hold**, **Find**, and **Charge** at the top. It makes no difference whether a man is asleep in his stack on the march or awake in a fight.

- **In a fight** (and near it, within about 200 m of the men), an order is the battle's own: they march to the place in their formation, go after the foe you tap, hold, scatter. Once ordered, a stack's men stand to your orders as in a skirmish until you march them again, or until the fight is over.
- **Further off, or for men asleep,** a tap on the ground, on a town, on a gate or on an enemy is a **march**: the **squads** of the men selected (a squad is an army: its soldiers all go) set off for it along the quickest road, apart from the rest of their stack if you chose only some of its squads, at their pace, and meet whatever is in their way. **Hold** halts them at the next cell. Formations and **Scatter** are for men on their feet and in a fight: asleep, they keep the formation chosen for when they are.
- Men who are not yours, and gates, are looked at when you tap them with nothing selected: a grey card with his life (or the gate's hit points), view only, and no order for them.

## Attacking {#attacking}

To attack, order your men into an enemy stack, an enemy city or a neutral town: tap the enemy soldiers or the town with your men selected. The march ends at the enemy: the attack is its last step, at the stack's pace. Attacking is also the only way to take a city.

There is no prompt. When two stacks of different lords come within a long bowshot of each other (about 150 m between their edges), and either is on the move or already fighting, **their men are in a fight**: from then on every man in them is stepped in full by the battle's rules (chapter [](#battlefield)), where he stands. The soldiers on the map are those men. Zoom out and you see them as points in their lords' colours, and a man who is fighting flashes, rapidly and at random in strength; zoom in and the points become the battle's own soldiers, striking, shooting and flinching, standing exactly where the points were. A stack marching past a town's garrison at rest behind its walls does not wake it; one that comes up to the walls does.

When nobody hostile has been near for ten seconds, the fight is over: each man stays where it left him, with his wounds, his kills, his rank and his morale, and walks back to his place in his squad. The dead lie about for twenty seconds, then are taken away. Nothing was rolled: the men who are left are the men who survived.

Every stack, yours and the computer lords', marches its route with the battle's initiative, fighting whoever strikes it or crosses its path; standing, its men meet whoever comes within about 45 m of where they stand (missile troops close to a good bowshot and shoot), and a garrison holds its post. **Fight!** brings the camera to the fiercest fight on the island, and the minimap glows where men are fighting.

## How the computer lords reckon a fight {#dice}

Before it attacks, a computer lord reckons its chances. It does so with the old *Warlords* dice rules, which survive only for this: an estimate, worked out exactly, of whether an attack would succeed. It decides whether to go, never what happens when it gets there.

The estimate follows the *Warlords* appendix:

1. Each side's armies are **lined up**: the weakest first, the strongest last, and heroes, the most valuable, last of all.
2. The first army of each side is set against the first of the other, **one on one**.
3. Each round, both are given a ten-sided die. A roll no higher than the **other** army's strength is *low*; higher is *high*.
4. If both roll low, or both high, nothing happens. If one rolls low and the other high, **the low roller takes a hit**.
5. **Two hits destroy an army.** The survivor goes on against the next in the other line.
6. It ends when one side has no armies left.

So the lord thinks an army is hit more often the stronger its foe is, and hits more often the stronger it is itself. The strengths are not whole numbers, because of the small edges (below): an army's reckoned strength is

> (its strength + the force modifier) × (1 + its edges), never above 9 nor below 1.

## The force modifier {#force-modifier}

As in *Warlords*, the lord adds whole points to the strength of every army in a side's line for:

| Modifier | Worth |
|---|---|
| A flier in the stack (pegasi, griffins, dragons, archons) | +1 |
| A special army in the stack (wizards, undead, devils, demons, dragons) | +1 (a dragon counts for both) |
| A hero's command artifacts | +1 to +3 each (chapter [](#artifacts)) |
| Defending a city: walls 2 to 6 / 7 or 8 / 9 | +1 / +2 / +3 (walls 0-1: nothing) |
| Each catapult in the attacking stack, against a city's walls | takes one point off the walls' bonus |
| A catapult attacking a city | fights 2 points stronger itself |

In a real fight none of this is a number: a flier flies over the wall, a catapult's stone breaks the real gate, a hero's artifacts count as the blessings they always were, and the walls are the walls.

## The small edges {#edges}

Like the fittings on a good sword, each worth a few percent, the edges add up, and none decides a fight alone. A weaker army with every edge is still the weaker army. The lords reckon with them; and in a set battle the same edges, with the same numbers, count in every blow.

| Edge | When | Worth |
|---|---|---|
| **Home land** | the battle is fought in a hex of your side's land | +3% |
| **Familiar ground** | the army's kind likes the ground (elves in forest; dwarves and griffins in hills; pegasi in forest; the dead in marsh), +4%; and its lord's liking, +2% a point | at most +6% |
| **Veterans** | regular +3%, veteran +6%, elite +9% (soldiers; a hero's levels are his strength) | at most +9% |
| **A hero leads** | the best hero in the stack: strength 4-6 +2%, 7-8 +4%, 9 +6% | at most +6% |
| **High ground** | fighting from 3 m or more above the other side's cell | +4% |
| **Holding** | a defender holding a temple, a ruin's stones or an outpost tower | +4% |
| **All together** | | **at most +15%** |

The lords' likings are the *Warlords* empires' (by archetype): the Giants like hills; the Dwarves hills (two points); the Orcs and Malvoren marsh; the Elves forest; the Riders roads and plains; the Order the water and the shore; the Throne does not mind where it fights. The minuses of the old tables are gone: bad ground already costs movement on the map and speed on the battlefield, and fighting away from home costs nothing but the edge you lack.

What they are worth: four strength-4 armies against four are a coin toss; home land alone makes it 53 in 100; veterans 56; every edge together (+15%) about 69. Three and a half against four with every edge still loses.

## A lord's reckoning, round by round {#dice-example}

Day two. Your lord, **Lord of Kingshold**, a hero of level 1 (strength 4), marches out with his escort of light infantry (3) and the pegasi of the Throne (6) against a neutral town with walls 4, held by a garrison of light infantry (3) and archers (4).

*The force modifiers.* You have a flier: +1 to all three of yours. The town's walls (2 to 6) give its defenders +1. *The edges.* Your hero (strength 4) leads: +2%. Nobody is on his own land; nobody has the high ground.

*The lines.* Yours: light infantry (3 + 1) × 1.02 = 4.08; pegasi (6 + 1) × 1.02 = 7.14; and the hero last, (4 + 1) × 1.02 = 5.10. Theirs: light infantry 4, archers 5.

Your light infantry and theirs are evenly matched: each round, either may take the hit. Say yours fall after wounding theirs once. The pegasi step up at 7.14 against a wounded 4: they take a hit only one round in five that anything happens, and finish them. Against the archers (5) the pegasi still have the better of it, nearly three to one. Your hero, last in line, may never draw his sword.

Worked out exactly, as the computer lords do it, this attack looks like succeeding **91 times in a hundred**. Without the pegasi, the hero and his light infantry alone would win only **3 times in 10**: the flier's point is worth more than any army in the stack.

::: tip
The A-team of *Warlords* still works: a hero with artifacts, a flier, a special army, and strong armies behind them. One griffin adds a point to every army in the stack.
:::

## Sieges and walls {#siege-map}

A town defends with every man inside it, however many, and is attacked by every man who marches on it. Its walls, towers and gate are real, and so is everything that happens to them (chapter [](#sieges)): your men march up to the gate and hack at it, their archers shoot from the walk, your catapults throw stones, and what is hit stays hit. Catapults (trained at Kingshold, Deepforge and Dreadspire) are weak in the field and slow on the road (6 points, no marshes), but a stone is what breaks a gate.

## Raising a banner {#banners}

**Beating a town's defenders does not take the town.** When its men are dead or gone, or you march into a town nobody defends, your army stands in its courtyard and **raises its banner**. The town is yours when the banner reaches the top. What is counted is men: the banner rises only while the men of one lord stand in the courtyard, and none of the town's own or of any third lord's.

- **More troops raise it faster.** A lone army takes about a minute at normal speed (13 hours of the campaign's day), four armies half that, a full stack of eight about 19 seconds.
- **Enemies in the town stop it.** Armies of any other lord with men in the town contest it: while they are as many as yours or fewer, the banner stops; while they are more, it comes down as fast as it would rise for them.
- **Leaving gives it up.** If your armies march out, or fall, the attempt is abandoned and its progress lost.
- **At the top** the town is yours: its income, its training (stopped, for you to choose again), the loser's share of its treasury (half to you), its walls one lower, and its own hex and the six round it.
- **A town being taken trains nothing** and takes in no new armies until the raisers are gone.

The bar under the town's name says *Your banner rising · 45%*, *held up* (amber) or *coming down* (red); over the keep a tall pole carries the old colours down and the new ones up. The courtyard's flagpole of the set siege battle is a different rule (chapter [](#capture-point)).

![A banner going up over a newly won town, with its progress bar.](images/shots/camp_banner.jpg)

::: tip "Hold the gates"
A computer lord on Hard answers a banner going up over one of its towns at once: its nearest stack that can beat the raisers marches on them, or one that can reach the walls stands next to the town to hold the banner up. Bring enough armies to raise it quickly, and something to guard the road.
:::

## Territory and outposts {#territory}

The land itself is held, hex by hex. The hexes are drawn faintly on the map, each washed in its holder's colour, stronger at the rim and the firmer the claim; the top bar shows ⚑ and how many you hold.

- **Claiming.** Ten times a day each hex of land is reckoned. Where one lord's armies stand and nobody else's, that lord's claim grows: 10 points, and 5 more for each army there (four at most), to 100. Another lord's claim is worn away first, then the hex changes colour. Where two lords stand, nothing changes. Where nobody stands it fades a point at a time (a full claim in ten days) and is nobody's at nothing.
- **Towns** claim their own hex and the six round it as two armies would, so a town's land is its lord's unless an enemy stands on it; taking a town hands you that ring at once at half a claim.
- **Outposts.** A tower stands at each of the 24 river crossings. An outpost's hex is claimed twice as fast, and once held (half a claim or more) it claims the six hexes round it as one army would. On the map each shows as ♜ with its name, in its holder's colour.
![Hexes washed in their holders' colours: here Kingshold's land, gone orange after the town fell while its lord was away.](images/shots/camp_land.jpg)

- **Worth.** Land gives the **home land** edge (+3%) in the lords' reckoning (and in a set battle fought on it). A defender standing in an outpost's hex has the **holding** edge (+4%). And land pays: one gold a day for every five hexes.
