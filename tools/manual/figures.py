#!/usr/bin/env python3
"""The manual's drawn figures (docs/manual/images), made from the game's own data.

  python3 tools/manual/figures.py

- units/<type>.png: each unit type standing at ease, cut from its sprite sheet
  (game/web/sprites, the close-up sheet where there is one), dyed in the
  player's blue, its outline drawn, doubled in size with hard pixels.
- formations/<name>.svg: each formation of twelve men seen from above, front
  up, laid out as game/src/battle_command.cpp lays them (formationLayout).

Needs Pillow. Run it again whenever the sprites or the formations change.
"""
import json
import math
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPRITES = os.path.join(ROOT, "game", "web", "sprites")
OUT = os.path.join(ROOT, "docs", "manual", "images")

TEAM = (0.27, 0.45, 0.92)       # the player's blue
OUTLINE = (24, 20, 16)
FRAMES_PER_VIEW = 26            # the old layout; new sheets have 44
# Long beasts read better turned a little (view 1, front-right); men face us.
TURNED = {"light_cavalry", "heavy_cavalry", "wolf_riders", "giants", "minotaurs", "spiders", "griffins", "pegasi",
          "elephants", "unicorns", "dragons", "catapult", "devils", "demons", "archons"}


def unit_picture(name):
    hd = os.path.join(SPRITES, name + "_hd.json")
    base = hd if os.path.exists(hd) else os.path.join(SPRITES, name + ".json")
    table = json.load(open(base))
    sheet = Image.open(base[:-5] + ".webp").convert("RGBA")
    fpv = len(table["cells"]) // 5 if len(table["cells"]) in (130, 220) else FRAMES_PER_VIEW
    view = 1 if name in TURNED else 0
    x, y, w, h = [int(v) for v in table["cells"][view * fpv][:4]]
    y = sheet.height - y - h  # the table counts rows from the bottom, as GL does
    cell = sheet.crop((x, y, x + w, y + h))
    px = cell.load()
    for j in range(h):
        for i in range(w):
            r, g, b, a = px[i, j]
            if a > 223:
                px[i, j] = (r, g, b, 255)
            elif a > 170:  # the side's cloth, drawn white-shaded
                px[i, j] = (int(r * TEAM[0]), int(g * TEAM[1]), int(b * TEAM[2]), 255)
            elif a > 100:
                px[i, j] = OUTLINE + (255,)
            else:
                px[i, j] = (0, 0, 0, 0)
    scale = 2 if table["ppm"] < 90 else 1
    if h * scale < 160:
        scale = max(scale, math.ceil(160 / h))
    cell = cell.resize((w * scale, h * scale), Image.NEAREST)
    os.makedirs(os.path.join(OUT, "units"), exist_ok=True)
    cell.save(os.path.join(OUT, "units", name + ".png"), optimize=True)
    return cell.size


# ---- Formations (a port of formationLayout, front = up) ----

def layout(kind, count, spacing=1.0):
    slots = []

    def rows(per_row, gap, depth, stagger):
        for i in range(count):
            row, in_row = i // per_row, min(per_row, count - (i // per_row) * per_row)
            col = (i % per_row) - (in_row - 1) / 2.0
            if row % 2 == 1 and (per_row - in_row) % 2 == 0:
                col += stagger
            slots.append([col * gap, -row * depth, 0.0, 1.0])

    if kind == "line":
        rows(max(1, (count + 1) // 2), spacing, spacing, 0)
    elif kind == "shortline":
        rows(max(1, (count + 2) // 3), spacing, spacing, 0)
    elif kind == "box":
        rows(max(1, math.ceil(math.sqrt(count * 1.4))), spacing, spacing, 0)
    elif kind == "column":
        rows(2, spacing, spacing, 0)
    elif kind == "staggered":
        rows(max(1, (count + 1) // 2), spacing * 2.0, spacing * 1.5, 0.5)
    elif kind == "loose":
        rows(max(1, (count + 1) // 2), spacing * 2.2, spacing * 2.2, 0)
        for i, s in enumerate(slots):
            h = ((i * 2654435761) % 1000) / 1000.0
            s[0] += (h - 0.5) * spacing * 0.6
            s[1] += (math.fmod(h * 7.0, 1.0) - 0.5) * spacing * 0.8
    elif kind == "wedge":
        row = placed = 0
        while placed < count:
            in_row = min(row + 1, count - placed)
            for k in range(in_row):
                slots.append([(k - (in_row - 1) / 2.0) * spacing, -row * spacing, 0.0, 1.0])
                placed += 1
            row += 1
    elif kind == "vee":
        for i in range(count):
            k = (i + 1) // 2
            side = 0.0 if i == 0 else (-1.0 if i % 2 == 1 else 1.0)
            slots.append([side * k * spacing * 0.8, k * spacing * 0.8, 0.0, 1.0])
    elif kind == "encircle":
        arc = 3.84
        step = arc / (count - 1)
        radius = max(spacing * 2.5, spacing / (2 * math.sin(step / 2)))
        for i in range(count):
            a = -arc / 2 + step * i
            slots.append([math.sin(a) * radius, math.cos(a) * radius, math.sin(a), math.cos(a)])
        return slots, True
    mx = sum(s[0] for s in slots) / len(slots)
    my = sum(s[1] for s in slots) / len(slots)
    for s in slots:
        s[0] -= mx
        s[1] -= my
    return slots, False


FORMATIONS = ["line", "shortline", "staggered", "box", "wedge", "column", "loose", "encircle", "vee"]


def formation_svg(kind, count=12):
    slots, centred = layout(kind, count)
    xs = [s[0] for s in slots] + ([0] if centred else [])
    ys = [s[1] for s in slots] + ([0] if centred else [])
    pad = 1.4
    x0, x1, y0, y1 = min(xs) - pad, max(xs) + pad, min(ys) - pad, max(ys) + pad
    scale = 150 / max(x1 - x0, y1 - y0, 6)
    W, H = (x1 - x0) * scale, (y1 - y0) * scale + 26
    ox = lambda x: (x - x0) * scale
    oy = lambda y: (y1 - y) * scale + 20  # front (+y) up
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.0f} {H:.0f}" width="{W:.0f}" height="{H:.0f}">',
           '<style>.m{fill:#3f6fe0;stroke:#141008;stroke-width:1.4}.f{stroke:#d6b46e;stroke-width:2.2;stroke-linecap:round}'
           '.a{fill:#b8941f;font:600 12px Georgia,serif;text-anchor:middle}</style>',
           f'<path class="f" d="M{W/2-14:.0f} 14 L{W/2:.0f} 4 L{W/2+14:.0f} 14" fill="none"/>',
           f'<text class="a" x="{W/2:.0f}" y="{H-2:.0f}" style="opacity:.0">.</text>']
    if centred:
        out.append(f'<circle cx="{ox(0):.1f}" cy="{oy(0):.1f}" r="{0.45*scale:.1f}" fill="none" stroke="#d9402f" stroke-width="2" stroke-dasharray="3 3"/>')
    r = 0.34 * scale
    for x, y, fx, fy in slots:
        cx, cy = ox(x), oy(y)
        out.append(f'<line class="f" x1="{cx:.1f}" y1="{cy:.1f}" x2="{cx + fx * r * 1.8:.1f}" y2="{cy - fy * r * 1.8:.1f}"/>')
        out.append(f'<circle class="m" cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}"/>')
    out.append("</svg>")
    os.makedirs(os.path.join(OUT, "formations"), exist_ok=True)
    with open(os.path.join(OUT, "formations", kind + ".svg"), "w") as f:
        f.write("\n".join(out) + "\n")


def main():
    names = sorted(n[:-5] for n in os.listdir(SPRITES) if n.endswith(".json") and not n.startswith("index") and not n.endswith("_hd.json"))
    for n in names:
        print(n, unit_picture(n))
    for k in FORMATIONS:
        formation_svg(k)
    print("formations:", ", ".join(FORMATIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
