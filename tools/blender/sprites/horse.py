"""A horse: skeleton, skinned body with the muscles that give a horse its
shape (shoulder, forearm, hindquarter, gaskin), legs with their knees, hocks,
fetlocks and hooves, head with ears, eyes and nostrils, mane and tail in
strands, and its tack (saddle, saddle cloth in the side's colour, girth,
bridle and reins). Measured for a horse 1.6 m at the withers standing at the
origin facing +x, and scaled.

Gaits are written as each leg's phase in the stride: a leg on the ground
sweeps back under the body; off the ground it folds and reaches forward.
"""

import math

from mathutils import Vector

from .geo import Part, Rand, blend, frame_for, smooth
from .rig import Bone, Pose

X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)

# The trunk, front to back: x, top, bottom, half width (1.6 m horse).
TRUNK = [(0.80, 1.34, 1.04, 0.13), (0.70, 1.48, 0.95, 0.205), (0.55, 1.58, 0.88, 0.245), (0.4, 1.6, 0.85, 0.27),
         (0.2, 1.52, 0.83, 0.295), (-0.05, 1.47, 0.85, 0.305), (-0.3, 1.5, 0.92, 0.3), (-0.5, 1.55, 0.99, 0.285),
         (-0.68, 1.54, 1.04, 0.255), (-0.82, 1.48, 1.1, 0.2), (-0.9, 1.41, 1.18, 0.13), (-0.94, 1.36, 1.26, 0.06)]


class Horse:
    def __init__(self, height=1.55, prefix='h_'):
        self.k = height / 1.6
        self.p = prefix

    def P(self, x, y, z):
        return Vector((x, y, z)) * self.k

    def n(self, name):
        return self.p + name

    def w(self, *pairs):
        if len(pairs) == 1:
            return {self.p + pairs[0]: 1.0}
        return {self.p + pairs[i]: pairs[i + 1] for i in range(0, len(pairs), 2)}

    # ------------------------------------------------------------ skeleton

    LEGS = {
        'f': [(0.40, 0.11, 1.50), (0.63, 0.165, 1.12), (0.49, 0.175, 0.88), (0.525, 0.165, 0.5), (0.525, 0.16, 0.17), (0.575, 0.16, 0.03)],
        'h': [(-0.5, 0.15, 1.3), (-0.33, 0.2, 0.93), (-0.64, 0.17, 0.56), (-0.61, 0.16, 0.17), (-0.56, 0.16, 0.03)],
    }
    FORE = ('scap', 'arm', 'fore', 'cannon', 'pastern')
    HIND = ('thigh', 'gaskin', 'cannon', 'pastern')

    def joint(self, leg, i, side):
        x, y, z = self.LEGS[leg][i]
        return self.P(x, y * side, z)

    def bones(self):
        P, n = self.P, self.n
        b = [Bone(n('root'), P(0, 0, 0), P(0, 0, 0.2), None, X),
             Bone(n('spine'), P(-0.55, 0, 1.3), P(0.0, 0, 1.3), n('root'), Z),
             Bone(n('chest'), P(0.0, 0, 1.3), P(0.5, 0, 1.36), n('spine'), Z),
             Bone(n('neck1'), P(0.58, 0, 1.4), P(0.85, 0, 1.72), n('chest'), Z),
             Bone(n('neck2'), P(0.85, 0, 1.72), P(1.06, 0, 1.96), n('neck1'), Z),
             Bone(n('head'), P(1.06, 0, 1.96), P(1.42, 0, 1.5), n('neck2'), X),
             Bone(n('tail1'), P(-0.9, 0, 1.4), P(-1.0, 0, 1.25), n('spine'), X),
             Bone(n('tail2'), P(-1.0, 0, 1.25), P(-1.06, 0, 0.98), n('tail1'), X),
             Bone(n('tail3'), P(-1.06, 0, 0.98), P(-1.08, 0, 0.62), n('tail2'), X)]
        for side, sf in ((1, 'L'), (-1, 'R')):
            parent = n('chest')
            for i, name in enumerate(self.FORE):
                b.append(Bone(n('f%s.%s' % (name, sf)), self.joint('f', i, side), self.joint('f', i + 1, side), parent, X))
                parent = n('f%s.%s' % (name, sf))
            parent = n('spine')
            for i, name in enumerate(self.HIND):
                b.append(Bone(n('h%s.%s' % (name, sf)), self.joint('h', i, side), self.joint('h', i + 1, side), parent, X))
                parent = n('h%s.%s' % (name, sf))
        return b

    # ------------------------------------------------------------ body

    def trunk_ring(self, x):
        """(top, bottom, half width) of the trunk at x, interpolated."""
        rows = TRUNK
        if x >= rows[0][0]:
            return rows[0][1:]
        for a, b in zip(rows, rows[1:]):
            if b[0] <= x <= a[0]:
                t = (a[0] - x) / (a[0] - b[0])
                return tuple(a[i] + (b[i] - a[i]) * t for i in (1, 2, 3))
        return rows[-1][1:]

    @staticmethod
    def trunk_shape(th):
        # Wider over the back than at the keel of the belly.
        return 1.0 + 0.1 * math.cos(th) - 0.04 * math.cos(2 * th)

    def trunk_point(self, x, th, out=0.0):
        """A point on the trunk's surface (1.6 m units in, scaled out): th 0 at the top, + toward the left."""
        top, bot, hw = self.trunk_ring(x)
        k = self.trunk_shape(th)
        c = Vector((x, 0, (top + bot) / 2))
        ru, rv = (top - bot) / 2, hw * self.GIRTH
        p = c + Vector((0, math.sin(th) * rv * k, math.cos(th) * ru * k))
        nrm = Vector((0, math.sin(th) / max(1e-6, rv), math.cos(th) / max(1e-6, ru))).normalized()
        return (p + nrm * out) * self.k, nrm

    def trunk_weights(self, x):
        t = smooth(-0.25, 0.25, x)
        return self.w('spine', 1 - t, 'chest', t)

    def body(self, part, coat):
        rings = []
        for x, top, bot, hw in TRUNK:
            rings.append({'c': self.P(x, 0, (top + bot) / 2), 'u': Vector((0, 0, 1)), 'v': Vector((0, 1, 0)),
                          'ru': (top - bot) / 2 * self.k, 'rv': hw * self.k * self.GIRTH, 'w': self.trunk_weights(x), 'shape': self.trunk_shape, 'dome': 0.6})
        part.loft(rings, coat, n=24)
        # The breast: a rounded front between the shoulders.
        part.ellipsoid(self.P(0.76, 0, 1.15), (0.1 * self.k, 0.16 * self.k, 0.18 * self.k), coat, self.w('chest'), seg=14, rings=8)
        self.neck(part, coat)
        self.head(part, coat)
        for side in (1, -1):
            self.foreleg(part, side, coat)
            self.hindleg(part, side, coat)

    def neck(self, part, coat):
        rows = [((0.42, 1.61), (0.8, 1.02), 0.21, self.w('chest')),
                ((0.6, 1.71), (0.93, 1.21), 0.172, self.w('chest', 0.5, 'neck1', 0.5)),
                ((0.76, 1.83), (1.01, 1.42), 0.138, self.w('neck1')),
                ((0.9, 1.95), (1.08, 1.62), 0.112, self.w('neck1', 0.5, 'neck2', 0.5)),
                ((1.0, 2.03), (1.14, 1.78), 0.095, self.w('neck2')),
                ((1.06, 2.06), (1.2, 1.86), 0.085, self.w('neck2', 0.4, 'head', 0.6))]
        rings = []
        for (tx, tz), (bx, bz), hw, w in rows:
            top, bot = self.P(tx, 0, tz), self.P(bx, 0, bz)
            u = (top - bot).normalized()
            rings.append({'c': (top + bot) / 2, 'u': u, 'v': Vector((0, 1, 0)), 'ru': (top - bot).length / 2, 'rv': hw * self.k * 1.12, 'w': w,
                          'shape': lambda th: 1.0 + 0.06 * math.cos(th)})
        part.loft(rings, coat, n=18, cap0=False)

    def head_frame(self):
        a, b = self.P(1.1, 0, 1.97), self.P(1.49, 0, 1.44)
        d = (b - a).normalized()
        front = Vector((d.z * -1, 0, d.x))  # perpendicular, toward the face
        if front.x < 0:
            front = -front
        return a, b, d, front

    def head(self, part, coat):
        a, b, d, front = self.head_frame()
        L = (b - a).length
        k = self.k
        rows = [(0.0, 0.0, 0.115, 0.085), (0.1, -0.035, 0.165, 0.102), (0.24, -0.02, 0.15, 0.1), (0.42, 0.0, 0.112, 0.08),
                (0.62, 0.005, 0.09, 0.067), (0.8, 0.0, 0.085, 0.068), (0.92, -0.004, 0.08, 0.07), (1.0, -0.01, 0.055, 0.055)]
        rings = []
        for t, off, ru, rv in rows:
            def shape(th, t=t):
                # The jowl rounds out behind and below; the face is flat in front.
                return 1.0 + 0.16 * max(0.0, -math.cos(th)) ** 2 * smooth(0.35, 0.05, t) - 0.06 * max(0.0, math.cos(th)) ** 3
            rings.append({'c': a + d * (L * t) + front * (off * k), 'u': front, 'v': Vector((0, 1, 0)), 'ru': ru * k * 1.08, 'rv': rv * k * 1.06,
                          'w': self.w('head'), 'shape': shape, 'dome': 0.5})
        part.loft(rings, coat, n=18)
        w = self.w('head')
        for side in (1, -1):
            # Ears, eyes, nostrils.
            base = a + d * (0.02 * L) + front * (0.04 * k) + Vector((0, side * 0.055 * k, 0))
            tip_dir = (Vector((0, side * 0.25, 1.0)) - d * 0.35).normalized()
            pts = [base, base + tip_dir * 0.06 * k, base + tip_dir * 0.12 * k, base + tip_dir * 0.155 * k]
            part.chain(pts, [(0.03 * k, 0.022 * k), (0.033 * k, 0.022 * k), (0.022 * k, 0.014 * k), (0.004 * k, 0.003 * k)], coat, [w] * 4, hint=front, n=8)
            eye = a + d * (0.25 * L) + front * (0.055 * k) + Vector((0, side * 0.1 * k, 0))
            part.ellipsoid(eye, (0.022 * k, 0.012 * k, 0.017 * k), 'horse_eye', w, seg=10, rings=6)
            nos = a + d * (0.94 * L) + front * (0.04 * k) + Vector((0, side * 0.04 * k, 0))
            part.ellipsoid(nos, (0.018 * k, 0.012 * k, 0.025 * k), 'horse_eye', w, seg=8, rings=5)

    def _leg(self, part, pts, radii, ws, coat, lower, hoof_at, hoof_w, socks_from=3):
        P = [self.P(*p) for p in pts]
        # Legs a little stouter than life, so they read at a sprite's size.
        R = [(r[0] * self.k * self.LEG, r[1] * self.k * self.LEG) for r in radii]
        mat = lambda j, s: lower if j >= socks_from else coat
        part.chain(P, R, mat, ws, hint=Vector((1, 0, 0)), n=12)
        # The hoof: a slanted cone to the ground.
        c = self.P(*hoof_at)
        rings = []
        for z, r, dx in ((0.075, 0.042, -0.005), (0.04, 0.05, 0.01), (0.0, 0.058, 0.025)):
            rings.append({'c': Vector((c.x + dx * self.k, c.y, z * self.k + 0.004)), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                          'ru': r * self.k * 1.05, 'rv': r * self.k * 0.92, 'w': hoof_w, 'dome': 0.1})
        part.loft(rings, 'hoof', n=12)

    def foreleg(self, part, side, coat):
        sf = 'L' if side > 0 else 'R'
        y = lambda v: v * side
        n = lambda b: 'f%s.%s' % (b, sf)
        # The shoulder: a broad flat muscle from the withers to the point of the shoulder and down to the elbow.
        pts = [self.P(0.38, y(0.12), 1.48), self.P(0.52, y(0.17), 1.3), self.P(0.62, y(0.185), 1.12), self.P(0.53, y(0.19), 0.96)]
        part.chain(pts, [(0.1 * self.k, 0.04 * self.k), (0.14 * self.k, 0.06 * self.k), (0.11 * self.k, 0.06 * self.k), (0.085 * self.k, 0.065 * self.k)],
                   coat, [self.w(n('scap'), 0.5, 'chest', 0.5), self.w(n('scap')), self.w(n('scap'), 0.5, n('arm'), 0.5), self.w(n('arm'), 0.6, n('fore'), 0.4)],
                   hint=Vector((1, 0, 0)), n=12)
        pts = [(0.5, y(0.175), 0.98), (0.505, y(0.17), 0.8), (0.52, y(0.165), 0.62), (0.525, y(0.165), 0.5), (0.525, y(0.162), 0.4),
               (0.525, y(0.16), 0.26), (0.53, y(0.16), 0.17), (0.555, y(0.16), 0.1), (0.57, y(0.16), 0.07)]
        radii = [(0.08, 0.065), (0.06, 0.05), (0.045, 0.04), (0.046, 0.042), (0.033, 0.03), (0.031, 0.028), (0.043, 0.036), (0.032, 0.03), (0.038, 0.035)]
        ws = [self.w(n('arm'), 0.3, n('fore'), 0.7), self.w(n('fore')), self.w(n('fore')), self.w(n('fore'), 0.5, n('cannon'), 0.5),
              self.w(n('cannon')), self.w(n('cannon')), self.w(n('cannon'), 0.5, n('pastern'), 0.5), self.w(n('pastern')), self.w(n('pastern'))]
        self._leg(part, pts, radii, ws, coat, self.points, (0.578, y(0.16), 0.0), self.w(n('pastern')))

    def hindleg(self, part, side, coat):
        sf = 'L' if side > 0 else 'R'
        y = lambda v: v * side
        n = lambda b: 'h%s.%s' % (b, sf)
        # The hindquarter: the great muscles from the croup over the hip to the stifle.
        pts = [self.P(-0.6, y(0.14), 1.45), self.P(-0.55, y(0.2), 1.25), self.P(-0.42, y(0.22), 1.05), self.P(-0.35, y(0.205), 0.92)]
        part.chain(pts, [(0.17 * self.k, 0.06 * self.k), (0.2 * self.k, 0.08 * self.k), (0.16 * self.k, 0.075 * self.k), (0.1 * self.k, 0.065 * self.k)],
                   coat, [self.w('spine'), self.w('spine', 0.5, n('thigh'), 0.5), self.w(n('thigh')), self.w(n('thigh'), 0.6, n('gaskin'), 0.4)],
                   hint=Vector((1, 0, 0)), n=12)
        # The gaskin to the hock, then the cannon, fetlock and pastern.
        pts = [(-0.38, y(0.19), 0.92), (-0.47, y(0.18), 0.78), (-0.57, y(0.172), 0.64), (-0.645, y(0.17), 0.555), (-0.63, y(0.165), 0.45),
               (-0.615, y(0.16), 0.28), (-0.61, y(0.16), 0.17), (-0.585, y(0.16), 0.1), (-0.565, y(0.16), 0.07)]
        radii = [(0.1, 0.07), (0.085, 0.06), (0.06, 0.045), (0.055, 0.04), (0.036, 0.03), (0.032, 0.028), (0.043, 0.036), (0.032, 0.03), (0.038, 0.035)]
        ws = [self.w(n('thigh'), 0.4, n('gaskin'), 0.6), self.w(n('gaskin')), self.w(n('gaskin')), self.w(n('gaskin'), 0.5, n('cannon'), 0.5),
              self.w(n('cannon')), self.w(n('cannon')), self.w(n('cannon'), 0.5, n('pastern'), 0.5), self.w(n('pastern')), self.w(n('pastern'))]
        self._leg(part, pts, radii, ws, coat, self.points, (-0.558, y(0.16), 0.0), self.w(n('pastern')))
        # The point of the hock.
        part.ellipsoid(self.P(-0.68, y(0.17), 0.58), (0.03 * self.k, 0.028 * self.k, 0.04 * self.k), coat, self.w(n('gaskin'), 0.5, n('cannon'), 0.5), seg=8, rings=5)

    points = 'coat_black'
    LEG = 1.3
    GIRTH = 1.08

    # ------------------------------------------------------------ hair

    def strand(self, part, pts, r0, mat, ws, flat=0.45):
        radii = []
        for i in range(len(pts)):
            t = i / (len(pts) - 1)
            r = r0 * (1 - 0.8 * t) + 0.002
            radii.append((r, r * flat))
        part.chain(pts, radii, mat, ws, hint=Vector((0, 1, 0)), n=6, dome=0.2)

    def mane_and_tail(self, part, mat='mane'):
        rnd = Rand(4242)
        k = self.k
        # Mane: locks along the crest from the poll to the withers, falling to the horse's right.
        for i in range(26):
            t = i / 25
            # Crest points (interpolate the neck's top line).
            x = 1.06 - 0.62 * t
            z = 2.06 - 0.5 * t - 0.15 * t * t
            root = self.P(x - 0.02, 0, z + 0.015)
            fall = (0.24 + 0.08 * rnd.next()) * k * (1.0 - 0.35 * t)
            side = Vector((0.0, -1.0, 0.0))
            pts = []
            for j in range(5):
                s = j / 4
                pts.append(root + side * (fall * 0.45 * math.sin(s * 1.6)) + Vector((-0.03 * s * k, 0, -fall * s)) + Vector((0, 0, 0)))
            nb = 'neck2' if t < 0.35 else ('neck1' if t < 0.8 else 'chest')
            self.strand(part, pts, 0.036 * k, mat, [self.w(nb)] * 5, flat=0.5)
        # Forelock.
        a, b, d, front = self.head_frame()
        for i in range(3):
            root = a + front * 0.06 * k + Vector((0, (i - 1) * 0.025 * k, 0.03 * k))
            pts = [root + (d * 0.05 * s + front * 0.02 * s) * k * 2.4 for s in (0, 0.33, 0.66, 1.0)]
            self.strand(part, pts, 0.016 * k, mat, [self.w('head')] * 4)
        # Tail: locks from the dock, falling and spreading.
        dock = self.P(-0.92, 0, 1.4)
        for i in range(14):
            ang = 2 * math.pi * i / 14
            spread = Vector((0, math.sin(ang), math.cos(ang) * 0.5)) * 0.03 * k
            length = (0.85 + 0.12 * rnd.next()) * k
            pts, ws = [], []
            for j in range(7):
                s = j / 6
                drop = length * s
                p = dock + spread * (1 + 3 * s) + Vector((-0.12 * k * math.sin(min(1.0, s * 2.2)) - 0.05 * k * s, 0, -drop))
                pts.append(p)
                ws.append(self.w('tail1') if s < 0.2 else (self.w('tail2') if s < 0.5 else self.w('tail3')))
            self.strand(part, pts, 0.026 * k, mat, ws, flat=0.6)

    # ------------------------------------------------------------ tack

    def tack(self, part, team='team', leather='leather', dark='leather_dark', metal='steel_dark'):
        k = self.k
        # Saddle cloth: over the back, hanging down both sides, in the side's colour.
        def cloth(s, t):
            x = 0.32 - 0.62 * t
            th = (2 * s - 1) * 1.55
            p, nrm = self.trunk_point(x, th, out=0.012)
            return p
        part.grid(cloth, 16, 10, team, lambda s, t: self.trunk_weights(0.32 - 0.62 * t))
        # A dark trim round its edge.
        for s in (0.0, 1.0):
            pts = [cloth(s, t) for t in (0.0, 0.25, 0.5, 0.75, 1.0)]
            pts = [p + (p - Vector((p.x, 0, 1.25 * k))).normalized() * 0.004 for p in pts]
            part.chain(pts, [0.012 * k] * 5, dark, [self.trunk_weights(0.32 - 0.62 * t) for t in (0.0, 0.25, 0.5, 0.75, 1.0)], hint=Vector((0, 0, 1)), n=6)

        # The saddle: seat, pommel and cantle, its flaps down the sides.
        def seat(s, t):
            x = 0.24 - 0.48 * t
            th = (2 * s - 1) * 1.05
            rise = 0.03 + 0.1 * smooth(0.25, 0.0, t) + 0.085 * smooth(0.75, 1.0, t)
            rise *= 1.0 - 0.6 * abs(2 * s - 1) ** 2
            p, nrm = self.trunk_point(x, th, out=0.03 + rise)
            return p
        part.slab(seat, 12, 12, 0.03 * k, leather, lambda s, t: self.trunk_weights(0.24 - 0.48 * t), rim_mat=dark)
        # Girth.
        rings = []
        for x in (0.3, 0.24):
            top, bot, hw = self.trunk_ring(x)
            rings.append({'c': self.P(x, 0, (top + bot) / 2), 'u': Vector((0, 0, 1)), 'v': Vector((0, 1, 0)),
                          'ru': ((top - bot) / 2 + 0.01) * k, 'rv': (hw * self.GIRTH + 0.01) * k, 'w': self.w('chest'), 'shape': self.trunk_shape})
        part.loft(rings, dark, n=24, cap0=False, cap1=False)
        # Stirrup leathers and irons.
        for side in (1, -1):
            top, _ = self.trunk_point(0.05, side * 1.2, out=0.03)
            iron = self.P(0.08, side * 0.33, 0.98)
            part.chain([top, top.lerp(iron, 0.5), iron + Vector((0, 0, 0.04 * k))], [0.008 * k] * 3, dark, [self.trunk_weights(0.05)] * 3, hint=Vector((1, 0, 0)), n=6)
            part.box(iron, (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))), (0.05 * k, 0.03 * k, 0.012 * k), metal, self.trunk_weights(0.05))
        # Bridle: noseband, browband, cheekpieces; the bit.
        a, b, d, front = self.head_frame()
        L = (b - a).length
        w = self.w('head')
        for t, grow in ((0.68, 0.012), (0.1, 0.01)):
            c = a + d * (L * t)
            rings = []
            for dz in (-0.012, 0.012):
                rings.append({'c': c + d * dz * k, 'u': front, 'v': Vector((0, 1, 0)), 'ru': (0.095 if t > 0.5 else 0.155) * k + grow,
                              'rv': (0.072 if t > 0.5 else 0.1) * k + grow, 'w': w})
            part.loft(rings, dark, n=16, cap0=False, cap1=False)
        for side in (1, -1):
            pts = [a + d * (L * t) + Vector((0, side * (0.1 - 0.03 * t) * k, 0)) - front * 0.01 * k for t in (0.08, 0.4, 0.75)]
            part.chain(pts, [0.008 * k] * 3, dark, [w] * 3, hint=front, n=6)
            part.ellipsoid(a + d * (L * 0.86) + Vector((0, side * 0.075 * k, 0)) - front * 0.04 * k, (0.016 * k, 0.006 * k, 0.016 * k), metal, w, seg=8, rings=4)

    def reins(self, part, hand_bone, hand_point, dark='leather_dark'):
        """Reins from the bit to the rider's hand, sharing the two ends' weights."""
        a, b, d, front = self.head_frame()
        L = (b - a).length
        k = self.k
        for side in (1, -1):
            bit = a + d * (L * 0.86) + Vector((0, side * 0.08 * k, 0)) - front * 0.04 * k
            end = hand_point + Vector((0, side * 0.015, 0))
            pts, ws = [], []
            for i in range(6):
                t = i / 5
                p = bit.lerp(end, t) + Vector((0, 0, -0.05 * k * math.sin(math.pi * t)))
                pts.append(p)
                ws.append(blend(self.w('head'), {hand_bone: 1.0}, smooth(0.15, 0.85, t)))
            part.chain(pts, [0.007 * k] * 6, dark, ws, hint=Vector((0, 0, 1)), n=6)

    def seat(self):
        """Where a rider's hip joints sit (the saddle's seat)."""
        p, _ = self.trunk_point(0.0, 0.0, out=0.08)
        return p

    # ------------------------------------------------------------ poses

    def stand(self, p):
        return p

    def leg_pose(self, p, leg, side, swing, flex, flex2=0.0, hoof=0.0):
        """swing: the leg forward (degrees); flex: the knee or hock folded; flex2:
        the elbow or stifle; hoof: the fetlock folded."""
        lr = lambda b, axis, deg: p.lr(self.n(b), side, axis, deg)
        if leg == 'f':
            lr('fscap', Y, -swing)
            lr('farm', Y, flex2 * 0.6)
            lr('ffore', Y, -flex2 * 1.0)
            lr('fcannon', Y, flex)
            lr('fpastern', Y, hoof)
        else:
            lr('hthigh', Y, -swing)
            lr('hgaskin', Y, flex2)
            lr('hcannon', Y, -flex * 1.0)
            lr('hpastern', Y, hoof)

    def gallop(self, p, t, rider_load=True):
        """t in [0, 1): one stride of the transverse gallop."""
        phases = {('h', 1): 0.0, ('h', -1): 0.11, ('f', 1): 0.3, ('f', -1): 0.42}
        stance = 0.32
        for (leg, side), ph in phases.items():
            q = (t - ph) % 1.0
            if q < stance:  # on the ground: from reaching forward to pushing back
                u = q / stance
                swing = (24 if leg == 'f' else 20) * (1 - 2 * u)
                flex = 6 * math.sin(math.pi * u)
                flex2 = 4 * math.sin(math.pi * u)
                hoof = -22 * math.sin(math.pi * u) - 6 * u
            else:  # in the air: folding, then reaching
                u = (q - stance) / (1 - stance)
                swing = (24 if leg == 'f' else 20) * (-1 + 2 * smooth(0.0, 0.85, u)) + (8 * math.sin(math.pi * u) if leg == 'f' else -6 * math.sin(math.pi * u))
                fold = math.sin(math.pi * min(1.0, u * 1.25)) ** 1.2
                flex = (95 if leg == 'f' else 70) * fold
                flex2 = (30 if leg == 'f' else 40) * fold
                hoof = (40 if leg == 'f' else 35) * fold
            self.leg_pose(p, leg, side, swing, flex, flex2, hoof)
        # The body rocks: forehand up as the hind legs land, down onto the leading foreleg.
        pitch = 5 * math.sin(2 * math.pi * (t - 0.05))
        lift = 0.05 * math.cos(2 * math.pi * (t - 0.85)) * self.k
        p.move(self.n('root'), (0, 0, lift))
        p.turn(self.n('spine'), Y, -pitch)
        p.turn(self.n('chest'), Y, -pitch * 0.3)
        p.turn(self.n('neck1'), Y, pitch * 1.4 + 8)
        p.turn(self.n('neck2'), Y, pitch * 0.6)
        p.turn(self.n('head'), Y, -pitch * 0.8)
        # The tail streams out behind.
        p.turn(self.n('tail1'), Y, 32).turn(self.n('tail2'), Y, 8 + 6 * math.sin(2 * math.pi * t)).turn(self.n('tail3'), Y, -4 + 8 * math.sin(2 * math.pi * t + 1))
        return p
