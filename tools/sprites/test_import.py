#!/usr/bin/env python3
"""The importer on a small synthetic delivery: python3 tools/sprites/test_import.py

Draws a few cells of heavy infantry (a figure with a team-blue tabard, on the
chroma green or on alpha, feet on the anchor), imports them over a copy of
today's sheet and checks the result is a sheet the renderer takes
(Renderer::loadSheet's rules), that the delivered cells are where and as big
as they should be, that missing clips borrow cells, and that a second run
changes nothing. Also --check on a bad delivery."""

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


def figure(path, height_m, chroma, lean=0.0, lying=False):
    k = layout.unit_class(UNIT)
    W, H = k['canvas']
    ppm = k['ppm']
    ax, ay = k['anchor']
    im = Image.new('RGBA', (W, H), (0, 255, 0, 255) if chroma else (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    tall = height_m * math.cos(math.radians(layout.ELEVATION_DEG)) * ppm
    if lying:
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
    """Renderer::loadSheet's checks."""
    W, H = size
    fpv, cells = meta['framesPerView'], meta['cells']
    if not ((fpv == 26 and len(cells) == 130) or (fpv == 44 and len(cells) in (220, 240))):
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


def main():
    tmp = tempfile.mkdtemp(prefix='sprite_import_')
    art, out, base = os.path.join(tmp, 'art'), os.path.join(tmp, 'out'), os.path.join(tmp, 'baseline')
    os.makedirs(out)
    for ext in ('.json', '.webp'):
        shutil.copyfile(os.path.join(ROOT, 'game', 'web', 'sprites', UNIT + ext), os.path.join(out, UNIT + ext))
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

    rc, log = run(art, '--out', out, '--baseline', base, '--check')
    check(rc == 0, '--check passes a good delivery')
    check('20 delivered' not in log and '51 delivered' in log, '--check counts the delivered files (%s)' % log.splitlines()[0])
    check(not os.path.exists(base), '--check writes nothing')

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
    check(meta['cells'][layout.gib_slot('head', 0)] == [0] * 8, 'a gib part not delivered is an empty row')
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
    shutil.rmtree(tmp)
    print('%d failed' % len(FAILS))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
