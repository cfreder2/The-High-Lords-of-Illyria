"""Packing rendered cells into the game's sprite-sheet format.

A sheet is one RGBA atlas per unit type plus a small JSON table, the same
data the runtime bake (Renderer::bakeSheet) leaves in a Sheet:

  <unit>.png   the atlas (and <unit>.webp, the same smaller: what the game
               fetches; its alpha is lossless). Alpha is a code, as kResolveFs writes it and kUnitFs
               reads it: 255 opaque, 191 (0.75) opaque in the side's colour
               (the shader multiplies the pixel by cloth(team)), 153 (0.6)
               the dark outline, 0 empty.
  <unit>.json  {"type": UnitType number, "ppm": pixels a metre, "size": [w, h],
               "cells": 130 rows [x, y, w, h, u0, v0, uw, vh]} in the order
               view * 26 + frame: the cell's pixels in the atlas (x from the
               left, y from the BOTTOM row, as GL stores textures; flip the PNG
               on upload) and the picture's extent about the feet in metres
               (left, bottom, width, height) -- uCellTable's two rows.

Every frame is rendered at ss times the resolution and reduced here: colour
averaged over covered subpixels, solid where at least half is covered, edge
pixels darkened and a 1 px outline drawn round the figure, as kResolveFs
does, so loaded and baked sheets look alike.
"""

import json
import os

import numpy as np
from PIL import Image

VIEWS, FRAMES = 5, 26
EDGE = 0.8  # kResolveFs darkens the figure's own edge pixels by 0.78
OUTLINE = (0.05, 0.045, 0.04)
GUTTER = 2


def _load(path):
    return np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32) / 255.0


def reduce(pic, mask, ss):
    """ss x ss blocks down to one pixel: colour, coverage and team share."""
    h, w = pic.shape[0] // ss, pic.shape[1] // ss
    pic = pic[:h * ss, :w * ss].reshape(h, ss, w, ss, 4)
    mask = mask[:h * ss, :w * ss].reshape(h, ss, w, ss, 4)
    a = pic[..., 3]
    cover = a.mean(axis=(1, 3))
    rgb = (pic[..., :3] * a[..., None]).sum(axis=(1, 3)) / np.maximum(1e-6, a.sum(axis=(1, 3)))[..., None]
    ma = mask[..., 3]
    team = (mask[..., 0] * ma).sum(axis=(1, 3)) / np.maximum(1e-6, ma.sum(axis=(1, 3)))
    return rgb, cover, team


def resolve(rgb, cover, team):
    """The cutout, edge darkening and outline; returns RGBA (0..1) with alpha codes."""
    solid = cover >= 0.5
    pad = np.pad(solid, 1)
    n4 = [pad[:-2, 1:-1], pad[2:, 1:-1], pad[1:-1, :-2], pad[1:-1, 2:]]
    all4 = n4[0] & n4[1] & n4[2] & n4[3]
    any4 = n4[0] | n4[1] | n4[2] | n4[3]
    out = np.zeros(rgb.shape[:2] + (4,), np.float32)
    out[..., :3] = rgb
    out[solid & ~all4, :3] *= EDGE
    out[..., 3] = np.where(solid, np.where(team >= 0.5, 0.75, 1.0), 0.0)
    ring = ~solid & any4
    out[ring, :3] = OUTLINE
    out[ring, 3] = 0.6
    out[~solid & ~ring] = 0
    return out


def to8(a):
    return np.clip(np.round(a * 255.0), 0, 255).astype(np.uint8)


def pack(d, name, number, meta):
    raw = os.path.join(d, 'raw')
    ppm, ss = meta['ppm'], meta['ss']
    cells, previews, mpreviews = [], {}, {}
    for v in range(VIEWS):
        for f in range(FRAMES):
            c = meta['cells']['%d_%d' % (v, f)]
            i0, j0, i1, j1 = c['lattice']
            pic = _load(os.path.join(raw, 'v%d_f%02d.png' % (v, f)))
            mask = _load(os.path.join(raw, 'v%d_f%02d_mask.png' % (v, f)))
            rgb, cover, team = reduce(pic, mask, ss)
            img = resolve(rgb, cover, team)
            rows = np.where(img[..., 3].max(axis=1) > 0)[0]
            cols = np.where(img[..., 3].max(axis=0) > 0)[0]
            r0, r1, c0, c1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
            cell = img[r0:r1, c0:c1]
            # Row 0 of the image is the lattice's top (j1).
            u0 = float(i0 + c0) / ppm
            v0 = float(j1 - r1) / ppm
            cells.append({'img': cell, 'u0': float(u0), 'v0': float(v0), 'w': int(c1 - c0), 'h': int(r1 - r0)})
            straight = np.concatenate([rgb, cover[..., None]], axis=2)[r0:r1, c0:c1]
            previews[(v, f)] = (straight, u0, v0)
            mpreviews[(v, f)] = (np.concatenate([np.repeat(team[..., None], 3, 2), cover[..., None]], axis=2)[r0:r1, c0:c1], u0, v0)
    # Shelves, tallest first, in an atlas as narrow as keeps it roughly square (bakeSheet's way).
    area = sum((c['w'] + 2 * GUTTER) * (c['h'] + 2 * GUTTER) for c in cells)
    W = 256
    while W < 4096 and W * W < area * 1.2:
        W *= 2
    order = sorted(range(len(cells)), key=lambda k: -cells[k]['h'])
    x = y = shelf = 0
    for k in order:
        w, h = cells[k]['w'] + 2 * GUTTER, cells[k]['h'] + 2 * GUTTER
        if x + w > W:
            x, y, shelf = 0, y + shelf, 0
        cells[k]['x'], cells[k]['ytop'] = x + GUTTER, y + GUTTER
        x += w
        shelf = max(shelf, h)
    H = y + shelf
    H = (H + 3) // 4 * 4
    atlas = np.zeros((H, W, 4), np.uint8)
    for c in cells:
        atlas[c['ytop']:c['ytop'] + c['h'], c['x']:c['x'] + c['w']] = to8(c['img'])
    Image.fromarray(atlas, 'RGBA').save(os.path.join(d, name + '.png'), optimize=True)
    # What the game downloads: WebP, the colour lossy (quality 90) but the
    # alpha codes lossless (exact), about a third of the PNG.
    Image.fromarray(atlas, 'RGBA').save(os.path.join(d, name + '.webp'), quality=90, alpha_quality=100, method=6, exact=True)
    table = [[c['x'], H - (c['ytop'] + c['h']), c['w'], c['h'], round(c['u0'], 5), round(c['v0'], 5), round(c['w'] / ppm, 5), round(c['h'] / ppm, 5)] for c in cells]
    with open(os.path.join(d, name + '.json'), 'w') as fh:
        json.dump({'type': number, 'name': name, 'ppm': ppm, 'size': [W, H], 'cells': table}, fh, separators=(',', ':'))
    # Contact sheets: views down, frames across, feet on a common line.
    for suffix, pv in (('_color', previews), ('_mask', mpreviews)):
        grid(pv, ppm, os.path.join(d, name + suffix + '.png'))
    print('%s: atlas %dx%d, %d KB png, %d KB webp' % (name, W, H, os.path.getsize(os.path.join(d, name + '.png')) // 1024,
                                                      os.path.getsize(os.path.join(d, name + '.webp')) // 1024))


def grid(cells, ppm, path, bg=None):
    left = min(u0 for _, u0, _ in cells.values())
    right = max(u0 + im.shape[1] / ppm for im, u0, _ in cells.values())
    bottom = min(v0 for _, _, v0 in cells.values())
    top = max(v0 + im.shape[0] / ppm for im, _, v0 in cells.values())
    cw = int(np.ceil((right - left) * ppm)) + 4
    ch = int(np.ceil((top - bottom) * ppm)) + 4
    out = np.zeros((ch * VIEWS, cw * FRAMES, 4), np.float32)
    if bg is not None:
        out[...] = bg
    for (v, f), (im, u0, v0) in cells.items():
        x = int(round((u0 - left) * ppm)) + 2 + f * cw
        y = int(round((top - (v0 + im.shape[0] / ppm)) * ppm)) + 2 + v * ch
        region = out[y:y + im.shape[0], x:x + im.shape[1]]
        a = im[..., 3:4]
        region[...] = im * a + region * (1 - a) if bg is not None else np.where(a > 0, im, region)
    Image.fromarray(to8(out), 'RGBA').save(path, optimize=True)


def tile(paths, out, bg=(0.33, 0.4, 0.27)):
    ims = [Image.open(p).convert('RGBA') for p in paths]
    W = sum(i.width for i in ims) + 8 * (len(ims) + 1)
    H = max(i.height for i in ims) + 16
    o = Image.new('RGBA', (W, H), tuple(int(c * 255) for c in bg) + (255,))
    x = 8
    for i in ims:
        o.alpha_composite(i, (x, H - 8 - i.height))
        x += i.width + 8
    o.save(out)
