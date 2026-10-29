# Voice lines: the recording script

Generated from `game/web/voice/lines.json` by `python3 tools/voice/script.py`; do not edit by hand.
Every line the soldiers can say, by voice and situation, with its file name and how to deliver it.
A printable tick-list of the same files is in [VOICE_CHECKLIST.md](VOICE_CHECKLIST.md).

## Recording guide

**Setup.** A quiet, soft room (a wardrobe full of clothes beats a bare kitchen). A pop filter, the
mouth about 30 cm from the microphone, a little off-axis. Record **mono, 48 kHz, 24-bit WAV**.
Set the gain so the loudest shouts peak around **-6 dBFS** and never clip; quiet lines will be
brought up later, so do not move closer for them.

**Takes.** Record **2-3 takes** of every line, with about **0.5 s of silence** before and after
each. Keep the best take; mark it by naming the file without a take suffix (or with `_a`, `_b`,
`_c` for the takes: the converter keeps `_a` unless you delete it).

**Delivery.** These are barks, not speeches: short, in character, as if heard across a battlefield
(Myth, Warlords). Shout the shouted lines for real, from the chest, standing up. Vary the takes
(pitch, speed, emphasis); the game picks a different line each time, so variety is worth more than
perfection. The **delivery note** on each line says how. The enemy plays the same recordings pitched
lower, so one actor per voice is enough; a different actor for each voice is better.

**Non-verbal voices** (Brute, Undead, Fiend, Beast) have no words: the text in brackets describes
the sound. Growl, moan, hiss, roar; layering two takes, or an animal recording, is fine.

**File names.** Name each take exactly as listed, in a folder per voice, e.g.
`soldier/hold_line_2.wav` (or `soldier/hold_line_2_a.wav` for take a). Lower case, no spaces.

## Putting the recordings in the game

1. Put the WAV files in one folder per voice, e.g. `~/takes/soldier/selected_1.wav`.
2. Run the converter (needs `ffmpeg`):

       tools/voice/convert.sh ~/takes

   It trims the silence, normalises each line to **-16 LUFS** (true peak -1.5 dB), makes it mono
   and writes **MP3** (48 kHz, ~96 kbps, plays on iPhones) to `game/web/voice/<voice>/<name>.mp3`,
   then updates `game/web/voice/available.json` and the checklist.
3. Rebuild (or just reload the page in a dev build). Every recorded line replaces its placeholder
   (the browser reading the text aloud); lines not yet recorded keep the placeholder, so you can
   record a few at a time. The placeholder can be switched off in the ⚙ settings.

## Counts

| Voice | Used by | Situations | Lines |
|---|---|---:|---:|
| Soldier (`soldier`) | Light Infantry, Heavy Infantry, Pikemen, Warriors | 27 | 93 |
| Archer (`archer`) | Archers | 27 | 92 |
| Dwarf (`dwarf`) | Dwarves, Dwarf Grenadiers | 29 | 100 |
| Rider (`rider`) | Light Cavalry, Heavy Cavalry, Wolf Riders | 27 | 92 |
| Hero (`hero`) | Heroes | 24 | 83 |
| Wizard (`wizard`) | Wizards, Journeymen | 28 | 96 |
| Berserker (`berserker`) | Berserkers | 27 | 91 |
| Archon (`archon`) | Archons | 27 | 90 |
| Engine crew (`engine`) (else soldier) | Catapult | 17 | 58 |
| Brute (`brute`) | Giants, Minotaurs | 15 | 49 |
| Undead (`undead`) | Undead | 11 | 38 |
| Fiend (`fiend`) | Devils, Demons | 14 | 46 |
| Beast (`beast`) | Spiders, Griffins, Pegasi, Elephants, Unicorns, Dragons | 14 | 46 |
| Herald (`herald`) | (the battle's narrator) | 3 | 9 |
| **Total** | | | **983** |

## Situations

| Id | Situation | When | Priority |
|---|---|---|---:|
| `selected` | Selected | The player picks him (or his squad). | 3 |
| `move` | Move ordered | Acknowledging a march order. | 3 |
| `attack` | Attack ordered | Acknowledging an order to attack. | 3 |
| `charge` | Charge | Charging: ordered, or riders breaking into the gallop. | 2 |
| `charge_seen` | Incoming charge | Horse thundering down on them. | 2 |
| `hold_line` | Hold the line! | A veteran's rally call to wavering friends nearby. | 4 |
| `hero_rally` | Hero rallies | A hero's rally call to wavering or broken troops. | 4 |
| `rallied` | Rallied | Turning back to the fight after breaking. | 2 |
| `wavering` | Wavering | Fear creeping in; morale low. | 1 |
| `breaking` | Breaking | Broken: dropping everything and running. | 2 |
| `hero_fell` | Our hero fell | Their own hero cut down near them. | 5 |
| `enemy_hero_fell` | Enemy hero fell | The enemy's hero cut down. | 4 |
| `friend_killed` | Friend killed | A comrade killed beside him. | 1 |
| `kill` | First kill | His first kill of the battle. | 2 |
| `vet_kill` | Veteran's kill | A veteran's kill: a taunt. | 1 |
| `promoted` | Promoted | Rank up (a hero: level up). | 2 |
| `wounded` | Badly wounded | Down to a third of his strength. | 1 |
| `arrow_fire` | Under arrow fire | Arrows falling on them. | 1 |
| `flanked` | Flanked | Struck from the side or behind. | 1 |
| `friendly_fire` | Friendly fire | Hit by his own side's arrows or bombs. | 2 |
| `fuse` | Fire in the hole | A grenadier lights and throws a satchel. | 2 |
| `healing` | Healing | A healer mends a wounded friend. | 1 |
| `engine_loose` | Engine looses | The catapult throws a stone. | 1 |
| `gate_broken` | Gate broken | The gate gives way. | 4 |
| `enemy_sighted` | Enemy sighted | First contact with the enemy. | 3 |
| `victory` | Victory | The battle is won. | 5 |
| `defeat` | Defeat | The battle is lost. | 5 |
| `idle` | Idle chatter | Rare, when nothing has happened for a while. | 0 |
| `pain` | Hurt | Struck and hurt, still standing: a grunt or a cry. | 1 |
| `death` | Death cry | Killed: a scream, or a last grunt as he goes down. | 2 |
| `death_moan` | Shot dead | Killed by an arrow: a short, choking moan. | 2 |
| `cackle` | Grenadier's glee | A grenadier whose satchel has just killed: a manic cackle. | 2 |
| `groan` | Undead groan | The walking dead, idly: a groan, a click of the jaw. | 0 |
| `casualties` | Casualties | A quarter of the player's soldiers have fallen. | 4 |
| `severe_casualties` | Severe casualties | Half of the player's soldiers have fallen. | 4 |
| `total_casualties` | Total casualties | Four fifths of the player's soldiers have fallen. | 5 |

Priority 5 is the gravest news (always heard); 0 is idle chatter (heard only when all is quiet).

## Soldier (`soldier`): 93 lines

*Used by:* Light Infantry, Heavy Infantry, Pikemen, Warriors.  
*Casting:* A seasoned foot soldier of thirty or forty: plain-spoken, rough, dependable. Myth's dwarf-less warriors; no accent needed.

### Soldier: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `soldier/selected_1.wav` | Sir? | alert, ready; at speaking volume, as if the captain just turned to him |
| `soldier/selected_2.wav` | Orders? | alert, ready; at speaking volume, as if the captain just turned to him |
| `soldier/selected_3.wav` | Ready. | alert, ready; at speaking volume, as if the captain just turned to him |
| `soldier/selected_4.wav` | Aye, captain? | alert, ready; at speaking volume, as if the captain just turned to him |
| `soldier/selected_5.wav` | Standing by. | alert, ready; at speaking volume, as if the captain just turned to him |

### Soldier: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `soldier/move_1.wav` | Moving. | brisk acknowledgement, already moving; clipped |
| `soldier/move_2.wav` | On our way. | brisk acknowledgement, already moving; clipped |
| `soldier/move_3.wav` | Aye. | brisk acknowledgement, already moving; clipped |
| `soldier/move_4.wav` | As you say. | brisk acknowledgement, already moving; clipped |
| `soldier/move_5.wav` | Marching. | brisk acknowledgement, already moving; clipped |

### Soldier: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `soldier/attack_1.wav` | At them! | eager, aggressive; raised voice |
| `soldier/attack_2.wav` | Engaging! | eager, aggressive; raised voice |
| `soldier/attack_3.wav` | For the banner! | eager, aggressive; raised voice |
| `soldier/attack_4.wav` | Cut them down! | eager, aggressive; raised voice |
| `soldier/attack_5.wav` | With me, lads! | eager, aggressive; raised voice |

### Soldier: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `soldier/charge_1.wav` | Charge! | shouted over battle, full chest, rising |
| `soldier/charge_2.wav` | Into them! | shouted over battle, full chest, rising |
| `soldier/charge_3.wav` | Forward, all! | shouted over battle, full chest, rising |

### Soldier: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `soldier/charge_seen_1.wav` | Horse! Brace! | urgent warning shout, alarmed |
| `soldier/charge_seen_2.wav` | Cavalry! | urgent warning shout, alarmed |
| `soldier/charge_seen_3.wav` | Set your feet! | urgent warning shout, alarmed |

### Soldier: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `soldier/hold_line_1.wav` | Hold the line! | commanding bellow over battle, steady and fierce |
| `soldier/hold_line_2.wav` | Stand fast! | commanding bellow over battle, steady and fierce |
| `soldier/hold_line_3.wav` | Shields up, hold! | commanding bellow over battle, steady and fierce |
| `soldier/hold_line_4.wav` | Nobody runs! | commanding bellow over battle, steady and fierce |

### Soldier: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `soldier/rallied_1.wav` | Back to it. | catching breath, regaining nerve; gruff, half ashamed |
| `soldier/rallied_2.wav` | I'm with you. | catching breath, regaining nerve; gruff, half ashamed |
| `soldier/rallied_3.wav` | Never again... | catching breath, regaining nerve; gruff, half ashamed |

### Soldier: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `soldier/wavering_1.wav` | Steady... steady... | breathless, afraid; under the breath |
| `soldier/wavering_2.wav` | There's too many... | breathless, afraid; under the breath |
| `soldier/wavering_3.wav` | Gods help us. | breathless, afraid; under the breath |

### Soldier: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `soldier/breaking_1.wav` | Run! | panicked scream, running, voice cracking |
| `soldier/breaking_2.wav` | Fall back! Fall back! | panicked scream, running, voice cracking |
| `soldier/breaking_3.wav` | I'm not dying here! | panicked scream, running, voice cracking |

### Soldier: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `soldier/hero_fell_1.wav` | The lord is down! | stricken, disbelieving shout |
| `soldier/hero_fell_2.wav` | No! He's fallen! | stricken, disbelieving shout |
| `soldier/hero_fell_3.wav` | Our captain's dead! | stricken, disbelieving shout |

### Soldier: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `soldier/enemy_hero_fell_1.wav` | Their champion's down! | triumphant shout over battle |
| `soldier/enemy_hero_fell_2.wav` | He's dead! Push! | triumphant shout over battle |
| `soldier/enemy_hero_fell_3.wav` | Their lord has fallen! | triumphant shout over battle |

### Soldier: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `soldier/friend_killed_1.wav` | Tom! No! | shocked, grieving cry; short |
| `soldier/friend_killed_2.wav` | Man down! | shocked, grieving cry; short |
| `soldier/friend_killed_3.wav` | They got him! | shocked, grieving cry; short |

### Soldier: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `soldier/kill_1.wav` | Got one! | grim, surprised satisfaction; mid-effort |
| `soldier/kill_2.wav` | Down you go. | grim, surprised satisfaction; mid-effort |
| `soldier/kill_3.wav` | That's one. | grim, surprised satisfaction; mid-effort |
| `soldier/kill_4.wav` | First blood! | grim, surprised satisfaction; mid-effort |

### Soldier: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `soldier/vet_kill_1.wav` | Stay down. | cold taunt, contemptuous; spoken to the fallen |
| `soldier/vet_kill_2.wav` | Next! | cold taunt, contemptuous; spoken to the fallen |
| `soldier/vet_kill_3.wav` | Too slow. | cold taunt, contemptuous; spoken to the fallen |
| `soldier/vet_kill_4.wav` | Is that all? | cold taunt, contemptuous; spoken to the fallen |

### Soldier: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `soldier/promoted_1.wav` | Not bad for a farm boy. | proud, grim pleasure; low |
| `soldier/promoted_2.wav` | I've learned a thing or two. | proud, grim pleasure; low |
| `soldier/promoted_3.wav` | Call me sergeant. | proud, grim pleasure; low |

### Soldier: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `soldier/wounded_1.wav` | I'm hit! | pained grunt with words; through gritted teeth |
| `soldier/wounded_2.wav` | Argh... still standing. | pained grunt with words; through gritted teeth |
| `soldier/wounded_3.wav` | Bleeding bad... | pained grunt with words; through gritted teeth |

### Soldier: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `soldier/arrow_fire_1.wav` | Arrows! | urgent warning, ducking; shouted |
| `soldier/arrow_fire_2.wav` | Shields up! | urgent warning, ducking; shouted |
| `soldier/arrow_fire_3.wav` | Incoming! | urgent warning, ducking; shouted |

### Soldier: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `soldier/flanked_1.wav` | Behind us! | alarmed shout, turning round |
| `soldier/flanked_2.wav` | On our flank! | alarmed shout, turning round |
| `soldier/flanked_3.wav` | Turn, turn! | alarmed shout, turning round |

### Soldier: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `soldier/friendly_fire_1.wav` | Watch your aim! | angry shout back over the shoulder |
| `soldier/friendly_fire_2.wav` | We're on your side! | angry shout back over the shoulder |
| `soldier/friendly_fire_3.wav` | Oi! Not us! | angry shout back over the shoulder |

### Soldier: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `soldier/gate_broken_1.wav` | The gate's down! | jubilant (attackers) shout over battle |
| `soldier/gate_broken_2.wav` | We're through! | jubilant (attackers) shout over battle |
| `soldier/gate_broken_3.wav` | Into the town! | jubilant (attackers) shout over battle |

### Soldier: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `soldier/enemy_sighted_1.wav` | Enemy ahead! | sharp warning call, carrying |
| `soldier/enemy_sighted_2.wav` | There they are. | sharp warning call, carrying |
| `soldier/enemy_sighted_3.wav` | Here they come! | sharp warning call, carrying |

### Soldier: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `soldier/victory_1.wav` | Victory! | exhausted cheer, then pride; loud |
| `soldier/victory_2.wav` | The field is ours! | exhausted cheer, then pride; loud |
| `soldier/victory_3.wav` | We did it, lads! | exhausted cheer, then pride; loud |

### Soldier: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `soldier/defeat_1.wav` | It's over... | broken, quiet despair |
| `soldier/defeat_2.wav` | We're beaten. | broken, quiet despair |
| `soldier/defeat_3.wav` | Fall back... all is lost. | broken, quiet despair |

### Soldier: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `soldier/idle_1.wav` | My feet ache. | relaxed, bored aside; quiet |
| `soldier/idle_2.wav` | Quiet. Too quiet. | relaxed, bored aside; quiet |
| `soldier/idle_3.wav` | Wish I was home. | relaxed, bored aside; quiet |
| `soldier/idle_4.wav` | Anyone got bread? | relaxed, bored aside; quiet |

### Soldier: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `soldier/pain_1.wav` | Agh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `soldier/pain_2.wav` | Ungh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `soldier/pain_3.wav` | Argh, damn it! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `soldier/pain_4.wav` | Hnngh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Soldier: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `soldier/death_1.wav` | Aaargh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `soldier/death_2.wav` | Nooo... (falling) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `soldier/death_3.wav` | Aaagh... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `soldier/death_4.wav` | Ugh... (last breath) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Soldier: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `soldier/death_moan_1.wav` | Hukk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `soldier/death_moan_2.wav` | Uhhh... (a gurgling moan) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `soldier/death_moan_3.wav` | Ghh... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Archer (`archer`): 92 lines

*Used by:* Archers.  
*Casting:* A younger bowman: sharp-eyed, a little cocky, quick-tongued. Lighter and higher than the Soldier.

### Archer: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `archer/selected_1.wav` | Bow ready. | alert, ready; at speaking volume, as if the captain just turned to him |
| `archer/selected_2.wav` | Yes? | alert, ready; at speaking volume, as if the captain just turned to him |
| `archer/selected_3.wav` | Nocked. | alert, ready; at speaking volume, as if the captain just turned to him |
| `archer/selected_4.wav` | Where to? | alert, ready; at speaking volume, as if the captain just turned to him |
| `archer/selected_5.wav` | Archers here. | alert, ready; at speaking volume, as if the captain just turned to him |

### Archer: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `archer/move_1.wav` | Moving. | brisk acknowledgement, already moving; clipped |
| `archer/move_2.wav` | Repositioning. | brisk acknowledgement, already moving; clipped |
| `archer/move_3.wav` | Right away. | brisk acknowledgement, already moving; clipped |
| `archer/move_4.wav` | Going. | brisk acknowledgement, already moving; clipped |
| `archer/move_5.wav` | Got it. | brisk acknowledgement, already moving; clipped |

### Archer: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `archer/attack_1.wav` | Loose! | eager, aggressive; raised voice |
| `archer/attack_2.wav` | Draw... loose! | eager, aggressive; raised voice |
| `archer/attack_3.wav` | Mark your target! | eager, aggressive; raised voice |
| `archer/attack_4.wav` | Feather them! | eager, aggressive; raised voice |
| `archer/attack_5.wav` | In range! | eager, aggressive; raised voice |

### Archer: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `archer/charge_1.wav` | Charge! | shouted over battle, full chest, rising |
| `archer/charge_2.wav` | Knives out! | shouted over battle, full chest, rising |
| `archer/charge_3.wav` | Go! | shouted over battle, full chest, rising |

### Archer: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `archer/charge_seen_1.wav` | Horse coming! | urgent warning shout, alarmed |
| `archer/charge_seen_2.wav` | Riders! Pull back! | urgent warning shout, alarmed |
| `archer/charge_seen_3.wav` | Cavalry on us! | urgent warning shout, alarmed |

### Archer: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `archer/hold_line_1.wav` | Hold! Keep shooting! | commanding bellow over battle, steady and fierce |
| `archer/hold_line_2.wav` | Steady your aim! | commanding bellow over battle, steady and fierce |
| `archer/hold_line_3.wav` | Don't run, shoot! | commanding bellow over battle, steady and fierce |

### Archer: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `archer/rallied_1.wav` | Back in line. | catching breath, regaining nerve; gruff, half ashamed |
| `archer/rallied_2.wav` | All right, all right. | catching breath, regaining nerve; gruff, half ashamed |
| `archer/rallied_3.wav` | Nock again. | catching breath, regaining nerve; gruff, half ashamed |

### Archer: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `archer/wavering_1.wav` | Too close... too close... | breathless, afraid; under the breath |
| `archer/wavering_2.wav` | They're getting through. | breathless, afraid; under the breath |
| `archer/wavering_3.wav` | I'm out of time... | breathless, afraid; under the breath |

### Archer: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `archer/breaking_1.wav` | Run for it! | panicked scream, running, voice cracking |
| `archer/breaking_2.wav` | Drop the bows, run! | panicked scream, running, voice cracking |
| `archer/breaking_3.wav` | Get away! | panicked scream, running, voice cracking |

### Archer: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `archer/hero_fell_1.wav` | The lord! No! | stricken, disbelieving shout |
| `archer/hero_fell_2.wav` | He's down! | stricken, disbelieving shout |
| `archer/hero_fell_3.wav` | Our lord is slain! | stricken, disbelieving shout |

### Archer: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `archer/enemy_hero_fell_1.wav` | Got their leader! | triumphant shout over battle |
| `archer/enemy_hero_fell_2.wav` | Their hero's down! | triumphant shout over battle |
| `archer/enemy_hero_fell_3.wav` | Who shot him? Good shot! | triumphant shout over battle |

### Archer: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `archer/friend_killed_1.wav` | Will! | shocked, grieving cry; short |
| `archer/friend_killed_2.wav` | They killed him! | shocked, grieving cry; short |
| `archer/friend_killed_3.wav` | No! | shocked, grieving cry; short |

### Archer: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `archer/kill_1.wav` | Hit! | grim, surprised satisfaction; mid-effort |
| `archer/kill_2.wav` | Got him! | grim, surprised satisfaction; mid-effort |
| `archer/kill_3.wav` | Clean shot. | grim, surprised satisfaction; mid-effort |
| `archer/kill_4.wav` | One down! | grim, surprised satisfaction; mid-effort |

### Archer: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `archer/vet_kill_1.wav` | Right in the eye. | cold taunt, contemptuous; spoken to the fallen |
| `archer/vet_kill_2.wav` | Didn't even see it. | cold taunt, contemptuous; spoken to the fallen |
| `archer/vet_kill_3.wav` | Another feather. | cold taunt, contemptuous; spoken to the fallen |
| `archer/vet_kill_4.wav` | Easy. | cold taunt, contemptuous; spoken to the fallen |

### Archer: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `archer/promoted_1.wav` | I never miss now. | proud, grim pleasure; low |
| `archer/promoted_2.wav` | Bullseye, every time. | proud, grim pleasure; low |
| `archer/promoted_3.wav` | Getting good at this. | proud, grim pleasure; low |

### Archer: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `archer/wounded_1.wav` | I'm hit! | pained grunt with words; through gritted teeth |
| `archer/wounded_2.wav` | My arm! | pained grunt with words; through gritted teeth |
| `archer/wounded_3.wav` | Can't draw... | pained grunt with words; through gritted teeth |

### Archer: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `archer/arrow_fire_1.wav` | Their archers! | urgent warning, ducking; shouted |
| `archer/arrow_fire_2.wav` | Take cover! | urgent warning, ducking; shouted |
| `archer/arrow_fire_3.wav` | Arrows! Down! | urgent warning, ducking; shouted |

### Archer: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `archer/flanked_1.wav` | They're behind us! | alarmed shout, turning round |
| `archer/flanked_2.wav` | Flank! Flank! | alarmed shout, turning round |
| `archer/flanked_3.wav` | Behind! | alarmed shout, turning round |

### Archer: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `archer/friendly_fire_1.wav` | Watch your aim! | angry shout back over the shoulder |
| `archer/friendly_fire_2.wav` | Who shot that? | angry shout back over the shoulder |
| `archer/friendly_fire_3.wav` | Wrong side, idiot! | angry shout back over the shoulder |

### Archer: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `archer/gate_broken_1.wav` | Gate's open! | jubilant (attackers) shout over battle |
| `archer/gate_broken_2.wav` | It's broken! | jubilant (attackers) shout over battle |
| `archer/gate_broken_3.wav` | We're in! | jubilant (attackers) shout over battle |

### Archer: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `archer/enemy_sighted_1.wav` | Movement! | sharp warning call, carrying |
| `archer/enemy_sighted_2.wav` | I see them! | sharp warning call, carrying |
| `archer/enemy_sighted_3.wav` | Enemy in sight! | sharp warning call, carrying |

### Archer: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `archer/victory_1.wav` | Victory! | exhausted cheer, then pride; loud |
| `archer/victory_2.wav` | Not a bad day. | exhausted cheer, then pride; loud |
| `archer/victory_3.wav` | We won! | exhausted cheer, then pride; loud |

### Archer: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `archer/defeat_1.wav` | We lost... | broken, quiet despair |
| `archer/defeat_2.wav` | No arrows left. | broken, quiet despair |
| `archer/defeat_3.wav` | It's done... | broken, quiet despair |

### Archer: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `archer/idle_1.wav` | Need more arrows. | relaxed, bored aside; quiet |
| `archer/idle_2.wav` | My fingers hurt. | relaxed, bored aside; quiet |
| `archer/idle_3.wav` | Wind's shifting. | relaxed, bored aside; quiet |
| `archer/idle_4.wav` | Bet I hit that crow. | relaxed, bored aside; quiet |

### Archer: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `archer/pain_1.wav` | Ah! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `archer/pain_2.wav` | Ow, curse it! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `archer/pain_3.wav` | Hnn! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `archer/pain_4.wav` | Agh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Archer: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `archer/death_1.wav` | Aaah! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `archer/death_2.wav` | Ah... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `archer/death_3.wav` | Nnno... | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `archer/death_4.wav` | Aaagh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Archer: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `archer/death_moan_1.wav` | Hhk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `archer/death_moan_2.wav` | Ohh... (a short moan) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `archer/death_moan_3.wav` | Ghh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Dwarf (`dwarf`): 100 lines

*Used by:* Dwarves, Dwarf Grenadiers.  
*Casting:* A gruff old dwarf: deep, gravelly, stubborn, dry humour. Myth's dwarves: a Scots or northern burr suits.

### Dwarf: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `dwarf/selected_1.wav` | Aye? | alert, ready; at speaking volume, as if the captain just turned to him |
| `dwarf/selected_2.wav` | What now? | alert, ready; at speaking volume, as if the captain just turned to him |
| `dwarf/selected_3.wav` | Speak up. | alert, ready; at speaking volume, as if the captain just turned to him |
| `dwarf/selected_4.wav` | Dwarf's ready. | alert, ready; at speaking volume, as if the captain just turned to him |
| `dwarf/selected_5.wav` | Hmph. Yes? | alert, ready; at speaking volume, as if the captain just turned to him |

### Dwarf: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `dwarf/move_1.wav` | Aye, aye. | brisk acknowledgement, already moving; clipped |
| `dwarf/move_2.wav` | Short legs, long march. | brisk acknowledgement, already moving; clipped |
| `dwarf/move_3.wav` | Going. | brisk acknowledgement, already moving; clipped |
| `dwarf/move_4.wav` | If we must. | brisk acknowledgement, already moving; clipped |
| `dwarf/move_5.wav` | On our way. | brisk acknowledgement, already moving; clipped |

### Dwarf: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `dwarf/attack_1.wav` | Axes high! | eager, aggressive; raised voice |
| `dwarf/attack_2.wav` | At 'em! | eager, aggressive; raised voice |
| `dwarf/attack_3.wav` | Crack their skulls! | eager, aggressive; raised voice |
| `dwarf/attack_4.wav` | For the deep halls! | eager, aggressive; raised voice |
| `dwarf/attack_5.wav` | Let's have them! | eager, aggressive; raised voice |

### Dwarf: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `dwarf/charge_1.wav` | Charge! Haha! | shouted over battle, full chest, rising |
| `dwarf/charge_2.wav` | Into 'em! | shouted over battle, full chest, rising |
| `dwarf/charge_3.wav` | Down with them! | shouted over battle, full chest, rising |

### Dwarf: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `dwarf/charge_seen_1.wav` | Horse! Set yourselves! | urgent warning shout, alarmed |
| `dwarf/charge_seen_2.wav` | Hah! Let 'em come! | urgent warning shout, alarmed |
| `dwarf/charge_seen_3.wav` | Riders! | urgent warning shout, alarmed |

### Dwarf: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `dwarf/hold_line_1.wav` | Hold, ye beardless lot! | commanding bellow over battle, steady and fierce |
| `dwarf/hold_line_2.wav` | Stand like stone! | commanding bellow over battle, steady and fierce |
| `dwarf/hold_line_3.wav` | Not one step back! | commanding bellow over battle, steady and fierce |
| `dwarf/hold_line_4.wav` | Hold the line! | commanding bellow over battle, steady and fierce |

### Dwarf: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `dwarf/rallied_1.wav` | Bah. Shame on me. | catching breath, regaining nerve; gruff, half ashamed |
| `dwarf/rallied_2.wav` | Back again. | catching breath, regaining nerve; gruff, half ashamed |
| `dwarf/rallied_3.wav` | Never tell my clan. | catching breath, regaining nerve; gruff, half ashamed |

### Dwarf: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `dwarf/wavering_1.wav` | This is bad... | breathless, afraid; under the breath |
| `dwarf/wavering_2.wav` | Stone and iron, keep me... | breathless, afraid; under the breath |
| `dwarf/wavering_3.wav` | Too many of 'em. | breathless, afraid; under the breath |

### Dwarf: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `dwarf/breaking_1.wav` | Run! Run! | panicked scream, running, voice cracking |
| `dwarf/breaking_2.wav` | Back to the hills! | panicked scream, running, voice cracking |
| `dwarf/breaking_3.wav` | Every dwarf for himself! | panicked scream, running, voice cracking |

### Dwarf: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `dwarf/hero_fell_1.wav` | The lord! They've slain him! | stricken, disbelieving shout |
| `dwarf/hero_fell_2.wav` | No! Not him! | stricken, disbelieving shout |
| `dwarf/hero_fell_3.wav` | Our lord is down! | stricken, disbelieving shout |

### Dwarf: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `dwarf/enemy_hero_fell_1.wav` | Their chief's dead! | triumphant shout over battle |
| `dwarf/enemy_hero_fell_2.wav` | Ha! Down he goes! | triumphant shout over battle |
| `dwarf/enemy_hero_fell_3.wav` | That'll teach him! | triumphant shout over battle |

### Dwarf: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `dwarf/friend_killed_1.wav` | Brother! | shocked, grieving cry; short |
| `dwarf/friend_killed_2.wav` | They'll pay for that! | shocked, grieving cry; short |
| `dwarf/friend_killed_3.wav` | By my beard, no! | shocked, grieving cry; short |

### Dwarf: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `dwarf/kill_1.wav` | One for the axe! | grim, surprised satisfaction; mid-effort |
| `dwarf/kill_2.wav` | Hah! Got one! | grim, surprised satisfaction; mid-effort |
| `dwarf/kill_3.wav` | That's one. | grim, surprised satisfaction; mid-effort |
| `dwarf/kill_4.wav` | Down ye go! | grim, surprised satisfaction; mid-effort |

### Dwarf: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `dwarf/vet_kill_1.wav` | Another for the tally. | cold taunt, contemptuous; spoken to the fallen |
| `dwarf/vet_kill_2.wav` | Too tall, too slow. | cold taunt, contemptuous; spoken to the fallen |
| `dwarf/vet_kill_3.wav` | Next! | cold taunt, contemptuous; spoken to the fallen |
| `dwarf/vet_kill_4.wav` | Pathetic. | cold taunt, contemptuous; spoken to the fallen |

### Dwarf: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `dwarf/promoted_1.wav` | That's worth a song. | proud, grim pleasure; low |
| `dwarf/promoted_2.wav` | Older and harder. | proud, grim pleasure; low |
| `dwarf/promoted_3.wav` | Another braid, I think. | proud, grim pleasure; low |

### Dwarf: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `dwarf/wounded_1.wav` | Just a scratch... | pained grunt with words; through gritted teeth |
| `dwarf/wounded_2.wav` | I've had worse! | pained grunt with words; through gritted teeth |
| `dwarf/wounded_3.wav` | Ach! Me leg! | pained grunt with words; through gritted teeth |

### Dwarf: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `dwarf/arrow_fire_1.wav` | Arrows! | urgent warning, ducking; shouted |
| `dwarf/arrow_fire_2.wav` | Helmets on! | urgent warning, ducking; shouted |
| `dwarf/arrow_fire_3.wav` | Hide behind me! | urgent warning, ducking; shouted |

### Dwarf: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `dwarf/flanked_1.wav` | Behind us! | alarmed shout, turning round |
| `dwarf/flanked_2.wav` | Turn about! | alarmed shout, turning round |
| `dwarf/flanked_3.wav` | Sneaky devils! | alarmed shout, turning round |

### Dwarf: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `dwarf/friendly_fire_1.wav` | Watch it! | angry shout back over the shoulder |
| `dwarf/friendly_fire_2.wav` | Oi! I'm a dwarf, not a target! | angry shout back over the shoulder |
| `dwarf/friendly_fire_3.wav` | Aim, ye fool! | angry shout back over the shoulder |

### Dwarf: Fire in the hole (`fuse`)

*When:* A grenadier lights and throws a satchel.  
*Delivery:* shouted warning to friends, throwing

| File | Line | Delivery |
|---|---|---|
| `dwarf/fuse_1.wav` | Fire in the hole! | shouted warning to friends, throwing |
| `dwarf/fuse_2.wav` | Stand clear! | shouted warning to friends, throwing |
| `dwarf/fuse_3.wav` | Here's a present! | shouted warning to friends, throwing |

### Dwarf: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `dwarf/gate_broken_1.wav` | Door's down! | jubilant (attackers) shout over battle |
| `dwarf/gate_broken_2.wav` | Ha! Kindling! | jubilant (attackers) shout over battle |
| `dwarf/gate_broken_3.wav` | In we go! | jubilant (attackers) shout over battle |

### Dwarf: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `dwarf/enemy_sighted_1.wav` | Enemy ahead! | sharp warning call, carrying |
| `dwarf/enemy_sighted_2.wav` | There they are. | sharp warning call, carrying |
| `dwarf/enemy_sighted_3.wav` | Smell that? Enemies. | sharp warning call, carrying |

### Dwarf: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `dwarf/victory_1.wav` | Victory! Ale for all! | exhausted cheer, then pride; loud |
| `dwarf/victory_2.wav` | Ha! They ran! | exhausted cheer, then pride; loud |
| `dwarf/victory_3.wav` | A fine day's work. | exhausted cheer, then pride; loud |

### Dwarf: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `dwarf/defeat_1.wav` | Back to the hills... | broken, quiet despair |
| `dwarf/defeat_2.wav` | Shame on us. | broken, quiet despair |
| `dwarf/defeat_3.wav` | It's lost. | broken, quiet despair |

### Dwarf: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `dwarf/idle_1.wav` | I need a drink. | relaxed, bored aside; quiet |
| `dwarf/idle_2.wav` | My axe is thirsty. | relaxed, bored aside; quiet |
| `dwarf/idle_3.wav` | Too much sky up here. | relaxed, bored aside; quiet |
| `dwarf/idle_4.wav` | Rain again... | relaxed, bored aside; quiet |

### Dwarf: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `dwarf/pain_1.wav` | Grah! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `dwarf/pain_2.wav` | Oof! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `dwarf/pain_3.wav` | Argh, me arm! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `dwarf/pain_4.wav` | Hrrgh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Dwarf: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `dwarf/death_1.wav` | Aaargh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `dwarf/death_2.wav` | Graah... (falling) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `dwarf/death_3.wav` | Me beard... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `dwarf/death_4.wav` | Hrrk! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Dwarf: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `dwarf/death_moan_1.wav` | Hrrk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `dwarf/death_moan_2.wav` | Uhhh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `dwarf/death_moan_3.wav` | Ghh... (gurgling) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

### Dwarf: Grenadier's glee (`cackle`)

*When:* A grenadier whose satchel has just killed: a manic cackle.  
*Delivery:* manic, breathless glee; 1-1.5 s

| File | Line | Delivery |
|---|---|---|
| `dwarf/cackle_1.wav` | Heh heh heh HAH! | manic, breathless glee; 1-1.5 s |
| `dwarf/cackle_2.wav` | Hahaha! Boom! | manic, breathless glee; 1-1.5 s |
| `dwarf/cackle_3.wav` | Hee hee! Look at 'em fly! | manic, breathless glee; 1-1.5 s |
| `dwarf/cackle_4.wav` | Ha! Another! | manic, breathless glee; 1-1.5 s |

## Rider (`rider`): 92 lines

*Used by:* Light Cavalry, Heavy Cavalry, Wolf Riders.  
*Casting:* A horseman: proud, dashing, loud from the saddle. Breathier on the move lines, as if riding.

### Rider: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `rider/selected_1.wav` | Mounted. | alert, ready; at speaking volume, as if the captain just turned to him |
| `rider/selected_2.wav` | Sir! | alert, ready; at speaking volume, as if the captain just turned to him |
| `rider/selected_3.wav` | Ready to ride. | alert, ready; at speaking volume, as if the captain just turned to him |
| `rider/selected_4.wav` | Horse is fresh. | alert, ready; at speaking volume, as if the captain just turned to him |
| `rider/selected_5.wav` | Yes, my lord? | alert, ready; at speaking volume, as if the captain just turned to him |

### Rider: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `rider/move_1.wav` | Riding. | brisk acknowledgement, already moving; clipped |
| `rider/move_2.wav` | We ride! | brisk acknowledgement, already moving; clipped |
| `rider/move_3.wav` | On the way. | brisk acknowledgement, already moving; clipped |
| `rider/move_4.wav` | Spurs! | brisk acknowledgement, already moving; clipped |
| `rider/move_5.wav` | Yah! | brisk acknowledgement, already moving; clipped |

### Rider: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `rider/attack_1.wav` | Ride them down! | eager, aggressive; raised voice |
| `rider/attack_2.wav` | Lances! | eager, aggressive; raised voice |
| `rider/attack_3.wav` | Run them through! | eager, aggressive; raised voice |
| `rider/attack_4.wav` | At the gallop! | eager, aggressive; raised voice |
| `rider/attack_5.wav` | Trample them! | eager, aggressive; raised voice |

### Rider: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `rider/charge_1.wav` | Charge! | shouted over battle, full chest, rising |
| `rider/charge_2.wav` | For glory! | shouted over battle, full chest, rising |
| `rider/charge_3.wav` | Ride! Ride! | shouted over battle, full chest, rising |

### Rider: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `rider/charge_seen_1.wav` | Their horse! Meet them! | urgent warning shout, alarmed |
| `rider/charge_seen_2.wav` | Riders coming! | urgent warning shout, alarmed |
| `rider/charge_seen_3.wav` | Counter-charge! | urgent warning shout, alarmed |

### Rider: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `rider/hold_line_1.wav` | Hold, rein in! | commanding bellow over battle, steady and fierce |
| `rider/hold_line_2.wav` | Stay together! | commanding bellow over battle, steady and fierce |
| `rider/hold_line_3.wav` | Hold, by the saddle! | commanding bellow over battle, steady and fierce |

### Rider: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `rider/rallied_1.wav` | Turn about! Again! | catching breath, regaining nerve; gruff, half ashamed |
| `rider/rallied_2.wav` | Easy... easy, girl. | catching breath, regaining nerve; gruff, half ashamed |
| `rider/rallied_3.wav` | Back to the fight. | catching breath, regaining nerve; gruff, half ashamed |

### Rider: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `rider/wavering_1.wav` | Easy... easy... | breathless, afraid; under the breath |
| `rider/wavering_2.wav` | The horse won't stand. | breathless, afraid; under the breath |
| `rider/wavering_3.wav` | Too many pikes... | breathless, afraid; under the breath |

### Rider: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `rider/breaking_1.wav` | Ride for your lives! | panicked scream, running, voice cracking |
| `rider/breaking_2.wav` | Away! Away! | panicked scream, running, voice cracking |
| `rider/breaking_3.wav` | Turn and run! | panicked scream, running, voice cracking |

### Rider: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `rider/hero_fell_1.wav` | The lord is down! | stricken, disbelieving shout |
| `rider/hero_fell_2.wav` | No! He's fallen! | stricken, disbelieving shout |
| `rider/hero_fell_3.wav` | Our lord! | stricken, disbelieving shout |

### Rider: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `rider/enemy_hero_fell_1.wav` | Their lord is down! | triumphant shout over battle |
| `rider/enemy_hero_fell_2.wav` | We got him! | triumphant shout over battle |
| `rider/enemy_hero_fell_3.wav` | Ride them, they're leaderless! | triumphant shout over battle |

### Rider: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `rider/friend_killed_1.wav` | Man down! | shocked, grieving cry; short |
| `rider/friend_killed_2.wav` | No! | shocked, grieving cry; short |
| `rider/friend_killed_3.wav` | They've got him! | shocked, grieving cry; short |

### Rider: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `rider/kill_1.wav` | Ridden down! | grim, surprised satisfaction; mid-effort |
| `rider/kill_2.wav` | One! | grim, surprised satisfaction; mid-effort |
| `rider/kill_3.wav` | Got him! | grim, surprised satisfaction; mid-effort |
| `rider/kill_4.wav` | Trampled! | grim, surprised satisfaction; mid-effort |

### Rider: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `rider/vet_kill_1.wav` | Under the hooves. | cold taunt, contemptuous; spoken to the fallen |
| `rider/vet_kill_2.wav` | Never saw me coming. | cold taunt, contemptuous; spoken to the fallen |
| `rider/vet_kill_3.wav` | Next! | cold taunt, contemptuous; spoken to the fallen |
| `rider/vet_kill_4.wav` | Too slow, foot-man. | cold taunt, contemptuous; spoken to the fallen |

### Rider: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `rider/promoted_1.wav` | Born in the saddle. | proud, grim pleasure; low |
| `rider/promoted_2.wav` | Ride with me now. | proud, grim pleasure; low |
| `rider/promoted_3.wav` | Best rider in the troop. | proud, grim pleasure; low |

### Rider: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `rider/wounded_1.wav` | I'm hit! | pained grunt with words; through gritted teeth |
| `rider/wounded_2.wav` | Still in the saddle... | pained grunt with words; through gritted teeth |
| `rider/wounded_3.wav` | Ugh... | pained grunt with words; through gritted teeth |

### Rider: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `rider/arrow_fire_1.wav` | Arrows! Ride through! | urgent warning, ducking; shouted |
| `rider/arrow_fire_2.wav` | Bows! | urgent warning, ducking; shouted |
| `rider/arrow_fire_3.wav` | Keep moving! | urgent warning, ducking; shouted |

### Rider: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `rider/flanked_1.wav` | Behind us! | alarmed shout, turning round |
| `rider/flanked_2.wav` | Flank! Wheel about! | alarmed shout, turning round |
| `rider/flanked_3.wav` | From the side! | alarmed shout, turning round |

### Rider: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `rider/friendly_fire_1.wav` | Watch your aim! | angry shout back over the shoulder |
| `rider/friendly_fire_2.wav` | That's our horse! | angry shout back over the shoulder |
| `rider/friendly_fire_3.wav` | Not us! | angry shout back over the shoulder |

### Rider: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `rider/gate_broken_1.wav` | Gate's down! Ride! | jubilant (attackers) shout over battle |
| `rider/gate_broken_2.wav` | Through the gate! | jubilant (attackers) shout over battle |
| `rider/gate_broken_3.wav` | We're in! | jubilant (attackers) shout over battle |

### Rider: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `rider/enemy_sighted_1.wav` | Enemy sighted! | sharp warning call, carrying |
| `rider/enemy_sighted_2.wav` | Riders, look sharp! | sharp warning call, carrying |
| `rider/enemy_sighted_3.wav` | There! | sharp warning call, carrying |

### Rider: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `rider/victory_1.wav` | Victory! | exhausted cheer, then pride; loud |
| `rider/victory_2.wav` | The field is ours! | exhausted cheer, then pride; loud |
| `rider/victory_3.wav` | Hurrah! | exhausted cheer, then pride; loud |

### Rider: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `rider/defeat_1.wav` | Ride home... | broken, quiet despair |
| `rider/defeat_2.wav` | Sound the retreat... | broken, quiet despair |
| `rider/defeat_3.wav` | Lost... | broken, quiet despair |

### Rider: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `rider/idle_1.wav` | Horse needs water. | relaxed, bored aside; quiet |
| `rider/idle_2.wav` | Saddle sore. | relaxed, bored aside; quiet |
| `rider/idle_3.wav` | Easy, girl. | relaxed, bored aside; quiet |
| `rider/idle_4.wav` | Nothing to chase. | relaxed, bored aside; quiet |

### Rider: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `rider/pain_1.wav` | Agh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `rider/pain_2.wav` | Hah... hit! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `rider/pain_3.wav` | Ungh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `rider/pain_4.wav` | Argh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Rider: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `rider/death_1.wav` | Aaaagh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `rider/death_2.wav` | Uhh... (falling from the saddle) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `rider/death_3.wav` | Nooo! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `rider/death_4.wav` | Agh... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Rider: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `rider/death_moan_1.wav` | Hukk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `rider/death_moan_2.wav` | Ohh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `rider/death_moan_3.wav` | Ghh... (gurgling) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Hero (`hero`): 83 lines

*Used by:* Heroes.  
*Casting:* The lord or lady at the army's head: noble, commanding, warm with his own men and iron with the foe. One actor; the enemy's heroes play the same lines pitched darker.

### Hero: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `hero/selected_1.wav` | I am here. | alert, ready; at speaking volume, as if the captain just turned to him |
| `hero/selected_2.wav` | Speak. | alert, ready; at speaking volume, as if the captain just turned to him |
| `hero/selected_3.wav` | My blade is ready. | alert, ready; at speaking volume, as if the captain just turned to him |
| `hero/selected_4.wav` | Command me. | alert, ready; at speaking volume, as if the captain just turned to him |
| `hero/selected_5.wav` | What would you have? | alert, ready; at speaking volume, as if the captain just turned to him |

### Hero: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `hero/move_1.wav` | I go. | brisk acknowledgement, already moving; clipped |
| `hero/move_2.wav` | Follow me. | brisk acknowledgement, already moving; clipped |
| `hero/move_3.wav` | As you will. | brisk acknowledgement, already moving; clipped |
| `hero/move_4.wav` | On, together. | brisk acknowledgement, already moving; clipped |
| `hero/move_5.wav` | Onward. | brisk acknowledgement, already moving; clipped |

### Hero: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `hero/attack_1.wav` | Face me! | eager, aggressive; raised voice |
| `hero/attack_2.wav` | For Illyria! | eager, aggressive; raised voice |
| `hero/attack_3.wav` | With me! | eager, aggressive; raised voice |
| `hero/attack_4.wav` | Strike them down! | eager, aggressive; raised voice |
| `hero/attack_5.wav` | Have at you! | eager, aggressive; raised voice |

### Hero: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `hero/charge_1.wav` | Charge! With me! | shouted over battle, full chest, rising |
| `hero/charge_2.wav` | For honour! | shouted over battle, full chest, rising |
| `hero/charge_3.wav` | Into them, all of you! | shouted over battle, full chest, rising |

### Hero: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `hero/charge_seen_1.wav` | Horse! Brace, behind me! | urgent warning shout, alarmed |
| `hero/charge_seen_2.wav` | Let them come! | urgent warning shout, alarmed |
| `hero/charge_seen_3.wav` | Stand with me! | urgent warning shout, alarmed |

### Hero: Hero rallies (`hero_rally`)

*When:* A hero's rally call to wavering or broken troops.  
*Delivery:* heroic roar over battle, inspiring

| File | Line | Delivery |
|---|---|---|
| `hero/hero_rally_1.wav` | Rally to me! | heroic roar over battle, inspiring |
| `hero/hero_rally_2.wav` | Stand with me! We are not beaten! | heroic roar over battle, inspiring |
| `hero/hero_rally_3.wav` | Back to the line! I am with you! | heroic roar over battle, inspiring |
| `hero/hero_rally_4.wav` | Look at me! Not at them! | heroic roar over battle, inspiring |

### Hero: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `hero/hero_fell_1.wav` | No! My brother in arms! | stricken, disbelieving shout |
| `hero/hero_fell_2.wav` | A lord has fallen! | stricken, disbelieving shout |
| `hero/hero_fell_3.wav` | He is down! Avenge him! | stricken, disbelieving shout |

### Hero: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `hero/enemy_hero_fell_1.wav` | Their champion falls! | triumphant shout over battle |
| `hero/enemy_hero_fell_2.wav` | So ends their lord! | triumphant shout over battle |
| `hero/enemy_hero_fell_3.wav` | Their leader is dead! Press on! | triumphant shout over battle |

### Hero: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `hero/friend_killed_1.wav` | You'll be avenged! | shocked, grieving cry; short |
| `hero/friend_killed_2.wav` | Another good man lost. | shocked, grieving cry; short |
| `hero/friend_killed_3.wav` | Curse them! | shocked, grieving cry; short |

### Hero: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `hero/kill_1.wav` | Yield to none! | grim, surprised satisfaction; mid-effort |
| `hero/kill_2.wav` | Fall! | grim, surprised satisfaction; mid-effort |
| `hero/kill_3.wav` | One less. | grim, surprised satisfaction; mid-effort |
| `hero/kill_4.wav` | Begone! | grim, surprised satisfaction; mid-effort |

### Hero: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `hero/vet_kill_1.wav` | Who's next? | cold taunt, contemptuous; spoken to the fallen |
| `hero/vet_kill_2.wav` | You were warned. | cold taunt, contemptuous; spoken to the fallen |
| `hero/vet_kill_3.wav` | Unworthy. | cold taunt, contemptuous; spoken to the fallen |
| `hero/vet_kill_4.wav` | Kneel. | cold taunt, contemptuous; spoken to the fallen |

### Hero: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `hero/promoted_1.wav` | I grow stronger. | proud, grim pleasure; low |
| `hero/promoted_2.wav` | My blade remembers. | proud, grim pleasure; low |
| `hero/promoted_3.wav` | None can stand before me now. | proud, grim pleasure; low |

### Hero: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `hero/wounded_1.wav` | It is nothing! | pained grunt with words; through gritted teeth |
| `hero/wounded_2.wav` | I still stand! | pained grunt with words; through gritted teeth |
| `hero/wounded_3.wav` | Ugh... not yet! | pained grunt with words; through gritted teeth |

### Hero: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `hero/arrow_fire_1.wav` | Arrows! Shields! | urgent warning, ducking; shouted |
| `hero/arrow_fire_2.wav` | Cover! | urgent warning, ducking; shouted |
| `hero/arrow_fire_3.wav` | Their bows! | urgent warning, ducking; shouted |

### Hero: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `hero/flanked_1.wav` | Behind me! | alarmed shout, turning round |
| `hero/flanked_2.wav` | Turn! They flank us! | alarmed shout, turning round |
| `hero/flanked_3.wav` | Cowards, from behind! | alarmed shout, turning round |

### Hero: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `hero/friendly_fire_1.wav` | Hold your fire! | angry shout back over the shoulder |
| `hero/friendly_fire_2.wav` | Watch your aim! | angry shout back over the shoulder |
| `hero/friendly_fire_3.wav` | Not at me, fools! | angry shout back over the shoulder |

### Hero: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `hero/gate_broken_1.wav` | The gate is broken! Take the town! | jubilant (attackers) shout over battle |
| `hero/gate_broken_2.wav` | Through, all of you! | jubilant (attackers) shout over battle |
| `hero/gate_broken_3.wav` | The way is open! | jubilant (attackers) shout over battle |

### Hero: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `hero/enemy_sighted_1.wav` | There. The enemy. | sharp warning call, carrying |
| `hero/enemy_sighted_2.wav` | They come. | sharp warning call, carrying |
| `hero/enemy_sighted_3.wav` | Make ready! | sharp warning call, carrying |

### Hero: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `hero/victory_1.wav` | Victory! The day is ours! | exhausted cheer, then pride; loud |
| `hero/victory_2.wav` | Well fought, all of you. | exhausted cheer, then pride; loud |
| `hero/victory_3.wav` | Illyria stands! | exhausted cheer, then pride; loud |

### Hero: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `hero/defeat_1.wav` | Retreat... save who you can. | broken, quiet despair |
| `hero/defeat_2.wav` | This is not the end. | broken, quiet despair |
| `hero/defeat_3.wav` | We are beaten... | broken, quiet despair |

### Hero: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `hero/idle_1.wav` | Patience. | relaxed, bored aside; quiet |
| `hero/idle_2.wav` | The waiting is the worst. | relaxed, bored aside; quiet |
| `hero/idle_3.wav` | I have seen too many wars. | relaxed, bored aside; quiet |
| `hero/idle_4.wav` | Stay sharp. | relaxed, bored aside; quiet |

### Hero: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `hero/pain_1.wav` | Hngh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `hero/pain_2.wav` | Argh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `hero/pain_3.wav` | Is that all? | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `hero/pain_4.wav` | Ugh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Hero: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `hero/death_1.wav` | Aaargh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `hero/death_2.wav` | Not... like this... | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `hero/death_3.wav` | Ugh... (falling) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Hero: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `hero/death_moan_1.wav` | Hukk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `hero/death_moan_2.wav` | Uhhh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `hero/death_moan_3.wav` | Ghh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Wizard (`wizard`): 96 lines

*Used by:* Wizards, Journeymen.  
*Casting:* An old scholar-mage and healer: measured, faintly eerie, a little proud. Speaks as if half his mind is elsewhere.

### Wizard: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `wizard/selected_1.wav` | Yes? | alert, ready; at speaking volume, as if the captain just turned to him |
| `wizard/selected_2.wav` | I am listening. | alert, ready; at speaking volume, as if the captain just turned to him |
| `wizard/selected_3.wav` | The stars are watching. | alert, ready; at speaking volume, as if the captain just turned to him |
| `wizard/selected_4.wav` | Speak, quickly. | alert, ready; at speaking volume, as if the captain just turned to him |
| `wizard/selected_5.wav` | Mm? | alert, ready; at speaking volume, as if the captain just turned to him |

### Wizard: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `wizard/move_1.wav` | If I must. | brisk acknowledgement, already moving; clipped |
| `wizard/move_2.wav` | I go. | brisk acknowledgement, already moving; clipped |
| `wizard/move_3.wav` | Walking. | brisk acknowledgement, already moving; clipped |
| `wizard/move_4.wav` | Very well. | brisk acknowledgement, already moving; clipped |
| `wizard/move_5.wav` | So be it. | brisk acknowledgement, already moving; clipped |

### Wizard: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `wizard/attack_1.wav` | Burn! | eager, aggressive; raised voice |
| `wizard/attack_2.wav` | Feel my art! | eager, aggressive; raised voice |
| `wizard/attack_3.wav` | By the old words! | eager, aggressive; raised voice |
| `wizard/attack_4.wav` | Ashes! | eager, aggressive; raised voice |
| `wizard/attack_5.wav` | Begone! | eager, aggressive; raised voice |

### Wizard: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `wizard/charge_1.wav` | Forward, then! | shouted over battle, full chest, rising |
| `wizard/charge_2.wav` | Into the fray! | shouted over battle, full chest, rising |
| `wizard/charge_3.wav` | Onward! | shouted over battle, full chest, rising |

### Wizard: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `wizard/charge_seen_1.wav` | Horsemen! Protect me! | urgent warning shout, alarmed |
| `wizard/charge_seen_2.wav` | Riders! | urgent warning shout, alarmed |
| `wizard/charge_seen_3.wav` | Guard the mages! | urgent warning shout, alarmed |

### Wizard: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `wizard/hold_line_1.wav` | Hold! Do not break! | commanding bellow over battle, steady and fierce |
| `wizard/hold_line_2.wav` | Courage! Stand! | commanding bellow over battle, steady and fierce |
| `wizard/hold_line_3.wav` | Hold, by the light! | commanding bellow over battle, steady and fierce |

### Wizard: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `wizard/rallied_1.wav` | Breathe... breathe. | catching breath, regaining nerve; gruff, half ashamed |
| `wizard/rallied_2.wav` | Where was I? | catching breath, regaining nerve; gruff, half ashamed |
| `wizard/rallied_3.wav` | Back to work. | catching breath, regaining nerve; gruff, half ashamed |

### Wizard: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `wizard/wavering_1.wav` | This was not foreseen... | breathless, afraid; under the breath |
| `wizard/wavering_2.wav` | Too close... | breathless, afraid; under the breath |
| `wizard/wavering_3.wav` | I need distance... | breathless, afraid; under the breath |

### Wizard: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `wizard/breaking_1.wav` | Flee! Flee! | panicked scream, running, voice cracking |
| `wizard/breaking_2.wav` | Not like this! | panicked scream, running, voice cracking |
| `wizard/breaking_3.wav` | Away, away! | panicked scream, running, voice cracking |

### Wizard: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `wizard/hero_fell_1.wav` | The lord is lost! | stricken, disbelieving shout |
| `wizard/hero_fell_2.wav` | Gods, no! | stricken, disbelieving shout |
| `wizard/hero_fell_3.wav` | He has fallen! | stricken, disbelieving shout |

### Wizard: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `wizard/enemy_hero_fell_1.wav` | Their champion is undone! | triumphant shout over battle |
| `wizard/enemy_hero_fell_2.wav` | As foretold! | triumphant shout over battle |
| `wizard/enemy_hero_fell_3.wav` | Their lord is dust! | triumphant shout over battle |

### Wizard: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `wizard/friend_killed_1.wav` | Too late for him. | shocked, grieving cry; short |
| `wizard/friend_killed_2.wav` | No! | shocked, grieving cry; short |
| `wizard/friend_killed_3.wav` | Death walks here. | shocked, grieving cry; short |

### Wizard: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `wizard/kill_1.wav` | Undone. | grim, surprised satisfaction; mid-effort |
| `wizard/kill_2.wav` | Dust. | grim, surprised satisfaction; mid-effort |
| `wizard/kill_3.wav` | So. | grim, surprised satisfaction; mid-effort |
| `wizard/kill_4.wav` | As it must be. | grim, surprised satisfaction; mid-effort |

### Wizard: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `wizard/vet_kill_1.wav` | Pitiful. | cold taunt, contemptuous; spoken to the fallen |
| `wizard/vet_kill_2.wav` | Burn. | cold taunt, contemptuous; spoken to the fallen |
| `wizard/vet_kill_3.wav` | Another fool. | cold taunt, contemptuous; spoken to the fallen |
| `wizard/vet_kill_4.wav` | Child's play. | cold taunt, contemptuous; spoken to the fallen |

### Wizard: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `wizard/promoted_1.wav` | My power grows. | proud, grim pleasure; low |
| `wizard/promoted_2.wav` | I see more clearly now. | proud, grim pleasure; low |
| `wizard/promoted_3.wav` | The art obeys me. | proud, grim pleasure; low |

### Wizard: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `wizard/wounded_1.wav` | I'm hurt... | pained grunt with words; through gritted teeth |
| `wizard/wounded_2.wav` | Ah! My robes! | pained grunt with words; through gritted teeth |
| `wizard/wounded_3.wav` | Need... a moment... | pained grunt with words; through gritted teeth |

### Wizard: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `wizard/arrow_fire_1.wav` | Arrows! | urgent warning, ducking; shouted |
| `wizard/arrow_fire_2.wav` | Shield me! | urgent warning, ducking; shouted |
| `wizard/arrow_fire_3.wav` | Down! | urgent warning, ducking; shouted |

### Wizard: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `wizard/flanked_1.wav` | Behind! | alarmed shout, turning round |
| `wizard/flanked_2.wav` | They're around us! | alarmed shout, turning round |
| `wizard/flanked_3.wav` | Guard my back! | alarmed shout, turning round |

### Wizard: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `wizard/friendly_fire_1.wav` | Watch where you aim! | angry shout back over the shoulder |
| `wizard/friendly_fire_2.wav` | I am on your side! | angry shout back over the shoulder |
| `wizard/friendly_fire_3.wav` | Careless! | angry shout back over the shoulder |

### Wizard: Healing (`healing`)

*When:* A healer mends a wounded friend.  
*Delivery:* calm, warm, murmured; close

| File | Line | Delivery |
|---|---|---|
| `wizard/healing_1.wav` | Be whole. | calm, warm, murmured; close |
| `wizard/healing_2.wav` | Rest easy. | calm, warm, murmured; close |
| `wizard/healing_3.wav` | There... there. | calm, warm, murmured; close |
| `wizard/healing_4.wav` | Mend. | calm, warm, murmured; close |

### Wizard: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `wizard/gate_broken_1.wav` | The gate is broken! | jubilant (attackers) shout over battle |
| `wizard/gate_broken_2.wav` | The way is open. | jubilant (attackers) shout over battle |
| `wizard/gate_broken_3.wav` | Through! | jubilant (attackers) shout over battle |

### Wizard: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `wizard/enemy_sighted_1.wav` | I sense them. | sharp warning call, carrying |
| `wizard/enemy_sighted_2.wav` | They are near. | sharp warning call, carrying |
| `wizard/enemy_sighted_3.wav` | Enemies, there. | sharp warning call, carrying |

### Wizard: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `wizard/victory_1.wav` | As foreseen. | exhausted cheer, then pride; loud |
| `wizard/victory_2.wav` | Victory. | exhausted cheer, then pride; loud |
| `wizard/victory_3.wav` | The light prevails. | exhausted cheer, then pride; loud |

### Wizard: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `wizard/defeat_1.wav` | All is lost. | broken, quiet despair |
| `wizard/defeat_2.wav` | The stars lied. | broken, quiet despair |
| `wizard/defeat_3.wav` | Fly, fly... | broken, quiet despair |

### Wizard: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `wizard/idle_1.wav` | Hmm... fascinating. | relaxed, bored aside; quiet |
| `wizard/idle_2.wav` | I should be reading. | relaxed, bored aside; quiet |
| `wizard/idle_3.wav` | Do you hear that? | relaxed, bored aside; quiet |
| `wizard/idle_4.wav` | The air tastes of rain. | relaxed, bored aside; quiet |

### Wizard: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `wizard/pain_1.wav` | Ah! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `wizard/pain_2.wav` | Oh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `wizard/pain_3.wav` | Ngh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `wizard/pain_4.wav` | Ow! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Wizard: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `wizard/death_1.wav` | Aaah! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `wizard/death_2.wav` | Ohh... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `wizard/death_3.wav` | Nnno... | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `wizard/death_4.wav` | Aaieee! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Wizard: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `wizard/death_moan_1.wav` | Hhk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `wizard/death_moan_2.wav` | Ohh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `wizard/death_moan_3.wav` | Ghh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Berserker (`berserker`): 91 lines

*Used by:* Berserkers.  
*Casting:* A wild northman, half-mad with battle joy: hoarse, laughing, shouting. Little patience for orders.

### Berserker: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `berserker/selected_1.wav` | WHAT? | alert, ready; at speaking volume, as if the captain just turned to him |
| `berserker/selected_2.wav` | Fight now? | alert, ready; at speaking volume, as if the captain just turned to him |
| `berserker/selected_3.wav` | Grr. | alert, ready; at speaking volume, as if the captain just turned to him |
| `berserker/selected_4.wav` | Who dies? | alert, ready; at speaking volume, as if the captain just turned to him |
| `berserker/selected_5.wav` | Speak! | alert, ready; at speaking volume, as if the captain just turned to him |

### Berserker: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `berserker/move_1.wav` | Hrr. Fine. | brisk acknowledgement, already moving; clipped |
| `berserker/move_2.wav` | Run! | brisk acknowledgement, already moving; clipped |
| `berserker/move_3.wav` | Going! | brisk acknowledgement, already moving; clipped |
| `berserker/move_4.wav` | Bah! | brisk acknowledgement, already moving; clipped |
| `berserker/move_5.wav` | Faster! | brisk acknowledgement, already moving; clipped |

### Berserker: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `berserker/attack_1.wav` | BLOOD! | eager, aggressive; raised voice |
| `berserker/attack_2.wav` | Kill! Kill! | eager, aggressive; raised voice |
| `berserker/attack_3.wav` | Haaaa! | eager, aggressive; raised voice |
| `berserker/attack_4.wav` | Rip them! | eager, aggressive; raised voice |
| `berserker/attack_5.wav` | Mine! | eager, aggressive; raised voice |

### Berserker: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `berserker/charge_1.wav` | RAAAAH! | shouted over battle, full chest, rising |
| `berserker/charge_2.wav` | Blood and fire! | shouted over battle, full chest, rising |
| `berserker/charge_3.wav` | KILL THEM ALL! | shouted over battle, full chest, rising |

### Berserker: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `berserker/charge_seen_1.wav` | Horses! Good, more meat! | urgent warning shout, alarmed |
| `berserker/charge_seen_2.wav` | Come on then! | urgent warning shout, alarmed |
| `berserker/charge_seen_3.wav` | Hah! Riders! | urgent warning shout, alarmed |

### Berserker: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `berserker/hold_line_1.wav` | Stand and bleed! | commanding bellow over battle, steady and fierce |
| `berserker/hold_line_2.wav` | Nobody runs! Fight! | commanding bellow over battle, steady and fierce |
| `berserker/hold_line_3.wav` | Hold, cowards! | commanding bellow over battle, steady and fierce |

### Berserker: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `berserker/rallied_1.wav` | Grr. Again! | catching breath, regaining nerve; gruff, half ashamed |
| `berserker/rallied_2.wav` | More! | catching breath, regaining nerve; gruff, half ashamed |
| `berserker/rallied_3.wav` | I'm back! Haha! | catching breath, regaining nerve; gruff, half ashamed |

### Berserker: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `berserker/wavering_1.wav` | Hrr... hrr... | breathless, afraid; under the breath |
| `berserker/wavering_2.wav` | Too many... ha... ha... | breathless, afraid; under the breath |
| `berserker/wavering_3.wav` | Blood in my eyes... | breathless, afraid; under the breath |

### Berserker: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `berserker/breaking_1.wav` | Aaagh! Away! | panicked scream, running, voice cracking |
| `berserker/breaking_2.wav` | Run! Run! | panicked scream, running, voice cracking |
| `berserker/breaking_3.wav` | Not today! | panicked scream, running, voice cracking |

### Berserker: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `berserker/hero_fell_1.wav` | The chief! NO! | stricken, disbelieving shout |
| `berserker/hero_fell_2.wav` | Who did that? WHO? | stricken, disbelieving shout |
| `berserker/hero_fell_3.wav` | Avenge him! | stricken, disbelieving shout |

### Berserker: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `berserker/enemy_hero_fell_1.wav` | Their chief is meat! | triumphant shout over battle |
| `berserker/enemy_hero_fell_2.wav` | HAHA! Dead! | triumphant shout over battle |
| `berserker/enemy_hero_fell_3.wav` | Skull for my belt! | triumphant shout over battle |

### Berserker: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `berserker/friend_killed_1.wav` | Brother! I'll eat their hearts! | shocked, grieving cry; short |
| `berserker/friend_killed_2.wav` | Grrraah! | shocked, grieving cry; short |
| `berserker/friend_killed_3.wav` | Blood for blood! | shocked, grieving cry; short |

### Berserker: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `berserker/kill_1.wav` | HAHA! | grim, surprised satisfaction; mid-effort |
| `berserker/kill_2.wav` | Dead! | grim, surprised satisfaction; mid-effort |
| `berserker/kill_3.wav` | More! | grim, surprised satisfaction; mid-effort |
| `berserker/kill_4.wav` | Blood! | grim, surprised satisfaction; mid-effort |

### Berserker: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `berserker/vet_kill_1.wav` | Next! NEXT! | cold taunt, contemptuous; spoken to the fallen |
| `berserker/vet_kill_2.wav` | Weak! | cold taunt, contemptuous; spoken to the fallen |
| `berserker/vet_kill_3.wav` | Again! | cold taunt, contemptuous; spoken to the fallen |
| `berserker/vet_kill_4.wav` | Haha, too easy! | cold taunt, contemptuous; spoken to the fallen |

### Berserker: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `berserker/promoted_1.wav` | I am the storm! | proud, grim pleasure; low |
| `berserker/promoted_2.wav` | More blood, more strength! | proud, grim pleasure; low |
| `berserker/promoted_3.wav` | Fear me! | proud, grim pleasure; low |

### Berserker: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `berserker/wounded_1.wav` | Pain is good! | pained grunt with words; through gritted teeth |
| `berserker/wounded_2.wav` | Hah! More! | pained grunt with words; through gritted teeth |
| `berserker/wounded_3.wav` | I bleed! I LIKE IT! | pained grunt with words; through gritted teeth |

### Berserker: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `berserker/arrow_fire_1.wav` | Arrows? Ha! | urgent warning, ducking; shouted |
| `berserker/arrow_fire_2.wav` | Little sticks! | urgent warning, ducking; shouted |
| `berserker/arrow_fire_3.wav` | Cowards with bows! | urgent warning, ducking; shouted |

### Berserker: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `berserker/flanked_1.wav` | From behind? COWARDS! | alarmed shout, turning round |
| `berserker/flanked_2.wav` | Turn! Kill! | alarmed shout, turning round |
| `berserker/flanked_3.wav` | Behind! | alarmed shout, turning round |

### Berserker: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `berserker/friendly_fire_1.wav` | Who threw that?! | angry shout back over the shoulder |
| `berserker/friendly_fire_2.wav` | I'll kill you next! | angry shout back over the shoulder |
| `berserker/friendly_fire_3.wav` | Watch it! | angry shout back over the shoulder |

### Berserker: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `berserker/gate_broken_1.wav` | Door is dead! | jubilant (attackers) shout over battle |
| `berserker/gate_broken_2.wav` | In! In! | jubilant (attackers) shout over battle |
| `berserker/gate_broken_3.wav` | Smash! Haha! | jubilant (attackers) shout over battle |

### Berserker: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `berserker/enemy_sighted_1.wav` | Meat! | sharp warning call, carrying |
| `berserker/enemy_sighted_2.wav` | Enemies! Finally! | sharp warning call, carrying |
| `berserker/enemy_sighted_3.wav` | There! Kill! | sharp warning call, carrying |

### Berserker: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `berserker/victory_1.wav` | HAHAHA! | exhausted cheer, then pride; loud |
| `berserker/victory_2.wav` | More! Where are the rest? | exhausted cheer, then pride; loud |
| `berserker/victory_3.wav` | Victory! Blood! | exhausted cheer, then pride; loud |

### Berserker: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `berserker/defeat_1.wav` | Bah... | broken, quiet despair |
| `berserker/defeat_2.wav` | Not... dead... yet... | broken, quiet despair |
| `berserker/defeat_3.wav` | Shame... | broken, quiet despair |

### Berserker: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `berserker/idle_1.wav` | Bored. | relaxed, bored aside; quiet |
| `berserker/idle_2.wav` | When do we kill? | relaxed, bored aside; quiet |
| `berserker/idle_3.wav` | Hrrr... | relaxed, bored aside; quiet |
| `berserker/idle_4.wav` | Need a fight. | relaxed, bored aside; quiet |

### Berserker: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `berserker/pain_1.wav` | Raargh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `berserker/pain_2.wav` | Hah! More! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `berserker/pain_3.wav` | Grraah! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `berserker/pain_4.wav` | Hrrgh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Berserker: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `berserker/death_1.wav` | Raaaargh! (dying roar) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `berserker/death_2.wav` | Ha... ha... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `berserker/death_3.wav` | Graaah! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Berserker: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `berserker/death_moan_1.wav` | Hrrk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `berserker/death_moan_2.wav` | Grrr... (a dying growl) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `berserker/death_moan_3.wav` | Ghh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Archon (`archon`): 90 lines

*Used by:* Archons.  
*Casting:* A winged warrior of the heavens: calm, resonant, unearthly, never hurried. Record dry; the page adds no reverb.

### Archon: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `archon/selected_1.wav` | We hear. | alert, ready; at speaking volume, as if the captain just turned to him |
| `archon/selected_2.wav` | Speak, mortal. | alert, ready; at speaking volume, as if the captain just turned to him |
| `archon/selected_3.wav` | We are here. | alert, ready; at speaking volume, as if the captain just turned to him |
| `archon/selected_4.wav` | Command. | alert, ready; at speaking volume, as if the captain just turned to him |
| `archon/selected_5.wav` | Yes. | alert, ready; at speaking volume, as if the captain just turned to him |

### Archon: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `archon/move_1.wav` | We go. | brisk acknowledgement, already moving; clipped |
| `archon/move_2.wav` | On wings. | brisk acknowledgement, already moving; clipped |
| `archon/move_3.wav` | It is done. | brisk acknowledgement, already moving; clipped |
| `archon/move_4.wav` | We fly. | brisk acknowledgement, already moving; clipped |
| `archon/move_5.wav` | So be it. | brisk acknowledgement, already moving; clipped |

### Archon: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `archon/attack_1.wav` | Be judged. | eager, aggressive; raised voice |
| `archon/attack_2.wav` | Fall before the light. | eager, aggressive; raised voice |
| `archon/attack_3.wav` | Strike! | eager, aggressive; raised voice |
| `archon/attack_4.wav` | Smite them. | eager, aggressive; raised voice |
| `archon/attack_5.wav` | The light comes. | eager, aggressive; raised voice |

### Archon: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* shouted over battle, full chest, rising

| File | Line | Delivery |
|---|---|---|
| `archon/charge_1.wav` | Descend! | shouted over battle, full chest, rising |
| `archon/charge_2.wav` | For the light! | shouted over battle, full chest, rising |
| `archon/charge_3.wav` | Strike as one! | shouted over battle, full chest, rising |

### Archon: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `archon/charge_seen_1.wav` | Riders below. | urgent warning shout, alarmed |
| `archon/charge_seen_2.wav` | They come at us. | urgent warning shout, alarmed |
| `archon/charge_seen_3.wav` | Stand firm. | urgent warning shout, alarmed |

### Archon: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `archon/hold_line_1.wav` | Be not afraid! | commanding bellow over battle, steady and fierce |
| `archon/hold_line_2.wav` | Stand in the light! | commanding bellow over battle, steady and fierce |
| `archon/hold_line_3.wav` | Hold, children! | commanding bellow over battle, steady and fierce |

### Archon: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `archon/rallied_1.wav` | We return. | catching breath, regaining nerve; gruff, half ashamed |
| `archon/rallied_2.wav` | Fear has passed. | catching breath, regaining nerve; gruff, half ashamed |
| `archon/rallied_3.wav` | Again. | catching breath, regaining nerve; gruff, half ashamed |

### Archon: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `archon/wavering_1.wav` | The dark is strong here... | breathless, afraid; under the breath |
| `archon/wavering_2.wav` | We falter... | breathless, afraid; under the breath |
| `archon/wavering_3.wav` | Light, sustain us... | breathless, afraid; under the breath |

### Archon: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `archon/breaking_1.wav` | Withdraw! | panicked scream, running, voice cracking |
| `archon/breaking_2.wav` | To the heavens! | panicked scream, running, voice cracking |
| `archon/breaking_3.wav` | We cannot stay! | panicked scream, running, voice cracking |

### Archon: Our hero fell (`hero_fell`)

*When:* Their own hero cut down near them.  
*Delivery:* stricken, disbelieving shout

| File | Line | Delivery |
|---|---|---|
| `archon/hero_fell_1.wav` | A bright soul is gone. | stricken, disbelieving shout |
| `archon/hero_fell_2.wav` | The lord has fallen. | stricken, disbelieving shout |
| `archon/hero_fell_3.wav` | Mourn him. | stricken, disbelieving shout |

### Archon: Enemy hero fell (`enemy_hero_fell`)

*When:* The enemy's hero cut down.  
*Delivery:* triumphant shout over battle

| File | Line | Delivery |
|---|---|---|
| `archon/enemy_hero_fell_1.wav` | Their darkness ends. | triumphant shout over battle |
| `archon/enemy_hero_fell_2.wav` | Judged and fallen. | triumphant shout over battle |
| `archon/enemy_hero_fell_3.wav` | So falls the wicked. | triumphant shout over battle |

### Archon: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `archon/friend_killed_1.wav` | A light goes out. | shocked, grieving cry; short |
| `archon/friend_killed_2.wav` | Rest now. | shocked, grieving cry; short |
| `archon/friend_killed_3.wav` | Fallen. | shocked, grieving cry; short |

### Archon: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* grim, surprised satisfaction; mid-effort

| File | Line | Delivery |
|---|---|---|
| `archon/kill_1.wav` | Judged. | grim, surprised satisfaction; mid-effort |
| `archon/kill_2.wav` | Begone. | grim, surprised satisfaction; mid-effort |
| `archon/kill_3.wav` | It ends. | grim, surprised satisfaction; mid-effort |
| `archon/kill_4.wav` | Fall. | grim, surprised satisfaction; mid-effort |

### Archon: Veteran's kill (`vet_kill`)

*When:* A veteran's kill: a taunt.  
*Delivery:* cold taunt, contemptuous; spoken to the fallen

| File | Line | Delivery |
|---|---|---|
| `archon/vet_kill_1.wav` | Unworthy. | cold taunt, contemptuous; spoken to the fallen |
| `archon/vet_kill_2.wav` | Another shadow. | cold taunt, contemptuous; spoken to the fallen |
| `archon/vet_kill_3.wav` | As it was written. | cold taunt, contemptuous; spoken to the fallen |
| `archon/vet_kill_4.wav` | Dust. | cold taunt, contemptuous; spoken to the fallen |

### Archon: Promoted (`promoted`)

*When:* Rank up (a hero: level up).  
*Delivery:* proud, grim pleasure; low

| File | Line | Delivery |
|---|---|---|
| `archon/promoted_1.wav` | We shine brighter. | proud, grim pleasure; low |
| `archon/promoted_2.wav` | Our light grows. | proud, grim pleasure; low |
| `archon/promoted_3.wav` | Glory. | proud, grim pleasure; low |

### Archon: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `archon/wounded_1.wav` | We are wounded. | pained grunt with words; through gritted teeth |
| `archon/wounded_2.wav` | The light dims... | pained grunt with words; through gritted teeth |
| `archon/wounded_3.wav` | Pain... | pained grunt with words; through gritted teeth |

### Archon: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `archon/arrow_fire_1.wav` | Arrows rise. | urgent warning, ducking; shouted |
| `archon/arrow_fire_2.wav` | Their bows. | urgent warning, ducking; shouted |
| `archon/arrow_fire_3.wav` | Beware the arrows. | urgent warning, ducking; shouted |

### Archon: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `archon/flanked_1.wav` | Behind us. | alarmed shout, turning round |
| `archon/flanked_2.wav` | They circle. | alarmed shout, turning round |
| `archon/flanked_3.wav` | Turn. | alarmed shout, turning round |

### Archon: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `archon/friendly_fire_1.wav` | Hold your arrows. | angry shout back over the shoulder |
| `archon/friendly_fire_2.wav` | We are your allies! | angry shout back over the shoulder |
| `archon/friendly_fire_3.wav` | Careful, mortal. | angry shout back over the shoulder |

### Archon: Gate broken (`gate_broken`)

*When:* The gate gives way.  
*Delivery:* jubilant (attackers) shout over battle

| File | Line | Delivery |
|---|---|---|
| `archon/gate_broken_1.wav` | The gate is fallen. | jubilant (attackers) shout over battle |
| `archon/gate_broken_2.wav` | The way is open. | jubilant (attackers) shout over battle |
| `archon/gate_broken_3.wav` | Enter. | jubilant (attackers) shout over battle |

### Archon: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* sharp warning call, carrying

| File | Line | Delivery |
|---|---|---|
| `archon/enemy_sighted_1.wav` | The enemy. | sharp warning call, carrying |
| `archon/enemy_sighted_2.wav` | We see them. | sharp warning call, carrying |
| `archon/enemy_sighted_3.wav` | Darkness approaches. | sharp warning call, carrying |

### Archon: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* exhausted cheer, then pride; loud

| File | Line | Delivery |
|---|---|---|
| `archon/victory_1.wav` | The light prevails. | exhausted cheer, then pride; loud |
| `archon/victory_2.wav` | It is finished. | exhausted cheer, then pride; loud |
| `archon/victory_3.wav` | Glory to the heavens. | exhausted cheer, then pride; loud |

### Archon: Defeat (`defeat`)

*When:* The battle is lost.  
*Delivery:* broken, quiet despair

| File | Line | Delivery |
|---|---|---|
| `archon/defeat_1.wav` | The dark has won this day. | broken, quiet despair |
| `archon/defeat_2.wav` | We must leave. | broken, quiet despair |
| `archon/defeat_3.wav` | Forgive us. | broken, quiet despair |

### Archon: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, bored aside; quiet

| File | Line | Delivery |
|---|---|---|
| `archon/idle_1.wav` | The wind is clean up here. | relaxed, bored aside; quiet |
| `archon/idle_2.wav` | We wait. | relaxed, bored aside; quiet |
| `archon/idle_3.wav` | All is still. | relaxed, bored aside; quiet |
| `archon/idle_4.wav` | Patience, mortal. | relaxed, bored aside; quiet |

### Archon: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `archon/pain_1.wav` | Ah! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `archon/pain_2.wav` | Hnn! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `archon/pain_3.wav` | Oh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Archon: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `archon/death_1.wav` | Aaaah! (a ringing cry) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `archon/death_2.wav` | Ohh... (fading) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `archon/death_3.wav` | Aaah! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Archon: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `archon/death_moan_1.wav` | Hhk... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `archon/death_moan_2.wav` | Ohh... (fading) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `archon/death_moan_3.wav` | Ah... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Engine crew (`engine`): 58 lines

*Used by:* Catapult.  
*Casting:* A siege engineer shouting to his crew: practical, sweaty, effortful. Falls back to the Soldier voice for the rest.
  
*Anything not listed here is said in the `soldier` voice.*

### Engine crew: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* alert, ready; at speaking volume, as if the captain just turned to him

| File | Line | Delivery |
|---|---|---|
| `engine/selected_1.wav` | Crew ready. | alert, ready; at speaking volume, as if the captain just turned to him |
| `engine/selected_2.wav` | Engine standing. | alert, ready; at speaking volume, as if the captain just turned to him |
| `engine/selected_3.wav` | Aye, sir? | alert, ready; at speaking volume, as if the captain just turned to him |
| `engine/selected_4.wav` | Loaded and waiting. | alert, ready; at speaking volume, as if the captain just turned to him |
| `engine/selected_5.wav` | Yes? | alert, ready; at speaking volume, as if the captain just turned to him |

### Engine crew: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brisk acknowledgement, already moving; clipped

| File | Line | Delivery |
|---|---|---|
| `engine/move_1.wav` | Heave! Moving! | brisk acknowledgement, already moving; clipped |
| `engine/move_2.wav` | Push, lads! | brisk acknowledgement, already moving; clipped |
| `engine/move_3.wav` | Slowly does it. | brisk acknowledgement, already moving; clipped |
| `engine/move_4.wav` | Rolling. | brisk acknowledgement, already moving; clipped |
| `engine/move_5.wav` | On the way. | brisk acknowledgement, already moving; clipped |

### Engine crew: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* eager, aggressive; raised voice

| File | Line | Delivery |
|---|---|---|
| `engine/attack_1.wav` | Range it! | eager, aggressive; raised voice |
| `engine/attack_2.wav` | Wind her up! | eager, aggressive; raised voice |
| `engine/attack_3.wav` | Target sighted! | eager, aggressive; raised voice |
| `engine/attack_4.wav` | Load! | eager, aggressive; raised voice |
| `engine/attack_5.wav` | Aim for the walls! | eager, aggressive; raised voice |

### Engine crew: Incoming charge (`charge_seen`)

*When:* Horse thundering down on them.  
*Delivery:* urgent warning shout, alarmed

| File | Line | Delivery |
|---|---|---|
| `engine/charge_seen_1.wav` | Horse! Guard the engine! | urgent warning shout, alarmed |
| `engine/charge_seen_2.wav` | Riders! Protect us! | urgent warning shout, alarmed |
| `engine/charge_seen_3.wav` | They're coming for the engine! | urgent warning shout, alarmed |

### Engine crew: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding bellow over battle, steady and fierce

| File | Line | Delivery |
|---|---|---|
| `engine/hold_line_1.wav` | Stay at your posts! | commanding bellow over battle, steady and fierce |
| `engine/hold_line_2.wav` | Hold, crew! | commanding bellow over battle, steady and fierce |
| `engine/hold_line_3.wav` | Don't leave the engine! | commanding bellow over battle, steady and fierce |

### Engine crew: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* catching breath, regaining nerve; gruff, half ashamed

| File | Line | Delivery |
|---|---|---|
| `engine/rallied_1.wav` | Back to the engine. | catching breath, regaining nerve; gruff, half ashamed |
| `engine/rallied_2.wav` | Right. Load again. | catching breath, regaining nerve; gruff, half ashamed |
| `engine/rallied_3.wav` | Sorry, sir. | catching breath, regaining nerve; gruff, half ashamed |

### Engine crew: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* breathless, afraid; under the breath

| File | Line | Delivery |
|---|---|---|
| `engine/wavering_1.wav` | We can't hold here... | breathless, afraid; under the breath |
| `engine/wavering_2.wav` | They're too close! | breathless, afraid; under the breath |
| `engine/wavering_3.wav` | No one to guard us... | breathless, afraid; under the breath |

### Engine crew: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked scream, running, voice cracking

| File | Line | Delivery |
|---|---|---|
| `engine/breaking_1.wav` | Leave it! Run! | panicked scream, running, voice cracking |
| `engine/breaking_2.wav` | Abandon the engine! | panicked scream, running, voice cracking |
| `engine/breaking_3.wav` | Run for it! | panicked scream, running, voice cracking |

### Engine crew: Friend killed (`friend_killed`)

*When:* A comrade killed beside him.  
*Delivery:* shocked, grieving cry; short

| File | Line | Delivery |
|---|---|---|
| `engine/friend_killed_1.wav` | We've lost a crewman! | shocked, grieving cry; short |
| `engine/friend_killed_2.wav` | No! | shocked, grieving cry; short |
| `engine/friend_killed_3.wav` | Who'll wind it now? | shocked, grieving cry; short |

### Engine crew: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained grunt with words; through gritted teeth

| File | Line | Delivery |
|---|---|---|
| `engine/wounded_1.wav` | I'm hurt! | pained grunt with words; through gritted teeth |
| `engine/wounded_2.wav` | Ah! My hand! | pained grunt with words; through gritted teeth |
| `engine/wounded_3.wav` | Keep loading... | pained grunt with words; through gritted teeth |

### Engine crew: Under arrow fire (`arrow_fire`)

*When:* Arrows falling on them.  
*Delivery:* urgent warning, ducking; shouted

| File | Line | Delivery |
|---|---|---|
| `engine/arrow_fire_1.wav` | Arrows! Get under it! | urgent warning, ducking; shouted |
| `engine/arrow_fire_2.wav` | Cover! | urgent warning, ducking; shouted |
| `engine/arrow_fire_3.wav` | Incoming! | urgent warning, ducking; shouted |

### Engine crew: Flanked (`flanked`)

*When:* Struck from the side or behind.  
*Delivery:* alarmed shout, turning round

| File | Line | Delivery |
|---|---|---|
| `engine/flanked_1.wav` | Behind the engine! | alarmed shout, turning round |
| `engine/flanked_2.wav` | They're round us! | alarmed shout, turning round |
| `engine/flanked_3.wav` | Flank! | alarmed shout, turning round |

### Engine crew: Friendly fire (`friendly_fire`)

*When:* Hit by his own side's arrows or bombs.  
*Delivery:* angry shout back over the shoulder

| File | Line | Delivery |
|---|---|---|
| `engine/friendly_fire_1.wav` | Watch your aim! | angry shout back over the shoulder |
| `engine/friendly_fire_2.wav` | We're crewing this! | angry shout back over the shoulder |
| `engine/friendly_fire_3.wav` | Not us! | angry shout back over the shoulder |

### Engine crew: Engine looses (`engine_loose`)

*When:* The catapult throws a stone.  
*Delivery:* crew call, effortful as the arm swings

| File | Line | Delivery |
|---|---|---|
| `engine/engine_loose_1.wav` | Loose! | crew call, effortful as the arm swings |
| `engine/engine_loose_2.wav` | Away she goes! | crew call, effortful as the arm swings |
| `engine/engine_loose_3.wav` | Release! | crew call, effortful as the arm swings |
| `engine/engine_loose_4.wav` | Stand clear of the arm! | crew call, effortful as the arm swings |

### Engine crew: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `engine/pain_1.wav` | Agh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `engine/pain_2.wav` | Ow! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `engine/pain_3.wav` | Ungh! | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Engine crew: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `engine/death_1.wav` | Aaargh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `engine/death_2.wav` | Ugh... (dying) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `engine/death_3.wav` | Aaagh! | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Engine crew: Shot dead (`death_moan`)

*When:* Killed by an arrow: a short, choking moan.  
*Delivery:* short, choked, as if the breath were cut off; 0.4-0.8 s; quieter

| File | Line | Delivery |
|---|---|---|
| `engine/death_moan_1.wav` | Hukk... (choking) | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `engine/death_moan_2.wav` | Uhhh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |
| `engine/death_moan_3.wav` | Ghh... | short, choked, as if the breath were cut off; 0.4-0.8 s; quieter |

## Brute (`brute`): 49 lines

*Used by:* Giants, Minotaurs.  
*Casting:* Huge creatures: deep grunts, bellows and the odd slow word. Record low and close, then it is pitched down further.

### Brute: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* short, curious; close to the mic; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/selected_1.wav` | (deep grunt) Hm? | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `brute/selected_2.wav` | (rumbling) Yes... | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `brute/selected_3.wav` | (snort) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `brute/selected_4.wav` | (slow) Who... calls? | short, curious; close to the mic; non-verbal: no words; the text describes the sound |

### Brute: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brief, effortful; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/move_1.wav` | (grunt) Go. | brief, effortful; non-verbal: no words; the text describes the sound |
| `brute/move_2.wav` | (heavy exhale) | brief, effortful; non-verbal: no words; the text describes the sound |
| `brute/move_3.wav` | (rumble) Walk... | brief, effortful; non-verbal: no words; the text describes the sound |
| `brute/move_4.wav` | (snort, stomp) | brief, effortful; non-verbal: no words; the text describes the sound |

### Brute: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* aggressive, loud; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/attack_1.wav` | (bellow) SMASH! | aggressive, loud; non-verbal: no words; the text describes the sound |
| `brute/attack_2.wav` | (roar) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `brute/attack_3.wav` | (grunt) Crush! | aggressive, loud; non-verbal: no words; the text describes the sound |
| `brute/attack_4.wav` | (snarling bellow) | aggressive, loud; non-verbal: no words; the text describes the sound |

### Brute: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* as loud as you dare, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/charge_1.wav` | (thundering roar) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `brute/charge_2.wav` | (bellow) RAAAH! | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `brute/charge_3.wav` | (snorting charge) | as loud as you dare, long; non-verbal: no words; the text describes the sound |

### Brute: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding, carrying; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/hold_line_1.wav` | (bellow) STAY! | commanding, carrying; non-verbal: no words; the text describes the sound |
| `brute/hold_line_2.wav` | (roar to the others) | commanding, carrying; non-verbal: no words; the text describes the sound |
| `brute/hold_line_3.wav` | (chest-beating roar) | commanding, carrying; non-verbal: no words; the text describes the sound |

### Brute: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* gathering itself, angry; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/rallied_1.wav` | (angry snort) | gathering itself, angry; non-verbal: no words; the text describes the sound |
| `brute/rallied_2.wav` | (low growl, gathering) | gathering itself, angry; non-verbal: no words; the text describes the sound |
| `brute/rallied_3.wav` | (grunt) Again... | gathering itself, angry; non-verbal: no words; the text describes the sound |

### Brute: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* uneasy, quiet; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/wavering_1.wav` | (uneasy rumble) | uneasy, quiet; non-verbal: no words; the text describes the sound |
| `brute/wavering_2.wav` | (low whine) | uneasy, quiet; non-verbal: no words; the text describes the sound |
| `brute/wavering_3.wav` | (nervous snorting) | uneasy, quiet; non-verbal: no words; the text describes the sound |

### Brute: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked, high; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/breaking_1.wav` | (frightened bellow) | panicked, high; non-verbal: no words; the text describes the sound |
| `brute/breaking_2.wav` | (howl of fear) | panicked, high; non-verbal: no words; the text describes the sound |
| `brute/breaking_3.wav` | (panicked roar) | panicked, high; non-verbal: no words; the text describes the sound |

### Brute: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* satisfied, short; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/kill_1.wav` | (satisfied grunt) | satisfied, short; non-verbal: no words; the text describes the sound |
| `brute/kill_2.wav` | (roar) Ha! | satisfied, short; non-verbal: no words; the text describes the sound |
| `brute/kill_3.wav` | (snort) | satisfied, short; non-verbal: no words; the text describes the sound |
| `brute/kill_4.wav` | (rumbling laugh) | satisfied, short; non-verbal: no words; the text describes the sound |

### Brute: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/wounded_1.wav` | (pained bellow) | pained; non-verbal: no words; the text describes the sound |
| `brute/wounded_2.wav` | (groan) | pained; non-verbal: no words; the text describes the sound |
| `brute/wounded_3.wav` | (hurt roar) | pained; non-verbal: no words; the text describes the sound |

### Brute: Enemy sighted (`enemy_sighted`)

*When:* First contact with the enemy.  
*Delivery:* suspicious, low; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/enemy_sighted_1.wav` | (sniffing) ...Men. | suspicious, low; non-verbal: no words; the text describes the sound |
| `brute/enemy_sighted_2.wav` | (low rumble) | suspicious, low; non-verbal: no words; the text describes the sound |
| `brute/enemy_sighted_3.wav` | (snort, warning) | suspicious, low; non-verbal: no words; the text describes the sound |

### Brute: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* triumphant, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/victory_1.wav` | (triumphant roar) | triumphant, long; non-verbal: no words; the text describes the sound |
| `brute/victory_2.wav` | (chest-pounding bellow) | triumphant, long; non-verbal: no words; the text describes the sound |
| `brute/victory_3.wav` | (laughing rumble) | triumphant, long; non-verbal: no words; the text describes the sound |

### Brute: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, soft; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `brute/idle_1.wav` | (yawning rumble) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `brute/idle_2.wav` | (scratching grunt) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `brute/idle_3.wav` | (stomach rumble) | relaxed, soft; non-verbal: no words; the text describes the sound |

### Brute: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `brute/pain_1.wav` | (a pained bellow) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `brute/pain_2.wav` | (a grunting snarl) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `brute/pain_3.wav` | (a short roar of pain) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Brute: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `brute/death_1.wav` | (a long dying bellow) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `brute/death_2.wav` | (a groaning roar, falling) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `brute/death_3.wav` | (a final grunt) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

## Undead (`undead`): 38 lines

*Used by:* Undead.  
*Casting:* The walking dead: moans, rattling breaths, dry hisses. Never words. Record breathy and hollow.

### Undead: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* short, curious; close to the mic; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/selected_1.wav` | (hollow moan) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `undead/selected_2.wav` | (dry rattle) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `undead/selected_3.wav` | (rasping breath) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `undead/selected_4.wav` | (low groan) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |

### Undead: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brief, effortful; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/move_1.wav` | (shuffling moan) | brief, effortful; non-verbal: no words; the text describes the sound |
| `undead/move_2.wav` | (hiss) | brief, effortful; non-verbal: no words; the text describes the sound |
| `undead/move_3.wav` | (rattling exhale) | brief, effortful; non-verbal: no words; the text describes the sound |
| `undead/move_4.wav` | (groan) | brief, effortful; non-verbal: no words; the text describes the sound |

### Undead: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* aggressive, loud; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/attack_1.wav` | (rising wail) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `undead/attack_2.wav` | (hungry moan) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `undead/attack_3.wav` | (shriek) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `undead/attack_4.wav` | (snarling rasp) | aggressive, loud; non-verbal: no words; the text describes the sound |

### Undead: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* as loud as you dare, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/charge_1.wav` | (chorus-like wail) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `undead/charge_2.wav` | (screech) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `undead/charge_3.wav` | (howling moan) | as loud as you dare, long; non-verbal: no words; the text describes the sound |

### Undead: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* satisfied, short; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/kill_1.wav` | (satisfied hiss) | satisfied, short; non-verbal: no words; the text describes the sound |
| `undead/kill_2.wav` | (gurgling moan) | satisfied, short; non-verbal: no words; the text describes the sound |
| `undead/kill_3.wav` | (rattle) | satisfied, short; non-verbal: no words; the text describes the sound |
| `undead/kill_4.wav` | (sigh) | satisfied, short; non-verbal: no words; the text describes the sound |

### Undead: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/wounded_1.wav` | (cracking groan) | pained; non-verbal: no words; the text describes the sound |
| `undead/wounded_2.wav` | (hiss) | pained; non-verbal: no words; the text describes the sound |
| `undead/wounded_3.wav` | (broken moan) | pained; non-verbal: no words; the text describes the sound |

### Undead: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* triumphant, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/victory_1.wav` | (long eerie moan) | triumphant, long; non-verbal: no words; the text describes the sound |
| `undead/victory_2.wav` | (whispering hiss) | triumphant, long; non-verbal: no words; the text describes the sound |
| `undead/victory_3.wav` | (rattling sigh) | triumphant, long; non-verbal: no words; the text describes the sound |

### Undead: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, soft; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `undead/idle_1.wav` | (distant moan) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `undead/idle_2.wav` | (dry creak) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `undead/idle_3.wav` | (whisper, unintelligible) | relaxed, soft; non-verbal: no words; the text describes the sound |

### Undead: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `undead/pain_1.wav` | (a dry hiss) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `undead/pain_2.wav` | (bones grinding, a rasp) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `undead/pain_3.wav` | (a hollow moan) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Undead: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `undead/death_1.wav` | (a long rattling wail) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `undead/death_2.wav` | (a sigh as the bones fall apart) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `undead/death_3.wav` | (a hollow shriek, fading) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

### Undead: Undead groan (`groan`)

*When:* The walking dead, idly: a groan, a click of the jaw.  
*Delivery:* slow, hollow, from deep in the throat; jaw clicks are welcome; 1-2 s

| File | Line | Delivery |
|---|---|---|
| `undead/groan_1.wav` | (a long low groan) | slow, hollow, from deep in the throat; jaw clicks are welcome; 1-2 s |
| `undead/groan_2.wav` | (a jaw clicking, then a rasp) | slow, hollow, from deep in the throat; jaw clicks are welcome; 1-2 s |
| `undead/groan_3.wav` | (a hollow sigh) | slow, hollow, from deep in the throat; jaw clicks are welcome; 1-2 s |
| `undead/groan_4.wav` | (teeth chattering) | slow, hollow, from deep in the throat; jaw clicks are welcome; 1-2 s |

## Fiend (`fiend`): 46 lines

*Used by:* Devils, Demons.  
*Casting:* Devils and demons: snarls, hissing laughter, guttural roars. No words. Record harsh, then pitched down.

### Fiend: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* short, curious; close to the mic; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/selected_1.wav` | (hissing laugh) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `fiend/selected_2.wav` | (low snarl) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `fiend/selected_3.wav` | (guttural purr) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `fiend/selected_4.wav` | (crackling breath) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |

### Fiend: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brief, effortful; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/move_1.wav` | (snarl) | brief, effortful; non-verbal: no words; the text describes the sound |
| `fiend/move_2.wav` | (hiss) | brief, effortful; non-verbal: no words; the text describes the sound |
| `fiend/move_3.wav` | (grunting chuckle) | brief, effortful; non-verbal: no words; the text describes the sound |
| `fiend/move_4.wav` | (wings and growl) | brief, effortful; non-verbal: no words; the text describes the sound |

### Fiend: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* aggressive, loud; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/attack_1.wav` | (shrieking roar) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `fiend/attack_2.wav` | (snarl) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `fiend/attack_3.wav` | (howl) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `fiend/attack_4.wav` | (cackling roar) | aggressive, loud; non-verbal: no words; the text describes the sound |

### Fiend: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* as loud as you dare, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/charge_1.wav` | (hellish roar) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `fiend/charge_2.wav` | (screech) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `fiend/charge_3.wav` | (bellowing snarl) | as loud as you dare, long; non-verbal: no words; the text describes the sound |

### Fiend: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding, carrying; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/hold_line_1.wav` | (commanding roar) | commanding, carrying; non-verbal: no words; the text describes the sound |
| `fiend/hold_line_2.wav` | (snarling bark) | commanding, carrying; non-verbal: no words; the text describes the sound |
| `fiend/hold_line_3.wav` | (threatening growl) | commanding, carrying; non-verbal: no words; the text describes the sound |

### Fiend: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* gathering itself, angry; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/rallied_1.wav` | (furious snarl) | gathering itself, angry; non-verbal: no words; the text describes the sound |
| `fiend/rallied_2.wav` | (growl) | gathering itself, angry; non-verbal: no words; the text describes the sound |
| `fiend/rallied_3.wav` | (hissing laugh) | gathering itself, angry; non-verbal: no words; the text describes the sound |

### Fiend: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* uneasy, quiet; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/wavering_1.wav` | (uneasy hiss) | uneasy, quiet; non-verbal: no words; the text describes the sound |
| `fiend/wavering_2.wav` | (wary growl) | uneasy, quiet; non-verbal: no words; the text describes the sound |
| `fiend/wavering_3.wav` | (snarling whine) | uneasy, quiet; non-verbal: no words; the text describes the sound |

### Fiend: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked, high; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/breaking_1.wav` | (shriek of fear) | panicked, high; non-verbal: no words; the text describes the sound |
| `fiend/breaking_2.wav` | (screaming hiss) | panicked, high; non-verbal: no words; the text describes the sound |
| `fiend/breaking_3.wav` | (fleeing howl) | panicked, high; non-verbal: no words; the text describes the sound |

### Fiend: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* satisfied, short; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/kill_1.wav` | (cackle) | satisfied, short; non-verbal: no words; the text describes the sound |
| `fiend/kill_2.wav` | (satisfied snarl) | satisfied, short; non-verbal: no words; the text describes the sound |
| `fiend/kill_3.wav` | (hiss) | satisfied, short; non-verbal: no words; the text describes the sound |
| `fiend/kill_4.wav` | (deep laugh) | satisfied, short; non-verbal: no words; the text describes the sound |

### Fiend: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/wounded_1.wav` | (pained shriek) | pained; non-verbal: no words; the text describes the sound |
| `fiend/wounded_2.wav` | (hiss of pain) | pained; non-verbal: no words; the text describes the sound |
| `fiend/wounded_3.wav` | (snarl) | pained; non-verbal: no words; the text describes the sound |

### Fiend: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* triumphant, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/victory_1.wav` | (triumphant howl) | triumphant, long; non-verbal: no words; the text describes the sound |
| `fiend/victory_2.wav` | (long cackle) | triumphant, long; non-verbal: no words; the text describes the sound |
| `fiend/victory_3.wav` | (roar) | triumphant, long; non-verbal: no words; the text describes the sound |

### Fiend: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, soft; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `fiend/idle_1.wav` | (low chuckle) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `fiend/idle_2.wav` | (snuffling) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `fiend/idle_3.wav` | (crackle and hiss) | relaxed, soft; non-verbal: no words; the text describes the sound |

### Fiend: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `fiend/pain_1.wav` | (a shriek of pain) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `fiend/pain_2.wav` | (a hissing snarl) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `fiend/pain_3.wav` | (a high screech) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Fiend: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `fiend/death_1.wav` | (a piercing death shriek) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `fiend/death_2.wav` | (a howling scream) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `fiend/death_3.wav` | (a gurgling screech) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

## Beast (`beast`): 46 lines

*Used by:* Spiders, Griffins, Pegasi, Elephants, Unicorns, Dragons.  
*Casting:* Beasts of every kind share one set of calls: screeches, trumpets, whinnies, chitters. Pick sounds that could be any of them, or record a mix; the page pitches the enemy's lower.

### Beast: Selected (`selected`)

*When:* The player picks him (or his squad).  
*Delivery:* short, curious; close to the mic; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/selected_1.wav` | (alert screech) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `beast/selected_2.wav` | (snort and whinny) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `beast/selected_3.wav` | (chitter) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |
| `beast/selected_4.wav` | (low trumpet) | short, curious; close to the mic; non-verbal: no words; the text describes the sound |

### Beast: Move ordered (`move`)

*When:* Acknowledging a march order.  
*Delivery:* brief, effortful; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/move_1.wav` | (huff) | brief, effortful; non-verbal: no words; the text describes the sound |
| `beast/move_2.wav` | (wing beats and cry) | brief, effortful; non-verbal: no words; the text describes the sound |
| `beast/move_3.wav` | (clicking) | brief, effortful; non-verbal: no words; the text describes the sound |
| `beast/move_4.wav` | (snort) | brief, effortful; non-verbal: no words; the text describes the sound |

### Beast: Attack ordered (`attack`)

*When:* Acknowledging an order to attack.  
*Delivery:* aggressive, loud; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/attack_1.wav` | (screeching cry) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `beast/attack_2.wav` | (trumpeting blast) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `beast/attack_3.wav` | (hiss) | aggressive, loud; non-verbal: no words; the text describes the sound |
| `beast/attack_4.wav` | (roar) | aggressive, loud; non-verbal: no words; the text describes the sound |

### Beast: Charge (`charge`)

*When:* Charging: ordered, or riders breaking into the gallop.  
*Delivery:* as loud as you dare, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/charge_1.wav` | (thundering trumpet) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `beast/charge_2.wav` | (diving screech) | as loud as you dare, long; non-verbal: no words; the text describes the sound |
| `beast/charge_3.wav` | (roar) | as loud as you dare, long; non-verbal: no words; the text describes the sound |

### Beast: Hold the line! (`hold_line`)

*When:* A veteran's rally call to wavering friends nearby.  
*Delivery:* commanding, carrying; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/hold_line_1.wav` | (rallying cry) | commanding, carrying; non-verbal: no words; the text describes the sound |
| `beast/hold_line_2.wav` | (trumpet call) | commanding, carrying; non-verbal: no words; the text describes the sound |
| `beast/hold_line_3.wav` | (screech to the flock) | commanding, carrying; non-verbal: no words; the text describes the sound |

### Beast: Rallied (`rallied`)

*When:* Turning back to the fight after breaking.  
*Delivery:* gathering itself, angry; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/rallied_1.wav` | (snort, calming) | gathering itself, angry; non-verbal: no words; the text describes the sound |
| `beast/rallied_2.wav` | (cry) | gathering itself, angry; non-verbal: no words; the text describes the sound |
| `beast/rallied_3.wav` | (stamping huff) | gathering itself, angry; non-verbal: no words; the text describes the sound |

### Beast: Wavering (`wavering`)

*When:* Fear creeping in; morale low.  
*Delivery:* uneasy, quiet; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/wavering_1.wav` | (nervous whinny) | uneasy, quiet; non-verbal: no words; the text describes the sound |
| `beast/wavering_2.wav` | (uneasy chittering) | uneasy, quiet; non-verbal: no words; the text describes the sound |
| `beast/wavering_3.wav` | (whine) | uneasy, quiet; non-verbal: no words; the text describes the sound |

### Beast: Breaking (`breaking`)

*When:* Broken: dropping everything and running.  
*Delivery:* panicked, high; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/breaking_1.wav` | (panicked shriek) | panicked, high; non-verbal: no words; the text describes the sound |
| `beast/breaking_2.wav` | (terrified trumpet) | panicked, high; non-verbal: no words; the text describes the sound |
| `beast/breaking_3.wav` | (squeal) | panicked, high; non-verbal: no words; the text describes the sound |

### Beast: First kill (`kill`)

*When:* His first kill of the battle.  
*Delivery:* satisfied, short; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/kill_1.wav` | (victorious screech) | satisfied, short; non-verbal: no words; the text describes the sound |
| `beast/kill_2.wav` | (snort) | satisfied, short; non-verbal: no words; the text describes the sound |
| `beast/kill_3.wav` | (roar) | satisfied, short; non-verbal: no words; the text describes the sound |
| `beast/kill_4.wav` | (chitter) | satisfied, short; non-verbal: no words; the text describes the sound |

### Beast: Badly wounded (`wounded`)

*When:* Down to a third of his strength.  
*Delivery:* pained; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/wounded_1.wav` | (pained squeal) | pained; non-verbal: no words; the text describes the sound |
| `beast/wounded_2.wav` | (hurt cry) | pained; non-verbal: no words; the text describes the sound |
| `beast/wounded_3.wav` | (whine) | pained; non-verbal: no words; the text describes the sound |

### Beast: Victory (`victory`)

*When:* The battle is won.  
*Delivery:* triumphant, long; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/victory_1.wav` | (triumphant trumpet) | triumphant, long; non-verbal: no words; the text describes the sound |
| `beast/victory_2.wav` | (soaring screech) | triumphant, long; non-verbal: no words; the text describes the sound |
| `beast/victory_3.wav` | (roar) | triumphant, long; non-verbal: no words; the text describes the sound |

### Beast: Idle chatter (`idle`)

*When:* Rare, when nothing has happened for a while.  
*Delivery:* relaxed, soft; non-verbal: no words; the text describes the sound

| File | Line | Delivery |
|---|---|---|
| `beast/idle_1.wav` | (soft snort) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `beast/idle_2.wav` | (preening chirr) | relaxed, soft; non-verbal: no words; the text describes the sound |
| `beast/idle_3.wav` | (rumbling breath) | relaxed, soft; non-verbal: no words; the text describes the sound |

### Beast: Hurt (`pain`)

*When:* Struck and hurt, still standing: a grunt or a cry.  
*Delivery:* short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch

| File | Line | Delivery |
|---|---|---|
| `beast/pain_1.wav` | (a yelp) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `beast/pain_2.wav` | (a pained screech) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |
| `beast/pain_3.wav` | (a snorting squeal) | short and sharp, from the gut; 0.2-0.5 s; vary the vowel and pitch |

### Beast: Death cry (`death`)

*When:* Killed: a scream, or a last grunt as he goes down.  
*Delivery:* a real scream or a last grunt, falling away; 0.5-1.2 s; full voice

| File | Line | Delivery |
|---|---|---|
| `beast/death_1.wav` | (a dying scream) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `beast/death_2.wav` | (a long screech) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |
| `beast/death_3.wav` | (a last snorting groan) | a real scream or a last grunt, falling away; 0.5-1.2 s; full voice |

## Herald (`herald`): 9 lines

*Used by:* (the battle's narrator).  
*Casting:* The voice over the battle, as in Myth: older, grave, unhurried, close to the microphone. Reports the player's losses; heard in the centre, never positional.

### Herald: Casualties (`casualties`)

*When:* A quarter of the player's soldiers have fallen.  
*Delivery:* clear and grave, a herald's report, mono; not shouted

| File | Line | Delivery |
|---|---|---|
| `herald/casualties_1.wav` | Casualties. | clear and grave, a herald's report, mono; not shouted |
| `herald/casualties_2.wav` | We are taking casualties. | clear and grave, a herald's report, mono; not shouted |
| `herald/casualties_3.wav` | Men are falling. | clear and grave, a herald's report, mono; not shouted |

### Herald: Severe casualties (`severe_casualties`)

*When:* Half of the player's soldiers have fallen.  
*Delivery:* clear and grave, a herald's report, mono; not shouted

| File | Line | Delivery |
|---|---|---|
| `herald/severe_casualties_1.wav` | Severe casualties. | clear and grave, a herald's report, mono; not shouted |
| `herald/severe_casualties_2.wav` | Half our strength is gone. | clear and grave, a herald's report, mono; not shouted |
| `herald/severe_casualties_3.wav` | Severe casualties, my lord. | clear and grave, a herald's report, mono; not shouted |

### Herald: Total casualties (`total_casualties`)

*When:* Four fifths of the player's soldiers have fallen.  
*Delivery:* clear and grave, a herald's report, mono; not shouted

| File | Line | Delivery |
|---|---|---|
| `herald/total_casualties_1.wav` | Total casualties. | clear and grave, a herald's report, mono; not shouted |
| `herald/total_casualties_2.wav` | The army is all but lost. | clear and grave, a herald's report, mono; not shouted |
| `herald/total_casualties_3.wav` | Total casualties, my lord. | clear and grave, a herald's report, mono; not shouted |

