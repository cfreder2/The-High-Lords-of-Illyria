"""Give an unrigged Tripo figure a rigged one's skeleton (the Mixamo-named humanoid): the donor's armature and
skinned body are stretched to the figure's box (height, span, depth, about the feet) and the figure takes the
donor's weights from the nearest point of its surface. Bone heat (Blender's automatic weights) fails on Tripo's
meshes: they are not watertight. Pick a donor of the same build and pose; hands, hooves, a tail or horns want a
look afterwards.

    $BPY -I tools/sprites/tripo/rig.py WORK TARGET DONOR      e.g. minotaur_fd1c8499 caveman_figure_9b4f445b
writes WORK/models/TARGET.blend over (the unrigged one is kept as TARGET_unrigged.blend) and posed previews."""
import math
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', '..', 'blender')))
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Quaternion, Vector  # noqa: E402

from sprites import render as R  # noqa: E402

WORK, TARGET, DONOR = sys.argv[1], sys.argv[2], sys.argv[3]
path = os.path.join(WORK, 'models', TARGET + '.blend')
keep = os.path.join(WORK, 'models', TARGET + '_unrigged.blend')
if not os.path.exists(keep):
    shutil.copy(path, keep)
bpy.ops.wm.open_mainfile(filepath=keep)
scene = bpy.context.scene
body = next(o for o in scene.objects if o.type == 'MESH')
root = bpy.data.objects['rig_root']
with bpy.data.libraries.load(os.path.join(WORK, 'models', DONOR + '.blend'), link=False) as (src, dst):
    dst.objects = [n for n in src.objects if n == 'Armature' or n.startswith('tripo_node')]
arm = next(o for o in dst.objects if o.type == 'ARMATURE')
donor = next(o for o in dst.objects if o.type == 'MESH')
for o in (arm, donor):
    scene.collection.objects.link(o)
arm.parent = root
bpy.context.view_layer.update()


def pts(ob):
    co = np.empty(len(ob.data.vertices) * 3)
    ob.data.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    mw = np.array(ob.matrix_world)
    return co @ mw[:3, :3].T + mw[:3, 3]


P, D = pts(body), pts(donor)
lo, hi = P.min(0), P.max(0)
dlo, dhi = D.min(0), D.max(0)
k = [float((hi[i] - lo[i]) / (dhi[i] - dlo[i])) for i in range(3)]
arm.matrix_world = Matrix.Diagonal((k[0], k[1], k[2], 1.0)) @ arm.matrix_world
bpy.context.view_layer.update()
# Bake the stretch into the bones and the donor's skin, so the rests are clean (no sheared bones).
bpy.ops.object.select_all(action='DESELECT')
donor.select_set(True)
bpy.context.view_layer.objects.active = donor
for md in list(donor.modifiers):
    donor.modifiers.remove(md)
mw = donor.matrix_world.copy()
donor.parent = None
donor.matrix_world = mw
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.select_all(action='DESELECT')
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
mw = arm.matrix_world.copy()
arm.parent = None
arm.matrix_world = mw
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
# The figure, unparented and in the same space.
mw = body.matrix_world.copy()
body.parent = None
body.matrix_world = mw
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for g in donor.vertex_groups:
    body.vertex_groups.new(name=g.name)
md = body.modifiers.new('weights', 'DATA_TRANSFER')
md.object = donor
md.use_vert_data = True
md.data_types_verts = {'VGROUP_WEIGHTS'}
md.vert_mapping = 'POLYINTERP_NEAREST'
md.layers_vgroup_select_src = 'ALL'
md.layers_vgroup_select_dst = 'NAME'
bpy.ops.object.modifier_apply(modifier='weights')
# The nearest donor surface is a rough guide where the figure is not the donor's shape (a bull's chest is deeper
# than a caveman's, its horns stand out over the shoulders): whatever stands above the head's joint is the head's
# alone (skull, ears, horns: they must not follow a shoulder), the weights are smoothed over the surface so no
# vertex jumps from its neighbours, kept to four bones, and normalised.
B = 'mixamorig:'
head_z = (arm.matrix_world @ arm.data.bones[B + 'Head'].head_local).z
hg = body.vertex_groups[B + 'Head']
mwb = body.matrix_world
names = {g.index: g.name for g in body.vertex_groups}
crown = [v.index for v in body.data.vertices if (mwb @ v.co).z > head_z]
for i in crown:
    for g in list(body.data.vertices[i].groups):
        if names[g.group] != B + 'Head':
            body.vertex_groups[g.group].remove([i])
    hg.add([i], 1.0, 'REPLACE')
bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
bpy.ops.object.vertex_group_smooth(group_select_mode='ALL', factor=0.5, repeat=4, expand=0.0)
bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
bpy.ops.object.vertex_group_normalize_all(group_select_mode='ALL', lock_active=False)
bpy.ops.object.mode_set(mode='OBJECT')
for i in crown:      # the smoothing spreads a little back into the crown
    for g in list(body.data.vertices[i].groups):
        if names[g.group] != B + 'Head':
            body.vertex_groups[g.group].remove([i])
    hg.add([i], 1.0, 'REPLACE')
# A leg's bones hold nothing above the waist (the transfer gave the minotaur's flanks to its thighs).
waist = (arm.matrix_world @ arm.data.bones[B + 'Spine1'].head_local).z
legs = {g.index for g in body.vertex_groups if any(k in g.name for k in ('UpLeg', 'Leg', 'Foot', 'Toe'))}
spine = body.vertex_groups[B + 'Spine']
freed = 0
for v in body.data.vertices:
    if (mwb @ v.co).z > waist:
        w = sum(g.weight for g in v.groups if g.group in legs)
        if w > 0:
            for g in [g for g in v.groups if g.group in legs]:
                body.vertex_groups[g.group].remove([v.index])
            spine.add([v.index], w, 'ADD')
            freed += 1
print('weights: %d vertices of the head and horns to the head alone; smoothed, four bones, normalised; %d above the waist off the legs'
      % (len(crown), freed), flush=True)
body.parent = arm
am = body.modifiers.new('Armature', 'ARMATURE')
am.object = arm
bpy.data.objects.remove(donor)
# As prep.py leaves a model: the armature under rig_root, which unit.py turns and sizes.
root.matrix_world = Matrix()
arm.parent = root
bpy.context.view_layer.update()
unw = sum(1 for v in body.data.vertices if not any(g.weight > 1e-4 for g in v.groups))
print('%s: rigged to %s, stretched %s, %d groups, %d unweighted verts' % (TARGET, DONOR, [round(x, 2) for x in k], len(body.vertex_groups), unw), flush=True)
bpy.ops.wm.save_as_mainfile(filepath=path, compress=True)

# A posed look: an arm raised, a stride, the trunk and head turned.
for pb in arm.pose.bones:
    pb.rotation_mode = 'QUATERNION'


def turn(name, axis, deg):
    pb = arm.pose.bones['mixamorig:' + name]
    W = arm.matrix_world
    q = Quaternion(Vector(axis), math.radians(deg)) @ (W @ pb.matrix).to_quaternion()
    pb.matrix = W.inverted() @ (Matrix.Translation(W @ pb.head) @ q.to_matrix().to_4x4() @ Matrix.Scale(W.to_scale()[0], 4))
    bpy.context.view_layer.update()


# The figure faces +x here, its left to +y.
turn('Spine1', (0, 0, 1), 20)
turn('Head', (0, 0, 1), -30)
turn('RightArm', (0, 1, 0), -70)
turn('RightForeArm', (0, 1, 0), -50)
turn('LeftArm', (1, 0, 0), 35)
turn('LeftUpLeg', (0, 1, 0), -35)
turn('LeftLeg', (0, 1, 0), 45)
turn('RightUpLeg', (0, 1, 0), 25)
scene.render.engine = 'CYCLES'
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 2.2
studio = R.Studio(scene)
for l, e in zip(studio.lights, (4.5, 1.4, 3.0)):
    l.data.energy = e
scene.cycles.samples = 24
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.use_border = False
scene.render.resolution_x = scene.render.resolution_y = 512
c = Vector((lo + hi) / 2)
for v in (1, 3):
    right, up, toward = R.view(v)
    studio.aim(v, c.dot(right), c.dot(up), float(max(hi - lo)) * 1.4)
    scene.render.filepath = os.path.join(WORK, 'previews', '%s_posed_v%d.png' % (TARGET, v))
    bpy.ops.render.render(write_still=True)
print('posed previews written', flush=True)
