"""The whole roster on one picture, from the installed sheets
(game/web/sprites): every type idle in the front view, at one scale, in the
player's blue and the enemy's red as the game tints them (kUnitFs multiplies
the team pixels by cloth(team)).

    python tools/blender/roster_sheet.py docs/images/sprites/roster.png [px a metre] [view] [frame]
"""

import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SPRITES = os.path.join(HERE, '..', '..', 'game', 'web', 'sprites')
PLAYER, ENEMY = (0.22, 0.42, 0.92), (0.86, 0.18, 0.14)  # renderer.cpp's kPlayerColor, kEnemyColor
GROUND = (78, 96, 62)


def cloth(t):
    g = sum(t) / 3
    return tuple(c + (g - c) * 0.25 for c in t)


def cell(name, view, frame):
    meta = json.load(open(os.path.join(SPRITES, name + '.json')))
    atlas = np.asarray(Image.open(os.path.join(SPRITES, name + '.webp')).convert('RGBA'), dtype=np.float32) / 255.0
    H = atlas.shape[0]
    x, y, w, h, u0, v0, uw, vh = meta['cells'][view * 26 + frame]
    x, y, w, h = int(x), int(y), int(w), int(h)
    img = atlas[H - (y + h):H - y, x:x + w]
    return img, u0, v0, meta['ppm'], meta['type']


def tinted(img, team):
    a = np.round(img[..., 3] * 255)
    out = np.zeros_like(img)
    out[..., :3] = img[..., :3]
    tm = np.abs(a - 191) < 3
    out[tm, :3] = img[tm, :3] * np.array(cloth(team))
    out[..., 3] = (a > 0).astype(np.float32)
    return out


def main(argv):
    out = argv[0] if argv else os.path.join(HERE, '..', '..', 'docs', 'images', 'sprites', 'roster.png')
    ppm = float(argv[1]) if len(argv) > 1 else 56.0
    view = int(argv[2]) if len(argv) > 2 else 0
    frame = int(argv[3]) if len(argv) > 3 else 0
    index = json.load(open(os.path.join(SPRITES, 'index.json')))
    tiles = []
    for number, name in sorted(index.items(), key=lambda kv: int(kv[0])):
        img, u0, v0, sp, _ = cell(name, view, frame)
        pics = []
        for team in (PLAYER, ENEMY):
            t = Image.fromarray(np.clip(tinted(img, team) * 255, 0, 255).astype(np.uint8), 'RGBA')
            k = ppm / sp
            t = t.resize((max(1, round(t.width * k)), max(1, round(t.height * k))), Image.LANCZOS)
            pics.append(t)
        tiles.append((name, pics, u0 * ppm, v0 * ppm))
    font = ImageFont.load_default()
    pad, max_w = 12, 1900
    # Flowed into rows, each tile as wide as its pair of figures (and its label).
    widths = [max(sum(p.width for p in t[1]) + pad, 6 * len(t[0]) + 4) + 2 * pad for t in tiles]
    rows, row, x = [], [], 0
    for t, w in zip(tiles, widths):
        if row and x + w > max_w:
            rows.append(row)
            row, x = [], 0
        row.append((t, w))
        x += w
    rows.append(row)
    heights = [int(max(max(p.height for p in t[1]) + max(0, -t[3]) for t, _ in r)) + 2 * pad + 16 for r in rows]
    W, H = max(sum(w for _, w in r) for r in rows), sum(heights)
    sheet = Image.new('RGBA', (W, H), GROUND + (255,))
    d = ImageDraw.Draw(sheet)
    y = 0
    for r, rh in zip(rows, heights):
        ground = y + rh - 16 - pad
        x0 = 0
        for (name, pics, ux, vy), w in r:
            x = x0 + pad
            for p in pics:
                sheet.alpha_composite(p, (int(x), int(ground - vy - p.height)))
                x += p.width + pad
            d.text((x0 + pad, ground + 4), name.replace('_', ' '), fill=(235, 230, 210, 255), font=font)
            x0 += w
        y += rh
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    sheet.convert('RGB').save(out, optimize=True)
    print('roster: %d types, %dx%d -> %s' % (len(tiles), W, H, out))


if __name__ == '__main__':
    main(sys.argv[1:])
