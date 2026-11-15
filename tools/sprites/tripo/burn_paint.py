"""Paint a burning death over rendered cells, as the knight's painted one burns: tongues of fire licking up off the
body (white-yellow at the root, orange, deep red at the tips), the clothes charring black from the first frame to
the last, dark smoke over it, and at the end a blackened body smouldering. The team mask loses what has burnt.

Every pixel stays opaque or empty (the sheets keep no soft alpha), so fire and smoke are solid shapes. The noise is
one tall field per view, read a little higher each frame, so the tongues climb from frame to frame.

    python3 burn_paint.py CLIP_DIR [--views 0,1,...] [--seed N]
reads CLIP_DIR/v<V>_f<NN>.png and _team.png and writes them over (the originals go to CLIP_DIR/unburnt/ once)."""
import glob
import math
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

PPM = float(sys.argv[sys.argv.index('--ppm') + 1]) if '--ppm' in sys.argv else 192.0   # the render's pixels a metre
TONGUE_M = 0.5               # how far a tongue reaches above what burns
SMOKE_M = 0.55               # and the smoke above that


def fire_at(t):
    """How much of him is alight, 0..1, through the clip (t 0..1): caught at once, ablaze, then smouldering."""
    return float(np.interp(t, [0.0, 0.12, 0.35, 0.65, 0.85, 1.0], [0.55, 0.9, 1.0, 0.85, 0.45, 0.22]))


def char_at(t):
    """The share of him charred black: little while the fire is young, nearly all at the end."""
    return float(np.interp(t, [0.0, 0.3, 0.6, 0.85, 1.0], [0.0, 0.12, 0.42, 0.8, 0.93]))


def smoke_at(t):
    return float(np.interp(t, [0.0, 0.3, 0.7, 1.0], [0.25, 0.45, 0.7, 0.8]))


def fbm(shape, rng, octaves=4, base=24):
    """Fractal value noise 0..1, `base` px at its coarsest."""
    h, w = shape
    out = np.zeros(shape, np.float32)
    amp, total = 1.0, 0.0
    cell = base
    for _ in range(octaves):
        gh, gw = max(2, h // cell + 2), max(2, w // cell + 2)
        g = rng.random((gh, gw)).astype(np.float32)
        up = np.asarray(Image.fromarray(g).resize((gw * cell, gh * cell), Image.BICUBIC))[:h, :w]
        out += amp * up
        total += amp
        amp *= 0.55
        cell = max(2, cell // 2)
    out /= total
    return np.clip((out - out.min()) / max(1e-6, out.max() - out.min()), 0, 1)


def rise(src, px, falloff=1.0):
    """Each pixel's heat from what burns below it, within `px`, fading with height (a flame's reach)."""
    out = src.copy()
    step = 2
    for d in range(step, px + 1, step):
        shifted = np.zeros_like(src)
        shifted[:-d] = src[d:]
        out = np.maximum(out, shifted * (1.0 - (d / px) ** falloff))
    return out


RAMP = np.array([  # heat 0..1 -> colour
    [0.00, 0.30, 0.03, 0.01],
    [0.25, 0.62, 0.10, 0.02],
    [0.50, 0.93, 0.36, 0.04],
    [0.72, 1.00, 0.62, 0.12],
    [0.88, 1.00, 0.86, 0.38],
    [1.00, 1.00, 0.97, 0.78]], np.float32)


def ramp(h):
    return np.stack([np.interp(h, RAMP[:, 0], RAMP[:, i]) for i in (1, 2, 3)], axis=-1)


def paint(rgba, team, t, field, smoke_field, tongues, rng):
    """One frame: rgba float 0..1 (H, W, 4), team bool (H, W); field/smoke_field/tongues: this frame's slice of the
    noise (tongues: stretched tall, so the fire breaks into licks)."""
    h, w = team.shape
    body = rgba[..., 3] > 0.5
    rgb = rgba[..., :3].copy()
    alight, charred, smoky = fire_at(t), char_at(t), smoke_at(t)

    # Char: the clothes and skin blacken in patches, more as it goes; burnt cloth is no longer his colour.
    patch = ndimage.gaussian_filter(field, 3)
    share = lambda q: np.quantile(patch[body], min(1.0, max(0.0, q))) if body.any() else 0.0  # noqa: E731
    burnt = body & (patch < share(charred)) if charred > 0 else np.zeros_like(body)
    scorch = body & (patch < share(charred + 0.2)) & ~burnt
    soot = np.array([0.07, 0.055, 0.045], np.float32)
    rgb[burnt] = rgb[burnt] * 0.12 + soot * 0.88
    rgb[scorch] = rgb[scorch] * 0.45 + soot * 0.55
    # Embers in the black, late on.
    if t > 0.55:
        ember = burnt & (field > 0.86 - 0.1 * (t - 0.55))
        rgb[ember] = ramp(np.full(ember.sum(), 0.45 + 0.3 * rng.random()))
    team_out = team & ~burnt

    # What burns: patches of him (a third at most, while he is ablaze); from each the fire licks up in tongues,
    # thinning as they climb. On him only the hottest of it shows, so he is seen through it.
    inner = ndimage.binary_erosion(body, iterations=3)
    lumpy = ndimage.gaussian_filter(field, 2)
    seed = inner & (lumpy > np.quantile(lumpy[inner], 1.0 - 0.34 * alight)) if inner.any() else inner
    heat = np.minimum(1.0, ndimage.gaussian_filter(seed.astype(np.float32), 3.0) * 1.6)
    heat = rise(heat, int(TONGUE_M * PPM * (0.6 + 0.4 * alight)), falloff=0.9)
    heat = heat * (0.15 + 1.35 * tongues)
    flame = (heat > 0.5) & (~body | (heat > 0.66))
    flame = ndimage.binary_opening(flame, iterations=1)
    core = np.clip((heat - 0.5) / 0.6, 0, 1)

    # Smoke: dark puffs above the fire, drifting; opaque, ragged at the edge.
    lift = int(0.18 * PPM)
    above = np.zeros_like(heat)
    above[:-lift] = ndimage.gaussian_filter((heat > 0.35).astype(np.float32), 5)[lift:]
    puff = ndimage.gaussian_filter(rise(above, int(SMOKE_M * PPM), falloff=0.7), 6)
    puff = puff * (0.3 + 1.0 * smoke_field)
    smoke = (puff > 0.52 - 0.14 * smoky) & ~flame & ~body
    smoke = ndimage.binary_opening(smoke, iterations=3)
    grey = 0.13 + 0.14 * smoke_field

    out_rgb = rgb
    out_a = body.astype(np.float32)
    out_rgb[smoke] = np.stack([grey[smoke], grey[smoke] * 0.97, grey[smoke] * 0.93], axis=-1)
    out_a[smoke] = 1.0
    out_rgb[flame] = ramp(core[flame])
    out_a[flame] = 1.0
    team_out = team_out & ~flame
    return np.concatenate([out_rgb, out_a[..., None]], axis=-1), team_out


def main():
    d = sys.argv[1]
    views = [int(v) for v in sys.argv[sys.argv.index('--views') + 1].split(',')] if '--views' in sys.argv else None
    seed = int(sys.argv[sys.argv.index('--seed') + 1]) if '--seed' in sys.argv else 7
    keep = os.path.join(d, 'unburnt')
    os.makedirs(keep, exist_ok=True)
    cells = {}
    for p in sorted(glob.glob(os.path.join(d, 'v*_f*.png'))):
        m = re.match(r'v(\d+)_f(\d+)\.png$', os.path.basename(p))
        if m:
            cells.setdefault(int(m.group(1)), []).append((int(m.group(2)), p))
    for v, frames in sorted(cells.items()):
        if views is not None and v not in views:
            continue
        frames.sort()
        n = len(frames)
        rng = np.random.default_rng(seed * 101 + v)
        first = np.asarray(Image.open(frames[0][1]))
        h, w = first.shape[:2]
        climb = 26  # px the tongues climb a frame
        tall = fbm((h + climb * n + 8, w), rng, octaves=5, base=28)
        tall_smoke = fbm((h + climb * n + 8, w), rng, octaves=3, base=44)
        # Tongues: noise three times as tall as it is wide.
        th = (h + climb * n + 8) // 3 + 2
        squat = fbm((th, w), rng, octaves=4, base=14)
        tall_tongues = np.asarray(Image.fromarray(squat).resize((w, th * 3), Image.BICUBIC))[:h + climb * n + 8]
        for f, p in frames:
            src = os.path.join(keep, os.path.basename(p))
            src_team = src.replace('.png', '_team.png')
            if not os.path.exists(src):
                shutil.copy(p, src)
                shutil.copy(p.replace('.png', '_team.png'), src_team)
            rgba = np.asarray(Image.open(src).convert('RGBA')).astype(np.float32) / 255
            team = np.asarray(Image.open(src_team).convert('L')) > 127
            t = f / max(1, n - 1)
            off = climb * (n - 1 - f)  # read higher each frame: the field climbs
            field = tall[off:off + h]
            smoke_field = tall_smoke[off:off + h]
            tongues = tall_tongues[off:off + h]
            out, team_out = paint(rgba, team, t, field, smoke_field, tongues, np.random.default_rng(seed * 997 + v * 31 + f))
            Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(p)
            Image.fromarray((team_out * 255).astype(np.uint8), 'L').save(p.replace('.png', '_team.png'))
            print('v%d f%02d t %.2f' % (v, f, t), flush=True)


if __name__ == '__main__':
    main()
