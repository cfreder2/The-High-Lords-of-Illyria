"""The sprite atlas layout and the art delivery conventions, in one place.

docs/SPRITE_SPEC.md describes these for the artist; game/render/sprite_layout.h
holds the same numbers for the renderer (tests/test_sprite_layout.cpp checks
the two agree through the manifest). Change one, change all three.
"""

import json
import math
import re

# Views drawn: 0 front, 1 front-right (45 deg), 2 right side, 3 back-right, 4 back.
# Views 5-7 are 3, 2, 1 mirrored by the renderer and are never drawn.
VIEWS = 5

# Clips in atlas order: (name, frames, required, loop). The first five are the
# old 26-frame layout (die_back is the old "die"); a sheet with 26 frames a view
# is read as that layout and the new deaths fall back to die_back.
CLIPS = (
    ('idle', 4, True, True),
    ('walk', 8, True, True),
    ('attack', 6, True, False),
    ('flinch', 2, True, False),
    ('die_back', 6, True, False),
    ('die_front', 6, True, False),
    ('die_crumple', 6, True, False),
    ('die_burn', 6, False, False),
)
FIRST = {}
_n = 0
for _name, _frames, _req, _loop in CLIPS:
    FIRST[_name] = _n
    _n += _frames
FRAMES_PER_VIEW = _n  # 44
LEGACY_FRAMES_PER_VIEW = 26
FRAMES = {c[0]: c[1] for c in CLIPS}
# A clip not delivered (and not in the old sheet) borrows another's cells.
FALLBACK = {'die_front': 'die_back', 'die_crumple': 'die_back', 'die_burn': 'die_crumple'}

# Gib parts: separate small pictures for the gore system (severed limbs,
# bodies blown apart), each in two looks (a and b) the renderer spins in the
# picture's plane. Slots in atlas order after the views' cells.
GIB_PARTS = ('head', 'torso', 'arm', 'leg', 'chunk', 'weapon', 'shield', 'mount_head', 'mount_leg', 'wing')
GIB_VARIANTS = 2
GIB_CELLS = len(GIB_PARTS) * GIB_VARIANTS  # 20
CELLS = VIEWS * FRAMES_PER_VIEW + GIB_CELLS  # 240
LEGACY_CELLS = VIEWS * LEGACY_FRAMES_PER_VIEW  # 130

# Canvas classes for the delivered pictures: size, pixels a metre, and the
# anchor (the point between the feet on the ground) in pixels from the top left.
CLASSES = {
    'man': {'canvas': [1024, 1024], 'ppm': 192, 'anchor': [512, 560]},
    'large': {'canvas': [1536, 1024], 'ppm': 128, 'anchor': [768, 560]},
    'huge': {'canvas': [1536, 1024], 'ppm': 80, 'anchor': [768, 600]},
}
GIB_CANVAS = [512, 512]  # at the unit's class ppm, the part centred

# Team colour key: paint everything in the side's colour (tabard, shield field,
# plume, banner, caparison) in this hue; the importer turns it into the tint mask.
TEAM_KEY = '#0050FF'
TEAM_KEY_HUE = 221.0  # degrees
TEAM_KEY_HUE_TOLERANCE = 18.0
TEAM_KEY_MIN_SATURATION = 0.55
CHROMA_KEY = '#00FF00'  # background, when the tool cannot write alpha

# Camera and light, as tools/blender/sprites/render.py renders them.
ELEVATION_DEG = 30.0  # figures::kElevation = 0.52 rad
GAME_PPM = 48.0       # the normal sheet's pixels a metre (less for the biggest types)
HD_SCALE = 2.0        # the close-up sheet: twice the normal sheet's pixels a metre

# The unit types: sheet name (unit_type_id) -> number, class, height (m),
# attack seconds (figures::attackSeconds), flier, ranged, gib parts, look.
_MAN = ('head', 'torso', 'arm', 'leg', 'chunk', 'weapon')
_RIDER = _MAN + ('mount_head', 'mount_leg')
UNITS = {
    'light_infantry': (0, 'man', 1.80, 0.78, False, False, _MAN + ('shield',), 'lightly armoured spearman, round shield, kettle helm'),
    'heavy_infantry': (1, 'man', 1.90, 0.90, False, False, _MAN + ('shield',), 'mail and plate swordsman, great helm with plume, kite shield with a cross'),
    'archers': (2, 'man', 1.80, 0.90, False, True, _MAN, 'elven longbowman, hood, quiver'),
    'pikemen': (3, 'large', 1.80, 0.90, False, False, _MAN, 'pikeman with a 5 m pike, open helm, tabard'),
    'light_cavalry': (4, 'large', 2.50, 0.72, False, False, _RIDER, 'light horseman with lance on a brown horse'),
    'heavy_cavalry': (5, 'large', 2.70, 0.90, False, False, _RIDER, 'armoured knight with couched lance on a barded horse'),
    'dwarves': (6, 'man', 1.35, 0.90, False, False, _MAN + ('shield',), 'stocky dwarf axeman, round shield, long beard'),
    'wolf_riders': (7, 'large', 1.90, 0.66, False, False, _RIDER, 'goblin rider with spear on a grey wolf'),
    'giants': (8, 'huge', 5.50, 0.90, False, False, _MAN, 'hill giant, fur and kilt, huge wooden club'),
    'minotaurs': (9, 'large', 3.00, 0.90, False, False, _MAN, 'bull-headed minotaur with a halberd, kilt'),
    'spiders': (10, 'large', 1.30, 0.60, False, False, ('head', 'torso', 'leg', 'chunk'), 'giant black spider, team-coloured mark on its back'),
    'griffins': (11, 'large', 2.20, 0.72, True, False, ('head', 'torso', 'leg', 'chunk', 'wing'), 'griffin, eagle front and lion back, team-coloured harness'),
    'pegasi': (12, 'large', 2.30, 0.72, True, False, _RIDER + ('wing',), 'lancer riding a white winged horse'),
    'elephants': (13, 'huge', 4.00, 0.90, False, False, _RIDER, 'war elephant with a howdah, caparison and a rider'),
    'unicorns': (14, 'large', 2.20, 0.78, False, False, ('mount_head', 'mount_leg', 'torso', 'chunk'), 'white unicorn with a team-coloured caparison'),
    'wizards': (15, 'man', 1.90, 0.80, False, True, _MAN, 'robed wizard with a pointed hat and a staff'),
    'dragons': (16, 'huge', 4.50, 0.80, True, True, ('head', 'torso', 'leg', 'chunk', 'wing'), 'green dragon, team-coloured saddle cloth'),
    'devils': (17, 'huge', 3.00, 0.90, False, False, _MAN, 'red horned devil with a spear'),
    'demons': (18, 'huge', 3.60, 0.90, False, False, _MAN + ('wing',), 'purple bat-winged demon'),
    'undead': (19, 'man', 1.80, 0.84, False, False, _MAN + ('shield',), 'skeleton warrior, rusty sword, round shield'),
    'archons': (20, 'large', 2.40, 0.72, True, False, _MAN + ('wing',), 'tall robed archon with white feathered wings'),
    'hero': (21, 'man', 2.00, 0.66, False, False, _MAN + ('shield',), 'champion knight with cloak, plumed helm, heater shield'),
    'dwarf_grenadier': (22, 'man', 1.30, 0.80, False, True, _MAN, 'dwarf throwing a fused satchel bomb'),
    'journeyman': (23, 'man', 1.80, 0.72, False, False, _MAN, 'hooded healer in long robes with a staff'),
    'warrior': (24, 'man', 1.90, 0.72, False, False, _MAN + ('shield',), 'Myth-style warrior, sword and heater shield, plume'),
    'berserker': (25, 'man', 1.90, 0.56, False, False, _MAN, 'bare-chested berserker with a claymore, kilt'),
    'catapult': (26, 'large', 2.50, 1.00, False, True, ('chunk', 'weapon'), 'wooden siege catapult on four wheels, team-coloured pennant'),
    'hyena': (27, 'man', 1.55, 0.60, False, False, _MAN, 'hunched hyena-folk scout, ragged blue kilt, long scimitar'),
}

# The attack's telling frame (0-based within the 6): the blow lands (melee) or
# the missile leaves (ranged). The renderer starts the clip when the sim strikes.
IMPACT_FRAME_MELEE = 3
RELEASE_FRAME_RANGED = 4


def clip_of(frame):
    """(clip, index in clip) of a frame number within a view."""
    for name, frames, _, _ in CLIPS:
        if FIRST[name] <= frame < FIRST[name] + frames:
            return name, frame - FIRST[name]
    raise ValueError(frame)


def cell_file(clip, view, frame):
    return '%s/v%d_f%02d.png' % (clip, view, frame)


def gib_file(part, variant):
    return 'gibs/%s_%s.png' % (part, 'ab'[variant])


def gib_slot(part, variant, frames_per_view=FRAMES_PER_VIEW):
    return VIEWS * frames_per_view + GIB_PARTS.index(part) * GIB_VARIANTS + variant


# ---------------------------------------------------------------- a unit's own clips
#
# A unit delivered with ART_DIR/<unit>/clips.json lists its own clips, in
# atlas order, in place of CLIPS (3D renders: more and other clips than the
# fixed layout's). Its cells are named as ever (<clip>/v<view>_f<NN>.png),
# laid out view by view in that order, then the gib parts. The sheet JSON
# lists them as "clips" (listed_clips), and the renderer plays them by name
# (game/render/sprite_clips.h), a clip a sheet lacks by LISTED_FALLBACK.
CLIP_NAME_RE = re.compile(r'^[a-z][a-z_]*$')
LISTED_REQUIRED = ('idle', 'walk', 'attack', 'flinch', 'die_back')
LISTED_MAX_FRAMES = 100  # two digits in the file names
LISTED_KEYS = ('name', 'frames', 'seconds', 'loop', 'stride', 'release', 'times')
LISTED_FALLBACK = {'run': 'walk', 'celebrate': 'idle', 'die_front': 'die_back', 'die_crumple': 'die_back',
                   'die_burn': 'die_crumple', 'pickup': 'attack', 'throw': 'attack', 'slash': 'attack'}
# The format by example (the archers' list as rendered): stride, metres of
# ground one loop of a moving clip covers; release, the second into the clip
# the arrow (or the thrown thing) leaves; times, when each frame shows, from
# the clip's start (default: evenly over seconds).
EXAMPLE_CLIPS = [
    {'name': 'idle', 'frames': 8, 'seconds': 2.6, 'loop': True},
    {'name': 'walk', 'frames': 8, 'loop': True, 'stride': 1.40},
    {'name': 'run', 'frames': 8, 'loop': True, 'stride': 2.74},
    {'name': 'attack', 'frames': 12, 'seconds': 2.40, 'release': 1.45,
     'times': [0.0, 0.25, 0.5, 0.75, 1.0, 1.15, 1.3, 1.45, 1.6, 1.8, 2.0, 2.2]},
    {'name': 'flinch', 'frames': 4, 'seconds': 0.60},
    {'name': 'die_back', 'frames': 8, 'seconds': 1.50},
    {'name': 'die_front', 'frames': 8, 'seconds': 1.40},
    {'name': 'die_crumple', 'frames': 8, 'seconds': 1.50},
    {'name': 'die_burn', 'frames': 8, 'seconds': 2.20},
    {'name': 'celebrate', 'frames': 16, 'seconds': 2.70, 'loop': True},
]


def check_clips(clips):
    """A clip list (clips.json's "clips") checked; raises ValueError saying what is wrong."""
    if not isinstance(clips, list) or not clips:
        raise ValueError('"clips" must be a list of clips')
    seen = set()
    for k, c in enumerate(clips):
        if not isinstance(c, dict):
            raise ValueError('clip %d is not an object' % k)
        name = c.get('name')
        what = '%s' % (name if isinstance(name, str) else 'clip %d' % k)
        if not isinstance(name, str) or not CLIP_NAME_RE.match(name) or name == 'gibs':
            raise ValueError('%s: a name of lower-case letters and _ (not "gibs")' % what)
        if name in seen:
            raise ValueError('%s: listed twice' % what)
        seen.add(name)
        extra = sorted(set(c) - set(LISTED_KEYS))
        if extra:
            raise ValueError('%s: unknown keys %s' % (what, ', '.join(extra)))
        n = c.get('frames')
        if not isinstance(n, int) or isinstance(n, bool) or not 1 <= n <= LISTED_MAX_FRAMES:
            raise ValueError('%s: frames must be 1 to %d' % (what, LISTED_MAX_FRAMES))

        def number(key, positive):
            v = c.get(key)
            if v is None:
                return None
            if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 or (positive and v == 0):
                raise ValueError('%s: %s must be a number%s' % (what, key, ' above 0' if positive else ' of 0 or more'))
            return float(v)
        seconds = number('seconds', True)
        number('stride', True)
        release = number('release', False)
        if 'loop' in c and not isinstance(c['loop'], bool):
            raise ValueError('%s: loop must be true or false' % what)
        if release is not None and seconds is not None and release > seconds:
            raise ValueError('%s: release %.2f s is past its %.2f s' % (what, release, seconds))
        times = c.get('times')
        if times is not None:
            if seconds is None:
                raise ValueError('%s: times need seconds' % what)
            if (not isinstance(times, list) or len(times) != n or
                    any(isinstance(t, bool) or not isinstance(t, (int, float)) or not math.isfinite(t) for t in times)):
                raise ValueError('%s: times must be %d numbers, one a frame' % (what, n))
            if times[0] < 0 or any(b < a for a, b in zip(times, times[1:])) or times[-1] >= seconds:
                raise ValueError('%s: times must rise from 0 and end before %.2f s' % (what, seconds))
    missing = [r for r in LISTED_REQUIRED if r not in seen]
    if missing:
        raise ValueError('missing the clips %s' % ', '.join(missing))
    return clips


def read_clips(path):
    """A unit's clips.json ({"clips": [...]}), checked (check_clips)."""
    with open(path) as fh:
        try:
            doc = json.load(fh)
        except ValueError as e:
            raise ValueError('not JSON: %s' % e)
    if not isinstance(doc, dict) or 'clips' not in doc:
        raise ValueError('no "clips"')
    return check_clips(doc['clips'])


def listed_clips(clips):
    """The clips as a sheet JSON lists them ("clips": each with its first
    frame within a view, and loop always), and the frames a view."""
    out, first = [], 0
    for c in clips:
        e = {'name': c['name'], 'first': first, 'frames': c['frames']}
        for k in ('seconds', 'loop', 'stride', 'release', 'times'):
            if k in c:
                e[k] = c[k]
        e['loop'] = bool(c.get('loop', False))
        out.append(e)
        first += c['frames']
    return out, first


def clip_line(c):
    """A listed clip as the renderer takes it: "name first frames seconds loop
    stride release [times...]" (game/web/sheets.cpp, sprite_clips.h parseClips)."""
    words = [c['name'], c['first'], c['frames'], c.get('seconds', 0), 1 if c.get('loop') else 0, c.get('stride', 0),
             c.get('release', -1)] + list(c.get('times', []))
    return ' '.join(w if isinstance(w, str) else repr(w) if isinstance(w, float) else str(w) for w in words)


def unit_cells(unit, listed):
    """Every picture a unit delivered with its own clips takes (as manifest()'s
    cells, all of each clip required), with its slot in that sheet."""
    number, klass, height, attack, flier, ranged, gibs, look = UNITS[unit]
    c = CLASSES[klass]
    fpv = sum(k['frames'] for k in listed)
    cells = []
    for k in listed:
        for v in range(VIEWS):
            for f in range(k['frames']):
                cells.append({'file': '%s/%s' % (unit, cell_file(k['name'], v, f)), 'unit': unit, 'anim': k['name'], 'view': v, 'frame': f,
                              'slot': v * fpv + k['first'] + f, 'canvas': c['canvas'], 'ppm': c['ppm'],
                              'anchor': c['anchor'], 'required': True})
    for part in gibs:
        for k in range(GIB_VARIANTS):
            cells.append({'file': '%s/%s' % (unit, gib_file(part, k)), 'unit': unit, 'anim': 'gib', 'part': part, 'variant': 'ab'[k],
                          'slot': gib_slot(part, k, fpv), 'canvas': GIB_CANVAS, 'ppm': c['ppm'],
                          'anchor': [GIB_CANVAS[0] // 2, GIB_CANVAS[1] // 2], 'required': False})
    return cells


def unit_class(unit):
    return CLASSES[UNITS[unit][1]]


def manifest():
    """Every picture the art pipeline can take, for the artist's script and the importer."""
    units, cells = {}, []
    for unit, (number, klass, height, attack, flier, ranged, gibs, look) in sorted(UNITS.items(), key=lambda kv: kv[1][0]):
        c = CLASSES[klass]
        units[unit] = {'type': number, 'class': klass, 'canvas': c['canvas'], 'ppm': c['ppm'], 'anchor': c['anchor'],
                       'height_m': height, 'flier': flier, 'ranged': ranged, 'attack_seconds': attack, 'look': look,
                       'gib_parts': list(gibs)}
        for name, frames, required, loop in CLIPS:
            for v in range(VIEWS):
                for f in range(frames):
                    cells.append({'file': '%s/%s' % (unit, cell_file(name, v, f)), 'unit': unit, 'anim': name, 'view': v, 'frame': f,
                                  'slot': v * FRAMES_PER_VIEW + FIRST[name] + f, 'canvas': c['canvas'], 'ppm': c['ppm'],
                                  'anchor': c['anchor'], 'required': required})
        for part in gibs:
            for k in range(GIB_VARIANTS):
                cells.append({'file': '%s/%s' % (unit, gib_file(part, k)), 'unit': unit, 'anim': 'gib', 'part': part, 'variant': 'ab'[k],
                              'slot': gib_slot(part, k), 'canvas': GIB_CANVAS, 'ppm': c['ppm'],
                              'anchor': [GIB_CANVAS[0] // 2, GIB_CANVAS[1] // 2], 'required': False})
    clips = [{'name': n, 'frames': f, 'first': FIRST[n], 'required': r, 'loop': l, 'fallback': FALLBACK.get(n)} for n, f, r, l in CLIPS]
    return {'version': 2, 'views': VIEWS, 'frames_per_view': FRAMES_PER_VIEW, 'cells_per_sheet': CELLS, 'clips': clips,
            'gib_parts': list(GIB_PARTS), 'gib_variants': GIB_VARIANTS, 'classes': CLASSES, 'gib_canvas': GIB_CANVAS,
            'team_key': TEAM_KEY, 'chroma_key': CHROMA_KEY, 'elevation_deg': ELEVATION_DEG,
            'impact_frame_melee': IMPACT_FRAME_MELEE, 'release_frame_ranged': RELEASE_FRAME_RANGED,
            'clip_list': {'file': '<unit>/clips.json', 'keys': list(LISTED_KEYS), 'required': list(LISTED_REQUIRED),
                          'fallback': LISTED_FALLBACK, 'example': {'clips': EXAMPLE_CLIPS}},
            'units': units, 'cells': cells}
