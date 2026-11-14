#!/usr/bin/env python3
"""sheet_gibs.py on a small drawn sheet, and the shipped sheets: python3 tools/sprites/test_sheet_gibs.py

Draws an old-layout sheet (26 frames a view) of a man and of a four-legged
beast, cuts their parts and checks: the sheet is one the renderer takes
(Renderer::loadSheet's rules: 150 cells, each part inside the picture), no
old pixel or cell moved, the parts are pieces of the figure (every solid
pixel of a part is the figure's or blood), a beast gets its head, barrel and
legs and no arms, and a second run changes nothing. Then every sheet in
game/web/sprites: each has gib cells, and each has at least a head, a trunk,
a leg and a gobbet (the catapult: timber), so nothing falls back to the 3D parts.
"""

import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import layout  # noqa: E402
import sheet_gibs  # noqa: E402

FAILS = []
PPM = 48.0


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        FAILS.append(what)


def man(d, x, ground, tall):
    d.rectangle([x - 0.12 * tall, ground - 0.85 * tall, x + 0.12 * tall, ground - 1], fill=(150, 120, 90, 255))
    d.rectangle([x - 0.1 * tall, ground - 0.8 * tall, x + 0.1 * tall, ground - 0.45 * tall], fill=(230, 230, 230, 191))
    d.ellipse([x - 0.07 * tall, ground - tall, x + 0.07 * tall, ground - 0.84 * tall], fill=(220, 180, 150, 255))


def beast(d, x, ground, tall):
    d.rectangle([x - 0.9 * tall, ground - 0.8 * tall, x + 0.5 * tall, ground - 0.4 * tall], fill=(120, 90, 60, 255))  # barrel
    d.rectangle([x + 0.5 * tall, ground - tall, x + 0.9 * tall, ground - 0.6 * tall], fill=(140, 100, 70, 255))    # head and neck
    for leg in (-0.8, -0.6, 0.2, 0.4):
        d.rectangle([x + leg * tall, ground - 0.4 * tall, x + (leg + 0.08) * tall, ground - 1], fill=(90, 70, 50, 255))


def sheet(draw, height_m):
    """An old-layout sheet: every cell the same figure, in a grid, its json as tools/blender writes it."""
    tall = height_m * math.cos(math.radians(layout.ELEVATION_DEG)) * PPM
    cw, ch = int(tall * 2.2) + 8, int(tall * 1.2) + 8
    cols = 13
    rows = math.ceil(layout.LEGACY_CELLS / cols)
    im = Image.new('RGBA', (cols * cw, rows * ch), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cells = []
    for c in range(layout.LEGACY_CELLS):
        x0, y0 = (c % cols) * cw, (c // cols) * ch
        ax, ground = x0 + cw // 2, y0 + ch - 4
        draw(d, ax, ground, tall)
        cells.append([x0, im.height - y0 - ch, cw, ch, -(cw // 2) / PPM, -4 / PPM, cw / PPM, ch / PPM])
    return np.array(im), cells


def loads(px, meta):
    """Renderer::loadSheet's rules for an old sheet with its parts."""
    H, W = px.shape[:2]
    cells = meta['cells']
    if len(cells) != layout.LEGACY_CELLS + layout.GIB_CELLS or meta['size'] != [W, H]:
        return False
    for i, r in enumerate(cells):
        if i >= layout.LEGACY_CELLS and r[2] == 0 and r[3] == 0:
            continue
        if r[0] < 0 or r[1] < 0 or r[2] < 1 or r[3] < 1 or r[0] + r[2] > W or r[1] + r[3] > H or r[6] <= 0 or r[7] <= 0:
            return False
    return True


def part(px, meta, name, variant=0):
    r = meta['cells'][layout.LEGACY_CELLS + layout.GIB_PARTS.index(name) * 2 + variant]
    if r[2] == 0:
        return None
    H = px.shape[0]
    return px[H - r[1] - r[3]:H - r[1], r[0]:r[0] + r[2]]


def drawn(unit, draw):
    px, cells = sheet(draw, layout.UNITS[unit][2])
    meta = {'type': layout.UNITS[unit][0], 'name': unit, 'ppm': PPM, 'size': [px.shape[1], px.shape[0]], 'cells': cells}
    parts = sheet_gibs.cut_sheet(px, meta, unit)
    out, new = sheet_gibs.pack(px, meta, parts)
    check(loads(out, new), '%s: a sheet the renderer takes (150 cells, every part inside it)' % unit)
    check(np.array_equal(out[out.shape[0] - px.shape[0]:], px) and new['cells'][:layout.LEGACY_CELLS] == cells,
          '%s: no old pixel or cell moved' % unit)
    again = sheet_gibs.cut_sheet(px, meta, unit)
    check(sorted(again) == sorted(parts) and all(np.array_equal(again[k], parts[k]) for k in parts), '%s: the same cut each time' % unit)
    return out, new


def main():
    out, new = drawn('light_infantry', man)
    for name in ('head', 'torso', 'arm', 'leg', 'chunk'):
        check(part(out, new, name) is not None and part(out, new, name, 1) is not None, 'man: %s in both looks' % name)
    head = part(out, new, 'head')
    solid = head[..., 3] > 170
    skin = np.abs(head[..., :3].astype(int) - np.array([220, 180, 150])).sum(axis=2) < 12
    check(head is not None and (skin & solid).sum() > 0.4 * solid.sum(), 'man: the head is the head')
    torso = part(out, new, 'torso')
    check(torso is not None and (torso[..., 3] == 191).sum() > 20, 'man: the trunk keeps its cloth in the side\'s colour')
    check(part(out, new, 'mount_head') is None and part(out, new, 'wing') is None, 'man: no mount, no wings')

    out, new = drawn('unicorns', beast)
    for name in ('head', 'torso', 'leg', 'chunk'):
        check(part(out, new, name) is not None, 'beast: %s' % name)
    check(part(out, new, 'arm') is None, 'beast: no arms')
    barrel, leg = part(out, new, 'torso'), part(out, new, 'leg')
    check(barrel is not None and barrel.shape[1] > barrel.shape[0], 'beast: the barrel is long, not tall')
    check(leg is not None and leg.shape[0] > 2 * leg.shape[1], 'beast: a leg is a leg (tall and thin, no strip of belly)')
    red = (barrel[..., 0] > 80) & (barrel[..., 1] < 40) & (barrel[..., 3] > 170)
    check(red.sum() > 4, 'beast: blood where the head and legs were cut away')

    # The shipped sheets.
    sprites = os.path.join(ROOT, 'game', 'web', 'sprites')
    index = json.load(open(os.path.join(sprites, 'index.json')))
    names = [index[k] for k in sorted(index, key=int)]
    names += [n + '_hd' for n in names if os.path.exists(os.path.join(sprites, n + '_hd.json'))]
    bare, thin = [], []
    for name in names:
        meta = json.load(open(os.path.join(sprites, name + '.json')))
        fpv = int(meta.get('framesPerView', layout.LEGACY_FRAMES_PER_VIEW))
        gib = meta['cells'][layout.VIEWS * fpv:]
        if len(gib) != layout.GIB_CELLS:
            bare.append(name)
            continue
        has = {layout.GIB_PARTS[i // 2] for i, r in enumerate(gib) if r[2] > 0}
        need = {'chunk'} if sheet_gibs.plan_of(name.replace('_hd', '')) == 'engine' else {'head', 'torso', 'leg', 'chunk'}
        if not need <= has:
            thin.append('%s lacks %s' % (name, ', '.join(sorted(need - has))))
    check(not bare, 'every shipped sheet has gib cells' + (': not ' + ', '.join(bare) if bare else ''))
    check(not thin, 'every shipped sheet has a head, a trunk, a leg and a gobbet' + (': ' + '; '.join(thin) if thin else ''))
    print('%d failed' % len(FAILS) if FAILS else 'all passed')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
