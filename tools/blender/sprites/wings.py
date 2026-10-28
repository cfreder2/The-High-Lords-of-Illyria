"""Feathered wings: a bird's wing skeleton (humerus, forearm, hand) with a bone
for every feather, so the wing fans open and folds shut as a real one does.

Layers, from the trailing edge in: primaries on the hand, secondaries on the
forearm, tertials by the body, then rows of greater, median and lesser
coverts over them, an alula at the wrist, and a feathered leading edge over
the bones. Each feather is a cambered, tapering vane bent along its length.

At rest the wing lies flat and level, its bones in a line out to the side;
angles in the wing's plane are measured from straight back (0) toward
outward (90) and forward (180). A wing's pose gives each bone and feather
the in-plane angle it should point at; rig rotations are worked out from
those, and the whole plane is then turned at the root (lift: about the
figure's forward axis; -90 hangs the wing down the back).
"""

import math

from mathutils import Vector

from .geo import Rand, blend, smooth
from .rig import Bone

Z = (0, 0, 1)


class Wing:
    def __init__(self, B, side, root, scale=1.0, parent='chest', mat='feather', edge_mat='feather'):
        self.B, self.side, self.scale, self.parent = B, side, scale * B.s, B.n(parent)
        self.sf = 'L' if side > 0 else 'R'
        self.root = Vector(root)
        s = self.scale
        self.len = {'arm': 0.31 * s, 'fore': 0.38 * s, 'hand': 0.24 * s}
        self.mat, self.edge_mat = mat, edge_mat
        self.feathers = []  # (bone, parent bone, rest angle, kind, index)

    def n(self, name):
        return self.B.n('wing_%s.%s' % (name, self.sf))

    def at(self, bone, t):
        """A point a fraction t along a wing bone at rest."""
        y0 = 0.02 * self.scale
        if bone in ('fore', 'hand'):
            y0 += self.len['arm']
        if bone == 'hand':
            y0 += self.len['fore']
        return self.root + Vector((0, self.side * (y0 + self.len[bone] * t), 0))

    def bones(self):
        side, s = self.side, self.scale
        out = [Bone(self.n('root'), self.root, self.root + Vector((0, side * 0.02 * s, 0)), self.parent, Z)]
        prev = self.n('root')
        for b in ('arm', 'fore', 'hand'):
            out.append(Bone(self.n(b), self.at(b, 0), self.at(b, 1), prev, Z))
            prev = self.n(b)
        return out

    def _dir(self, phi):
        """In-plane direction at rest: phi from straight back toward outward."""
        a = math.radians(phi)
        return Vector((-math.cos(a), self.side * math.sin(a), 0))

    def feather(self, part, bones, kind, k, bone, t, phi, length, width, lift, outer=0.5, curve=0.06, seg=(7, 4)):
        """One feather rooted a fraction t along a wing bone, pointing at phi,
        raised lift above the wing's plane (its layer)."""
        s = self.scale
        name = self.B.n('f_%s%d.%s' % (kind, k, self.sf))
        root = self.at(bone, t) + Vector((0, 0, lift * s))
        d = self._dir(phi)
        across = Vector((0, 0, 1)).cross(d) * -self.side  # toward the outer vane (phi increasing)
        L, Wd = length * s, width * s
        bones.append(Bone(name, root, root + d * L, self.n(bone), Z))
        self.feathers.append((name, self.n(bone), phi, kind, k))
        w = {name: 1.0}
        up = Vector((0, 0, 1))

        def fn(a, b):
            # a: along the feather (0 root, 1 tip); b: across, 0 trailing edge to 1 leading edge.
            prof = math.sin(math.pi * min(1.0, 0.12 + a * 0.95)) ** 0.6 if a < 0.75 else math.cos((a - 0.75) / 0.25 * math.pi / 2) ** 0.7
            prof = max(0.04, prof) * (0.35 + 0.65 * smooth(0.0, 0.35, a))
            x = (b - (1 - outer)) * 2  # -1..+1 with the shaft at 0
            half = Wd * (outer if x > 0 else (1 - outer)) * prof
            p = root + d * (L * a) + across * (x * half)
            # Cambered across, curving down toward the tip.
            p += up * (0.006 * s * (1 - x * x) * prof - curve * L * a * a)
            return p
        part.grid(fn, seg[0], seg[1], self.mat, w)

    def build(self, part, bones, edge):
        """All the feathers into part, the leading edge into edge; bones appended."""
        s = self.scale
        r = Rand(97 if self.side > 0 else 98)
        j = lambda a: a * r.range(0.96, 1.04)
        for k in range(10):  # primaries, inner to outer
            t = 0.08 + 0.92 * k / 9
            self.feather(part, bones, 'p', k, 'hand', t, 22 + 66 * (k / 9) ** 0.9, j(0.4 + 0.12 * math.sin(math.pi * 0.85 * (k + 1) / 10)), 0.065, -0.002 * k, outer=0.35, curve=0.05)
        for k in range(12):  # secondaries
            t = 0.04 + 0.96 * k / 11
            self.feather(part, bones, 's', k, 'fore', t, 4 + 12 * k / 11, j(0.34), 0.075, 0.0016 * (11 - k), outer=0.45, curve=0.04)
        for k in range(4):  # tertials
            self.feather(part, bones, 't', k, 'arm', 0.35 + 0.6 * k / 3, -6 + 3 * k, j(0.3 - 0.03 * k), 0.08, 0.02, outer=0.5, curve=0.03)
        for k in range(16):  # greater coverts over the flights
            if k < 10:
                bone, t, phi = 'fore', 0.05 + 0.95 * k / 9, 8 + 10 * k / 9
            else:
                bone, t, phi = 'hand', 0.05 + 0.9 * (k - 10) / 5, 25 + 45 * (k - 10) / 5
            self.feather(part, bones, 'g', k, bone, t, phi, j(0.17 if bone == 'fore' else 0.16), 0.06, 0.03, outer=0.5, curve=0.03, seg=(5, 3))
        for k in range(14):  # median coverts
            bone, t = ('arm', 0.3 + 0.7 * k / 3) if k < 4 else (('fore', (k - 4) / 6) if k < 11 else ('hand', 0.1 + 0.6 * (k - 11) / 2))
            phi = 10 + 4 * k
            self.feather(part, bones, 'm', k, bone, t, min(phi, 60), j(0.1), 0.05, 0.042, outer=0.5, curve=0.02, seg=(4, 3))
        for k in range(3):  # alula
            self.feather(part, bones, 'a', k, 'hand', 0.02 + 0.05 * k, 120 + 12 * k, j(0.12 - 0.02 * k), 0.03, 0.05, outer=0.4, curve=0.02, seg=(4, 2))
        # The leading edge: a flattened tube along the bones, with the lesser coverts.
        pts, radii, ws = [], [], []
        for b, n in (('arm', 4), ('fore', 5), ('hand', 4)):
            for i in range(n):
                t = i / n
                pts.append(self.at(b, t) + Vector((-0.03 * s, 0, 0.03 * s)))
                k = len(pts)
                rr = (0.055 - 0.0028 * k) * s
                radii.append((rr * 0.45, rr))
                w = {self.n(b): 1.0}
                if i == 0 and b != 'arm':
                    w = blend({self.n(b): 1.0}, {self.n('arm' if b == 'fore' else 'fore'): 1.0}, 0.5)
                ws.append(w)
        pts.append(self.at('hand', 1.0) + Vector((-0.03 * s, 0, 0.02 * s)))
        radii.append((0.008 * s, 0.012 * s))
        ws.append({self.n('hand'): 1.0})
        rv = [rr[1] for rr in radii]
        edge.chain(pts, [(rr[0], rr[1]) for rr in radii], self.edge_mat, ws, hint=Vector((0, 0, 1)), n=10)
        # The root, where the wing meets the back.
        edge.ellipsoid(self.root + Vector((-0.01 * s, self.side * 0.04 * s, 0.02 * s)), (0.07 * s, 0.07 * s, 0.06 * s), self.edge_mat, {self.n('root'): 0.5, self.n('arm'): 0.5}, seg=10, rings=6)

    # ---------------------------------------------------------------- posing

    def pose(self, p, fold, lift, sweep=0.0, twist=0.0, spread_bias=0.0, ripple=0.0):
        """fold: 0 spread, 1 folded shut (hanging); lift: degrees about the
        forward axis (0 level, + up, -90 hanging down the back); sweep: back
        toward the spine; twist: the leading edge up."""
        side = self.side
        # Bones' in-plane angles, spread and folded (folded: the plane hangs, so 90 is down).
        spread = {'arm': 82.0, 'fore': 100.0 + spread_bias, 'hand': 88.0 + spread_bias * 0.5}
        folded = {'arm': -82.0, 'fore': 62.0, 'hand': 88.0}
        A = {b: spread[b] + (folded[b] - spread[b]) * fold for b in spread}
        # The plane, turned at the root (as for the left wing; Pose.lr mirrors).
        root = self.B.n('wing_root')
        p.lr(root, side, (0, 1, 0), twist)
        p.lr(root, side, (1, 0, 0), lift)
        p.lr(root, side, (0, 0, 1), sweep)
        # In-plane turns: phi_now = phi_rest - (sum of turns), positive about +z for the left wing.
        prev = 90.0
        for b in ('arm', 'fore', 'hand'):
            total = 90.0 - A[b]
            p.lr(self.B.n('wing_' + b), side, Z, total - (90.0 - prev))
            prev = A[b]
        for name, parent, phi, kind, k in self.feathers:
            pb = parent[len(self.B.n('wing_')):-2]
            tgt_spread = phi
            if kind == 'p':
                tgt_fold = 88 + 0.8 * k
            elif kind == 's':
                tgt_fold = 80 + 0.8 * k
            elif kind == 't':
                tgt_fold = 70 + 3 * k
            elif kind == 'g':
                tgt_fold = 78 + 1.0 * k if k < 10 else 86 + 1.0 * (k - 10)
            elif kind == 'm':
                tgt_fold = 70 + 1.5 * k
            else:
                tgt_fold = 70 + 5 * k
            tgt = tgt_spread + (tgt_fold - tgt_spread) * fold
            if ripple:
                tgt += ripple * math.sin(k * 0.9 + (0 if kind == 'p' else 1.7))
            # This feather's own turn, relative to its bone.
            own = (phi - tgt) - (90.0 - A[pb])
            name_l = name[:-2]
            p.lr(name_l, side, Z, own)
