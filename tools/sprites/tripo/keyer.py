"""Pose-to-pose keying for a Tripo model on a Mixamo-named skeleton (first written for the knight; the archers,
the hyena-folk and the units of tools/sprites/tripo/unit.py use it).

A pose is written in his own space (x forward, y his left, z up, metres, origin between his feet): where his feet
stand, how low and how turned his hips are, how his chest leans and turns, where his sword hand is and which way the
blade points, which way the shield faces. Arms and legs are solved with two-bone IK, so a pose is exact and
repeatable. A clip is a list of timed key poses; the in-betweens slerp every bone (arcs, not straight lines) with an
ease per segment, and the legs are re-solved every frame so planted feet do not slide."""
import math
import bpy
from mathutils import Matrix, Quaternion, Vector

B = 'mixamorig:'
X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
FPS = 24


def yaw(deg):
    return Quaternion(Z, math.radians(deg))


def lean(pitch=0.0, roll=0.0, turn=0.0):
    """Turn about z (+ = toward his left), then pitch about his left axis (+ = forward), then roll (+ = tip left)."""
    return yaw(turn) @ Quaternion(Y, math.radians(pitch)) @ Quaternion(X, math.radians(-roll))


def unit(*v):
    return Vector(v).normalized()


class Rig:
    def __init__(self, arm):
        self.arm, self.P = arm, arm.pose.bones
        self.W = arm.matrix_world.copy()
        self.Wi = self.W.inverted()
        self.k = self.W.to_scale()[0]
        self.rest()
        self.rest_rot = {pb.name[len(B):]: self.rot(pb.name[len(B):]) for pb in self.P}
        self.rest_head = {pb.name[len(B):]: self.head(pb.name[len(B):]) for pb in self.P}
        L = lambda a, b: (self.rest_head[b] - self.rest_head[a]).length
        self.lengths = {s: (L(s + 'Arm', s + 'ForeArm'), L(s + 'ForeArm', s + 'Hand'),
                            L(s + 'UpLeg', s + 'Leg'), L(s + 'Leg', s + 'Foot')) for s in ('Left', 'Right')}
        self.sword = self.shield = None
        self.fist_depth = 0.03 * self.rest_head['Hips'].z / 1.04

    def gear(self, sword=None, shield=None):
        """The knight's own: a sword in the right hand (pose keys `blade`, `edge`) and a shield strapped across the
        left forearm (`shield_face`). Units without them leave this out and use build_pose()."""
        self.sword, self.shield = sword, shield
        if sword is not None:
            self._blade_frame()

    # --- basics ------------------------------------------------------------------------------------------------
    def rest(self):
        for pb in self.P:
            pb.matrix_basis = Matrix()
        self.update()

    def update(self):
        bpy.context.view_layer.update()

    def head(self, n):
        return self.W @ self.P[B + n].head

    def rot(self, n):
        return (self.W @ self.P[B + n].matrix).to_quaternion()

    def set_rot(self, n, q, pos=None):
        """Give bone n this world rotation (and its head this world position, for the hips)."""
        pb = self.P[B + n]
        h = pos if pos is not None else self.head(n)
        pb.matrix = self.Wi @ (Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Scale(self.k, 4))
        self.update()

    def yaxis(self, n):
        return self.rot(n) @ Y

    def aim(self, n, d):
        """Point bone n along world direction d by the smallest turn, keeping its roll."""
        self.set_rot(n, self.yaxis(n).rotation_difference(d.normalized()) @ self.rot(n))

    def roll(self, n, deg):
        self.set_rot(n, Quaternion(self.yaxis(n), math.radians(deg)) @ self.rot(n))

    def two_bone(self, a, b, end, target, pole, la, lb):
        A = self.head(a)
        d = target - A
        dist = max(1e-3, min(d.length, la + lb - 1e-4))
        dn = d.normalized()
        p = pole - A
        p = (p - dn * dn.dot(p)).normalized()
        ca = (la * la + dist * dist - lb * lb) / (2 * la * dist)
        sa = math.sqrt(max(0.0, 1 - ca * ca))
        E = A + dn * (la * ca) + p * (la * sa)
        self.aim(a, E - A)
        self.aim(b, A + dn * dist - E)

    # --- hands -------------------------------------------------------------------------------------------------
    def hand_follows_forearm(self, side):
        self.set_rot(side + 'Hand', self.rot(side + 'ForeArm') @ (self.rest_rot[side + 'ForeArm'].inverted() @ self.rest_rot[side + 'Hand']))

    def knuckles(self, side):
        return [self.head(side + 'Hand' + f + '1') for f in ('Index', 'Middle', 'Ring', 'Pinky')]

    def turn_knuckles(self, side, want):
        """Roll the hand about its own axis so its knuckle line (pinky to index) points as near `want` as it can."""
        a = self.yaxis(side + 'Hand')
        kn = self.knuckles(side)
        have = kn[0] - kn[3]
        have = (have - a * a.dot(have)).normalized()
        w = (want - a * a.dot(want)).normalized()
        self.roll(side + 'Hand', math.degrees(math.atan2(a.dot(have.cross(w)), have.dot(w))))

    def palm(self, side):
        """The palm's normal, from the knuckle line and the hand's axis; the thumb is on the palm side."""
        kn = self.knuckles(side)
        a = self.yaxis(side + 'Hand')
        n = a.cross(kn[0] - kn[3]).normalized()
        if (self.head(side + 'HandThumb2') - kn[1]).dot(n) < 0:
            n = -n
        return n

    def fist(self, side):
        """The middle of the closed hand: where a hilt or shaft lies."""
        kn = self.knuckles(side)
        return sum(kn, Vector()) / 4 + self.palm(side) * self.fist_depth

    def palm_offset(self, side):
        """From the wrist to the middle of the closed hand, as the hand is now."""
        return self.fist(side) - self.head(side + 'Hand')

    def curl(self, side, deg):
        """Close the hand: every finger joint bent `deg` toward the palm (a fist at about 60), the thumb half as far."""
        n = self.palm(side)
        for f in ('Index', 'Middle', 'Ring', 'Pinky', 'Thumb'):
            for k in (1, 2, 3):
                name = '%sHand%s%d' % (side, f, k)
                if B + name not in self.P:
                    continue
                d = self.yaxis(name)
                axis = d.cross(n)
                if axis.length < 1e-4:
                    continue
                a = deg * (0.5 if f == 'Thumb' else 1.0)
                self.set_rot(name, Quaternion(axis.normalized(), math.radians(a)) @ self.rot(name))

    # --- the gear ----------------------------------------------------------------------------------------------
    def _blade_frame(self):
        """The sword's blade direction and flat normal relative to the hand bone, from the sword's own shape."""
        self.rest()
        ob = self.sword
        pts = [ob.matrix_world @ v.co for v in ob.data.vertices]
        c = sum(pts, Vector()) / len(pts)
        cov = Matrix(((0,) * 3,) * 3)
        for p in pts:
            q = p - c
            for i in range(3):
                for j in range(3):
                    cov[i][j] += q[i] * q[j]
        import numpy as np
        w, v = np.linalg.eigh(np.array(cov))
        blade = Vector(v[:, 2]); flat = Vector(v[:, 0])
        wrist = self.head('RightHand')
        if blade.dot(c - wrist) < 0:
            blade = -blade
        hand = self.rot('RightHand').inverted()
        self.blade_local, self.flat_local = hand @ blade, hand @ flat

    def grip(self, blade, edge):
        """Turn the sword hand so the blade points along `blade`, its edge leading toward `edge`."""
        b = blade.normalized()
        e = (edge - b * b.dot(edge)).normalized()
        n = b.cross(e)
        q = self.rot('RightHand')
        b0, n0 = q @ self.blade_local, q @ self.flat_local
        e0 = n0.cross(b0)
        F0 = Matrix((b0, e0, n0)).transposed()
        F1 = Matrix((b, e, n)).transposed()
        self.set_rot('RightHand', (F1 @ F0.inverted()).to_quaternion() @ q)
        self.settle_wrist('Right')

    def settle_wrist(self, side, limit=65.0):
        """A hand turned to aim a weapon, made a hand an arm could hold so: the twist about the forearm's own line is
        given to the forearm (it rolls: that is what forearms do), and what bend remains past `limit` degrees is
        taken off (the weapon then points a little short of where the pose asked)."""
        fa, hand = side + 'ForeArm', side + 'Hand'
        want = self.rot(hand)
        rel = self.rest_rot[fa].inverted() @ self.rest_rot[hand]
        d = (self.rot(fa) @ rel).rotation_difference(want)
        axis = self.yaxis(fa)
        proj = Vector((d.x, d.y, d.z)).dot(axis)
        twist = math.degrees(2.0 * math.atan2(proj, d.w))
        twist = (twist + 180.0) % 360.0 - 180.0
        self.roll(fa, max(-80.0, min(80.0, twist * 0.85)))
        straight = self.rot(fa) @ rel
        bend = math.degrees(straight.rotation_difference(want).angle)
        if bend > 180.0:
            bend = 360.0 - bend
        self.set_rot(hand, straight.slerp(want, limit / bend) if bend > limit else want)

    def strap_shield(self):
        """Strap the shield ACROSS the left forearm (rest pose): the forearm crosses its upper third, the face
        perpendicular to the forearm, its top along forearm x face. With the forearm level the shield hangs upright."""
        self.rest()
        ob = self.shield
        co = [v.co for v in ob.data.vertices]
        dims = [max(c[i] for c in co) - min(c[i] for c in co) for i in range(3)]
        H = max(range(3), key=lambda i: dims[i]); D = min(range(3), key=lambda i: dims[i]); Wd = 3 - H - D
        mid = [c for c in co if abs(c[H]) < 0.08 and abs(c[Wd]) < 0.08]
        front = 1.0 if max(c[D] for c in mid) > -min(c[D] for c in mid) else -1.0
        elbow, wrist = self.head('LeftForeArm'), self.head('LeftHand')
        along = (wrist - elbow).normalized()
        out = Y - along * Y.dot(along); out.normalize()
        up = along.cross(out)
        x = up.cross(out)
        target = Matrix((x, up, out)).transposed()
        sx = Vector([0, 0, 0]); sx[Wd] = 1.0
        sy = Vector([0, 0, 0]); sy[H] = 1.0
        sz = Vector([0, 0, 0]); sz[D] = front
        source = Matrix((sx, sy, sz)).transposed()
        h = dims[H]
        centre = (elbow + wrist) / 2 + out * 0.09 - up * (h * (0.5 - 0.32))
        ob.matrix_world = Matrix.Translation(centre) @ (target @ source.inverted()).to_4x4()
        self.update()
        self.shield_face_local = self.rot('LeftForeArm').inverted() @ out

    def face_shield(self, d):
        """Roll the left forearm about itself so the shield faces as near to world direction d as it can."""
        a = self.yaxis('LeftForeArm')
        want = d - a * a.dot(d); want.normalize()
        have = self.rot('LeftForeArm') @ self.shield_face_local
        have = have - a * a.dot(have); have.normalize()
        ang = math.degrees(math.atan2(a.dot(have.cross(want)), have.dot(want)))
        self.roll('LeftForeArm', ang)
        # The hand stays straight on the forearm, gripping the strap.
        self.set_rot('LeftHand', self.rot('LeftForeArm') @ (self.rest_rot['LeftForeArm'].inverted() @ self.rest_rot['LeftHand']))

    # --- a whole pose --------------------------------------------------------------------------------------------
    def pose(self, p):
        """Build pose `p` (a dict, see GUARD) on the rig from rest, and return it as local bone rotations."""
        self.rest()
        hips = Vector(p['hips'])
        self.set_rot('Hips', p.get('hips_rot', lean()) @ self.rest_rot['Hips'], hips)
        chest = p.get('chest', lean())
        hr = p.get('hips_rot', lean())
        for n, w in (('Spine', 0.34), ('Spine1', 0.67), ('Spine2', 1.0)):
            self.set_rot(n, hr.slerp(chest, w) @ self.rest_rot[n])
        look = p.get('head', chest)
        self.set_rot('Neck', chest.slerp(look, 0.5) @ self.rest_rot['Neck'])
        self.set_rot('Head', look @ self.rest_rot['Head'])
        for side, n in (('Left', 'LeftShoulder'), ('Right', 'RightShoulder')):
            if side + '_shrug' in p:
                self.set_rot(n, Quaternion(self.rot('Spine2') @ X, math.radians(p[side + '_shrug'])) @ self.rot(n))
        # sword arm
        la, lb = self.lengths['Right'][:2]
        self.two_bone('RightArm', 'RightForeArm', 'RightHand', Vector(p['sword_hand']), Vector(p['sword_elbow']), la, lb)
        self.set_rot('RightHand', self.rot('RightForeArm') @ (self.rest_rot['RightForeArm'].inverted() @ self.rest_rot['RightHand']))
        self.grip(Vector(p['blade']), Vector(p.get('edge', (0, 0, -1))))
        # shield arm
        la, lb = self.lengths['Left'][:2]
        self.two_bone('LeftArm', 'LeftForeArm', 'LeftHand', Vector(p['shield_hand']), Vector(p['shield_elbow']), la, lb)
        self.face_shield(Vector(p['shield_face']))
        self.legs(p['feet'], p.get('free', 0.0), {s: knee_dir(p, s) for s in ('Left', 'Right')})
        return self.snapshot(), p

    def legs(self, feet, free=0.0, knee=None):
        """Solve both legs to put each ankle at its spot (x, y, toe yaw, lift), the knee bending toward its
        direction, the foot flat and turned by its yaw; `free` (0..1) lets the foot hang off the shin instead,
        for a man lying down."""
        for side in ('Left', 'Right'):
            fx, fy, fyaw = feet[side][:3]
            lift = feet[side][3] if len(feet[side]) > 3 else 0.0
            ankle = Vector((fx, fy, self.rest_head[side + 'Foot'].z + lift))
            bend = knee[side] if knee else yaw(fyaw) @ X * 0.6 + Z * 0.5
            la, lb = self.lengths[side][2:]
            self.two_bone(side + 'UpLeg', side + 'Leg', side + 'Foot', ankle, ankle + bend, la, lb)
            flat = yaw(fyaw) @ self.rest_rot[side + 'Foot']
            if free > 0:
                hang = self.rot(side + 'Leg') @ (self.rest_rot[side + 'Leg'].inverted() @ self.rest_rot[side + 'Foot'])
                flat = flat.slerp(hang, free)
            self.set_rot(side + 'Foot', flat)

    def snapshot(self):
        return {pb.name: (pb.location.copy(), pb.rotation_quaternion.copy()) for pb in self.P}

    def apply(self, snap):
        for pb in self.P:
            loc, q = snap[pb.name]
            pb.location, pb.rotation_quaternion = loc, q
        self.update()


# --- easing and clips ------------------------------------------------------------------------------------------------
EASE = {
    'linear': lambda u: u,
    'ease': lambda u: u * u * (3 - 2 * u),
    'in': lambda u: u * u * u,              # accelerating into the blow
    'out': lambda u: 1 - (1 - u) ** 3,      # decelerating out of it
    'hold': lambda u: 0.0,
}


def knee_dir(p, side):
    """Which way a pose's knee bends: its own `knee` entry, else forward over the toes and up."""
    if 'knee' in p and side in p['knee']:
        return Vector(p['knee'][side])
    return yaw(p['feet'][side][2]) @ X * 0.6 + Z * 0.5


def treadmill(period, swing, lift, feet):
    """In-place stepping: feet[side] = (front x, back x, y, toe yaw, phase its swing starts). A planted foot slides
    from front to back at an even speed (the ground going by); in its swing (that fraction of the cycle) it eases
    forward again, lifted on an arc."""
    def gait(t):
        out = {}
        for side, (front, back, y, fyaw, start) in feet.items():
            ph = (t / period - start) % 1.0
            if ph < swing:
                u = ph / swing
                out[side] = (back + (front - back) * u * u * (3 - 2 * u), y, fyaw, lift * math.sin(math.pi * u))
            else:
                u = (ph - swing) / (1 - swing)
                out[side] = (front + (back - front) * u, y, fyaw, 0.0)
        return out
    return gait


def build_pose(rig, p):
    """Rig.pose for a figure with no sword or shield: the right hand goes to `sword_hand` (bending toward
    `sword_elbow`), the left to `shield_hand`; each hand straight on its forearm, closed by `fist` (side -> degrees)."""
    rig.rest()
    hips = Vector(p['hips'])
    hr = p.get('hips_rot', lean())
    rig.set_rot('Hips', hr @ rig.rest_rot['Hips'], hips)
    chest = p.get('chest', lean())
    for n, w in (('Spine', 0.34), ('Spine1', 0.67), ('Spine2', 1.0)):
        rig.set_rot(n, hr.slerp(chest, w) @ rig.rest_rot[n])
    look = p.get('head', chest)
    rig.set_rot('Neck', chest.slerp(look, 0.5) @ rig.rest_rot['Neck'])
    rig.set_rot('Head', look @ rig.rest_rot['Head'])
    for side, hand, elbow in (('Right', 'sword_hand', 'sword_elbow'), ('Left', 'shield_hand', 'shield_elbow')):
        la, lb = rig.lengths[side][:2]
        rig.two_bone(side + 'Arm', side + 'ForeArm', side + 'Hand', Vector(p[hand]), Vector(p[elbow]), la, lb)
        rig.hand_follows_forearm(side)
        if p.get('fist', {}).get(side):
            rig.curl(side, p['fist'][side])
    rig.legs(p['feet'], p.get('free', 0.0), {s: knee_dir(p, s) for s in ('Left', 'Right')})
    return rig.snapshot(), p


def measured_pose(rig, snap):
    """A pose dict for an existing snapshot (an animation's frame): where its feet stand, for the bake's leg solve."""
    rig.apply(snap)
    feet = {}
    for side in ('Left', 'Right'):
        ankle = rig.head(side + 'Foot')
        toe = rig.head(side + 'ToeBase') - ankle
        feet[side] = (ankle.x, ankle.y, math.degrees(math.atan2(toe.y, toe.x)), max(0.0, ankle.z - rig.rest_head[side + 'Foot'].z))
    return {'feet': feet}


def bake(rig, name, keys, poses, gait=None):
    """keys: [(seconds, pose name, ease into this key)]. Writes an action, every frame keyed. Feet step on an arc
    when a key moves them; `gait(t)` places them instead, for walking and running in place."""
    arm = rig.arm
    ad = arm.animation_data or arm.animation_data_create()
    act = bpy.data.actions.new('Armature|' + name)
    act.use_fake_user = True
    ad.action = act
    last = int(round(keys[-1][0] * FPS))
    prev = {}
    for f in range(0, last + 1):
        t = f / FPS
        i = max(j for j in range(len(keys)) if keys[j][0] <= t + 1e-9)
        if i == len(keys) - 1:
            a = b = keys[i]; u = 0.0
        else:
            a, b = keys[i], keys[i + 1]
            u = EASE[b[2]]((t - a[0]) / (b[0] - a[0]))
        (sa, pa), (sb, pz) = poses[a[1]], poses[b[1]]
        for pb in rig.P:
            la, qa = sa[pb.name]; lb, qb = sb[pb.name]
            if qa.dot(qb) < 0:
                qb = -qb
            pb.location = la.lerp(lb, u)
            pb.rotation_quaternion = qa.slerp(qb, u)
        rig.update()
        feet, knee = {}, {}
        for side in ('Left', 'Right'):
            A = list(pa['feet'][side]) + [0.0] * (4 - len(pa['feet'][side]))
            Bf = list(pz['feet'][side]) + [0.0] * (4 - len(pz['feet'][side]))
            spot = [A[k] + (Bf[k] - A[k]) * u for k in range(4)]
            if math.hypot(A[0] - Bf[0], A[1] - Bf[1]) > 0.02:
                spot[3] += 0.10 * math.sin(math.pi * u)
            feet[side] = spot
            knee[side] = knee_dir(pa, side).lerp(knee_dir(pz, side), u)
        if gait:
            feet = gait(t)
        free = pa.get('free', 0.0) + (pz.get('free', 0.0) - pa.get('free', 0.0)) * u
        rig.legs(feet, free, knee)
        for pb in rig.P:
            q = pb.rotation_quaternion
            if pb.name in prev and prev[pb.name].dot(q) < 0:
                pb.rotation_quaternion = -q
            prev[pb.name] = pb.rotation_quaternion.copy()
            pb.keyframe_insert('location', frame=f + 1)
            pb.keyframe_insert('rotation_quaternion', frame=f + 1)
    return act
