#!/usr/bin/env python3
"""Body parts for the packed sheets that have none, cut from each sheet's own pictures.

    python3 tools/sprites/sheet_gibs.py [--sprites game/web/sprites] [--units a,b] [--review DIR] [--check]

A sheet packed before the gib parts existed (the Blender sheets: 26 frames a
view, 130 cells) held no pieces of its soldier, so whatever was cut or blown
off him flew as a coloured capsule or ball. This gives every such sheet (and
its close-up sheet) the 20 gib cells after its views' cells, each part cut
from the sheet's own idle picture and packed in new rows above the old ones
(no old pixel or cell moves). One pipeline for every type, nothing placed by
hand for a unit; the output depends only on the sheet.

Three body plans, chosen from the unit's list of parts (layout.UNITS):

  man     head, trunk, arms and legs and no mount: gibs.py's outlines (in
          heights of the figure) on the idle picture seen from behind and
          from in front; a wing where the type has one.
  beast   everything else that bleeds (riders on their mounts, spiders,
          griffins, unicorns, dragons): the side view is measured (the belly
          line, the legs under it, the back line, what rides or folds above
          it, the head and neck in front) and cut along those lines.
  engine  the catapult: two baulks of its timber.

Sheets that already have gib cells (the importer's) are left alone. --check
writes nothing and says what it would do; --review writes each sheet's parts
side by side, enlarged, for a look.
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

import gibs  # noqa: E402
import layout  # noqa: E402

SOLID, TEAM, OUTLINE = 255, 191, 153  # the sheet's alpha codes (docs/SPRITE_SPEC.md)
BLOODLESS = ('undead', 'catapult')    # sim::UnitStats::bleeds
SIDE = 2
PAD = 2  # empty pixels between packed parts


# ---------------------------------------------------------------- pictures

class Cell:
    """One cell of a sheet as arrays, top row first: rgb (0..1), the alpha code, and where the feet's anchor is."""

    def __init__(self, px, row):
        H = px.shape[0]
        x, y, w, h = (int(v) for v in row[:4])
        crop = px[H - y - h:H - y, x:x + w]
        self.rgb = crop[..., :3].astype(np.float32) / 255.0
        self.code = crop[..., 3].copy()
        self.ppm = w / row[6]
        self.ax = -row[4] * self.ppm            # the anchor's column
        self.ay = (row[5] + row[7]) * self.ppm  # and its row, from the top

    @property
    def solid(self):
        return self.code > 100

    @property
    def team(self):
        return self.code == TEAM


class Part:
    """A cut: which pixels of a cell, and how deep the blood goes at the cut (0: none)."""

    def __init__(self, cell, keep, cut_from=None, gore=False):
        self.cell, self.keep, self.cut_from, self.gore = cell, keep, cut_from, gore


def finish(part, bleeds, outline_rgb):
    """The part as an RGBA uint8 picture trimmed to what it holds (alpha in the sheet's codes), or None."""
    c, keep = part.cell, part.keep
    if keep is None or keep.sum() < 8:
        return None
    rgb, code = c.rgb.copy(), np.where(keep, c.code, 0).astype(np.uint8)
    if bleeds and (part.cut_from is not None or part.gore):
        # Blood where the cut went through the body: near what was left behind.
        tall = math.sqrt(float(c.solid.sum()))
        depth = max(1, int(round(0.05 * tall)))
        k = np.zeros(keep.shape, np.float32)
        if part.cut_from is not None and part.cut_from.any():
            _, dist = gibs.grow(part.cut_from, depth)
            k = np.clip(1.0 - (dist - 1) / depth, 0.0, 1.0) * keep * 0.85
        cloth = c.team & keep
        blood = np.broadcast_to(gibs.BLOOD, rgb.shape).copy()
        if part.gore:
            # A gobbet is mostly gore: the blood keeps the picture's shading.
            blood = gibs.BLOOD * (0.5 + rgb @ np.array([0.3, 0.59, 0.11], np.float32))[..., None]
            k = np.maximum(k, 0.9 * keep)
            soak = cloth
        else:
            soak = cloth & (k > 0.35)
        # Cloth of the side's colour is soaked or left as it is (half bloodied it would be dyed, not red).
        k = np.where(cloth, np.where(soak, 1.0, 0.0), k)
        rgb = rgb * (1 - k[..., None]) + blood * k[..., None]
        code[soak] = SOLID
    # The dark line round every picture in the sheet, round the cut too.
    solid = code > 0
    edge, _ = gibs.grow(solid, 1)
    ring = edge & ~solid
    ring[1:-1, 1:-1] &= (solid[:-2, 1:-1] | solid[2:, 1:-1] | solid[1:-1, :-2] | solid[1:-1, 2:])
    out = np.zeros(keep.shape + (4,), np.uint8)
    out[..., :3] = np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8)
    out[..., 3] = code
    lone = ring & (c.code == 0)  # only where the sheet had nothing: a cut edge keeps the body's own pixels out
    out[lone, :3] = outline_rgb
    out[lone, 3] = OUTLINE
    out[code == OUTLINE, :3] = c.rgb[code == OUTLINE] * 255
    has = out[..., 3] > 0
    rows, cols = np.where(has.any(axis=1))[0], np.where(has.any(axis=0))[0]
    return out[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]


# ---------------------------------------------------------------- the man

def man_frame(cell, height_m):
    """The middle of the head, the feet's line and the figure's height in pixels (as gibs.figure_frame), or None."""
    solid = cell.solid
    tall0 = height_m * math.cos(math.radians(layout.ELEVATION_DEG)) * cell.ppm
    ax = int(round(cell.ax))
    wide, narrow = int(0.25 * tall0), max(2, int(0.09 * tall0))
    body = solid[:, max(0, ax - wide):ax + wide]
    rows = np.where(body.any(axis=1))[0]
    if not len(rows):
        return None
    ground = float(rows[-1] + 1)
    # The top of the head: the type's height above the anchor (the sheets are drawn to it; a pike, a club or a
    # plume stands higher, so the picture's own top is not it).
    top = cell.ay - tall0
    tall = ground - top
    if tall < 8:
        return None
    c0 = max(0, ax - narrow * 2)
    band = solid[max(0, int(top)):int(top + max(2, 0.1 * tall)), c0:ax + narrow * 2]
    xs = np.nonzero(band)[1]
    cx = float(c0 + np.median(xs) + 0.5) if len(xs) else cell.ax
    return cx, ground, tall


def thin_piece(solid, reach):
    """The longest thin thing in a picture (a blade, a spear, a lance): what a body's width of wearing away removes."""
    core = solid.copy()
    for _ in range(3):
        c = core.copy()
        c[1:] &= core[:-1]
        c[:-1] &= core[1:]
        c[:, 1:] &= core[:, :-1]
        c[:, :-1] &= core[:, 1:]
        c[0] = c[-1] = False
        c[:, 0] = c[:, -1] = False
        core = c
    thick, _ = gibs.grow(core, 4)
    best, length = None, reach
    for p in pieces(solid & ~thick)[:6]:
        r, c = np.nonzero(p)
        d = math.hypot(r.max() - r.min() + 1, c.max() - c.min() + 1)
        if d >= length and p.sum() <= d * max(5.0, 0.12 * d):
            best, length = p, d
    return best


def man_parts(views, height_m, wing, listed):
    """The man's parts by (part, variant): gibs.CUTS' outlines on his idle pictures from behind and in front."""
    out, frames = {}, {}
    for (part, variant), (view, outline) in gibs.CUTS.items():
        if part == 'shield' and 'shield' not in listed:
            continue
        cell = views[view]
        if view not in frames:
            frames[view] = man_frame(cell, height_m)
        if frames[view] is None:
            continue
        cx, ground, tall = frames[view]

        def px(u, w):
            return cx + u * tall, ground - w * tall

        solid = cell.solid
        if outline[0] == 'blob':
            x, y = px(outline[1], outline[2])
            region = gibs.blob_mask(solid.shape, x, y, outline[3] * tall)
        else:
            region = gibs.polygon_mask(solid.shape, [px(u, w) for u, w in outline])
        keep = gibs.largest_piece(region & solid)
        clean = part in gibs.CLEAN
        if keep.sum() >= 8:
            out[(part, variant)] = Part(cell, keep, None if clean else solid & ~region, gore=part == 'chunk')
    if ('weapon', 0) not in out and ('weapon', 1) not in out and frames.get(gibs.BACK):
        # Not where the outlines look for it: the longest thin thing in his picture from the side.
        blade = thin_piece(views[SIDE].solid, 0.3 * frames[gibs.BACK][2])
        if blade is not None:
            out[('weapon', 0)] = out[('weapon', 1)] = Part(views[SIDE], blade)
    else:
        for v in (0, 1):  # one look found: both
            if ('weapon', v) in out and ('weapon', 1 - v) not in out:
                out[('weapon', 1 - v)] = out[('weapon', v)]
    if wing and gibs.BACK in frames and frames[gibs.BACK] is not None:
        # What spreads beyond the shoulders, seen from behind: a wing a side.
        cell = views[gibs.BACK]
        cx, ground, tall = frames[gibs.BACK]
        solid = cell.solid
        xs = np.arange(solid.shape[1])[None, :] + 0.5
        ys = np.arange(solid.shape[0])[:, None] + 0.5
        high = ys < ground - 0.45 * tall
        for variant, side in ((0, xs > cx + 0.17 * tall), (1, xs < cx - 0.17 * tall)):
            region = side & high
            out[('wing', variant)] = Part(cell, gibs.largest_piece(region & solid), solid & ~region)
    return out


# ---------------------------------------------------------------- the beast

def pieces(mask):
    """The mask's 8-connected pieces, largest first."""
    left, found = mask.copy(), []
    while left.any():
        one = gibs.largest_piece(left)
        found.append(one)
        left &= ~one
        if len(found) > 12:
            break
    return found


def beast_parts(cell, rider, flier):
    """A four-legged (or eight-legged) body seen from its right side, facing right, cut along its own lines."""
    solid = cell.solid
    H, W = solid.shape
    out = {}
    rows = np.where(solid.any(axis=1))[0]
    if not len(rows):
        return out
    ground = rows[-1] + 1
    # Each row's longest unbroken run: legs are narrow runs with gaps between, the body one long one.
    run = np.zeros(H, np.float32)
    for y in rows:
        edges = np.flatnonzero(np.diff(np.concatenate(([0], solid[y].astype(np.int8), [0]))))
        run[y] = (edges[1::2] - edges[::2]).max() if len(edges) else 0
    ref = np.percentile(run[rows], 90)
    # The belly line: going up from the ground, the first row the body is across.
    belly = ground
    for y in range(ground - 1, rows[0], -1):
        if run[y] >= 0.45 * ref:
            belly = y + 1
            break
    ys = np.arange(H)[:, None]
    xs = np.arange(W)[None, :]
    under = solid & (ys >= belly)
    upper = solid & (ys < belly)
    # Legs: what hangs under the belly and reaches the ground.
    legs = []
    for p in pieces(under):
        # Less the strip of belly it hangs from: rows far wider than the leg.
        w = p.sum(axis=1)
        p = gibs.largest_piece(p & (w <= 2.5 * np.median(w[w > 0]))[:, None])
        r = np.where(p.any(axis=1))[0]
        if p.sum() >= 6 and r[-1] + 1 >= ground - 0.2 * (ground - belly) and r[-1] - r[0] + 1 >= 0.5 * (ground - belly):
            legs.append(p)
    legs.sort(key=lambda p: -np.where(p.any(axis=0))[0].mean())  # front first
    if legs:
        out[('leg', 0)] = Part(cell, legs[0], upper)
        out[('leg', 1)] = Part(cell, legs[-1], upper)
    # How long the body is: its thick part's columns (a spider's legs, a tail's tip and a lance are thin).
    core, r = upper.copy(), max(1, int(round(0.03 * math.sqrt(float(solid.sum())))))
    for _ in range(r):
        c = core.copy()
        c[1:] &= core[:-1]
        c[:-1] &= core[1:]
        c[:, 1:] &= core[:, :-1]
        c[:, :-1] &= core[:, 1:]
        core = c
    core = gibs.largest_piece(core) if core.any() else upper
    cols = np.where(core.any(axis=0))[0]
    if not len(cols):
        return out
    x0, x1 = cols[0], cols[-1] + 1
    # The head and neck: the front quarter and a bit.
    neck = x1 - int(round(0.27 * (x1 - x0)))
    front = upper & (xs >= neck)
    head = gibs.largest_piece(front)
    rest = upper & (xs < neck)
    # The back line: as high as the body stands along its middle, where nothing rides on it.
    tops = [np.argmax(rest[:, x]) for x in range(x0 + int(0.15 * (x1 - x0)), neck) if rest[:, x].any()]
    riding = None
    if tops:
        back = int(np.percentile(tops, 75))  # rows grow downward: the low quarter of the tops
        above = rest & (ys < back - max(1, int(0.03 * (belly - rows[0]))))
        if above.any():
            top = gibs.largest_piece(above)
            if top.sum() >= 0.04 * solid.sum():
                riding = top
    barrel = rest & ~riding if riding is not None else rest
    barrel = gibs.largest_piece(barrel)
    out[('torso', 0)] = out[('torso', 1)] = Part(cell, barrel, solid & ~barrel)
    if rider and riding is not None:
        # The man up there: his head is the top of him, where he is first as wide as a head.
        r = np.where(riding.any(axis=1))[0]
        w = riding.sum(axis=1)
        wide = np.where(w >= 0.3 * w.max())[0]
        top = wide[0] if len(wide) else r[0]
        chin = top + max(2, int(round(0.32 * (r[-1] + 1 - top))))
        his_head = gibs.largest_piece(riding & (ys >= top) & (ys < chin))
        his_body = gibs.largest_piece(riding & (ys >= chin))
        out[('head', 0)] = out[('head', 1)] = Part(cell, his_head, riding & ~his_head)
        out[('arm', 0)] = out[('arm', 1)] = Part(cell, his_body, solid & ~his_body)
        out[('mount_head', 0)] = out[('mount_head', 1)] = Part(cell, head, solid & ~head)
        if legs:
            out[('mount_leg', 0)], out[('mount_leg', 1)] = out[('leg', 0)], out[('leg', 1)]
        lance = thin_piece(solid & (ys < belly), 0.5 * (belly - rows[0]))
        if lance is not None:
            out[('weapon', 0)] = out[('weapon', 1)] = Part(cell, lance)
    else:
        out[('head', 0)] = out[('head', 1)] = Part(cell, head, solid & ~head)
        if riding is not None and flier:
            out[('wing', 0)] = out[('wing', 1)] = Part(cell, riding, solid & ~riding)
        elif riding is not None:
            whole = gibs.largest_piece(rest)
            out[('torso', 0)] = out[('torso', 1)] = Part(cell, whole, solid & ~whole)
            barrel = whole
    # Gobbets: of the barrel's middle.
    r, c = np.nonzero(barrel)
    if len(r):
        size = math.sqrt(float(barrel.sum()))
        for variant, (dx, dy, k) in enumerate(((-0.12, 0.0, 0.2), (0.15, 0.05, 0.16))):
            blob = gibs.blob_mask(solid.shape, c.mean() + dx * size, r.mean() + dy * size, k * size)
            out[('chunk', variant)] = Part(cell, gibs.largest_piece(blob & barrel), None, gore=True)
    return out


def engine_parts(cell):
    """Two baulks of an engine's timber, from the middle of its side."""
    solid = cell.solid
    r, c = np.nonzero(solid)
    out = {}
    if len(r):
        size = math.sqrt(float(solid.sum()))
        for variant, (dx, dy, k) in enumerate(((-0.2, 0.1, 0.22), (0.2, -0.05, 0.18))):
            blob = gibs.blob_mask(solid.shape, c.mean() + dx * size, r.mean() + dy * size, k * size)
            out[('chunk', variant)] = Part(cell, gibs.largest_piece(blob & solid), None)
    return out


# ---------------------------------------------------------------- a sheet

def plan_of(unit):
    parts = layout.UNITS[unit][6]
    if gibs.cuttable(unit):
        return 'man'
    return 'beast' if 'torso' in parts else 'engine'


def cut_sheet(px, meta, unit):
    """{slot index (0..19): RGBA picture} for a sheet's pixels (top row first) and its json."""
    fpv = int(meta.get('framesPerView', layout.LEGACY_FRAMES_PER_VIEW))
    idle = 0
    for k in meta.get('clips') or []:
        if k['name'] == 'idle':
            idle = int(k['first'])
    cells = meta['cells']
    views = {v: Cell(px, cells[v * fpv + idle]) for v in range(layout.VIEWS)}
    number, klass, height, attack, flier, ranged, listed, look = layout.UNITS[unit]
    plan = plan_of(unit)
    if plan == 'man':
        parts = man_parts(views, height, 'wing' in listed, listed)
    elif plan == 'beast':
        parts = beast_parts(views[SIDE], 'mount_head' in listed and 'head' in listed, flier)
    else:
        parts = engine_parts(views[SIDE])
    codes = px[..., 3]
    dark = px[codes == OUTLINE][:, :3]
    outline_rgb = dark[0] if len(dark) else np.array([20, 16, 14], np.uint8)
    out = {}
    for (part, variant), p in parts.items():
        im = finish(p, unit not in BLOODLESS, outline_rgb)
        if im is not None:
            out[layout.GIB_PARTS.index(part) * layout.GIB_VARIANTS + variant] = im
    return out


def pack(px, meta, parts):
    """The sheet with the parts in new rows above its old ones, and its json with their cells."""
    H, W = px.shape[:2]
    fpv = int(meta.get('framesPerView', layout.LEGACY_FRAMES_PER_VIEW))
    ppm = float(meta['ppm'])
    place, x, y, shelf = {}, PAD, PAD, 0
    for slot in sorted(parts):
        h, w = parts[slot].shape[:2]
        if x + w + PAD > W:
            x, y, shelf = PAD, y + shelf + PAD, 0
        place[slot] = (x, y)
        x += w + PAD
        shelf = max(shelf, h)
    extra = y + shelf + PAD
    extra += (-extra) % 4
    out = np.zeros((H + extra, W, 4), np.uint8)
    out[extra:] = px
    rows = [list(r) for r in meta['cells'][:layout.VIEWS * fpv]]
    for slot in range(layout.GIB_CELLS):
        if slot not in parts:
            rows.append([0, 0, 0, 0, 0, 0, 0, 0])
            continue
        im = parts[slot]
        h, w = im.shape[:2]
        px0, up = place[slot]  # up: rows above the old sheet's top, to the part's bottom row
        top = extra - up - h
        out[top:top + h, px0:px0 + w] = im
        rows.append([px0, H + up, w, h, round(-w / 2 / ppm, 5), round(-h / 2 / ppm, 5), round(w / ppm, 5), round(h / ppm, 5)])
    new = dict(meta)
    new['cells'] = rows
    new['size'] = [W, H + extra]
    new['gibs'] = 'cut'  # by this tool, from the sheet's own idle pictures
    return out, new


def review(parts, path, scale):
    names = [layout.GIB_PARTS[s // 2] + '_' + 'ab'[s % 2] for s in sorted(parts)]
    ims = [Image.fromarray(parts[s], 'RGBA') for s in sorted(parts)]
    for im in ims:
        im.putalpha(im.getchannel('A').point(lambda a: 255 if a > 100 else 0))
    ims = [im.resize((im.width * scale, im.height * scale), Image.NEAREST) for im in ims]
    W = sum(im.width + 8 for im in ims) + 8
    Hh = max(im.height for im in ims) + 16
    sheet = Image.new('RGB', (W, Hh), (96, 112, 84))
    x = 8
    for im in ims:
        sheet.paste(im, (x, Hh - 8 - im.height), im)
        x += im.width + 8
    sheet.save(path)
    return names


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--sprites', default=os.path.join(ROOT, 'game', 'web', 'sprites'))
    ap.add_argument('--units', help='comma-separated unit ids (default: every sheet in the index)')
    ap.add_argument('--review', help='a folder for a picture of each sheet\'s parts')
    ap.add_argument('--check', action='store_true', help='write nothing')
    args = ap.parse_args(argv)
    index = json.load(open(os.path.join(args.sprites, 'index.json')))
    hd_path = os.path.join(args.sprites, 'index_hd.json')
    hd = json.load(open(hd_path)) if os.path.exists(hd_path) else {}
    units = args.units.split(',') if args.units else [index[k] for k in sorted(index, key=int)]
    hd_changed = False
    for unit in units:
        number = str(layout.UNITS[unit][0])
        for name in (unit, unit + '_hd'):
            jpath, wpath = (os.path.join(args.sprites, name + e) for e in ('.json', '.webp'))
            if not os.path.exists(jpath):
                continue
            meta = json.load(open(jpath))
            fpv = int(meta.get('framesPerView', layout.LEGACY_FRAMES_PER_VIEW))
            if len(meta['cells']) != layout.VIEWS * fpv:
                print('%s: has its gib cells, left alone' % name)
                if args.review:
                    # The parts it holds, as they are in the sheet.
                    px = np.array(Image.open(wpath).convert('RGBA'))
                    held = {}
                    for slot, r in enumerate(meta['cells'][layout.VIEWS * fpv:]):
                        if r[2] > 0:
                            held[slot] = px[px.shape[0] - r[1] - r[3]:px.shape[0] - r[1], r[0]:r[0] + r[2]].copy()
                    if held:
                        os.makedirs(args.review, exist_ok=True)
                        review(held, os.path.join(args.review, name + '.png'), 2 if name.endswith('_hd') else 4)
                continue
            px = np.array(Image.open(wpath).convert('RGBA'))
            parts = cut_sheet(px, meta, unit)
            names = [layout.GIB_PARTS[s // 2] + '_' + 'ab'[s % 2] for s in sorted(parts)]
            print('%s (%s): %s' % (name, plan_of(unit), ', '.join(names) if names else 'nothing cut'))
            if args.review and parts:
                os.makedirs(args.review, exist_ok=True)
                review(parts, os.path.join(args.review, name + '.png'), 4 if not name.endswith('_hd') else 2)
            if args.check or not parts:
                continue
            out, new = pack(px, meta, parts)
            if out.shape[0] > 4096:
                print('%s: would be taller than 4096, left alone' % name)
                continue
            Image.fromarray(out, 'RGBA').save(wpath, lossless=True, quality=100, method=6, exact=True)
            with open(jpath, 'w') as f:
                json.dump(new, f, separators=(',', ':'))
            if name.endswith('_hd') and number in hd:
                hd[number]['size'] = new['size']
                hd_changed = True
    if hd_changed:
        with open(hd_path, 'w') as f:
            json.dump(hd, f, separators=(',', ':'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
