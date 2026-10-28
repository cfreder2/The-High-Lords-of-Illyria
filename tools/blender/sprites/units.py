"""The pilot's unit types, each a function that builds its figure into the
current scene, rigs it and keys its 26 frames.

UnitType numbers are engine/sim/include/illyria/sim/units.h's; heights are
the sim's (units.cpp)."""

import math

from mathutils import Vector

from . import gear, geo, horse, human, mats, outfit, rig, wings
from .geo import smooth
from .human import X, Y, Z, Stance, keyed
from .rig import Pose

TYPES = {}


def unit(number, name):
    def wrap(f):
        TYPES[name] = (number, f)
        return f
    return wrap


def _parts(col, arm, L, parts):
    for part, subdiv, solid in parts:
        part.make(col, arm, subdiv=subdiv, materials=L, solidify=solid)


def _cycle(t):
    return math.sin(2 * math.pi * t), math.cos(2 * math.pi * t)


def walk_legs(st, t, swing=24.0, knee=48.0, bob=0.018):
    a, c = _cycle(t)
    st.thigh = [st.thigh[0] + swing * a, st.thigh[1] - swing * a]
    # The knee bends as the leg swings through, straight as it lands and pushes.
    st.knee = [6 + knee * max(0.0, math.cos(2 * math.pi * t + 0.6)) ** 1.5, 6 + knee * max(0.0, -math.cos(2 * math.pi * t + 0.6)) ** 1.5]
    st.ankle = [8 * max(0.0, -a), 8 * max(0.0, a)]
    st.root = Vector((0, 0, -bob * (1 - abs(math.cos(2 * math.pi * t))) + bob * 0.3))
    st.hip_twist = 5 * a
    st.twist = -8 * a
    return st


def die_keys(base, arm_fling=60.0):
    """Hit, knees go, falling backward, down."""
    k0 = base.copy()
    k1 = base.copy()
    k1.lean, k1.nod, k1.knee = -12, -15, [25, 18]
    k1.shoulder_out = [x + 20 for x in base.shoulder_out]
    k2 = k1.copy()
    k2.fall, k2.knee, k2.thigh = 30, [45, 30], [20, 10]
    k2.root = Vector((0, 0, -0.08))
    k3 = k2.copy()
    k3.fall, k3.knee, k3.thigh = 65, [30, 20], [25, 15]
    k3.shoulder, k3.shoulder_out = [100, 90], [arm_fling, arm_fling * 0.8]
    k3.root = Vector((0, 0, 0.0))
    k4 = k3.copy()
    k4.fall, k4.knee, k4.thigh, k4.nod = 88, [10, 25], [12, 22], 8
    k4.root = Vector((0, 0, 0.13))
    k4.shoulder, k4.elbow = [130, 110], [20, 30]
    return [(0, k0), (0.2, k1), (0.45, k2), (0.7, k3), (1.0, k4)]


def flinch_keys(base):
    k = base.copy()
    k.lean -= 12
    k.nod -= 14
    k.knee = [x + 12 for x in base.knee]
    k.shoulder_out = [x + 10 for x in base.shoulder_out]
    k.root = Vector((0, 0, -0.03))
    return [(0, k), (1, mixed(base, k, 0.45))]


def mixed(a, b, t):
    return human.mix(a, b, t)


# ---------------------------------------------------------------- knight

@unit(1, 'heavy_infantry')
def heavy_infantry():
    """A knight: mail from head to foot, a great helm, a surcoat and a large
    heater shield in the side's colour, an arming sword."""
    col = __import__('bpy').context.scene.collection
    L = mats.library()
    B = human.Body(1.9, 1.18)
    arm = rig.build('rig', human.skeleton(B), col)
    body = geo.Part('body')
    human.torso(body, B, 'mail', grow=0.004)
    human.neck(body, B, 'mail')
    for side in (1, -1):
        human.arm(body, B, side, 'mail', sleeve=0.024)
        human.leg(body, B, side, 'mail', sleeve=0.016)
    hands = geo.Part('hands')
    for side in (1, -1):
        human.hand(hands, B, side, 'leather_dark', grip=True)
        human.foot(hands, B, side, 'leather_dark')
    head = geo.Part('head')
    human.head(head, B, 'skin')
    outfit.great_helm(head, B)
    gear.plume(head, B, 'team', back=0.32, rise=0.2, width=0.055, at=human.head_center(B) + Vector((0, 0, human.head_radii(B)[2] * 1.15)))
    mail = geo.Part('hauberk')
    outfit.coif(mail, B)
    outfit.skirt(mail, B, 'mail', 1.0, 0.56, (0.105, 0.17), (0.15, 0.21), fold_depth=0.02, seed=3)
    coat = geo.Part('surcoat')
    human.torso(coat, B, 'team', grow=0.024, z_from=0.93, z_to=1.49)
    outfit.skirt(coat, B, 'team', 1.0, 0.44, (0.125, 0.19), (0.17, 0.24), fold_depth=0.07, seed=5, a=(-1.2, 1.2), n=22)
    outfit.skirt(coat, B, 'team', 1.0, 0.44, (0.125, 0.19), (0.17, 0.24), fold_depth=0.07, seed=7, a=(math.pi - 1.2, math.pi + 1.2), n=22)
    kit = geo.Part('kit')
    outfit.belt(kit, B, 'leather_dark', 1.02, 0.172, 0.118, buckle='gold')
    for side in (1, -1):
        sf = 'L' if side > 0 else 'R'
        # Poleyns and couters: plates on the knees and elbows.
        kit.ellipsoid(B.P(0.05, 0.1 * side, 0.53), (0.032 * B.s, 0.05 * B.s, 0.055 * B.s), 'steel', B.w('shin.' + sf, 0.5, 'thigh.' + sf, 0.5), seg=12, rings=8)
        kit.ellipsoid(B.P(-0.035, 0.235 * side, 1.15), (0.04 * B.s, 0.042 * B.s, 0.045 * B.s), 'steel', B.w('upperarm.' + sf, 0.5, 'forearm.' + sf, 0.5), seg=12, rings=8)
    shield = geo.Part('shield')
    outfit.heater_shield(shield, B, height=0.82, width=0.6)
    blade = geo.Part('sword')
    outfit.sword(blade, B, side=-1, blade=0.8)
    _parts(col, arm, L, [(body, 2, 0), (hands, 2, 0), (head, 2, 0), (mail, 2, 0.006), (coat, 2, 0.008), (kit, 1, 0), (shield, 1, 0), (blade, 0, 0)])

    guard = Stance()
    guard.lean = 4
    guard.thigh = [10, -6]
    guard.knee = [12, 10]
    guard.thigh_out = [5, 5]
    guard.shoulder = [32, 22]
    guard.shoulder_out = [24, 14]
    guard.arm_twist = [78, 0]
    guard.elbow = [95, 75]
    guard.wrist = [0, -25]
    guard.twist = -6

    def pose_of(anim, i, n):
        t = i / n
        if anim == 'idle':
            st = guard.copy()
            b = math.sin(2 * math.pi * t)
            st.lean += 1.2 * b
            st.shoulder = [st.shoulder[0] + 2 * b, st.shoulder[1] + 3 * b]
            st.nod = 2 * b
            st.root = Vector((0, 0, -0.004 * (1 + b)))
        elif anim == 'walk':
            st = guard.copy()
            st.thigh = [0, 0]
            st.knee = [6, 6]
            walk_legs(st, t)
            a, _ = _cycle(t)
            st.shoulder = [st.shoulder[0] - 3 * a, st.shoulder[1] + 6 * a]
            st.lean = 6
        elif anim == 'attack':
            up = guard.copy()
            up.shoulder, up.shoulder_out, up.elbow, up.wrist = [30, 150], [22, 45], [95, 110], [0, 35]
            up.twist, up.lean, up.arm_twist = -28, 0, [45, -20]
            hit = guard.copy()
            hit.shoulder, hit.shoulder_out, hit.elbow, hit.wrist = [26, 75], [24, 18], [95, 5], [0, -20]
            hit.twist, hit.lean, hit.thigh, hit.knee = 22, 16, [22, -12], [22, 8]
            follow = hit.copy()
            follow.shoulder, follow.shoulder_out, follow.elbow, follow.wrist, follow.arm_twist = [24, 35], [24, -10], [95, 10], [0, -30], [50, 40]
            follow.twist, follow.lean = 30, 18
            st = keyed([(0, guard), (0.25, up), (0.55, hit), (0.8, follow), (1.0, mixed(follow, guard, 0.6))], i / (n - 1))
        elif anim == 'flinch':
            st = keyed(flinch_keys(guard), i / (n - 1))
        else:
            st = keyed(die_keys(guard), i / (n - 1))
        return human.to_pose(st, B)

    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- archon

@unit(20, 'archons')
def archons():
    """An angel: tall, robed in white with a gilded breastplate, haloed,
    long-haired, a sword in hand, and great feathered wings; the side's
    colour on the sash and the stole hanging before and behind."""
    col = __import__('bpy').context.scene.collection
    L = mats.library()
    B = human.Body(2.4, 0.94)
    bones = human.skeleton(B)
    wl = wings.Wing(B, 1, B.P(-0.1, 0.07, 1.36), scale=1.18)
    wr = wings.Wing(B, -1, B.P(-0.1, -0.07, 1.36), scale=1.18)
    feathers, edges = geo.Part('feathers'), geo.Part('wing_edges')
    for wg in (wl, wr):
        bones += wg.bones()
        wg.build(feathers, bones, edges)
    arm = rig.build('rig', bones, col)
    body = geo.Part('body')
    human.torso(body, B, 'robe', grow=0.006)
    human.neck(body, B, 'skin_pale')
    for side in (1, -1):
        human.arm(body, B, side, 'skin_pale')
        human.leg(body, B, side, 'skin_pale')
        human.foot(body, B, side, 'skin_pale')
        human.hand(body, B, side, 'skin_pale', grip=side < 0)
    head = geo.Part('head')
    human.head(head, B, 'skin_pale')
    human.hair_shell(head, B, 'hair_gold', long=0.3 * B.s)
    glow = geo.Part('halo')
    outfit.halo(glow, B, radius=0.15, tube=0.016)
    robe = geo.Part('robe')
    outfit.skirt(robe, B, lambda j, s: 'gold' if j == 11 else 'robe', 1.0, 0.07, (0.11, 0.17), (0.26, 0.32), rows=12, n=36,
                 fold_depth=0.07, seed=11, grow_pow=1.3, hem=lambda th: 0.03 * math.sin(th * 5))
    for side in (1, -1):
        outfit.sleeve(robe, B, side, 'robe', flare=0.06, end=6, cuff='gold')
    armour = geo.Part('armour')
    outfit.cuirass(armour, B, 'gold', grow=0.03)
    for side in (1, -1):
        outfit.pauldron(armour, B, side, 'gold')
    sash = geo.Part('sash')
    outfit.belt(sash, B, 'team', 1.07, 0.178, 0.122, height=0.07, buckle='gold')
    outfit.skirt(sash, B, 'team', 1.06, 0.3, (0.13, 0.19), (0.215, 0.27), rows=8, n=8, fold_depth=0.02, seed=13, a=(-0.3, 0.3), grow_pow=1.3)
    outfit.skirt(sash, B, 'team', 1.06, 0.36, (0.13, 0.19), (0.215, 0.27), rows=8, n=8, fold_depth=0.02, seed=17, a=(math.pi - 0.32, math.pi + 0.32), grow_pow=1.3)
    blade = geo.Part('sword')
    outfit.sword(blade, B, side=-1, blade=0.92, width=0.03, guard='gold', pommel='gold', guard_len=0.12)
    _parts(col, arm, L, [(body, 2, 0), (head, 2, 0), (glow, 1, 0), (robe, 2, 0.006), (armour, 2, 0), (sash, 2, 0.005),
                         (feathers, 1, 0.003), (edges, 2, 0), (blade, 0, 0)])

    base = Stance()
    base.shoulder = [8, 25]
    base.shoulder_out = [14, 10]
    base.elbow = [30, 80]
    base.wrist = [0, -35]
    base.thigh_out = [2, 2]

    def wings_pose(p, fold, lift, sweep=0.0, twist=0.0, ripple=0.0):
        for wg in (wl, wr):
            wg.pose(p, fold, lift, sweep, twist, ripple=ripple)

    def pose_of(anim, i, n):
        t = i / n
        if anim == 'idle':
            st = base.copy()
            b = math.sin(2 * math.pi * t)
            st.lean = 1.0 * b
            st.root = Vector((0, 0, 0.004 * b))
            p = human.to_pose(st, B)
            # Wings raised and half open, as angels are drawn.
            wings_pose(p, 0.3 - 0.03 * b, 20 + 3 * b, 40, 25, ripple=1.5 * b)
            return p
        if anim == 'walk' and i < 4:
            # On foot (frames 0-3): a stately walk, the wings half folded and raised.
            st = base.copy()
            walk_legs(st, i / 4, swing=20, knee=40)
            a, _ = _cycle(i / 4)
            st.lean = 4
            st.shoulder = [st.shoulder[0] - 6 * a, st.shoulder[1] + 5 * a]
            p = human.to_pose(st, B)
            wings_pose(p, 0.32, 20 + 3 * a, 40, 25, ripple=1.5 * a)
            return p
        if anim == 'walk':
            # Flight (frames 4-7): a full wingbeat, the body leaning in, legs trailing.
            t = (i - 4) / 4
            st = base.copy()
            a, c = _cycle(t)
            st.lean = 14 + 3 * c
            st.thigh, st.knee, st.ankle = [-6 + 4 * a, 6 - 4 * a], [22 + 6 * a, 34 - 6 * a], [35, 30]
            st.root = Vector((0, 0, 0.06 * c))
            st.shoulder = [20 + 6 * c, 35 + 4 * c]
            p = human.to_pose(st, B)
            lift = 8 + 52 * c  # top of the stroke at t = 0
            up = max(0.0, -a)  # the upstroke: the hand folds in
            wings_pose(p, 0.08 + 0.32 * up, lift, -6 + 14 * up, -10 * a, ripple=3 * a)
            return p
        if anim == 'attack':
            up = base.copy()
            up.shoulder, up.shoulder_out, up.elbow, up.wrist = [20, 155], [25, 40], [40, 100], [0, 40]
            up.twist, up.lean, up.arm_twist = -25, -4, [0, -20]
            hit = base.copy()
            hit.shoulder, hit.shoulder_out, hit.elbow, hit.wrist = [30, 80], [20, 15], [30, 5], [0, -20]
            hit.twist, hit.lean = 22, 15
            follow = hit.copy()
            follow.shoulder, follow.shoulder_out, follow.elbow, follow.wrist, follow.arm_twist = [25, 38], [20, -8], [30, 10], [0, -30], [0, 40]
            follow.twist, follow.lean = 30, 18
            u = i / (n - 1)
            st = keyed([(0, base), (0.3, up), (0.55, hit), (0.8, follow), (1.0, mixed(follow, base, 0.6))], u)
            p = human.to_pose(st, B)
            # The wings flare open and high, then beat down with the blow.
            fold = [0.3, 0.15, 0.08, 0.1, 0.2, 0.28][i]
            lift = [20, 40, 50, -15, -30, 5][i]
            wings_pose(p, fold, lift, [40, 10, -8, -5, 10, 30][i], [25, 10, 10, -10, -5, 20][i])
            return p
        if anim == 'flinch':
            st = keyed(flinch_keys(base), i / (n - 1))
            p = human.to_pose(st, B)
            wings_pose(p, [0.45, 0.35][i], [40, 28][i], [30, 36][i], [15, 22][i])
            return p
        st = keyed(die_keys(base), i / (n - 1))
        p = human.to_pose(st, B)
        u = i / (n - 1)
        # Limp: the wings fall open and lie on the ground as the body goes down.
        wings_pose(p, 0.3 + 0.15 * smooth(0.1, 0.8, u), 20 - 45 * smooth(0.1, 1.0, u), 40 - 40 * u, 25 - 25 * u, ripple=4 * u)
        return p

    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- light cavalry

@unit(4, 'light_cavalry')
def light_cavalry():
    """A bay horse, black-legged, saddled over a cloth in the side's colour;
    its rider in mail and a nasal helm, a surcoat in the side's colour,
    sword in hand, reins in the other."""
    col = __import__('bpy').context.scene.collection
    L = mats.library()
    H = horse.Horse(1.55)
    seat = H.seat()
    B0 = human.Body(1.8, 1.0)
    B = human.Body(1.8, 1.0, offset=seat - B0.P(-0.02, 0, 0.97), prefix='r_')
    bones = H.bones() + human.skeleton(B, parent=H.n('spine'))
    arm = rig.build('rig', bones, col)
    hide = geo.Part('horse')
    H.body(hide, 'coat_bay')
    hair = geo.Part('mane')
    H.mane_and_tail(hair)
    tack = geo.Part('tack')
    H.tack(tack)
    H.reins(tack, B.n('hand.L'), human.grip_point(B, 1))
    body = geo.Part('rider')
    human.torso(body, B, 'mail', grow=0.004)
    human.neck(body, B, 'mail')
    for side in (1, -1):
        human.arm(body, B, side, 'mail', sleeve=0.012)
        human.leg(body, B, side, lambda j, s: 'leather' if j >= 4 else 'hose', sleeve=0.006)
    ends = geo.Part('rider_ends')
    for side in (1, -1):
        human.hand(ends, B, side, 'leather', grip=True)
        human.foot(ends, B, side, 'leather_dark')
    head = geo.Part('rider_head')
    human.head(head, B, 'skin')
    outfit.nasal_helm(head, B)
    gear.plume(head, B, 'team', back=0.3)
    mail = geo.Part('rider_mail')
    outfit.coif(mail, B)
    coat = geo.Part('rider_coat')
    human.torso(coat, B, 'team', grow=0.022, z_from=0.93, z_to=1.49)
    # A riding surcoat, split before and behind: its skirts hang over the thighs.
    outfit.skirt(coat, B, 'team', 1.0, 0.66, (0.12, 0.185), (0.16, 0.23), fold_depth=0.05, seed=21, a=(0.35, math.pi - 0.35), n=16, rows=6)
    outfit.skirt(coat, B, 'team', 1.0, 0.66, (0.12, 0.185), (0.16, 0.23), fold_depth=0.05, seed=23, a=(math.pi + 0.35, 2 * math.pi - 0.35), n=16, rows=6)
    outfit.belt(coat, B, 'leather_dark', 1.02, 0.17, 0.115, buckle='gold')
    blade = geo.Part('sword')
    outfit.sword(blade, B, side=-1, blade=0.85)
    _parts(col, arm, L, [(hide, 2, 0), (hair, 1, 0), (tack, 1, 0), (body, 2, 0), (ends, 2, 0), (head, 2, 0), (mail, 2, 0.005), (coat, 2, 0.007), (blade, 0, 0)])

    ride = Stance()
    ride.thigh = [62, 62]
    ride.thigh_out = [24, 24]
    ride.knee = [82, 82]
    ride.ankle = [-12, -12]
    ride.lean = 6
    ride.shoulder = [28, 30]
    ride.shoulder_out = [12, 14]
    ride.elbow = [80, 85]
    ride.arm_twist = [25, 0]
    ride.wrist = [-10, -35]

    def rider(st):
        return human.to_pose(st, B, mounted=True)

    def pose_of(anim, i, n):
        t = i / n
        if anim == 'idle':
            st = ride.copy()
            b = math.sin(2 * math.pi * t)
            st.lean += 1.5 * b
            st.root = Vector((0, 0, 0.004 * b))
            p = rider(st)
            p.turn(H.n('neck1'), Y, 4 + 3 * b).turn(H.n('head'), Y, 2 * b)
            p.turn(H.n('tail1'), Y, 6).turn(H.n('tail2'), Z, 8 * b).turn(H.n('tail3'), Z, 10 * b)
            H.leg_pose(p, 'h', -1, -4, 12, 6, 10)  # resting a hind foot
            return p
        if anim == 'walk':
            a, c = _cycle(t)
            st = ride.copy()
            # The rider sits the gallop: forward, rising a little out of the saddle.
            st.lean = 14 + 3 * c
            st.root = Vector((0.02, 0, 0.03 + 0.025 * c))
            st.knee = [78 - 4 * c, 78 - 4 * c]
            st.shoulder = [32, 40 + 5 * a]
            p = rider(st)
            return H.gallop(p, t)
        if anim == 'attack':
            up = ride.copy()
            up.shoulder, up.shoulder_out, up.elbow, up.wrist = [28, 160], [12, 50], [80, 110], [-10, 40]
            up.twist, up.lean, up.side_lean = -25, 0, 6
            cut = ride.copy()
            cut.shoulder, cut.shoulder_out, cut.elbow, cut.wrist = [28, 60], [12, 40], [80, 10], [-10, -10]
            cut.twist, cut.lean, cut.side_lean = 20, 18, -14
            follow = cut.copy()
            follow.shoulder, follow.shoulder_out, follow.elbow = [28, 20], [12, 30], [80, 15]
            follow.twist, follow.side_lean = 28, -18
            u = i / (n - 1)
            st = keyed([(0, ride), (0.3, up), (0.55, cut), (0.8, follow), (1.0, mixed(follow, ride, 0.6))], u)
            p = rider(st)
            # The horse checks and half rears under the blow.
            r = math.sin(math.pi * u)
            p.turn(H.n('spine'), Y, -6 * r).turn(H.n('neck1'), Y, -10 * r).turn(H.n('head'), Y, 6 * r)
            for side in (1, -1):
                H.leg_pose(p, 'f', side, 18 * r * (1 if side > 0 else 0.6), 60 * r, 20 * r, 25 * r)
                H.leg_pose(p, 'h', side, 10 * r, 10 * r, 12 * r, 0)
            p.turn(H.n('tail1'), Y, 15 * r)
            return p
        if anim == 'flinch':
            st = keyed(flinch_keys(ride), i / (n - 1))
            st.thigh, st.knee, st.thigh_out = ride.thigh, ride.knee, ride.thigh_out
            p = rider(st)
            k = [1.0, 0.5][i]
            p.turn(H.n('neck1'), Y, -14 * k).turn(H.n('head'), Y, -12 * k).turn(H.n('spine'), Y, -3 * k)
            H.leg_pose(p, 'f', 1, 8 * k, 30 * k, 10 * k, 10 * k)
            p.turn(H.n('tail1'), Y, 20 * k)
            return p
        # Down: the forelegs buckle, the horse sinks and rolls onto its side, the rider with it.
        u = i / (n - 1)
        st = ride.copy()
        st.lean = 6 - 30 * smooth(0.0, 0.6, u)
        st.nod = -10 * u
        st.shoulder_out = [12 + 40 * u, 14 + 50 * u]
        st.shoulder = [28 + 40 * u, 30 + 30 * u]
        p = rider(st)
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

    rig.key_all(arm, pose_of)
    return arm
