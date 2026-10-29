#!/usr/bin/env python3
"""Templates and reference sheets for the artist (docs/SPRITE_SPEC.md).

    python3 tools/sprites/templates.py [--out docs/sprite_templates]
        manifest.json and files.txt (every picture the pipeline takes), blank
        canvases per size class (_blank/), and per unit type one labelled
        reference sheet per clip (<unit>/<clip>.webp: views down, frames
        across, at a quarter of the delivery scale, from today's sheets).

    python3 tools/sprites/templates.py guides UNIT OUT_DIR [--clip walk]
        full-size pose guides, one per cell, named as the delivery files
        (today's figure on the delivery canvas at the delivery scale and
        anchor, team areas in the key colour), to give an image generator as
        a reference picture.
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

import layout  # noqa: E402
import sheet  # noqa: E402

BG = (222, 226, 214, 255)
GRID = (196, 202, 188, 255)
GROUND = (120, 130, 110, 255)
ANCHOR = (220, 30, 30, 255)
HEAD = (60, 120, 200, 255)
INK = (30, 30, 30, 255)
KEY = tuple(int(layout.TEAM_KEY[i:i + 2], 16) for i in (1, 3, 5))
VIEW_NAMES = ('front', 'front-right 45', 'right side', 'back-right 135', 'back')
QUARTER = 4
NOTES = {
    'idle': 'loop, ~2.2 s a cycle: breathing, weight shift',
    'walk': 'loop: one full stride (left and right step) over 8 frames; fliers: 0-3 on foot, 4-7 a wingbeat in the air',
    'attack': 'frame 3 the blow lands (ranged: frame 4 the missile has left)',
    'flinch': 'struck: recoil, recover',
    'die_back': 'struck from the front: falls on his back; frame 5 the corpse, flat',
    'die_front': 'NEW. struck from behind: falls forward, face down; frame 5 the corpse',
    'die_crumple': 'NEW. cut down: knees buckle, slumps sideways; frame 5 the corpse',
    'die_burn': 'NEW, optional. burning: writhes, collapses; frame 5 the charred corpse',
}


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def source_sheet(unit):
    """Today's best sheet of a unit: the close-up one if there is one."""
    for p in (os.path.join(ROOT, 'game', 'web', 'sprites', unit + '_hd.json'), os.path.join(ROOT, 'build', 'sprites', unit, unit + '_hd.json'),
              os.path.join(HERE, 'baseline', unit + '.json'), os.path.join(ROOT, 'game', 'web', 'sprites', unit + '.json')):
        if os.path.exists(p):
            meta, cells = sheet.read(p)
            if meta.get('source') == 'import' and 'baseline' not in p:
                continue
            return meta, cells
    return None, None


def display(cell):
    """An atlas cell as a picture: team pixels in the key colour, outline dark, straight RGBA."""
    img = cell['img'].astype(np.float32)
    a = img[..., 3]
    out = img.copy()
    team = (a > 170) & (a < 223)
    out[team, :3] = img[team, :3] / 255.0 * np.array(KEY, np.float32)
    out[..., 3] = np.where(a > 100, 255, 0)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA')


def figure(cells, fpv, clip, v, f):
    """(picture, u0, v0, mirrored, guide) of a clip's frame as today's sheet has it."""
    guide = False
    src_clip, src_v, mirror = clip, v, False
    if clip not in layout.FIRST or layout.FIRST[clip] + layout.FRAMES[clip] > fpv:
        guide = True
        src_clip = 'die_back'
        if clip == 'die_front':  # die_back seen from the other side
            src_v, mirror = 4 - v, v not in (0, 4)
    c = cells[src_v * fpv + layout.FIRST[src_clip] + min(f, layout.FRAMES[src_clip] - 1)]
    return c, mirror, guide


def place(canvas, c, mirror, ppm_src, ppm, ax, ay, ghost=1.0):
    im = display(c)
    s = ppm / ppm_src
    w, h = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = im.resize((w, h), Image.Resampling.LANCZOS)
    u0 = c['u0']
    if mirror:
        im = im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        u0 = -(c['u0'] + c['w'] / ppm_src)
    if ghost < 1:
        a = np.asarray(im).copy()
        a[..., 3] = (a[..., 3] * ghost).astype(np.uint8)
        im = Image.fromarray(a, 'RGBA')
    x = round(ax + u0 * ppm)
    y = round(ay - (c['v0'] * ppm + h))
    canvas.alpha_composite(im, (x, y))


def guides_layer(W, H, ppm, ax, ay, height=None, labels=True, fs=12):
    """The canvas with its 30-degree ground grid (1 m squares), ground line, anchor and a 1 m bar."""
    im = Image.new('RGBA', (W, H), BG)
    d = ImageDraw.Draw(im)
    sin = math.sin(math.radians(layout.ELEVATION_DEG))
    k = -30
    while k <= 30:
        x = ax + k * ppm
        if 0 <= x < W:
            d.line([(x, 0), (x, H)], fill=GRID, width=1)
        y = ay - k * ppm * sin
        if 0 <= y < H:
            d.line([(0, y), (W, y)], fill=GRID, width=1)
        k += 1
    d.line([(0, ay), (W, ay)], fill=GROUND, width=max(1, int(ppm / 64)))
    r = max(4, int(ppm * 0.12))
    d.line([(ax - r, ay), (ax + r, ay)], fill=ANCHOR, width=max(1, int(ppm / 48)))
    d.line([(ax, ay - r), (ax, ay + r)], fill=ANCHOR, width=max(1, int(ppm / 48)))
    if height:
        hy = ay - height * math.cos(math.radians(layout.ELEVATION_DEG)) * ppm
        d.line([(ax - 0.6 * ppm, hy), (ax + 0.6 * ppm, hy)], fill=HEAD, width=max(1, int(ppm / 64)))
        if labels:
            d.text((ax + 0.65 * ppm, hy - fs * 0.6), 'top of a %.1f m figure' % height, fill=HEAD, font=font(fs))
    bx, by = 0.05 * W, H - 0.04 * H
    d.line([(bx, by), (bx + ppm, by)], fill=INK, width=max(2, int(ppm / 40)))
    if labels:
        d.text((bx, by - fs * 1.4), '1 m = %d px' % round(ppm), fill=INK, font=font(fs))
    return im


def reference(unit, clip, meta, cells, out_path):
    info = layout.UNITS[unit]
    klass = layout.CLASSES[info[1]]
    cw, ch = klass['canvas'][0] // QUARTER, klass['canvas'][1] // QUARTER
    ppm = klass['ppm'] / QUARTER
    ax, ay = klass['anchor'][0] / QUARTER, klass['anchor'][1] / QUARTER
    frames = layout.FRAMES[clip]
    head, left = 64, 120
    W, H = left + frames * cw, head + layout.VIEWS * ch
    out = Image.new('RGBA', (W, H), (255, 255, 255, 255))
    d = ImageDraw.Draw(out)
    title = '%s  %s  (%d views x %d frames, canvas %dx%d at %d px/m, anchor %d,%d)' % (
        unit, clip, layout.VIEWS, frames, klass['canvas'][0], klass['canvas'][1], klass['ppm'], *klass['anchor'])
    d.text((8, 6), title, fill=INK, font=font(16))
    d.text((8, 28), NOTES[clip], fill=INK, font=font(13))
    d.text((8, 46), 'shown at 1/4 scale; blue = team key %s; red cross = anchor (feet)' % layout.TEAM_KEY, fill=INK, font=font(11))
    fpv = meta['framesPerView'] if meta else 0
    ppm_src = float(meta['ppm']) if meta else 1
    for v in range(layout.VIEWS):
        d.text((6, head + v * ch + 6), 'v%d\n%s' % (v, VIEW_NAMES[v]), fill=INK, font=font(12))
        for f in range(frames):
            cell = guides_layer(cw, ch, ppm, ax, ay, info[2], labels=False)
            guide = False
            if cells:
                c, mirror, guide = figure(cells, fpv, clip, v, f)
                if c is not None:
                    place(cell, c, mirror, ppm_src, ppm, ax, ay, ghost=0.4 if guide else 1.0)
            cd = ImageDraw.Draw(cell)
            cd.rectangle([0, 0, cw - 1, ch - 1], outline=GRID)
            cd.text((4, 3), 'v%d_f%02d' % (v, f), fill=INK, font=font(12))
            if guide and f == 0:
                cd.text((4, 18), 'guide only', fill=ANCHOR, font=font(11))
            out.alpha_composite(cell, (left + f * cw, head + v * ch))
    out.convert('RGB').save(out_path, quality=60, method=6)


def blanks(out):
    d = os.path.join(out, '_blank')
    os.makedirs(d, exist_ok=True)
    heights = {'man': 1.8, 'large': 2.7, 'huge': 5.5}
    for name, c in layout.CLASSES.items():
        im = guides_layer(c['canvas'][0], c['canvas'][1], c['ppm'], c['anchor'][0], c['anchor'][1], heights[name], fs=18)
        dr = ImageDraw.Draw(im)
        dr.text((12, 10), '%s class: %dx%d, %d px/m, anchor (feet) at %d,%d; camera 30 deg above the horizon' % (
            name, c['canvas'][0], c['canvas'][1], c['ppm'], c['anchor'][0], c['anchor'][1]), fill=INK, font=font(18))
        im.convert('RGB').save(os.path.join(d, '%s.png' % name), optimize=True)
    g = layout.GIB_CANVAS
    im = Image.new('RGBA', tuple(g), BG)
    dr = ImageDraw.Draw(im)
    dr.line([(g[0] // 2 - 12, g[1] // 2), (g[0] // 2 + 12, g[1] // 2)], fill=ANCHOR, width=2)
    dr.line([(g[0] // 2, g[1] // 2 - 12), (g[0] // 2, g[1] // 2 + 12)], fill=ANCHOR, width=2)
    dr.text((8, 8), 'gib part %dx%d: the part centred on the cross,\nat the unit\'s class scale' % tuple(g), fill=INK, font=font(14))
    im.convert('RGB').save(os.path.join(d, 'gib.png'), optimize=True)


def write_all(out):
    os.makedirs(out, exist_ok=True)
    man = layout.manifest()
    with open(os.path.join(out, 'manifest.json'), 'w') as fh:
        json.dump(man, fh, separators=(',', ':'))
    with open(os.path.join(out, 'files.txt'), 'w') as fh:
        # Header lines (#clip, #unit) for tests/test_sprite_layout.cpp; then one file a line.
        for c in man['clips']:
            fh.write('#clip %s %d %d\n' % (c['name'], c['frames'], c['first']))
        for unit, u in man['units'].items():
            fh.write('#unit %s %d %s\n' % (unit, u['type'], u['class']))
        for e in man['cells']:
            fh.write('%s %s\n' % (e['file'], 'required' if e['required'] else 'optional'))
    blanks(out)
    for unit in sorted(layout.UNITS, key=lambda u: layout.UNITS[u][0]):
        meta, cells = source_sheet(unit)
        d = os.path.join(out, unit)
        os.makedirs(d, exist_ok=True)
        for clip, _, _, _ in layout.CLIPS:
            reference(unit, clip, meta, cells, os.path.join(d, clip + '.webp'))
        print(unit, 'from', '%.0f px/m sheet' % meta['ppm'] if meta else 'nothing')
    req = sum(e['required'] for e in man['cells'])
    print('%d pictures in the manifest, %d required' % (len(man['cells']), req))


def write_guides(unit, out, clip=None):
    """Full-size pose guides, named as the delivery files."""
    meta, cells = source_sheet(unit)
    if not meta:
        raise SystemExit('%s: no sheet to make guides from' % unit)
    info = layout.UNITS[unit]
    k = layout.CLASSES[info[1]]
    for name, frames, _, _ in layout.CLIPS:
        if clip and name != clip:
            continue
        for v in range(layout.VIEWS):
            for f in range(frames):
                im = guides_layer(k['canvas'][0], k['canvas'][1], k['ppm'], k['anchor'][0], k['anchor'][1], info[2], fs=18)
                c, mirror, guide = figure(cells, meta['framesPerView'], name, v, f)
                if c is not None:
                    place(im, c, mirror, float(meta['ppm']), k['ppm'], k['anchor'][0], k['anchor'][1], 0.4 if guide else 1.0)
                p = os.path.join(out, unit, layout.cell_file(name, v, f))
                os.makedirs(os.path.dirname(p), exist_ok=True)
                im.convert('RGB').save(p)
    print('guides for %s in %s' % (unit, os.path.join(out, unit)))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ['guides']:
        ap = argparse.ArgumentParser()
        ap.add_argument('unit')
        ap.add_argument('out')
        ap.add_argument('--clip')
        a = ap.parse_args(argv[1:])
        write_guides(a.unit, a.out, a.clip)
        return 0
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'docs', 'sprite_templates'))
    a = ap.parse_args(argv)
    write_all(a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
