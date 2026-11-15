#!/usr/bin/env python3
"""A packed sheet's idle, attack and corpse in both sides' colours, as the game dyes them (the alpha code 191 is
the side's cloth): to check the team mask took the cloth and nothing else.

    python3 tools/sprites/tripo/colours.py game/web/sprites UNIT OUT.png"""
import json
import sys

import numpy as np
from PIL import Image

sprites, unit, out = sys.argv[1:4]
meta = json.load(open('%s/%s_hd.json' % (sprites, unit)))
im = np.asarray(Image.open('%s/%s_hd.webp' % (sprites, unit)).convert('RGBA')).astype(np.float32)
clips = {c['name']: c for c in meta['clips']}
fpv = sum(c['frames'] for c in meta['clips'])
picks = []
for view in (0, 1, 3):
    picks.append(view * fpv + clips['idle']['first'])
a = clips['attack']
picks += [fpv * 1 + a['first'] + a['frames'] // 2, fpv * 1 + clips['walk']['first'] + 2,
          fpv * 1 + clips['die_back']['first'] + clips['die_back']['frames'] - 1]
SIDES = [(0.18, 0.32, 0.85), (0.80, 0.12, 0.10)]
cells = meta['cells']
tiles = []
for cloth in SIDES:
    row = []
    for k in picks:
        c = cells[k]
        x, y, w, h = [int(v) for v in c[:4]]
        y = im.shape[0] - y - h   # cells count from the bottom of the atlas
        t = im[y:y + h, x:x + w].copy()
        team = np.abs(t[..., 3] - 191) < 20
        t[team, :3] *= np.array(cloth)
        t[..., 3] = np.where(t[..., 3] > 127, 255, 0)
        row.append(Image.fromarray(t.astype(np.uint8)))
    tiles.append(row)
W = sum(t.width for t in tiles[0]) + 12 * len(picks)
H = max(t.height for t in tiles[0]) + 12
page = Image.new('RGB', (W, H * 2), (118, 128, 112))
for r, row in enumerate(tiles):
    x = 6
    for t in row:
        page.paste(t, (x, r * H + H - 6 - t.height), t)
        x += t.width + 12
page.save(out)
print('both sides:', out)
