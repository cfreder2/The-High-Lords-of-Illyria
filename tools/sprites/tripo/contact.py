#!/usr/bin/env python3
"""Contact sheets of a unit's rendered cells, to judge the motion before it is packed: a page per clip, a row per
view, every frame, cropped round the anchor, the team mask's cloth shown as rendered.

    python3 tools/sprites/tripo/contact.py ART/UNIT OUT_DIR [--views 2,0,1] [--half-m 1.3]"""
import json
import os
import sys

from PIL import Image, ImageDraw

src, out = sys.argv[1], sys.argv[2]
views = [int(v) for v in sys.argv[sys.argv.index('--views') + 1].split(',')] if '--views' in sys.argv else [2, 1, 0]
half = float(sys.argv[sys.argv.index('--half-m') + 1]) if '--half-m' in sys.argv else 1.3
unit = os.path.basename(src.rstrip('/'))
os.makedirs(out, exist_ok=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import layout  # noqa: E402

with open(os.path.join(src, 'clips.json')) as fh:
    doc = json.load(fh)
clips, k = doc['clips'], doc.get('scale', 1)
klass = layout.unit_class(unit)
PPM, AX, AY = klass['ppm'] * k, klass['anchor'][0] * k, klass['anchor'][1] * k
if '--half-m' not in sys.argv:
    half = layout.UNITS[unit][2] * 0.75
box = (int(AX - half * PPM), int(AY - 1.5 * PPM * half), int(AX + half * PPM), int(AY + 0.4 * PPM * half))
cw, ch = 250, int(250 * (box[3] - box[1]) / (box[2] - box[0]))
for c in clips:
    n = c['frames']
    page = Image.new('RGB', (cw * n, ch * len(views) + 22), (128, 132, 128))
    dr = ImageDraw.Draw(page)
    times = c.get('times') or [c.get('seconds', 1.0) * i / n for i in range(n)]
    dr.text((6, 5), '%s  %s: %d frames, %.2f s%s%s' % (unit, c['name'], n, c.get('seconds', 0), ', loop' if c.get('loop') else '',
                                                    ', release %.2f s' % c['release'] if 'release' in c else ''), fill=(255, 255, 255))
    for r, v in enumerate(views):
        for f in range(n):
            im = Image.open(os.path.join(src, c['name'], 'v%d_f%02d.png' % (v, f))).convert('RGBA').crop(box).resize((cw, ch), Image.LANCZOS)
            page.paste(im, (f * cw, 22 + r * ch), im)
            dr.text((f * cw + 4, 24 + r * ch), 'v%d f%02d  %.2fs' % (v, f, times[f]), fill=(230, 230, 120))
    page.save(os.path.join(out, '%s_%s.png' % (unit, c['name'])))
print('contact sheets:', out)
