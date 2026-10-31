"""The ruin set piece: a textured 3D model (a Tripo FBX) made into what the
game loads, and the battle's stones derived from that same mesh.

From the repository root, with Blender as a Python module (tools/blender/README.md):

    <bpy-python> -I tools/models/ruin.py -- <ruin.fbx> [--preview DIR]

It writes
    game/web/models/ruin.bin        the decimated mesh (format below)
    game/web/models/ruin.webp       its colour texture
    engine/sim/src/site_ruin.inc    the battle's floor, stair and stones (Region::fromContinent)

The model: a temple's platform with a stair at the front (-y), a sunken stair
pit in the middle, columns, broken walls and paving out front. Its ground
level is the paving's underside (the pit goes below it). The script

  1. decimates the mesh to about --tris triangles (edge collapse, which keeps
     the UVs), turns its normals smooth with sharp edges past 35 degrees, and
     pulls the bottom of its walls down into a skirt below the ground, so
     nothing floats where the land is not quite level;
  2. centres the footprint on the origin, sets the ground at z = 0 and scales
     the longer side of the footprint to 1 (the game scales it by
     Continent::Site::kRuinSize and turns it by quarter turns);
  3. samples the decimated mesh on a grid of 1/256 of its size: the height of
     its top, and whether a man's chest (1 m above where he stands) would be
     inside stone;
  4. finds the floor (the commonest raised height, its extent), the stair (the
     run from the paving up to the floor's front edge), and as solid stones
     whatever blocks a man on the floor or on the ground about it, and the pit:
     compact, round pieces as columns, the rest as boxes no more than 6 m long.

ruin.bin, little-endian:
    char[4] "IMDL", uint32 version (1), uint32 vertices, uint32 indices,
    float32[6] bounds (min x y z, max x y z),
    vertices x {float32 pos[3]; int8 normal[3], 0; uint16 uv[2]} (uv unorm, v down the image),
    indices x uint16 (triangles).
"""
import argparse
import math
import os
import struct
import sys
from collections import deque

import bpy  # first: it makes bmesh and mathutils importable
import bmesh
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

GRID = 256          # height-field samples across the model
STEP = 0.8          # metres a man steps over without noticing
CHEST = 1.0         # metres above his feet at which stone stops him
FLOOR_TOL = 0.35    # metres either side of the floor's height that count as floor
EDGE = 0.6          # metres round the floor's edge left to the platform (its sloping base)
MAX_BOX = 6.0       # metres: the longest side of a block (the sim looks 7 m round a man)


def parse():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument('fbx')
    p.add_argument('--size', type=float, default=24.0, help='metres across (Continent::Site::kRuinSize)')
    p.add_argument('--tris', type=int, default=14000)
    p.add_argument('--texture', type=int, default=2048)
    p.add_argument('--quality', type=int, default=82)
    p.add_argument('--out', default='game/web/models')
    p.add_argument('--inc', default='engine/sim/src/site_ruin.inc')
    p.add_argument('--preview', default='')
    return p.parse_args(argv)


def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if len(meshes) != 1:
        sys.exit('expected one mesh, found %d' % len(meshes))
    o = meshes[0]
    o.data.transform(o.matrix_world)
    o.parent = None
    o.matrix_world.identity()
    return o


def decimate(o, tris):
    me = o.data
    before = sum(len(p.vertices) - 2 for p in me.polygons)
    mod = o.modifiers.new('decimate', 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = min(1.0, tris / before)
    mod.use_collapse_triangulate = True
    deps = bpy.context.evaluated_depsgraph_get()
    out = bpy.data.meshes.new_from_object(o.evaluated_get(deps))
    o.modifiers.remove(mod)
    bm = bmesh.new()
    bm.from_mesh(out)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    print('decimated %d -> %d triangles' % (before, len(bm.faces)))
    return bm


def measure(me):
    """The footprint, the ground level (the paving's underside) and the pit."""
    P = np.array([v.co[:] for v in me.vertices])
    lo, hi = P.min(0), P.max(0)
    front = P[:, 1] < lo[1] + 0.1 * (hi[1] - lo[1])
    ground = float(P[front, 2].min())
    below = P[P[:, 2] < ground - 0.01]
    pit = (below[:, :2].min(0) - 0.01, below[:, :2].max(0) + 0.01) if len(below) else None
    return lo, hi, ground, pit


def skirt(bm, ground, pit, reach, depth):
    """Pull the bottom of the walls (vertices near the ground with nothing
    facing up beside them) down below the ground; not the pit's."""
    bm.normal_update()
    moved = 0
    for v in bm.verts:
        if v.co.z > ground + reach:
            continue
        if pit and pit[0][0] < v.co.x < pit[1][0] and pit[0][1] < v.co.y < pit[1][1]:
            continue
        if any(f.normal.z > 0.6 for f in v.link_faces):
            continue
        v.co.z = ground - depth
        moved += 1
    print('skirt: %d vertices pulled down' % moved)


def normalise(bm, lo, hi, ground):
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    k = 1.0 / max(hi[0] - lo[0], hi[1] - lo[1])
    for v in bm.verts:
        v.co = Vector(((v.co.x - cx) * k, (v.co.y - cy) * k, (v.co.z - ground) * k))
    return k


def export(bm, path):
    me = bpy.data.meshes.new('ruin')
    bm.to_mesh(me)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(35))
    uv = me.uv_layers.active.data
    normals = me.corner_normals
    index = {}
    verts = []
    tris = []
    for p in me.polygons:
        if len(p.loop_indices) != 3:
            sys.exit('not triangulated')
        for li in p.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            n = normals[li].vector
            u, v = uv[li].uv
            key = (me.loops[li].vertex_index,
                   tuple(max(-127, min(127, round(c * 127))) for c in n),
                   max(0, min(65535, round(u * 65535))), max(0, min(65535, round((1 - v) * 65535))))
            k = index.get(key)
            if k is None:
                k = index[key] = len(verts)
                verts.append((co.x, co.y, co.z) + key[1] + key[2:])
            tris.append(k)
    if len(verts) > 65535:
        sys.exit('too many vertices for 16-bit indices: %d' % len(verts))
    P = np.array([v[:3] for v in verts])
    lo, hi = P.min(0), P.max(0)
    with open(path, 'wb') as f:
        f.write(b'IMDL' + struct.pack('<III', 1, len(verts), len(tris)))
        f.write(struct.pack('<6f', *lo, *hi))
        for v in verts:
            f.write(struct.pack('<3f3bb2H', v[0], v[1], v[2], v[3], v[4], v[5], 0, v[6], v[7]))
        f.write(struct.pack('<%dH' % len(tris), *tris))
    print('%s: %d vertices, %d triangles, %d bytes; bounds %s %s' % (path, len(verts), len(tris) // 3, os.path.getsize(path),
                                                                     np.round(lo, 3), np.round(hi, 3)))
    return me


def texture(fbx, size, quality, path):
    from PIL import Image
    src = os.path.join(os.path.splitext(fbx)[0] + '.fbm', 'Color.jpg')
    im = Image.open(src).convert('RGB')
    im = im.resize((size, size), Image.LANCZOS)
    im.save(path, 'WEBP', quality=quality, method=6)
    print('%s: %dx%d from %s, %d bytes' % (path, size, size, os.path.basename(src), os.path.getsize(path)))


def sample(bm):
    """Top height and chest-high solidity on a GRID x GRID raster of [-0.5, 0.5]^2."""
    bvh = BVHTree.FromBMesh(bm)
    xs = (np.arange(GRID) + 0.5) / GRID - 0.5
    top = np.full((GRID, GRID), np.nan)
    for j, y in enumerate(xs):
        for i, x in enumerate(xs):
            hit = bvh.ray_cast(Vector((x, y, 5.0)), Vector((0, 0, -1)))
            if hit[0] is not None:
                top[j, i] = hit[0].z
    return bvh, xs, top


def inside(bvh, x, y, z):
    """Whether the point is inside stone (the nearest surface faces away from it)."""
    hit = bvh.find_nearest(Vector((x, y, z)))
    if hit[0] is None:
        return False
    d = Vector((x, y, z)) - hit[0]
    return d.length < 1e-4 or d.dot(hit[1]) < 0


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


def derive(bm, size):
    """The floor, the stair and the solid stones, in metres in model space (x, y from the middle)."""
    cell = size / GRID
    bvh, xs, top = sample(bm)
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
    # The widest stretch of such columns.
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
    # What stops a man: on the floor, stone at his chest or the pit; about it, stone at his chest.
    solid = np.zeros_like(main)
    on_floor = np.zeros_like(main)
    on_floor[j0:j1 + 1, i0:i1 + 1] = True
    margin = int(math.ceil(EDGE / cell))
    edge = np.zeros_like(main)
    edge[max(0, j0 - margin):j1 + margin + 1, max(0, i0 - margin):i1 + margin + 1] = True
    edge &= ~on_floor
    for j in range(GRID):
        y = xs[j]
        for i in range(GRID):
            x = xs[i]
            if on_floor[j, i]:
                solid[j, i] = H[j, i] < floor - STEP or (H[j, i] > floor + STEP and inside(bvh, x, y, (floor + CHEST) / size))
            elif edge[j, i] or (sx0 <= x * size <= sx1 and stair['foot'] <= y * size <= fy0):
                continue
            else:
                solid[j, i] = H[j, i] > STEP and inside(bvh, x, y, CHEST / size)
    stones = []
    lab, sizes = label(solid, diagonal=True)
    for n in range(1, len(sizes)):
        comp = lab == n
        area = sizes[n] * cell * cell
        if area < 0.3:
            continue  # a stray stone a man steps round
        jj, ii = np.nonzero(comp)
        w, h = (ii.max() - ii.min() + 1) * cell, (jj.max() - jj.min() + 1) * cell
        tops = H[comp]
        # Round, compact and standing tall: a column.
        if max(w, h) <= 3.0 and min(w, h) / max(w, h) >= 0.6 and area / (w * h) >= 0.55 and tops.max() > floor + 2.0:
            stones.append({'x': float(xs[ii].mean() * size), 'y': float(xs[jj].mean() * size), 'r': float(math.sqrt(area / math.pi)),
                           'top': float(tops.max()), 'round': True})
            continue
        # Otherwise blocks: the largest rectangles of it, no side longer than MAX_BOX.
        left = comp.copy()
        while left.sum() * cell * cell >= 0.3:
            r = largest_rect(left)
            if r is None:
                break
            a0, b0, a1, b1 = r
            if (a1 - a0 + 1) * (b1 - b0 + 1) * cell * cell < 0.25:
                break
            left[a0:a1 + 1, b0:b1 + 1] = False
            ny = int(math.ceil((a1 - a0 + 1) * cell / MAX_BOX))
            nx = int(math.ceil((b1 - b0 + 1) * cell / MAX_BOX))
            for p in range(ny):
                for q in range(nx):
                    c0 = a0 + (a1 - a0 + 1) * p // ny
                    c1 = a0 + (a1 - a0 + 1) * (p + 1) // ny - 1
                    d0 = b0 + (b1 - b0 + 1) * q // nx
                    d1 = b0 + (b1 - b0 + 1) * (q + 1) // nx - 1
                    t = float(H[c0:c1 + 1, d0:d1 + 1].max())
                    stones.append({'x': float((xs[d0] + xs[d1]) / 2 * size), 'y': float((xs[c0] + xs[c1]) / 2 * size),
                                   'hx': (d1 - d0 + 1) * cell / 2, 'hy': (c1 - c0 + 1) * cell / 2,
                                   'top': max(t, floor + STEP) if on_floor[c0, d0] else max(t, STEP), 'round': False})
    return {'floor': floor, 'rect': (fx0, fy0, fx1, fy1), 'stair': stair, 'stones': stones, 'H': H, 'solid': solid,
            'cell': cell, 'xs': xs}


def write_inc(d, size, path, tris):
    k = lambda v: int(round(v / size * 1000))
    fx0, fy0, fx1, fy1 = d['rect']
    st = d['stair']
    mid = (st['x0'] + st['x1']) / 2
    lines = [
        '// Generated by tools/models/ruin.py from the ruin model (game/web/models/ruin.bin,',
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
        '// Its stones: x, y, half sizes (a column\'s radius twice), top; round for a column.',
        'const std::vector<SiteStone> kRuinStones = {',
    ]
    for s in d['stones']:
        if s['round']:
            lines.append('    {%d, %d, %d, %d, %d, true},' % (k(s['x']), k(s['y']), k(s['r']), k(s['r']), k(s['top'])))
        else:
            lines.append('    {%d, %d, %d, %d, %d, false},' % (k(s['x']), k(s['y']), k(s['hx']), k(s['hy']), k(s['top'])))
    lines.append('};')
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    cols = sum(1 for s in d['stones'] if s['round'])
    print('%s: floor %.2f m high, %.1f x %.1f m; stair %.1f m wide, %.1f m run; %d columns, %d blocks' % (
        path, d['floor'], fx1 - fx0, fy1 - fy0, st['x1'] - st['x0'], st['head'] - st['foot'], cols, len(d['stones']) - cols))


def preview_map(d, size, path):
    from PIL import Image, ImageDraw
    H, cell = d['H'], d['cell']
    scale = 4
    img = np.clip((H + 2) / (H.max() + 2), 0, 1)
    rgb = np.stack([img * 200 + 30] * 3, -1).astype(np.uint8)
    rgb[d['solid']] = (rgb[d['solid']] * 0.5 + np.array([120, 30, 30])).astype(np.uint8)
    im = Image.fromarray(rgb[::-1]).resize((GRID * scale, GRID * scale), Image.NEAREST)
    g = ImageDraw.Draw(im)
    px = lambda x, y: ((x / size + 0.5) * GRID * scale, (0.5 - y / size) * GRID * scale)
    fx0, fy0, fx1, fy1 = d['rect']
    g.rectangle([px(fx0, fy1), px(fx1, fy0)], outline=(80, 200, 255), width=2)
    st = d['stair']
    g.rectangle([px(st['x0'], st['head']), px(st['x1'], st['foot'])], outline=(255, 220, 60), width=2)
    for s in d['stones']:
        if s['round']:
            a, b = px(s['x'] - s['r'], s['y'] + s['r']), px(s['x'] + s['r'], s['y'] - s['r'])
            g.ellipse([a, b], outline=(60, 255, 90), width=2)
        else:
            g.rectangle([px(s['x'] - s['hx'], s['y'] + s['hy']), px(s['x'] + s['hx'], s['y'] - s['hy'])], outline=(255, 90, 200), width=2)
    im.save(path)
    print('preview', path)


def preview_render(me, tex, path):
    """The decimated model, textured, from above and at an angle (EEVEE)."""
    o = bpy.data.objects.new('ruin', me)
    s = bpy.context.scene
    s.collection.objects.link(o)
    mat = bpy.data.materials.new('ruin')
    mat.use_nodes = True
    nt = mat.node_tree
    img = nt.nodes.new('ShaderNodeTexImage')
    img.image = bpy.data.images.load(tex)
    nt.links.new(img.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
    me.materials.append(mat)
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    s.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    s.render.resolution_x = s.render.resolution_y = 640
    world = bpy.data.worlds.new('w')
    s.world = world
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1.0
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    s.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(50), 0, math.radians(30))
    sun.data.energy = 3
    ground = bpy.data.meshes.new('ground')
    bmg = bmesh.new()
    bmesh.ops.create_grid(bmg, x_segments=1, y_segments=1, size=1.5)
    bmg.to_mesh(ground)
    s.collection.objects.link(bpy.data.objects.new('ground', ground))
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    s.collection.objects.link(cam)
    s.camera = cam
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 1.2
    for name, loc in (('top', (0, 0, 3)), ('iso', (2, -2, 1.4)), ('low', (1.2, -2.6, 0.25))):
        cam.location = Vector(loc)
        cam.rotation_euler = (Vector((0, 0, 0.12)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
        s.render.filepath = '%s_%s.png' % (path, name)
        bpy.ops.render.render(write_still=True)
    print('preview', path + '_*.png')


def main():
    a = parse()
    o = load(a.fbx)
    lo, hi, ground, pit = measure(o.data)
    print('footprint %.3f x %.3f, ground at %.4f, top %.3f; pit %s' % (hi[0] - lo[0], hi[1] - lo[1], ground, hi[2],
                                                                     None if pit is None else np.round(pit, 3).tolist()))
    bm = decimate(o, a.tris)
    # A skirt about 1.5 m deep at the game's size, from walls whose foot stands within 0.6 m of the ground.
    unit = max(hi[0] - lo[0], hi[1] - lo[1]) / a.size
    skirt(bm, ground, pit, 0.6 * unit, 1.5 * unit)
    normalise(bm, lo, hi, ground)
    os.makedirs(a.out, exist_ok=True)
    tris = len(bm.faces)
    me = export(bm, os.path.join(a.out, 'ruin.bin'))
    tex = os.path.join(a.out, 'ruin.webp')
    texture(a.fbx, a.texture, a.quality, tex)
    d = derive(bm, a.size)
    write_inc(d, a.size, a.inc, tris)
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
        preview_map(d, a.size, os.path.join(a.preview, 'ruin_stones.png'))
        preview_render(me, os.path.abspath(tex), os.path.join(a.preview, 'ruin'))


main()
