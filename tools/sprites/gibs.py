#!/usr/bin/env python3
"""Gib parts cut from a unit's own pictures, for the parts the artist has not drawn.

    python3 tools/sprites/gibs.py ART_DIR [--units a,b] --out DIR

writes DIR/<unit>/gibs/<part>_<a|b>.png (a delivery import.py takes as it is,
for review); import.py calls cut() itself for every gib part a unit lacks.

A blown-apart soldier flies as pieces of his own picture: the head, the
trunk, an arm, a leg, gobbets, the weapon and the shield are cut from his
idle picture seen from behind (the a looks) and from in front (the b looks;
the shield's painted face is a, its back b), each by a fixed outline in
proportion to the figure: across and up in heights of the figure from its
feet to the top of its head, from the middle of the head. Where a cut goes
through the body, the edge is bloody; weapons and shields come away clean.
Only man-shaped types are cut (head, trunk, arms and legs; no mount); the
rest keep their old 3D parts until their gibs are drawn. The output depends
only on the pictures.
"""

import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import layout  # noqa: E402

BLOOD = np.array([0.42, 0.03, 0.02], np.float32)
BLOOD_EDGE = 0.025  # how deep a cut's blood goes, in figure heights
CLEAN = ('weapon', 'shield')  # parts that come away without blood

# Outlines (u across, w up; see the module's doc) by part and look: the view
# and frame cut from, and a polygon, or ('blob', u, w, r) for a gobbet.
BACK, FRONT = 4, 0
CUTS = {
    ('head', 0): (BACK, [(-0.09, 0.80), (0.09, 0.80), (0.12, 0.95), (0.15, 1.45), (-0.15, 1.45), (-0.12, 0.95)]),
    ('head', 1): (FRONT, [(-0.085, 0.825), (0.085, 0.825), (0.12, 0.95), (0.15, 1.45), (-0.15, 1.45), (-0.12, 0.95)]),
    ('torso', 0): (BACK, [(-0.10, 0.82), (0.10, 0.82), (0.155, 0.77), (0.13, 0.48), (-0.13, 0.42), (-0.155, 0.77)]),
    ('torso', 1): (BACK, [(-0.10, 0.83), (0.10, 0.83), (0.16, 0.78), (0.16, 0.32), (0.02, 0.38), (-0.16, 0.29), (-0.16, 0.78)]),
    ('arm', 0): (BACK, [(0.10, 0.81), (0.21, 0.81), (0.28, 0.44), (0.15, 0.40), (0.10, 0.60)]),
    ('arm', 1): (FRONT, [(-0.29, 0.80), (-0.10, 0.80), (-0.10, 0.44), (-0.31, 0.40)]),
    ('leg', 0): (BACK, [(0.0, 0.40), (0.15, 0.40), (0.16, -0.06), (-0.01, -0.06)]),
    ('leg', 1): (FRONT, [(-0.14, 0.36), (0.0, 0.36), (0.01, -0.06), (-0.15, -0.06)]),
    ('chunk', 0): (BACK, ('blob', 0.0, 0.62, 0.085)),
    ('chunk', 1): (BACK, ('blob', 0.05, 0.73, 0.07)),
    ('weapon', 0): (BACK, [(0.19, 0.47), (0.7, 0.47), (0.7, -0.06), (0.19, -0.06)]),
    ('weapon', 1): (FRONT, [(-0.7, 0.45), (-0.18, 0.45), (-0.18, -0.06), (-0.7, -0.06)]),
    ('shield', 0): (FRONT, [(-0.10, 0.81), (0.29, 0.77), (0.25, 0.50), (-0.04, 0.37), (-0.13, 0.57)]),
    ('shield', 1): (BACK, [(-0.32, 0.96), (-0.08, 0.96), (-0.08, 0.54), (-0.32, 0.54)]),
}


def cuttable(unit):
    """Whether the unit is man-shaped enough for the outlines (head, trunk, arms and legs; no mount)."""
    parts = layout.UNITS[unit][6]
    return all(p in parts for p in ('head', 'torso', 'arm', 'leg')) and 'mount_head' not in parts


def polygon_mask(shape, pts):
    """Pixels whose centres lie inside the polygon (pixel coordinates), even-odd."""
    H, W = shape
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    inside = np.zeros(shape, bool)
    n = len(pts)
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        if y0 == y1:
            continue
        crosses = (ys >= min(y0, y1)) & (ys < max(y0, y1))
        xc = x0 + (ys - y0) * (x1 - x0) / (y1 - y0)
        inside ^= crosses & (xs < xc)
    return inside


def blob_mask(shape, cx, cy, r):
    """A gobbet: a disc with a ragged edge (fixed, so the output is the same each time)."""
    H, W = shape
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    a = np.arctan2(ys - cy, xs - cx)
    rr = r * (1 + 0.22 * np.sin(3 * a + 0.7) + 0.13 * np.sin(5 * a + 2.1) + 0.08 * np.sin(9 * a))
    return (xs - cx) ** 2 + (ys - cy) ** 2 < rr ** 2


def grow(mask, steps):
    """The mask grown by `steps` pixels (a square at a time), with the distance each pixel was reached at."""
    dist = np.where(mask, 0, steps + 1).astype(np.int32)
    m = mask.copy()
    for s in range(1, steps + 1):
        g = m.copy()
        g[1:] |= m[:-1]
        g[:-1] |= m[1:]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        dist[g & ~m] = s
        m = g
    return m, dist


def largest_piece(mask):
    """The mask's largest 8-connected piece (a cut leaves specks of its neighbours)."""
    rows, cols = np.where(mask.any(axis=1))[0], np.where(mask.any(axis=0))[0]
    if len(rows) == 0:
        return mask
    r0, c0 = rows[0], cols[0]
    m = mask[r0:rows[-1] + 1, c0:cols[-1] + 1]
    left = m.copy()
    best, best_n = None, 0
    while left.any():
        ys, xs = np.nonzero(left)
        piece = np.zeros_like(m)
        piece[ys[0], xs[0]] = True
        while True:
            g = piece.copy()
            g[1:] |= piece[:-1]
            g[:-1] |= piece[1:]
            g[:, 1:] |= g[:, :-1]
            g[:, :-1] |= g[:, 1:]
            g &= m
            if (g == piece).all():
                break
            piece = g
        left &= ~piece
        n = int(piece.sum())
        if n > best_n:
            best, best_n = piece, n
    out = np.zeros_like(mask)
    out[r0:r0 + m.shape[0], c0:c0 + m.shape[1]] = best
    return out


def figure_frame(alpha, team, ppm, head, feet):
    """The middle of the head (x), the feet's line and the figure's height, in pixels."""
    h = head({'alpha': alpha, 'team': team}, ppm)
    f = feet(alpha, ppm)
    if h is None or f is None:
        return None
    return h[0], f[0][1], f[0][1] - h[1]


def cut(pictures, unit, part, variant, ppm, canvas, head, feet):
    """The part as a canvas-sized RGBA float picture (straight colour), the
    part centred, or None. pictures(view) gives the keyed RGBA and team share
    of the unit's idle frame 0 in that view (or None); head and feet are
    import.py's."""
    if not cuttable(unit) or (part, variant) not in CUTS:
        return None
    view, outline = CUTS[(part, variant)]
    got = pictures(view)
    if got is None:
        return None
    im, team = got
    alpha = im[..., 3]
    frame = figure_frame(alpha, team, ppm, head, feet)
    if frame is None:
        return None
    cx, ground, tall = frame

    def px(u, w):
        return cx + u * tall, ground - w * tall

    if outline[0] == 'blob':
        x, y = px(outline[1], outline[2])
        region = blob_mask(alpha.shape, x, y, outline[3] * tall)
    else:
        region = polygon_mask(alpha.shape, [px(u, w) for u, w in outline])
    solid = alpha > 0.5
    keep = largest_piece(region & (alpha > 0.02))
    if keep.sum() < 16:
        return None
    rgb = im[..., :3].copy()
    a = np.where(keep, alpha, 0.0)
    if part not in CLEAN:
        # Blood where the cut went through the body: near figure left behind.
        depth = max(2, int(round(BLOOD_EDGE * tall)))
        _, dist = grow(solid & ~region, depth)
        k = np.clip(1.0 - (dist - 1) / depth, 0.0, 1.0) * keep * 0.85
        # Cloth of the side's colour is soaked or left as it is (the key's blue
        # half bloodied would turn purple, not the side's colour).
        cloth = team > 0.5
        k = np.where(cloth, np.where(k > 0.35, 1.0, 0.0), k)
        blood = np.broadcast_to(BLOOD, rgb.shape).copy()
        blood[cloth] = BLOOD * (0.55 + 0.5 * rgb[cloth].max(axis=1, keepdims=True))
        if part == 'chunk':
            # Mostly gore: the blood keeps the picture's shading.
            blood = BLOOD * (0.5 + rgb @ np.array([0.3, 0.59, 0.11], np.float32))[..., None]
            k = np.maximum(k, np.where(cloth, 1.0, 0.9) * keep)
        rgb = rgb * (1 - k[..., None]) + blood * k[..., None]
    rows, cols = np.where(a.max(axis=1) > 0)[0], np.where(a.max(axis=0) > 0)[0]
    r0, r1, c0, c1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
    W, H = canvas
    if r1 - r0 > H or c1 - c0 > W:
        return None
    out = np.zeros((H, W, 4), np.float32)
    top, left = (H - (r1 - r0)) // 2, (W - (c1 - c0)) // 2
    out[top:top + r1 - r0, left:left + c1 - c0, :3] = rgb[r0:r1, c0:c1]
    out[top:top + r1 - r0, left:left + c1 - c0, 3] = a[r0:r1, c0:c1]
    return out


def main(argv=None):
    import importlib.util
    spec = importlib.util.spec_from_file_location('sprite_import', os.path.join(HERE, 'import.py'))
    imp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(imp)
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('art', help='the folder of unit folders')
    ap.add_argument('--units', help='comma-separated unit ids (default: every folder in ART the manifest knows)')
    ap.add_argument('--out', required=True, help='where to write <unit>/gibs/*.png')
    args = ap.parse_args(argv)
    units = args.units.split(',') if args.units else sorted(d for d in os.listdir(args.art) if d in layout.UNITS)
    for unit in units:
        k = layout.unit_class(unit)
        pics = imp.idle_pictures(args.art, unit)
        written = []
        for part in layout.UNITS[unit][6]:
            for v in range(layout.GIB_VARIANTS):
                im = cut(pics, unit, part, v, float(k['ppm']), layout.GIB_CANVAS, imp.head, imp.feet)
                if im is None:
                    continue
                path = os.path.join(args.out, unit, layout.gib_file(part, v))
                os.makedirs(os.path.dirname(path), exist_ok=True)
                Image.fromarray((np.clip(im, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(path)
                written.append(os.path.basename(path)[:-4])
        print('%s: %s' % (unit, ', '.join(written) if written else 'nothing cut'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
