"""Reading and writing the game's sprite sheets (the format of
tools/blender/sprites/pack.py: one RGBA atlas whose alpha is a code -- 255
opaque, 191 team colour, 153 outline, 0 empty -- and a JSON cell table of
[x, y-from-bottom, w, h, u0, v0, uw, vh] rows)."""

import json
import os

import numpy as np
from PIL import Image

GUTTER = 2


def read(json_path):
    """A sheet as cells: each {'img': HxWx4 uint8 (top row first), 'u0', 'v0', 'w', 'h'} or None (empty gib slot)."""
    with open(json_path) as fh:
        meta = json.load(fh)
    atlas = np.asarray(Image.open(os.path.splitext(json_path)[0] + '.webp').convert('RGBA'))
    H = atlas.shape[0]
    cells = []
    for x, y, w, h, u0, v0, uw, vh in meta['cells']:
        if w <= 0 or h <= 0:
            cells.append(None)
            continue
        x, y, w, h = int(x), int(y), int(w), int(h)
        top = H - (y + h)
        cells.append({'img': atlas[top:top + h, x:x + w].copy(), 'u0': u0, 'v0': v0, 'w': w, 'h': h, 'uw': uw, 'vh': vh})
    meta.setdefault('framesPerView', 26)
    return meta, cells


def snap_codes(img):
    """Alpha back to its exact codes (lossy colour never touches alpha, but be sure)."""
    a = img[..., 3]
    out = img.copy()
    out[..., 3] = np.where(a > 223, 255, np.where(a > 170, 191, np.where(a > 100, 153, 0))).astype(np.uint8)
    out[out[..., 3] == 0] = 0
    return out


def write(out_dir, name, number, ppm, cells, frames_per_view, extra=None, max_side=4096):
    """Packs cells (list as in read(); an int entry k means "the same pixels as
    cell k", None an empty slot) into <out_dir>/<name>.webp and .json. Returns (W, H)."""
    real = [k for k, c in enumerate(cells) if isinstance(c, dict)]
    area = sum((cells[k]['w'] + 2 * GUTTER) * (cells[k]['h'] + 2 * GUTTER) for k in real)
    W = 256
    while W < max_side and W * W < area * 1.2:
        W *= 2
    order = sorted(real, key=lambda k: (-cells[k]['h'], k))
    x = y = shelf = 0
    place = {}
    for k in order:
        w, h = cells[k]['w'] + 2 * GUTTER, cells[k]['h'] + 2 * GUTTER
        if x + w > W:
            x, y, shelf = 0, y + shelf, 0
        place[k] = (x + GUTTER, y + GUTTER)
        x += w
        shelf = max(shelf, h)
    H = (y + shelf + 3) // 4 * 4
    if H > max_side:
        raise ValueError('%s: atlas %dx%d is over %d px' % (name, W, H, max_side))
    atlas = np.zeros((H, W, 4), np.uint8)
    for k in real:
        c = cells[k]
        px, top = place[k]
        atlas[top:top + c['h'], px:px + c['w']] = c['img']
    rows = []
    for k, c in enumerate(cells):
        while isinstance(c, int):
            k, c = c, cells[c]
        if c is None:
            rows.append([0, 0, 0, 0, 0, 0, 0, 0])
            continue
        px, top = place[k]
        rows.append([px, H - (top + c['h']), c['w'], c['h'], round(c['u0'], 5), round(c['v0'], 5),
                     round(c['w'] / ppm, 5), round(c['h'] / ppm, 5)])
    os.makedirs(out_dir, exist_ok=True)
    # Colour lossy, alpha codes exact (as pack.py).
    Image.fromarray(atlas, 'RGBA').save(os.path.join(out_dir, name + '.webp'), quality=82, alpha_quality=100, method=6, exact=True)
    meta = {'type': number, 'name': name, 'ppm': round(float(ppm), 3), 'size': [W, H], 'framesPerView': frames_per_view}
    meta.update(extra or {})
    meta['cells'] = rows
    with open(os.path.join(out_dir, name + '.json'), 'w') as fh:
        json.dump(meta, fh, separators=(',', ':'))
    return W, H
