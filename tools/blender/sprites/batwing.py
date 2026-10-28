"""Bat wings (demons, dragons): an arm, a forearm and four long fingers, the
membrane stretched between them and back to the body, scalloped between the
finger tips; a claw at the thumb.

As in wings.py, the wing lies flat and level at rest, out to the side; angles
in its plane run from straight back (0) through outward (90) to forward
(180). A pose gives each bone its in-plane angle (spread or folded) and turns
the whole plane at the root."""

import math

from mathutils import Vector

from .geo import blend, smooth
from .rig import Bone

Z = (0, 0, 1)

SPREAD = {'arm': 92.0, 'fore': 118.0, 'f0': 128.0, 'f1': 95.0, 'f2': 62.0, 'f3': 32.0}
FOLDED = {'arm': -60.0, 'fore': 150.0, 'f0': -10.0, 'f1': -18.0, 'f2': -24.0, 'f3': -30.0}
FINGERS = ('f0', 'f1', 'f2', 'f3')


class BatWing:
    def __init__(self, prefix, side, root, span=1.6, parent='chest', body_attach=None, membrane='membrane', bone_mat=None):
        """span: the root to the longest finger's tip, spread; body_attach: the
        point on the flank the membrane's trailing edge runs back to."""
        self.p, self.side, self.parent = prefix, side, prefix + parent
        self.sf = 'L' if side > 0 else 'R'
        self.root = Vector(root)
        s = span / 1.6
        self.s = s
        self.len = {'arm': 0.4 * s, 'fore': 0.5 * s, 'f0': 0.2 * s, 'f1': 0.85 * s, 'f2': 0.75 * s, 'f3': 0.6 * s}
        self.attach = Vector(body_attach) if body_attach is not None else self.root + Vector((-0.6 * s, 0, -0.2 * s))
        self.mat, self.bone_mat = membrane, bone_mat or membrane

    def n(self, name):
        return '%sbw_%s.%s' % (self.p, name, self.sf)

    def _dir(self, phi):
        a = math.radians(phi)
        return Vector((-math.cos(a), self.side * math.sin(a), 0))

    def joints(self):
        """Rest positions: root, shoulder, elbow, wrist; finger tips."""
        sh = self.root + Vector((0, self.side * 0.03 * self.s, 0))
        el = sh + self._dir(90) * self.len['arm']
        wr = el + self._dir(90) * self.len['fore']
        tips = {f: wr + self._dir(SPREAD[f] - SPREAD['fore'] + 90) * self.len[f] for f in FINGERS}
        return sh, el, wr, tips

    def bones(self):
        sh, el, wr, tips = self.joints()
        out = [Bone(self.n('root'), self.root, sh, self.parent, Z),
               Bone(self.n('arm'), sh, el, self.n('root'), Z),
               Bone(self.n('fore'), el, wr, self.n('arm'), Z)]
        for f in FINGERS:
            out.append(Bone(self.n(f), wr, tips[f], self.n('fore'), Z))
        return out

    def build(self, part, bones_part):
        """The membrane into part; the arm and finger bones (thicker, darker) into bones_part."""
        sh, el, wr, tips = self.joints()
        s = self.s
        w = lambda b: {self.n(b): 1.0}
        # Bones of the wing.
        bones_part.chain([sh, el], [0.05 * s, 0.035 * s], self.bone_mat, [blend(w('root'), w('arm'), 0.5), blend(w('arm'), w('fore'), 0.5)], hint=Vector((0, 0, 1)), n=8)
        bones_part.chain([el, wr], [0.035 * s, 0.03 * s], self.bone_mat, [blend(w('arm'), w('fore'), 0.5), w('fore')], hint=Vector((0, 0, 1)), n=8)
        for f in FINGERS:
            bones_part.chain([wr, wr.lerp(tips[f], 0.5), tips[f]], [0.022 * s, 0.014 * s, 0.004 * s], self.bone_mat, [w('fore'), w(f), w(f)], hint=Vector((0, 0, 1)), n=6)
        # The thumb's claw.
        c = tips['f0']
        bones_part.chain([c, c + self._dir(150) * 0.08 * s + Vector((0, 0, -0.03 * s))], [0.014 * s, 0.002 * s], 'horn_dark', [w('f0'), w('f0')], hint=Vector((0, 0, 1)), n=6)
        # Membrane panels: f1-f2, f2-f3, f3 to the flank (via the arm).
        def edge(f, t):
            return wr.lerp(tips[f], t)

        def panel(a_pts, b_pts, wa, wb, scallop=0.12):
            nu, nv = len(a_pts) - 1, 6

            def fn(u, v):
                i = u * nu
                k = min(nu - 1, int(i))
                fr = i - k
                pa = a_pts[k].lerp(a_pts[k + 1], fr)
                pb = b_pts[k].lerp(b_pts[k + 1], fr)
                p = pa.lerp(pb, v)
                # Scalloped trailing edge: the outer row drawn in between the tips.
                if u > 0.999:
                    pass
                return p

            def ww(u, v):
                i = u * nu
                k = min(nu - 1, int(i))
                fr = i - k
                a = blend(wa[k], wa[k + 1], fr)
                b = blend(wb[k], wb[k + 1], fr)
                return blend(a, b, v)
            first = part.grid(fn, nu, nv, self.mat, ww)
            # The scallop: pull the free edge's middle back toward the wrist.
            cols = nu + 1
            for j in range(nv + 1):
                v = j / nv
                k = first + j * cols + nu
                pull = scallop * math.sin(math.pi * v)
                part.verts[k] = part.verts[k].lerp(wr, pull)
            return first
        steps = [0.0, 0.25, 0.5, 0.75, 1.0]
        for fa, fb in (('f1', 'f2'), ('f2', 'f3')):
            panel([edge(fa, t) for t in steps], [edge(fb, t) for t in steps], [w('fore')] + [w(fa)] * 4, [w('fore')] + [w(fb)] * 4)
        # f3 back to the body: along the arm to the flank.
        arm_pts = [wr, wr.lerp(el, 0.5), el, el.lerp(sh, 0.5), sh]
        flank = [tips['f3'], tips['f3'].lerp(self.attach, 0.35), tips['f3'].lerp(self.attach, 0.65), self.attach.lerp(sh, 0.3), self.attach]
        aw = [w('fore'), w('fore'), blend(w('arm'), w('fore'), 0.5), w('arm'), w('root')]
        fw = [w('f3'), blend(w('f3'), w('arm'), 0.5), w('arm'), w('root'), {self.parent: 1.0}]
        panel(arm_pts, flank, aw, fw, scallop=0.05)
        # The leading edge: forearm to f0, f0 to f1.
        panel([edge('f0', t) for t in steps], [edge('f1', t * 0.3) for t in steps], [w('fore')] + [w('f0')] * 4, [w('fore')] + [w('f1')] * 4, scallop=0.0)

    def pose(self, p, fold, lift, sweep=0.0, twist=0.0, flex=0.0):
        """fold 0 spread .. 1 folded against the body; lift about the forward
        axis (+ up); sweep back; twist: leading edge up; flex: fingers curled down."""
        side = self.side
        A = {b: SPREAD[b] + (FOLDED[b] - SPREAD[b]) * fold for b in SPREAD}
        p.lr(self.n('root')[:-2], side, (0, 1, 0), twist)
        p.lr(self.n('root')[:-2], side, (1, 0, 0), lift)
        p.lr(self.n('root')[:-2], side, (0, 0, 1), sweep)
        prev = 90.0
        for b in ('arm', 'fore'):
            p.lr(self.n(b)[:-2], side, Z, (90.0 - A[b]) - (90.0 - prev))
            prev = A[b]
        for f in FINGERS:
            rest = SPREAD[f] - SPREAD['fore'] + 90.0  # the finger's rest angle in the plane
            # Finger's absolute angle wanted A[f]; its parent (fore) now points at A['fore'].
            p.lr(self.n(f)[:-2], side, Z, (rest - A[f]) - (90.0 - A['fore']))
            if flex:
                p.lr(self.n(f)[:-2], side, (1, 0, 0), -flex)
