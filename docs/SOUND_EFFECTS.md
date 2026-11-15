# Sound effects

Generated from `game/web/sfx/sounds.json` by `python3 tools/sfx/script.py`; do not edit by hand.
90 sounds in 256 files (4.6 MB of MP3; the page fetches what a battle needs when audio starts, the rest soon after).

## How it works

The battle has no music (as in Myth): every sound tells the player something. `game/src/sfx.cpp` turns each
tick's simulation events (missiles loosed and landing, blows by damage type and armour, blasts, gates, the
death matrix's deaths, bodies of men on the move, lit fuses) into positional cues, merged and throttled so a
volley is one louder twang and never a roar of noise; it also works out the ambience (wind by the land under
the view, the day's air, swamp insects, carrion flies over the dead, the enemy's war drums, fire, rain, and
from the far camera the mass of a fight and the tramp of columns). The table of cues (`kSpecs` in `sfx.cpp`:
id, priority, gap, whether it carries far) and this manifest (files, levels, and the `mix` block: channels,
fade, the distance filter and reverb) are the one list both players read: `game/web/sfx.js` (Web Audio) and
`game/native/audio.cpp` (the desktop's mixer). Each cue is panned, quieter, duller and more reverberant with
distance, at most `mix.strips` at once, the gravest taking the place of the slightest with a short fade.
Cries of pain and death are voices (`docs/VOICE_LINES.md`).

**Hearing** (`hear()` in `sfx.cpp`): what is on screen is heard. The reach widens with the camera
(160 m + 1.1 x its distance; 420 m + 1.5 x for the loud ones); its height takes the level down only to a floor
(about -13 dB by distance 600, -8 dB for blasts and gates) and dulls the tone instead; past 600 single sounds fade
out and the loops `snd_amb_battle_far` and `snd_amb_march_column` carry the field. **Pace**: at most 24 cues a
second of real time (a burst of 8) whatever the game's speed, priority 4 and 5 always; gaps are real time, so
at 4x and 8x they are four and eight times as many ticks, delays are divided by the speed, and priority 0 and
movement are left out from 4x. **Blows and the picture**: a hand blow's hit sounds when its attack clip shows it land
(half the clip after the strike, by the striker's kind: `Sfx::setBlowSeconds` from the sheets), the whoosh leading it. Paused, nothing starts, what is playing fades, the ambience drops to a third.

## Hooks: sounds for features being built

Call `battle.sfx().hook(Sound::X, where)` during the step in which the thing happens (before
`Sfx::observe`, i.e. from the battle's event collection); it is placed, merged and throttled like any cue.
For a sound in the player's ears whatever the camera, `battle.sfx().ui(Sound::X)`.

| Feature | Call | Notes |
|---|---|---|
| Feet, a body, a part, a stone into water | nothing to call: `Wade`, `BodySplash`, `WaterPlop`, `StoneWater` are read from the world (`Sfx::wading`, the `Splash` impact by its radius) | men in mud and marsh sound as `FootWet`, men in water as `Wade` |
| Rolling boulder | wired: `BoulderRumble` while a landed stone moves, `BoulderImpact` on a hard bounce or a wall, `BoulderCrush` for men under it; `BoulderLaunch` is still a hook, to call once where it is levered loose | launch, rumble and impact carry far |
| Satchel lay and detonate chain | nothing to call: a laid satchel sounds `SatchelLay` to its owner, any satchel whose fuse is burning down hisses `SatchelFuse` every 1.5 s (thrown, or lit by fire or a blast beside it), and each blast is `SatchelBlast` | read from the world's projectiles in `Sfx::fuses` |
| Fire on water | nothing to call: `Steam` | the water work's |
| A call for help and its answer | `CallHelp` at the caller, `CallAnswer` at the squad that turns to it | each at most once in 2 s |
| Cavalry charge striking a line | `ChargeImpact` | carries far |
| A line breaking | `Rout` | at most once in 3 s |


Every sound is rendered by `python3 tools/sfx/synth/render.py` (numpy, scipy and ffmpeg; deterministic) into
`game/web/sfx/<id>_<n>.mp3`: one-shots mono at -18 LUFS, ambience loops stereo at -24 LUFS, made seamless. The
level column is the mix level the page applies on top. Audition them all on `sfx-test.html` (the ⚙ panel's
*Test sounds* link).

## Replacing a sound with a recording

1. Record or find the sound. **Licence: only CC0 (public domain) sounds or your own recordings**, nothing that
   needs attribution or forbids redistribution: the game is open and its files are published as they are.
   Freesound.org's CC0 filter, Sonniss' GDC bundles (royalty-free, check the licence) and your own microphone
   are good sources. Keep the source and licence in a note next to your takes.
2. Record **mono, 48 kHz, 24-bit WAV** (ambience: stereo), a little longer than you need, with silence either
   side. Make several takes: each becomes one variant (`_1`, `_2`, ...); the game picks a different one each time.
3. Name each take after the file it replaces, e.g. `snd_weap_arrow_bowstring_release_2.wav`, and run

       tools/sfx/convert.sh ~/takes

   It trims the silence, normalises the loudness like the rendered files, writes the MP3 in place and lists it
   in `game/web/sfx/recorded.json`, so re-rendering never overwrites it. Then rebuild or reload.

## Ambience

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Mountain wind**<br>`snd_amb_wind_mountain` | Over high passes and peaks (the continent's mountain value under the view): a hollow high howl. (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -18 dB | Wind across a bottle mouth; a real gale through a gap (wind shield on the mic). |
| **Valley wind**<br>`snd_amb_wind_valley` | Over the lowlands: broad low gusts with grit; louder as the camera rises. (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -20 dB | Wind in grass and trees on an open field, no traffic. |
| **Swamp insects**<br>`snd_amb_swamp_insects` | Near marsh and mud: cicadas and crickets, panned towards the wet ground. (an ambience loop, faded by the game) | loop, stereo, panned, 10.2-10.2 s | 1 (rendered) | -22 dB | A summer marsh at dusk, or CC0 insect field recordings. |
| **Frog**<br>`snd_amb_swamp_frogs` | Now and then out in the marsh: a single croak at a random point. | one-shot, positional, 0.9-1.7 s | 4 (rendered) | -12 dB | Any frog recording; or a balloon squeaked and a comb strummed. |
| **Carrion flies**<br>`snd_amb_carrion_flies` | Over the dead (12 s after they fall), louder the more bodies lie near the view. (an ambience loop, faded by the game) | loop, stereo, panned, 10.2-10.2 s | 1 (rendered) | -20 dB | Flies over rotting fruit in a bin, close mic. |
| **War drums**<br>`snd_amb_war_drums` | Distant, muffled: the enemy approaching (beyond bowshot), or held land (a siege). (an ambience loop, faded by the game) | loop, stereo, panned, 9.8-9.8 s | 1 (rendered) | -16 dB | Floor toms or a big drum in a large room, recorded from the next room. |
| **Light rain**<br>`snd_amb_rain_light` | Weather hook: light rain, a soft patter. (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -20 dB | Rain on leaves and a tent. |
| **Heavy rain**<br>`snd_amb_rain_heavy` | Weather hook: a downpour, roaring white noise (damps open fuses). (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -18 dB | A heavy shower on a roof, mic indoors by an open window. |
| **Thunder**<br>`snd_amb_thunder` | Weather hook: a low rolling rumble 3-8 s after a lightning flash. | one-shot, 2D, 4.4-7.4 s | 3 (rendered) | -6 dB | A sheet of thin metal shaken, pitched down; or CC0 thunder. |
| **Cave drips (future)**<br>`snd_amb_cave_drip` | For future cave maps: water drops with a long reverb. (for cave maps: not played yet) | one-shot, positional, 2.0-2.1 s | 4 (rendered) | -14 dB | Drops from a tap into a bucket in a tiled bathroom. |
| **Deep rumble (future)**<br>`snd_amb_subterranean_rumble` | For future cave or quake moments: a sub-bass rumble. (for caves: not played yet) | loop, 2D, 10.2-10.2 s | 1 (rendered) | -18 dB | A car engine idling heard through a wall, pitched down. |
| **Battle, far off**<br>`snd_amb_battle_far` | A fight heard from high above or from the map: steel, thuds and shouting as one murmur, no blow distinct. (an ambience loop, faded by the game) | loop, 2D stereo, 12.2-12.2 s | 1 (rendered) | -12 dB | A crowd at a match and a re-enactment melee, mixed and heard through a wall; 12 s or more. |
| **Column on the march**<br>`snd_amb_march_column` | An army on the move heard from afar: a steady tramp with the jingle of gear. (an ambience loop, faded by the game) | loop, 2D stereo, 9.8-9.8 s | 1 (rendered) | -14 dB | A walking group in step on a track, recorded from fifty metres, keys and buckles jingling; a whole number of paces. |
| **Open air by day**<br>`snd_amb_bed_day` | Always there by day: very light air and a few distant birds, so the field is never dead silent. (an ambience loop, faded by the game) | loop, 2D stereo, 20.2-20.2 s | 1 (rendered) | -22 dB | A quiet field in the morning away from roads, 20 s or more with no one bird dominating. |
| **Ground burning**<br>`snd_amb_fire` | Fire on the ground near the view: crackle over a low roar. (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -14 dB | A bonfire of dry wood from two metres, with no voices. |

## Weapons

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Bowstring release**<br>`snd_weap_arrow_bowstring_release` | An archer looses an arrow (a volley: one louder twang). | one-shot, positional, pitch +-5%, 0.4-0.5 s | 5 (rendered) | -3 dB | A recurve or longbow released without an arrow in a quiet room (dry fire can break a bow: use a blank arrow into a bale). Close mic, 30 cm. |
| **Arrow passing**<br>`snd_weap_arrow_whistle` | An arrow flies close past the listener while the camera is low; pitched by its closing speed (doppler). | one-shot, positional, pitch swept by the page, 0.9-1.1 s | 4 (rendered) | -6 dB | Whip a thin dowel or an arrow past the mic, or swing a fishing rod tip; trim to the pass. |
| **Sword swing**<br>`snd_weap_sword_swing` | A cut or thrust is struck (just before it lands). | one-shot, positional, 0.5-0.5 s | 5 (rendered) | -8 dB | Swing a wooden sword or a thin stick fast past the mic; several speeds. |
| **Heavy swing**<br>`snd_weap_heavy_swing` | A club, axe or great beast's swipe. | one-shot, positional, 0.7-0.8 s | 4 (rendered) | -6 dB | Swing a broom handle or a towel hard past the mic; low and slow. |
| **Blade into flesh**<br>`snd_weap_sword_flesh` | A cut or chop lands on an unarmoured body: wet tearing slash. | one-shot, positional, 0.5-0.5 s | 5 (rendered) | -3 dB | Knife into a cabbage or a raw chicken, wet celery snapped and twisted; layer with a short wet squelch. |
| **Blade on armour**<br>`snd_weap_sword_armor` | A blow lands on mail or plate: bright metallic clash. | one-shot, positional, 1.4-1.6 s | 5 (rendered) | -4 dB | Strike a steel pan lid, a shovel or a tool with a metal bar; hold it loosely so it rings. |
| **Blunt crush**<br>`snd_weap_blunt_crush` | A club, hoof or staff lands: heavy concussive crush. | one-shot, positional, 0.6-0.7 s | 4 (rendered) | -3 dB | A rubber mallet into a pumpkin or a wet phone book; a thump on a sandbag. |
| **Point into flesh**<br>`snd_weap_pierce_flesh` | A pike, lance, fang or horn goes in. | one-shot, positional, 0.4-0.5 s | 4 (rendered) | -4 dB | Stab a melon or a cabbage with a screwdriver; a short wet push. |
| **Blow on a shield**<br>`snd_weap_block_shield` | A blow is taken on a wooden shield: a solid wooden thud. | one-shot, positional, 0.5-0.5 s | 4 (rendered) | -5 dB | Hit a thick plank or a wooden chopping board with a stick. |
| **Blow on bone**<br>`snd_weap_hit_bone` | A blow or arrow strikes the walking dead: dry bone crack and rattle. | one-shot, positional, 0.4-0.5 s | 4 (rendered) | -5 dB | Snap dry twigs or a chicken bone, rattle a bag of sticks. |
| **Satchel thrown**<br>`snd_weap_satchel_throw` | A dwarf grenadier lobs a satchel charge (the grunt is the dwarf's voice). | one-shot, positional, 0.9-0.9 s | 3 (rendered) | -6 dB | A cloth bag of gravel and a half-full bottle swung and tossed onto grass. |
| **Fuse hissing**<br>`snd_weap_satchel_fuse` | A satchel's lit fuse fizzing as it flies and rolls. | one-shot, positional, 1.6-1.7 s | 3 (rendered) | -10 dB | A sparkler or a match head hissing, close mic (outdoors, safely). |
| **Fireball cast**<br>`snd_weap_fireball_cast` | A wizard gathers fire and hurls it: crackle building to a whoosh. | one-shot, positional, 1.8-1.8 s | 3 (rendered) | -4 dB | A torch whooshed past the mic, a gas burner lit; layer with crackling paper. |
| **Catapult release**<br>`snd_weap_catapult_release` | A catapult's arm slams into its crossbar: creak and thunk. | one-shot, positional, carries far, 2.0-2.0 s | 3 (rendered) | -2 dB | A heavy door slammed into its frame, a log dropped on a beam; creak a wooden chair first. |
| **Thrown spear (future)**<br>`snd_weap_spear_scream` | For a future javelin unit (Myth's Soulless): a metallic scream into an air-rending whistle. (for javelin throwers: not played yet) | one-shot, positional, 1.0-1.1 s | 3 (rendered) | -4 dB | A metal rod scraped then whipped past the mic. |
| **Lightning bolt (future)**<br>`snd_weap_lightning` | For a future storm caster (Myth's Fetch): an electric crackle building, then a huge snap. (for storm casters: not played yet) | one-shot, positional, 3.7-3.8 s | 3 (rendered) | -2 dB | A cracking whip close up layered with crackling foil and a big thunder clap. |
| **Healing**<br>`snd_magic_heal` | A journeyman mends a wounded friend: a soft rising shimmer. | one-shot, positional, 2.9-2.9 s | 3 (rendered) | -10 dB | Rub a wet finger round a wine glass; brush wind chimes gently. |
| **Satchel laid**<br>`snd_weap_satchel_lay` | A satchel is set down as a trap and its slow match struck: a soft thump, a flint scratch. | one-shot, positional, 0.9-0.9 s | 3 (rendered) | -8 dB | Set a canvas bag of sand on earth, then strike a match or a flint and steel close to the mic. |

## Impacts

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Arrow into the ground**<br>`snd_impact_arrow_dirt` | An arrow strikes earth or grass: a dull dead thud. | one-shot, positional, 0.3-0.4 s | 5 (rendered) | -10 dB | Push a stick sharply into soft earth or a bale of straw. |
| **Arrow into wood**<br>`snd_impact_arrow_wood` | An arrow sticks in a tree, a door or a beam. | one-shot, positional, 0.4-0.5 s | 4 (rendered) | -6 dB | Throw a dart into a plank; a knife into a log. |
| **Arrow off stone**<br>`snd_impact_arrow_stone` | An arrow glances off a wall, rock or merlon. | one-shot, positional, 0.4-0.4 s | 4 (rendered) | -7 dB | Flick a nail or a pencil against a stone step; a short tick and skitter. |
| **Arrow into a shield**<br>`snd_impact_arrow_shield` | A shield catches an arrow: a splintering wooden crack. | one-shot, positional, 0.5-0.5 s | 4 (rendered) | -5 dB | Snap a thin batten, or hit a plywood sheet with a dart. |
| **Arrow off armour**<br>`snd_impact_arrow_armor` | An arrow strikes mail or plate: a metallic ring. | one-shot, positional, 0.7-0.8 s | 4 (rendered) | -6 dB | Flick a nail at a steel pan or a shovel blade. |
| **Arrow into flesh**<br>`snd_impact_arrow_flesh` | An arrow hits a man: a wet thwack. | one-shot, positional, 0.4-0.4 s | 5 (rendered) | -4 dB | Slap a wet towel; a dart into a raw steak. |
| **Arrow into water**<br>`snd_impact_arrow_water` | An arrow falls in a river or lake. | one-shot, positional, 0.6-0.6 s | 3 (rendered) | -10 dB | Drop a pencil into a bath from a height. |
| **Wading**<br>`snd_water_wade` | Men, horses and beasts going through water (a ford, the shallows): the slosh at their feet, louder the more of them and the faster. | one-shot, positional, 0.7-0.8 s | 4 (rendered) | -12 dB | Walk and run through a stream or a paddling pool in boots; a hand swept through a full bath. |
| **Something falls in the water**<br>`snd_water_plop` | A head, a limb or a gobbet lands in a river or lake. | one-shot, positional, 0.6-0.7 s | 3 (rendered) | -8 dB | Drop an apple and a wet sponge into a bath from shoulder height. |
| **Stone lands**<br>`snd_impact_stone_smash` | A catapult stone smashes down: crash, crunch and scattering grit. | one-shot, positional, carries far, 2.0-2.2 s | 3 (rendered) | +0 dB | Drop a paving slab onto gravel; a rock onto a pile of stones. |
| **Stone into water**<br>`snd_impact_stone_water` | A catapult stone lands in a river or lake: a boom, a column of water and its rain. | one-shot, positional, carries far, 3.4-3.5 s | 3 (rendered) | +0 dB | Throw the biggest rock you can lift into a deep pond, mic well back. |
| **Gate struck**<br>`snd_impact_gate_wood` | A blow or blast hits a castle gate: a deep wooden boom. | one-shot, positional, carries far, 1.4-1.5 s | 4 (rendered) | -2 dB | A sledgehammer on a thick wooden door; record from inside the room too. |
| **Gate breaks**<br>`snd_impact_gate_break` | A gate gives way: splintering timbers and a crash. | one-shot, positional, carries far, 3.4-3.6 s | 3 (rendered) | +0 dB | Break a pallet or old crates with a sledgehammer; layer a heavy fall. |

## Explosions

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Satchel charge**<br>`snd_expl_satchel` | A satchel charge explodes: huge sub-bass blast with a long tail. | one-shot, positional, carries far, 4.3-4.5 s | 4 (rendered) | +0 dB | Not recordable at home: use a CC0 explosion library, or a balloon burst close up pitched down an octave and layered with a sub sine drop. |
| **Fireball bursts**<br>`snd_expl_fireball` | A fireball (or a dwarf's cocktail) lands: glass shatter crossfaded into a roaring, crackling fire whoosh. | one-shot, positional, carries far, 4.2-4.2 s | 4 (rendered) | -1 dB | A bottle broken in a bin (wear goggles) layered with a gas burner whoosh and crackling kindling. |
| **Fire on water**<br>`snd_water_steam` | A fireball or a burning bottle bursts on water: a hiss of steam, the surface boiling and spitting. | one-shot, positional, 2.6-2.7 s | 3 (rendered) | -5 dB | Quench a hot pan or a burning stick in a bucket of water. |

## Gore

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Dismember snap**<br>`snd_gore_dismember_snap` | A cut takes off a limb or head: a sharp organic crunch, a tendon snapping. | one-shot, positional, 0.6-0.7 s | 4 (rendered) | -3 dB | Snap a stick of celery or a carrot wrapped in a wet cloth; twist a raw chicken wing apart. |
| **Gib burst**<br>`snd_gore_gib_burst` | A body is torn apart (a blast, or a blow far past death): a saturated, wet, low organic burst. | one-shot, positional, 1.3-1.4 s | 4 (rendered) | -1 dB | Drop a water-soaked sponge or a wet bag of jelly onto concrete; layer a stomped melon. |
| **Blood rain**<br>`snd_gore_blood_rain` | After a gib burst: drops and bits pattering down all round. | one-shot, positional, 1.7-1.7 s | 3 (rendered) | -8 dB | Flick water from a brush onto paper and leaves; drop wet pasta onto a board. |

## Deaths

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Body falls**<br>`snd_gore_body_fall` | A body hits the ground (after the death cry). | one-shot, positional, 0.7-0.9 s | 5 (rendered) | -5 dB | Drop a sack of potatoes or a duffel bag of clothes onto earth. |
| **Armoured body falls**<br>`snd_gore_body_fall_armor` | A man in armour (or a great beast) hits the ground: thud and clatter. | one-shot, positional, 1.0-1.0 s | 4 (rendered) | -4 dB | Drop a duffel bag with a few pans and chains in it. |
| **Body falls in water**<br>`snd_water_splash_body` | A man or a beast killed in the water goes down into it; a trunk thrown into it. | one-shot, positional, 1.3-1.5 s | 3 (rendered) | -4 dB | Push a full sack or a big log into a pond; a belly flop recorded from the bank. |
| **Bones collapse**<br>`snd_gore_bones_collapse` | One of the walking dead falls apart: a dry clattering heap. | one-shot, positional, 1.1-1.4 s | 4 (rendered) | -5 dB | Drop a bundle of dry sticks and a few wooden spoons onto a floor. |
| **Burning body**<br>`snd_gore_flesh_burn` | A body killed by fire: sizzle and crackle as it burns. | one-shot, positional, 2.2-2.3 s | 3 (rendered) | -7 dB | Bacon in a hot pan, close; crackling twigs on a fire. |

## Notifications

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Victory**<br>`snd_state_victory_fanfare` | The battle is won: a short brass fanfare. | one-shot, 2D, 4.4-4.4 s | 1 (rendered) | -2 dB | A brass player, or CC0: a short major fanfare, 3-4 s. |
| **Defeat**<br>`snd_state_defeat_drone` | The battle is lost: a low minor-chord string drone with sub-bass. | one-shot, 2D, 8.6-8.6 s | 1 (rendered) | -3 dB | Cello or synth strings holding a low minor chord, 5-6 s, fading. |
| **War horn**<br>`snd_state_war_horn` | Horde: the next wave sets out for the hill. | one-shot, 2D, 5.8-5.8 s | 1 (rendered) | -4 dB | A cow horn or brass, two long low calls a fifth apart, 3-4 s. |
| **Alarm**<br>`snd_state_alarm` | Capture the flag: enemies close on the player's flag. | one-shot, 2D, 2.1-2.1 s | 1 (rendered) | -4 dB | Three short rising blasts of a hand horn, about 1.2 s. |
| **The lord's Rally**<br>`snd_state_rally_horn` | The lord calls his men to him: a short bright horn call, two rising notes. | one-shot, 2D, 1.3-1.3 s | 2 (rendered) | -5 dB | A hunting horn, bugle or trumpet: two quick rising notes, 1.2 s. |
| **The battle begins**<br>`snd_state_deploy_begin` | Deployment ends and the battle starts: a short drum roll into a horn stab. | one-shot, 2D, 1.8-1.8 s | 1 (rendered) | -4 dB | A snare or tabor roll of under a second into one stroke of a big drum and a single brass chord. |
| **The results sheet**<br>`snd_state_results` | The results are shown: parchment unrolled and one low drum stroke. | one-shot, 2D, 1.2-1.2 s | 1 (rendered) | -8 dB | Unroll a sheet of stiff paper close to the mic; one soft stroke on a floor tom. |

## UI

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Button**<br>`snd_ui_click` | Any button tapped. (played by the page) | one-shot, 2D, 0.0-0.0 s | 3 (rendered) | -8 dB | A pen click or a wooden bead tapped on a table. |
| **Page turn**<br>`snd_ui_page_turn` | A panel opens (settings, news, a town, a briefing). (played by the page) | one-shot, 2D, 0.5-0.6 s | 3 (rendered) | -8 dB | Turn a stiff page of an old book, close mic. |
| **Order given**<br>`snd_ui_order_confirm` | The player's order is accepted (move, attack, any command): a short soft drum tap with a creak of leather. | one-shot, 2D, 0.2-0.2 s | 3 (rendered) | -9 dB | A finger tap on a small hand drum or a leather-bound book, with a belt flexed; 0.2 s, dry. |
| **Order refused**<br>`snd_ui_order_refuse` | An order that cannot be carried out: a dull double knock, the second lower. | one-shot, 2D, 0.3-0.3 s | 2 (rendered) | -9 dB | Two knuckle knocks on a table, the second nearer the middle; 0.25 s, dry. |
| **Toggle**<br>`snd_ui_toggle` | Hold/Support and other switches: a wooden latch, a two-tone click. | one-shot, 2D, 0.1-0.1 s | 2 (rendered) | -11 dB | A wooden latch or a clothes peg opened and closed. |
| **Squad selected**<br>`snd_ui_select` | A squad or lord is selected: a very short soft tick. | one-shot, 2D, quiet, 0.1-0.1 s | 3 (rendered) | -13 dB | A fingernail on a wooden bead or the edge of a book. |

## Movement

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Men marching**<br>`snd_move_foot_march` | A squad on foot moving over earth: a loose cluster of footfalls, cloth and leather. | one-shot, positional, retriggered while they move, 0.7-0.8 s | 4 (rendered) | -9 dB | Four or five people walking out of step on packed earth or a lawn, coats on; cut 0.7 s pieces with no single clear step at the start. |
| **Armoured men marching**<br>`snd_move_foot_armor` | A squad in mail or plate moving: footfalls with the clink and rattle of harness. | one-shot, positional, retriggered while they move, 0.7-0.7 s | 4 (rendered) | -8 dB | As the march, with a bag of chain, buckles and a few pan lids shaken in step. |
| **Men in mud or shallows**<br>`snd_move_foot_wet` | A squad moving through mud, marsh or shallow water: sloshing, sucking steps. | one-shot, positional, retriggered while they move, 0.8-0.8 s | 3 (rendered) | -8 dB | Wellington boots in a muddy ditch or the edge of a pond, several steps overlapping. |
| **Hooves**<br>`snd_move_hooves` | Cavalry moving: several horses at the trot or gallop on turf. | one-shot, positional, retriggered while they move, 0.7-0.8 s | 4 (rendered) | -6 dB | Horses on grass at a riding school; or halved coconut shells and fists on turf, three or four beats to a stride, layered. |
| **A giant's tread**<br>`snd_move_giant_tread` | A great beast or giant sets a foot down: a heavy thump with a crunch. | one-shot, positional, one per footfall, 0.8-0.8 s | 3 (rendered) | -4 dB | Drop a sandbag on gravel; layer a fist on a mattress for the weight. |
| **Siege engine rolling**<br>`snd_move_wheels` | A catapult or other engine on the move: the axle creaking, wheels grinding over the ground. | one-shot, positional, retriggered while they move, 1.0-1.1 s | 3 (rendered) | -8 dB | A loaded wheelbarrow or handcart pushed over gravel; an unoiled gate hinge worked slowly for the axle. |
| **Great wings**<br>`snd_move_wings` | A griffin, dragon or other winged beast beats its wings once or twice. | one-shot, positional, retriggered while they move, 0.6-0.9 s | 3 (rendered) | -8 dB | Snap a heavy coat, a leather jacket or a tarpaulin through the air, once or twice a take. |

## Conquest

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Town taken**<br>`snd_conq_town_captured` | Conquest: one of your armies takes a town: a bright peal of bells and a short fanfare. | one-shot, 2D, 2.8-2.8 s | 1 (rendered) | -4 dB | Church bells ringing rounds (CC0), under a three-note rising brass call. |
| **Town lost**<br>`snd_conq_town_lost` | Conquest: a town of yours falls: a low bell tolling twice and a horn in the minor. | one-shot, 2D, 2.6-2.6 s | 1 (rendered) | -4 dB | One low bell struck twice, a second apart; a horn or trombone holding a minor third and falling. |
| **A new day**<br>`snd_conq_day_tick` | Conquest: dawn of a new day: one soft bell. | one-shot, 2D, quiet, 1.2-1.2 s | 2 (rendered) | -12 dB | A small hand bell or a wine glass struck once with a soft beater. |
| **Squad trained**<br>`snd_conq_recruit` | Conquest: a squad is trained in a town: a drum tap, a clink of coin and steel. | one-shot, 2D, 0.8-0.8 s | 2 (rendered) | -9 dB | A tap on a small drum, coins dropped on a table, a knife set down on a plate. |
| **A lord has fallen**<br>`snd_conq_lord_fallen` | Conquest: a lord is killed: a low horn falling away over a muffled drum. | one-shot, 2D, 3.2-3.2 s | 1 (rendered) | -4 dB | A horn or trombone holding a low note and letting it fall a fourth; a big drum with a cloth over the head, three slow strokes. |

## Hooks for features being built

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Boulder levered loose**<br>`snd_boulder_launch` | A boulder is set rolling: timber cracking and stone grinding. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, positional, 1.9-1.9 s | 2 (rendered) | -3 dB | A crowbar levering a paving slab; a thick branch broken. |
| **Boulder rolling**<br>`snd_boulder_rumble` | A boulder on its way down: rumble with stones clattering. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, positional, retriggered while it rolls, 1.2-1.2 s | 3 (rendered) | -4 dB | A bowling ball or a heavy stone rolled over gravel and boards; cut 1.2 s pieces from the middle. |
| **Boulder strikes**<br>`snd_boulder_impact` | A boulder hits the ground or a wall: a crash and a scatter of stone. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, positional, carries far, 2.2-2.5 s | 3 (rendered) | +0 dB | A big rock dropped on rubble from a height. |
| **Crushed by a boulder**<br>`snd_boulder_crush` | Men under a rolling boulder: a wet crunch and a clatter of gear. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, positional, 0.9-0.9 s | 3 (rendered) | -2 dB | A rock dropped on a crate of celery, wet rags and pan lids. |
| **Call for help**<br>`snd_call_help` | A squad in trouble calls for help: an urgent horn, two quick rising notes. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, 2D, 1.0-1.0 s | 2 (rendered) | -5 dB | A bugle or horn: two quick rising notes, urgent, 1 s. |
| **The call answered**<br>`snd_call_answer` | Help is coming: one lower steady note in answer. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, 2D, 0.8-0.8 s | 2 (rendered) | -6 dB | The same horn: one steady note a fifth or more below the call, 0.8 s. |
| **Charge strikes home**<br>`snd_charge_impact` | Cavalry crashes into a line: bodies, cracking lances, harness. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, positional, carries far, 1.2-1.2 s | 3 (rendered) | -1 dB | Layer a dropped sack, broom handles snapped, a bag of chain thrown down and a horse blowing. |
| **A line breaks**<br>`snd_morale_rout` | A squad routs: a ragged falling cry of many voices and arms dropped. (a hook: the feature that plays it is being built; ask for it with Sfx::hook) | one-shot, positional, 2.0-2.1 s | 2 (rendered) | -4 dB | Six or more people crying out and trailing off as they turn away from the mic; pans and poles dropped after. |

