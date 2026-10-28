"""War elephants, griffins and dragons (quad.py), giant spiders, and the
catapult."""

import math

import bpy
from mathutils import Vector

from . import gear, human, mats, outfit, people, rig, wings
from .batwing import BatWing
from .beasts import make, paws
from .geo import Part, Rand, blend, smooth
from .human import X, Y, Z, Stance, keyed
from .quad import Adapter, Quad, bound_phases, walk_phases
from .rig import Bone, Pose
from .units import unit

AX = (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))


def _cycle(t):
    return math.sin(2 * math.pi * t), math.cos(2 * math.pi * t)


def chain_bones(q, name, pts, parent):
    out = []
    for i in range(len(pts) - 1):
        out.append(Bone(q.n('%s%d' % (name, i)), q.P(*pts[i]), q.P(*pts[i + 1]), q.n(parent) if i == 0 else q.n('%s%d' % (name, i - 1)), (0, 0, 1)))
    return out


def chain_weights(q, name, n, first_parent):
    """Weights for n + 1 points along a chain of n bones (blended at the joints)."""
    ws = []
    for k in range(n + 1):
        if k == 0:
            ws.append(blend(q.w(first_parent), q.w('%s0' % name), 0.5))
        elif k == n:
            ws.append(q.w('%s%d' % (name, n - 1)))
        else:
            ws.append(blend(q.w('%s%d' % (name, k - 1)), q.w('%s%d' % (name, k)), 0.5))
    return ws


def collar(q, part, x, mat='team', grow=0.03, width=0.06, studs='gold'):
    """A band round the neck at the neck row nearest x (a war-beast's collar)."""
    rows = q.S['neck']
    best = min(range(len(rows)), key=lambda i: abs((rows[i][0][0] + rows[i][1][0]) / 2 - x))
    (tx, tz), (bx, bz), hw = rows[best]
    top, bot = q.P(tx, 0, tz), q.P(bx, 0, bz)
    u = (top - bot).normalized()
    c = (top + bot) / 2
    axis = u.cross(Vector((0, 1, 0))).normalized()
    rings = []
    for d in (-width, width):
        rings.append({'c': c + axis * d * q.k, 'u': u, 'v': Vector((0, 1, 0)), 'ru': (top - bot).length / 2 + grow * q.k, 'rv': hw * q.k + grow * q.k, 'w': q.neck_weights(best)})
    part.loft(rings, mat, n=20, cap0=False, cap1=False)
    if studs:
        for k in range(8):
            a = 2 * math.pi * k / 8
            p = c + (u * math.cos(a) * ((top - bot).length / 2 + grow * q.k * 1.4) + Vector((0, 1, 0)) * math.sin(a) * (hw + grow * 1.4) * q.k)
            part.ellipsoid(p, (0.025 * q.k,) * 3, studs, q.neck_weights(best), seg=6, rings=4)


def back_cloth(q, part, x0, x1, half_angle, mat='team', trim='gold', out=0.02):
    def fn(s, t):
        x = x0 + (x1 - x0) * t
        p, _ = q.trunk_point(x, (2 * s - 1) * half_angle, out=out)
        return p
    part.grid(fn, 14, 10, lambda s, t: trim if abs(2 * s - 1) > 0.9 or t > 0.93 or t < 0.07 else mat, lambda s, t: q.trunk_weights(x0 + (x1 - x0) * t))


# ---------------------------------------------------------------- elephants

ELEPHANT = {
    'trunk': [(1.25, 2.75, 1.45, 0.6), (1.0, 3.05, 1.2, 0.78), (0.6, 3.1, 1.05, 0.88), (0.1, 3.0, 1.0, 0.92), (-0.4, 2.95, 1.05, 0.9),
              (-0.9, 2.85, 1.2, 0.82), (-1.25, 2.55, 1.45, 0.62), (-1.42, 2.2, 1.75, 0.32)],
    'neck': [((1.05, 2.95), (1.15, 1.5), 0.72), ((1.3, 2.98), (1.35, 1.75), 0.62), ((1.45, 3.0), (1.55, 2.05), 0.52)],
    'head_bone': ((1.5, 2.8), (2.1, 2.4)),
    'neck_bones': 1,
    'legs': {'f': [(0.85, 0.52, 2.1), (0.9, 0.52, 1.35), (0.88, 0.52, 0.62), (0.9, 0.52, 0.22), (0.93, 0.52, 0.0)],
             'h': [(-0.95, 0.52, 2.1), (-0.85, 0.52, 1.3), (-0.95, 0.52, 0.62), (-0.93, 0.52, 0.22), (-0.9, 0.52, 0.0)]},
    'radii': {'f': [(0.36, 0.32), (0.29, 0.27), (0.25, 0.24), (0.26, 0.25), (0.29, 0.28)],
              'h': [(0.4, 0.34), (0.31, 0.28), (0.26, 0.25), (0.26, 0.25), (0.29, 0.28)]},
    'mass': {'f': ((0.85, 0.45, 2.15), (0.42, 0.25, 0.5)), 'h': ((-0.95, 0.45, 2.15), (0.48, 0.28, 0.55))},
    'tail': [(-1.38, 0, 2.35), (-1.46, 0, 1.95), (-1.5, 0, 1.5)],
    'fold_signs': {'f': [-1, 1, 1], 'h': [1, -1, 1]},
    'back_wide': 0.12,
}
TRUNK_PTS = [(2.05, 0, 2.45), (2.18, 0, 1.95), (2.24, 0, 1.4), (2.22, 0, 0.9), (2.32, 0, 0.55)]


def elephant_head(q, part):
    w = q.w('head')
    P, k = q.P, q.k
    part.ellipsoid(P(1.68, 0, 2.72), (0.52 * k, 0.5 * k, 0.6 * k), 'hide_elephant', w, seg=20, rings=14)
    part.ellipsoid(P(1.88, 0, 2.95), (0.3 * k, 0.36 * k, 0.32 * k), 'hide_elephant', w, seg=14, rings=8)
    tw = chain_weights(q, 'trunk', 4, 'head')
    part.chain([q.P(*p) for p in TRUNK_PTS], [0.24 * k, 0.17 * k, 0.13 * k, 0.1 * k, 0.085 * k], 'hide_elephant', tw, hint=Vector((1, 0, 0)), n=14,
               shape=lambda i, th: 1.0 + 0.04 * math.sin(th * 2))
    for side in (1, -1):
        part.chain([P(1.98, 0.27 * side, 2.25), P(2.3, 0.36 * side, 1.88), P(2.62, 0.4 * side, 1.84), P(2.82, 0.38 * side, 2.02)],
                   [0.075 * k, 0.06 * k, 0.045 * k, 0.012 * k], 'ivory', [w] * 4, hint=Vector((0, 0, 1)), n=10)
        part.ellipsoid(P(1.98, 0.36 * side, 2.82), (0.04 * k, 0.03 * k, 0.03 * k), 'horse_eye', w, seg=8, rings=5)

        def ear(s, t, side=side):
            # s: front (0) to back (1); t: top (0) to the hanging tip (1).
            x = 1.62 - 0.6 * s * (1 - 0.3 * t)
            z = 3.1 - 1.05 * t * (0.75 + 0.25 * math.sin(math.pi * s))
            y = side * (0.5 + 0.12 * s + 0.05 * math.sin(math.pi * t))
            return P(x, y, z)
        first = part.slab(ear, 8, 8, 0.03 * k, 'hide_elephant', w)
        if side < 0:
            human._flip_since(part, first)


@unit(13, 'elephants')
def elephants():
    """A war elephant: grey and wrinkled, long tusks, a caparison and a
    canopied howdah in the side's colour, a mahout on its neck and a spearman
    in the howdah."""
    col = bpy.context.scene.collection
    L = mats.library()
    q = Quad(ELEPHANT, k=0.88, prefix='e_')
    bones = q.bones() + chain_bones(q, 'trunk', TRUNK_PTS, 'head')
    # The crew: a mahout astride the neck, a spearman standing in the howdah.
    neck_top = q.P(1.2, 0, 3.0) + Vector((0, 0, 0.06))
    M0 = human.Body(1.7, 0.95)
    M = human.Body(1.7, 0.95, offset=neck_top - M0.P(-0.02, 0, 0.97), prefix='m_')
    floor = q.trunk_point(-0.05, 0.0, out=0.1)[0]
    A = human.Body(1.75, 1.0, offset=floor, prefix='a_')
    kit = people.Kit('spear', strike='thrust')
    bones += human.skeleton(M, parent=q.n('chest')) + human.skeleton(A, parent=q.n('spine'))
    g = human.grip_point(A, -1)
    bones.append(Bone(A.n('grip2'), g + Vector((0.4 * A.s, 0, 0)), g + Vector((0.45 * A.s, 0, 0)), A.n('hand.R'), (0, 0, 1)))
    p0 = A.P(-0.15, 0.6, 0.95)
    bones.append(Bone(A.n('pole.L'), p0, p0 + Vector((0, 0, 0.05)), A.n('chest'), (1, 0, 0)))
    arm = rig.build('rig', bones, col)
    rig.ik(arm, A.n('forearm.L'), A.n('grip2'), A.n('pole.L'), chain=2, pole_angle=-90)
    hide = Part('elephant')
    q.body(hide, 'hide_elephant')
    elephant_head(q, hide)
    for side in (1, -1):
        for leg in ('f', 'h'):
            sf = 'L' if side > 0 else 'R'
            c = q.joint(leg, 4, side)
            ww = q.w('%s3.%s' % (leg, sf))
            hide.chain([c + Vector((0, 0, 0.0)), c + Vector((0, 0, 0.14 * q.k))], [0.3 * q.k, 0.28 * q.k], 'hide_elephant', [ww, ww], hint=Vector((1, 0, 0)), n=14)
            for t in range(3):
                a = -0.5 + 0.5 * t
                hide.ellipsoid(c + Vector((0.27 * q.k * math.cos(a), 0.27 * q.k * math.sin(a), 0.05 * q.k)), (0.05 * q.k, 0.06 * q.k, 0.045 * q.k), 'ivory', ww, seg=8, rings=5)
    tl = Part('tail')
    tp = [q.P(*p) for p in ELEPHANT['tail']]
    tl.chain(tp, [0.05 * q.k, 0.035 * q.k, 0.02 * q.k], 'hide_elephant', chain_weights(q, 'tail', 2, 'spine'), hint=Vector((1, 0, 0)), n=8)
    tl.ellipsoid(tp[-1], (0.05 * q.k, 0.05 * q.k, 0.12 * q.k), 'hair_black', q.w('tail1'), seg=8, rings=5)
    # The caparison over the back, the howdah on it.
    cap = Part('caparison')

    def cloth(s, t):
        x = 0.75 - 1.4 * t
        th = (2 * s - 1) * 2.1
        tt = max(-1.45, min(1.45, th))
        p, nrm = q.trunk_point(x, tt, out=0.03)
        if abs(th) > 1.45:
            drop = (abs(th) - 1.45) / 0.65
            p = Vector((p.x, p.y, p.z + (1.85 * q.k - p.z) * drop))
        return p
    cap.grid(cloth, 22, 14, lambda s, t: 'gold' if abs(2 * s - 1) > 0.94 else 'team', lambda s, t: q.trunk_weights(0.75 - 1.4 * t))
    how = Part('howdah')
    wsp = q.trunk_weights(-0.05)
    fz = floor.z - 0.02
    hx, hy = 0.55 * q.k, 0.6 * q.k
    cx = floor.x
    how.box(Vector((cx, 0, fz - 0.04)), AX, (hx, hy, 0.05), 'wood_dark', wsp)
    for sx in (-1, 1):
        for sy in (-1, 1):
            how.chain([Vector((cx + sx * hx, sy * hy, fz)), Vector((cx + sx * hx, sy * hy, fz + 1.0 * q.k))], [0.03, 0.03], 'gold', [wsp, wsp], hint=Vector((1, 0, 0)), n=6)
    for sy in (-1, 1):
        how.box(Vector((cx, sy * hy, fz + 0.22)), AX, (hx, 0.025, 0.2), 'wood', wsp)
        how.box(Vector((cx, sy * (hy + 0.03), fz + 0.22)), AX, (hx * 0.8, 0.01, 0.14), 'team', wsp)
    for sx in (-1, 1):
        how.box(Vector((cx + sx * hx, 0, fz + 0.22)), AX, (0.025, hy, 0.2), 'wood', wsp)
    # The canopy: a peaked roof in the side's colour with a gold finial.
    top = fz + 1.0 * q.k

    def roof(s, t):
        a = 2 * math.pi * s
        r = 1.0 - t
        return Vector((cx + math.cos(a) * (hx + 0.12) * r * 1.05, math.sin(a) * (hy + 0.12) * r * 1.05, top + 0.35 * t ** 0.8 * q.k))
    how.grid(roof, 16, 4, 'team', wsp)
    how.ellipsoid(Vector((cx, 0, top + 0.38 * q.k)), (0.06, 0.06, 0.08), 'gold', wsp, seg=8, rings=5)
    # The mahout: bare-armed, a turban in the side's colour, a goad.
    crew = Part('crew')
    human.torso(crew, M, 'linen')
    human.neck(crew, M, 'skin')
    for side in (1, -1):
        human.arm(crew, M, side, 'skin')
        human.leg(crew, M, side, 'linen')
        human.hand(crew, M, side, 'skin', grip=True)
        human.foot(crew, M, side, 'skin')
    mh = Part('crew_heads')
    human.head(mh, M, 'skin')
    mh.ellipsoid(human.head_center(M) + Vector((0, 0, 0.06)), tuple(r * 1.2 for r in human.head_radii(M)), 'team', M.w('head'), seg=16, rings=10)
    gear.spear(crew, M, side=-1, length=1.0, butt=0.2, head=0.12, width=0.02)
    # The spearman.
    human.torso(crew, A, 'mail')
    human.neck(crew, A, 'mail')
    for side in (1, -1):
        human.arm(crew, A, side, 'mail', sleeve=0.012)
        human.leg(crew, A, side, 'hose')
        human.hand(crew, A, side, 'leather', grip=True)
        human.foot(crew, A, side, 'leather_dark')
    human.head(mh, A, 'skin')
    gear.kettle_hat(mh, A)
    gear.tabard(crew, A, 'team', z_bottom=0.62)
    gear.spear(crew, A, side=-1, length=2.6, butt=0.8, head=0.26)
    make(col, arm, L, [(hide, 2, 0), (tl, 1, 0), (cap, 2, 0.01), (how, 1, 0), (crew, 2, 0), (mh, 2, 0)])

    seat = Stance(thigh=[60, 60], thigh_out=[35, 35], knee=[70, 70], shoulder=[25, 30], elbow=[70, 80], wrist=[0, -20], lean=8)
    guard = people.stance(kit)

    def trunk_pose(p, curl, sway=0.0):
        for k in range(4):
            p.turn(q.n('trunk%d' % k), Y, -curl * (0.5 + 0.3 * k)).turn(q.n('trunk%d' % k), Z, sway)

    def pose_of(anim, i, n):
        t = i / n
        u = i / (n - 1)
        crew_st = people.stance_at(kit, guard, anim if anim == 'attack' else 'idle', i if anim == 'attack' else i % 4, n if anim == 'attack' else 4)
        if anim == 'die':
            crew_st = keyed(people.flinch_keys(guard), min(1.0, u))
        crew_st.thigh, crew_st.knee = [0, 0], [6, 6]
        p = human.to_pose(crew_st, A, mounted=True)
        human.to_pose(seat, M, pose=p, mounted=True)
        if anim == 'idle':
            b = math.sin(2 * math.pi * t)
            trunk_pose(p, 6 + 4 * b, 4 * b)
            p.turn(q.n('head'), Y, 2 * b)
            p.turn(q.n('tail0'), Z, 10 * b)
        elif anim == 'walk':
            q.gait(p, t, walk_phases(), stance=0.62, reach=(14, 12), lift=(40, 35), pitch=1.5, bob=0.04,
                   upper_fold={'f': (0.5, 1.0, 0.4), 'h': (0.6, 0.8, 0.4)})
            a, _ = _cycle(t)
            trunk_pose(p, 4, 8 * a)
            p.turn(q.n('head'), Z, 3 * a)
        elif anim == 'attack':
            # Head and trunk up, then a sweep of the tusks down and forward.
            up = [0, 1.0, 0.8, -0.6, -0.4, 0.0][i]
            p.turn(q.n('head'), Y, -14 * up).turn(q.n('neck0'), Y, -6 * up)
            trunk_pose(p, 30 * max(0.0, up) + 5, 0)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 10 * max(0.0, up) * (1 if side > 0 else 0.3), [0, 40 * max(0.0, up) * (1 if side > 0 else 0), 0])
        elif anim == 'flinch':
            kk = [1.0, 0.5][i]
            p.turn(q.n('head'), Y, -10 * kk)
            trunk_pose(p, 25 * kk)
        else:
            sink = smooth(0.0, 0.8, u)
            roll = 75 * smooth(0.35, 1.0, u)
            p.move(q.n('root'), (0, 0, -1.0 * q.k * sink + 0.55 * q.k * smooth(0.35, 1.0, u)))
            p.turn(q.n('root'), X, roll)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 20 * sink, [0, 70 * sink, 20 * sink])
                q.leg_pose(p, 'h', side, 15 * sink, [50 * sink, 50 * sink, 10 * sink])
            trunk_pose(p, -10 * sink)
        return p
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- griffins

GRIFFIN = {
    'trunk': [(0.62, 1.32, 0.86, 0.19), (0.45, 1.42, 0.76, 0.25), (0.2, 1.36, 0.72, 0.26), (-0.1, 1.25, 0.77, 0.22), (-0.4, 1.22, 0.8, 0.22),
              (-0.62, 1.18, 0.85, 0.18), (-0.72, 1.1, 0.93, 0.08)],
    'neck': [((0.52, 1.45), (0.7, 1.0), 0.19), ((0.7, 1.66), (0.85, 1.26), 0.15), ((0.82, 1.84), (0.95, 1.52), 0.12)],
    'head_bone': ((0.9, 1.8), (1.25, 1.75)),
    'legs': {'f': [(0.5, 0.15, 1.18), (0.56, 0.17, 0.8), (0.5, 0.17, 0.45), (0.54, 0.17, 0.08), (0.6, 0.17, 0.02)],
             'h': [(-0.48, 0.16, 1.1), (-0.32, 0.18, 0.72), (-0.55, 0.17, 0.38), (-0.52, 0.17, 0.08), (-0.45, 0.17, 0.02)]},
    'radii': {'f': [(0.1, 0.07), (0.07, 0.06), (0.035, 0.03), (0.03, 0.026), (0.03, 0.026)],
              'h': [(0.15, 0.08), (0.09, 0.07), (0.05, 0.045), (0.04, 0.035), (0.045, 0.04)]},
    'mass': {'f': ((0.5, 0.14, 1.05), (0.13, 0.07, 0.18)), 'h': ((-0.46, 0.14, 1.0), (0.2, 0.08, 0.22))},
    'leg_mats': {'f': ['feather', 'feather', 'beak', 'beak'], 'h': ['tawny'] * 4},
    'tail': [(-0.7, 0, 1.1), (-0.9, 0, 0.95), (-1.1, 0, 0.75), (-1.3, 0, 0.62)],
    'fold_signs': {'f': [-1, 1, 1], 'h': [1, -1, 1]},
    'paw_mat': 'tawny',
}


def griffin_head(q, part):
    w = q.w('head')
    P, k = q.P, q.k
    part.ellipsoid(P(0.96, 0, 1.85), (0.17 * k, 0.13 * k, 0.14 * k), 'feather', w, seg=18, rings=12,
                   shape=lambda v: 1.0 + 0.12 * smooth(0.2, -0.9, v.x) * smooth(-0.2, 0.6, v.z))
    # The hooked beak.
    pts = [P(1.06, 0, 1.84), P(1.18, 0, 1.83), P(1.27, 0, 1.78), P(1.3, 0, 1.7)]
    part.chain(pts, [(0.07 * k, 0.06 * k), (0.055 * k, 0.045 * k), (0.035 * k, 0.03 * k), (0.006, 0.006)], 'beak', [w] * 4, hint=Vector((0, 0, 1)), n=10)
    part.chain([P(1.06, 0, 1.78), P(1.18, 0, 1.76), P(1.24, 0, 1.74)], [(0.03 * k, 0.05 * k), (0.022 * k, 0.035 * k), (0.005, 0.01)], 'beak', [w] * 3, hint=Vector((0, 0, 1)), n=8)
    for side in (1, -1):
        part.ellipsoid(P(1.04, 0.09 * side, 1.89), (0.025 * k, 0.018 * k, 0.025 * k), 'eye_red', w, seg=8, rings=5)
        # Ear tufts swept back.
        b = P(0.88, 0.08 * side, 1.95)
        part.chain([b, b + Vector((-0.1, 0.03 * side, 0.06)) * k, b + Vector((-0.2, 0.05 * side, 0.08)) * k], [(0.03 * k, 0.012 * k), (0.022 * k, 0.008 * k), (0.003, 0.002)],
                   'feather', [w] * 3, hint=Vector((0, 0, 1)), n=6)


def talons(q, part, leg, mat='talon', toe_mat='beak', size=0.05):
    for side in (1, -1):
        sf = 'L' if side > 0 else 'R'
        w = q.w('%s3.%s' % (leg, sf))
        c = q.joint(leg, 4, side)
        for a in (-0.5, 0.0, 0.5, math.pi):
            d = Vector((math.cos(a), math.sin(a), 0))
            L = size * (2.4 if a != math.pi else 1.4) * q.k
            part.chain([c + Vector((0, 0, 0.03 * q.k)), c + d * L * 0.6 + Vector((0, 0, 0.02 * q.k)), c + d * L],
                       [0.018 * q.k, 0.014 * q.k, 0.01 * q.k], toe_mat, [w] * 3, hint=Vector((0, 0, 1)), n=6)
            part.chain([c + d * L, c + d * (L + 0.04 * q.k) - Vector((0, 0, 0.02 * q.k))], [0.01 * q.k, 0.002], mat, [w, w], hint=Vector((0, 0, 1)), n=5)


def flier_pose(anim, i, n):
    """(on_ground, t) for a flier's frame: walk frames 0-3 on foot, 4-7 a wingbeat."""
    if anim == 'walk':
        return (i < 4), ((i % 4) / 4)
    return True, i / n


@unit(11, 'griffins')
def griffins():
    """A griffin: an eagle's white head, hooked golden beak and feathered
    forelegs with talons, a lion's tawny body, hind legs and tufted tail,
    broad brown wings; a war collar and harness in the side's colour."""
    col = bpy.context.scene.collection
    L = mats.library()
    q = Quad(GRIFFIN, k=1.0, prefix='g_')
    WA = Adapter(q, s=1.0)
    wl = wings.Wing(WA, 1, q.P(0.32, 0.2, 1.4), scale=1.45, mat='feather_brown', edge_mat='feather_brown')
    wr = wings.Wing(WA, -1, q.P(0.32, -0.2, 1.4), scale=1.45, mat='feather_brown', edge_mat='feather_brown')
    bones = q.bones()
    feathers, edges = Part('feathers'), Part('wing_edges')
    for wg in (wl, wr):
        bones += wg.bones()
        wg.build(feathers, bones, edges)
    arm = rig.build('rig', bones, col)
    hide = Part('griffin')
    q.body(hide, 'tawny')
    griffin_head(q, hide)
    neck = Part('neck_feathers')
    # The white feathered neck and breast over the lion's body.
    rows = GRIFFIN['neck']
    rings = []
    for idx, ((tx, tz), (bx, bz), hw) in enumerate(rows):
        top, bot = q.P(tx, 0, tz), q.P(bx, 0, bz)
        rings.append({'c': (top + bot) / 2, 'u': (top - bot).normalized(), 'v': Vector((0, 1, 0)), 'ru': (top - bot).length / 2 + 0.02, 'rv': hw * q.k + 0.02,
                      'w': q.neck_weights(idx), 'shape': lambda th: 1.0 + 0.05 * math.sin(th * 10)})
    neck.loft(rings, 'feather', n=20, cap0=False, cap1=False)
    neck.ellipsoid(q.P(0.6, 0, 1.05), (0.18, 0.2, 0.26), 'feather', q.w('chest'), seg=14, rings=8)
    talons(q, hide, 'f')
    paws(q, hide, mat='talon', size=0.045)
    tl = Part('tail')
    tp = [q.P(*p) for p in GRIFFIN['tail']]
    tl.chain(tp, [0.035, 0.03, 0.025, 0.02], 'tawny', chain_weights(q, 'tail', 3, 'spine'), hint=Vector((0, 0, 1)), n=8)
    tl.ellipsoid(tp[-1] + Vector((-0.04, 0, -0.02)), (0.08, 0.05, 0.05), 'fur', q.w('tail2'), seg=10, rings=6)
    harness = Part('harness')
    collar(q, harness, 0.62, grow=0.035, width=0.05)
    back_cloth(q, harness, 0.15, -0.35, 1.25)
    make(col, arm, L, [(hide, 2, 0), (neck, 2, 0), (tl, 1, 0), (harness, 1, 0.008), (feathers, 1, 0.003), (edges, 2, 0)])

    def wings_pose(p, fold, lift, sweep=0.0, twist=0.0, ripple=0.0):
        for wg in (wl, wr):
            wg.pose(p, fold, lift, sweep, twist, ripple=ripple)

    def tail(p, a, up):
        for k in range(3):
            p.turn(q.n('tail%d' % k), Z, a * (0.5 + 0.3 * k)).turn(q.n('tail%d' % k), Y, -up)

    def pose_of(anim, i, n):
        p = Pose()
        ground, t = flier_pose(anim, i, n)
        u = i / (n - 1)
        a, c = _cycle(t)
        if anim == 'idle':
            p.turn(q.n('head'), Z, 8 * a).turn(q.n('neck1'), Y, 2 * c)
            tail(p, 10 * a, 5)
            wings_pose(p, 0.8, 5 + 2 * a, 60, 10, ripple=a)
        elif anim == 'walk' and ground:
            q.gait(p, t, walk_phases(), stance=0.6, reach=(16, 14), lift=(45, 40), pitch=2, bob=0.02)
            tail(p, 8 * a, 10)
            wings_pose(p, 0.8, 5, 60, 10)
        elif anim == 'walk':
            # Flight: forelegs drawn up, hind legs trailing, a full wingbeat.
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 30, [-20, 90, 60])
                q.leg_pose(p, 'h', side, -40, [10, -20, 10])
            p.move(q.n('root'), (0, 0, 0.1 * c))
            p.turn(q.n('neck0'), Y, -10).turn(q.n('head'), Y, 10)
            tail(p, 0, 25)
            up = max(0.0, -a)
            wings_pose(p, 0.08 + 0.32 * up, 8 + 52 * c, -6 + 14 * up, -10 * a, ripple=3 * a)
        elif anim == 'attack':
            # Rears, wings flared, and strikes down with the talons and beak.
            r = [0.0, 0.8, 1.0, 0.4, 0.1, 0.0][i]
            s = [0.0, 0.0, 0.3, 1.0, 0.6, 0.2][i]
            p.move(q.n('root'), (0.1 * s, 0, 0))
            p.turn(q.n('spine'), Y, -18 * r).turn(q.n('chest'), Y, -10 * r + 10 * s)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 50 * r - 10 * s, [-30 * r, 60 * r, 40 * r])
                q.leg_pose(p, 'h', side, -10 * r, [10 * r, -10 * r, 0])
            p.turn(q.n('neck0'), Y, 10 * s - 10 * r).turn(q.n('head'), Y, 20 * s)
            wings_pose(p, [0.8, 0.3, 0.1, 0.15, 0.4, 0.75][i], [5, 30, 45, 0, -10, 5][i], [60, 10, 0, 0, 30, 55][i], 10)
            tail(p, 0, 15 * r)
        elif anim == 'flinch':
            kk = [1.0, 0.5][i]
            p.turn(q.n('neck0'), Y, -15 * kk).turn(q.n('head'), Y, -10 * kk)
            wings_pose(p, 0.5, -20 * kk - 40, 10, 0)
        else:
            sink = smooth(0.1, 0.8, u)
            roll = 80 * smooth(0.35, 1.0, u)
            p.move(q.n('root'), (0, 0, -0.5 * sink + 0.25 * smooth(0.35, 1.0, u)))
            p.turn(q.n('root'), X, roll)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, -10 * sink, [20 * sink, 80 * sink, 40 * sink])
                q.leg_pose(p, 'h', side, 30 * sink, [50 * sink, 50 * sink, 20 * sink])
            p.turn(q.n('neck0'), Y, 25 * sink)
            wings_pose(p, 0.8 - 0.5 * smooth(0.1, 0.7, u), 5 - 30 * u, 60 - 40 * u, 10, ripple=3 * u)
        return p
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- dragons

DRAGON = {
    'trunk': [(1.0, 2.05, 1.25, 0.42), (0.6, 2.3, 1.0, 0.62), (0.1, 2.25, 0.95, 0.68), (-0.4, 2.1, 1.0, 0.62), (-0.9, 2.0, 1.1, 0.52),
              (-1.3, 1.85, 1.25, 0.38), (-1.6, 1.7, 1.38, 0.24)],
    'neck': [((0.95, 2.2), (1.1, 1.45), 0.38), ((1.25, 2.62), (1.48, 2.05), 0.29), ((1.45, 3.06), (1.72, 2.6), 0.23), ((1.62, 3.42), (1.9, 3.05), 0.19)],
    'neck_bones': 3,
    'head_bone': ((1.8, 3.3), (2.6, 3.15)),
    'jaw': ((1.85, 3.15), (2.55, 3.02)),
    'legs': {'f': [(0.65, 0.5, 1.55), (0.82, 0.68, 1.0), (0.68, 0.66, 0.45), (0.78, 0.64, 0.08), (0.95, 0.64, 0.02)],
             'h': [(-0.95, 0.5, 1.55), (-0.62, 0.68, 0.95), (-1.0, 0.64, 0.45), (-0.95, 0.62, 0.08), (-0.78, 0.62, 0.02)]},
    'radii': {'f': [(0.22, 0.17), (0.16, 0.14), (0.11, 0.1), (0.09, 0.08), (0.1, 0.09)],
              'h': [(0.3, 0.2), (0.2, 0.16), (0.12, 0.11), (0.1, 0.09), (0.11, 0.1)]},
    'mass': {'f': ((0.65, 0.45, 1.4), (0.3, 0.18, 0.36)), 'h': ((-0.95, 0.45, 1.4), (0.42, 0.22, 0.45))},
    'tail': [(-1.55, 0, 1.6), (-2.1, 0, 1.3), (-2.7, 0, 0.95), (-3.3, 0, 0.6), (-3.9, 0, 0.35), (-4.5, 0, 0.25)],
    'fold_signs': {'f': [-1, 1, 1], 'h': [1, -1, 1]},
    'paw_mat': 'scale_green',
}


def dragon_head(q, part):
    w, wj = q.w('head'), q.w('jaw')
    P, k = q.P, q.k
    # Skull and snout, brow ridges, the lower jaw on its own bone; teeth, horns, glowing eyes.
    part.chain([P(1.78, 0, 3.32), P(2.05, 0, 3.3), P(2.35, 0, 3.22), P(2.62, 0, 3.13)],
               [(0.2 * k, 0.21 * k), (0.16 * k, 0.17 * k), (0.11 * k, 0.12 * k), (0.07 * k, 0.08 * k)], 'scale_green', [w] * 4, hint=Vector((0, 0, 1)), n=14, dome=0.5)
    part.chain([P(1.85, 0, 3.12), P(2.15, 0, 3.08), P(2.55, 0, 3.02)], [(0.08 * k, 0.16 * k), (0.06 * k, 0.12 * k), (0.04 * k, 0.07 * k)], 'scale_belly', [wj] * 3,
               hint=Vector((0, 0, 1)), n=12)
    part.chain([P(1.95, 0, 3.16), P(2.5, 0, 3.08)], [(0.03 * k, 0.1 * k), (0.02 * k, 0.06 * k)], 'mouth', [wj, wj], hint=Vector((0, 0, 1)), n=10)
    for side in (1, -1):
        for t in range(5):
            x = 2.05 + 0.12 * t
            part.chain([P(x, 0.1 * side * (1 - 0.1 * t), 3.17 - 0.02 * t), P(x + 0.01, 0.1 * side * (1 - 0.1 * t), 3.1 - 0.02 * t)], [0.016 * k, 0.002], 'teeth', [w, w], hint=Vector((1, 0, 0)), n=5)
        part.ellipsoid(P(2.0, 0.17 * side, 3.4), (0.06 * k, 0.03 * k, 0.03 * k), 'eye_glow', w, seg=8, rings=5)
        part.chain([P(1.9, 0.14 * side, 3.45), P(1.7, 0.22 * side, 3.62), P(1.45, 0.26 * side, 3.7), P(1.3, 0.25 * side, 3.66)],
                   [0.06 * k, 0.045 * k, 0.025 * k, 0.004], 'horn', [w] * 4, hint=Vector((0, 0, 1)), n=8)
        part.chain([P(1.85, 0.2 * side, 3.25), P(1.65, 0.3 * side, 3.25)], [0.035 * k, 0.004], 'horn', [w] * 2, hint=Vector((0, 0, 1)), n=6)


def spines(q, part, pts_w, size, mat='horn'):
    """Back spines: (point, weight, scale) along the neck, back and tail."""
    for p, w, s in pts_w:
        part.chain([p - Vector((0, 0, 0.04)), p + Vector((-0.12 * s, 0, size * s))], [0.07 * s * q.k, 0.004], mat, [w, w], hint=Vector((1, 0, 0)), n=6)


@unit(16, 'dragons')
def dragons():
    """A green dragon: horned, its long neck and tail ridged with spines,
    great bat wings, clawed feet; a war collar and a cloth between its
    wings in the side's colour. Breathes fire (the attack); flies."""
    col = bpy.context.scene.collection
    L = mats.library()
    q = Quad(DRAGON, k=1.0, prefix='d_')
    bw = [BatWing('d_', side, q.P(0.35, 0.42 * side, 2.2), span=3.4, body_attach=q.P(-0.7, 0.5 * side, 1.7), membrane='membrane', bone_mat='scale_green')
          for side in (1, -1)]
    bones = q.bones() + [b for w in bw for b in w.bones()]
    bones.append(Bone(q.n('fire'), q.P(2.62, 0, 3.08), q.P(2.8, 0, 3.08), q.n('head'), (0, 0, 1)))
    arm = rig.build('rig', bones, col)
    hide = Part('dragon')
    q.body(hide, 'scale_green', belly='scale_belly')
    dragon_head(q, hide)
    paws(q, hide, mat='horn_dark', toes=3, size=0.085)
    tl = Part('tail')
    tp = [q.P(*p) for p in DRAGON['tail']]
    tl.chain(tp, [0.3, 0.24, 0.17, 0.11, 0.07, 0.03], lambda j, s: 'scale_belly' if abs(s - 0.5) < 0.15 else 'scale_green', chain_weights(q, 'tail', 5, 'spine'),
             hint=Vector((0, 0, 1)), n=14)
    gear.blade_profile(tl, tp[-1] - Vector((0.05, 0, 0)), (tp[-1] - tp[-2]).normalized(), Vector((0, 0, 1)), 0.4, 0.15, 0.02, 'horn', q.w('tail4'), point=0.45, taper=0.0)
    sp = Part('spines')
    rows = DRAGON['neck']
    pts = []
    for idx, ((tx, tz), _, _) in enumerate(rows):
        pts.append((q.P(tx, 0, tz), q.neck_weights(idx), 0.8))
    for x in (0.6, 0.2, -0.2, -0.6, -1.0, -1.35):
        p, _ = q.trunk_point(x, 0.0)
        pts.append((p, q.trunk_weights(x), 1.1))
    for k_ in range(5):
        a, b = tp[k_], tp[k_ + 1]
        for f in (0.2, 0.7):
            r = [0.3, 0.24, 0.17, 0.11, 0.07][k_]
            pts.append((a.lerp(b, f) + Vector((0, 0, r * 0.9)), blend(q.w('tail%d' % k_), q.w('tail%d' % min(4, k_ + 1)), f * 0.5), 0.9 - 0.15 * k_))
    spines(q, sp, pts, 0.28)
    mem, wb = Part('membrane'), Part('wing_bones')
    for w in bw:
        w.build(mem, wb)
    harness = Part('harness')
    collar(q, harness, 1.0, grow=0.04, width=0.09)
    back_cloth(q, harness, 0.55, -0.55, 1.1)
    fire = Part('fire')
    fire.ellipsoid(q.P(2.95, 0, 3.08), (0.4, 0.2, 0.2), 'fire', q.w('fire'), seg=12, rings=8)
    fire.ellipsoid(q.P(3.45, 0, 3.08), (0.35, 0.28, 0.28), 'fire', q.w('fire'), seg=12, rings=8)
    make(col, arm, L, [(hide, 2, 0), (tl, 2, 0), (sp, 1, 0), (mem, 1, 0.008), (wb, 1, 0), (harness, 1, 0.01), (fire, 1, 0)])

    def tail(p, a, up):
        for k in range(5):
            p.turn(q.n('tail%d' % k), Z, a * (0.4 + 0.2 * k)).turn(q.n('tail%d' % k), Y, -up)

    def wing(p, fold, lift, sweep, twist=5.0, flex=0.0):
        for w in bw:
            w.pose(p, fold, lift, sweep, twist, flex)

    def pose_of(anim, i, n):
        p = Pose()
        ground, t = flier_pose(anim, i, n)
        u = i / (n - 1)
        a, c = _cycle(t)
        p.scale(q.n('fire'), 0.0)
        if anim == 'idle':
            p.turn(q.n('neck2'), Y, 3 * a).turn(q.n('head'), Z, 6 * a)
            p.turn(q.n('jaw'), Y, 3 + 2 * c)
            tail(p, 6 * a, 0)
            wing(p, 0.85, 15 + 3 * a, 30)
        elif anim == 'walk' and ground:
            q.gait(p, t, walk_phases(), stance=0.6, reach=(16, 14), lift=(45, 40), pitch=2, bob=0.05)
            p.turn(q.n('neck0'), Z, 4 * a)
            tail(p, 10 * a, 0)
            wing(p, 0.85, 15, 30)
        elif anim == 'walk':
            for side in (1, -1):
                q.leg_pose(p, 'f', side, -30, [-10, 60, 30])
                q.leg_pose(p, 'h', side, -45, [20, -30, 20])
            p.move(q.n('root'), (0, 0, 0.2 * c))
            p.turn(q.n('neck0'), Y, 10).turn(q.n('neck1'), Y, 8).turn(q.n('neck2'), Y, -10).turn(q.n('head'), Y, -5)
            tail(p, 4 * a, -8)
            wing(p, 0.05 + 0.25 * max(0.0, -a), 5 + 50 * c, -5 + 12 * max(0.0, -a), -8 * a, flex=10 * max(0.0, -a))
        elif anim == 'attack':
            # Rears back, draws breath, and throws its head forward breathing fire.
            back = [0.0, 1.0, 0.6, -0.4, -0.5, 0.0][i]
            p.turn(q.n('neck0'), Y, -12 * back).turn(q.n('neck1'), Y, -10 * back).turn(q.n('neck2'), Y, -8 * back).turn(q.n('head'), Y, 10 * back)
            p.turn(q.n('chest'), Y, -6 * max(0.0, back))
            p.turn(q.n('jaw'), Y, [3, 6, 25, 40, 35, 10][i])
            p.scale(q.n('fire'), [0.0, 0.0, 0.5, 1.2, 0.9, 0.0][i])
            for side in (1, -1):
                q.leg_pose(p, 'f', side, 12 * max(0.0, back), [-10 * max(0.0, back), 20 * max(0.0, back), 0])
            tail(p, 0, 6 * back)
            wing(p, [0.85, 0.3, 0.2, 0.25, 0.45, 0.7][i], [15, 40, 45, 10, 5, 12][i], [30, 10, 5, 10, 20, 28][i])
        elif anim == 'flinch':
            kk = [1.0, 0.5][i]
            p.turn(q.n('neck1'), Y, -12 * kk).turn(q.n('head'), Y, -15 * kk).turn(q.n('jaw'), Y, 20 * kk)
            wing(p, 0.5, 30, 20)
        else:
            sink = smooth(0.1, 0.8, u)
            roll = 70 * smooth(0.35, 1.0, u)
            p.move(q.n('root'), (0, 0, -0.75 * sink + 0.5 * smooth(0.35, 1.0, u)))
            p.turn(q.n('root'), X, roll)
            for side in (1, -1):
                q.leg_pose(p, 'f', side, -10 * sink, [20 * sink, 60 * sink, 20 * sink])
                q.leg_pose(p, 'h', side, 25 * sink, [40 * sink, 40 * sink, 20 * sink])
            p.turn(q.n('neck0'), Y, 20 * sink).turn(q.n('neck1'), Y, 20 * sink).turn(q.n('jaw'), Y, 25 * u)
            tail(p, 15 * sink, 0)
            wing(p, 0.85 - 0.6 * smooth(0.1, 0.7, u), 15 - 40 * smooth(0.2, 1.0, u), 30 - 20 * u)
        return p
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- spiders

@unit(10, 'spiders')
def spiders():
    """A giant spider: a black, bristling body, eight long jointed legs, a
    cluster of red eyes and fangs, a mark in the side's colour on its back."""
    col = bpy.context.scene.collection
    L = mats.library()
    pre = 's_'
    n = lambda s: pre + s
    k = 1.0
    bones = [Bone(n('root'), Vector((0, 0, 0)), Vector((0, 0, 0.2)), None, X),
             Bone(n('body'), Vector((0.0, 0, 0.72)), Vector((0.55, 0, 0.74)), n('root'), Z),
             Bone(n('abdomen'), Vector((0.0, 0, 0.75)), Vector((-0.9, 0, 0.95)), n('body'), Z),
             Bone(n('fang.L'), Vector((0.6, 0.07, 0.68)), Vector((0.72, 0.07, 0.48)), n('body'), X),
             Bone(n('fang.R'), Vector((0.6, -0.07, 0.68)), Vector((0.72, -0.07, 0.48)), n('body'), X)]
    # Legs: base on the body's side, a high knee, a foot far out on the ground.
    LEGS = []
    for i, (bx, ang) in enumerate(((0.42, 35), (0.28, 75), (0.14, 105), (0.0, 145))):
        a = math.radians(ang)
        out = Vector((math.cos(a), math.sin(a), 0))
        base = Vector((bx, 0, 0.7)) + out * 0.2
        knee = base + out * 0.55 + Vector((0, 0, 0.55))
        mid = base + out * 1.05 + Vector((0, 0, 0.25))
        foot = base + out * 1.35 + Vector((0, 0, -0.7))
        LEGS.append((base, knee, mid, foot))
    for side, sf in ((1, 'L'), (-1, 'R')):
        for i, pts in enumerate(LEGS):
            pts = [Vector((p.x, p.y * side, p.z)) for p in pts]
            prev = n('body')
            for j, seg in enumerate('abc'):
                b = n('leg%d%s.%s' % (i, seg, sf))
                bones.append(Bone(b, pts[j], pts[j + 1], prev, (0, 0, 1)))
                prev = b
    arm = rig.build('rig', bones, col)
    body = Part('spider')
    body.ellipsoid(Vector((0.32, 0, 0.72)), (0.32, 0.26, 0.2), 'chitin', {n('body'): 1.0}, seg=20, rings=12,
                   shape=lambda v: 1.0 - 0.15 * smooth(0.3, 0.9, v.z))
    ab = Vector((-0.5, 0, 0.88))
    body.ellipsoid(ab, (0.55, 0.45, 0.4), 'chitin_hair', {n('abdomen'): 1.0}, seg=24, rings=16)
    # The mark in the side's colour on its back: a broad hourglass.
    def mark(v):
        x, y, z = v.x, v.y, v.z
        w = 0.18 + 0.25 * abs(x)
        return 1.035 if z > 0.25 and abs(y) < w else 0.95
    body.ellipsoid(ab, (0.55, 0.45, 0.4), 'team', {n('abdomen'): 1.0}, seg=32, rings=20, shape=mark)
    for side in (1, -1):
        for j, (ex, ey, ez, r) in enumerate(((0.6, 0.06, 0.82, 0.035), (0.58, 0.13, 0.8, 0.03), (0.55, 0.05, 0.88, 0.025), (0.52, 0.12, 0.86, 0.02))):
            body.ellipsoid(Vector((ex, ey * side, ez)), (r, r, r), 'eye_red', {n('body'): 1.0}, seg=8, rings=5)
        sf = 'L' if side > 0 else 'R'
        body.chain([Vector((0.6, 0.07 * side, 0.68)), Vector((0.7, 0.08 * side, 0.58)), Vector((0.7, 0.06 * side, 0.48))], [0.05, 0.035, 0.008], 'horn_dark',
                   [{n('fang.' + sf): 1.0}] * 3, hint=Vector((1, 0, 0)), n=8)
        for i, pts in enumerate(LEGS):
            pts = [Vector((p.x, p.y * side, p.z)) for p in pts]
            ws = [{n('body'): 0.5, n('leg%da.%s' % (i, sf)): 0.5}, {n('leg%da.%s' % (i, sf)): 0.5, n('leg%db.%s' % (i, sf)): 0.5},
                  {n('leg%db.%s' % (i, sf)): 0.5, n('leg%dc.%s' % (i, sf)): 0.5}, {n('leg%dc.%s' % (i, sf)): 1.0}]
            body.chain(pts, [0.055, 0.045, 0.035, 0.008], 'chitin', ws, hint=Vector((0, 0, 1)), n=8,
                       shape=lambda ii, th: 1.0 + 0.1 * math.sin(th * 6 + ii))
            for j in (1, 2):
                body.ellipsoid(pts[j], (0.05, 0.05, 0.05), 'chitin_hair', ws[j], seg=8, rings=5)
    make(col, arm, L, [(body, 2, 0)])

    def leg(p, i, side, swing, lift, curl=0.0):
        sf = 'L' if side > 0 else 'R'
        # Forward swing about the vertical, lift about the leg's own outward line (approximated by x).
        p.lr(n('leg%da' % i), side, Z, -swing)
        p.lr(n('leg%da' % i), side, X, lift)
        if curl:
            p.lr(n('leg%db' % i), side, X, -curl)
            p.lr(n('leg%dc' % i), side, X, -curl * 1.3)

    def pose_of(anim, i, nn):
        p = Pose()
        t = i / nn
        u = i / (nn - 1)
        if anim == 'idle':
            b = math.sin(2 * math.pi * t)
            p.move(n('body'), (0, 0, 0.01 * b))
            p.turn(n('abdomen'), Y, 3 * b)
            for k_ in range(4):
                for side in (1, -1):
                    leg(p, k_, side, 2 * b * (1 if k_ % 2 else -1), 0)
            p.lr(n('fang'), 1, Y, 8 * b)
        elif anim == 'walk':
            # Alternating tetrapods: L0 R1 L2 R3 together, then the others.
            for k_ in range(4):
                for side in (1, -1):
                    ph = 0.0 if (k_ % 2 == 0) == (side > 0) else 0.5
                    q_ = (t - ph) % 1.0
                    if q_ < 0.5:
                        uu = q_ / 0.5
                        sw, lf = 18 * (1 - 2 * uu), 0
                    else:
                        uu = (q_ - 0.5) / 0.5
                        sw, lf = 18 * (-1 + 2 * uu), 22 * math.sin(math.pi * uu)
                    leg(p, k_, side, sw, lf)
            p.move(n('body'), (0, 0, 0.02 * math.cos(4 * math.pi * t)))
            p.turn(n('abdomen'), Z, 4 * math.sin(2 * math.pi * t))
        elif anim == 'attack':
            r = [0.0, 0.8, 1.0, 0.5, 0.2, 0.0][i]
            s = [0.0, 0.0, 0.4, 1.0, 0.6, 0.1][i]
            p.turn(n('body'), Y, -20 * r + 8 * s)
            p.move(n('body'), (0.15 * s, 0, 0.1 * r))
            for side in (1, -1):
                leg(p, 0, side, 15 * r, 45 * r - 20 * s)
                leg(p, 1, side, 5 * r, 25 * r)
                p.lr(n('fang'), side, Y, -35 * s + 10 * r)
        elif anim == 'flinch':
            kk = [1.0, 0.5][i]
            p.turn(n('body'), Y, -8 * kk)
            for side in (1, -1):
                leg(p, 0, side, -10 * kk, 20 * kk)
        else:
            # On its back, legs curling in.
            sink = smooth(0.0, 0.6, u)
            flip = 160 * smooth(0.2, 0.9, u)
            p.move(n('root'), (0, 0, 0.7 * smooth(0.2, 0.6, u) - 0.25 * smooth(0.6, 1.0, u)))
            p.turn(n('root'), X, flip)
            for k_ in range(4):
                for side in (1, -1):
                    leg(p, k_, side, 0, -20 * sink, curl=60 * sink)
        return p
    rig.key_all(arm, pose_of)
    return arm


# ---------------------------------------------------------------- catapult

@unit(26, 'catapult')
def catapult():
    """A mangonel: a timber bed on four spoked wheels, a throwing arm sprung
    from a twisted-rope skein, its bucket loaded with a stone, a pennon in
    the side's colour. Rolls (wheels turn), throws (the arm swings up and
    slams the crossbar), falls apart."""
    col = bpy.context.scene.collection
    L = mats.library()
    n = lambda s: 'c_' + s
    R = 0.42  # wheel radius
    arm_pivot = Vector((0.15, 0, 0.95))
    bones = [Bone(n('root'), Vector((0, 0, 0)), Vector((0, 0, 0.3)), None, X),
             Bone(n('bed'), Vector((-1.3, 0, 0.75)), Vector((1.3, 0, 0.75)), n('root'), Z),
             Bone(n('arm'), arm_pivot, arm_pivot + Vector((-1.7, 0, 0.12)), n('bed'), Z),
             Bone(n('stone'), arm_pivot + Vector((-1.7, 0, 0.3)), arm_pivot + Vector((-1.7, 0, 0.5)), n('arm'), X),
             Bone(n('pennon'), Vector((1.15, 0.45, 1.5)), Vector((1.15, 0.45, 2.0)), n('bed'), X)]
    wheels = [(0.85, 0.62), (0.85, -0.62), (-0.85, 0.62), (-0.85, -0.62)]
    for k, (x, y) in enumerate(wheels):
        bones.append(Bone(n('wheel%d' % k), Vector((x, y, R)), Vector((x, y + 0.2 * (1 if y > 0 else -1), R)), n('bed'), Z))
    arm = rig.build('rig', bones, col)
    wb = {n('bed'): 1.0}
    bed = Part('bed')
    for y in (0.42, -0.42):
        bed.box(Vector((0, y, 0.75)), AX, (1.35, 0.08, 0.09), 'wood', wb)
    for x in (-1.1, -0.55, 0.3, 1.1):
        bed.box(Vector((x, 0, 0.78)), AX, (0.07, 0.5, 0.06), 'wood', wb)
    # Uprights and the crossbar that stops the arm; the skein of rope at the pivot.
    for y in (0.42, -0.42):
        bed.box(Vector((0.4, y, 1.45)), AX, (0.07, 0.07, 0.72), 'wood', wb)
        bed.box(Vector((0.85, y, 1.2)), (Vector((-0.6, 0, 0.8)).normalized(), Vector((0, 1, 0)), Vector((0.8, 0, 0.6)).normalized()), (0.6, 0.05, 0.05), 'wood', wb)
    bed.box(Vector((0.4, 0, 2.12)), AX, (0.09, 0.5, 0.08), 'wood_dark', wb)
    bed.box(Vector((0.3, 0, 2.05)), AX, (0.05, 0.3, 0.1), 'leather', wb)
    bed.chain([arm_pivot - Vector((0, 0.45, 0)), arm_pivot + Vector((0, 0.45, 0))], [0.12, 0.12], 'rope', [wb, wb], hint=Vector((0, 0, 1)), n=12)
    for y in (0.5, -0.5):
        bed.chain([arm_pivot + Vector((0, y, 0)), arm_pivot + Vector((0, y * 1.08, 0))], [0.16, 0.16], 'iron', [wb, wb], hint=Vector((0, 0, 1)), n=12)
    # A winch at the back.
    bed.chain([Vector((-1.15, -0.5, 0.95)), Vector((-1.15, 0.5, 0.95))], [0.08, 0.08], 'wood_dark', [wb, wb], hint=Vector((0, 0, 1)), n=10)
    for y in (0.55, -0.55):
        for a in range(4):
            ang = a * math.pi / 2
            bed.chain([Vector((-1.15, y, 0.95)), Vector((-1.15 + 0.22 * math.cos(ang), y, 0.95 + 0.22 * math.sin(ang)))], [0.025, 0.02], 'wood', [wb, wb], hint=Vector((0, 1, 0)), n=5)
    # The pennon on its pole.
    bed.chain([Vector((1.15, 0.45, 0.8)), Vector((1.15, 0.45, 2.1))], [0.03, 0.025], 'wood_dark', [wb, wb], hint=Vector((1, 0, 0)), n=6)
    pw = {n('pennon'): 1.0}
    bed.grid(lambda s, t: Vector((1.15 - 0.75 * s, 0.45 + 0.05 * math.sin(s * 6), 2.08 - 0.32 * t * (1 - 0.45 * s))), 10, 4, 'team', pw)
    # Team cloth over the bed's side boards too (large enough to read).
    for y in (0.5, -0.5):
        bed.box(Vector((-0.2, y, 0.75)), AX, (0.55, 0.012, 0.07), 'team', wb)
    # The arm, its bucket and the stone.
    aw = {n('arm'): 1.0}
    tipp = arm_pivot + Vector((-1.7, 0, 0.12))
    bed.chain([arm_pivot + Vector((0.15, 0, 0)), arm_pivot + Vector((-0.85, 0, 0.06)), tipp], [0.07, 0.06, 0.05], 'wood', [aw] * 3, hint=Vector((0, 0, 1)), n=8)
    bed.ellipsoid(tipp + Vector((0.0, 0, 0.1)), (0.24, 0.24, 0.14), 'wood_dark', aw, seg=14, rings=8, shape=lambda v: 1.0 if v.z < 0.3 else 0.7)
    bed.ellipsoid(tipp + Vector((0.0, 0, 0.28)), (0.19, 0.19, 0.17), 'stone', {n('stone'): 1.0}, seg=12, rings=8)
    whe = Part('wheels')
    for k, (x, y) in enumerate(wheels):
        ww = {n('wheel%d' % k): 1.0}
        c = Vector((x, y, R))
        rings = []
        for dy in (-0.05, 0.05):
            rings.append({'c': c + Vector((0, dy, 0)), 'u': Vector((1, 0, 0)), 'v': Vector((0, 0, 1)), 'ru': R, 'rv': R, 'w': ww})
        whe.loft(rings, 'wood_dark', n=24, cap0=False, cap1=False)
        rings = []
        for dy in (-0.05, 0.05):
            rings.append({'c': c + Vector((0, dy, 0)), 'u': Vector((1, 0, 0)), 'v': Vector((0, 0, 1)), 'ru': R - 0.06, 'rv': R - 0.06, 'w': ww})
        whe.loft(rings, 'wood_dark', n=24, cap0=False, cap1=False)
        rr = []
        for dy, rad in ((-0.05, R), (-0.05, R + 0.02), (0.05, R + 0.02), (0.05, R)):
            rr.append({'c': c + Vector((0, dy, 0)), 'u': Vector((1, 0, 0)), 'v': Vector((0, 0, 1)), 'ru': rad, 'rv': rad, 'w': ww})
        whe.loft(rr, 'iron', n=24, cap0=False, cap1=False)
        for s_ in range(6):
            a = s_ * math.pi / 3
            whe.chain([c, c + Vector((math.cos(a) * (R - 0.04), 0, math.sin(a) * (R - 0.04)))], [0.025, 0.02], 'wood', [ww, ww], hint=Vector((0, 1, 0)), n=5)
        whe.chain([c - Vector((0, 0.09, 0)), c + Vector((0, 0.09, 0))], [0.07, 0.07], 'iron', [ww, ww], hint=Vector((0, 0, 1)), n=8)
    make(col, arm, L, [(bed, 1, 0), (whe, 1, 0)])

    def pose_of(anim, i, nn):
        p = Pose()
        t = i / nn
        u = i / (nn - 1)
        flutter = math.sin(2 * math.pi * t)
        p.turn(n('pennon'), Z, 10 * flutter)
        if anim == 'walk':
            for k in range(4):
                p.turn(n('wheel%d' % k), Y, 360 * t)
            p.turn(n('pennon'), Z, 20 * math.sin(4 * math.pi * t))
        elif anim == 'attack':
            # Wound down, loosed: the arm swings up to the crossbar and the stone flies.
            lift = [0.0, 0.0, 0.6, 1.0, 0.85, 0.3][i]
            p.turn(n('arm'), Y, 84 * lift)
            p.move(n('root'), (0, 0, 0.04 * math.sin(math.pi * max(0.0, lift - 0.5) * 2)))
            p.scale(n('stone'), 1.0 if i < 2 else 0.0)
            p.turn(n('bed'), Y, -2 * lift)
        elif anim == 'flinch':
            kk = [1.0, 0.4][i]
            p.turn(n('bed'), X, 3 * kk)
        elif anim == 'die':
            # Off a wheel and onto its side, the arm broken loose.
            d = smooth(0.0, 0.9, u)
            p.turn(n('root'), X, 22 * d)
            p.move(n('root'), (0, 0, -0.2 * d))
            p.turn(n('arm'), Y, 30 * d).turn(n('arm'), X, 25 * d)
            p.turn(n('wheel0'), X, 60 * d).turn(n('wheel2'), X, 40 * d)
            p.move(n('wheel0'), (0.0, 0.0, 0.0))
            p.scale(n('stone'), 1.0 - d)
        return p
    rig.key_all(arm, pose_of)
    return arm
