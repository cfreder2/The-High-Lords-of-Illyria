"""The sprite atlas layout and the art delivery conventions, in one place.

docs/SPRITE_SPEC.md describes these for the artist; game/render/sprite_layout.h
holds the same numbers for the renderer (tests/test_sprite_layout.cpp checks
the two agree through the manifest). Change one, change all three.
"""

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


def gib_slot(part, variant):
    return VIEWS * FRAMES_PER_VIEW + GIB_PARTS.index(part) * GIB_VARIANTS + variant


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
            'units': units, 'cells': cells}
