#!/usr/bin/env python3
"""Reference images for Tripo (tripo3d.ai image-to-3D): front, side and back of a unit, plus its held items.

    python3 tools/sprites/refs.py draw UNIT... [--only front,side,back,ITEM] [--force] [-j 4]
    python3 tools/sprites/refs.py contact [--out DIR]      # contact sheet per class and an overall one
    python3 tools/sprites/refs.py list

Matches art/refs/archers and art/refs/hyena: 1024x1536 (or 1536x1024 for a long beast's side view),
a plain light grey background, a neutral standing pose, the weapon NOT in the figure's hands
(it is its own picture, like bow.png). The front view is drawn from the text; side, back and items
are drawn with the front image as Image 1 so the character stays the same. Output goes to
art/refs/UNIT/UNIT_{front,side,back}.png and UNIT/ITEM.png, and is mirrored to --mirror
(default ~/Desktop/tripo-refs). Cells that exist are skipped, so a run can be stopped and resumed.
Several `codex exec` calls run at once; each plate is found by its session id.

The art is generated, not committed (art/ is gitignored).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
CODEX_IMAGES = os.path.expanduser('~/.codex/generated_images')
MIRROR = os.path.expanduser('~/Desktop/tripo-refs')
TEAM = '#0050FF'

BG = 'a perfectly plain, flat, light grey background (about #C8C8C8) filling the whole picture, nothing else in it'

# kind: man (humanoid: A-pose), beast (quadruped or creature: natural standing), mount (rider on a mount, one subject),
# machine. wide: the side view is landscape. Each item is (file, description), drawn alone.
UNITS = {
    'light_infantry': dict(cls='man', kind='man', look=(
        'a Light Infantry soldier of the realm: a lightly armoured spearman in a padded leather jerkin with a little mail at the '
        'shoulders, an open-faced kettle helm, a team-blue tabard over it, a leather belt, plain trousers and leather boots. '
        'Ordinary, lean, a bit weathered.'),
        items=[('spear', 'a plain 2 metre wooden spear with a leaf-shaped iron head, shown vertical, whole from butt to point'),
               ('shield', 'a round wooden shield with an iron boss and a rim, its field painted saturated blue #0050FF, seen face-on')]),
    'pikemen': dict(cls='large', kind='man', look=(
        'a Pikeman: a foot soldier in an open helm (no visor), a padded gambeson and a short mail collar, a team-blue tabard, '
        'leather gloves, a belt with a short sword in a scabbard, and leather boots. Solid and steady.'),
        items=[('pike', 'a very long 5 metre pike: a slim ash shaft with a small steel head and langets, shown vertical and whole, '
                        'shrunk to fit the picture tall and thin')]),
    'light_cavalry': dict(cls='large', kind='mount', wide=True, look=(
        'a Light Cavalry horseman: a rider in leather armour and an open helm with a team-blue tabard and a sabre on his belt, '
        'on a fast, lean brown horse (bay, dark mane and tail) with a plain saddle and bridle and a team-blue saddle cloth. '
        'Rider and horse are ONE subject, the rider seated upright in the saddle, his right hand resting on his thigh, '
        'his left holding the reins low.'),
        items=[('lance', 'a light cavalry lance: a slim wooden shaft about 3 metres long with a small steel head and a hand guard, '
                         'with a small blue #0050FF pennon below the head, shown vertical and whole')]),
    'heavy_cavalry': dict(cls='large', kind='mount', wide=True, look=(
        'a Heavy Cavalry knight: a rider in full plate armour with a closed great helm and a team-blue plume, a team-blue surcoat, '
        'on a big armoured warhorse (grey) barded in plate on the head (chanfron), neck and chest, with a team-blue caparison '
        'cloth hanging to its knees. Rider and horse are ONE subject, the knight seated upright, his right hand resting on his thigh, '
        'his left holding the reins.'),
        items=[('lance', 'a heavy jousting lance: a long tapered wooden shaft about 4 metres, a steel head and a round vamplate hand guard, '
                         'with a blue #0050FF pennon, shown vertical and whole'),
               ('shield', 'a steel-rimmed heater shield with a plain cross, its field saturated blue #0050FF, seen face-on')]),
    'dwarves': dict(cls='man', kind='man', look=(
        'a Dwarf axeman of the hills: stocky and short, broad, about 1.35 metres tall, a long braided beard, a steel helm with a nose guard, '
        'a mail shirt under a team-blue tabard, thick belted arms, heavy boots. Proportions: big head, wide shoulders, short legs.'),
        items=[('axe', 'a dwarven battle axe: a short stout haft about 80 cm, a broad double-bitted steel head with an engraved edge, '
                       'shown vertical and whole'),
               ('shield', 'a round steel-banded wooden shield with a central iron boss, its field painted saturated blue #0050FF, seen face-on')]),
    'wolf_riders': dict(cls='large', kind='mount', wide=True, look=(
        'a Wolf Rider: a small wiry goblin (green-grey skin, big pointed ears, a sharp-toothed grin) in scrappy leather armour and a '
        'rag-trimmed team-blue tabard, riding a great grey wolf, as big as a pony, with a rope-and-leather bridle and a team-blue blanket '
        'for a saddle. Rider and wolf are ONE subject: the goblin seated low on the wolf\'s shoulders, his right hand open at his side, '
        'his left holding the bridle.'),
        items=[('spear', 'a goblin spear: a rough wooden shaft about 1.6 metres with a crude iron head tied on with sinew and a few feathers, '
                         'shown vertical and whole')]),
    'giants': dict(cls='huge', kind='man', look=(
        'a Hill Giant of the northern heights, about 5.5 metres tall (shown as a normal standing figure): a huge, broad, shaggy, '
        'weathered man with a heavy brow, a rough beard and matted hair, bare chested with a fur pelt over one shoulder, '
        'a team-blue kilt of coarse cloth with a rope belt, leather wraps on the wrists and bare feet. Big hands.'),
        items=[('club', 'a huge wooden club: a roughly trimmed tree trunk, thick at the head with knots and a few iron studs, '
                        'a rope-wrapped grip, shown vertical and whole')]),
    'minotaurs': dict(cls='large', kind='man', look=(
        'a Minotaur, about 3 metres tall: a muscular man\'s body with the head of a bull, short curved horns, a ringed nose, '
        'dark brown short fur on the head and shoulders and bare muscular torso and arms, a team-blue kilt of cloth with a leather belt, '
        'leather bracers, hooves for feet. Powerful and heavy.'),
        items=[('halberd', 'a minotaur\'s great halberd: a thick wooden shaft about 2.5 metres with a heavy steel axe blade, a back spike '
                           'and a top spike, shown vertical and whole')]),
    'spiders': dict(cls='large', kind='beast', wide=True, look=(
        'a Giant Spider the size of a pony: a black, hairy spider with eight jointed legs, a big round abdomen, two large curved '
        'fangs and a cluster of eyes, glossy black with dark brown hair. A saturated blue #0050FF mark painted/marked on top of the '
        'abdomen, and nowhere else blue. All eight legs visible, standing on the ground in a natural spread.'),
        items=[]),
    'griffins': dict(cls='large', kind='beast', wide=True, look=(
        'a Griffin: the head, forelegs with talons and the feathered wings of a golden-brown eagle and the hind body and tail of a lion '
        '(tawny fur, tufted tail), standing on four legs, a sharp hooked beak. Wings half open and raised, symmetrical. '
        'A leather harness across the chest and back, with a team-blue saddle cloth and blue straps.'),
        items=[]),
    'pegasi': dict(cls='large', kind='mount', wide=True, look=(
        'a Pegasus rider: a lancer in a light steel helm, a mail shirt and a team-blue tabard, on a white winged horse (white feathered '
        'wings half open and raised, symmetrical), with a bridle, a saddle and a team-blue saddle cloth. Rider and pegasus are ONE subject, '
        'the lancer seated upright, his right hand resting on his thigh, his left holding the reins.'),
        items=[('lance', 'a cavalry lance: a slim wooden shaft about 3 metres long with a steel head and a hand guard, with a blue #0050FF '
                         'pennon, shown vertical and whole')]),
    'elephants': dict(cls='huge', kind='mount', wide=True, look=(
        'a War Elephant of the Valdor plains, about 4 metres tall: a great grey elephant with tusks (the tusks capped with steel bands), '
        'a team-blue caparison cloth over its back and flanks with a fringe, and on its back a wooden howdah (a tower-like box with a '
        'team-blue canopy and railings) carrying two crewmen in helms and mail with team-blue tabards, standing, hands resting on the '
        'rail. Elephant, howdah and crew are ONE subject.'),
        items=[]),
    'unicorns': dict(cls='large', kind='beast', wide=True, look=(
        'a Unicorn: a slim, fierce white horse with a single long spiralled ivory horn, a flowing white mane and tail, with a team-blue '
        'caparison cloth over its back and a plain bridle. No rider.'),
        items=[]),
    'wizards': dict(cls='man', kind='man', look=(
        'a Wizard of the old learning: a tall man with a long grey beard in long layered robes of deep grey, a pointed hat with a '
        'wide brim, a team-blue sash and a team-blue mantle at the shoulders, a belt with pouches, a few bracelets. Gaunt, wise.'),
        items=[('staff', 'a wizard\'s staff: a tall twisted wooden staff about 2 metres with a knotted crook head holding a small glowing '
                         'crystal, shown vertical and whole')]),
    'dragons': dict(cls='huge', kind='beast', wide=True, look=(
        'a Dragon, a green ancient wyrm: a long-necked, long-tailed green dragon with scaled skin, a horned head, a toothed jaw, four '
        'legs with claws, a ridge of spines along its back, and two big leathery bat wings (half open and raised, symmetrical). A team-blue '
        'saddle cloth lies over its shoulders, held with a strap, and no other blue anywhere. Fearsome but standing calmly.'),
        items=[]),
    'devils': dict(cls='huge', kind='man', look=(
        'a Devil, about 3 metres tall: a hulking, broad, muscular red-skinned fiend with curled black horns, a heavy brow, small tusks, a '
        'tail with a spade tip, clawed hands and cloven hooves, wearing only a team-blue loincloth with a belt of old chain and '
        'iron wrist cuffs.'),
        items=[('spear', 'a devil\'s spear: a thick black iron-shod shaft about 3 metres with a wicked barbed steel head, '
                         'shown vertical and whole')]),
    'demons': dict(cls='huge', kind='man', look=(
        'a Demon, bigger and worse than a devil, about 3.6 metres tall: a massive purple-skinned fiend with huge swept-back horns, '
        'glowing eyes, a fanged mouth, a muscular body, clawed hands, cloven hooves, a barbed tail and two big bat wings (half open, '
        'raised, symmetrical), wearing only a team-blue sash and loincloth and iron cuffs.'),
        items=[]),
    'undead': dict(cls='man', kind='man', look=(
        'an Undead warrior of the barrows: a walking skeleton, bleached yellowed bones with a little rotting leather and hanging rags, '
        'a rusted open helm, a few rusted scraps of mail, a ragged team-blue tabard in tatters over the ribs, and empty eye sockets '
        'with a faint pale glow. No flesh.'),
        items=[('sword', 'a rusty, notched, pitted iron arming sword with a worn leather grip, shown vertical and whole'),
               ('shield', 'a battered round wooden shield with an iron rim, cracked, with remnants of a blue #0050FF painted field, seen face-on')]),
    'archons': dict(cls='large', kind='man', look=(
        'an Archon, a winged warrior of the heavens, about 2.4 metres tall: a tall, serene, strong figure in long white robes with '
        'a gold-trimmed white breastplate, a team-blue sash and a team-blue stole, a plain golden circlet, large white feathered wings '
        '(half open, raised, symmetrical), bare forearms and sandals. Noble, calm.'),
        items=[('sword', 'an archon\'s sword: a long straight silver blade with a gold cross guard and white grip, shown vertical and whole')]),
    'hero': dict(cls='man', kind='man', look=(
        'a Hero, a champion knight: a tall man (2 metres) in a gleaming plate and mail harness with a team-blue surcoat bearing a plain '
        'cross, a long team-blue cloak fastened at the shoulders, a plumed open-faced great helm with a tall team-blue plume, gauntlets '
        'and fine boots. A noble commander\'s bearing, bareheaded face visible under the raised visor.'),
        items=[('sword', 'a hero\'s longsword: a fine steel blade with a gold cross guard and a wrapped grip, shown vertical and whole'),
               ('shield', 'a steel-rimmed heater shield with a gold cross, its field saturated blue #0050FF, seen face-on')]),
    'dwarf_grenadier': dict(cls='man', kind='man', look=(
        'a Dwarven Grenadier, a demolitionist: stocky, short (1.3 metres), broad, with a thick braided beard and goggles pushed up on a '
        'leather cap with a steel crown, a leather apron over a team-blue tunic, a bandolier of five satchel charges across the chest '
        'and a belt of small glass bottles (Molotov oil bottles with rag wicks), thick gloves and boots.'),
        items=[('satchel', 'a demolition satchel prop for a fantasy strategy game: a small canvas bag cinched with rope and a leather strap, with a short length of cord sticking out of the top, shown from the front on its own')]),
    'journeyman': dict(cls='man', kind='man', look=(
        'a Journeyman, a healer of the Moonward Order: a hooded figure in long plain robes of pale grey-green wool with the hood up '
        'and a face visible, a team-blue sash and a team-blue scarf, a satchel and a belt with small vials, leather sandals. Gentle.'),
        items=[('staff', 'a healer\'s staff: a plain wooden pole about 1.8 metres with a small carved crescent moon at the top, shown vertical and whole')]),
    'warrior': dict(cls='man', kind='man', look=(
        'a Warrior of the orcs\' foot and the woodland militia: a sturdy fighter in a mail hauberk and a plain steel helm with a '
        'team-blue plume, a team-blue tabard, leather vambraces and greaves, a sword belt with an empty scabbard and heavy boots.'),
        items=[('sword', 'a plain steel arming sword with a cross guard and a leather grip, shown vertical and whole'),
               ('shield', 'a big steel-rimmed heater shield, its field saturated blue #0050FF, seen face-on')]),
    'berserker': dict(cls='man', kind='man', look=(
        'a Berserker: a wild, big, bare-chested warrior with wild long hair and a scarred, warpainted face, a team-blue tartan-style '
        'kilt with a leather belt, leather wrist wraps and bare legs with strapped boots, tattoos and scars. No armour and no shield.'),
        items=[('claymore', 'a two-handed claymore: a long straight blade with a down-sloped cross guard and a long wrapped grip, shown '
                            'vertical and whole')]),
    'catapult': dict(cls='large', kind='machine', wide=True, look=(
        'a wooden siege Catapult, a wheeled mangonel: a sturdy timber frame on four iron-shod wooden wheels, a throwing arm with a '
        'sling cup resting down against the frame, a twisted-rope torsion bundle, ratchet and windlass, and a team-blue pennant on a '
        'short pole at the rear. No crew, no stone in the cup.'),
        items=[]),
}

CLASSES = ('man', 'large', 'huge')

VIEWS = {
    'front': 'a FRONT view: the camera is dead ahead at the subject\'s mid-height, the subject faces the camera squarely, perfectly symmetrical',
    'side': 'a SIDE view in profile: the subject faces the LEFT of the picture, the camera exactly at its side at its mid-height',
    'back': 'a BACK view: the camera is directly behind the subject at its mid-height; we see the subject\'s back, it faces away from the camera',
}

POSE = {
    'man': ('a neutral A-pose for rigging: standing straight, feet about shoulder width apart and flat on the ground, the arms hanging '
            'straight but held clearly away from the body at about 30 degrees, hands open with the palms toward the thighs and fingers '
            'relaxed, head level, looking straight ahead, mouth closed. The hands are EMPTY.'),
    'beast': ('a neutral natural standing pose: all legs planted on the ground and clearly separated so every leg and the tail can be '
              'seen, head held level and forward, jaws closed, wings (if it has them) half open and raised symmetrically.'),
    'mount': ('a neutral standing pose: the mount stands square on all four legs, legs clearly separated, head level and forward, '
              'every leg visible; the rider sits upright and centred, hands empty of weapons (the left hand low on the reins, '
              'the right hand resting on the thigh), the rider\'s legs clear of the mount\'s side as far as possible.'),
    'machine': 'standing level on all four wheels, resting, nothing in motion.',
}


def prompt(unit, view=None, item=None, has_front=False):
    u = UNITS[unit]
    name = unit.replace('_', ' ')
    base = ('Fantasy medieval, realistic and richly detailed like classic strategy game unit art but rendered as a clean, textured, '
            'physically-plausible studio reference render for 3D modelling: crisp readable materials, even soft light from the front and '
            'above, NO cast shadow on the ground, no ground plane, no text, no border, nothing else in the picture. Orthographic-looking '
            '(no perspective distortion, no fisheye, no extreme foreshortening). The whole subject is fully inside the picture: nothing '
            'cropped, from the lowest foot, hoof or wheel to the highest hat, horn, plume, wing tip or pennant, with a small margin all round, '
            'and it fills most of the frame. Background: %s. Team colour: everything that carries his army\'s colour is a saturated pure blue '
            '%s, shaded naturally; there is NO other saturated blue anywhere (no blue eyes glow, no blue metal, no blue sky), and no '
            'text or emblems other than those described.' % (BG, TEAM))
    if item:
        d = dict(u['items'])[item]
        lines = ['Draw ONE object on its own, centred, as a reference picture for 3D modelling: %s. It is the %s of the character in '
                 'Image 1, who carries it. Match its materials, age and style to his gear. The object is alone: no hand, no body, no '
                 'figure, no stand. The picture is portrait, 2:3 (tall and narrow), the object upright filling the height.' % (d, item.replace('_', ' ')), base]
        return '\n\n'.join(lines)
    wide = u.get('wide') and view == 'side'
    lines = ['Draw ONE subject alone: %s. Seen as %s. The pose: %s' % (u['look'], VIEWS[view], POSE[u['kind']]), base,
             'The picture is %s.' % ('LANDSCAPE, 3:2 (wide)' if wide else 'PORTRAIT, 2:3 (tall)')]
    if has_front:
        lines.append('Image 1 is the FRONT view of this very same subject. Draw the same subject from the new angle: identical identity, '
                     'proportions, face, body, armour, clothing, colours, patterns, markings, wing and horn shapes, left and right '
                     'sides kept the same (what is on its left in Image 1 is still on its left; do not mirror it). Same pose, same '
                     'scale. Only the camera moves.')
    return '\n\n'.join(lines)


def codex(text, refs, timeout=1500):
    cmd = ['codex', 'exec', '--enable', 'image_generation', '--skip-git-repo-check']
    for r in refs:
        cmd += ['-i', os.path.abspath(r)]
    scratch = tempfile.mkdtemp(prefix='illyria-refs-')
    try:
        res = subprocess.run(cmd, cwd=scratch, input=text, capture_output=True, text=True, timeout=timeout)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    m = re.search(r'session id:\s*([0-9a-f-]+)', res.stderr + res.stdout)
    d = os.path.join(CODEX_IMAGES, m.group(1)) if m else None
    files = sorted((os.path.join(d, f) for f in os.listdir(d) if f.endswith('.png')), key=os.path.getmtime) if d and os.path.isdir(d) else []
    if not files:
        tail = (res.stderr or res.stdout or '').strip().splitlines()
        raise RuntimeError('codex wrote no image: %s' % (tail[-1] if tail else 'no output'))
    return files[-1]


calls = 0
lock = threading.Lock()


def make(unit, name, out_dir, mirror, view=None, item=None, force=False, front=None):
    path = os.path.join(out_dir, unit, '%s_%s.png' % (unit, name) if view else '%s.png' % name)
    if os.path.exists(path) and not force:
        return path
    global calls
    refs = [front] if (front and os.path.exists(front)) else []
    for attempt in range(2):
        try:
            plate = codex(prompt(unit, view, item, bool(refs) and view != 'front'), refs if (view != 'front') else [])
            break
        except Exception as e:  # noqa: BLE001
            print('  %s %s: %s' % (unit, name, e), flush=True)
            if attempt:
                return None
    with lock:
        calls += 1
    os.makedirs(os.path.dirname(path), exist_ok=True)
    shutil.copy(plate, path)
    if mirror:
        os.makedirs(os.path.join(mirror, unit), exist_ok=True)
        shutil.copy(plate, os.path.join(mirror, unit, os.path.basename(path)))
    print('  made %s' % os.path.relpath(path, ROOT), flush=True)
    return path


def draw_unit(unit, args, pool):
    out = args.out
    only = set(args.only.split(',')) if args.only else None
    want = lambda n: only is None or n in only  # noqa: E731
    front = make(unit, 'front', out, args.mirror, view='front', force=args.force and want('front')) if want('front') or not os.path.exists(
        os.path.join(out, unit, unit + '_front.png')) else os.path.join(out, unit, unit + '_front.png')
    if not front:
        return
    jobs = []
    for v in ('side', 'back'):
        if want(v):
            jobs.append(pool.submit(make, unit, v, out, args.mirror, v, None, args.force, front))
    for it, _ in UNITS[unit]['items']:
        if want(it):
            jobs.append(pool.submit(make, unit, it, out, args.mirror, None, it, args.force, front))
    for j in jobs:
        j.result()


def contact(args):
    from PIL import Image, ImageDraw
    cell = (320, 480)
    allrows = []
    for cls in CLASSES + ('all',):
        units = [u for u, d in UNITS.items() if cls == 'all' or d['cls'] == cls]
        rows = []
        for u in units:
            imgs = []
            for v in ('front', 'side', 'back'):
                p = os.path.join(args.out, u, '%s_%s.png' % (u, v))
                imgs.append(p if os.path.exists(p) else None)
            for it, _ in UNITS[u]['items']:
                p = os.path.join(args.out, u, it + '.png')
                imgs.append(p if os.path.exists(p) else None)
            rows.append((u, imgs))
        if cls == 'all':
            cell = (170, 255)
        cols = max(len(r[1]) for r in rows)
        per = 1 if cls != 'all' else 3
        W = cell[0] * cols * per
        H = (cell[1] + 18) * ((len(rows) + per - 1) // per)
        sheet = Image.new('RGB', (W, H), (235, 235, 235))
        d = ImageDraw.Draw(sheet)
        for i, (u, imgs) in enumerate(rows):
            x0 = (i % per) * cell[0] * cols
            y0 = (i // per) * (cell[1] + 18)
            d.text((x0 + 4, y0 + 3), u, fill=(0, 0, 0))
            for k, p in enumerate(imgs):
                if not p:
                    continue
                im = Image.open(p).convert('RGB')
                im.thumbnail(cell)
                sheet.paste(im, (x0 + k * cell[0] + (cell[0] - im.width) // 2, y0 + 18))
        name = 'CONTACT_SHEET_%s.png' % cls
        sheet.save(os.path.join(args.mirror, name))
        print('wrote', os.path.join(args.mirror, name))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('draw', 'contact', 'list'))
    ap.add_argument('units', nargs='*')
    ap.add_argument('--only')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'refs'))
    ap.add_argument('--mirror', default=MIRROR)
    args = ap.parse_args()
    if args.cmd == 'list':
        for u, d in UNITS.items():
            print(u, d['cls'], d['kind'], [i for i, _ in d['items']])
        return
    if args.cmd == 'contact':
        return contact(args)
    units = args.units or list(UNITS)
    bad = [u for u in units if u not in UNITS]
    if bad:
        raise SystemExit('unknown: %s' % ', '.join(bad))
    pool = ThreadPoolExecutor(args.j)  # side/back/items of every unit
    outer = ThreadPoolExecutor(args.j)  # fronts
    futs = [outer.submit(draw_unit, u, args, pool) for u in units]
    for f in futs:
        f.result()
    print('codex calls:', calls)


if __name__ == '__main__':
    main()
