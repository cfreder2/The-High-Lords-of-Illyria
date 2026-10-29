#!/usr/bin/env python3
"""Import the artist's sprite pictures into the game's sprite sheets.

    python3 tools/sprites/import.py ART_DIR [--units a,b] [--out game/web/sprites]
                                    [--baseline tools/sprites/baseline] [--snap-feet] [--check]

ART_DIR holds one folder per unit type, named as in the manifest
(docs/sprite_templates/manifest.json, docs/SPRITE_SPEC.md):

    ART_DIR/heavy_infantry/walk/v2_f03.png         one picture per cell
    ART_DIR/heavy_infantry/walk/v2_f03_team.png    optional team mask (white = team)
    ART_DIR/heavy_infantry/gibs/arm_a.png          gib parts

For each unit folder: checks every file name and canvas size against the
manifest, reports what is missing, keys out the background (alpha, or a flat
colour sampled from the corners), builds the team mask from the key hue (or
the _team.png), checks the feet stand on the anchor (--snap-feet moves each
clip's frames together so they do), scales to the game's pixels a metre for
the normal sheet and to twice that for the close-up sheet, trims, packs and
writes <unit>.webp/.json and <unit>_hd.webp/.json into --out, with the
sprites/index.json and index_hd.json entries. Any cell not delivered is taken
from the type's sheet as it was before the first import (kept in --baseline),
so a partial delivery works; a clip the old sheet lacks borrows another's
cells (die_front -> die_back, ...).

--check validates and reports only, writing nothing; it exits 1 on errors.
The output depends only on the inputs: running it again changes nothing.
"""

import argparse
import json
import math
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'blender'))

import layout  # noqa: E402
import sheet  # noqa: E402
from sprites import pack  # noqa: E402  (resolve: the cutout, edge darkening and outline, as the bake does)

NORMAL_AREA = 2048 * 2048 / 1.35  # texels the normal sheet's cells may cover (render.choose_ppm)
HD_AREA = 5.5e6                   # the close-up sheet's, so the atlas stays inside closeup::kBudget
LIVING = ('idle', 'walk', 'attack', 'flinch')
FILE_RE = re.compile(r'^(?:(?P<clip>[a-z_]+)/v(?P<view>\d)_f(?P<frame>\d\d)|gibs/(?P<part>[a-z_]+)_(?P<var>[ab]))(?P<team>_team)?\.png$')


class Report:
    def __init__(self):
        self.errors, self.warnings, self.notes = [], [], []

    def error(self, s):
        self.errors.append(s)

    def warn(self, s):
        self.warnings.append(s)

    def note(self, s):
        self.notes.append(s)


# ---------------------------------------------------------------- pictures

def keyed(path, report):
    """RGBA float (straight colour) with the background keyed out."""
    im = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32) / 255.0
    a = im[..., 3]
    if a.min() < 0.5:  # the tool wrote alpha
        return im
    # A flat background: the corners' colour (the spec asks for #00FF00, but any flat colour works).
    k = max(4, min(im.shape[0], im.shape[1]) // 64)
    corners = np.concatenate([im[:k, :k, :3].reshape(-1, 3), im[:k, -k:, :3].reshape(-1, 3),
                              im[-k:, :k, :3].reshape(-1, 3), im[-k:, -k:, :3].reshape(-1, 3)])
    bg = np.median(corners, axis=0)
    if np.abs(corners - bg).max() > 0.12:
        report.warn('%s: no alpha and no flat background colour in the corners' % path)
    d = np.sqrt(((im[..., :3] - bg) ** 2).sum(axis=2))
    alpha = np.clip((d - 0.12) / 0.18, 0.0, 1.0)
    rgb = im[..., :3].copy()
    # Spill: the edge pixels lose the background's tint.
    edge = (alpha > 0) & (alpha < 1)
    if edge.any():
        dom = int(np.argmax(bg))
        others = [c for c in range(3) if c != dom]
        cap = np.maximum(rgb[..., others[0]], rgb[..., others[1]])
        rgb[..., dom] = np.where(edge, np.minimum(rgb[..., dom], cap), rgb[..., dom])
    return np.concatenate([rgb, alpha[..., None]], axis=2)


def team_mask(im, team_path):
    """Team share 0..1 of each pixel, and the colour with team pixels made grey (the shader dyes them)."""
    rgb = im[..., :3]
    if team_path:
        m = np.asarray(Image.open(team_path).convert('L'), dtype=np.float32) / 255.0
        team = (m > 0.5).astype(np.float32)
    else:
        mx, mn = rgb.max(axis=2), rgb.min(axis=2)
        sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
        r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        # Hue in degrees (blue-ish pixels only matter).
        hue = np.degrees(np.arctan2(math.sqrt(3) * (g - b), 2 * r - g - b)) % 360
        dh = np.abs((hue - layout.TEAM_KEY_HUE + 180) % 360 - 180)
        team = ((dh < layout.TEAM_KEY_HUE_TOLERANCE) & (sat >= layout.TEAM_KEY_MIN_SATURATION) & (mx > 0.12)).astype(np.float32)
    grey = rgb.max(axis=2)  # the key colour's brightness is the cloth's shading
    out = rgb.copy()
    out[team > 0.5] = grey[team > 0.5][:, None]
    return out, team


def resample(rgb, alpha, team, ax, ay, src_ppm, dst_ppm):
    """The picture on the sheet's lattice at dst_ppm, anchored at (ax, ay)
    (source pixels from the top left): straight colour, coverage, team share,
    and the lattice's left column and top row (i0, j1) in dest pixels."""
    s = src_ppm / dst_ppm
    pad = int(math.ceil(s)) + 4
    H, W = alpha.shape
    ax, ay = ax + pad, ay + pad

    def plane(x):
        return Image.fromarray(np.pad(x.astype(np.float32), pad), 'F')

    i0, i1 = math.floor(-(ax - pad) / s) - 1, math.ceil((W + pad - ax) / s) + 1
    j0, j1 = math.floor((ay - H - pad) / s) - 1, math.ceil((ay - pad) / s) + 1
    box = (ax + i0 * s, ay - j1 * s, ax + i1 * s, ay - j0 * s)
    size = (i1 - i0, j1 - j0)
    # Coverage and team share by area (as the renders' supersampling); colour with a sharper filter.
    up = s < 1
    cover_f = Image.Resampling.BICUBIC if up else Image.Resampling.BOX
    colour_f = Image.Resampling.BICUBIC if up else Image.Resampling.LANCZOS
    cover = np.asarray(plane(alpha).resize(size, cover_f, box=box))
    tshare = np.asarray(plane(team * alpha).resize(size, cover_f, box=box)) / np.maximum(cover, 1e-6)
    al = np.asarray(plane(alpha).resize(size, colour_f, box=box))
    out = np.stack([np.asarray(plane(rgb[..., c] * alpha).resize(size, colour_f, box=box)) for c in range(3)], axis=2)
    out = np.clip(out / np.maximum(al, 1e-3)[..., None], 0, 1)
    return out, np.clip(cover, 0, 1), np.clip(tshare, 0, 1), i0, j1


def to_cell(rgb, cover, team, i0, j1, ppm):
    img = pack.resolve(rgb, cover, team)
    rows = np.where(img[..., 3].max(axis=1) > 0)[0]
    cols = np.where(img[..., 3].max(axis=0) > 0)[0]
    if len(rows) == 0:
        return None
    r0, r1, c0, c1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
    return {'img': pack.to8(img[r0:r1, c0:c1]), 'u0': float(i0 + c0) / ppm, 'v0': float(j1 - r1) / ppm,
            'w': int(c1 - c0), 'h': int(r1 - r0)}


def rescale_cell(c, src_ppm, dst_ppm):
    """An old sheet's cell at another resolution (its outline dropped and drawn again)."""
    if c is None or abs(src_ppm - dst_ppm) < 1e-6:
        return c
    a = c['img'][..., 3]
    solid = (a > 170).astype(np.float32)
    team = ((a > 170) & (a < 223)).astype(np.float32)
    rgb = c['img'][..., :3].astype(np.float32) / 255.0
    ax, ay = -c['u0'] * src_ppm, c['h'] + c['v0'] * src_ppm
    return to_cell(*resample(rgb, solid, team, ax, ay, src_ppm, dst_ppm), dst_ppm)


def feet(alpha, ppm):
    """The bottom-centre of the figure: (x, y) in pixels, or None."""
    solid = alpha > 0.5
    rows = np.where(solid.any(axis=1))[0]
    if len(rows) == 0:
        return None
    bottom = rows[-1] + 1
    band = solid[max(rows[0], bottom - int(0.15 * ppm)):bottom]
    xs = np.where(band.any(axis=0))[0]
    return (float(xs.min() + xs.max() + 1) / 2, float(bottom)), float(bottom - rows[0])


# ---------------------------------------------------------------- a unit

def scan(art, unit, entries, report):
    """The files delivered for a unit: {slot: (path, team_path)}, checked against the manifest."""
    d = os.path.join(art, unit)
    known = {e['file'][len(unit) + 1:]: e for e in entries}
    found = {}
    for base, _, files in sorted(os.walk(d)):
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(base, f), d).replace(os.sep, '/')
            if not f.lower().endswith('.png'):
                report.warn('%s/%s: not a PNG, ignored' % (unit, rel))
                continue
            m = FILE_RE.match(rel)
            key = rel.replace('_team.png', '.png')
            if not m or key not in known:
                report.error('%s/%s: not a file the manifest names' % (unit, rel))
                continue
            e = known[key]
            with Image.open(os.path.join(base, f)) as im:
                size = list(im.size)
            if size != e['canvas']:
                report.error('%s/%s: canvas %dx%d, expected %dx%d' % (unit, rel, size[0], size[1], *e['canvas']))
                continue
            path, team = found.get(e['slot'], (None, None))
            if m.group('team'):
                team = os.path.join(base, f)
            else:
                path = os.path.join(base, f)
            found[e['slot']] = (path, team)
    for slot, (path, team) in list(found.items()):
        if path is None:
            report.error('%s: a _team.png without its picture (slot %d)' % (unit, slot))
            del found[slot]
    missing_req = [e['file'] for e in entries if e['required'] and e['slot'] not in found]
    missing_opt = [e['file'] for e in entries if not e['required'] and e['slot'] not in found]
    delivered = len(found)
    report.note('%s: %d delivered, %d required missing, %d optional missing' % (unit, delivered, len(missing_req), len(missing_opt)))
    if missing_req:
        report.note('  missing (required): ' + ', '.join(sorted(m[len(unit) + 1:] for m in missing_req)[:12]) +
                    (' ...' if len(missing_req) > 12 else ''))
    return found


def baseline_sheet(unit, out, baseline, suffix, write):
    """The type's sheet as it was before any import (copied aside the first time)."""
    b = os.path.join(baseline, unit + suffix + '.json')
    if os.path.exists(b):
        return sheet.read(b)
    o = os.path.join(out, unit + suffix + '.json')
    if not os.path.exists(o):
        return None, None
    meta, cells = sheet.read(o)
    if meta.get('source') == 'import':
        return None, None  # imported, but its baseline is gone: no fallback
    if write:
        os.makedirs(baseline, exist_ok=True)
        for ext in ('.json', '.webp'):
            shutil.copyfile(os.path.join(out, unit + suffix + ext), os.path.join(baseline, unit + suffix + ext))
    return meta, cells


def old_slot(slot, fpv_old):
    """The old sheet's cell standing in for a slot of the new layout (or None)."""
    nv = layout.VIEWS * layout.FRAMES_PER_VIEW
    if slot >= nv:
        return None if fpv_old == layout.LEGACY_FRAMES_PER_VIEW else slot
    v, f = divmod(slot, layout.FRAMES_PER_VIEW)
    if fpv_old == layout.FRAMES_PER_VIEW:
        return slot
    return v * fpv_old + f if f < fpv_old else None


def alias_slot(slot):
    """The slot a missing clip's cell borrows (die_front -> die_back ...), or None."""
    nv = layout.VIEWS * layout.FRAMES_PER_VIEW
    if slot >= nv:
        return None
    v, f = divmod(slot, layout.FRAMES_PER_VIEW)
    clip, i = layout.clip_of(f)
    to = layout.FALLBACK.get(clip)
    if not to:
        return None
    return v * layout.FRAMES_PER_VIEW + layout.FIRST[to] + min(i, layout.FRAMES[to] - 1)


def import_unit(art, unit, man, args, report):
    info = man['units'][unit]
    entries = [e for e in man['cells'] if e['unit'] == unit]
    by_slot = {e['slot']: e for e in entries}
    found = scan(art, unit, entries, report)
    if not found:
        return
    src_ppm = float(info['ppm'])
    ax0, ay0 = info['anchor']
    # Read everything once: keyed, team-masked pictures and their feet.
    pics = {}
    for slot, (path, team_path) in sorted(found.items()):
        im = keyed(path, report)
        rgb, team = team_mask(im, team_path)
        pics[slot] = {'rgb': rgb, 'alpha': im[..., 3], 'team': team, 'entry': by_slot[slot]}
    # Feet on the anchor: each clip of each view is checked (and with --snap-feet moved) as one.
    shifts = {}
    for (clip, v) in sorted({(p['entry']['anim'], p['entry'].get('view')) for p in pics.values() if p['entry']['anim'] in LIVING}):
        offs = []
        for p in pics.values():
            e = p['entry']
            if e['anim'] == clip and e['view'] == v:
                ft = feet(p['alpha'], src_ppm)
                if ft:
                    (fx, fy), tall = ft
                    offs.append((fx - ax0, fy - ay0, tall))
        if not offs:
            continue
        dx = float(np.median([o[0] for o in offs]))
        dy = float(np.median([o[1] for o in offs]))
        tall = float(np.median([o[2] for o in offs]))
        if abs(dx) > 0.3 * src_ppm or dy < -0.15 * src_ppm or dy > 0.3 * src_ppm:
            msg = '%s %s v%d: feet %+.0f, %+.0f px from the anchor' % (unit, clip, v, dx, dy)
            (report.note if args.snap_feet else report.warn)(msg + (' (moved)' if args.snap_feet else ''))
            if args.snap_feet:
                shifts[(clip, v)] = (dx, dy - 0.05 * src_ppm)
        if clip == 'idle':
            expect = info['height_m'] * math.cos(math.radians(layout.ELEVATION_DEG)) * src_ppm
            if not 0.7 < tall / expect < 1.45:
                report.warn('%s idle v%d: figure %.0f px tall, about %.0f expected at %d px/m' % (unit, v, tall, expect, src_ppm))
    # The deaths move with the same view's idle.
    for p in pics.values():
        e = p['entry']
        if e['anim'] == 'gib':
            p['anchor'] = tuple(e['anchor'])
            continue
        sh = shifts.get((e['anim'], e['view'])) or (shifts.get(('idle', e['view'])) if e['anim'] not in LIVING else None) or (0, 0)
        p['anchor'] = (ax0 + sh[0], ay0 + sh[1])

    old = {sfx: baseline_sheet(unit, args.out, args.baseline, sfx, not args.check) for sfx in ('', '_hd')}
    meta0, cells0 = old['']
    base_ppm = float(meta0['ppm']) if meta0 else layout.GAME_PPM
    written = []
    for sfx, ppm_want, limit in (('', base_ppm, NORMAL_AREA), ('_hd', base_ppm * layout.HD_SCALE, HD_AREA)):
        ppm = min(ppm_want, src_ppm)
        # Shrink until the cells fit their atlas (estimated from the pictures' trimmed areas).
        area = 0.0
        for slot in range(layout.CELLS):
            if slot in pics:
                al = pics[slot]['alpha'] > 0.02
                rows, cols = np.where(al.any(axis=1))[0], np.where(al.any(axis=0))[0]
                if len(rows):
                    area += (rows[-1] - rows[0] + 1) * (cols[-1] - cols[0] + 1) / src_ppm ** 2
        mo, co = old[sfx] if old[sfx][0] else old['']
        if mo:
            for slot in range(layout.CELLS):
                k = old_slot(slot, mo['framesPerView'])
                if slot not in pics and k is not None and k < len(co) and co[k]:
                    area += co[k]['w'] * co[k]['h'] / float(mo['ppm']) ** 2
        while area * ppm * ppm > limit and ppm > 8:
            ppm *= 0.95
        ppm = round(ppm, 2)
        cells = [None] * layout.CELLS
        for slot in range(layout.CELLS):
            if slot in pics:
                p = pics[slot]
                cells[slot] = to_cell(*resample(p['rgb'], p['alpha'], p['team'], p['anchor'][0], p['anchor'][1], src_ppm, ppm), ppm)
                if cells[slot] is None:
                    report.warn('%s: %s is empty' % (unit, p['entry']['file']))
            elif mo:
                k = old_slot(slot, mo['framesPerView'])
                if k is not None and k < len(co):
                    cells[slot] = rescale_cell(co[k], float(mo['ppm']), ppm)
        # Clips neither delivered nor in the old sheet borrow another's cells.
        for slot in range(layout.CELLS):
            if cells[slot] is None:
                a = alias_slot(slot)
                while a is not None and cells[a] is None:
                    a = alias_slot(a)
                if a is not None:
                    cells[slot] = a
        holes = [s for s in range(layout.VIEWS * layout.FRAMES_PER_VIEW) if cells[s] is None]
        if holes:
            report.error('%s%s: %d cells neither delivered nor in the old sheet (first: slot %d)' % (unit, sfx, len(holes), holes[0]))
            return
        if args.check:
            continue
        W, H = sheet.write(args.out, unit + sfx, info['type'], ppm, cells, layout.FRAMES_PER_VIEW,
                           {'source': 'import', 'layout': 2}, max_side=2048 if sfx == '' else 4096)
        written.append((sfx, W, H, ppm))
    if written:
        idx_path = os.path.join(args.out, 'index.json')
        hd_path = os.path.join(args.out, 'index_hd.json')
        idx = json.load(open(idx_path)) if os.path.exists(idx_path) else {}
        hdx = json.load(open(hd_path)) if os.path.exists(hd_path) else {}
        for sfx, W, H, ppm in written:
            if sfx:
                hdx[str(info['type'])] = {'name': unit + '_hd', 'size': [W, H], 'ppm': ppm}
            else:
                idx[str(info['type'])] = unit
        with open(idx_path, 'w') as fh:
            json.dump(dict(sorted(idx.items(), key=lambda kv: int(kv[0]))), fh, indent=1)
        with open(hd_path, 'w') as fh:
            json.dump(dict(sorted(hdx.items(), key=lambda kv: int(kv[0]))), fh, indent=1)
        report.note('%s: ' % unit + ', '.join('%s %dx%d at %.1f px/m' % ('close-up' if s else 'sheet', W, H, p) for s, W, H, p in written))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('art', help='the folder of unit folders')
    ap.add_argument('--units', help='comma-separated unit ids (default: every folder in ART that the manifest knows)')
    ap.add_argument('--out', default=os.path.join(ROOT, 'game', 'web', 'sprites'))
    ap.add_argument('--baseline', default=os.path.join(HERE, 'baseline'), help='the sheets as they were before the first import')
    ap.add_argument('--manifest', default=None, help='default: built from tools/sprites/layout.py')
    ap.add_argument('--snap-feet', action='store_true', help='move each clip so the feet stand on the anchor')
    ap.add_argument('--check', action='store_true', help='validate and report only')
    args = ap.parse_args(argv)
    man = json.load(open(args.manifest)) if args.manifest else layout.manifest()
    report = Report()
    names = sorted(d for d in os.listdir(args.art) if os.path.isdir(os.path.join(args.art, d)))
    for d in names:
        if d not in man['units']:
            report.error('%s: not a unit type id (see the manifest)' % d)
    units = args.units.split(',') if args.units else [d for d in names if d in man['units']]
    for unit in units:
        if unit not in man['units']:
            report.error('%s: not a unit type id' % unit)
            continue
        if not os.path.isdir(os.path.join(args.art, unit)):
            report.error('%s: no folder in %s' % (unit, args.art))
            continue
        import_unit(args.art, unit, man, args, report)
    for s in report.notes:
        print(s)
    for s in report.warnings:
        print('warning:', s)
    for s in report.errors:
        print('error:', s)
    print('%d units, %d errors, %d warnings%s' % (len(units), len(report.errors), len(report.warnings), ' (check only)' if args.check else ''))
    return 1 if report.errors else 0


if __name__ == '__main__':
    sys.exit(main())
