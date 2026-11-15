"""Fitting the knight's poses to another body.

poses.py is written for a man whose hips stand at 1.04 m. A dwarf is not that man made smaller: his legs are half
as long, his trunk and shoulders nearly as wide. So a pose is fitted limb by limb: the hips and feet by the legs'
length, each hand by where it is from the man's shoulder scaled by the arm's length and set off from the model's
own shoulder (the same reach and bend, whatever the trunk between), hands kept clear of the trunk's width, and a
lying body set down on the ground by its own thickness."""
import math

import bpy
import numpy as np
from mathutils import Quaternion, Vector

import keyer
from keyer import lean

MAN_HIPS = 1.04
MAN_SHOULDER = Vector((0.0, 0.19, 0.46))   # the arm's joint from the hips, at rest
MAN_ARM = 0.55                             # shoulder to wrist
MAN_FOOT_Y = 0.15


def body_points(body):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    me = ev.to_mesh()
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(n, 3)
    mw = np.array(ev.matrix_world)
    ev.to_mesh_clear()
    return co @ mw[:3, :3].T + mw[:3, 3]


def free_cloth(rig, body, reach):
    """A cape or long skirt skinned to the legs follows every stride like trousers. Its vertices are those the
    leg bones hold from further off than any leg is thick (`reach`, as a share of the hips' height): their leg
    weights go to the hips, so the cloth hangs from the body and the legs move inside it."""
    arm = rig.arm
    rig.rest()
    W = np.array(arm.matrix_world)
    me = body.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(n, 3)
    bw = np.array(body.matrix_world)
    pts = co @ bw[:3, :3].T + bw[:3, 3]
    far = np.full(n, True)
    limit = reach * rig.rest_head['Hips'].z
    legs = []
    for side in ('Left', 'Right'):
        for a, b in (('UpLeg', 'Leg'), ('Leg', 'Foot'), ('Foot', 'ToeBase')):
            legs.append((np.array(rig.rest_head[side + a]), np.array(rig.rest_head[side + b])))
    for a, b in legs:
        ab = b - a
        t = np.clip(((pts - a) @ ab) / (ab @ ab), 0, 1)
        d = np.linalg.norm(pts - (a + t[:, None] * ab), axis=1)
        far &= d > limit
    names = {g.index: g.name for g in body.vertex_groups}
    legset = {i for i, nm in names.items() if any(k in nm for k in ('UpLeg', 'Leg', 'Foot', 'Toe'))}
    hips = body.vertex_groups[keyer.B + 'Hips']
    moved = 0
    cloth = body.vertex_groups.get('tripo_cloth') or body.vertex_groups.new(name='tripo_cloth')
    for v in me.vertices:
        if not far[v.index]:
            continue
        w = sum(g.weight for g in v.groups if g.group in legset)
        if w > 1e-4:
            for gi in [g.group for g in v.groups if g.group in legset]:
                body.vertex_groups[gi].remove([v.index])
            hips.add([v.index], w, 'ADD')
            cloth.add([v.index], 0.0, 'REPLACE')     # marked (no weight: no bone has this name)
            moved += 1
    print('cloth freed from the legs: %d vertices' % moved, flush=True)


def _seg_dist(pts, a, b):
    ab = b - a
    t = np.clip(((pts - a) @ ab) / max(float(ab @ ab), 1e-12), 0, 1)
    return np.linalg.norm(pts - (a + t[:, None] * ab), axis=1)


def trunk_off_arms(rig, body):
    """Tripo's automatic weights give an arm bone some of the back and the chest: a vertex in the middle of the
    back a third on the upper arm is pulled into the body whenever the arm swings (the hero's and the journeyman's
    'right arm in the spine'). A vertex nearer the spine's bones than its arm's, at rest, is trunk: its weight on
    that arm (upper arm, forearm, hand) goes to the spine bone nearest it. So for a thigh (the minotaur's arm weights,
    the giant's, put on its hips): nearer a thigh bone than the arm, the weight goes to the thigh. Only clearly
    trunk (nearer by a fifth) and less than 0.6 the arm's; a cape's cloth always. The shoulder (clavicle) keeps
    its own."""
    rig.rest()
    me = body.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get('co', co)
    bw = np.array(body.matrix_world)
    pts = co.reshape(n, 3) @ bw[:3, :3].T + bw[:3, 3]
    R = {k: np.array(v) for k, v in rig.rest_head.items()}
    spine = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck']
    segs = list(zip(spine, spine[1:])) + [(s + 'UpLeg', s + 'Leg') for s in ('Left', 'Right')]
    dist = np.array([_seg_dist(pts, R[a], R[b]) for a, b in segs])
    d_trunk, nearest = dist.min(0), dist.argmin(0)
    names = {g.index: g.name.split(':')[-1] for g in body.vertex_groups}
    # A cape freed from the legs (free_cloth) hangs from the back: none of it follows an arm.
    cg = body.vertex_groups.get('tripo_cloth')
    cloth = np.zeros(n, bool)
    if cg is not None:
        for v in me.vertices:
            if any(g.group == cg.index for g in v.groups):
                cloth[v.index] = True
    groups = {nm: body.vertex_groups[g] for g, nm in ((g.index, g.name.split(':')[-1]) for g in body.vertex_groups)}
    moved = 0
    for side in ('Left', 'Right'):
        tips = [side + 'HandMiddle' + k for k in '1234' if side + 'HandMiddle' + k in R]
        chain = [side + 'Arm', side + 'ForeArm', side + 'Hand'] + tips
        d_arm = np.min([_seg_dist(pts, R[a], R[b]) for a, b in zip(chain, chain[1:]) if a != b], axis=0)
        # And the flank under the armpit of a broad chest (the minotaur's): further from the upper arm's bone than
        # the upper arm itself is thick, and on its inner side, is not the arm's skin, however far the spine is.
        sh, el = R[side + 'Arm'], R[side + 'ForeArm']
        ab = el - sh
        t = ((pts - sh) @ ab) / float(ab @ ab)
        radial = _seg_dist(pts, sh, el)
        aw = np.zeros(n)
        ag = body.vertex_groups.get(keyer.B + side + 'Arm')
        if ag is not None:
            for v in me.vertices:
                for g in v.groups:
                    if g.group == ag.index:
                        aw[v.index] = g.weight
        core = (aw > 0.9) & (t > 0.2) & (t < 0.9)
        thick = float(np.percentile(radial[core], 90)) if core.sum() > 20 else 1e9
        axis_y = sh[1] + np.clip(t, 0, 1) * ab[1]
        inner = np.abs(pts[:, 1]) < np.abs(axis_y)
        flank = (t > 0.15) & (t < 1.0) & (radial > 1.25 * thick) & inner
        trunk = np.nonzero((d_trunk < 0.8 * d_arm) | cloth | flank)[0]
        for i in trunk:
            v = me.vertices[int(i)]
            w = sum(g.weight for g in v.groups if names.get(g.group, '').startswith((side + 'Arm', side + 'ForeArm', side + 'Hand')))
            if w <= 1e-4 or (w >= 0.6 and not cloth[i] and not flank[i]):   # mostly the arm's: it is the arm (a sleeve, a hand at the thigh)
                continue
            for g in list(v.groups):
                if names.get(g.group, '').startswith((side + 'Arm', side + 'ForeArm', side + 'Hand')):
                    body.vertex_groups[g.group].remove([int(i)])
            near = segs[int(nearest[i])][0]
            g = groups.get(near) or body.vertex_groups.new(name=keyer.B + near)
            groups[near] = g
            g.add([int(i)], w, 'ADD')
            moved += 1
    print('arm weight taken off the trunk: %d vertices' % moved, flush=True)
    return moved


class Fit:
    def __init__(self, rig, body, step=1.0):
        self.rig, self.body = rig, body
        self.S = rig.rest_head['Hips'].z / MAN_HIPS
        self.A = sum(rig.lengths['Right'][:2]) / MAN_ARM
        self.Wf = abs(rig.rest_head['LeftFoot'].y - rig.rest_head['RightFoot'].y) / 2 / MAN_FOOT_Y
        # The trunk's half width below the chest (not the arms): what a hanging hand must clear.
        rig.rest()
        pts = body_points(body)
        hips, chest = rig.rest_head['Hips'].z, rig.rest_head['Spine2'].z
        arm_y = abs(rig.rest_head['LeftArm'].y)
        band = pts[(pts[:, 2] > hips * 0.8) & (pts[:, 2] < chest) & (np.abs(pts[:, 1]) < arm_y * 1.05)]
        self.trunk = float(np.abs(band[:, 1]).max()) if len(band) else arm_y
        self.front = float(band[:, 0].max()) if len(band) else 0.15
        # The trunk as it really is, for keeping hands out of it: an ellipse a level (half width, belly, back) from
        # the hips to the top of the chest, measured off the skin between the arms.
        chest = pts[(pts[:, 2] > hips) & (pts[:, 2] < rig.rest_head['Neck'].z) & (np.abs(pts[:, 1]) < arm_y * 0.95)]
        self.back = float(-chest[:, 0].min()) if len(chest) else 0.15
        self.belly = float(chest[:, 0].max()) if len(chest) else self.front
        self.half = arm_y * 0.95
        self.chest_top = rig.rest_head['Neck'].z - hips
        self.hand_r = 0.075 * self.A

    def options(self, cfg):
        """A unit's own allowances: "clear" false lets hands pass the trunk's side (a shield arm does)."""
        self.clear = cfg.get('clear', True)
        # metres a hanging hand keeps from the trunk's side: a hand's thickness, so a giant's as his hand is big
        self.margin = cfg.get('clear_m', 0.07) * max(1.0, self.A)
        self.hunch = cfg.get('hunch', 0.0)   # degrees his rest pose stoops: taken out of him as he lies down

    def describe(self):
        return 'legs x%.2f, arms x%.2f, stance x%.2f, trunk half width %.2f m, belly %.2f m' % (self.S, self.A, self.Wf, self.trunk, self.front)

    def feet(self, feet):
        return {s: (f[0] * self.S, f[1] * self.Wf, f[2]) + tuple(x * self.S for x in f[3:]) for s, f in feet.items()}

    def gait(self, g):
        feet = {s: (f[0] * self.S, f[1] * self.S, f[2] * self.Wf, f[3], f[4]) for s, f in g['feet'].items()}
        stride = (feet['Left'][0] - feet['Left'][1]) / (1 - g['swing'])
        return keyer.treadmill(g['period'], g['swing'], g['lift'] * self.S, feet), stride

    def pose(self, pm, _lift=None):
        """Builds the man's pose pm on the model; returns (snapshot, the fitted pose dict the bake needs)."""
        rig = self.rig
        p = dict(pm)
        hm = Vector(pm['hips'])
        p['hips'] = (hm.x * self.S, hm.y * self.S, hm.z * self.S + (_lift or 0.0))
        p['feet'] = self.feet(pm['feet'])
        rig.rest()
        hr = p.get('hips_rot', lean())
        rig.set_rot('Hips', hr @ rig.rest_rot['Hips'], Vector(p['hips']))
        chest = p.get('chest', lean())
        look = p.get('head', chest)
        if getattr(self, 'hunch', 0.0):
            down = min(1.0, max(0.0, (0.55 - hm.z) / 0.40))
            back = Quaternion(Vector((0, 1, 0)), math.radians(-self.hunch * down))
            chest, look = chest @ back, look @ back
        for n, w in (('Spine', 0.34), ('Spine1', 0.67), ('Spine2', 1.0)):
            rig.set_rot(n, hr.slerp(chest, w) @ rig.rest_rot[n])
        rig.set_rot('Neck', chest.slerp(look, 0.5) @ rig.rest_rot['Neck'])
        rig.set_rot('Head', look @ rig.rest_rot['Head'])
        # A shoulder rolled forward (+, _shrug) or lifted (+, _lift), in degrees: the weight of a walk shows there.
        frame = rig.rot('Spine2') @ rig.rest_rot['Spine2'].inverted()
        for side, sgn in (('Left', 1.0), ('Right', -1.0)):
            if side + '_shrug' in p:
                rig.set_rot(side + 'Shoulder', Quaternion(frame @ Vector((0, 0, 1)), math.radians(-sgn * p[side + '_shrug'])) @ rig.rot(side + 'Shoulder'))
            if side + '_lift' in p:
                rig.set_rot(side + 'Shoulder', Quaternion(frame @ Vector((1, 0, 0)), math.radians(-sgn * p[side + '_lift'])) @ rig.rot(side + 'Shoulder'))
        trunk_q = hr.slerp(chest, 0.6)
        lying = hm.z < 0.6
        for side, sgn, hand, elbow in (('Right', -1.0, 'sword_hand', 'sword_elbow'), ('Left', 1.0, 'shield_hand', 'shield_elbow')):
            sm = hm + trunk_q @ Vector((MAN_SHOULDER.x, sgn * MAN_SHOULDER.y, MAN_SHOULDER.z))
            su = rig.head(side + 'Arm')
            h = su + (Vector(pm[hand]) - sm) * self.A
            # No hand inside the trunk: the knight's poses bring hands in before a chest slimmer than this one's.
            # In the trunk's own frame, a hand within the trunk's ellipse (and a hand's thickness) is put out to it.
            origin = Vector(p['hips'])
            loc = trunk_q.inverted() @ (h - origin)
            if -0.05 * self.chest_top < loc.z < self.chest_top:
                a = (self.belly if loc.x >= 0 else self.back) + self.hand_r
                b = self.half + self.hand_r
                r = math.sqrt((loc.x / a) ** 2 + (loc.y / b) ** 2)
                if r < 1.0:
                    if r < 1e-3:
                        loc.x, r = a * 0.5, 0.5
                    loc.x, loc.y = loc.x / r, loc.y / r
                    h = origin + trunk_q @ loc
            e = su + (Vector(pm[elbow]) - sm) * self.A
            if not lying and getattr(self, 'clear', True):
                # A hand below the shoulder and not out in front stays clear of the trunk's side.
                clear = self.trunk + getattr(self, 'margin', 0.07)
                if h.z < su.z and h.x < self.front + 0.05 and sgn * h.y < clear:
                    h.y = sgn * clear
            h.z = max(h.z, 0.05)
            la, lb = rig.lengths[side][:2]
            rig.two_bone(side + 'Arm', side + 'ForeArm', side + 'Hand', h, e, la, lb)
            rig.hand_follows_forearm(side)
            if p.get('fist', {}).get(side):
                rig.curl(side, p['fist'][side])
        # What he holds: the weapon hand turned so the blade (or shaft) points along `blade`, its edge leading
        # toward `edge`; the strapped shield turned to face `shield_face`; a second hand on a long hilt or shaft
        # (`two_hands`: metres from the weapon hand toward the butt, or - toward the point).
        if rig.sword is not None and 'blade' in p:
            rig.grip(Vector(p['blade']), Vector(p.get('edge', (0, 0, -1))))
        if rig.shield is not None and 'shield_face' in p:
            rig.face_shield(Vector(p['shield_face']))
            if p.get('fist', {}).get('Left'):
                rig.curl('Left', p['fist']['Left'])
        if rig.sword is not None and p.get('two_hands') is not None:
            blade = (rig.rot('RightHand') @ rig.blade_local).normalized()
            fist = rig.fist('Right')
            want = fist - blade * (p['two_hands'] * self.A) - rig.palm_offset('Left')
            la, lb = rig.lengths['Left'][:2]
            su = rig.head('LeftArm')
            e = su + (Vector(pm['shield_elbow']) - (hm + trunk_q @ Vector((MAN_SHOULDER.x, MAN_SHOULDER.y, MAN_SHOULDER.z)))) * self.A
            rig.two_bone('LeftArm', 'LeftForeArm', 'LeftHand', want, e, la, lb)
            rig.hand_follows_forearm('Left')
            # The left palm to the shaft: its knuckle line along the weapon.
            rig.turn_knuckles('Left', blade)
            rig.curl('Left', 65)
        rig.legs(p['feet'], p.get('free', 0.0), {s: keyer.knee_dir(p, s) for s in ('Left', 'Right')})
        if lying and _lift is None:
            # Down on the ground: lying on his own thickness, not the knight's.
            low = float(np.percentile(body_points(self.body)[:, 2], 1.0))  # a toe or a finger may dip
            if abs(low) > 0.01:
                return self.pose(pm, _lift=-low)
        return rig.snapshot(), p
