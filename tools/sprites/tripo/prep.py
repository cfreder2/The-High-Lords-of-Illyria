"""Import each delivered Tripo FBX once: report it, decimate to about TRIS triangles, turn it to face +x (Tripo faces
-y), save MODELS/<name>.blend (textures stay where they were delivered) and render front / side / back previews.

    python -I prep.py SRC_ALL OUT_DIR [name-part,name-part] [--tris 40000]"""
import glob
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'blender')))
import bpy
import numpy as np
from mathutils import Matrix, Vector

from sprites import render as R

SRC, OUT = sys.argv[1], sys.argv[2]
PATS = sys.argv[3].split(',') if len(sys.argv) > 3 and not sys.argv[3].startswith('--') else None
TRIS = int(sys.argv[sys.argv.index('--tris') + 1]) if '--tris' in sys.argv else 40000
os.makedirs(os.path.join(OUT, 'models'), exist_ok=True)
os.makedirs(os.path.join(OUT, 'previews'), exist_ok=True)


def world_pts(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(n, 3)
    mw = np.array(ev.matrix_world)
    ev.to_mesh_clear()
    return co @ mw[:3, :3].T + mw[:3, 3]


for d in sorted(os.listdir(SRC)):
    if PATS and not any(p in d for p in PATS):
        continue
    fb = glob.glob(os.path.join(SRC, d, '*.fbx'))
    if not fb:
        continue
    name = d.replace('_3d_model', '')
    dst = os.path.join(OUT, 'models', name + '.blend')
    if os.path.exists(dst) and '--force' not in sys.argv:
        print('have', name, flush=True)
        continue
    t0 = time.time()
    scene = R.reset()
    # No image search: the importer walks the disk for textures (minutes). They are beside the FBX, in its .fbm folder.
    bpy.ops.import_scene.fbx(filepath=fb[0], use_image_search=False)
    fbm = glob.glob(os.path.join(SRC, d, '*.fbm'))
    for im in bpy.data.images:
        if im.source == 'FILE' and fbm:
            cand = os.path.join(fbm[0], os.path.basename(im.filepath.replace('\\', '/')))
            if os.path.exists(cand):
                if im.packed_file:
                    im.unpack(method='REMOVE')
                im.filepath = cand
                im.reload()
    info = {'name': name, 'src': fb[0]}
    arm = next((o for o in bpy.data.objects if o.type == 'ARMATURE'), None)
    body = next(o for o in bpy.data.objects if o.type == 'MESH')
    info['tris_delivered'] = sum(len(p.vertices) - 2 for p in body.data.polygons)
    info['actions'] = [a.name for a in bpy.data.actions]
    if arm:
        info['bones'] = [b.name for b in arm.data.bones]
        groups = {g.index: g.name for g in body.vertex_groups}
        used, unweighted, maxinf = set(), 0, 0
        for v in body.data.vertices:
            w = [g for g in v.groups if g.weight > 1e-4]
            if not w:
                unweighted += 1
            maxinf = max(maxinf, len(w))
            used.update(groups[g.group] for g in w)
        info['weighted_bones'] = sorted(used)
        info['unweighted_verts'] = unweighted
        info['max_influences'] = maxinf
    # Decimate, keeping weights and UVs.
    if info['tris_delivered'] > TRIS * 1.1:
        md = body.modifiers.new('dec', 'DECIMATE')
        md.ratio = TRIS / info['tris_delivered']
        bpy.context.view_layer.objects.active = body
        while body.modifiers[0].name != 'dec':
            bpy.ops.object.modifier_move_up(modifier='dec')
        bpy.ops.object.modifier_apply(modifier='dec')
    info['tris'] = sum(len(p.vertices) - 2 for p in body.data.polygons)
    # A root to turn and size it by: facing +x.
    root = bpy.data.objects.new('rig_root', None)
    scene.collection.objects.link(root)
    top = arm or body
    top.parent = root
    root.matrix_world = Matrix.Rotation(math.pi / 2, 4, 'Z')
    bpy.context.view_layer.update()
    pts = world_pts(body)
    lo, hi = pts.min(0), pts.max(0)
    info['size_xyz'] = [round(float(x), 3) for x in hi - lo]
    info['min'] = [round(float(x), 3) for x in lo]
    info['missing_images'] = [im.name for im in bpy.data.images if im.source == 'FILE' and not os.path.exists(bpy.path.abspath(im.filepath))]
    info['images'] = [os.path.basename(im.filepath) for im in bpy.data.images if im.filepath]
    bpy.ops.wm.save_as_mainfile(filepath=dst, compress=True)
    # Previews: the studio's front, right side and back, lit as the sheets are.
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 2.2
    studio = R.Studio(scene)
    for l, e in zip(studio.lights, (4.5, 1.4, 3.0)):
        l.data.energy = e
    scene.cycles.samples = 24
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 4
    scene.render.use_border = False
    span = float(max(hi - lo)) * 1.25
    scene.render.resolution_x = scene.render.resolution_y = 512
    c = (lo + hi) / 2
    for v, tag in ((0, 'front'), (2, 'side'), (4, 'back')):
        right, up, toward = R.view(v)
        cv = Vector(c)
        studio.aim(v, cv.dot(right), cv.dot(up), span)
        scene.render.filepath = os.path.join(OUT, 'previews', '%s_%s.png' % (name, tag))
        bpy.ops.render.render(write_still=True)
    info['seconds'] = round(time.time() - t0)
    with open(os.path.join(OUT, 'models', name + '.json'), 'w') as fh:
        json.dump(info, fh, indent=1)
    print('DONE', name, {k: v for k, v in info.items() if k not in ('bones', 'weighted_bones', 'src')}, flush=True)
