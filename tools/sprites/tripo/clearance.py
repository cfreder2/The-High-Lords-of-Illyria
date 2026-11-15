"""No weapon through its bearer: the check and the solver, run on every frame of every clip before a unit is drawn.

The check. Each frame the body's skin (every body in the scene: the rider's and the mount's, capes and kilts and
all) is taken as it is deformed and put in a BVH tree. Each held thing is a cloud of points in its own space: its
mesh's vertices (thinned to a few hundred: they lie along a blade's edges, a shaft's sides, a shield's rim and
face) and its long axis from butt to point. A point is inside the body when the nearest skin faces away from it
and rays from it in three directions each cross the skin an odd number of times (Tripo's skins have holes: either
test alone gives false alarms); how deep is how far it is from the skin. Points in the hand that holds the thing
are left out (a sphere round each gripping fist; for a strapped shield, round the forearm and hand that carry it).
BVHTree.overlap of the thing's own faces with the skin's is counted too, as a second witness.

The solver. A frame whose worst point is deeper than the tolerance is turned clear about the grip: the offending
points' way out (the skin's normals there) and their lever from the fist give an axis; the hand turns about it by
the least angle that clears, and what the wrist may not give (WRIST degrees) the shoulder gives, the whole arm
turning about the same axis. If that way does not clear, eight other axes round the weapon are tried. A second
hand on the weapon is put back on it by IK at the same place along the handle. The turns are then spread over the
neighbouring frames (widened, then smoothed) so nothing pops, applied to the keys, and every frame checked again.

    report = clearance.settle(rig, bodies, items, action, scene)       # dicts: before, after, frames turned
"""
import math

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree

import keyer
from keyer import B

TOLERANCE = 0.010      # metres a point may be under the skin, for a man 1.8 m tall (scaled by the figure's size)
SHIELD_TOLERANCE = 0.03
WRIST = 40.0           # degrees the solver may turn a hand at the wrist before the arm must give
WRIST_MOST = 78.0      # and the most a wrist may be bent from straight on the forearm when it has
RAYS = (Vector((0.3, 0.2, 0.93)).normalized(), Vector((0.9, -0.4, 0.1)).normalized(), Vector((-0.2, 0.95, -0.2)).normalized())


OWNERS = []   # of the last tree built: per face, (body, a vertex of it), to say which part of him a point is in


def skin_tree(bodies):
    dg = bpy.context.evaluated_depsgraph_get()
    verts, polys = [], []
    del OWNERS[:]
    for ob in bodies:
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        base = len(verts)
        verts.extend(mw @ v.co for v in me.vertices)
        polys.extend(tuple(base + i for i in p.vertices) for p in me.polygons)
        OWNERS.extend((ob, p.vertices[0]) for p in me.polygons)
        ev.to_mesh_clear()
    return BVHTree.FromPolygons(verts, polys, epsilon=0.0)


def cloud(ob, n=360):
    """The thing's test points in its own space: thinned vertices and its long axis."""
    co = np.empty(len(ob.data.vertices) * 3)
    ob.data.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    step = max(1, len(co) // n)
    pts = [Vector(c) for c in co[::step]]
    lo, hi = co.min(0), co.max(0)
    long_axis = int(np.argmax(hi - lo))
    mid = (lo + hi) / 2
    for i in range(41):
        p = mid.copy()
        p[long_axis] = lo[long_axis] + (hi[long_axis] - lo[long_axis]) * i / 40
        pts.append(Vector(p))
    return pts


def crossings(tree, p, d):
    n, o = 0, p
    for _ in range(16):
        hit = tree.ray_cast(o, d)
        if hit[0] is None:
            break
        n += 1
        o = hit[0] + d * 1e-4
    return n


def depth_of(tree, p, reach):
    """How far under the skin p is (0 outside), and the way out."""
    loc, normal, _, dist = tree.find_nearest(p, reach)
    if loc is None or dist < 1e-5 or (p - loc).dot(normal) >= 0:
        return 0.0, None
    if sum(crossings(tree, p, d) % 2 for d in RAYS) < 2:
        return 0.0, None
    return dist, normal


class Held:
    """One held thing in one frame: its points in the world, what is left out, and how to turn it."""

    def __init__(self, rig, ob, scale):
        import json
        self.rig, self.ob = rig, ob
        self.it = json.loads(ob['tripo_item'])
        self.strap = self.it.get('hold') == 'strap'
        self.hangs = self.it.get('hold', 'hang') == 'hang'   # a sack on a cord: checked, not turned (the hand does not steer it)
        self.side = 'Left' if self.strap else self.it.get('hand', 'Right')
        self.local = cloud(ob)
        self.scale = scale
        self.tol = (SHIELD_TOLERANCE if self.strap or self.hangs else TOLERANCE) * scale

    def frame(self):
        rig = self.rig
        mw = self.ob.matrix_world
        self.pts = [mw @ p for p in self.local]
        self.pivot = rig.head('LeftArm') if self.strap else rig.fist(self.side)
        k = self.scale
        self.spheres = []
        if self.strap:
            a, b = rig.head('LeftForeArm'), rig.head('LeftHand')
            self.spheres += [(a + (b - a) * t, 0.17 * k) for t in (0.0, 0.33, 0.66, 1.0)] + [(rig.fist('Left'), 0.14 * k)]
        else:
            self.spheres.append((rig.fist(self.side), 0.15 * k))
            self.spheres.append((rig.head(self.side + 'Hand'), 0.10 * k))
            other = 'Left' if self.side == 'Right' else 'Right'
            fo = rig.fist(other)
            # A second hand on it: its fist lies on the thing.
            self.second = min((p - fo).length for p in self.pts) < 0.07 * k
            if self.second:
                self.spheres.append((fo, 0.15 * k))
                self.other = other

    def worst(self, tree, turn=None):
        """(depth, [(point, way out, depth)]) with the thing turned by `turn` (a Quaternion) about its pivot."""
        bad, deep = [], 0.0
        for p in self.pts:
            if turn is not None:
                p = self.pivot + turn @ (p - self.pivot)
            if any((p - c).length < r for c, r in self.spheres):
                continue
            d, n = depth_of(tree, p, 0.6 * self.scale)
            if d > self.tol:
                bad.append((p, n, d))
            deep = max(deep, d)
        return deep, bad

    def way_clear(self, tree, bad):
        """The least turn about the pivot that clears it: (Quaternion, degrees), or (None, 0)."""
        c = sum((b[0] for b in bad), Vector()) / len(bad)
        out = sum((b[1] * b[2] for b in bad), Vector())
        lever = c - self.pivot
        axes = []
        if lever.length > 1e-4 and out.length > 1e-6:
            ax = lever.cross(out)
            if ax.length > 1e-6:
                axes.append(ax.normalized())
        long_dir = (self.pts[-1] - self.pts[-41]).normalized()
        side = long_dir.orthogonal().normalized()
        for i in range(8):
            axes.append(Quaternion(long_dir, math.pi * i / 4) @ side)
        best = (None, 0.0)
        for ax in axes:
            for deg in range(3, 91, 3):
                if best[0] is not None and deg >= best[1]:
                    break
                q = Quaternion(ax, math.radians(deg))
                if self.worst(tree, q)[0] <= self.tol * 0.6:
                    best = (q, float(deg))
                    break
            if best[0] is not None and best[1] <= 6:
                break
        return best


def rotvec(q):
    ax, ang = q.to_axis_angle()
    return Vector(ax) * ang


def from_rotvec(v):
    return Quaternion(v.normalized(), v.length) if v.length > 1e-7 else Quaternion()


def apply_turn(rig, held, v, frame):
    """Turn the held thing by rotation vector v about its pivot at this frame and key the bones moved."""
    ang = math.degrees(v.length)
    if ang < 0.3:
        return
    axis = v.normalized()
    P = rig.P
    side = held.side
    two = (not held.strap) and getattr(held, 'second', False)
    if two:
        other = held.other
        inv = held.ob.matrix_world.inverted()
        fist_local = inv @ rig.fist(other)
        wrist_local = inv @ rig.head(other + 'Hand')
        rel = held.ob.matrix_world.to_quaternion().inverted() @ rig.rot(other + 'Hand')
        elbow = rig.head(other + 'ForeArm')
    moved = []
    if held.strap:
        # A shield turns with the whole arm about the shoulder.
        rig.set_rot('LeftArm', Quaternion(axis, v.length) @ rig.rot('LeftArm'))
        moved += ['LeftArm']
    else:
        straight = rig.rot(side + 'ForeArm') @ (rig.rest_rot[side + 'ForeArm'].inverted() @ rig.rest_rot[side + 'Hand'])
        bent = math.degrees(straight.rotation_difference(rig.rot(side + 'Hand')).angle)
        bent = min(bent, 360.0 - bent)
        wrist = min(ang, WRIST, max(0.0, WRIST_MOST - bent))   # a wrist already bent has less to give
        rest = ang - wrist
        if rest > 0.3:
            rig.set_rot(side + 'Arm', Quaternion(axis, math.radians(rest)) @ rig.rot(side + 'Arm'))
            moved.append(side + 'Arm')
        rig.set_rot(side + 'Hand', Quaternion(axis, math.radians(wrist)) @ rig.rot(side + 'Hand'))
        moved.append(side + 'Hand')
    if two:
        mw = held.ob.matrix_world
        la, lb = rig.lengths[other][:2]
        rig.two_bone(other + 'Arm', other + 'ForeArm', other + 'Hand', mw @ wrist_local, elbow, la, lb)
        rig.set_rot(other + 'Hand', mw.to_quaternion() @ rel)
        moved += [other + 'Arm', other + 'ForeArm', other + 'Hand']
    for n in moved:
        pb = P[B + n]
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        pb.keyframe_insert('location', frame=frame)


def settle(rig, bodies, items, act, scene, fix=True, until=None, log=print):
    """Checks every frame of the action (up to `until`: item name -> the last frame it is in the hand); turns clear
    what penetrates. Returns one row per held thing."""
    arm = rig.arm
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act
    if getattr(ad, 'action_slot', None) is None and getattr(act, 'slots', None):
        ad.action_slot = act.slots[0]
    scale = rig.rest_head['Hips'].z / 1.04
    held = [Held(rig, ob, scale) for ob in items]
    a, b = (int(x) for x in act.frame_range)
    frames = list(range(a, b + 1))
    until = until or {}
    inhand = lambda h, f: f <= until.get(h.ob.name, b)

    def at(f):
        scene.frame_set(f)
        rig.W = arm.matrix_world.copy()
        rig.Wi = rig.W.inverted()
        for h in held:
            h.frame()
        return skin_tree(bodies)

    rows = []
    turns = {id(h): {} for h in held}
    before = {id(h): {} for h in held}
    for f in frames:
        tree = at(f)
        for h in held:
            if not inhand(h, f):
                continue
            deep, bad = h.worst(tree)
            before[id(h)][f] = deep
            if fix and bad and not h.hangs:
                q, deg = h.way_clear(tree, bad)
                if q is not None:
                    turns[id(h)][f] = rotvec(q)
    after = {id(h): dict(before[id(h)]) for h in held}
    if fix and any(turns[id(h)] for h in held):
        for h in held:
            t = turns[id(h)]
            if not t:
                continue
            # Widened (each frame takes the largest turn within two frames of it), then smoothed: no pop.
            wide = {}
            for f in frames:
                near = [t[g] for g in range(f - 2, f + 3) if g in t]
                if near:
                    wide[f] = max(near, key=lambda v: v.length)
            smooth = {}
            for f in frames:
                acc, wsum = Vector(), 0.0
                for g, w in ((f - 2, 1), (f - 1, 2), (f, 3), (f + 1, 2), (f + 2, 1)):
                    if a <= g <= b:
                        acc += wide.get(g, Vector()) * w
                        wsum += w
                v = acc / wsum
                if f in t and v.length < t[f].length:
                    v = t[f]          # never less than the frame itself needed
                if v.length > 1e-4:
                    smooth[f] = v
            for f, v in smooth.items():
                at(f)
                apply_turn(rig, h, v, f)
        # Again, every frame; what still penetrates is turned on its own frame (no neighbours: a last resort), up to
        # three times over (a turn that frees the blade from his thigh may put the pommel in his belly).
        for _ in range(3):
            left = 0
            for f in frames:
                tree = at(f)
                for h in held:
                    if not inhand(h, f):
                        continue
                    deep, bad = h.worst(tree)
                    if bad and not h.hangs:
                        q, deg = h.way_clear(tree, bad)
                        if q is not None:
                            apply_turn(rig, h, rotvec(q), f)
                            tree = at(f)
                            deep, bad = h.worst(tree)
                    after[id(h)][f] = deep
                    left += bool(bad)
            if not left:
                break
    def where(h, f):
        """Which bones' skin the thing is in at frame f, most points first."""
        tree = at(f)
        count = {}
        for p, n, d in h.worst(tree)[1]:
            idx = tree.find_nearest(p)[2]
            ob, vi = OWNERS[idx]
            g = max(ob.data.vertices[vi].groups, key=lambda g: g.weight, default=None) if vi < len(ob.data.vertices) else None
            name = ob.vertex_groups[g.group].name.split(':')[-1] if g is not None else ob.name
            count[name] = count.get(name, 0) + 1
        return ', '.join('%s %d' % kv for kv in sorted(count.items(), key=lambda kv: -kv[1])[:3])

    for h in held:
        bf, af = before[id(h)], after[id(h)]
        if not bf:
            continue
        wb, wa = max(bf, key=bf.get), max(af, key=af.get)
        rows.append({'clip': act.name, 'item': h.ob.name, 'tolerance_cm': round(h.tol * 100, 1),
                     'before_cm': round(bf[wb] * 100, 1), 'before_frame': wb, 'frames_in': sum(1 for d in bf.values() if d > h.tol),
                     'after_cm': round(af[wa] * 100, 1), 'after_frame': wa, 'frames_left': sum(1 for d in af.values() if d > h.tol),
                     'frames': len(bf), 'turned': len(turns[id(h)]),
                     'where': where(h, wa) if af[wa] > h.tol else (where(h, wb) if bf[wb] > h.tol and not fix else '')})
    ad.action = None
    return rows


def wrist_report(rig, act, scene):
    """The worst bend of each wrist through the clip (degrees between the hand and where it would lie straight on
    the forearm) and the frame: beyond about 80 no wrist goes."""
    arm = rig.arm
    ad = arm.animation_data
    ad.action = act
    if getattr(ad, 'action_slot', None) is None and getattr(act, 'slots', None):
        ad.action_slot = act.slots[0]
    worst = {'Left': (0.0, 0), 'Right': (0.0, 0)}
    a, b = (int(x) for x in act.frame_range)
    for f in range(a, b + 1):
        scene.frame_set(f)
        rig.W = arm.matrix_world.copy()
        rig.Wi = rig.W.inverted()
        for side in ('Left', 'Right'):
            straight = rig.rot(side + 'ForeArm') @ (rig.rest_rot[side + 'ForeArm'].inverted() @ rig.rest_rot[side + 'Hand'])
            bend = math.degrees(straight.rotation_difference(rig.rot(side + 'Hand')).angle)
            bend = min(bend, 360.0 - bend)
            if bend > worst[side][0]:
                worst[side] = (bend, f)
    ad.action = None
    return worst
