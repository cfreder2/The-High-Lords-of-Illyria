"""The couched lance tried over a grid of hand, elbow and aim on a cavalry unit's real rider and horse: for each, where
the lance ends up pointing (the wrist cannot give every aim asked) and how far it is through the rider or the horse.
Prints the poses cavalry.rider_clips uses now, then the best of the grid.

    $BPY -I tools/sprites/tripo/couch_search.py REPO heavy_cavalry"""
import json
import math
import os
import sys

REPO, UNIT = sys.argv[1], sys.argv[2]
sys.argv = [sys.argv[0], 'build', UNIT]
sys.path.insert(0, os.path.join(REPO, 'tools', 'sprites', 'tripo'))
import bpy  # noqa
from mathutils import Matrix, Vector  # noqa
import unit as U  # noqa
import cavalry  # noqa
import clearance  # noqa
import keyer  # noqa
from keyer import B, lean  # noqa

cfg = U.load_config(UNIT)
m = cfg['mount']
# --- as build_mounted, without the clips
bpy.ops.wm.open_mainfile(filepath=os.path.join(cfg['work'], 'models', m['body'] + '.blend'))
scene = bpy.context.scene
harm = next(o for o in scene.objects if o.type == 'ARMATURE')
hbody = next(o for o in scene.objects if o.type == 'MESH' and o.parent == harm)
root = bpy.data.objects['rig_root']
for pb in harm.pose.bones:
    pb.matrix_basis = Matrix()
root.matrix_world = Matrix()
bpy.context.view_layer.update()
F = U.fitting
pts = F.body_points(hbody)
high = pts[pts[:, 2] > pts[:, 2].min() + 0.9 * (pts[:, 2].max() - pts[:, 2].min())]
d = high.mean(0) - pts.mean(0)
turn = Matrix.Rotation(-math.atan2(d[1], d[0]), 4, 'Z')
root.matrix_world = turn
bpy.context.view_layer.update()
pts = F.body_points(hbody)
k = m['length_m'] / float(pts[:, 0].max() - pts[:, 0].min())
root.matrix_world = Matrix.Scale(k, 4) @ turn
bpy.context.view_layer.update()
pts = F.body_points(hbody)
low = pts[pts[:, 2] < pts[:, 2].min() + 0.05 * m['length_m']]
root.matrix_world = Matrix.Translation(Vector((-float(low[:, 0].mean()), -float(low[:, 1].mean()), -float(pts[:, 2].min())))) @ root.matrix_world
bpy.context.view_layer.update()
H = cavalry.Horse(harm, hbody).read()
H.repair()
H.rest()
seat, half = cavalry.seat_of(H, m.get('seat', 0.60))
with bpy.data.libraries.load(os.path.join(cfg['work'], 'models', cfg['body'] + '.blend'), link=False) as (src, dst):
    dst.objects = [n for n in src.objects if n == 'Armature' or n.startswith('tripo_node')]
rarm = next(o for o in dst.objects if o.type == 'ARMATURE')
rbody = next(o for o in dst.objects if o.type == 'MESH')
for o in (rarm, rbody):
    scene.collection.objects.link(o)
rarm.name, rbody.name = 'rider_rig', 'body'
rarm.parent = None
bpy.context.view_layer.update()
Bn = rarm.data.bones
fwd = rarm.matrix_world.to_3x3() @ sum(((Bn[B + sd + 'ToeBase'].head_local - Bn[B + sd + 'Foot'].head_local) for sd in ('Left', 'Right')), Vector())
rp = F.body_points(rbody)
rk = cfg['rider_height_m'] / float(rp[:, 2].max() - rp[:, 2].min())
rarm.matrix_world = Matrix.Scale(rk, 4) @ Matrix.Rotation(-math.atan2(fwd.y, fwd.x), 4, 'Z') @ rarm.matrix_world
bpy.context.view_layer.update()
rp = F.body_points(rbody)
feet = (rarm.matrix_world @ Bn[B + 'LeftFoot'].head_local + rarm.matrix_world @ Bn[B + 'RightFoot'].head_local) / 2
rarm.matrix_world = Matrix.Translation(Vector((-feet.x, -feet.y, -float(rp[:, 2].min())))) @ rarm.matrix_world
bpy.context.view_layer.update()
for pb in rarm.pose.bones:
    pb.rotation_mode = 'QUATERNION'
rig = keyer.Rig(rarm)
F.trunk_off_arms(rig, rbody)
upright = bpy.data.objects.new('upright', None)
scene.collection.objects.link(upright)
for it in cfg.get('items', []):
    U.attach_item(it, cfg, scene, rarm, rig, upright)
lance = bpy.data.objects['lance']
kk = rig.rest_head['Hips'].z / 1.04
held = clearance.Held(rig, lance, kk)
P, _ = cavalry.rider_clips({n: 1.0 for n in ('idle', 'walk', 'run', 'attack', 'flinch', 'die_back', 'die_front', 'die_crumple', 'die_burn', 'celebrate')})


def axis():
    pl = held.local
    a, b = lance.matrix_world @ pl[-41], lance.matrix_world @ pl[-1]
    return a, b


def trial(p):
    cavalry.seated(rig, seat, half, p)
    bpy.context.view_layer.update()
    tree = clearance.skin_tree([rbody, hbody])
    held.frame()
    deep, bad = held.worst(tree)
    a, b = axis()
    dirn = (b - a).normalized()
    if dirn.x < 0:
        dirn = -dirn
    hips = rig.head('Hips')
    fist = rig.fist('Right')
    owners = {}
    for q, n, dd in bad:
        loc, nr, idx, dist = tree.find_nearest(q)
        ob, v = clearance.OWNERS[idx]
        owners[ob.name] = owners.get(ob.name, 0) + 1
    return deep, dirn, (fist - hips) / kk, owners, len(bad)


for name in ('COUCH', 'COUCH_UP', 'DRAW', 'THRUST'):
    deep, dirn, f, ow, nb = trial(P[name])
    print('%-9s now: %.1f cm through (%d pts %s), lance points (%.2f %.2f %.2f), fist at (%.2f %.2f %.2f) man-m from hips' % (
        name, deep * 100, nb, ow, dirn.x, dirn.y, dirn.z, f.x, f.y, f.z), flush=True)

import itertools
for pname in ('COUCH', 'THRUST'):
    base = dict(P[pname])
    best = []
    for (hx, hy, hz), (ex, ey), (bx, by, bz), edge in itertools.product(
            [(0.16, -0.26, 0.14), (0.22, -0.28, 0.18), (0.10, -0.30, 0.20), (0.20, -0.32, 0.10)],
            [(0.0, -0.50), (-0.12, -0.45), (0.05, -0.42)],
            [(1, -0.06, -0.03), (1, -0.4, -0.3), (1, -0.7, -0.5), (1, -0.3, -0.6), (1, -0.8, -0.1), (1, 0.0, -0.6)],
            [(0, 0, -1), (0, 1, 0), (0, -1, 0), (0, 0, 1)]):
        p = dict(base, sword_hand=(hx, hy, hz), sword_elbow=(ex, ey, hz + 0.05), blade=(bx, by, bz), edge=edge)
        deep, dirn, f, ow, nb = trial(p)
        err = math.degrees(math.acos(max(-1, min(1, dirn.dot(Vector((1, 0, 0)))))))
        best.append((deep > held.tol, err, deep, (hx, hy, hz), (ex, ey), (bx, by, bz), edge, (round(dirn.x, 2), round(dirn.y, 2), round(dirn.z, 2)), ow))
    best.sort(key=lambda r: (r[0], r[1]))
    print('==', pname)
    for r in best[:8]:
        print('clear' if not r[0] else 'IN   ', 'aim off %.0f deg, %.1f cm; hand %s elbow %s blade %s edge %s -> %s %s' % (r[1], r[2] * 100, r[3], r[4], r[5], r[6], r[7], r[8]), flush=True)
