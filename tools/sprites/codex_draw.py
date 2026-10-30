#!/usr/bin/env python3
"""Draw sprite cells with the codex CLI (docs/SPRITE_SPEC.md), one picture a cell.

    python3 tools/sprites/codex_draw.py UNIT [--clip idle] [--views 0-4] [--frames 0-3]
        [--out art/codex] [--identity FILE] [--style FILE] [--force] [--dry-run]

Each cell goes to codex with its full-size pose guide (today's figure at the
delivery scale and anchor, from templates.py) as Image 1, and optionally the
character as already drawn (--identity) and a picture to match the painting of
(--style). Codex returns 1254x1254 whatever is asked and does not hold a size or
a position, so the result is fitted rather than trusted: keyed, scaled so the
figure is as tall as the guide's (as long, for a body lying down), and moved so
its feet stand where the guide's do. The raw plates and the guides sent go to
a work folder beside the art (OUT-work/), so OUT holds only what import.py takes.

Cells already drawn are skipped, so a run can be stopped and resumed. One
generation at a time: the plate is found by watching ~/.codex/generated_images.

Then: python3 tools/sprites/import.py art/codex --check
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

import layout  # noqa: E402
import templates  # noqa: E402


CODEX_IMAGES = os.path.expanduser('~/.codex/generated_images')

# Below this overlap between the drawn silhouette and the guide's, a frame is
# flagged for a look (tuned on the first plates; a faithful redraw scores ~0.6-0.8).
MIN_OVERLAP = 0.5
# light_side() depends on what a figure is made of (bright steel on one edge, a dark
# tabard on the other), so it is not judged against a fixed number. Every frame of a
# unit is lit alike, though, so a frame this far from its clip's median is flagged.
LIGHT_SPREAD = 0.15

VIEW_PHRASE = (
    'Seen from the front: he faces the viewer.',
    'Seen from his front-right: a three-quarter front view, his body turned toward the right of the picture.',
    'Seen from his right side: he faces the right of the picture, in profile.',
    'Seen from his back-right: a three-quarter rear view, facing away from the viewer toward the right.',
    'Seen from behind: he faces away from the viewer.',
)

CLIP_PHRASE = {
    'idle': 'Standing at ease, frame {n} of 4 of a slow breathing loop with a small weight shift.',
    'walk': 'Walking, frame {n} of 8 of one full stride (both feet), evenly spaced.',
    'attack': 'Attacking, frame {n} of 6: frames 1-3 wind up, the blow lands on frame 4, frames 5-6 recover.',
    'flinch': 'Struck by a blow, frame {n} of 2: recoiling, then half recovered.',
    'die_back': 'Dying, frame {n} of 6: struck from the front, he staggers and falls on his back. Frame 6 is the body lying flat.',
    'die_front': 'Dying, frame {n} of 6: struck from behind, he pitches forward and lands face down. Frame 6 is the body lying flat.',
    'die_crumple': 'Dying, frame {n} of 6: cut down, his knees buckle first and he slumps sideways. Frame 6 is the body lying flat.',
    'die_burn': 'Dying in flames, frame {n} of 6: writhing, then collapsing. Frame 6 is a charred body lying flat.',
}


# The flat backdrop each unit is drawn on: green, unless the unit has green in it
# (elves, goblins, the green dragon, the healer's robes); then magenta, which no
# unit uses (the purple demons stay on green). The keyer removes the backdrop's
# tint from every pixel, so the art must never contain it.
BACKDROPS = {'archers': '#FF00FF', 'wolf_riders': '#FF00FF', 'dragons': '#FF00FF', 'journeyman': '#FF00FF'}


# What each unit holds and where: a rough pose guide shows a thin sword as a few grey
# pixels, and the generator sheathes or drops what it is not told about (the first
# heavy infantry plate came back with its sword in the scabbard and an empty hand).
ARMS = {
    'light_infantry': 'a spear in his right hand and a round shield on his left arm',
    'heavy_infantry': 'a drawn longsword in his right hand and the kite shield on his left arm',
    'archers': 'a longbow in his left hand, an arrow in his right, a quiver on his back',
    'pikemen': 'a very long pike, five metres, held in both hands',
    'light_cavalry': 'a lance in his right hand',
    'heavy_cavalry': 'a lance in his right hand and a shield on his left arm',
    'dwarves': 'a battle axe in his right hand and a round shield on his left arm',
    'wolf_riders': 'a spear in his right hand',
    'giants': 'a huge wooden club in his right hand',
    'minotaurs': 'a halberd held in both hands',
    'pegasi': 'the rider holds a lance in his right hand',
    'elephants': 'the crew in the howdah hold spears',
    'wizards': 'a tall staff in his right hand',
    'devils': 'a spear in his right hand',
    'undead': 'a rusty sword in his right hand and a round shield on his left arm',
    'archons': 'a sword in his right hand',
    'hero': 'a drawn sword in his right hand and a heater shield on his left arm',
    'dwarf_grenadier': 'a fused satchel bomb in his right hand',
    'journeyman': 'a staff in his right hand',
    'warrior': 'a drawn sword in his right hand and a heater shield on his left arm',
    'berserker': 'a two-handed claymore held in both hands',
}


def span(text, limit):
    """'0-3' or '0,2' or '' (all) as a list of ints below limit."""
    if not text:
        return list(range(limit))
    out = []
    for part in text.split(','):
        a, _, b = part.partition('-')
        out += list(range(int(a), int(b or a) + 1))
    return [i for i in out if 0 <= i < limit]


def lying(clip, frame):
    """A death's last frames: the body is longer than it is tall."""
    return clip.startswith('die_') and frame >= 3


def guide_pictures(unit, clip, view, frame):
    """(the pose guide as codex sees it, today's figure alone on a clear canvas)."""
    meta, cells = templates.source_sheet(unit)
    if not meta:
        raise SystemExit('%s: no sheet to make guides from' % unit)
    info = layout.UNITS[unit]
    k = layout.CLASSES[info[1]]
    W, H = k['canvas']
    ax, ay = k['anchor']
    c, mirror, ghost = templates.figure(cells, meta['framesPerView'], clip, view, frame)
    guide = templates.guides_layer(W, H, k['ppm'], ax, ay, info[2], fs=18)
    alone = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    if c is not None:
        templates.place(guide, c, mirror, float(meta['ppm']), k['ppm'], ax, ay, 0.4 if ghost else 1.0)
        templates.place(alone, c, mirror, float(meta['ppm']), k['ppm'], ax, ay)
    return guide.convert('RGB'), alone


def extent(alpha):
    """Box (x0, y0, x1, y1) of the solid figure and its feet (x, y), or None."""
    solid = alpha > 0.5
    rows = np.where(solid.sum(axis=1) >= 3)[0]
    cols = np.where(solid.sum(axis=0) >= 3)[0]
    if len(rows) == 0 or len(cols) == 0:
        return None
    y0, y1, x0, x1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
    # The feet: the middle of what touches the ground (the lowest 6% of the figure).
    band = solid[max(y0, y1 - max(2, (y1 - y0) // 16)):y1]
    xs = np.where(band.any(axis=0))[0]
    return (x0, y0, x1, y1), ((xs[0] + xs[-1] + 1) / 2.0, float(y1))


def key(plate_path):
    """The plate as straight RGBA floats with its flat backdrop keyed out and the spill taken off.

    A colour-distance key (import.py's) leaves a green rim: anti-aliased edges and
    fine strands (a plume, hair, fur) are part backdrop, and keep its tint at full
    opacity. So the key here is a green-screen one: opacity from how much MORE of
    the backdrop's colour a pixel has than a neutral one, the backdrop's tint taken
    out of every pixel (safe because each unit's backdrop is a colour its art does
    not use: BACKDROPS), and the outermost pixel choked away.
    """
    im = np.asarray(Image.open(plate_path).convert('RGB'), dtype=np.float32) / 255.0
    k = max(4, min(im.shape[:2]) // 64)
    corners = np.concatenate([im[:k, :k].reshape(-1, 3), im[:k, -k:].reshape(-1, 3),
                              im[-k:, :k].reshape(-1, 3), im[-k:, -k:].reshape(-1, 3)])
    bg = np.median(corners, axis=0)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    if bg[1] >= bg[0] and bg[1] >= bg[2]:  # a green backdrop: green above the other two is backdrop
        excess = g - np.maximum(r, b)
        full = bg[1] - max(bg[0], bg[2])
    else:  # magenta: red and blue above green
        excess = np.minimum(r, b) - g
        full = min(bg[0], bg[2]) - bg[1]
    t = np.clip(excess / max(full, 1e-3), 0.0, 1.0)  # 1: pure backdrop, 0: no backdrop tint
    alpha = np.clip((0.7 - t) / 0.5, 0.0, 1.0)
    # Choke one pixel: the outermost ring is mostly backdrop whatever its alpha says.
    a8 = Image.fromarray((alpha * 255).astype(np.uint8))
    alpha = np.asarray(a8.filter(ImageFilter.MinFilter(3)), dtype=np.float32) / 255.0
    # Despill everywhere: the backdrop shows through gaps (between an arm and the body)
    # as well as round the outline.
    spill = np.clip(excess, 0.0, None)
    rgb = im.copy()
    if bg[1] >= bg[0] and bg[1] >= bg[2]:
        rgb[..., 1] -= spill
    else:
        rgb[..., 0] -= spill
        rgb[..., 2] -= spill
    return np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], axis=2)


def resized(rgba, s):
    """Scaled by s with premultiplied alpha, so no backdrop colour bleeds into the edges."""
    pre = rgba.copy()
    pre[..., :3] *= pre[..., 3:4]
    h, w = pre.shape[:2]
    size = (max(1, round(w * s)), max(1, round(h * s)))
    chans = [np.asarray(Image.fromarray(pre[..., i].astype(np.float32), 'F').resize(size, Image.Resampling.LANCZOS))
             for i in range(4)]
    out = np.clip(np.stack(chans, axis=2), 0.0, 1.0)
    a = out[..., 3:4]
    out[..., :3] = np.where(a > 1e-4, out[..., :3] / np.maximum(a, 1e-4), 0.0)
    return np.clip(out, 0.0, 1.0)


def fit(plate_path, alone, clip, frame):
    """The plate keyed and fitted onto the delivery canvas over the guide's figure."""
    im = key(plate_path)
    got = extent(im[..., 3])
    want = extent(np.asarray(alone, dtype=np.float32)[..., 3] / 255.0)
    if got is None or want is None:
        raise RuntimeError('nothing to fit (an empty plate or no guide figure)')
    (gx0, gy0, gx1, gy1), (gfx, gfy) = got
    (wx0, wy0, wx1, wy1), (wfx, wfy) = want
    s = (wx1 - wx0) / (gx1 - gx0) if lying(clip, frame) else (wy1 - wy0) / (gy1 - gy0)
    # The feet are a first guess (staggered feet or a hanging blade fool them); then
    # slide and scale the drawing until its outline covers the guide's best.
    s, x, y = settle(im[..., 3], np.asarray(alone)[..., 3] > 127, s, (gfx, gfy), (wfx, wfy), lying(clip, frame))
    small = resized(im, s)
    rgba = Image.fromarray((small * 255 + 0.5).astype(np.uint8))
    out = Image.new('RGBA', alone.size, (0, 0, 0, 0))
    # alpha_composite needs a non-negative destination: crop what falls off the canvas.
    sx, sy = max(0, -x), max(0, -y)
    rgba = rgba.crop((sx, sy, rgba.width, rgba.height))
    out.alpha_composite(rgba, (x + sx, y + sy))
    return out, s


def settle(alpha, want, s, feet, spot, lying_down, ks=np.linspace(0.88, 1.12, 13), reach=None):
    """Scale and place (s, x, y) of a drawing whose solid pixels best cover the guide's
    silhouette `want`: the drawing scaled by s about its feet (feet, in its own pixels)
    standing on spot is the first guess, then scale (s times each of ks) and offset are
    searched round it on a quarter-size grid (reach: how far, in those pixels). One
    scale for both axes: the drawing is moved, never stretched."""
    q = 4
    W, H = want.shape[1] // q, want.shape[0] // q
    target = np.asarray(Image.fromarray(want.astype(np.uint8) * 255).resize((W, H), Image.Resampling.BOX)) > 127
    tsum = int(target.sum())
    src = Image.fromarray((np.clip(alpha, 0, 1) * 255).astype(np.uint8))
    best = (-1.0, s, round(spot[0] - feet[0] * s), round(spot[1] - feet[1] * s))
    for k in ks:
        sk = s * k
        w, h = max(1, round(src.width * sk / q)), max(1, round(src.height * sk / q))
        m = np.asarray(src.resize((w, h), Image.Resampling.BOX)) > 127
        msum = int(m.sum())
        x0, y0 = (spot[0] - feet[0] * sk) / q, (spot[1] - feet[1] * sk) / q
        rx, ry = reach or (12, 10 if lying_down else 6)  # in quarter-size pixels
        for dx in range(-rx, rx + 1):
            for dy in range(-ry, ry + 1):
                ox, oy = int(round(x0)) + dx, int(round(y0)) + dy
                cx0, cy0, cx1, cy1 = max(0, ox), max(0, oy), min(W, ox + w), min(H, oy + h)
                if cx1 <= cx0 or cy1 <= cy0:
                    continue
                inter = int((m[cy0 - oy:cy1 - oy, cx0 - ox:cx1 - ox] & target[cy0:cy1, cx0:cx1]).sum())
                score = inter / max(1, msum + tsum - inter)
                if score > best[0]:
                    best = (score, sk, ox * q, oy * q)
    return best[1], best[2], best[3]


def agreement(out, alone):
    """How well the drawn figure covers the guide's (intersection over union of the
    two silhouettes), and the same against the guide mirrored: a frame drawn the wrong
    way round matches the mirror better. Coarse on purpose: a 1990s-style redraw is
    meant to differ in detail, not in where its sword is."""
    a = np.asarray(out)[..., 3] > 127
    b = np.asarray(alone)[..., 3] > 127

    def iou(m):
        u = (a | m).sum()
        return float((a & m).sum()) / u if u else 0.0
    ys, xs = np.where(b)
    if len(xs) == 0:
        return 0.0, 0.0
    # Mirror about the guide's own feet (its anchor), not the canvas centre.
    ext = extent(b.astype(np.float32))
    fx = ext[1][0] if ext else b.shape[1] / 2
    mirror = np.zeros_like(b)
    mx = np.clip(np.round(2 * fx - xs).astype(int), 0, b.shape[1] - 1)
    mirror[ys, mx] = True
    return iou(b), iou(mirror)


def light_side(out):
    """Which side the light comes from, judged at the outline: along each row the
    surfaces just inside the left edge face left and those inside the right edge face
    right, so with the spec's upper-left key light the left band is the brighter.
    Returns (left - right) / (left + right) brightness: positive is lit from the left.
    The outermost 2 px are skipped (the spec's thin rim light lives there)."""
    f = np.asarray(out).astype(np.float32) / 255.0
    solid = f[..., 3] > 0.5
    lum = 0.3 * f[..., 0] + 0.59 * f[..., 1] + 0.11 * f[..., 2]
    # Only uncoloured surfaces (steel, bone, cloth greys): a dark blue tabard on one
    # side and bright steel on the other is colour, not light.
    mx, mn = f[..., :3].max(axis=2), f[..., :3].min(axis=2)
    neutral = solid & ((mx - mn) < 0.3 * np.maximum(mx, 1e-3) + 0.02)
    left, right = [], []
    for row in range(solid.shape[0]):
        xs = np.where(solid[row])[0]
        if len(xs) < 30 or xs[-1] - xs[0] < 30:
            continue
        l0, r0 = xs[0] + 2, xs[-1] - 2
        ok_l, ok_r = neutral[row, l0:l0 + 8], neutral[row, r0 - 7:r0 + 1]
        if ok_l.sum() >= 3 and ok_r.sum() >= 3:
            left.append(lum[row, l0:l0 + 8][ok_l].mean())
            right.append(lum[row, r0 - 7:r0 + 1][ok_r].mean())
    if len(left) < 10:
        return 0.0
    L, R = float(np.mean(left)), float(np.mean(right))
    return (L - R) / max(1e-6, L + R)


def review(work, unit, clip, cells):
    """A sheet for a person: every view and frame of a clip, the guide's dummy beside the drawing."""
    if not cells:
        return None
    tile = 220
    views = sorted({v for v, _ in cells})
    frames = sorted({f for _, f in cells})
    sheet = Image.new('RGB', (len(frames) * tile * 2, len(views) * tile), (60, 66, 54))
    for (v, f), (dummy, drawn) in cells.items():
        for k, im in enumerate((dummy, drawn)):
            bb = im.getbbox() or (0, 0, im.width, im.height)
            c = im.crop(bb)
            sc = min((tile - 16) / c.width, (tile - 16) / c.height)
            c = c.resize((max(1, int(c.width * sc)), max(1, int(c.height * sc))), Image.Resampling.LANCZOS)
            bg = Image.new('RGBA', (tile, tile), (60, 66, 54, 255) if k else (40, 44, 36, 255))
            bg.alpha_composite(c, ((tile - c.width) // 2, tile - 8 - c.height))
            sheet.paste(bg.convert('RGB'), ((frames.index(f) * 2 + k) * tile, views.index(v) * tile))
    p = os.path.join(work, 'review', unit, clip + '.png')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    sheet.save(p)
    return p


# The spec's lighting (docs/SPRITE_SPEC.md 1): it is fixed to the PICTURE, not to the
# figure, so every view and frame is lit alike and units look lit by one sun on the field.
LIGHT = ('Lighting, the same in every picture whichever way he faces: the main light comes from the UPPER LEFT of the '
         'picture and from in front, over the viewer\'s left shoulder, so the surfaces facing the left and the viewer are '
         'brightest, with the strongest highlights on the left of the helm, armour and shield, and the shading falls to '
         'the right and below. A soft, dimmer fill light from the right. A thin rim light from behind on the right edge. '
         'No cast shadow on the ground.')


def arms(unit, clip, frame):
    """The prompt's line about weapons: what, which hand, and that it is held as the guide holds it."""
    held = ARMS.get(unit)
    if not held:
        return 'He carries nothing that Image 1 does not show.'
    line = ('He holds %s, exactly where Image 1 shows them: the same hand, the same length, the same angle. Every '
            'weapon is drawn and held in every frame, never sheathed, hidden, doubled or moved to the other hand.' % held)
    if clip.startswith('die_') and frame >= 2:
        line += ' As he falls he may drop it; it lies where it falls, beside him.'
    return line


def prompt(unit, clip, view, frame, backdrop, identity, style):
    look = layout.manifest()['units'][unit]['look']
    name = unit.replace('_', ' ')
    lines = [
        'Image 1 is a pose guide: a rough 3D figure on a grey grid, a red cross where his feet stand, a ground line, '
        'and a blue line at the top of his head. Redraw that figure as a finished, detailed, hand-painted game sprite '
        'in the SAME pose, facing the SAME way, the SAME size and in the SAME place in the picture: his feet on the red '
        'cross, the top of his head at the blue line. Do NOT copy the grid, the lines, the cross, the scale bar or any '
        'text. Paint the figure alone on a perfectly flat, plain %s background that fills the whole picture to every '
        'edge: no ground, no shadow, no scenery, no border, no text. Use no %s anywhere in the figure itself.' % (
            backdrop, 'bright green' if backdrop.upper() == '#00FF00' else 'magenta or pink'),
        'The figure: %s, %s. %s %s' % (name, look, VIEW_PHRASE[view], CLIP_PHRASE[clip].format(n=frame + 1)),
        arms(unit, clip, frame),
        'Fantasy medieval, painterly and realistic like classic 1990s strategy game unit art, crisp clean edges, rich '
        'detail in the armour and cloth. Orthographic view from 30 degrees above the horizon, no perspective distortion.',
        LIGHT,
        'Team colour: everything that shows his army\'s colour (tabard, shield field, plume, banner, saddle cloth) is '
        'painted saturated pure blue %s, shaded naturally. Use no other blue anywhere.' % layout.TEAM_KEY,
    ]
    n = 2
    if identity:
        lines.append('Image %d is this same character as already drawn: keep his armour, helm, plume, shield, weapon and '
                     'colours exactly the same. Ignore its pose and its size.' % n)
        n += 1
    if style:
        lines.append('Image %d shows the painting style to match: brushwork, palette, outline and level of detail. '
                     'Do not copy its subject or composition.' % n)
    return '\n\n'.join(lines)


def plates():
    if not os.path.isdir(CODEX_IMAGES):
        return set()
    return {os.path.join(d, f) for d in (os.path.join(CODEX_IMAGES, x) for x in os.listdir(CODEX_IMAGES))
            if os.path.isdir(d) for f in os.listdir(d) if f.endswith('.png')}


def codex(text, refs, model=None, timeout=1500):
    """Draw one picture through `codex exec`; the path of the plate it wrote."""
    cmd = ['codex', 'exec', '--enable', 'image_generation', '--skip-git-repo-check']
    if model:
        cmd += ['-m', model]
    for r in refs:
        cmd += ['-i', os.path.abspath(r)]
    before = plates()
    scratch = tempfile.mkdtemp(prefix='illyria-codex-')
    try:
        # The prompt on stdin: -i takes several files and would swallow a positional prompt.
        res = subprocess.run(cmd, cwd=scratch, input=text, capture_output=True, text=True, timeout=timeout)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    made = sorted(plates() - before, key=os.path.getmtime)
    if not made:
        tail = (res.stderr or res.stdout or '').strip().splitlines()
        raise RuntimeError('codex wrote no image (exit %d): %s' % (res.returncode, tail[-1] if tail else 'no output'))
    return made[-1]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('unit')
    ap.add_argument('--clip', action='append', help='clip(s) to draw (default: all, in the spec\'s order)')
    ap.add_argument('--views', default='', help='e.g. 0-4 or 0,2 (default all)')
    ap.add_argument('--frames', default='', help='e.g. 0-3 (default all)')
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'codex'))
    ap.add_argument('--work', help='raw plates and guides (default: OUT-work)')
    ap.add_argument('--identity', help='the character as already drawn (Image 2)')
    ap.add_argument('--style', help='a picture whose painting to match')
    ap.add_argument('--backdrop', help='flat background colour (default: BACKDROPS, else #00FF00)')
    ap.add_argument('--model', help='the relaying codex agent (-m)')
    ap.add_argument('--force', action='store_true', help='redraw cells that exist')
    ap.add_argument('--dry-run', action='store_true', help='write the guides and print the prompts; draw nothing')
    args = ap.parse_args(argv)

    if args.unit not in layout.UNITS:
        raise SystemExit('unknown unit %s; one of: %s' % (args.unit, ', '.join(layout.UNITS)))
    backdrop = args.backdrop or BACKDROPS.get(args.unit, '#00FF00')
    clips = [c for c in layout.CLIPS if not args.clip or c[0] in args.clip]
    work = args.work or args.out.rstrip('/') + '-work'
    drawn = failed = 0
    flagged = []
    for name, frames, _, _ in clips:
        for v in span(args.views, layout.VIEWS):
            for f in span(args.frames, frames):
                rel = layout.cell_file(name, v, f)
                dest = os.path.join(args.out, args.unit, rel)
                if os.path.exists(dest) and not args.force:
                    continue
                guide, alone = guide_pictures(args.unit, name, v, f)
                gpath = os.path.join(work, 'guides', args.unit, rel)
                os.makedirs(os.path.dirname(gpath), exist_ok=True)
                guide.save(gpath)
                refs = [gpath] + [p for p in (args.identity, args.style) if p]
                text = prompt(args.unit, name, v, f, backdrop, args.identity, args.style)
                print('%s/%s' % (args.unit, rel))
                if args.dry_run:
                    print('  refs: ' + ', '.join(refs))
                    print('  ' + text.replace('\n\n', '\n  '))
                    continue
                t = time.time()
                try:
                    plate = codex(text, refs, args.model)
                    raw = os.path.join(work, 'raw', args.unit, rel)
                    os.makedirs(os.path.dirname(raw), exist_ok=True)
                    shutil.copyfile(plate, raw)
                    out, s = fit(raw, alone, name, f)
                    os.makedirs(os.path.dirname(dest), exist_ok=True)
                    out.save(dest)
                    drawn += 1
                    same, flipped = agreement(out, alone)
                    print('  drawn in %.0f s, scaled %.2f, overlap with the guide %.2f (mirrored %.2f)' % (
                        time.time() - t, s, same, flipped))
                    if flipped > same + 0.05:
                        flagged.append('%s: looks MIRRORED (%.2f vs %.2f)' % (rel, flipped, same))
                    elif same < MIN_OVERLAP:
                        flagged.append('%s: low overlap with the guide (%.2f): a weapon or limb astray?' % (rel, same))
                except Exception as e:  # noqa: BLE001 -- one bad cell must not stop the run
                    failed += 1
                    print('  FAILED: %s' % e)
    # Review sheets for every clip touched: all of its drawn cells, old and new.
    if not args.dry_run:
        for name, frames, _, _ in clips:
            cells = {}
            for v in range(layout.VIEWS):
                for f in range(frames):
                    p = os.path.join(args.out, args.unit, layout.cell_file(name, v, f))
                    if os.path.exists(p):
                        cells[(v, f)] = (guide_pictures(args.unit, name, v, f)[1], Image.open(p).convert('RGBA'))
            r = review(work, args.unit, name, cells)
            if r:
                print('review: ' + r)
            # Lighting: every frame lit alike (the spec's upper-left key light).
            lights = {vf: light_side(drawn) for vf, (_, drawn) in cells.items()}
            if len(lights) >= 4:
                mid = float(np.median(list(lights.values())))
                for (v, f), val in sorted(lights.items()):
                    if abs(val - mid) > LIGHT_SPREAD:
                        flagged.append('%s: lit unlike the rest of %s (%+.2f against %+.2f): light from the wrong side?' % (
                            layout.cell_file(name, v, f), name, val, mid))
    for line in flagged:
        print('CHECK ' + line)
    print('%d drawn, %d failed, %d flagged -> %s' % (drawn, failed, len(flagged), os.path.join(args.out, args.unit)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
