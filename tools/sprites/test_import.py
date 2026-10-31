#!/usr/bin/env python3
"""The importer on a small synthetic delivery: python3 tools/sprites/test_import.py

Draws a few cells of heavy infantry (a figure with a team-blue tabard, on the
chroma green or on alpha, feet on the anchor), imports them over a copy of
today's sheet and checks the result is a sheet the renderer takes
(Renderer::loadSheet's rules), that the delivered cells are where and as big
as they should be, that missing clips borrow cells, and that a second run
changes nothing. Also --check on a bad delivery; and deliveries of a unit's
own clips (clips.json): laid out as listed, placed as drawn, whole, and with
fewer pixels a metre when they would not fit."""

import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import layout  # noqa: E402

UNIT = 'heavy_infantry'
FAILS = []


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        FAILS.append(what)


def figure(path, height_m, chroma, lean=0.0, lying=False, wide=0.0):
    k = layout.unit_class(UNIT)
    W, H = k['canvas']
    ppm = k['ppm']
    ax, ay = k['anchor']
    im = Image.new('RGBA', (W, H), (0, 255, 0, 255) if chroma else (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    tall = height_m * math.cos(math.radians(layout.ELEVATION_DEG)) * ppm
    if wide:
        # A big picture (a spread of wings, a sprawl): wide metres across, the figure's height up.
        d.rectangle([ax - wide / 2 * ppm, ay - tall, ax + wide / 2 * ppm, ay], fill=(150, 120, 90, 255))
    elif lying:
        d.rectangle([ax - 1.7 * ppm, ay - 0.35 * ppm, ax, ay - 0.05 * ppm], fill=(150, 120, 90, 255))
        d.rectangle([ax - 1.2 * ppm, ay - 0.3 * ppm, ax - 0.5 * ppm, ay - 0.1 * ppm], fill=(0, 80, 255, 255))
    else:
        x = ax + lean * ppm
        d.rectangle([x - 0.22 * ppm, ay - tall * 0.85, x + 0.22 * ppm, ay], fill=(150, 120, 90, 255))  # body and legs
        d.rectangle([x - 0.2 * ppm, ay - tall * 0.8, x + 0.2 * ppm, ay - tall * 0.45], fill=(0, 64, 210, 255))  # tabard, shaded key blue
        d.ellipse([x - 0.12 * ppm, ay - tall, x + 0.12 * ppm, ay - tall * 0.8], fill=(220, 180, 150, 255))  # head
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path)


def digest(d):
    h = hashlib.sha256()
    for f in sorted(os.listdir(d)):
        p = os.path.join(d, f)
        if os.path.isfile(p):
            h.update(f.encode())
            h.update(open(p, 'rb').read())
    return h.hexdigest()


def loadable(meta, size):
    """Renderer::loadSheet's checks (and SheetClips::valid's, for a sheet of its own clips)."""
    W, H = size
    fpv, cells = meta['framesPerView'], meta['cells']
    if 'clips' in meta:
        first = 0
        for c in meta['clips']:
            if c['first'] != first or c['frames'] < 1:
                return False
            first += c['frames']
        if first != fpv or len(cells) not in (5 * fpv, 5 * fpv + layout.GIB_CELLS):
            return False
    elif not ((fpv == 26 and len(cells) == 130) or (fpv == 44 and len(cells) in (220, 240))):
        return False
    for c, r in enumerate(cells):
        if c >= 5 * fpv and r[2] == 0 and r[3] == 0:
            continue
        if r[0] < 0 or r[1] < 0 or r[2] < 1 or r[3] < 1 or r[0] + r[2] > W or r[1] + r[3] > H or r[6] <= 0 or r[7] <= 0:
            return False
    return True


def run(*args):
    p = subprocess.run([sys.executable, os.path.join(HERE, 'import.py'), *args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def deliver(art, unit, clips, height, **kw):
    """A delivery of a unit's own clips: clips.json and every cell (kw: figure()'s, or per clip and frame by a function)."""
    d = os.path.join(art, unit)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'clips.json'), 'w') as fh:
        json.dump({'clips': clips}, fh)
    for c in clips:
        for v in range(5):
            for f in range(c['frames']):
                args = kw['each'](c['name'], f) if 'each' in kw else kw
                figure(os.path.join(d, layout.cell_file(c['name'], v, f)), height, chroma=False, **args)


def listed(tmp):
    """Units delivered with their own clips (clips.json): the archers, small,
    over today's sheet; the hyena-folk, big enough that their sheets shrink."""
    art, out, base = (os.path.join(tmp, 'listed', d) for d in ('art', 'out', 'baseline'))
    os.makedirs(out)
    for sfx in ('', '_hd'):
        for ext in ('.json', '.webp'):
            shutil.copyfile(os.path.join(ROOT, 'game', 'web', 'sprites', 'archers' + sfx + ext), os.path.join(out, 'archers' + sfx + ext))
    clips = [
        {'name': 'idle', 'frames': 2, 'seconds': 2.0, 'loop': True},
        {'name': 'walk', 'frames': 2, 'loop': True, 'stride': 1.4},
        {'name': 'attack', 'frames': 3, 'seconds': 2.4, 'release': 1.45, 'times': [0.0, 1.0, 1.45]},
        {'name': 'flinch', 'frames': 1, 'seconds': 0.6},
        {'name': 'die_back', 'frames': 2, 'seconds': 1.5},
        {'name': 'celebrate', 'frames': 2, 'seconds': 2.7, 'loop': True},
    ]
    # The idle's second frame stands a hand to the right of its first, as a render might put it.
    deliver(art, 'archers', clips, layout.UNITS['archers'][2],
            each=lambda name, f: {'lean': 0.1 * f if name == 'idle' else 0.0, 'lying': name == 'die_back' and f == 1})
    rc, log = run(art, '--out', out, '--baseline', base, '--check')
    check(rc == 0 and 'its own 6 clips' in log and '60 delivered' in log, '--check takes a delivery of its own clips')
    rc, log = run(art, '--out', out, '--baseline', base)
    print(log)
    check(rc == 0, 'a delivery of its own clips imports')
    meta = json.load(open(os.path.join(out, 'archers.json')))
    hd = json.load(open(os.path.join(out, 'archers_hd.json')))
    fpv = 12
    check(meta['framesPerView'] == fpv and len(meta['cells']) == 5 * fpv + layout.GIB_CELLS and meta['layout'] == 3,
          'its own layout: 12 frames a view, 60 cells and the gib parts')
    check(meta['clips'] == layout.listed_clips(clips)[0] and hd['clips'] == meta['clips'], 'both sheets list its clips')
    check([c['first'] for c in meta['clips']] == [0, 2, 4, 7, 8, 10] and meta['clips'][2]['release'] == 1.45 and
          meta['clips'][2]['times'] == [0.0, 1.0, 1.45] and meta['clips'][1]['stride'] == 1.4 and meta['clips'][0]['loop'],
          'each clip with its first frame and its timing')
    check(loadable(meta, Image.open(os.path.join(out, 'archers.webp')).size) and
          loadable(hd, Image.open(os.path.join(out, 'archers_hd.webp')).size), 'both sheets pass the renderer\'s checks')
    f0, f1 = meta['cells'][2 * fpv + 0], meta['cells'][2 * fpv + 1]
    check(abs((f1[4] - f0[4]) - 0.1) < 0.02, 'placed as rendered, not planted (the idle frames %.2f m apart, drawn 0.10)' % (f1[4] - f0[4]))
    check(meta['cells'][layout.gib_slot('head', 0, fpv)][2] > 0, 'its gib parts cut from the idle, after its views')
    check(json.load(open(os.path.join(out, 'index.json')))['2'] == 'archers' and
          json.load(open(os.path.join(out, 'index_hd.json')))['2']['name'] == 'archers_hd', 'the index lists both sheets')
    before = digest(out)
    rc, _ = run(art, '--out', out, '--baseline', base)
    check(rc == 0 and digest(out) == before, 'a second run changes nothing')
    # A cell missing, and a list without a walk.
    os.remove(os.path.join(art, 'archers', 'attack', 'v3_f02.png'))
    rc, log = run(art, '--out', out, '--baseline', base, '--check')
    check(rc == 1 and 'not delivered' in log and 'attack/v3_f02.png' in log, 'every cell of its clips is needed')
    with open(os.path.join(art, 'archers', 'clips.json'), 'w') as fh:
        json.dump({'clips': [c for c in clips if c['name'] != 'walk']}, fh)
    rc, log = run(art, '--out', out, '--baseline', base, '--check')
    check(rc == 1 and 'missing the clips walk' in log, 'clips.json is checked')
    shutil.rmtree(os.path.join(art, 'archers'))
    # The hyena-folk, 30 frames a view of pictures 5 m wide: the pixels a metre come down until they fit.
    big = [{'name': n, 'frames': 6, 'seconds': 1.0} for n in ('idle', 'walk', 'attack', 'flinch', 'die_back')]
    deliver(art, 'hyena', big, 2.5, wide=5.0)
    rc, log = run(art, '--out', out, '--baseline', base)
    check(rc == 0, 'a big delivery imports (%s)' % log.strip().splitlines()[-2])
    meta = json.load(open(os.path.join(out, 'hyena.json')))
    hd = json.load(open(os.path.join(out, 'hyena_hd.json')))
    W, H = Image.open(os.path.join(out, 'hyena.webp')).size
    Wh, Hh = Image.open(os.path.join(out, 'hyena_hd.webp')).size
    check(meta['ppm'] < 48 and hd['ppm'] < 96 and max(W, H) <= 2048 and max(Wh, Hh) <= 4096,
          'its sheets shrink to fit: %dx%d at %.1f px/m, close-up %dx%d at %.1f' % (W, H, meta['ppm'], Wh, Hh, hd['ppm']))
    check(json.load(open(os.path.join(out, 'index.json')))['27'] == 'hyena', 'the hyena-folk listed as type 27')


def main():
    tmp = tempfile.mkdtemp(prefix='sprite_import_')
    art, out, base = os.path.join(tmp, 'art'), os.path.join(tmp, 'out'), os.path.join(tmp, 'baseline')
    os.makedirs(out)
    os.makedirs(base)
    for ext in ('.json', '.webp'):
        shutil.copyfile(os.path.join(ROOT, 'game', 'web', 'sprites', UNIT + ext), os.path.join(out, UNIT + ext))
        # Today's sheet is an import of its own: the cells not delivered come from the one before it.
        for sfx in ('', '_hd'):
            shutil.copyfile(os.path.join(HERE, 'baseline', UNIT + sfx + ext), os.path.join(base, UNIT + sfx + ext))
    height = layout.UNITS[UNIT][2]
    for v in range(5):
        for f in range(4):
            figure(os.path.join(art, UNIT, layout.cell_file('idle', v, f)), height, chroma=True, lean=0.02 * f)
        for f in range(6):
            figure(os.path.join(art, UNIT, layout.cell_file('die_front', v, f)), height, chroma=False, lying=f == 5)
    # A gib, and a team mask given as a file for one cell.
    g = Image.new('RGBA', tuple(layout.GIB_CANVAS), (0, 0, 0, 0))
    ImageDraw.Draw(g).rectangle([200, 240, 320, 272], fill=(170, 130, 110, 255))
    os.makedirs(os.path.join(art, UNIT, 'gibs'))
    g.save(os.path.join(art, UNIT, 'gibs', 'arm_a.png'))
    m = Image.new('L', tuple(layout.unit_class(UNIT)['canvas']), 0)
    k = layout.unit_class(UNIT)
    ImageDraw.Draw(m).rectangle([k['anchor'][0] - 40, k['anchor'][1] - 200, k['anchor'][0] + 40, k['anchor'][1] - 120], fill=255)
    m.save(os.path.join(art, UNIT, 'idle', 'v4_f03_team.png'))

    before = digest(out), digest(base)
    rc, log = run(art, '--out', out, '--baseline', base, '--check')
    check(rc == 0, '--check passes a good delivery')
    check('20 delivered' not in log and '51 delivered' in log, '--check counts the delivered files (%s)' % log.splitlines()[0])
    check((digest(out), digest(base)) == before, '--check writes nothing')

    rc, log = run(art, '--out', out, '--baseline', base)
    print(log)
    check(rc == 0, 'import runs')
    meta = json.load(open(os.path.join(out, UNIT + '.json')))
    hd = json.load(open(os.path.join(out, UNIT + '_hd.json')))
    W, H = Image.open(os.path.join(out, UNIT + '.webp')).size
    check(meta['framesPerView'] == layout.FRAMES_PER_VIEW and len(meta['cells']) == layout.CELLS, 'the new layout: 44 frames a view, 240 cells')
    check(loadable(meta, (W, H)), 'the normal sheet passes the renderer\'s checks')
    check(loadable(hd, Image.open(os.path.join(out, UNIT + '_hd.webp')).size), 'the close-up sheet passes them too')
    check(abs(hd['ppm'] - 2 * meta['ppm']) < 0.01, 'close-up at twice the pixels a metre (%.1f, %.1f)' % (meta['ppm'], hd['ppm']))
    idle = meta['cells'][2 * 44 + 0]
    expect = height * math.cos(math.radians(30))
    check(abs(idle[7] - expect) < 0.08, 'an idle cell is the figure\'s height (%.2f m, %.2f expected)' % (idle[7], expect))
    check(abs(idle[4] + idle[6] / 2) < 0.06 and abs(idle[5]) < 0.06, 'feet on the anchor (u0 %.2f w %.2f v0 %.2f)' % (idle[4], idle[6], idle[5]))
    atlas = np.asarray(Image.open(os.path.join(out, UNIT + '.webp')).convert('RGBA'))
    x, y, w, h = (int(t) for t in idle[:4])
    a = atlas[atlas.shape[0] - (y + h):atlas.shape[0] - y, x:x + w, 3]
    check((a == 191).sum() > 0.2 * (a == 255).sum(), 'the key-blue tabard became team-coloured pixels')
    check(set(np.unique(atlas[..., 3])) <= {0, 153, 191, 255}, 'alpha holds only the codes')
    crumple = [meta['cells'][v * 44 + 32 + f] for v in range(5) for f in range(6)]
    back = [meta['cells'][v * 44 + 20 + f] for v in range(5) for f in range(6)]
    check(crumple == back, 'die_crumple (not delivered) borrows die_back\'s cells')
    front = meta['cells'][1 * 44 + 26 + 5]
    check(front != back[6 + 5], 'die_front (delivered) has cells of its own')
    walk_old = json.load(open(os.path.join(base, UNIT + '.json')))['cells'][3 * 26 + 6]
    walk_new = meta['cells'][3 * 44 + 6]
    check(walk_old[2:] == walk_new[2:], 'undelivered walk frames are today\'s')
    gib = meta['cells'][layout.gib_slot('arm', 0)]
    check(gib[2] > 0 and abs(gib[6] - 121 / 192) < 0.05, 'the gib part is packed at scale (%.2f m)' % gib[6])
    check(meta['cells'][layout.gib_slot('head', 0)][2] > 0, 'a gib part not delivered is cut from the idle (gibs.py)')
    check(meta['cells'][layout.gib_slot('mount_head', 0)] == [0] * 8, 'a gib part the type has not is an empty row')
    t = meta['cells'][4 * 44 + 3]
    x, y, w, h = (int(q) for q in t[:4])
    a = atlas[atlas.shape[0] - (y + h):atlas.shape[0] - y, x:x + w, 3]
    check((a == 191).sum() > 0, 'a _team.png mask is used')
    idx = json.load(open(os.path.join(out, 'index_hd.json')))
    check(idx.get('1', {}).get('name') == UNIT + '_hd', 'index_hd.json lists the close-up sheet')

    before = digest(out)
    rc, _ = run(art, '--out', out, '--baseline', base)
    check(rc == 0 and digest(out) == before, 'a second run changes nothing')

    # A bad delivery: a wrong name and a wrong canvas.
    Image.new('RGBA', (10, 10)).save(os.path.join(art, UNIT, 'idle', 'v9_f00.png'))
    Image.new('RGBA', (512, 512)).save(os.path.join(art, UNIT, 'walk_v0.png'))
    os.makedirs(os.path.join(art, UNIT, 'walk'), exist_ok=True)
    Image.new('RGBA', (512, 512)).save(os.path.join(art, UNIT, 'walk', 'v0_f00.png'))
    rc, log = run(art, '--out', out, '--baseline', base, '--check')
    check(rc == 1 and 'canvas 512x512' in log and 'walk_v0.png' in log and 'v9_f00' in log, '--check reports bad names and canvases')
    listed(tmp)
    shutil.rmtree(tmp)
    print('%d failed' % len(FAILS))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
