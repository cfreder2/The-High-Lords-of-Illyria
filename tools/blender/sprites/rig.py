"""Armatures and their keyframes.

A rig is built from bone specs (name, head, tail, parent). Poses are written
the way one thinks of them: each bone turned about axes of the armature's own
rest frame (x forward, y the figure's left, z up), in its parent's turned
frame; rig.pose() converts that to Blender's bone-local quaternions. Left
bones end in .L (+y side) and right bones in .R; a pose given for the left
side is mirrored onto the right with mirror().

The sheet's 26 frames (figures::kFirstFrame) are scene frames 1..26, every
bone keyed on every frame; timeline markers name the five animations.
"""

import math

import bpy
from mathutils import Matrix, Quaternion, Vector

ANIMS = (('idle', 4), ('walk', 8), ('attack', 6), ('flinch', 2), ('die', 6))
FIRST = {'idle': 0, 'walk': 4, 'attack': 12, 'flinch': 18, 'die': 20}
FRAMES = 26


class Bone:
    def __init__(self, name, head, tail, parent=None, up=(0, 0, 1)):
        self.name, self.head, self.tail, self.parent, self.up = name, Vector(head), Vector(tail), parent, Vector(up)


def build(name, bones, collection):
    arm = bpy.data.armatures.new(name)
    ob = bpy.data.objects.new(name, arm)
    collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode='EDIT')
    eb = {}
    for b in bones:
        e = arm.edit_bones.new(b.name)
        e.head, e.tail = b.head, b.tail
        # Roll: the bone's z axis as near b.up as it can be.
        e.align_roll(b.up if abs((b.tail - b.head).normalized().dot(b.up.normalized())) < 0.99 else Vector((1, 0, 0)))
        e.use_deform = True
        eb[b.name] = e
    for b in bones:
        if b.parent:
            eb[b.name].parent = eb[b.parent]
            eb[b.name].use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in ob.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    return ob


def rot(axis, deg):
    return Quaternion(Vector(axis).normalized(), math.radians(deg))


def mirror_axis(axis):
    # Reflection y -> -y turns a rotation about (x, y, z) into one about (-x, y, -z).
    return (-axis[0], axis[1], -axis[2])


class Pose(dict):
    """bone -> list of (axis, degrees), turned in order; '@loc' entries: bone -> Vector."""

    def __init__(self):
        super().__init__()
        self.loc = {}

    def turn(self, bone, axis, deg):
        self.setdefault(bone, []).append((tuple(axis), deg))
        return self

    def lr(self, base, side, axis, deg):
        """Turn base.L (side +1) or base.R (side -1), the axis given for the left."""
        if side > 0:
            return self.turn(base + '.L', axis, deg)
        return self.turn(base + '.R', mirror_axis(axis), deg)

    def both(self, base, axis, deg):
        self.lr(base, 1, axis, deg)
        return self.lr(base, -1, axis, deg)

    def move(self, bone, delta):
        self.loc[bone] = self.loc.get(bone, Vector()) + Vector(delta)
        return self


def apply(ob, pose):
    """Sets the pose bones of armature ob to pose (others to rest)."""
    for pb in ob.pose.bones:
        rest = pb.bone.matrix_local.to_3x3()
        q = Quaternion()
        for axis, deg in pose.get(pb.name, []):
            q = rot(axis, deg) @ q
        local = (rest.inverted() @ q.to_matrix() @ rest).to_quaternion()
        pb.rotation_quaternion = local
        d = pose.loc.get(pb.name)
        if d is not None:
            # A pose bone's location is in its rest frame.
            pb.location = rest.inverted() @ d
        else:
            pb.location = (0, 0, 0)


def key(ob, frame):
    for pb in ob.pose.bones:
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        pb.keyframe_insert('location', frame=frame)


def key_all(ob, pose_of):
    """pose_of(anim, i, n) -> Pose for frame i of n of anim; keys all 26 frames."""
    scene = bpy.context.scene
    for anim, n in ANIMS:
        scene.timeline_markers.new(anim, frame=FIRST[anim] + 1)
        for i in range(n):
            apply(ob, pose_of(anim, i, n))
            key(ob, FIRST[anim] + i + 1)
    # Exactly the keyed values on each frame.
    act = ob.animation_data.action if ob.animation_data else None
    if act is not None:
        for fc in _fcurves(act):
            for kp in fc.keyframe_points:
                kp.interpolation = 'CONSTANT'
    scene.frame_start, scene.frame_end = 1, FRAMES


def _fcurves(act):
    if hasattr(act, 'fcurves') and act.fcurves is not None:
        try:
            return list(act.fcurves)
        except Exception:
            pass
    out = []
    for layer in getattr(act, 'layers', []):
        for strip in layer.strips:
            for bag in strip.channelbags:
                out.extend(bag.fcurves)
    return out
