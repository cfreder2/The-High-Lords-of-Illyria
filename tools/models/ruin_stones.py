"""The battle's ruin from the model the game draws: its floor, stair, base and
stones, so that what is solid in battle is what is drawn.

From the repository root (numpy; Pillow for the preview):

    python3 -I tools/models/ruin_stones.py [--bin game/web/models/ruin.bin] [--preview DIR]

tools/models/ruin.py runs this once it has written the model; run it alone
when only these rules change. It writes

    engine/sim/src/site_ruin.inc        the floor, stair, base and stones (Region::fromContinent)
    engine/sim/src/site_ruin_drawn.inc  the drawn footprint, for the tests (tests/test_site_clearance.cpp)

From ruin.bin (its footprint centred on the origin, its longer side 1, the
ground at z = 0), at the game's size (Continent::Site::kRuinSize), it

  1. rasterises the mesh on a grid of 1/256 of its size: the height of its top
     at each point, and whether a point at a given height there is inside
     stone (an odd number of the mesh's surfaces above it);
  2. finds the floor (the commonest raised height, its well-filled extent) and
     the stair (the run from the paving up to the floor's front edge);
  3. finds the base, the stepped plinth the floor stands on: solid to the
     ground and wider than the floor, out to where each side's step ends;
  4. takes as solid whatever stone stands higher than LOW above the ground
     (a sprite's feet go out of sight behind anything higher), outside the
     base and the stair, and up on the floor whatever stands higher than
     LOW_FLOOR above it, and the pit in it;
  5. covers the solid with stones: compact, round, tall pieces as columns (a
     circle round all of the piece), the rest as boxes over every solid point
     (the largest rectangles first, no side longer than MAX_BOX).

The drawn footprint is the same solid at a man's own level, as a bitmap of
FOOT_GRID x FOOT_GRID cells (a cell is set if any of the finer grid's points
in it is solid): on the ground (the base, the floor over it and the stones
off it; the stair and the paving are walked on) and up on the floor.

ruin.bin, little-endian:
    char[4] "IMDL", uint32 version (1), uint32 vertices, uint32 indices,
    float32[6] bounds (min x y z, max x y z),
    vertices x {float32 pos[3]; int8 normal[3], 0; uint16 uv[2]},
    indices x uint16 (triangles).
"""
import argparse
import math
import os
import struct
import sys
from collections import deque

import numpy as np

GRID = 256          # samples across the model
FOOT_GRID = 128     # the drawn footprint's cells across the model
STEP = 0.8          # metres: lower than this below the floor is the pit
LOW = 0.5           # metres above the ground: higher stone hides a man's feet (the paving's broken slabs are lower)
LOW_FLOOR = 0.8     # metres above the floor: up on the platform its broken paving and low rubble are walked
                    # over, so that its cramped floor keeps room for a fight of two squads
FIRST_STEP = 0.8    # metres: the stair's lowest step may reach this far past the foot of its run
BAND = (0.4, 0.7, 1.0, 1.5)  # heights above his feet where stone stops him (inside tests)
FLOOR_TOL = 0.35    # metres either side of the floor's height that count as floor
MAX_BOX = 6.0       # metres: the longest side of a block (the sim looks 7 m round a man)
COLUMN_TALL = 2.0   # metres above his feet: a column stands at least this tall
BLOB = 2.5          # metres: a lump no longer than this is the one box round it
SQUARE = 0.85       # a piece filling this much of its box or more is square, not round


def parse():
    p = argparse.ArgumentParser()
    p.add_argument('--bin', default='game/web/models/ruin.bin')
    p.add_argument('--size', type=float, default=24.0, help='metres across (Continent::Site::kRuinSize)')
    p.add_argument('--inc', default='engine/sim/src/site_ruin.inc')
    p.add_argument('--drawn', default='engine/sim/src/site_ruin_drawn.inc')
    p.add_argument('--preview', default='')
    return p.parse_args()


def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'IMDL':
        sys.exit('%s: not a model' % path)
    _, nv, ni = struct.unpack_from('<III', data, 4)
    vt = np.dtype([('p', '<f4', 3), ('n', 'i1', 4), ('uv', '<u2', 2)])
    verts = np.frombuffer(data, dtype=vt, count=nv, offset=40)
    idx = np.frombuffer(data, dtype='<u2', count=ni, offset=40 + nv * vt.itemsize)
    return verts['p'].astype(np.float64), idx.reshape(-1, 3).astype(np.int64)


def rasterise(P, T, xs):
    """The surfaces over each grid point: (top height, cell index of each hit, its height)."""
    n = len(xs)
    lo = xs[0]
    step = xs[1] - xs[0]
    top = np.full(n * n, np.nan)
    cells, zs = [], []
    for t in T:
        a, b, c = P[t]
        i0 = max(0, int(math.ceil((min(a[0], b[0], c[0]) - lo) / step)))
        i1 = min(n - 1, int(math.floor((max(a[0], b[0], c[0]) - lo) / step)))
        j0 = max(0, int(math.ceil((min(a[1], b[1], c[1]) - lo) / step)))
        j1 = min(n - 1, int(math.floor((max(a[1], b[1], c[1]) - lo) / step)))
        if i1 < i0 or j1 < j0:
            continue
        v0, v1 = b - a, c - a
        den = v0[0] * v1[1] - v1[0] * v0[1]
        if abs(den) < 1e-14:
            continue  # standing on edge: no vertical ray goes through it
        X, Y = np.meshgrid(xs[i0:i1 + 1], xs[j0:j1 + 1])
        px, py = X - a[0], Y - a[1]
        u = (px * v1[1] - v1[0] * py) / den
        v = (v0[0] * py - px * v0[1]) / den
        hit = (u >= 0) & (v >= 0) & (u + v < 1)
        if not hit.any():
            continue
        z = (a[2] + u * v0[2] + v * v1[2])[hit]
        jj, ii = np.nonzero(hit)
        k = (jj + j0) * n + (ii + i0)
        cells.append(k)
        zs.append(z)
    cells = np.concatenate(cells)
    zs = np.concatenate(zs)
    np.fmax.at(top, cells, zs)
    return top.reshape(n, n), cells, zs


def inside_at(cells, zs, n, h):
    """Whether each grid point at height h (an n x n array, or a number) is inside stone."""
    hh = np.broadcast_to(np.asarray(h, float), (n, n)).ravel()
    count = np.zeros(n * n, int)
    np.add.at(count, cells, (zs > hh[cells]).astype(int))
    return (count % 2 == 1).reshape(n, n)


def label(mask, diagonal=False):
    """Connected regions of a boolean grid: (labels, sizes)."""
    lab = np.zeros(mask.shape, int)
    sizes = [0]
    steps = [(1, 0), (-1, 0), (0, 1), (0, -1)] + ([(1, 1), (1, -1), (-1, 1), (-1, -1)] if diagonal else [])
    for j0, i0 in zip(*np.nonzero(mask)):
        if lab[j0, i0]:
            continue
        n = len(sizes)
        lab[j0, i0] = n
        q = deque([(j0, i0)])
        count = 0
        while q:
            j, i = q.popleft()
            count += 1
            for dj, di in steps:
                a, b = j + dj, i + di
                if 0 <= a < mask.shape[0] and 0 <= b < mask.shape[1] and mask[a, b] and not lab[a, b]:
                    lab[a, b] = n
                    q.append((a, b))
        sizes.append(count)
    return lab, sizes


def largest_rect(cells):
    """The largest all-true rectangle in a boolean grid: (j0, i0, j1, i1) inclusive, or None."""
    rows, cols = cells.shape
    heights = np.zeros(cols, int)
    best, out = 0, None
    for j in range(rows):
        heights = np.where(cells[j], heights + 1, 0)
        stack = []
        for i in range(cols + 1):
            h = heights[i] if i < cols else 0
            start = i
            while stack and stack[-1][1] >= h:
                s, sh = stack.pop()
                if sh * (i - s) > best:
                    best, out = sh * (i - s), (j - sh + 1, s, j, i - 1)
                start = s
            stack.append((start, h))
    return out


def derive(P, T, size):
    """The floor, stair, base and stones, in metres in model space (x, y from the middle)."""
    cell = size / GRID
    xs = (np.arange(GRID) + 0.5) / GRID - 0.5
    top, cells, zs = rasterise(P, T, xs)
    zs = zs * size
    H = np.nan_to_num(top, nan=0.0) * size  # metres above the ground (the pit's floor below 0)
    # The floor: the commonest height of the raised ground, refined.
    raised = H[H > 1.0]
    counts, edges = np.histogram(raised, bins=np.arange(1.0, raised.max() + 0.1, 0.1))
    floor = float(edges[np.argmax(counts)] + 0.05)
    floor = float(np.median(raised[abs(raised - floor) < 0.2]))
    near = abs(H - floor) <= FLOOR_TOL
    lab, sizes = label(near)
    main = lab == int(np.argmax(sizes))
    # Its extent: rows and columns well filled (not a stray step or ledge).
    rows, cols = main.sum(1), main.sum(0)
    rj = np.nonzero(rows >= 0.25 * rows.max())[0]
    ci = np.nonzero(cols >= 0.25 * cols.max())[0]
    j0, j1, i0, i1 = rj.min(), rj.max(), ci.min(), ci.max()
    fx0, fx1, fy0, fy1 = xs[i0] * size - cell / 2, xs[i1] * size + cell / 2, xs[j0] * size - cell / 2, xs[j1] * size + cell / 2
    # The stair: in front (-y) of the floor, the columns where the ground
    # climbs from the paving to the floor over a metre or more.
    runs = []
    for i in range(i0, i1 + 1):
        j = j0 - 1
        while j >= 0 and 0.3 < H[j, i] < floor - 0.15:
            j -= 1
        runs.append((j0 - 1 - j) * cell)
    at, k = None, 0
    while k < len(runs):
        e = k
        while e < len(runs) and runs[e] >= 1.0:
            e += 1
        if e > k and (at is None or e - k > at[1] - at[0]):
            at = (k, e)
        k = e + 1
    if at is None:
        sys.exit('no stair found in front of the floor')
    s0, s1 = i0 + at[0], i0 + at[1] - 1
    run = float(np.median(runs[at[0]:at[1]]))
    sx0, sx1 = xs[s0] * size - cell / 2, xs[s1] * size + cell / 2
    stair = {'x0': sx0, 'x1': sx1, 'foot': fy0 - run, 'head': fy0}
    X, Y = np.meshgrid(xs * size, xs * size)
    on_floor = np.zeros(H.shape, bool)
    on_floor[j0:j1 + 1, i0:i1 + 1] = True
    # (and its lowest step, where it reaches out past the run's foot)
    on_stair = (X >= sx0) & (X <= sx1) & (Y >= stair['foot'] - FIRST_STEP) & (Y <= fy0)
    # The base: out from each side of the floor while the stone stands above
    # LOW (the stepped plinth); each side's edge where most of its rows end.
    def reach(lines):
        out = []
        for line in lines:
            n = 0
            while n < len(line) and line[n] > LOW:
                n += 1
            out.append(n)
        return float(np.median(out)) * cell
    left = reach([H[j, i0 - 1::-1] for j in range(j0, j1 + 1)])
    right = reach([H[j, i1 + 1:] for j in range(j0, j1 + 1)])
    back = reach([H[j1 + 1:, i] for i in range(i0, i1 + 1)])
    front = reach([H[j0 - 1::-1, i] for i in range(i0, i1 + 1) if not s0 <= i <= s1])
    base = (fx0 - left, fy0 - front, fx1 + right, fy1 + back)
    in_base = (X >= base[0]) & (X <= base[2]) & (Y >= base[1]) & (Y <= base[3])
    # What stands above a man's feet: about the floor where he stands on it, else about the ground.
    feet = np.where(on_floor, floor, 0.0)
    within = np.zeros(H.shape, bool)
    for b in BAND:
        within |= inside_at(cells, zs, GRID, feet + b)
    tall = (H > feet + np.where(on_floor, LOW_FLOOR, LOW)) & within
    pit = on_floor & (H < floor - STEP)
    solid = np.where(on_floor, tall | pit, tall & ~in_base & ~on_stair)
    # The drawn footprint at a man's level: on the ground, the base (and the
    # floor over it) is solid too, but not the stair he climbs.
    ground = ((tall & ~on_floor) | in_base | on_floor) & ~on_stair
    up = on_floor & (tall | pit)
    # Covered on cells twice the size (each solid point within one): fewer, plainer stones.
    stones = []
    half = GRID // 2
    cover = solid.reshape(half, 2, half, 2).any(axis=(1, 3))
    lifted = on_floor.reshape(half, 2, half, 2).any(axis=(1, 3))
    tops = H.reshape(half, 2, half, 2).max(axis=(1, 3))
    cc = cell * 2
    mids = (np.arange(half) + 0.5) * cc - size / 2  # the coarse cells' middles
    lab, sizes = label(cover, diagonal=True)
    for n in range(1, len(sizes)):
        comp = lab == n
        area = sizes[n] * cc * cc
        jj, ii = np.nonzero(comp)
        w, h = (ii.max() - ii.min() + 1) * cc, (jj.max() - jj.min() + 1) * cc
        t = tops[comp]
        base_z = floor if lifted[jj[0], ii[0]] else 0.0
        # Round, compact and standing tall: a column, a circle round all of it
        # (one on a square base fills its box: the box, below).
        if max(w, h) <= 3.0 and min(w, h) / max(w, h) >= 0.6 and 0.55 <= area / (w * h) < SQUARE and t.max() > base_z + COLUMN_TALL:
            mx, my = float(mids[ii].mean()), float(mids[jj].mean())
            r = float(np.sqrt((mids[ii] - mx) ** 2 + (mids[jj] - my) ** 2).max()) + cc * 0.71
            stones.append({'x': mx, 'y': my, 'r': r, 'top': float(t.max()), 'round': True})
            continue
        rects = []
        if max(w, h) <= BLOB and area / (w * h) >= 0.5 and not base_z:
            # A small lump on the ground (rubble, a stump of wall): the box
            # round it (up on the cramped floor, only what it covers).
            rects.append((jj.min(), ii.min(), jj.max(), ii.max()))
        else:
            # Otherwise blocks: rectangles over every point of it, the largest first.
            left_over = comp.copy()
            while left_over.any():
                a0, b0, a1, b1 = largest_rect(left_over)
                left_over[a0:a1 + 1, b0:b1 + 1] = False
                rects.append((a0, b0, a1, b1))
        for a0, b0, a1, b1 in rects:
            # No side longer than MAX_BOX.
            ny = int(math.ceil((a1 - a0 + 1) * cc / MAX_BOX))
            nx = int(math.ceil((b1 - b0 + 1) * cc / MAX_BOX))
            for p in range(ny):
                for q in range(nx):
                    c0 = a0 + (a1 - a0 + 1) * p // ny
                    c1 = a0 + (a1 - a0 + 1) * (p + 1) // ny - 1
                    d0 = b0 + (b1 - b0 + 1) * q // nx
                    d1 = b0 + (b1 - b0 + 1) * (q + 1) // nx - 1
                    tt = float(tops[c0:c1 + 1, d0:d1 + 1].max())
                    stones.append({'x': float((mids[d0] + mids[d1]) / 2), 'y': float((mids[c0] + mids[c1]) / 2),
                                   'hx': (d1 - d0 + 1) * cc / 2, 'hy': (c1 - c0 + 1) * cc / 2,
                                   'top': max(tt, base_z + STEP), 'round': False})
    return {'floor': floor, 'rect': (fx0, fy0, fx1, fy1), 'stair': stair, 'base': base, 'stones': stones, 'H': H,
            'solid': solid, 'ground': ground, 'up': up, 'cell': cell, 'xs': xs}


def write_inc(d, size, path, tris):
    k = lambda v: int(round(v / size * 1000))
    fx0, fy0, fx1, fy1 = d['rect']
    st = d['stair']
    mid = (st['x0'] + st['x1']) / 2
    bx0, by0, bx1, by1 = d['base']
    lines = [
        '// Generated by tools/models/ruin_stones.py from the ruin model (game/web/models/ruin.bin,',
        '// %d triangles): do not edit. The battle\'s ruin (Region::fromContinent): in' % tris,
        '// thousandths of its size, unturned (the stair faces -y), heights above the ground.',
        '',
        '// The size the stones were derived at (metres; Continent::Site::kRuinSize).',
        'constexpr int kRuinModelSize = %d;' % round(size),
        '// The platform\'s floor: x0, y0, x1, y1, its height.',
        'constexpr int64_t kRuinFloor[5] = {%d, %d, %d, %d, %d};' % (k(fx0), k(fy0), k(fx1), k(fy1), k(d['floor'])),
        '// The stair up to it: its foot on the ground and its head at the floor (x, y), half its width.',
        'constexpr int64_t kRuinStair[5] = {%d, %d, %d, %d, %d};' % (k(mid), k(st['foot']), k(mid), k(st['head']),
                                                                    k((st['x1'] - st['x0']) / 2)),
        '// The stepped base the floor stands on, solid to the ground: x0, y0, x1, y1.',
        'constexpr int64_t kRuinBase[4] = {%d, %d, %d, %d};' % (k(bx0), k(by0), k(bx1), k(by1)),
        '// Its stones: x, y, half sizes (a column\'s radius twice), top; round for a column.',
        'const std::vector<SiteStone> kRuinStones = {',
    ]
    for s in d['stones']:
        if s['round']:
            lines.append('    {%d, %d, %d, %d, %d, true},' % (k(s['x']), k(s['y']), k(s['r']), k(s['r']), k(s['top'])))
        else:
            # Half sizes rounded up, so a box still covers what it was cut from.
            hx, hy = int(math.ceil(s['hx'] / size * 1000 - 1e-6)), int(math.ceil(s['hy'] / size * 1000 - 1e-6))
            lines.append('    {%d, %d, %d, %d, %d, false},' % (k(s['x']), k(s['y']), hx, hy, k(s['top'])))
    lines.append('};')
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    cols = sum(1 for s in d['stones'] if s['round'])
    print('%s: floor %.2f m high, %.1f x %.1f m; base %.1f x %.1f m; stair %.1f m wide, %.1f m run; %d columns, %d blocks' % (
        path, d['floor'], fx1 - fx0, fy1 - fy0, bx1 - bx0, by1 - by0, st['x1'] - st['x0'], st['head'] - st['foot'], cols,
        len(d['stones']) - cols))


def footprint(mask):
    """A GRID x GRID mask as FOOT_GRID x FOOT_GRID cells, each set if any point in it is."""
    f = GRID // FOOT_GRID
    return mask.reshape(FOOT_GRID, f, FOOT_GRID, f).any(axis=(1, 3))


def write_drawn(d, size, path):
    def rows(mask):
        out = []
        for j in range(FOOT_GRID):
            bits = ''.join('1' if v else '0' for v in mask[j])
            out.append('    "%s",' % ''.join('%x' % int(bits[i:i + 4], 2) for i in range(0, FOOT_GRID, 4)))
        return out
    lines = [
        '// Generated by tools/models/ruin_stones.py from the ruin model (game/web/models/ruin.bin):',
        '// do not edit. Where the drawn ruin stands higher than a man\'s feet, unturned (the stair',
        '// faces -y), on a grid of %d x %d cells across its size: row j is y from' % (FOOT_GRID, FOOT_GRID),
        '// (j / %d - 1/2) of its size, each row %d hex digits, the first digit\'s high bit at the' % (FOOT_GRID, FOOT_GRID // 4),
        '// lowest x. kRuinDrawnGround is for a man on the ground (the base and the floor over it are',
        '// solid, the stair is not), kRuinDrawnFloor for one up on the floor.',
        '',
        'constexpr int kRuinDrawnCells = %d;' % FOOT_GRID,
        'constexpr const char* kRuinDrawnGround[%d] = {' % FOOT_GRID,
    ] + rows(footprint(d['ground'])) + [
        '};',
        'constexpr const char* kRuinDrawnFloor[%d] = {' % FOOT_GRID,
    ] + rows(footprint(d['up'])) + ['};']
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('%s: %d ground cells, %d floor cells' % (path, footprint(d['ground']).sum(), footprint(d['up']).sum()))


def preview_map(d, size, path):
    from PIL import Image, ImageDraw
    H = d['H']
    scale = 4
    img = np.clip((H + 2) / (H.max() + 2), 0, 1)
    rgb = np.stack([img * 200 + 30] * 3, -1).astype(np.uint8)
    rgb[d['solid']] = (rgb[d['solid']] * 0.5 + np.array([120, 30, 30])).astype(np.uint8)
    im = Image.fromarray(rgb[::-1]).resize((GRID * scale, GRID * scale), Image.NEAREST)
    g = ImageDraw.Draw(im)
    px = lambda x, y: ((x / size + 0.5) * GRID * scale, (0.5 - y / size) * GRID * scale)
    fx0, fy0, fx1, fy1 = d['rect']
    g.rectangle([px(fx0, fy1), px(fx1, fy0)], outline=(80, 200, 255), width=2)
    bx0, by0, bx1, by1 = d['base']
    g.rectangle([px(bx0, by1), px(bx1, by0)], outline=(255, 255, 0), width=2)
    st = d['stair']
    g.rectangle([px(st['x0'], st['head']), px(st['x1'], st['foot'])], outline=(255, 160, 40), width=2)
    for s in d['stones']:
        if s['round']:
            a, b = px(s['x'] - s['r'], s['y'] + s['r']), px(s['x'] + s['r'], s['y'] - s['r'])
            g.ellipse([a, b], outline=(60, 255, 90), width=2)
        else:
            g.rectangle([px(s['x'] - s['hx'], s['y'] + s['hy']), px(s['x'] + s['hx'], s['y'] - s['hy'])], outline=(255, 90, 200), width=2)
    im.save(path)
    print('preview', path)


def main():
    a = parse()
    P, T = load(a.bin)
    d = derive(P, T, a.size)
    write_inc(d, a.size, a.inc, len(T))
    write_drawn(d, a.size, a.drawn)
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
        preview_map(d, a.size, os.path.join(a.preview, 'ruin_stones.png'))


if __name__ == '__main__':
    main()
