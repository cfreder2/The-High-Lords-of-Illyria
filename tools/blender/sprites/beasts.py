"""Mounts and monsters: heavy cavalry, pegasi and unicorns on horse.py's
horse; wolf riders, war elephants, griffins and dragons on quad.py; giant
spiders on a body of their own.

Fliers (griffins, pegasi, dragons; archons in units.py) use the walk's eight
frames as two cycles: frames 0-3 walking on the ground, 4-7 a wingbeat in
flight (the renderer picks the half by whether the unit is in the air)."""

import math

import bpy
from mathutils import Vector

from . import gear, geo, horse, human, mats, outfit, people, rig, wings
from .batwing import BatWing
from .geo import Part, Rand, blend, smooth
from .human import X, Y, Z, Stance, keyed
from .quad import Adapter, Quad, bound_phases, walk_phases
from .rig import Bone, Pose
from .units import unit

AX = (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))


def _cycle(t):
    return math.sin(2 * math.pi * t), math.cos(2 * math.pi * t)


def make(col, arm, L, parts):
    for part, subdiv, solid in parts:
        part.make(col, arm, subdiv=subdiv, materials=L, solidify=solid)


# ---------------------------------------------------------------- riders

def ride_stance(weapon='sword', shield=False, legs=(62, 24, 82)):
    r = Stance()
    r.thigh = [legs[0], legs[0]]
    r.thigh_out = [legs[1], legs[1]]
    r.knee = [legs[2], legs[2]]
    r.ankle = [-12, -12]
    r.lean = 6
    r.shoulder = [28, 30]
    r.shoulder_out = [12, 14]
    r.elbow = [80, 85]
    r.arm_twist = [25, 0]
    r.wrist = [-10, -35]
    if weapon == 'lance':
        r.shoulder[1], r.shoulder_out[1], r.elbow[1], r.wrist[1] = 14, 10, 82, 0
    if shield:
        r.shoulder[0], r.shoulder_out[0], r.arm_twist[0], r.elbow[0] = 30, 22, 70, 90
    return r


def ride_attack(ride, weapon='sword'):
    if weapon == 'lance':
        up = ride.copy()
        up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1] = 18, 18, 62, -60
        up.lean = 12
        hit = ride.copy()
        hit.shoulder[1], hit.shoulder_out[1], hit.elbow[1], hit.wrist[1] = 38, 10, 32, -48
        hit.lean, hit.twist = 24, 10
        follow = human.mix(hit, up, 0.4)
        return [(0, ride), (0.3, up), (0.55, hit), (0.8, follow), (1.0, human.mix(follow, ride, 0.6))]
    up = ride.copy()
    up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1] = 160, 50, 110, 40
    up.twist, up.lean, up.side_lean = -25, 0, 6
    cut = ride.copy()
    cut.shoulder[1], cut.shoulder_out[1], cut.elbow[1], cut.wrist[1] = 60, 40, 10, -10
    cut.twist, cut.lean, cut.side_lean = 20, 18, -14
    follow = cut.copy()
    follow.shoulder[1], follow.shoulder_out[1], follow.elbow[1] = 20, 30, 15
    follow.twist, follow.side_lean = 28, -18
    return [(0, ride), (0.3, up), (0.55, cut), (0.8, follow), (1.0, human.mix(follow, ride, 0.6))]


def rider_frame(ride, anim, i, n, weapon='sword'):
    """The rider's stance for a frame (the mount's own motion is separate)."""
    t = i / n
    u = i / (n - 1)
    if anim == 'idle':
        st = ride.copy()
        b = math.sin(2 * math.pi * t)
        st.lean += 1.5 * b
        st.root = Vector((0, 0, 0.004 * b))
        return st
    if anim == 'walk':
        a, c = _cycle(t)
        st = ride.copy()
        st.lean = 14 + 3 * c
        st.root = Vector((0.02, 0, 0.03 + 0.025 * c))
        st.knee = [ride.knee[0] - 4 - 4 * c] * 2
        st.shoulder = [st.shoulder[0], st.shoulder[1] + 5 * a]
        return st
    if anim == 'attack':
        return keyed(ride_attack(ride, weapon), u)
    if anim == 'flinch':
        st = keyed(people.flinch_keys(ride), u)
        st.thigh, st.knee, st.thigh_out = ride.thigh, ride.knee, ride.thigh_out
        return st
    st = ride.copy()
    st.lean = ride.lean - 30 * smooth(0.0, 0.6, u)
    st.nod = -10 * u
    st.shoulder_out = [ride.shoulder_out[0] + 40 * u, ride.shoulder_out[1] + 50 * u]
    st.shoulder = [ride.shoulder[0] + 40 * u, ride.shoulder[1] + 30 * u]
    return st


def rider_body(B, coat='team', helm='nasal', mail='mail', legs_mat=None, plume=True, skin='skin'):
    """A mailed rider in a split surcoat; returns parts."""
    body = Part('rider')
    human.torso(body, B, mail, grow=0.004)
    human.neck(body, B, mail)
    for side in (1, -1):
        human.arm(body, B, side, mail, sleeve=0.012)
        human.leg(body, B, side, legs_mat or (lambda j, s: 'leather' if j >= 4 else 'hose'), sleeve=0.006)
    ends = Part('rider_ends')
    for side in (1, -1):
        human.hand(ends, B, side, 'leather', grip=True)
        human.foot(ends, B, side, 'leather_dark')
    head = Part('rider_head')
    human.head(head, B, skin)
    if helm == 'nasal':
        outfit.nasal_helm(head, B)
    elif helm == 'great':
        outfit.great_helm(head, B)
    if plume:
        gear.plume(head, B, 'team', back=0.3)
    m = Part('rider_mail')
    outfit.coif(m, B)
    c = Part('rider_coat')
    human.torso(c, B, coat, grow=0.022, z_from=0.93, z_to=1.49)
    outfit.skirt(c, B, coat, 1.0, 0.66, (0.12, 0.185), (0.16, 0.23), fold_depth=0.05, seed=21, a=(0.35, math.pi - 0.35), n=16, rows=6)
    outfit.skirt(c, B, coat, 1.0, 0.66, (0.12, 0.185), (0.16, 0.23), fold_depth=0.05, seed=23, a=(math.pi + 0.35, 2 * math.pi - 0.35), n=16, rows=6)
    outfit.belt(c, B, 'leather_dark', 1.02, 0.17, 0.115, buckle='gold')
    return [(body, 2, 0), (ends, 2, 0), (head, 2, 0), (m, 2, 0.005), (c, 2, 0.007)]


# ---------------------------------------------------------------- the horse's motions

def horse_motion(H, p, anim, i, n, gallop_t=None):
    """The horse under its rider (light_cavalry's, shared)."""
    t = i / n
    u = i / (n - 1)
    if anim == 'idle':
        b = math.sin(2 * math.pi * t)
        p.turn(H.n('neck1'), Y, 4 + 3 * b).turn(H.n('head'), Y, 2 * b)
        p.turn(H.n('tail1'), Y, 6).turn(H.n('tail2'), Z, 8 * b).turn(H.n('tail3'), Z, 10 * b)
        H.leg_pose(p, 'h', -1, -4, 12, 6, 10)
        return p
    if anim == 'walk':
        return H.gallop(p, t if gallop_t is None else gallop_t)
    if anim == 'attack':
        r = math.sin(math.pi * u)
        p.turn(H.n('spine'), Y, -6 * r).turn(H.n('neck1'), Y, -10 * r).turn(H.n('head'), Y, 6 * r)
        for side in (1, -1):
            H.leg_pose(p, 'f', side, 18 * r * (1 if side > 0 else 0.6), 60 * r, 20 * r, 25 * r)
            H.leg_pose(p, 'h', side, 10 * r, 10 * r, 12 * r, 0)
        p.turn(H.n('tail1'), Y, 15 * r)
        return p
    if anim == 'flinch':
        k = [1.0, 0.5][i]
        p.turn(H.n('neck1'), Y, -14 * k).turn(H.n('head'), Y, -12 * k).turn(H.n('spine'), Y, -3 * k)
        H.leg_pose(p, 'f', 1, 8 * k, 30 * k, 10 * k, 10 * k)
        p.turn(H.n('tail1'), Y, 20 * k)
        return p
    fold = smooth(0.0, 0.55, u)
    sink = smooth(0.1, 0.75, u)
    roll = 80 * smooth(0.35, 1.0, u)
    p.move(H.n('root'), (0, 0, -0.55 * H.k * sink + 0.36 * H.k * smooth(0.35, 1.0, u)))
    p.turn(H.n('root'), X, roll)
    p.turn(H.n('spine'), Y, 8 * fold - 8 * sink)
    for side in (1, -1):
        H.leg_pose(p, 'f', side, -10 * fold + 30 * sink, 120 * fold, 40 * fold, 40 * fold)
        H.leg_pose(p, 'h', side, 30 * sink, 70 * sink, 60 * sink, 30 * sink)
    p.turn(H.n('neck1'), Y, 25 * sink - 20 * smooth(0.6, 1.0, u)).turn(H.n('head'), Y, 10 * u)
    p.turn(H.n('tail1'), Y, 10)
    return p


def horse_flight(H, p, t):
    """In the air: forelegs folded up, hind legs trailing, neck out."""
    a, c = _cycle(t)
    for side in (1, -1):
        H.leg_pose(p, 'f', side, 25 + 4 * a, 100, 30, 40)
        H.leg_pose(p, 'h', side, -35 - 4 * a, 20, 30, 20)
    p.move(H.n('root'), (0, 0, 0.12 * H.k * c))
    p.turn(H.n('neck1'), Y, -12).turn(H.n('head'), Y, 8)
    p.turn(H.n('tail1'), Y, 40).turn(H.n('tail2'), Y, 10)
    return p


def caparison(H, part, mat='team', hem=0.72, x0=0.62, x1=-0.92, trim='gold'):
    """A cloth over the horse from the breast to the croup, hanging to hem (m, 1.6 m horse)."""
    k = H.k

    def fn(s, t):
        x = x0 + (x1 - x0) * t
        th = (2 * s - 1) * 2.4
        tt = max(-1.5, min(1.5, th))
        p, nrm = H.trunk_point(x, tt, out=0.03)
        if abs(th) > 1.5:
            # Hanging straight down from the side.
            drop = (abs(th) - 1.5) / 0.9
            z_side = p.z
            p = Vector((p.x, p.y + math.copysign(0.03 * k * drop, th), z_side + (hem * k - z_side) * drop))
        return p

    def mt(s, t):
        return trim if abs(2 * s - 1) > 0.95 else mat
    part.grid(fn, 24, 14, mt, lambda s, t: H.trunk_weights(x0 + (x1 - x0) * t))


def chanfron(H, part, mat='steel'):
    a, b, d, front = H.head_frame()
    L = (b - a).length
    k = H.k

    def fn(s, t):
        c = a + d * (L * (0.05 + 0.8 * t))
        ang = (2 * s - 1) * 1.3
        rr = (0.16 - 0.08 * t) * k
        return c + front * (math.cos(ang) * rr * 0.95) + Vector((0, math.sin(ang) * rr * 0.8, 0))
    part.grid(fn, 8, 8, mat, H.w('head'))


def horse_parts(H, coat, mane='mane'):
    hide = Part('horse')
    H.body(hide, coat)
    hair = Part('mane')
    H.mane_and_tail(hair, mane)
    return hide, hair


# ---------------------------------------------------------------- heavy cavalry

@unit(5, 'heavy_cavalry')
def heavy_cavalry():
    """A knight on a grey destrier barded in the side's colour to the knees,
    a steel chanfron; great helm with a plume, plate and mail, a lance with a
    pennon and a shield."""
    col = bpy.context.scene.collection
    L = mats.library()
    H = horse.Horse(1.65)
    H.points = 'coat_grey'
    seat = H.seat()
    B0 = human.Body(1.85, 1.05)
    B = human.Body(1.85, 1.05, offset=seat - B0.P(-0.02, 0, 0.97), prefix='r_')
    bones = H.bones() + human.skeleton(B, parent=H.n('spine'))
    arm = rig.build('rig', bones, col)
    hide, hair = horse_parts(H, 'coat_grey', 'mane')
    tack = Part('tack')
    H.tack(tack)
    H.reins(tack, B.n('hand.L'), human.grip_point(B, 1))
    bard = Part('barding')
    caparison(H, bard, 'team', hem=0.7)
    neck = Part('crinet')
    # The crinet: cloth over the neck in the side's colour.
    rows = [((0.42, 1.63), (0.8, 1.02), 0.22), ((0.6, 1.73), (0.93, 1.21), 0.182), ((0.76, 1.85), (1.01, 1.42), 0.148), ((0.9, 1.97), (1.08, 1.62), 0.122)]
    rings = []
    for idx, ((tx, tz), (bx, bz), hw) in enumerate(rows):
        top, bot = H.P(tx, 0, tz), H.P(bx, 0, bz)
        ww = [H.w('chest'), H.w('chest', 0.5, 'neck1', 0.5), H.w('neck1'), H.w('neck1', 0.5, 'neck2', 0.5)][idx]
        rings.append({'c': (top + bot) / 2, 'u': (top - bot).normalized(), 'v': Vector((0, 1, 0)), 'ru': (top - bot).length / 2 + 0.02, 'rv': hw * H.k * 1.12 + 0.02, 'w': ww})
    neck.loft(rings, 'team', n=18, cap0=False, cap1=False)
    chanfron(H, neck)
    parts = rider_body(B, helm='great', legs_mat=lambda j, s: 'steel' if j >= 4 else 'mail')
    plate = Part('plate')
    outfit.cuirass(plate, B, 'steel', grow=0.035)
    for side in (1, -1):
        outfit.pauldron(plate, B, side, 'steel')
    sh = Part('shield')
    gear.round_shield(sh, B, radius=0.3, band='gold')
    ln = Part('lance')
    gear.lance(ln, B, side=-1, length=3.2, butt=0.75)
    make(col, arm, L, [(hide, 2, 0), (hair, 1, 0), (tack, 1, 0), (bard, 2, 0.008), (neck, 2, 0.006)] + parts + [(plate, 2, 0), (sh, 1, 0), (ln, 1, 0)])
    ride = ride_stance('lance', shield=True)

    def pose_of(anim, i, n):
        p = human.to_pose(rider_frame(ride, anim, i, n, 'lance'), B, mounted=True)
        return horse_motion(H, p, anim, i, n)
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- pegasi

# Wings at rest on a horse's back: fold, lift, sweep, twist (wings.Wing.pose).
FOLDED_WINGS = (0.8, 5, 60, 10)


@unit(12, 'pegasi')
def pegasi():
    """A white winged horse, its rider in mail and a surcoat in the side's
    colour with a sword. Walks (frames 0-3 of the walk) and flies (4-7)."""
    col = bpy.context.scene.collection
    L = mats.library()
    H = horse.Horse(1.45)
    H.points = 'coat_white'
    seat = H.seat()
    B0 = human.Body(1.75, 1.0)
    B = human.Body(1.75, 1.0, offset=seat - B0.P(-0.02, 0, 0.97), prefix='r_')
    WA = Adapter(H, s=H.k)
    wl = wings.Wing(WA, 1, H.P(0.32, 0.17, 1.52), scale=1.35, mat='feather', edge_mat='feather')
    wr = wings.Wing(WA, -1, H.P(0.32, -0.17, 1.52), scale=1.35, mat='feather', edge_mat='feather')
    bones = H.bones() + human.skeleton(B, parent=H.n('spine'))
    feathers, edges = Part('feathers'), Part('wing_edges')
    for wg in (wl, wr):
        bones += wg.bones()
        wg.build(feathers, bones, edges)
    arm = rig.build('rig', bones, col)
    hide, hair = horse_parts(H, 'coat_white', 'mane_white')
    tack = Part('tack')
    H.tack(tack)
    H.reins(tack, B.n('hand.L'), human.grip_point(B, 1))
    parts = rider_body(B, helm='nasal')
    sw = Part('sword')
    outfit.sword(sw, B, side=-1, blade=0.85)
    make(col, arm, L, [(hide, 2, 0), (hair, 1, 0), (tack, 1, 0)] + parts + [(feathers, 1, 0.003), (edges, 2, 0), (sw, 0, 0)])
    ride = ride_stance('sword')

    def wings_pose(p, fold, lift, sweep=0.0, twist=0.0, ripple=0.0):
        for wg in (wl, wr):
            wg.pose(p, fold, lift, sweep, twist, ripple=ripple)

    def pose_of(anim, i, n):
        t = i / n
        if anim == 'walk' and i >= 4:
            tt = (i - 4) / 4
            st = rider_frame(ride, 'walk', i - 4, 4)
            st.lean = 10
            p = human.to_pose(st, B, mounted=True)
            horse_flight(H, p, tt)
            a, c = _cycle(tt)
            wings_pose(p, 0.05 + 0.3 * max(0.0, -a), 8 + 55 * c, -6 + 12 * max(0.0, -a), -8 * a, ripple=3 * a)
            return p
        if anim == 'walk':
            tt = i / 4
            p = human.to_pose(rider_frame(ride, 'walk', i, 4), B, mounted=True)
            horse_motion(H, p, 'walk', i, n, gallop_t=tt)
            wings_pose(p, *FOLDED_WINGS)
            return p
        p = human.to_pose(rider_frame(ride, anim, i, n), B, mounted=True)
        horse_motion(H, p, anim, i, n)
        u = i / (n - 1)
        if anim == 'idle':
            b = math.sin(2 * math.pi * t)
            f, l, sw, tw = FOLDED_WINGS
            wings_pose(p, f, l + 2 * b, sw, tw, ripple=b)
        elif anim == 'attack':
            wings_pose(p, [0.8, 0.3, 0.1, 0.2, 0.5, 0.75][i], [5, 20, 40, 10, -10, 5][i], [60, 10, 0, 5, 30, 55][i], 10)
        elif anim == 'flinch':
            wings_pose(p, [0.4, 0.55][i], [20, 5][i], 15, 10)
        else:
            wings_pose(p, 0.8 - 0.5 * smooth(0.1, 0.8, u), 5 - 30 * u, 60 - 40 * u, 10)
        return p
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- unicorns

@unit(14, 'unicorns')
def unicorns():
    """A white unicorn: a spiralled gold horn, a golden mane and tail, a
    collar and a light cloth in the side's colour."""
    col = bpy.context.scene.collection
    L = mats.library()
    H = horse.Horse(1.6)
    H.points = 'coat_white'
    arm = rig.build('rig', H.bones(), col)
    hide, hair = horse_parts(H, 'coat_white', 'mane_gold')
    horn = Part('horn')
    a, b, d, front = H.head_frame()
    base = a + front * 0.09 * H.k + d * 0.06 * H.k
    tipd = (front * 0.9 + d * -0.35).normalized()
    tipd = (tipd + Vector((0.35, 0, 0.2))).normalized()
    pts = [base + tipd * (0.55 * H.k * t) for t in (0, 0.33, 0.66, 1.0)]
    horn.chain(pts, [0.035 * H.k, 0.026 * H.k, 0.015 * H.k, 0.003], 'gold', [H.w('head')] * 4, hint=Vector((0, 1, 0)), n=8,
               shape=lambda i, th: 1.0 + 0.15 * math.sin(th * 2 + i * 5.0))
    cloth = Part('cloth')
    caparison(H, cloth, 'team', hem=1.05, x0=0.35, x1=-0.55, trim='gold')
    rows = [((0.62, 1.74), (0.94, 1.24), 0.18), ((0.7, 1.79), (0.98, 1.33), 0.165)]
    rings = []
    for (tx, tz), (bx, bz), hw in rows:
        top, bot = H.P(tx, 0, tz), H.P(bx, 0, bz)
        rings.append({'c': (top + bot) / 2, 'u': (top - bot).normalized(), 'v': Vector((0, 1, 0)), 'ru': (top - bot).length / 2 + 0.025, 'rv': hw * H.k * 1.12 + 0.025,
                      'w': H.w('chest', 0.4, 'neck1', 0.6)})
    cloth.loft(rings, 'team', n=18, cap0=False, cap1=False)
    make(col, arm, L, [(hide, 2, 0), (hair, 1, 0), (horn, 1, 0), (cloth, 2, 0.006)])

    def pose_of(anim, i, n):
        p = Pose()
        u = i / (n - 1)
        if anim == 'attack':
            # Head down, the horn driven forward, a lunge off the hind legs.
            r = math.sin(math.pi * min(1.0, u * 1.3))
            p.turn(H.n('neck1'), Y, 30 * r).turn(H.n('neck2'), Y, 15 * r).turn(H.n('head'), Y, -25 * r)
            p.move(H.n('root'), (0.15 * r * H.k, 0, -0.04 * r))
            for side in (1, -1):
                H.leg_pose(p, 'f', side, -10 * r, 10 * r, 5 * r, 0)
                H.leg_pose(p, 'h', side, -15 * r, 10 * r, 10 * r, 5 * r)
            p.turn(H.n('tail1'), Y, 25 * r)
            return p
        return horse_motion(H, p, anim, i, n)
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- wolf riders

WOLF = {
    'trunk': [(0.62, 0.98, 0.64, 0.12), (0.5, 1.04, 0.52, 0.19), (0.3, 1.02, 0.5, 0.21), (0.1, 0.98, 0.56, 0.19), (-0.1, 0.94, 0.62, 0.16),
              (-0.3, 0.93, 0.64, 0.155), (-0.45, 0.92, 0.66, 0.15), (-0.58, 0.88, 0.7, 0.11), (-0.64, 0.84, 0.75, 0.05)],
    'neck': [((0.48, 1.06), (0.64, 0.64), 0.17), ((0.64, 1.13), (0.8, 0.82), 0.14), ((0.78, 1.2), (0.9, 0.98), 0.115)],
    'head_bone': ((0.85, 1.15), (1.15, 1.08)),
    'jaw': ((0.9, 1.08), (1.15, 1.04)),
    'legs': {'f': [(0.44, 0.11, 0.88), (0.5, 0.13, 0.58), (0.48, 0.13, 0.3), (0.5, 0.13, 0.08), (0.57, 0.13, 0.02)],
             'h': [(-0.48, 0.12, 0.86), (-0.34, 0.14, 0.58), (-0.55, 0.13, 0.32), (-0.52, 0.13, 0.08), (-0.45, 0.13, 0.02)]},
    'radii': {'f': [(0.09, 0.06), (0.06, 0.05), (0.04, 0.035), (0.035, 0.03), (0.04, 0.035)],
              'h': [(0.13, 0.07), (0.08, 0.06), (0.045, 0.04), (0.035, 0.03), (0.04, 0.035)]},
    'mass': {'f': ((0.45, 0.12, 0.8), (0.12, 0.06, 0.16)), 'h': ((-0.46, 0.13, 0.8), (0.16, 0.07, 0.17))},
    'tail': [(-0.62, 0, 0.86), (-0.78, 0, 0.78), (-0.92, 0, 0.66), (-1.02, 0, 0.52)],
    'fold_signs': {'f': [-1, 1, 1], 'h': [1, -1, 1]},
}


def wolf_head(q, part, coat):
    w = q.w('head')
    P = q.P
    part.ellipsoid(P(0.92, 0, 1.18), (0.13 * q.k, 0.11 * q.k, 0.11 * q.k), coat, w, seg=16, rings=10)
    part.chain([P(0.98, 0, 1.17), P(1.1, 0, 1.13), P(1.2, 0, 1.1)], [(0.065 * q.k, 0.065 * q.k), (0.05 * q.k, 0.05 * q.k), (0.035 * q.k, 0.035 * q.k)], coat, [w] * 3, hint=Vector((0, 0, 1)), n=12)
    part.ellipsoid(P(1.22, 0, 1.115), (0.025 * q.k, 0.025 * q.k, 0.02 * q.k), 'horse_eye', w, seg=8, rings=5)
    wj = q.w('jaw')
    part.chain([P(0.98, 0, 1.1), P(1.1, 0, 1.07), P(1.18, 0, 1.06)], [(0.03 * q.k, 0.05 * q.k), (0.025 * q.k, 0.04 * q.k), (0.02 * q.k, 0.03 * q.k)], coat, [wj] * 3, hint=Vector((0, 0, 1)), n=10)
    part.chain([P(1.0, 0, 1.115), P(1.17, 0, 1.08)], [(0.015 * q.k, 0.045 * q.k), (0.012 * q.k, 0.03 * q.k)], 'teeth', [wj, wj], hint=Vector((0, 0, 1)), n=8)
    for side in (1, -1):
        part.ellipsoid(P(1.0, 0.07 * side, 1.21), (0.02 * q.k, 0.015 * q.k, 0.015 * q.k), 'eye_red', w, seg=8, rings=5)
        b = P(0.88, 0.07 * side, 1.27)
        part.chain([b, b + Vector((-0.01, 0.02 * side, 0.08)) * q.k, b + Vector((-0.02, 0.025 * side, 0.15)) * q.k],
                   [(0.04 * q.k, 0.015 * q.k), (0.03 * q.k, 0.012 * q.k), (0.003, 0.002)], coat, [w] * 3, hint=Vector((1, 0, 0)), n=8)
    # The ruff of a wolf's neck.
    part.ellipsoid(P(0.68, 0, 0.98), (0.2 * q.k, 0.2 * q.k, 0.24 * q.k), coat, q.w('neck0', 0.5, 'chest', 0.5), seg=16, rings=10,
                   shape=lambda v: 1.0 + 0.06 * math.sin(math.atan2(v.y, v.x) * 12 + v.z * 4))


def paws(q, part, mat='talon', toes=4, size=0.04):
    for side in (1, -1):
        for leg in ('f', 'h'):
            sf = 'L' if side > 0 else 'R'
            w = q.w('%s3.%s' % (leg, sf))
            c = q.joint(leg, 4, side)
            part.ellipsoid(c + Vector((0, 0, size * 0.5 * q.k)), (size * 1.6 * q.k, size * 1.2 * q.k, size * 0.7 * q.k), q.S.get('paw_mat', 'fur_grey'), w, seg=12, rings=6)
            for t in range(toes):
                y = (t - (toes - 1) / 2) * size * 0.6 * q.k
                part.chain([c + Vector((size * 1.2 * q.k, y, size * 0.4 * q.k)), c + Vector((size * 2.0 * q.k, y * 1.1, 0.004))],
                           [size * 0.22 * q.k, 0.003], mat, [w, w], hint=Vector((0, 0, 1)), n=6)


@unit(7, 'wolf_riders')
def wolf_riders():
    """A goblin on a great grey wolf: the goblin green and big-eared in a
    leather cap and a tunic in the side's colour, a curved sword; the wolf
    saddled over a cloth in the side's colour."""
    col = bpy.context.scene.collection
    L = mats.library()
    q = Quad(WOLF, k=1.05, prefix='w_')
    seat, _ = q.trunk_point(0.02, 0.0, out=0.07)
    B0 = human.Body(1.15, 0.9, head=1.3)
    B = human.Body(1.15, 0.9, head=1.3, offset=seat - B0.P(-0.02, 0, 0.97), prefix='r_')
    bones = q.bones() + human.skeleton(B, parent=q.n('spine'))
    arm = rig.build('rig', bones, col)
    hide = Part('wolf')
    q.body(hide, 'fur_grey', belly='fur_light')
    wolf_head(q, hide, 'fur_grey')
    paws(q, hide)
    tail = Part('tail')
    tl = [q.P(*p) for p in WOLF['tail']]
    tail.chain(tl, [0.06 * q.k, 0.08 * q.k, 0.075 * q.k, 0.02 * q.k], 'fur_grey', [q.w('tail0', 0.5, 'spine', 0.5), q.w('tail0', 0.5, 'tail1', 0.5), q.w('tail1', 0.5, 'tail2', 0.5), q.w('tail2')],
               hint=Vector((0, 0, 1)), n=10, shape=lambda i, th: 1.0 + 0.08 * math.sin(th * 9 + i))
    tack = Part('tack')

    def cloth(s, t):
        x = 0.25 - 0.5 * t
        th = (2 * s - 1) * 1.55
        p, nrm = q.trunk_point(x, th, out=0.015)
        return p
    tack.grid(cloth, 14, 8, 'team', lambda s, t: q.trunk_weights(0.25 - 0.5 * t))

    def seatf(s, t):
        x = 0.15 - 0.3 * t
        th = (2 * s - 1) * 0.9
        rise = 0.02 + 0.06 * smooth(0.25, 0.0, t) + 0.05 * smooth(0.75, 1.0, t)
        p, nrm = q.trunk_point(x, th, out=0.03 + rise)
        return p
    tack.slab(seatf, 10, 10, 0.025 * q.k, 'leather', lambda s, t: q.trunk_weights(0.15 - 0.3 * t), rim_mat='leather_dark')
    rows = []
    for x in (0.28, 0.22):
        top, bot, hw = q.trunk_ring(x)
        rows.append({'c': q.P(x, 0, (top + bot) / 2), 'u': Vector((0, 0, 1)), 'v': Vector((0, 1, 0)), 'ru': ((top - bot) / 2 + 0.01) * q.k,
                     'rv': (hw + 0.01) * q.k, 'w': q.w('chest'), 'shape': q.trunk_shape})
    tack.loft(rows, 'leather_dark', n=20, cap0=False, cap1=False)
    # The goblin.
    rb = Part('rider')
    human.torso(rb, B, 'leather', grow=0.004)
    human.neck(rb, B, 'skin_goblin')
    for side in (1, -1):
        human.arm(rb, B, side, 'skin_goblin')
        human.leg(rb, B, side, lambda j, s: 'leather' if j >= 5 else 'hose', sleeve=0.006)
    ends = Part('rider_ends')
    for side in (1, -1):
        human.hand(ends, B, side, 'skin_goblin', grip=True)
        human.foot(ends, B, side, 'leather_dark')
    h = Part('rider_head')
    human.head(h, B, 'skin_goblin', eye_mat='eye_red', ears=False)
    gear.pointed_ears(h, B, 'skin_goblin', length=0.16)
    gear.kettle_hat(h, B, steel='leather', brim=1.3)
    c = h  # the goblin's nose: long and hooked
    hc, hr = human.head_center(B), human.head_radii(B)
    c.chain([hc + Vector((hr[0] * 0.9, 0, 0)), hc + Vector((hr[0] * 1.35, 0, -hr[2] * 0.3))], [hr[1] * 0.2, hr[1] * 0.05], 'skin_goblin', [B.w('head')] * 2, hint=Vector((0, 0, 1)), n=8)
    tu = Part('rider_tunic')
    human.torso(tu, B, 'team', grow=0.02, z_from=0.93, z_to=1.49)
    outfit.skirt(tu, B, 'team', 1.0, 0.7, (0.12, 0.185), (0.16, 0.23), fold_depth=0.05, seed=81, a=(0.35, math.pi - 0.35), n=12, rows=5,
                 hem=lambda th: 0.15 * math.sin(th * 6))
    outfit.skirt(tu, B, 'team', 1.0, 0.7, (0.12, 0.185), (0.16, 0.23), fold_depth=0.05, seed=83, a=(math.pi + 0.35, 2 * math.pi - 0.35), n=12, rows=5,
                 hem=lambda th: 0.15 * math.sin(th * 6))
    sw = Part('sword')
    outfit.sword(sw, B, side=-1, blade=0.62, width=0.034)
    # The blade curved: a scimitar.
    for i in range(len(sw.verts)):
        p = sw.verts[i]
        g = human.grip_point(B, -1)
        along = (p - g).x
        if along > 0.08:
            p.z -= 0.25 * (along - 0.08) ** 2 / B.s
    make(col, arm, L, [(hide, 2, 0), (tail, 1, 0), (tack, 1, 0)] + [(rb, 2, 0), (ends, 2, 0), (h, 2, 0), (tu, 2, 0.006), (sw, 0, 0)])
    ride = ride_stance('sword', legs=(55, 32, 70))
    ride.lean = 14

    def tail_pose(p, a, up=0.0):
        for k in range(3):
            p.turn(q.n('tail%d' % k), Z, a * (k + 1) * 0.6).turn(q.n('tail%d' % k), Y, -up)

    def pose_of(anim, i, n):
        t = i / n
        u = i / (n - 1)
        st = rider_frame(ride, anim, i, n)
        p = human.to_pose(st, B, mounted=True)
        if anim == 'idle':
            b = math.sin(2 * math.pi * t)
            p.turn(q.n('neck0'), Y, 3 * b).turn(q.n('head'), Y, -2 * b)
            p.turn(q.n('jaw'), Y, 4 + 3 * b)
            tail_pose(p, 6 * b, 5)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 0, [0, 0, 0])
        elif anim == 'walk':
            q.gait(p, t, bound_phases(), stance=0.38, reach=(26, 22), lift=(70, 60), pitch=6, bob=0.06)
            p.turn(q.n('neck0'), Y, -6).turn(q.n('head'), Y, 8)
            p.turn(q.n('jaw'), Y, 10)
            tail_pose(p, 4 * math.sin(2 * math.pi * t), 15)
        elif anim == 'attack':
            # A lunge and a snap.
            r = math.sin(math.pi * u)
            p.move(q.n('root'), (0.25 * r * q.k, 0, 0.04 * r))
            p.turn(q.n('spine'), Y, -8 * r).turn(q.n('neck0'), Y, -10 * r).turn(q.n('head'), Y, 10 * r)
            p.turn(q.n('jaw'), Y, [5, 30, 35, 5, 10, 5][i])
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 25 * r, [-10 * r, 40 * r, 20 * r])
                q.leg_pose(p, 'h', side, -15 * r, [20 * r, 20 * r, 0])
            tail_pose(p, 0, 20 * r)
        elif anim == 'flinch':
            k = [1.0, 0.5][i]
            p.turn(q.n('neck0'), Y, -12 * k).turn(q.n('head'), Y, -10 * k)
            tail_pose(p, 0, -10 * k)
        else:
            sink = smooth(0.1, 0.8, u)
            roll = 80 * smooth(0.35, 1.0, u)
            p.move(q.n('root'), (0, 0, -0.45 * q.k * sink + 0.2 * q.k * smooth(0.35, 1.0, u)))
            p.turn(q.n('root'), X, roll)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, -10 * sink, [40 * sink, 80 * sink, 30 * sink])
                q.leg_pose(p, 'h', side, 30 * sink, [60 * sink, 60 * sink, 20 * sink])
            p.turn(q.n('neck0'), Y, 20 * sink).turn(q.n('jaw'), Y, 20 * u)
        return p
    rig.key_all(arm, pose_of)
    return arm
