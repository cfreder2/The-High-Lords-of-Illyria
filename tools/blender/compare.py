"""Old against new: a type's runtime-baked sheet (read out of the game with
grab_sheets.js) beside its pre-rendered one, shown as the battle shows them
(team colour applied, the cutout, nearest-pixel magnification) at the same
size in metres, once at in-game size and once three times larger.

    python3 tools/blender/compare.py <grab dir> <type> <new dir>/<name> out.png [px_per_metre]

Needs numpy and Pillow (any Python).
"""

import json
import sys

import numpy as np
from PIL import Image, ImageDraw

FRAMES = 26
TEAM = (0.22, 0.42, 0.92)  # kPlayerColor
GRASS = (0.31, 0.37, 0.22)
# Which cells: (view, frame) -- idle from all five views, then walk, attack and death.
CELLS = [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (1, 6), (2, 8), (1, 13), (2, 14), (1, 23), (2, 25)]


def cloth(t):
    g = sum(t) / 3
    return np.array([c + (g - c) * 0.25 for c in t], np.float32)


def load_old(d, t):
    meta = json.load(open('%s/%d.json' % (d, t)))
    w, h = meta['size']
    raw = np.frombuffer(open('%s/%d.rgba' % (d, t), 'rb').read(), np.uint8).reshape(h, w, 4)[::-1]  # top row first
    return raw, [meta['cells'][c * 4:c * 4 + 4] + meta['cells'][(130 + c) * 4:(130 + c) * 4 + 4] for c in range(130)]


def load_new(path):
    meta = json.load(open(path + '.json'))
    img = np.asarray(Image.open(path + '.png').convert('RGBA'))
    return img, meta['cells']


def cell(img, row):
    x, y, w, h = (int(round(v)) for v in row[:4])
    H = img.shape[0]
    return img[H - (y + h):H - y, x:x + w], row[4], row[5], row[6], row[7]


def shade(px):
    """kUnitFs: discard below 0.5 alpha, team pixels times cloth(team)."""
    a = px[..., 3] / 255.0
    rgb = px[..., :3] / 255.0
    team = (a > 0.68) & (a < 0.88)
    rgb = np.where(team[..., None], rgb * cloth(TEAM), rgb) * 0.95
    return rgb, a >= 0.5


def compose(sheets, labels, scale, path_out):
    """sheets: [(img, cells)], each a row; drawn at scale px a metre, feet on a line."""
    rows = []
    for img, cells in sheets:
        items = []
        for v, f in CELLS:
            px, u0, v0, uw, vh = cell(img, cells[v * FRAMES + f])
            items.append((px, u0, v0, uw, vh))
        rows.append(items)
    # Column widths and row heights in metres, common to both rows.
    left = [min(r[k][1] for r in rows) for k in range(len(CELLS))]
    right = [max(r[k][1] + r[k][3] for r in rows) for k in range(len(CELLS))]
    top = max(it[2] + it[4] for r in rows for it in r)
    bottom = min(it[2] for r in rows for it in r)
    colw = [int(np.ceil((right[k] - left[k]) * scale)) + 6 for k in range(len(CELLS))]
    rowh = int(np.ceil((top - bottom) * scale)) + 8
    pad = 70
    W = pad + sum(colw)
    out = np.zeros((rowh * len(rows), W, 3), np.float32)
    out[...] = GRASS
    for r, items in enumerate(rows):
        x0 = pad
        for k, (px, u0, v0, uw, vh) in enumerate(items):
            w = max(1, int(round(uw * scale)))
            h = max(1, int(round(vh * scale)))
            im = np.asarray(Image.fromarray(px, 'RGBA').resize((w, h), Image.NEAREST))
            rgb, solid = shade(im)
            ox = x0 + 3 + int(round((u0 - left[k]) * scale))
            oy = r * rowh + 4 + int(round((top - (v0 + vh)) * scale))
            region = out[oy:oy + h, ox:ox + w]
            region[solid] = rgb[solid]
            x0 += colw[k]
    img = Image.fromarray(np.clip(out * 255 + 0.5, 0, 255).astype(np.uint8), 'RGB')
    d = ImageDraw.Draw(img)
    for r, label in enumerate(labels):
        d.text((6, r * rowh + 6), label, fill=(235, 235, 220))
    img.save(path_out, optimize=True)
    return img


def main(argv):
    grab, t, new, out = argv[0], int(argv[1]), argv[2], argv[3]
    ppm = float(argv[4]) if len(argv) > 4 else 48.0
    old = load_old(grab, t)
    nw = load_new(new)
    a = compose([old, nw], ['old', 'new'], ppm, out.replace('.png', '_1x.png'))
    b = compose([old, nw], ['old', 'new'], ppm * 3, out.replace('.png', '_3x.png'))
    print(out, a.size, b.size)


if __name__ == '__main__':
    main(sys.argv[1:])
