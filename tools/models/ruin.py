"""The ruin set piece: a textured 3D model (a Tripo FBX) made into what the
game loads, and the battle's stones derived from that same mesh.

From the repository root, with Blender as a Python module (tools/blender/README.md):

    <bpy-python> -I tools/models/ruin.py -- <ruin.fbx> [--preview DIR]

It writes
    game/web/models/ruin.bin        the decimated mesh (format below)
    game/web/models/ruin.webp       its colour texture
    engine/sim/src/site_ruin.inc    the battle's floor, stair, base and stones (Region::fromContinent),
    engine/sim/src/site_ruin_drawn.inc  and its drawn footprint, for the tests: both by
                                    tools/models/ruin_stones.py from ruin.bin (numpy only; run
                                    it alone to derive them again without Blender)

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
  3. writes it, and derives from what it wrote the floor (the commonest
     raised height, its extent), the stair (the run from the paving up to the
     floor's front edge), the stepped base the floor stands on, and as solid
     stones whatever stands higher than a man's feet where he would stand
     (ruin_stones.py): what is solid in battle is what the game draws.

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
import subprocess
import sys

import bpy  # first: it makes bmesh and mathutils importable
import bmesh
import numpy as np
from mathutils import Vector


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
    # The battle's ruin, from the mesh just written (the Python running this has numpy).
    stones = [sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ruin_stones.py'),
              '--bin', os.path.join(a.out, 'ruin.bin'), '--size', str(a.size), '--inc', a.inc]
    if a.preview:
        stones += ['--preview', a.preview]
    subprocess.run(stones, check=True)
    print('%d triangles' % tris)
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
        preview_render(me, os.path.abspath(tex), os.path.join(a.preview, 'ruin'))


main()
