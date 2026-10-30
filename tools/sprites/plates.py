#!/usr/bin/env python3
"""Whole-clip plates for art-cli: a sheet of dummies to draw over, and the drawn plate cut back into cells.

    python3 tools/sprites/plates.py sheets UNIT
    python3 tools/sprites/plates.py run UNIT [--clips walk,idle] [--views 0-4] [--force]
    python3 tools/sprites/plates.py template UNIT CLIP VIEW
    python3 tools/sprites/plates.py export UNIT CLIP VIEW [PLATE] [--out art/codex]
    python3 tools/sprites/plates.py page UNIT

art-cli (tools/art-cli: `art draw UNIT-CLIP-vVIEW --provider codex`) draws every
frame of one clip of one view on a single plate. Drawn together, the frames come
back as the same character, which one picture a cell (codex_draw.py) cannot
promise. art.yaml says what to draw; this file does the parts that are this
game's own:

  sheets    writes the unit's sheets into art.yaml: one a clip a view, each with
            its grid, the way it faces and its own pose reference. What the unit
            looks like and what each clip's frames are stay hand-written there.
  template  lays the clip's dummies (today's figure, templates.py, as grey
            mannequins) on the sheet's grid at the canvas size, all at one scale
            and standing where they stand on the delivery canvas, so the motion
            between frames is in the reference; a JSON beside it records where
            each cell came from. A death is not drawn over today's figure (it
            falls stiff as a plank), so its template is art-cli's kind instead:
            a groundline and a standing mark in each cell, after a first cell of
            him standing over the idle dummy (cells_of).
  export    cuts a plate (default: the newest candidate) on the same grid, keys
            each cell, scales it back to the delivery canvas and places it over
            its dummy: one scale for the whole clip, so frames cannot pulse, and
            each frame's offset searched against its dummy's outline. A death is
            fitted by its standing cell, which is then cut away, and every frame
            keeps its place in its cell. Writes the cells import.py reads, a
            review sheet and a looping GIF beside today's.
  run       template, `art draw`, export, for every sheet of a unit not yet
            exported. Stop it and run it again to carry on.
  page      every clip and view of a unit as a looping GIF on one page, all at
            one scale and at the spec's timing, so sizes and motion compare.

Then: python3 tools/sprites/import.py art/codex --check
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

import numpy as np
import yaml
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

import codex_draw as cd  # noqa: E402
import layout  # noqa: E402
import templates  # noqa: E402

# How much of a cell the clip's figures may span, and where their ground sits in it.
FILL = 0.84
GROUND = 0.92
# The guide colour of art-cli's own templates (art/template.py): a grey nothing else uses.
GUIDE = (128, 128, 128)


# The way each drawn view faces, as art-cli's prompt says it ("Every frame faces the
# SAME direction: ..."). Views 5-7 are these mirrored by the renderer.
FACING = (
    "toward the viewer: the FRONT view, seen from 30 degrees above. His right side is on the picture's left",
    'three-quarters toward the viewer and turned to the RIGHT of the picture: his front-right, seen from 30 '
    'degrees above. His chest, face-plate and shield are turned toward the lower RIGHT of the picture and his '
    'right side is nearer us. NOT square to the viewer',
    'the RIGHT of the picture, in PROFILE: we see his right side, from 30 degrees above. His chest faces the right '
    'edge of the picture, the shield on his far arm, out in front of him. NOT turned toward the viewer',
    'three-quarters AWAY from the viewer and turned to the RIGHT of the picture: his back-right, seen from 30 '
    'degrees above. We see his BACK -- the back of his helm, his back plate, the back of his tabard -- and only '
    'the edge of his face-plate, turned to the right. The shield, on his far arm, shows its plain back or its '
    'edge. NOT facing the viewer',
    "AWAY from the viewer: the BACK view, seen from 30 degrees above. We see the back of his helm, his back plate "
    "and the back of his tabard, and his right side is on the picture's right. The shield, on his left arm on the "
    "picture's left, shows its plain back. NOT his face-plate, NOT the front of the shield",
)


def sheet_name(unit, clip, view):
    return '%s-%s-v%d' % (unit, clip, view)


def frames_of(clip):
    return layout.FRAMES[clip]


def cells_of(unit, clip):
    """Cells on the clip's plate. A death has one more than its frames: first, the
    unit standing over the full idle mannequin. Nothing else on a death's plate says
    how big he is drawn or which way he faces (the generator fills its cells, and now
    and then mirrors one); that cell does, and is cut away."""
    return frames_of(clip) + (0 if guided(unit, clip) else 1)


def guided(unit, clip):
    """Whether the clip is drawn over today's figure. Deaths are not: today's renders
    fall stiff as a plank, and a death drawn over one is the same plank painted. They
    get art-cli's template (a groundline and a standing mark) and art.yaml's words."""
    if clip.startswith('die_'):
        return False
    meta, _ = templates.source_sheet(unit)
    return bool(meta) and layout.FIRST[clip] + layout.FRAMES[clip] <= meta['framesPerView']


def profile_path():
    return os.path.join(ROOT, 'art.yaml')


def profile():
    with open(profile_path()) as fh:
        return yaml.safe_load(fh)


def grid(prof, unit, clip, view):
    """(cols, rows) of the sheet as art.yaml lays it out: art-cli's prompt tells the
    generator this grid, so the template and the cut must use the same one."""
    sheet = ((prof['subjects'].get(unit) or {}).get('sheets') or {}).get(sheet_name(unit, clip, view))
    if not sheet:
        raise SystemExit('art.yaml has no sheet %s (plates.py sheets %s)' % (sheet_name(unit, clip, view), unit))
    n = cells_of(unit, clip)
    cols = int(sheet.get('cols') or n)
    return cols, -(-n // cols)


def sheets(unit):
    """Write every clip and view of a unit into art.yaml as a sheet of its own. The
    file is rewritten the way art-cli rewrites it (art/profile.py save)."""
    path = profile_path()
    with open(path) as fh:
        text = fh.read()
    header = ''.join(line for line in text.splitlines(True) if line.startswith('#'))
    data = yaml.safe_load(text)
    subject = data['subjects'].get(unit)
    if subject is None:
        raise SystemExit('art.yaml has no subject %s: write its description and clips first' % unit)
    canvas = int(data.get('canvas', 1254))
    made = {}
    for clip, n, _, _ in layout.CLIPS:
        role = 'pose' if guided(unit, clip) else 'template'
        for v in range(layout.VIEWS):
            name = sheet_name(unit, clip, v)
            made[name] = {'anims': [clip], 'cols': best_cols(canvas, cells_of(unit, clip), figures(unit, clip, v)[1]),
                          'facing': FACING[v], 'reference': {role: 'art/templates/%s.png' % name}}
    subject['sheets'] = made
    with open(path, 'w') as fh:
        fh.write(header + yaml.safe_dump(data, sort_keys=False, allow_unicode=True, width=100))
    print('%s: %d sheets in art.yaml' % (unit, len(made)))


def figures(unit, clip, view):
    """(today's frames of the clip alone on the delivery canvas, the box round all of
    them). A clip with no dummy of its own gets die_back's frames (templates.figure's
    stand-in) to size it, with room either side of the anchor for a fall either way."""
    alones = [cd.guide_pictures(unit, clip, view, f)[1] for f in range(frames_of(clip))]
    boxes = [a.getbbox() for a in alones + ([] if guided(unit, clip) else [stand(unit, view)]) if a.getbbox()]
    ux0, uy0 = min(b[0] for b in boxes), min(b[1] for b in boxes)
    ux1, uy1 = max(b[2] for b in boxes), max(b[3] for b in boxes)
    if not guided(unit, clip):
        ax = layout.unit_class(unit)['anchor'][0]
        half = max(ax - ux0, ux1 - ax)
        ux0, ux1 = ax - half, ax + half
    return alones, (ux0, uy0, ux1, uy1)


def fit_scale(canvas, cols, rows, box):
    uw, uh = box[2] - box[0], box[3] - box[1]
    return min(FILL * (canvas // cols) / uw, FILL * (canvas // rows) / uh)


def best_cols(canvas, n, box):
    """The grid that draws this clip's figures biggest: a long fall seen from the side
    wants wide cells, a man standing wants tall ones. Fewer empty cells breaks a tie."""
    return max(range(1, n + 1), key=lambda c: (round(fit_scale(canvas, c, -(-n // c), box), 3), -(-(-n // c) * c)))


def stand(unit, view):
    """The idle dummy's first frame: the unit standing, on the delivery canvas."""
    return cd.guide_pictures(unit, 'idle', view, 0)[1]


def mannequin(alone):
    """Today's figure as a grey mannequin: its shapes and shading, none of its colours,
    so the reference says where the limbs and weapons are and nothing about the look."""
    a = np.asarray(alone, dtype=np.float32)
    lum = (0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]) / 255.0
    grey = (0.30 + 0.55 * lum) * 255.0
    return np.dstack([grey, grey, grey, a[..., 3]]).astype(np.uint8)


def template(unit, clip, view):
    prof = profile()
    canvas = int(prof.get('canvas', 1254))
    cols, rows = grid(prof, unit, clip, view)
    cw, ch = canvas // cols, canvas // rows
    backdrop = cd.BACKDROPS.get(unit, prof.get('backdrop', '#00FF00'))
    ax, ay = layout.unit_class(unit)['anchor']
    led = guided(unit, clip)
    # One box round every frame, so all of them share a scale and a ground.
    alones, (ux0, uy0, ux1, uy1) = figures(unit, clip, view)
    uw, uh = ux1 - ux0, uy1 - uy0
    s = fit_scale(canvas, cols, rows, (ux0, uy0, ux1, uy1))
    sheet = Image.new('RGBA', (canvas, canvas), backdrop)
    draw = ImageDraw.Draw(sheet)
    line = max(4, canvas // 220)
    cells = []
    # A death's first cell is him standing (cells_of); the rest have a groundline and a mark.
    for i, alone in enumerate(alones if led else [stand(unit, view)] + alones):
        c, r = i % cols, i // cols
        ox = c * cw + (cw - uw * s) / 2.0
        oy = r * ch + ch * GROUND - uh * s
        if led or i == 0:
            fig = Image.fromarray(mannequin(alone)).crop((ux0, uy0, ux1, uy1))
            fig = fig.resize((max(1, round(uw * s)), max(1, round(uh * s))), Image.Resampling.LANCZOS)
            sheet.alpha_composite(fig, (round(ox), round(oy)))
        else:
            gx, gy = ox + (ax - ux0) * s, oy + (ay - uy0) * s
            pad = cw * 0.06
            draw.line([(c * cw + pad, gy), ((c + 1) * cw - pad, gy)], fill=GUIDE, width=line)
            tick = ch * 0.16
            draw.line([(gx, gy - tick), (gx, gy + tick / 2)], fill=GUIDE, width=line)
        cells.append([round(ox), round(oy)])
    os.makedirs(os.path.join(ROOT, 'art', 'templates'), exist_ok=True)
    path = os.path.join(ROOT, 'art', 'templates', sheet_name(unit, clip, view) + '.png')
    sheet.convert('RGB').save(path)
    with open(path[:-4] + '.json', 'w') as fh:
        json.dump({'unit': unit, 'clip': clip, 'view': view, 'canvas': canvas, 'cols': cols, 'rows': rows,
                   'cell': [cw, ch], 'scale': s, 'box': [ux0, uy0, ux1, uy1], 'cells': cells,
                   'backdrop': backdrop, 'guided': led}, fh, indent=1)
    print('%s  %dx%d, cells %dx%d, the figure drawn at %.2fx (%d px tall)%s' % (
        os.path.relpath(path, ROOT), cols, rows, cw, ch, s, round(uh * s), '' if led else ', no dummy'))
    return path


def newest_candidate(unit, clip, view):
    name = sheet_name(unit, clip, view)
    found = []
    for p in glob.glob(os.path.join(ROOT, 'art', 'candidates', name + '-*.png')):
        m = re.match(re.escape(name) + r'-(\d+)\.png$', os.path.basename(p))
        if m:
            found.append((int(m.group(1)), p))
    if not found:
        raise SystemExit('no candidate for %s: art draw %s --provider codex' % (name, name))
    return max(found)[1]


def cut(keyed, cols, rows, cw, ch, count):
    """The first `count` cells of a keyed plate, each as (its drawing, the drawing's
    corner from the cell's corner). A generator does not keep to its grid's lines -- a
    tall pose crosses into the row below, a flung sword into the next cell -- so the
    plate is split by shape: each separate shape (a body, a sword in the air) goes to
    the cell its middle is in, however far it spills, and specks go nowhere."""
    from scipy import ndimage
    solid = keyed[..., 3] > 0.1
    labels, m = ndimage.label(solid)
    if not m:
        return [(None, (0, 0))] * count
    idx = range(1, m + 1)
    owner = np.full(m + 1, -1)
    for j, (cy, cx), size in zip(idx, ndimage.center_of_mass(solid, labels, idx), ndimage.sum(solid, labels, idx)):
        if size >= 40:
            owner[j] = min(rows - 1, int(cy // ch)) * cols + min(cols - 1, int(cx // cw))
    out = []
    for i in range(count):
        mine = owner[labels] == i
        if not mine.any():
            out.append((None, (0, 0)))
            continue
        c, r = i % cols, i // cols
        ys, xs = np.where(mine)
        # The cell's own bounds and wherever its shapes spill.
        x0, y0 = min(xs.min(), round(c * cw)), min(ys.min(), round(r * ch))
        x1, y1 = max(xs.max() + 1, round((c + 1) * cw)), max(ys.max() + 1, round((r + 1) * ch))
        part = keyed[y0:y1, x0:x1].copy()
        part[..., 3] *= mine[y0:y1, x0:x1]
        out.append((part, (x0 - round(c * cw), y0 - round(r * ch))))
    return out


def place(rgba, s, x, y, size):
    """rgba (floats) scaled by s onto a clear canvas of size, its corner at (x, y)."""
    small = cd.resized(rgba, s)
    im = Image.fromarray((small * 255 + 0.5).astype(np.uint8))
    out = Image.new('RGBA', size, (0, 0, 0, 0))
    sx, sy = max(0, -x), max(0, -y)
    im = im.crop((sx, sy, im.width, im.height))
    out.alpha_composite(im, (x + sx, y + sy))
    return out


def gif(path, old, new, fps=10):
    """Today's frames beside the drawn ones, looping, cropped to what either covers."""
    boxes = [im.getbbox() for im in old + new if im.getbbox()]
    x0, y0 = min(b[0] for b in boxes) - 12, min(b[1] for b in boxes) - 12
    x1, y1 = max(b[2] for b in boxes) + 12, max(b[3] for b in boxes) + 12
    k = 420.0 / (y1 - y0)
    w, h = round((x1 - x0) * k), round((y1 - y0) * k)
    frames = []
    for a, b in zip(old, new):
        pair = Image.new('RGBA', (w * 2, h), (74, 82, 60, 255))
        for i, im in enumerate((a, b)):
            pair.alpha_composite(im.crop((x0, y0, x1, y1)).resize((w, h), Image.Resampling.LANCZOS), (i * w, 0))
        frames.append(pair.convert('RGB'))
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0)


def export(unit, clip, view, plate=None, out=None):
    name = sheet_name(unit, clip, view)
    with open(os.path.join(ROOT, 'art', 'templates', name + '.json')) as fh:
        t = json.load(fh)
    plate = plate or newest_candidate(unit, clip, view)
    out = out or os.path.join(ROOT, 'art', 'codex')
    work = out.rstrip('/') + '-work'
    keyed = cd.key(plate)
    # The plate's cells, in the template's pixels (codex returns its own size).
    k = keyed.shape[1] / float(t['canvas'])
    cw, ch = t['cell']
    s = t['scale']
    ux0, uy0 = t['box'][:2]
    ax, ay = layout.unit_class(unit)['anchor']
    n = frames_of(clip)
    led = t.get('guided', True)
    cells, offsets = [], []
    for i, (cell, (dx, dy)) in enumerate(cut(keyed, t['cols'], t['rows'], cw * k, ch * k, n if led else n + 1)):
        if cell is None:
            raise SystemExit('%s: cell %d is empty' % (name, i))
        # Where the anchor sat in this cell of the template (in the cut's own pixels): the
        # first guess for where it is drawn.
        ox, oy = t['cells'][i]
        c, r = i % t['cols'], i // t['cols']
        feet = (((ax - ux0) * s + ox) * k - c * cw * k - dx, ((ay - uy0) * s + oy) * k - r * ch * k - dy)
        cells.append((cell, feet))
        offsets.append((dx, dy))
    alones = [cd.guide_pictures(unit, clip, view, f)[1] if led else None for f in range(n)]
    size = tuple(layout.unit_class(unit)['canvas'])
    lying = clip.startswith('die_')
    places = []
    if led:
        # The generator draws a plate at its own size (now and then a fifth bigger than
        # the mannequins it was given), so the first guess is each frame's height against
        # its dummy's, drawn feet on the dummy's feet; then one scale for the clip (the
        # median of each frame's best by outline), and each frame's place at that scale.
        guesses = []
        for (cell, _), alone in zip(cells, alones):
            (_, dy0, _, dy1), dfeet = cd.extent(np.asarray(alone)[..., 3] / 255.0)
            (_, gy0, _, gy1), gfeet = cd.extent(cell[..., 3])
            guesses.append(((dy1 - dy0) / float(gy1 - gy0), gfeet, dfeet))
        s0 = float(np.median([g[0] for g in guesses]))
        first = [cd.settle(cell[..., 3], np.asarray(alone)[..., 3] > 127, s0, gfeet, dfeet, lying)
                 for (cell, _), alone, (_, gfeet, dfeet) in zip(cells, alones, guesses)]
        scale = float(np.median([b[0] for b in first]))
        for (cell, _), alone, (_, gfeet, dfeet) in zip(cells, alones, guesses):
            _, x, y = cd.settle(cell[..., 3], np.asarray(alone)[..., 3] > 127, scale, gfeet, dfeet, lying, ks=[1.0])
            places.append((x, y))
    else:
        # A death: the first cell is him standing, drawn over the idle dummy, so it is
        # fitted to that like a guided frame (a wide search: the generator fills its
        # cells rather than hold a size), and is then cut away. Every frame keeps its
        # place in its cell from there, so the plate's own layout is the motion.
        ref = cells.pop(0)[0]
        rdx, rdy = offsets.pop(0)
        upright = stand(unit, view)
        want = np.asarray(upright)[..., 3] > 127
        (_, sy0, _, sy1), _ = cd.extent(want.astype(np.float32))
        (_, gy0, _, gy1), feet0 = cd.extent(ref[..., 3])
        scale, x, y = cd.settle(ref[..., 3], want, (sy1 - sy0) / float(gy1 - gy0), feet0, (ax, ay), False,
                                ks=np.linspace(0.8, 1.25, 19))
        same, flipped = cd.agreement(place(ref, scale, x, y, tuple(layout.unit_class(unit)['canvas'])), upright)
        print('  standing  overlap %.2f (mirrored %.2f)' % (same, flipped))
        places = [(round(x + (dx - rdx) * scale), round(y + (dy - rdy) * scale)) for dx, dy in offsets]
        if clip in ('die_back', 'die_front'):
            # The generator does not keep a plate's ground: it enlarges the standing
            # cell and lays the bodies along the drawn groundline. Today's dummy falls
            # stiffly but lands in the right place (die_front is die_back seen from the
            # other side), so each frame is slid, at the plate's scale, to cover the
            # dummy's frame best -- its place, never its pose.
            for f, (cell, _) in enumerate(cells):
                dummy = np.asarray(cd.guide_pictures(unit, clip, view, f)[1])[..., 3] > 127
                _, x, y = cd.settle(cell[..., 3], dummy, scale, (0, 0), places[f], True, ks=[1.0], reach=(40, 60))
                places[f] = (x, y)
        # And no frame of a death floats: its lowest point is never above the ground he
        # stood on (the spec: the body ends on the ground, the feet at the anchor).
        for f, (cell, _) in enumerate(cells):
            rows_drawn = np.where(cell[..., 3].max(axis=1) > 0.5)[0]
            bottom = places[f][1] + (rows_drawn[-1] + 1) * scale
            if bottom < ay:
                places[f] = (places[f][0], round(places[f][1] + ay - bottom))
    drawn, flagged = [], []
    if not led and flipped > same + 0.05:
        flagged.append('%s: the standing cell looks MIRRORED (%.2f vs %.2f)' % (name, flipped, same))
    for f, ((cell, feet), alone) in enumerate(zip(cells, alones)):
        x, y = places[f]
        im = place(cell, scale, x, y, size)
        dest = os.path.join(out, unit, layout.cell_file(clip, view, f))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        im.save(dest)
        drawn.append(im)
        if alone is None:
            continue
        same, flipped = cd.agreement(im, alone)
        # How far the drawing stood from where the template put its dummy, in delivery pixels.
        gx, gy = round(ax - feet[0] * scale), round(ay - feet[1] * scale)
        print('  f%d  overlap %.2f (mirrored %.2f)  moved %+d, %+d px  light %+.2f' % (
            f, same, flipped, x - gx, y - gy, cd.light_side(im)))
        if flipped > same + 0.05:
            flagged.append('%s f%d looks MIRRORED (%.2f vs %.2f)' % (name, f, flipped, same))
        elif same < cd.MIN_OVERLAP:
            flagged.append('%s f%d: low overlap with its dummy (%.2f): a weapon or limb astray?' % (name, f, same))
    # Lighting is judged on standing figures only: which way a falling body's edges face
    # changes from frame to frame, so its outline says nothing about the light.
    if led:
        lights = [cd.light_side(im) for im in drawn]
        mid = float(np.median(lights))
        flagged += ['%s f%d lit unlike the rest (%+.2f against %+.2f)' % (name, f, v, mid)
                    for f, v in enumerate(lights) if abs(v - mid) > cd.LIGHT_SPREAD]
    print('%s: scale %.3f (%s)' % (name, scale, os.path.relpath(plate, ROOT)))
    blank = Image.new('RGBA', size, (0, 0, 0, 0))
    old = [a if a is not None else blank for a in alones]
    sheet = cd.review(work, unit, '%s-v%d' % (clip, view), {(view, f): (a, d) for f, (a, d) in enumerate(zip(old, drawn))})
    loop = os.path.join(work, 'review', unit, '%s-v%d.gif' % (clip, view))
    gif(loop, old, drawn)
    print('review: %s\nloop:   %s' % (os.path.relpath(sheet, ROOT), os.path.relpath(loop, ROOT)))
    for line in flagged:
        print('CHECK ' + line)
    return flagged


def done(unit, clip, view, out):
    return all(os.path.exists(os.path.join(out, unit, layout.cell_file(clip, view, f))) for f in range(frames_of(clip)))


def run(unit, clips, views, force, out):
    """Every sheet of the unit not yet exported: template, draw, export. One at a time."""
    out = out or os.path.join(ROOT, 'art', 'codex')
    todo = [(c, v) for v in views for c in clips if force or not done(unit, c, v, out)]
    print('%s: %d sheet(s) to draw' % (unit, len(todo)))
    flagged, failed = [], []
    for i, (clip, view) in enumerate(todo, 1):
        name = sheet_name(unit, clip, view)
        print('\n[%d/%d] %s' % (i, len(todo), name), flush=True)
        t = time.time()
        try:
            template(unit, clip, view)
            res = subprocess.run(['art', 'draw', name, '--provider', 'codex'], cwd=ROOT)
            if res.returncode:
                raise RuntimeError('art draw exited %d' % res.returncode)
            flagged += export(unit, clip, view, out=out)
        except (Exception, SystemExit) as e:  # noqa: BLE001 -- one bad sheet must not stop the run
            failed.append('%s: %s' % (name, e))
            print('FAILED %s: %s' % (name, e))
        print('  %.0f s' % (time.time() - t), flush=True)
    print('\n%d drawn, %d failed, %d flagged' % (len(todo) - len(failed), len(failed), len(flagged)))
    for line in failed:
        print('FAILED ' + line)
    for line in flagged:
        print('CHECK ' + line)
    return 1 if failed else 0


# How long each clip plays (docs/SPRITE_SPEC.md 3), in seconds. Walk has no fixed
# length in the spec (it follows the unit's speed); a second a stride reads right.
SECONDS = {'idle': 2.2, 'walk': 1.0, 'flinch': 0.3, 'die_back': 0.9, 'die_front': 0.9, 'die_crumple': 0.9,
           'die_burn': 1.5}
VIEW_NAMES = ('front', 'front-right', 'right', 'back-right', 'back')


def page(unit, out=None):
    """art/codex-work/review/UNIT/page/index.html: every drawn clip and view, looping."""
    out = out or os.path.join(ROOT, 'art', 'codex')
    dest = os.path.join(out.rstrip('/') + '-work', 'review', unit, 'page')
    os.makedirs(dest, exist_ok=True)
    W, H = layout.unit_class(unit)['canvas']
    ax, ay = layout.unit_class(unit)['anchor']
    # One window for every clip: a body lying either way, a sword overhead.
    box = (max(0, ax - 400), max(0, ay - 500), min(W, ax + 400), min(H, ay + 140))
    k = 0.4
    size = (round((box[2] - box[0]) * k), round((box[3] - box[1]) * k))
    attack = layout.manifest()['units'][unit].get('attack_seconds', 0.9)
    rows = []
    for clip, n, _, loop in layout.CLIPS:
        cells = []
        for v in range(layout.VIEWS):
            files = [os.path.join(out, unit, layout.cell_file(clip, v, f)) for f in range(n)]
            if not all(os.path.exists(f) for f in files):
                cells.append('<td class="none">not drawn</td>')
                continue
            frames = []
            for f in files:
                bg = Image.new('RGBA', size, (74, 82, 60, 255))
                bg.alpha_composite(Image.open(f).convert('RGBA').crop(box).resize(size, Image.Resampling.LANCZOS))
                frames.append(bg.convert('RGB'))
            each = round(1000 * SECONDS.get(clip, attack) / n)
            times = [each] * n
            if not loop:
                times[-1] += 900  # hold the last frame (a corpse, a recovered guard) before it loops
            name = '%s-v%d.gif' % (clip, v)
            frames[0].save(os.path.join(dest, name), save_all=True, append_images=frames[1:], duration=times, loop=0)
            cells.append('<td><img src="%s" width="%d" height="%d"></td>' % (name, size[0], size[1]))
        rows.append('<tr><th>%s</th>%s</tr>' % (clip, ''.join(cells)))
    head = ''.join('<th>%s</th>' % v for v in VIEW_NAMES)
    html = ('<!doctype html><meta charset="utf-8"><title>%s</title><style>body{background:#2b2f26;color:#ddd;'
            'font:14px system-ui;margin:16px}td,th{padding:4px}th{text-align:left;font-weight:600}'
            'td.none{color:#777;text-align:center}</style><h2>%s</h2><p>Every clip and view at one scale, at the '
            "spec's timing; clips that do not loop hold their last frame.</p><table><tr><th></th>%s</tr>%s</table>"
            % (unit, unit.replace('_', ' '), head, ''.join(rows)))
    path = os.path.join(dest, 'index.html')
    with open(path, 'w') as fh:
        fh.write(html)
    print(os.path.relpath(path, ROOT))
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('verb', choices=('sheets', 'run', 'template', 'export', 'page'))
    ap.add_argument('unit')
    ap.add_argument('clip', nargs='?')
    ap.add_argument('view', nargs='?', type=int)
    ap.add_argument('plate', nargs='?', help='export: the plate to cut (default: the newest candidate)')
    ap.add_argument('--clips', default='', help='run: clips, e.g. walk,idle (default: all)')
    ap.add_argument('--views', default='', help='run: views, e.g. 0-4 or 0,2 (default: all)')
    ap.add_argument('--force', action='store_true', help='run: redraw sheets already exported')
    ap.add_argument('--out', help='where the cells go (default art/codex)')
    args = ap.parse_args(argv)
    if args.unit not in layout.UNITS:
        raise SystemExit('unknown unit %s' % args.unit)
    if args.verb == 'sheets':
        sheets(args.unit)
        return 0
    if args.verb == 'page':
        page(args.unit, args.out)
        return 0
    if args.verb == 'run':
        clips = [c[0] for c in layout.CLIPS if not args.clips or c[0] in args.clips.split(',')]
        return run(args.unit, clips, cd.span(args.views, layout.VIEWS), args.force, args.out)
    if args.clip is None or args.view is None:
        raise SystemExit('%s needs UNIT CLIP VIEW' % args.verb)
    if args.verb == 'template':
        template(args.unit, args.clip, args.view)
        return 0
    export(args.unit, args.clip, args.view, args.plate, args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
