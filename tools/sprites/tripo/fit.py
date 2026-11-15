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
        for n, w in (('Spine', 0.34), ('Spine1', 0.67), ('Spine2', 1.0)):
            rig.set_rot(n, hr.slerp(chest, w) @ rig.rest_rot[n])
        look = p.get('head', chest)
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
            e = su + (Vector(pm[elbow]) - sm) * self.A
            if not lying:
                # A hand below the shoulder and not out in front stays clear of the trunk's side.
                clear = self.trunk + 0.07
                if h.z < su.z and h.x < self.front + 0.05 and sgn * h.y < clear:
                    h.y = sgn * clear
            h.z = max(h.z, 0.05)
            la, lb = rig.lengths[side][:2]
            rig.two_bone(side + 'Arm', side + 'ForeArm', side + 'Hand', h, e, la, lb)
            rig.hand_follows_forearm(side)
            if p.get('fist', {}).get(side):
                rig.curl(side, p['fist'][side])
        rig.legs(p['feet'], p.get('free', 0.0), {s: keyer.knee_dir(p, s) for s in ('Left', 'Right')})
        if lying and _lift is None:
            # Down on the ground: lying on his own thickness, not the knight's.
            low = float(np.percentile(body_points(self.body)[:, 2], 1.0))  # a toe or a finger may dip
            if abs(low) > 0.01:
                return self.pose(pm, _lift=-low)
        return rig.snapshot(), p
