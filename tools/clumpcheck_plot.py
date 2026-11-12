#!/usr/bin/env python3
"""Top-down pictures of the melee, from `illyria_clumpcheck dump SCENARIO SEED 20,40,60`:
   illyria_clumpcheck dump inf24 1 10,12,15 | python3 tools/clumpcheck_plot.py OUTDIR/inf24
Bodies are drawn to scale (a circle of the man's radius, 40 px a metre); blue and red are the two sides, a bright rim marks
a man in reach of a foe, a grey fill a man who has broken and run, a tick the way he faces."""
import sys
from collections import defaultdict
from PIL import Image, ImageDraw

PX = 40
frames = defaultdict(list)
for line in sys.stdin:
    if line.startswith("U "):
        f = line.split()
        frames[int(f[1])].append((int(f[2]), *map(float, f[3:6]), int(f[6]), int(f[7]), float(f[8]), float(f[9])))
for sec, men in sorted(frames.items()):
    fight = [m for m in men if not m[5]] or men
    cx = sum(m[1] for m in fight) / len(fight)
    cy = sum(m[2] for m in fight) / len(fight)
    W, H = 1400, 700
    img = Image.new("RGB", (W, H), (88, 112, 70))
    d = ImageDraw.Draw(img)
    for gx in range(-40, 41, 5):  # a 5 m grid
        x = W / 2 + gx * PX
        d.line([(x - (cx % 5) * PX, 0), (x - (cx % 5) * PX, H)], fill=(80, 104, 63))
    for gy in range(-20, 21, 5):
        y = H / 2 + gy * PX
        d.line([(0, y - (cy % 5) * PX), (W, y - (cy % 5) * PX)], fill=(80, 104, 63))
    for side, x, y, r, eng, rout, fx, fy in men:
        px, py = W / 2 + (x - cx) * PX, H / 2 + (y - cy) * PX
        col = (60, 90, 230) if side == 0 else (220, 60, 50)
        if rout:
            col = (150, 150, 150)
        rr = r * PX
        d.ellipse([px - rr, py - rr, px + rr, py + rr], fill=col, outline=(255, 235, 90) if eng else (20, 20, 20), width=3 if eng else 1)
        d.line([(px, py), (px + fx * rr * 1.4, py + fy * rr * 1.4)], fill=(255, 255, 255), width=2)
    d.text((10, 10), f"t = {sec} s   blue {sum(1 for m in men if m[0] == 0)}  red {sum(1 for m in men if m[0] == 1)}   (5 m grid, bodies to scale)", fill=(255, 255, 255))
    img.save(f"{sys.argv[1]}-{sec:02d}s.png")
