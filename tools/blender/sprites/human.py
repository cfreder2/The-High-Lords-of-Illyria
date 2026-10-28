"""A human figure: skeleton, skinned body, head, hands, and the five
animations (idle, walk, attack, flinch, die) as poses of the rig.

Measurements are written for a man 1.8 m tall standing at the origin facing
+x (the game's convention) and scaled to the figure's height; widths are also
scaled by its bulk. Body parts are lofted tubes through anatomical
cross-sections, each ring weighted to the bones it follows, then smoothed by
a Subdivision Surface modifier after the armature deforms them.
"""

import math

from mathutils import Matrix, Quaternion, Vector

from . import geo
from .geo import W, blend, smooth
from .rig import Bone, Pose


class Body:
    """Scales the 1.8 m measurements: P(x, y, z) for points, R(r) for radii."""

    def __init__(self, height=1.8, bulk=1.0, offset=None, prefix=''):
        self.s = height / 1.8
        self.b = bulk
        self.o = offset or Vector()
        self.p = prefix

    def P(self, x, y, z):
        return Vector((x * self.s, y * self.s * self.b, z * self.s)) + self.o

    def R(self, r):
        if isinstance(r, tuple):
            return (r[0] * self.s * self.b, r[1] * self.s * self.b)
        return r * self.s * self.b

    def n(self, name):
        return self.p + name

    def w(self, *pairs):
        if len(pairs) == 1:
            return {self.p + pairs[0]: 1.0}
        return {self.p + pairs[i]: pairs[i + 1] for i in range(0, len(pairs), 2)}


def skeleton(B, parent=None):
    """The bones of a person. parent: the bone the hips hang from (a mount's
    saddle) or None (then a root bone at the feet, about which falls turn)."""
    P, n = B.P, B.n
    bones = []
    if parent is None:
        bones.append(Bone(n('root'), B.o, B.o + Vector((0, 0, 0.12)), None))
        hip_parent = n('root')
    else:
        hip_parent = parent
    bones += [
        Bone(n('hips'), P(0, 0, 0.98), P(0, 0, 1.10), hip_parent, (1, 0, 0)),
        Bone(n('spine'), P(0, 0, 1.10), P(0, 0, 1.28), n('hips'), (1, 0, 0)),
        Bone(n('chest'), P(0, 0, 1.28), P(0, 0, 1.50), n('spine'), (1, 0, 0)),
        Bone(n('neck'), P(0, 0, 1.50), P(0.01, 0, 1.60), n('chest'), (1, 0, 0)),
        Bone(n('head'), P(0.01, 0, 1.60), P(0.01, 0, 1.80), n('neck'), (1, 0, 0)),
    ]
    for side, sf in ((1, 'L'), (-1, 'R')):
        y = lambda v: v * side
        bones += [
            Bone(n('clavicle.' + sf), P(0.01, y(0.03), 1.45), P(-0.005, y(0.17), 1.465), n('chest'), (1, 0, 0)),
            Bone(n('upperarm.' + sf), P(0, y(0.185), 1.44), P(-0.01, y(0.235), 1.15), n('clavicle.' + sf), (1, 0, 0)),
            Bone(n('forearm.' + sf), P(-0.01, y(0.235), 1.15), P(0.0, y(0.263), 0.91), n('upperarm.' + sf), (1, 0, 0)),
            Bone(n('hand.' + sf), P(0.0, y(0.263), 0.91), P(0.004, y(0.27), 0.74), n('forearm.' + sf), (1, 0, 0)),
            Bone(n('thigh.' + sf), P(0.0, y(0.09), 0.95), P(0.015, y(0.1), 0.52), n('hips'), (1, 0, 0)),
            Bone(n('shin.' + sf), P(0.015, y(0.1), 0.52), P(-0.01, y(0.105), 0.085), n('thigh.' + sf), (1, 0, 0)),
            Bone(n('foot.' + sf), P(-0.01, y(0.105), 0.085), P(0.15, y(0.105), 0.025), n('shin.' + sf), (0, 0, 1)),
        ]
    return bones


def superellipse(e):
    def f(th):
        c, s = abs(math.cos(th)), abs(math.sin(th))
        return (c ** e + s ** e) ** (-1.0 / e)
    return f


# ------------------------------------------------------------------- body

def torso(part, B, mat, grow=0.0, z_from=0.86, z_to=1.52, mat_fn=None):
    """The trunk from crotch to the base of the neck. grow: metres added all
    round (clothes over it)."""
    rows = [
        (0.86, 0.115, 0.085, -0.005, B.w('hips')),
        (0.93, 0.165, 0.110, -0.012, B.w('hips')),
        (1.00, 0.160, 0.100, -0.004, B.w('hips')),
        (1.07, 0.146, 0.093, 0.004, B.w('hips', 0.6, 'spine', 0.4)),
        (1.14, 0.143, 0.096, 0.008, B.w('hips', 0.2, 'spine', 0.8)),
        (1.22, 0.150, 0.103, 0.013, B.w('spine')),
        (1.30, 0.163, 0.110, 0.020, B.w('spine', 0.4, 'chest', 0.6)),
        (1.38, 0.176, 0.112, 0.022, B.w('chest')),
        (1.44, 0.19, 0.100, 0.012, B.w('chest')),
        (1.49, 0.158, 0.085, 0.000, B.w('chest')),
        (1.52, 0.075, 0.062, -0.005, B.w('chest', 0.5, 'neck', 0.5)),
    ]
    rows = [r for r in rows if z_from - 1e-6 <= r[0] <= z_to + 1e-6]
    shape = superellipse(2.4)
    rings = []
    for z, hw, hd, cx, w in rows:
        rings.append({'c': B.P(cx, 0, z), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                      'ru': B.R(hd) + grow, 'rv': B.R(hw) + grow, 'w': w, 'shape': shape})
    return part.loft(rings, mat, n=20)


def neck(part, B, mat):
    pts = [B.P(0.0, 0, 1.47), B.P(0.005, 0, 1.55), B.P(0.012, 0, 1.64)]
    return part.chain(pts, [B.R((0.062, 0.06)), B.R((0.056, 0.054)), B.R((0.05, 0.05))], mat,
                      [B.w('chest', 0.6, 'neck', 0.4), B.w('neck'), B.w('neck', 0.4, 'head', 0.6)], n=12)


def arm(part, B, side, mat, mat_hand_end=None, sleeve=0.0):
    P, R, ws = arm_rows(B, side, sleeve)
    return part.chain(P, R, mat, ws, hint=Vector((1, 0, 0)), n=10)


def arm_rows(B, side, sleeve=0.0):
    """The arm's sections from the shoulder to the wrist: points, radii, weights."""
    sf = 'L' if side > 0 else 'R'
    y = lambda v: v * side
    pts = [(0.0, 0.15, 1.45), (0.0, 0.195, 1.425), (-0.005, 0.205, 1.32), (-0.008, 0.22, 1.23), (-0.01, 0.235, 1.15),
           (-0.007, 0.243, 1.08), (-0.003, 0.253, 0.99), (0.0, 0.263, 0.915)]
    radii = [(0.055, 0.055), (0.062, 0.064), (0.052, 0.048), (0.046, 0.042), (0.038, 0.037), (0.043, 0.038), (0.035, 0.03), (0.027, 0.021)]
    ws = [B.w('clavicle.' + sf, 0.5, 'upperarm.' + sf, 0.5), B.w('upperarm.' + sf, 0.8, 'clavicle.' + sf, 0.2),
          B.w('upperarm.' + sf), B.w('upperarm.' + sf), B.w('upperarm.' + sf, 0.5, 'forearm.' + sf, 0.5),
          B.w('forearm.' + sf), B.w('forearm.' + sf), B.w('forearm.' + sf, 0.8, 'hand.' + sf, 0.2)]
    P = [B.P(p[0], y(p[1]), p[2]) for p in pts]
    R = [tuple(x + sleeve for x in B.R(r)) for r in radii]
    return P, R, ws


def leg(part, B, side, mat, sleeve=0.0, to_z=0.0):
    sf = 'L' if side > 0 else 'R'
    y = lambda v: v * side
    rows = [((0.0, 0.085, 0.99), (0.09, 0.09), B.w('hips', 0.7, 'thigh.' + sf, 0.3)),
            ((0.005, 0.092, 0.90), (0.088, 0.083), B.w('hips', 0.2, 'thigh.' + sf, 0.8)),
            ((0.01, 0.097, 0.78), (0.075, 0.07), B.w('thigh.' + sf)),
            ((0.012, 0.1, 0.64), (0.06, 0.056), B.w('thigh.' + sf)),
            ((0.016, 0.1, 0.52), (0.05, 0.047), B.w('thigh.' + sf, 0.5, 'shin.' + sf, 0.5)),
            ((0.006, 0.102, 0.42), (0.053, 0.047), B.w('shin.' + sf)),
            ((-0.003, 0.104, 0.30), (0.046, 0.041), B.w('shin.' + sf)),
            ((-0.008, 0.105, 0.18), (0.035, 0.032), B.w('shin.' + sf)),
            ((-0.01, 0.105, 0.09), (0.03, 0.028), B.w('shin.' + sf, 0.6, 'foot.' + sf, 0.4))]
    rows = [r for r in rows if r[0][2] >= to_z]
    P = [B.P(p[0], y(p[1]), p[2]) for p, _, _ in rows]
    R = [tuple(x + sleeve for x in B.R(r)) for _, r, _ in rows]
    ws = [w for _, _, w in rows]

    def shape(i, th):
        z = rows[i][0][2]
        back = max(0.0, -math.cos(th))
        front = max(0.0, math.cos(th))
        k = 1.0
        if 0.25 < z < 0.47:  # the calf
            k += 0.16 * back ** 2
        if 0.48 < z < 0.56:  # the kneecap
            k += 0.08 * front ** 3
        if 0.6 < z < 0.85:  # the thigh's front
            k += 0.05 * front ** 2
        return k
    return part.chain(P, R, mat, ws, hint=Vector((1, 0, 0)), n=12, shape=shape)


def foot(part, B, side, mat):
    """A boot or bare foot: heel to toe, the sole flat on the ground."""
    sf = 'L' if side > 0 else 'R'
    y = 0.105 * side
    rows = [(-0.06, 0.06, (0.042, 0.03)), (-0.03, 0.07, (0.065, 0.036)), (0.02, 0.06, (0.06, 0.04)),
            (0.08, 0.04, (0.04, 0.046)), (0.14, 0.028, (0.028, 0.044)), (0.185, 0.024, (0.022, 0.032))]
    rings = []
    for x, z, r in rows:
        w = B.w('shin.' + sf, 0.4, 'foot.' + sf, 0.6) if x < -0.02 else B.w('foot.' + sf)
        rr = B.R(r)
        rings.append({'c': B.P(x, y, z), 'u': Vector((0, 0, 1)), 'v': Vector((0, 1, 0)), 'ru': rr[0], 'rv': rr[1], 'w': w, 'dome': 0.6})
    first = part.loft(rings, mat, n=12)
    floor = B.o.z + 0.004
    for i in range(first, len(part.verts)):
        if part.verts[i].z < floor:
            part.verts[i].z = floor
    return first


def hand(part, B, side, mat, grip=True):
    """A hand at the wrist, gripping (a fist round a haft along +x) or relaxed."""
    sf = 'L' if side > 0 else 'R'
    w = B.w('hand.' + sf)
    s = B.s * B.b ** 0.5
    # Built in a frame at the wrist: x forward, y outward, z down the hand.
    origin = B.P(0.0, 0.263 * side, 0.915)
    m = Matrix.Translation(origin) @ Matrix.Diagonal((s, s * side, s, 1))
    first = len(part.verts)
    part.ellipsoid(Vector((0.0, -0.002, -0.045)), (0.043, 0.019, 0.05), mat, w, seg=12, rings=8)
    if grip:
        # Curled fingers round the grip, and the thumb over them.
        part.ellipsoid(Vector((0.0, -0.018, -0.088)), (0.044, 0.024, 0.026), mat, w, seg=12, rings=8)
        part.chain([Vector((0.03, -0.012, -0.03)), Vector((0.045, -0.032, -0.06)), Vector((0.035, -0.045, -0.08))],
                   [0.014, 0.012, 0.01], mat, [w, w, w], n=8)
    else:
        part.ellipsoid(Vector((0.004, -0.008, -0.105)), (0.04, 0.016, 0.04), mat, w, seg=12, rings=8)
        part.chain([Vector((0.03, -0.012, -0.03)), Vector((0.05, -0.022, -0.06)), Vector((0.052, -0.03, -0.085))],
                   [0.013, 0.011, 0.009], mat, [w, w, w], n=8)
    part.transform(m, first)
    if side < 0:  # mirrored: faces turned inside out
        _flip_since(part, first)
    return first


def grip_point(B, side):
    """Where a gripping hand holds its haft (the haft runs along +x at rest)."""
    s = B.s * B.b ** 0.5
    return B.P(0.0, 0.263 * side, 0.915) + Vector((0.0, -0.018 * s * side, -0.088 * s))


def _flip_since(part, first_vert):
    for k, f in enumerate(part.faces):
        if min(f) >= first_vert:
            part.faces[k] = tuple(reversed(f))


# ------------------------------------------------------------------- head

def _g(d, w):
    return math.exp(-(d / w) ** 2)


def face_shape(q):
    """The head's radius factor in direction q (head frame: x forward, z up)."""
    x, y, z = q.x, q.y, q.z
    ay = abs(y)
    k = 1.0
    front = smooth(0.2, 0.75, x)
    # Jaw and chin: narrower below, the back of the neck drawn in.
    if z < 0:
        k *= 1.0 - 0.15 * (-z) * smooth(0.1, 0.9, ay) * (0.6 + 0.4 * front)
        k *= 1.0 - 0.18 * smooth(-0.2, -0.7, x) * smooth(-0.2, -0.8, z)
    k += 0.08 * _g(math.dist((x, y, z), (0.72, 0.0, -0.68)), 0.3)  # chin
    # Forehead and occiput.
    k += 0.04 * _g(math.dist((x, y, z), (-0.6, 0.0, 0.25)), 0.4)
    # Brow ridge, eye sockets, cheekbones.
    k += 0.07 * front * _g(z - 0.25, 0.07) * smooth(0.65, 0.2, ay)
    k -= 0.1 * front * _g(z - 0.13, 0.08) * _g(ay - 0.33, 0.12)
    k += 0.035 * front * _g(z + 0.02, 0.1) * _g(ay - 0.55, 0.12)
    # The nose: a ridge from the brow to the tip, widening into the nostrils.
    if x > 0.5 and -0.32 < z < 0.2:
        t = (0.2 - z) / 0.42  # 0 at the bridge, 1 at the tip
        width = 0.06 + 0.07 * t ** 2
        amp = (0.07 + 0.2 * t ** 1.5) * smooth(-0.3, -0.25, z)
        k += amp * _g(ay, width)
    # The mouth: a parting between the lips.
    k += 0.018 * front * _g(z + 0.4, 0.03) * _g(ay, 0.18)
    k -= 0.02 * front * _g(z + 0.45, 0.02) * _g(ay, 0.16)
    k += 0.012 * front * _g(z + 0.5, 0.03) * _g(ay, 0.15)
    return k


def head_center(B):
    return B.P(0.015, 0, 1.685)


def head_radii(B):
    s = B.s * (0.5 + 0.5 * B.b)
    return (0.102 * s, 0.078 * s, 0.118 * s)


def head(part, B, skin, eye_mat='eye', ears=True):
    """Skull and face, with eyes and ears; rigid to the head bone."""
    w = B.w('head')
    c, r = head_center(B), head_radii(B)
    part.ellipsoid(c, r, skin, w, seg=36, rings=28, shape=face_shape)
    for side in (1, -1):
        q = Vector((0.83, 0.33 * side, 0.13)).normalized()
        p = c + Vector((q.x * r[0], q.y * r[1], q.z * r[2])) * 0.93
        part.ellipsoid(p, (r[0] * 0.07, r[1] * 0.1, r[2] * 0.06), eye_mat, w, seg=8, rings=5)
        if ears:
            part.ellipsoid(c + Vector((-0.01 * B.s, side * r[1] * 0.98, -0.01 * B.s)), (r[0] * 0.26, r[1] * 0.16, r[2] * 0.3), skin, w, seg=10, rings=6)


def hair_shell(part, B, mat, long=0.0, w=None):
    """Hair over the skull: a shell that tucks under the skin over the face.
    long: metres it falls below the head at the back."""
    w = w or B.w('head')
    c, r = head_center(B), head_radii(B)

    def shape(q):
        face = smooth(0.15, 0.45, q.x) * smooth(0.42, 0.18, q.z)
        nape = smooth(-0.5, -0.85, q.z) * smooth(0.2, -0.3, q.x)
        k = 1.09 - 0.25 * face - 0.12 * nape * (1.0 if long <= 0 else 0.0)
        # Locks: a ripple round the head.
        ph = math.atan2(q.y, q.x)
        return k + 0.012 * math.sin(ph * 14 + q.z * 3)
    part.ellipsoid(c + Vector((-0.006 * B.s, 0, 0.006 * B.s)), r, mat, w, seg=32, rings=22, shape=shape)
    if long > 0:
        # A fall of hair down the back to the shoulders.
        rings = []
        for j in range(7):
            t = j / 6
            z = c.z - r[2] * 0.1 - long * t
            spread = 1.0 + 0.5 * t
            ww = blend(w, B.w('chest'), smooth(0.3, 1.0, t))
            rings.append({'c': Vector((c.x - r[0] * (0.55 + 0.25 * t), c.y, z)), 'u': Vector((-1, 0, 0)), 'v': Vector((0, 1, 0)),
                          'ru': r[0] * (0.42 - 0.2 * t), 'rv': r[1] * 1.08 * spread, 'w': ww,
                          'shape': lambda th, t=t: 1.0 + 0.05 * math.sin(th * 9 + t * 4)})
        part.loft(rings, mat, n=16, cap1=True)


# ------------------------------------------------------------------- poses
#
# Axes of the rig's rest frame: x forward, y the figure's left, z up. For the
# left limbs (mirrored for the right):
#   upper arm or thigh forward:   about +y, negative angle
#   elbow bent (forearm forward): about +y, negative
#   knee bent (shin back):        about +y, positive
#   arm out to the side:          about +x, positive
#   torso leaning forward:        about +y, positive
#   turning left:                 about +z, positive

Y = (0, 1, 0)
X = (1, 0, 0)
Z = (0, 0, 1)


class Stance:
    """The joint angles of a person in degrees (left/right), from which a Pose is made."""

    def __init__(self, **kw):
        self.lean = 0.0  # forward
        self.side_lean = 0.0  # toward the left
        self.twist = 0.0  # chest turned left relative to the hips
        self.hip_twist = 0.0
        self.nod = 0.0  # head down
        self.head_turn = 0.0
        self.thigh = [0.0, 0.0]  # forward (L, R)
        self.thigh_out = [3.0, 3.0]
        self.knee = [4.0, 4.0]
        self.ankle = [0.0, 0.0]  # toes down
        self.shoulder = [0.0, 0.0]  # forward
        self.shoulder_out = [6.0, 6.0]
        self.arm_twist = [0.0, 0.0]  # upper arm turned inward
        self.elbow = [10.0, 10.0]
        self.wrist = [0.0, 0.0]  # hand bent up (palm side forward)
        self.wrist_roll = [0.0, 0.0]
        self.clav = [0.0, 0.0]  # shoulder raised
        self.root = Vector()  # the whole figure moved
        self.fall = 0.0  # the whole figure turned backward about the feet
        self.roll = 0.0  # ... and onto its side (toward its left)
        for k, v in kw.items():
            setattr(self, k, v)

    def copy(self):
        s = Stance()
        for k, v in self.__dict__.items():
            setattr(s, k, list(v) if isinstance(v, list) else (v.copy() if isinstance(v, Vector) else v))
        return s


def mix(a, b, t):
    out = a.copy()
    for k, v in a.__dict__.items():
        u = getattr(b, k)
        if isinstance(v, list):
            setattr(out, k, [x + (y - x) * t for x, y in zip(v, u)])
        elif isinstance(v, Vector):
            setattr(out, k, v.lerp(u, t))
        else:
            setattr(out, k, v + (u - v) * t)
    return out


def keyed(keys, t):
    """keys: [(time, Stance)], t in [0, 1], eased between keys."""
    if t <= keys[0][0]:
        return keys[0][1].copy()
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t <= t1:
            u = (t - t0) / max(1e-6, t1 - t0)
            return mix(a, b, u * u * (3 - 2 * u))
    return keys[-1][1].copy()


def to_pose(st, B, pose=None, mounted=False):
    p = pose if pose is not None else Pose()
    n = B.n
    if not mounted:
        p.move(n('root'), st.root)
        if st.fall:
            p.turn(n('root'), Y, -st.fall)
        if st.roll:
            p.turn(n('root'), X, -st.roll)
    else:
        p.move(n('hips'), st.root)
    p.turn(n('hips'), Z, st.hip_twist)
    p.turn(n('spine'), Y, st.lean * 0.5).turn(n('spine'), X, -st.side_lean * 0.5).turn(n('spine'), Z, st.twist * 0.5)
    p.turn(n('chest'), Y, st.lean * 0.5).turn(n('chest'), X, -st.side_lean * 0.5).turn(n('chest'), Z, st.twist * 0.5)
    p.turn(n('neck'), Y, st.nod * 0.4).turn(n('neck'), Z, st.head_turn * 0.5)
    p.turn(n('head'), Y, st.nod * 0.6).turn(n('head'), Z, st.head_turn * 0.5)
    for i, side in ((0, 1), (1, -1)):
        sf = '.L' if side > 0 else '.R'
        lr = lambda bone, axis, deg: p.lr(n(bone), side, axis, deg)
        lr('clavicle', X, st.clav[i])
        # Upper arm: out to the side, then forward, then turned about itself.
        lr('upperarm', X, st.shoulder_out[i] - 6.0)
        lr('upperarm', Y, -st.shoulder[i])
        if st.arm_twist[i]:
            # About the upper arm's own (turned) line: approximated by the rest's line.
            lr('upperarm', Z, -st.arm_twist[i])
        lr('forearm', Y, -st.elbow[i] + 10.0)
        lr('hand', Y, -st.wrist[i])
        if st.wrist_roll[i]:
            lr('hand', Z, st.wrist_roll[i])
        lr('thigh', X, st.thigh_out[i] - 3.0)
        lr('thigh', Y, -st.thigh[i])
        lr('shin', Y, st.knee[i] - 4.0)
        lr('foot', Y, st.ankle[i] - (st.knee[i] - 4.0) + st.thigh[i] if not mounted else st.ankle[i])
    return p
