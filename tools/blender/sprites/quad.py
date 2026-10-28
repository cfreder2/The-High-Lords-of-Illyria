"""Four-legged creatures other than the horse (horse.py): a body lofted
through a trunk table, a neck, legs of four bones each (upper, middle, lower,
foot) through their joints, a tail; heads and feet by kind. A wolf, an
elephant, a griffin and a dragon are each a table of measurements here and a
head of their own in beasts.py.

Gaits are phases per leg as in horse.py: on the ground the leg sweeps back,
off it the joints fold (by each leg's fold signs) and it reaches forward."""

import math

from mathutils import Vector

from .geo import Part, Rand, blend, smooth
from .rig import Bone

X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)


class Quad:
    """spec: a dict of measurements in metres (the creature as built, k = 1):
    trunk [(x, top, bottom, half width)] front to back; neck [((tx, tz), (bx, bz), hw)]
    root to head; head_bone (start, end); legs {'f': [joints], 'h': [joints]} for the
    left side, 5 joints (4 bones) each; radii {'f': [(ru, rv)] * 5, 'h': ...};
    tail [points]; tail_r [radii]."""

    def __init__(self, spec, k=1.0, prefix='q_'):
        self.S, self.k, self.p = spec, k, prefix

    def P(self, x, y, z):
        return Vector((x, y, z)) * self.k

    def n(self, name):
        return self.p + name

    def w(self, *pairs):
        if len(pairs) == 1:
            return {self.p + pairs[0]: 1.0}
        return {self.p + pairs[i]: pairs[i + 1] for i in range(0, len(pairs), 2)}

    # ------------------------------------------------------------ skeleton

    def joint(self, leg, i, side):
        x, y, z = self.S['legs'][leg][i]
        return self.P(x, y * side, z)

    def bones(self):
        S, P, n = self.S, self.P, self.n
        tr = S['trunk']
        front, back = tr[0][0], tr[-1][0]
        mid_z = sum((r[1] + r[2]) / 2 for r in tr) / len(tr)
        sx = S.get('spine_x', 0.0)
        b = [Bone(n('root'), P(0, 0, 0), P(0, 0, 0.2), None, X),
             Bone(n('spine'), P(back * 0.7, 0, mid_z), P(sx, 0, mid_z), n('root'), Z),
             Bone(n('chest'), P(sx, 0, mid_z), P(front * 0.85, 0, mid_z + 0.02), n('spine'), Z)]
        nk = S['neck']
        mids = [Vector(((t[0] + bb[0]) / 2, 0, (t[1] + bb[1]) / 2)) for t, bb, _ in nk]
        h0, h1 = S['head_bone']
        nb = S.get('neck_bones', 2)
        pts = [mids[0].lerp(Vector((h0[0], 0, h0[1])), i / nb) for i in range(nb + 1)]
        parent = n('chest')
        for i in range(nb):
            b.append(Bone(n('neck%d' % i), P(*pts[i]), P(*pts[i + 1]), parent, Z))
            parent = n('neck%d' % i)
        b.append(Bone(n('head'), P(h0[0], 0, h0[1]), P(h1[0], 0, h1[1]), parent, Z))
        if 'jaw' in S:
            j0, j1 = S['jaw']
            b.append(Bone(n('jaw'), P(j0[0], 0, j0[1]), P(j1[0], 0, j1[1]), n('head'), Z))
        tl = S.get('tail')
        if tl:
            parent = n('spine')
            for i in range(len(tl) - 1):
                b.append(Bone(n('tail%d' % i), P(*tl[i]), P(*tl[i + 1]), parent, Z))
                parent = n('tail%d' % i)
        for side, sf in ((1, 'L'), (-1, 'R')):
            for leg, parent0 in (('f', 'chest'), ('h', 'spine')):
                parent = n(parent0)
                for i in range(4):
                    name = n('%s%d.%s' % (leg, i, sf))
                    b.append(Bone(name, self.joint(leg, i, side), self.joint(leg, i + 1, side), parent, X))
                    parent = name
        return b

    # ------------------------------------------------------------ body

    def trunk_ring(self, x):
        rows = self.S['trunk']
        if x >= rows[0][0]:
            return rows[0][1:]
        for a, b in zip(rows, rows[1:]):
            if b[0] <= x <= a[0]:
                t = (a[0] - x) / (a[0] - b[0])
                return tuple(a[i] + (b[i] - a[i]) * t for i in (1, 2, 3))
        return rows[-1][1:]

    def trunk_shape(self, th):
        return 1.0 + self.S.get('back_wide', 0.08) * math.cos(th) - 0.04 * math.cos(2 * th)

    def trunk_point(self, x, th, out=0.0):
        """th 0 at the top, + toward the left."""
        top, bot, hw = self.trunk_ring(x)
        kk = self.trunk_shape(th)
        c = Vector((x, 0, (top + bot) / 2))
        ru, rv = (top - bot) / 2, hw
        p = c + Vector((0, math.sin(th) * rv * kk, math.cos(th) * ru * kk))
        nrm = Vector((0, math.sin(th) / max(1e-6, rv), math.cos(th) / max(1e-6, ru))).normalized()
        return (p + nrm * out) * self.k, nrm

    def trunk_weights(self, x):
        sx = self.S.get('spine_x', 0.0)
        t = smooth(sx - 0.3, sx + 0.3, x)
        return self.w('spine', 1 - t, 'chest', t)

    def body(self, part, coat, belly=None):
        S = self.S
        rings = []
        for x, top, bot, hw in S['trunk']:
            rings.append({'c': self.P(x, 0, (top + bot) / 2), 'u': Vector((0, 0, 1)), 'v': Vector((0, 1, 0)),
                          'ru': (top - bot) / 2 * self.k, 'rv': hw * self.k, 'w': self.trunk_weights(x), 'shape': self.trunk_shape, 'dome': 0.6})
        mat = coat if not belly else (lambda j, s: belly if abs(s - 0.5) < 0.17 else coat)
        part.loft(rings, mat, n=24)
        self.neck(part, coat, belly)
        for side in (1, -1):
            for leg in ('f', 'h'):
                self.leg(part, leg, side, coat)

    def neck_weights(self, i):
        nb = self.S.get('neck_bones', 2)
        nk = len(self.S['neck'])
        t = i / (nk - 1) * nb
        a = int(min(nb - 1, math.floor(t)))
        fr = t - a
        wa = self.w('neck%d' % a)
        if i == 0:
            return blend(self.w('chest'), wa, 0.5)
        if i == nk - 1:
            return blend(self.w('neck%d' % (nb - 1)), self.w('head'), 0.5)
        return blend(wa, self.w('neck%d' % min(nb - 1, a + 1)), fr) if fr > 0 else wa

    def neck(self, part, coat, belly=None):
        rings = []
        rows = self.S['neck']
        for i, ((tx, tz), (bx, bz), hw) in enumerate(rows):
            top, bot = self.P(tx, 0, tz), self.P(bx, 0, bz)
            u = (top - bot).normalized()
            rings.append({'c': (top + bot) / 2, 'u': u, 'v': Vector((0, 1, 0)), 'ru': (top - bot).length / 2, 'rv': hw * self.k, 'w': self.neck_weights(i),
                          'shape': lambda th: 1.0 + 0.05 * math.cos(th)})
        mat = coat if not belly else (lambda j, s: belly if abs(s - 0.5) < 0.18 else coat)
        part.loft(rings, mat, n=18, cap0=False, cap1=self.S.get('neck_cap', False))

    def leg(self, part, leg, side, coat):
        sf = 'L' if side > 0 else 'R'
        n = lambda i: '%s%d.%s' % (leg, i, sf)
        J = [self.joint(leg, i, side) for i in range(5)]
        R = [(r[0] * self.k, r[1] * self.k) for r in self.S['radii'][leg]]
        parent = 'chest' if leg == 'f' else 'spine'
        pts, radii, ws = [], [], []
        for i in range(5):
            if i == 0:
                wj = blend(self.w(parent), self.w(n(0)), 0.6)
            elif i == 4:
                wj = self.w(n(3))
            else:
                wj = blend(self.w(n(i - 1)), self.w(n(i)), 0.5)
            pts.append(J[i])
            radii.append(R[i])
            ws.append(wj)
            if i < 4:
                pts.append(J[i].lerp(J[i + 1], 0.5))
                radii.append(((R[i][0] + R[i + 1][0]) / 2, (R[i][1] + R[i + 1][1]) / 2))
                ws.append(self.w(n(i)))
        mats = self.S.get('leg_mats', {}).get(leg)
        mat = (lambda j, s: mats[min(len(mats) - 1, j // 2)]) if mats else coat
        part.chain(pts, radii, mat, ws, hint=Vector((1, 0, 0)), n=12)
        # The upper leg's mass: shoulder or haunch.
        m = self.S.get('mass', {}).get(leg)
        if m:
            c, r = m
            part.ellipsoid(self.P(c[0], c[1] * side, c[2]), tuple(x * self.k for x in r), coat, blend(self.w(parent), self.w(n(0)), 0.5), seg=14, rings=8)

    # ------------------------------------------------------------ poses

    def leg_pose(self, p, leg, side, swing, folds):
        """swing: the upper bone forward (degrees); folds: the three lower joints, + folding."""
        signs = self.S['fold_signs'][leg]
        p.lr(self.n('%s0' % leg), side, Y, -swing)
        for i in range(3):
            p.lr(self.n('%s%d' % (leg, i + 1)), side, Y, signs[i] * folds[i])

    def gait(self, p, t, phases, stance=0.5, reach=(20, 18), lift=(60, 50), pitch=3.0, bob=0.03, upper_fold=None):
        """One stride at t in [0, 1): phases {(leg, side): phase}."""
        for (leg, side), ph in phases.items():
            q = (t - ph) % 1.0
            A = reach[0] if leg == 'f' else reach[1]
            F = lift[0] if leg == 'f' else lift[1]
            uf = (upper_fold or {}).get(leg, (0.6, 1.0, 0.7))
            if q < stance:
                u = q / stance
                swing = A * (1 - 2 * u)
                folds = [4 * math.sin(math.pi * u), 4 * math.sin(math.pi * u), -8 * math.sin(math.pi * u)]
            else:
                u = (q - stance) / (1 - stance)
                swing = A * (-1 + 2 * smooth(0.0, 0.9, u))
                f = math.sin(math.pi * min(1.0, u * 1.15)) ** 1.2
                folds = [F * uf[0] * f, F * uf[1] * f, F * uf[2] * f]
            self.leg_pose(p, leg, side, swing, folds)
        pt = pitch * math.sin(4 * math.pi * t)
        p.move(self.n('root'), (0, 0, bob * self.k * (0.5 + 0.5 * math.cos(4 * math.pi * t))))
        p.turn(self.n('spine'), Y, -pt)
        return p


def walk_phases():
    """A four-beat walk: hind left, fore left, hind right, fore right."""
    return {('h', 1): 0.0, ('f', 1): 0.25, ('h', -1): 0.5, ('f', -1): 0.75}


def bound_phases():
    """A running bound (wolves, lions): the hinds together, then the fores."""
    return {('h', 1): 0.0, ('h', -1): 0.06, ('f', 1): 0.45, ('f', -1): 0.53}


class Adapter:
    """What wings.Wing and batwing.BatWing need of a body (a scale and bone names) for a beast."""

    def __init__(self, q, s=1.0):
        self.q, self.s = q, s

    def n(self, name):
        return self.q.n(name)
