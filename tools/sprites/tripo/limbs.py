"""No arm inside its own body: the body checked against itself on every frame, with the model's own thickness.

The skin is one mesh, so each arm is taken apart from the rest by its weights. An arm's test points are the
vertices its forearm, hand and fingers hold (and its upper arm's beyond half way from the shoulder: nearer, an arm
is joined to the trunk); the rest of him (trunk, head, legs, the other arm), as he is deformed that frame, is the
BVH tree they are tested against. So the chest's depth is the berserker's own, not a bone line's. A point is in
when the nearest skin faces away from it and two of three rays agree (clearance.depth_of); TOLERANCE allows an arm
lying against his side and hands together.

What is in is turned out about the shoulder by the least angle that clears: first the elbow swung about the line
from shoulder to wrist (the hand stays on its hilt), then about the axis the points' way out gives, then six
others; the hand keeps its own turn; the turns are spread over the neighbouring frames. clearance.settle runs after.

    rows = limbs.settle(rig, body, action, scene)        # one row per arm, as clearance.settle's
"""
import math

import bpy
from mathutils import Quaternion, Vector
from mathutils.bvhtree import BVHTree

from keyer import B
from clearance import depth_of, rotvec

TOLERANCE = 0.025      # metres under the skin, for a man 1.8 m tall


class Limbs:
    def __init__(self, rig, body, scale):
        self.rig, self.body, self.tol, self.scale = rig, body, TOLERANCE * scale, scale
        names = {g.index: g.name.split(':')[-1] for g in body.vertex_groups}
        me = body.data
        dom = []
        for v in me.vertices:
            g = max(v.groups, key=lambda g: g.weight, default=None)
            dom.append(names[g.group] if g is not None else '')
        self.dom = dom
        self.upper = [n in ('LeftArm', 'RightArm') for n in dom]
        self.polys = [tuple(p.vertices) for p in me.polygons]
        self.sides = {}
        mw = body.matrix_world
        for side in ('Left', 'Right'):
            mine = [n.startswith((side + 'Shoulder', side + 'Arm', side + 'ForeArm', side + 'Hand')) for n in dom]
            rest = [i for i, p in enumerate(self.polys) if not any(mine[v] for v in p)]
            sh, el = rig.rest_head[side + 'Arm'], rig.rest_head[side + 'ForeArm']
            pts = []
            for i, n in enumerate(dom):
                if n.startswith((side + 'ForeArm', side + 'Hand')):
                    pts.append(i)
                elif n.startswith(side + 'Arm'):
                    p = mw @ me.vertices[i].co
                    if (p - sh).dot(el - sh) / (el - sh).length_squared > 0.5:
                        pts.append(i)
            self.sides[side] = (rest, pts[::max(1, len(pts) // 260)])

    def frame(self):
        dg = bpy.context.evaluated_depsgraph_get()
        ev = self.body.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        self.co = [mw @ v.co for v in me.vertices]
        ev.to_mesh_clear()
        self.trees = {side: BVHTree.FromPolygons(self.co, [self.polys[i] for i in rest], epsilon=0.0)
                      for side, (rest, pts) in self.sides.items()}

    def worst(self, side, turn=None, pivot=None):
        """(depth, [(point, way out, depth, vertex)]) of this arm in the rest of him."""
        tree = self.trees[side]
        bad, deep = [], 0.0
        for i in self.sides[side][1]:
            p = self.co[i]
            if turn is not None:
                p = pivot + turn @ (p - pivot)
            d, n = depth_of(tree, p, 0.5 * self.scale)
            # An upper arm lies against the side of a broad back: half as much again is allowed it.
            if d > (self.tol * 1.5 if self.upper[i] else self.tol):
                bad.append((p, n, d, i))
            deep = max(deep, d)
        return deep, bad

    def into(self, side, bad):
        """Which part of the arm is in what (by the nearest skin's bone)."""
        tree, rest = self.trees[side], self.sides[side][0]
        there, part = {}, {}
        for p, n, d, i in bad:
            name = self.dom[self.polys[rest[tree.find_nearest(p)[2]]][0]]
            there[name] = there.get(name, 0) + 1
            a = self.dom[i].replace(side, '')[:7]
            part[a] = part.get(a, 0) + 1
        top = lambda d: ', '.join('%s %d' % kv for kv in sorted(d.items(), key=lambda kv: -kv[1])[:2])
        return '%s in %s' % (top(part), top(there))


def settle(rig, body, act, scene, fix=True):
    arm = rig.arm
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act
    if getattr(ad, 'action_slot', None) is None and getattr(act, 'slots', None):
        ad.action_slot = act.slots[0]
    scale = rig.rest_head['Hips'].z / 1.04
    L = Limbs(rig, body, scale)
    a, b = (int(x) for x in act.frame_range)
    frames = list(range(a, b + 1))
    SIDES = ('Left', 'Right')

    def at(f):
        scene.frame_set(f)
        rig.W = arm.matrix_world.copy()
        rig.Wi = rig.W.inverted()
        L.frame()

    def way_out(side, bad):
        pivot = rig.head(side + 'Arm')
        c = sum((x[0] for x in bad), Vector()) / len(bad)
        out = sum((x[1] * x[2] for x in bad), Vector())
        # First the elbow swung out about the line from shoulder to wrist: the hand stays where it is (on a hilt,
        # on a shaft), only the elbow moves. Then the way the points themselves want to go, then the rest.
        swivel = (rig.head(side + 'Hand') - pivot).normalized()
        axes = [swivel, -swivel]
        ax = (c - pivot).cross(out)
        if ax.length > 1e-6:
            axes.append(ax.normalized())
        along = (rig.head(side + 'ForeArm') - pivot).normalized()
        o = along.orthogonal().normalized()
        axes += [Quaternion(along, math.pi * i / 3) @ o for i in range(6)]
        best = (None, 0.0)
        for ax in axes:
            for deg in range(3, 61, 3):
                if best[0] is not None and deg >= best[1]:
                    break
                q = Quaternion(ax, math.radians(deg))
                if L.worst(side, q, pivot)[0] <= L.tol * 0.7:
                    best = (q, float(deg))
                    break
        return best[0]

    def turn_arm(side, v, f):
        if v.length < math.radians(0.3):
            return
        hand = rig.rot(side + 'Hand')
        rig.set_rot(side + 'Arm', Quaternion(v.normalized(), v.length) @ rig.rot(side + 'Arm'))
        rig.set_rot(side + 'Hand', hand)      # the hand keeps its own turn: what it holds does not swing with the elbow
        for n in ('Arm', 'Hand'):
            rig.P[B + side + n].keyframe_insert('rotation_quaternion', frame=f)

    before = {s: {} for s in SIDES}
    inb = {s: {} for s in SIDES}     # frames in which a point is deeper than it is allowed
    what = {s: '' for s in SIDES}
    turns = {s: {} for s in SIDES}
    for f in frames:
        at(f)
        for side in SIDES:
            deep, bad = L.worst(side)
            if bad and deep >= max(before[side].values(), default=0.0):
                what[side] = L.into(side, bad)
            before[side][f] = deep
            inb[side][f] = bool(bad)
            if fix and bad:
                q = way_out(side, bad)
                if q is not None:
                    turns[side][f] = rotvec(q)
    after = {s: dict(before[s]) for s in SIDES}
    ina = {s: dict(inb[s]) for s in SIDES}
    if fix and any(turns.values()):
        for side in SIDES:
            t = turns[side]
            if not t:
                continue
            for f in frames:
                acc, wsum = Vector(), 0.0
                for g, w in ((f - 2, 1), (f - 1, 2), (f, 3), (f + 1, 2), (f + 2, 1)):
                    if a <= g <= b:
                        acc += t.get(g, Vector()) * w
                        wsum += w
                v = acc / wsum
                if f in t and v.length < t[f].length:
                    v = t[f]
                if v.length > 1e-4:
                    at(f)
                    turn_arm(side, v, f)
        what = {s: '' for s in SIDES}
        for _ in range(4):
            left = 0
            for f in frames:
                at(f)
                for side in SIDES:
                    deep, bad = L.worst(side)
                    if bad:
                        q = way_out(side, bad)
                        if q is not None:
                            turn_arm(side, rotvec(q), f)
                            at(f)
                            deep, bad = L.worst(side)
                    after[side][f] = deep
                    ina[side][f] = bool(bad)
                    left += bool(bad)
                    if bad:
                        what[side] = L.into(side, bad)
            if not left:
                break
    rows = []
    for side in SIDES:
        bf, af = before[side], after[side]
        wb, wa = max(bf, key=bf.get), max(af, key=af.get)
        rows.append({'clip': act.name, 'item': side.lower() + ' arm', 'tolerance_cm': round(L.tol * 100, 1),
                     'before_cm': round(bf[wb] * 100, 1), 'before_frame': wb, 'frames_in': sum(inb[side].values()),
                     'after_cm': round(af[wa] * 100, 1), 'after_frame': wa, 'frames_left': sum(ina[side].values()),
                     'frames': len(frames), 'turned': len(turns[side]),
                     'where': what[side] if (sum(ina[side].values()) or (not fix and sum(inb[side].values()))) else ''})
    ad.action = None
    return rows
