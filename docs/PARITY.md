# Parity: Skirmish and Conquest are one game

*Written with step 3e of One World (2026-11-05). The rule (docs/ARCHITECTURE_RULES.md): Skirmish is the baseline, a
small battle on the same World; Conquest is the same World on the whole island, with no fence. Every capability of
one must be the other's by the same code. This is the audit, both ways, and the plan for what is left.*

How to read "how verified": **test** names a test that runs the scenario; **by construction** means both modes run the
same function on the same unit (no test of its own yet, and so a gap in the proof, not a proof); **exception** is
documented and user-visible.

## 1. Every skirmish capability, in Conquest

Since 3e every man of a Conquest is a `sim::Unit` of the one World at all times (docs/ONE_WORLD.md: no sleep or
wake). So "asleep" is gone as a column: the old `asleep` state is deleted. The one remaining out-of-World state is a man
aboard a boat (the ship carries him).

| Capability | Skirmish | Conquest | How verified |
|---|---|---|---|
| Pathfinding round walls, houses, trees, water | World nav | same World nav | **test** `a_march_across_a_walled_town_goes_round_its_walls` (0 samples of a man in a house or wall, over two marches out of the capital; before the change 14,544) |
| Ramps and wall-walks (archers climb to an embrasure) | World nav, ramps | same | **test** `archers_ordered_onto_the_wall_walk_climb_the_ramps_as_in_a_skirmish` (same order, same ground: 2,706 ticks in Conquest with the squad in the way of each other, 2,467 in a skirmish) |
| Gates: hack and break (attackers) | `World::gateToHack` | same | by construction (`test_castle.cpp` runs it in a skirmish world; Conquest siege tests `a_stack_marches_on_a_neutral_town_and_takes_it_by_fighting_there` use it) |
| Gates: the holder's men pass | `World::updateGates`: the gate swings open (three seconds, blocking all the while) to its holder's men bound through it and shuts behind them; an enemy never opens it | same (the rule is the World's) | **test** `a_gate_opens_to_its_holders_men_and_to_nobody_else`, `a_squad_ordered_through_a_closed_gate_opens_it_passes_and_it_closes_behind`, `an_enemy_squad_cannot_pass_a_closed_gate_and_does_not_open_it` (skirmish world) and the capital marches (Conquest) |
| Gates: the hand on the gate (open, shut, let go) | `Command::SetGate` through the one command list; tap a gate of one's own, `O`, the card's buttons | same command, carried by an `Order` | **test** `the_holder_pins_his_gate_open_or_shut_and_nobody_else_can`, `a_gate_swings_the_same_in_a_conquest_town_and_a_skirmish_castle` (one scenario, both modes, the same ticks), `a_gate_the_player_holds_open_or_shut_is_saved_with_the_game` |
| Gates: a garrison keeps its gate (archers on the walls, foot in a horseshoe behind the doors, the lure) | `World::keepGates` for every `GateState::garrisoned` gate (Siege battle's, a neutral town's, a computer lord's) | same function | **test** `a_neutral_garrison_puts_its_archers_on_the_wall_and_its_foot_behind_the_gate` (skirmish world); Conquest towns by `a_stack_marches_on_a_neutral_town_and_takes_it_by_fighting_there` |
| Formations (shapes, narrowing, facing drag) | Battle's `moveGroup`/`Shape` over unit Commands | the same Battle functions, since a Conquest man is a unit. A *march* on the coarse route uses `Campaign::layout` (block/column), a second layout code | by construction for orders within 200 m; **gap** for the march (see section 3, item 1) |
| Waypoints, attack-move, Charge, Hold, Find, Scatter | Battle | same Battle commands on units; far orders become a stack march | by construction; `the_players_men_in_a_fight_are_ordered_as_in_a_skirmish...` runs Hold, Scatter, Box, As they stand, Charge in a Conquest fight |
| Hero abilities and edges | World | same | by construction (no Conquest hero-ability test) |
| Stamina, flying | World; the fliers' take-off and rest | same World; the coarse planner times fliers by `flightPace` | by construction |
| Fliers never stuck: on and off a wall-walk, over curtain walls, after a quarry, in a queue, at a shut gate, in a garrison | `World::updateFlight`, `takesWing`, `insideParapet`, `heldAtGate` | same (the men are units) | **test** `a_table_of_every_flier_in_every_walled_scenario` (every flier type, seven scenarios, a Conquest capital and a skirmish on its ground: 52 runs, 25 stuck before the fix, none after; the longest stand with an order in hand was 140 s before and under 9 s after), `three_griffins_on_the_curtain_wall_come_down_when_told_and_do_not_stand_running_at_the_merlons` (the complaint's own picture) |
| Arrows, ballistics, engines | World | same | by construction |
| Morale, rout, rally | World | same | by construction (`test_morale_rally.cpp` is World-only) |
| Scavenging, raids, satchels | World | same | by construction |
| Captures (flag, banner) | CTF mode flag; Conquest banner is `Campaign::bannerStep` | **differs**: two rules (see section 2) | n/a |
| Group hotkeys 1-9 | unit ids | man ids (units leave the World only aboard a boat) | `a_group_in_the_conquest_is_of_men...` |
| Tab / Shift+Tab cycle the groups (saved groups, then squads in none) | `Battle::cycleGroup` over groups and squads | same (a Conquest squad is an army's men, a saved group is of men) | **test** `tab_cycles_the_saved_groups_then_the_squads_in_none_and_shift_tab_goes_back` (a skirmish); Conquest by construction (same function, `groupMen`/`squads_`) |
| Queued orders (Shift + right-click appends; Space and a plain order clear) | `Unit::queue`, `World::advanceQueue` | same (the men are units); a stack's march beyond the men's reach has no queue (**exception**: a plain march, with a notice) | **test** `a_man_given_a_then_b_arrives_at_a_then_goes_on_to_b`, `a_plain_order_replaces_the_orders_in_hand_and_the_queue`, `halt_clears_the_queue_too`, `the_orders_queued_behind_a_mans_own_are_saved_with_the_game` (Conquest), `shift_right_click_queues_and_a_plain_one_replaces_and_stop_clears` (a skirmish) |
| Planning mode (hold orders, send them together) | `Battle` holds per-man orders; sends the same Commands at release | same Battle, same Commands; a far march of squads is held as such | **test** `planning_holds_orders_off_the_sim_and_the_release_sends_them_in_one_tick_as_if_given_then`, `a_plain_order_held_replaces_the_held_ones_and_backspace_drops_them_all`, `halt_while_planning_halts_at_once_and_drops_that_mens_held_orders_and_the_dead_are_skipped` (a skirmish); Conquest by construction (`unitOfPlanned` finds the man) |
| Camera, selection, command bar, minimap | Battle | Battle (`Battle::borrow`) | `test_campaign_battle.cpp` |
| Sounds, voices, effects | Battle `collectEvents` | same | `the_battle_presents_the_campaigns_fight_as_its_own` |
| A sleeping man selectable and orderable | n/a | now every man; no sleepers | `a_tap_selects_a_man_and_the_card_selects_his_squad` |
| Save and load of the state | skirmish saves nothing | `Campaign::save` takes every man out of the World and puts him back (arrows in the air and cooldowns are not kept), so the saved game and the loaded game carry on as one | `test_campaign_save.cpp` (checksum equal after a save in the middle of a fight) |

Fixed by this step: the cause of the complaint (a stack was one body on the coarse grid and walked through walls) is
deleted; marching men now go round walls through the World's nav, and the gate rule above was needed to let a town's
own men out of their own walls.

Known weaknesses that remain (honest list, not exceptions):

- The coarse route is planned with no knowledge of walls, so the first waypoints can lead round a long way
  (measured: a march from the capital eastward goes out by the south gate and round the curtain; two of six
  marches in the test needed more than 400 s). The World finds the way each leg, so it is correct but not clever.
  The fix is to plan long legs with the World's nav near towns, or to order the final destination straight
  (a 4 m grid search over 3 km is the cost the document warned about).
- A gate is a bottleneck: a stack of 24 men leaving a castle by its one gate takes minutes (the same as a skirmish's
  six defenders took 40 s to pass); the old body march had none of that. It is the World's rule, now also Conquest's.
  Measured (towns held by the computer lords at day 25, all-computer campaign, seeds 7 / 11 / 21): 35 / 36 / 36 at d6c56db;
  22 / 25 / 26 with garrisoned gates at the first cut, 29 / 23 / 25 after the recall reached 70 m; 37 / 37 with the gate keeping
  switched off; with only the neutral towns kept 33 / 20 / 23. So a neutral garrison holding its gate (archers up on the walls,
  foot behind the doors, no sortie to die in the open) is what slows the lords: about 28% fewer towns at day 25. Counting the
  gate in the dice (+1, +2, +3 strength points for a defending garrisoned town) did not speed them: they simply attacked less.
  What would is more means of taking a held gate (catapults beyond the three capitals, or bigger strike stacks), not a weaker garrison.
  The horseshoe's geometry (`keepGates`, `Fixed`) could serve the player's formation picker (`Formation::Encircle` is in floats
  in the game layer); it is not shared yet.
  Still weak with gates: the doors now take three seconds to swing, so a long column waits at each swing; a march
  through two rings (a capital's outer and inner gate) is routed to one gate and the straight line after it
  (`World::routeToGate` plans one gate), not through both; an enemy that follows a lured stream can hold the gate open
  (nobody may be swept by the doors).
- The coarse planner's flier estimate is optimistic by about 2.2 times against the World's flight cycles
  (`fliers_alone_go_at_their_flight_cycles_pace_over_anything` measures it); the lords plan with it.
- The cost of the whole population in one World (section 4 of docs/ONE_WORLD.md, "What it costs"): 19 ms a tick native
  and 41 ms in the browser at 7,000 men, against 33 ms at 1x. Campaign tests that play days (20 or more of them) take
  minutes each now (`hard_lords_outplay_easy_ones` is about 25 minutes native): they want shorter plays or fewer men.
- A stack's march sends each man to `anchor + slot` of `Campaign::layout`. Slots in a tree or a wall's foot are moved to
  the nearest open ground; a column through a sharp turn has no tail-speed control (the tail follows its own path
  at the squad's pace; the old "tail moves fast" fault is not reproduced, but not tested either).
- Formations and Scatter ordered on a march: they work in the World (the men are units) but a march in progress
  re-sends the layout slots at the next cell and overrides the shape: the previous worker's "a conquest group does not
  keep its formation shape" gap is not closed.

## 2. Every Conquest-only capability, in Skirmish

Per the principle: anything that can happen in the campaign can also happen in a skirmish; the rules and the way the
world works are identical.

| Capability | Where it lives now | In a skirmish? | What it needs |
|---|---|---|---|
| Hero enters a ruin, fights the guardian, takes the treasure, artifacts | `Campaign::search`, `campaign_quests.cpp`, `Continent::Site` | no | A `Site` as a World place (an object with a guardian and a prize) and an `Enter` command; the World's sites already exist as `Region` obstacles (ruin floors) |
| Enter a castle and produce a unit | `Campaign::produce`, `CityState` options, `newDay` | no | A castle as a World object with a stock, a production queue and gold per side; a `Produce` command |
| Hire heroes, companions | `Campaign::hire`, `Offer` | no | After production exists |
| Capture of a town (banner over the courtyard) | `campaign_capture.cpp` | CTF has a flag, not this | The banner rule as a World rule on a castle |
| Gold, income, upkeep, desertion | `Campaign::newDay`, `Side` | no | Economy in the World |
| Day clock | `Campaign::kTicksPerDay`, `newDay` | no | A day as a World tick count |
| Boats, ports, shipwrights | `campaign_sea.cpp` | no | Water as World ground a ship moves on; boats as World units that carry units |
| Quests, temples, sages | `campaign_quests.cpp` | no | Sites as World objects |
| Computer lords' planning | `campaign_ai.cpp` | no (Battle AI plays squads) | Their plan as a computer player of the World |
| Territory, outposts | `campaign_territory.cpp` | no | Hexes as a World overlay |
| Fog of war | `Campaign::canSee` | no | The same seam on the World |
| Route planner on the coarse grid | `campaign_march.cpp`, `findPath` | no | Long legs by the World's own nav, then it is deleted |
| Stacks (groups of up to eight squads) | `Campaign::Stack` | a squad is the skirmish's group | Stacks become the World's squad groups |

Done in this step as small World rules: a gate opens to its holder's men (`World::updateGates`, usable in a
skirmish and tested there). Not done: the rest, in the plan below.

## 3. Campaign class dissolution plan

The `Campaign` class (`engine/sim/include/illyria/sim/campaign.h`, `campaign_*.cpp`, about 4,700 lines) is what is left to
move into the World/Region/mode rules or to delete. Order is by what each step needs from the one before.

| # | Step | What moves | Size | Risk | Needs |
|---|---|---|---|---|---|
| 1 | **One World ground for Conquest** (done as far as the men): the island is a `Region`, every man a unit, the stack steers its men | `campaign_world.cpp`, `campaign_march.cpp` body-stepping deleted | done | low | nothing |
| 2 | **Sites and ruins as World places**: guardian, treasure, artifacts, temple blessing, sage; `Enter`/`Search` commands | `campaign_quests.cpp` (409), `campaign_rare.cpp` (178) | medium (about 600 lines, plus UI) | medium: heroes' item bookkeeping, saves | 1 |
| 3 | **Castles produce**: stock, queue, options, gold per side, hire; `Produce`/`Hire` commands; the "day" becomes a World tick count a mode sets | `produce`, `newDay` training, `hire`, `campaign_rules.cpp` (389) | medium-large (about 700) | medium: balance, AI; economy golden tests | 1 |
| 4 | **Capture as a World rule**: banner and flag are the same rule on a castle | `campaign_capture.cpp` (154), CTF flag | small | low | 3 |
| 5 | **Boats**: water ground in the World, boats as units that carry units; deletes the "out of the World aboard" exception | `campaign_sea.cpp` (526) | large | high: nav on water, fliers, landing, AI | 1 |
| 6 | **The computer lords as World players**: their planning (`runAi`, attack, defend, economy) as a player agent that issues commands, not code in the World | `campaign_ai.cpp` (749) | large | high: tuning, determinism, speed | 2, 3, 4 |
| 7 | **Territory and hexes**: a World overlay a mode reads | `campaign_territory.cpp` (165) | small | low | 1 |
| 8 | **Fog** on the World: the one `canSee` seam moves | `canSee` and its callers | small | low | 1 |
| 9 | **The route planner deleted**: long legs planned by the World's nav in the window ahead of the men (the doc's "refined on the 4 m grid"), the coarse grid kept only for the lords' estimates until 6 | `campaign_march.cpp` (340), `findPath` | medium | medium: cost of long searches | 1 |
| 10 | **Saves and orders**: a World/command-list save; `Campaign::Order` becomes the World's `Command`; delete `Campaign` | `campaign_save.cpp` (533), `Campaign::step` | medium | medium | 2 to 9 |

Headline: ten steps; 1 is done; 2, 3, 4, 7, 8 are independent small-to-medium World rules that can run in parallel;
5 and 6 are the big, risky ones; the class is deleted last. Steps 2 and 3 give the user's two examples (a hero
entering a ruin, a castle producing a unit) in a skirmish.

## 4. Documented exceptions (user-visible)

1. **Aboard a boat a man is out of the World** (until step 5). He is not drawn on the deck and cannot be ordered
   until he lands.
2. ~~Neither mode lets the player open or shut a gate by hand.~~ Done (gates swing; `Command::SetGate`; see the table).
3. **The conquest's speeds (1, 2, 4, 8) differ from the skirmish's**; at 8x with the island's full population the
   frame may not hold the clock (see docs/ONE_WORLD.md, "What it costs").
