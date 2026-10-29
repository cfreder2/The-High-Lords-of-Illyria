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
import importlib
import os
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

import layout  # noqa: E402
import templates  # noqa: E402

importer = importlib.import_module('import')  # tools/sprites/import.py: its keyer

CODEX_IMAGES = os.path.expanduser('~/.codex/generated_images')

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


def fit(plate_path, alone, clip, frame):
    """The plate keyed and fitted onto the delivery canvas over the guide's figure."""
    class Quiet:
        def warn(self, s):
            print('  ! ' + s)
    im = importer.keyed(plate_path, Quiet())
    got = extent(im[..., 3])
    want = extent(np.asarray(alone, dtype=np.float32)[..., 3] / 255.0)
    if got is None or want is None:
        raise RuntimeError('nothing to fit (an empty plate or no guide figure)')
    (gx0, gy0, gx1, gy1), (gfx, gfy) = got
    (wx0, wy0, wx1, wy1), (wfx, wfy) = want
    s = (wx1 - wx0) / (gx1 - gx0) if lying(clip, frame) else (wy1 - wy0) / (gy1 - gy0)
    rgba = Image.fromarray((np.clip(im, 0, 1) * 255).astype(np.uint8))
    w, h = max(1, round(rgba.width * s)), max(1, round(rgba.height * s))
    rgba = rgba.resize((w, h), Image.Resampling.LANCZOS)
    out = Image.new('RGBA', alone.size, (0, 0, 0, 0))
    x, y = round(wfx - gfx * s), round(wfy - gfy * s)
    # alpha_composite needs a non-negative destination: crop what falls off the canvas.
    sx, sy = max(0, -x), max(0, -y)
    rgba = rgba.crop((sx, sy, rgba.width, rgba.height))
    out.alpha_composite(rgba, (x + sx, y + sy))
    return out, s


def prompt(unit, clip, view, frame, backdrop, identity, style):
    look = layout.manifest()['units'][unit]['look']
    name = unit.replace('_', ' ')
    lines = [
        'Image 1 is a pose guide: a rough 3D figure on a grey grid, a red cross where his feet stand, a ground line, '
        'and a blue line at the top of his head. Redraw that figure as a finished, detailed, hand-painted game sprite '
        'in the SAME pose, facing the SAME way, the SAME size and in the SAME place in the picture: his feet on the red '
        'cross, the top of his head at the blue line. Do NOT copy the grid, the lines, the cross, the scale bar or any '
        'text. Paint the figure alone on a perfectly flat, plain %s background that fills the whole picture to every '
        'edge: no ground, no shadow, no scenery, no border, no text.' % backdrop,
        'The figure: %s, %s. %s %s' % (name, look, VIEW_PHRASE[view], CLIP_PHRASE[clip].format(n=frame + 1)),
        'Fantasy medieval, painterly and realistic like classic 1990s strategy game unit art, crisp clean edges, rich '
        'detail in the armour and cloth, lit from the upper left front with a soft fill from the right. Orthographic '
        'view from 30 degrees above the horizon, no perspective distortion.',
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
    ap.add_argument('--backdrop', help='flat background colour (default #00FF00; #FF00FF for green units)')
    ap.add_argument('--model', help='the relaying codex agent (-m)')
    ap.add_argument('--force', action='store_true', help='redraw cells that exist')
    ap.add_argument('--dry-run', action='store_true', help='write the guides and print the prompts; draw nothing')
    args = ap.parse_args(argv)

    if args.unit not in layout.UNITS:
        raise SystemExit('unknown unit %s; one of: %s' % (args.unit, ', '.join(layout.UNITS)))
    backdrop = args.backdrop or ('#FF00FF' if args.unit in ('dragons',) else '#00FF00')
    clips = [c for c in layout.CLIPS if not args.clip or c[0] in args.clip]
    work = args.work or args.out.rstrip('/') + '-work'
    drawn = failed = 0
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
                    print('  drawn in %.0f s, scaled %.2f' % (time.time() - t, s))
                except Exception as e:  # noqa: BLE001 -- one bad cell must not stop the run
                    failed += 1
                    print('  FAILED: %s' % e)
    print('%d drawn, %d failed -> %s' % (drawn, failed, os.path.join(args.out, args.unit)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
