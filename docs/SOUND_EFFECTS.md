# Sound effects

Generated from `game/web/sfx/sounds.json` by `python3 tools/sfx/script.py`; do not edit by hand.
51 sounds in 166 files (2.9 MB of MP3, each sound fetched the first time it plays).

## How it works

The battle has no music (as in Myth): every sound tells the player something. `game/src/sfx.cpp` turns each
tick's simulation events (missiles loosed and landing, blows by damage type and armour, blasts, gates, the
death matrix's deaths) into positional cues, merged and throttled so a volley is one louder twang and never a
roar of noise; it also works out the ambience (wind by the land under the view, swamp insects, carrion flies
over the dead, the enemy's war drums, rain). `game/web/sfx.js` plays them with Web Audio: panned, quieter,
duller and more reverberant with distance, at most 12 at once from a pool of reused channels, the gravest
taking the place of the slightest. Cries of pain and death are voices (`docs/VOICE_LINES.md`).

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
| **War drums**<br>`snd_amb_war_drums` | Distant, muffled: the enemy approaching (beyond bowshot), or held land (a siege). (an ambience loop, faded by the game) | loop, stereo, panned, 5.0-5.0 s | 1 (rendered) | -16 dB | Floor toms or a big drum in a large room, recorded from the next room. |
| **Light rain**<br>`snd_amb_rain_light` | Weather hook: light rain, a soft patter. (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -20 dB | Rain on leaves and a tent. |
| **Heavy rain**<br>`snd_amb_rain_heavy` | Weather hook: a downpour, roaring white noise (damps open fuses). (an ambience loop, faded by the game) | loop, 2D stereo, 10.2-10.2 s | 1 (rendered) | -18 dB | A heavy shower on a roof, mic indoors by an open window. |
| **Thunder**<br>`snd_amb_thunder` | Weather hook: a low rolling rumble 3-8 s after a lightning flash. | one-shot, 2D, 4.8-8.0 s | 3 (rendered) | -6 dB | A sheet of thin metal shaken, pitched down; or CC0 thunder. |
| **Cave drips (future)**<br>`snd_amb_cave_drip` | For future cave maps: water drops with a long reverb. (for cave maps: not played yet) | one-shot, positional, 2.0-2.1 s | 4 (rendered) | -14 dB | Drops from a tap into a bucket in a tiled bathroom. |
| **Deep rumble (future)**<br>`snd_amb_subterranean_rumble` | For future cave or quake moments: a sub-bass rumble. (for caves: not played yet) | loop, 2D, 10.2-10.2 s | 1 (rendered) | -18 dB | A car engine idling heard through a wall, pitched down. |

## Weapons

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Bowstring release**<br>`snd_weap_arrow_bowstring_release` | An archer looses an arrow (a volley: one louder twang). | one-shot, positional, pitch +-5%, 0.4-0.5 s | 5 (rendered) | -3 dB | A recurve or longbow released without an arrow in a quiet room (dry fire can break a bow: use a blank arrow into a bale). Close mic, 30 cm. |
| **Arrow passing**<br>`snd_weap_arrow_whistle` | An arrow flies close past the listener while the camera is low; pitched by its closing speed (doppler). | one-shot, positional, pitch swept by the page, 0.9-1.1 s | 4 (rendered) | -6 dB | Whip a thin dowel or an arrow past the mic, or swing a fishing rod tip; trim to the pass. |
| **Sword swing**<br>`snd_weap_sword_swing` | A cut or thrust is struck (just before it lands). | one-shot, positional, 0.5-0.5 s | 5 (rendered) | -8 dB | Swing a wooden sword or a thin stick fast past the mic; several speeds. |
| **Heavy swing**<br>`snd_weap_heavy_swing` | A club, axe or great beast's swipe. | one-shot, positional, 0.7-0.8 s | 4 (rendered) | -6 dB | Swing a broom handle or a towel hard past the mic; low and slow. |
| **Blade into flesh**<br>`snd_weap_sword_flesh` | A cut or chop lands on an unarmoured body: wet tearing slash. | one-shot, positional, 0.5-0.5 s | 5 (rendered) | -3 dB | Knife into a cabbage or a raw chicken, wet celery snapped and twisted; layer with a short wet squelch. |
| **Blade on armour**<br>`snd_weap_sword_armor` | A blow lands on mail or plate: bright metallic clash. | one-shot, positional, 0.8-0.9 s | 5 (rendered) | -4 dB | Strike a steel pan lid, a shovel or a tool with a metal bar; hold it loosely so it rings. |
| **Blunt crush**<br>`snd_weap_blunt_crush` | A club, hoof or staff lands: heavy concussive crush. | one-shot, positional, 0.8-0.9 s | 4 (rendered) | -3 dB | A rubber mallet into a pumpkin or a wet phone book; a thump on a sandbag. |
| **Point into flesh**<br>`snd_weap_pierce_flesh` | A pike, lance, fang or horn goes in. | one-shot, positional, 0.4-0.5 s | 4 (rendered) | -4 dB | Stab a melon or a cabbage with a screwdriver; a short wet push. |
| **Blow on a shield**<br>`snd_weap_block_shield` | A blow is taken on a wooden shield: a solid wooden thud. | one-shot, positional, 0.5-0.5 s | 4 (rendered) | -5 dB | Hit a thick plank or a wooden chopping board with a stick. |
| **Blow on bone**<br>`snd_weap_hit_bone` | A blow or arrow strikes the walking dead: dry bone crack and rattle. | one-shot, positional, 0.4-0.5 s | 4 (rendered) | -5 dB | Snap dry twigs or a chicken bone, rattle a bag of sticks. |
| **Satchel thrown**<br>`snd_weap_satchel_throw` | A dwarf grenadier lobs a satchel charge (the grunt is the dwarf's voice). | one-shot, positional, 0.9-0.9 s | 3 (rendered) | -6 dB | A cloth bag of gravel and a half-full bottle swung and tossed onto grass. |
| **Fuse hissing**<br>`snd_weap_satchel_fuse` | A satchel's lit fuse fizzing as it flies and rolls. | one-shot, positional, 1.6-1.7 s | 3 (rendered) | -10 dB | A sparkler or a match head hissing, close mic (outdoors, safely). |
| **Fireball cast**<br>`snd_weap_fireball_cast` | A wizard gathers fire and hurls it: crackle building to a whoosh. | one-shot, positional, 1.8-1.8 s | 3 (rendered) | -4 dB | A torch whooshed past the mic, a gas burner lit; layer with crackling paper. |
| **Catapult release**<br>`snd_weap_catapult_release` | A catapult's arm slams into its crossbar: creak and thunk. | one-shot, positional, carries far, 1.8-2.0 s | 3 (rendered) | -2 dB | A heavy door slammed into its frame, a log dropped on a beam; creak a wooden chair first. |
| **Thrown spear (future)**<br>`snd_weap_spear_scream` | For a future javelin unit (Myth's Soulless): a metallic scream into an air-rending whistle. (for javelin throwers: not played yet) | one-shot, positional, 1.0-1.1 s | 3 (rendered) | -4 dB | A metal rod scraped then whipped past the mic. |
| **Lightning bolt (future)**<br>`snd_weap_lightning` | For a future storm caster (Myth's Fetch): an electric crackle building, then a huge snap. (for storm casters: not played yet) | one-shot, positional, 3.7-3.8 s | 3 (rendered) | -2 dB | A cracking whip close up layered with crackling foil and a big thunder clap. |
| **Healing**<br>`snd_magic_heal` | A journeyman mends a wounded friend: a soft rising shimmer. | one-shot, positional, 2.9-2.9 s | 3 (rendered) | -10 dB | Rub a wet finger round a wine glass; brush wind chimes gently. |

## Impacts

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Arrow into the ground**<br>`snd_impact_arrow_dirt` | An arrow strikes earth or grass: a dull dead thud. | one-shot, positional, 0.3-0.4 s | 5 (rendered) | -10 dB | Push a stick sharply into soft earth or a bale of straw. |
| **Arrow into wood**<br>`snd_impact_arrow_wood` | An arrow sticks in a tree, a door or a beam. | one-shot, positional, 0.4-0.5 s | 4 (rendered) | -6 dB | Throw a dart into a plank; a knife into a log. |
| **Arrow off stone**<br>`snd_impact_arrow_stone` | An arrow glances off a wall, rock or merlon. | one-shot, positional, 0.4-0.4 s | 4 (rendered) | -7 dB | Flick a nail or a pencil against a stone step; a short tick and skitter. |
| **Arrow into a shield**<br>`snd_impact_arrow_shield` | A shield catches an arrow: a splintering wooden crack. | one-shot, positional, 0.5-0.5 s | 4 (rendered) | -5 dB | Snap a thin batten, or hit a plywood sheet with a dart. |
| **Arrow off armour**<br>`snd_impact_arrow_armor` | An arrow strikes mail or plate: a metallic ring. | one-shot, positional, 0.6-0.6 s | 4 (rendered) | -6 dB | Flick a nail at a steel pan or a shovel blade. |
| **Arrow into flesh**<br>`snd_impact_arrow_flesh` | An arrow hits a man: a wet thwack. | one-shot, positional, 0.4-0.5 s | 5 (rendered) | -4 dB | Slap a wet towel; a dart into a raw steak. |
| **Arrow into water**<br>`snd_impact_arrow_water` | An arrow falls in a river or lake. | one-shot, positional, 0.6-0.6 s | 3 (rendered) | -10 dB | Drop a pencil into a bath from a height. |
| **Stone lands**<br>`snd_impact_stone_smash` | A catapult stone smashes down: crash, crunch and scattering grit. | one-shot, positional, carries far, 2.2-2.3 s | 3 (rendered) | +0 dB | Drop a paving slab onto gravel; a rock onto a pile of stones. |
| **Gate struck**<br>`snd_impact_gate_wood` | A blow or blast hits a castle gate: a deep wooden boom. | one-shot, positional, carries far, 1.5-1.7 s | 4 (rendered) | -2 dB | A sledgehammer on a thick wooden door; record from inside the room too. |
| **Gate breaks**<br>`snd_impact_gate_break` | A gate gives way: splintering timbers and a crash. | one-shot, positional, carries far, 3.4-3.6 s | 3 (rendered) | +0 dB | Break a pallet or old crates with a sledgehammer; layer a heavy fall. |

## Explosions

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Satchel charge**<br>`snd_expl_satchel` | A satchel charge explodes: huge sub-bass blast with a long tail. | one-shot, positional, carries far, 5.4-5.5 s | 4 (rendered) | +0 dB | Not recordable at home: use a CC0 explosion library, or a balloon burst close up pitched down an octave and layered with a sub sine drop. |
| **Fireball bursts**<br>`snd_expl_fireball` | A fireball (or a dwarf's cocktail) lands: glass shatter crossfaded into a roaring, crackling fire whoosh. | one-shot, positional, carries far, 4.2-4.2 s | 4 (rendered) | -1 dB | A bottle broken in a bin (wear goggles) layered with a gas burner whoosh and crackling kindling. |

## Gore

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Dismember snap**<br>`snd_gore_dismember_snap` | A cut takes off a limb or head: a sharp organic crunch, a tendon snapping. | one-shot, positional, 0.6-0.7 s | 4 (rendered) | -3 dB | Snap a stick of celery or a carrot wrapped in a wet cloth; twist a raw chicken wing apart. |
| **Gib burst**<br>`snd_gore_gib_burst` | A body is torn apart (a blast, or a blow far past death): a saturated, wet, low organic burst. | one-shot, positional, 1.3-1.4 s | 4 (rendered) | -1 dB | Drop a water-soaked sponge or a wet bag of jelly onto concrete; layer a stomped melon. |
| **Blood rain**<br>`snd_gore_blood_rain` | After a gib burst: drops and bits pattering down all round. | one-shot, positional, 1.7-1.7 s | 3 (rendered) | -8 dB | Flick water from a brush onto paper and leaves; drop wet pasta onto a board. |

## Deaths

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Body falls**<br>`snd_gore_body_fall` | A body hits the ground (after the death cry). | one-shot, positional, 0.8-0.8 s | 5 (rendered) | -5 dB | Drop a sack of potatoes or a duffel bag of clothes onto earth. |
| **Armoured body falls**<br>`snd_gore_body_fall_armor` | A man in armour (or a great beast) hits the ground: thud and clatter. | one-shot, positional, 0.9-1.0 s | 4 (rendered) | -4 dB | Drop a duffel bag with a few pans and chains in it. |
| **Bones collapse**<br>`snd_gore_bones_collapse` | One of the walking dead falls apart: a dry clattering heap. | one-shot, positional, 1.1-1.4 s | 4 (rendered) | -5 dB | Drop a bundle of dry sticks and a few wooden spoons onto a floor. |
| **Burning body**<br>`snd_gore_flesh_burn` | A body killed by fire: sizzle and crackle as it burns. | one-shot, positional, 2.2-2.3 s | 3 (rendered) | -7 dB | Bacon in a hot pan, close; crackling twigs on a fire. |

## Notifications

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Victory**<br>`snd_state_victory_fanfare` | The battle is won: a short brass fanfare. | one-shot, 2D, 4.4-4.4 s | 1 (rendered) | -2 dB | A brass player, or CC0: a short major fanfare, 3-4 s. |
| **Defeat**<br>`snd_state_defeat_drone` | The battle is lost: a low minor-chord string drone with sub-bass. | one-shot, 2D, 8.6-8.6 s | 1 (rendered) | -3 dB | Cello or synth strings holding a low minor chord, 5-6 s, fading. |

## UI

| Sound | When it plays | How | Variants | Level | How to record or source it |
|---|---|---|---:|---:|---|
| **Button**<br>`snd_ui_click` | Any button tapped. (played by the page) | one-shot, 2D, quiet, 0.1-0.1 s | 3 (rendered) | -16 dB | A pen click or a wooden bead tapped on a table. |
| **Page turn**<br>`snd_ui_page_turn` | A panel opens (settings, news, a town, a briefing). (played by the page) | one-shot, 2D, quiet, 0.5-0.6 s | 3 (rendered) | -12 dB | Turn a stiff page of an old book, close mic. |

