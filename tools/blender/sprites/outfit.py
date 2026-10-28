"""Clothes, armour and arms, fitted over the body of human.py and weighted to
the same bones: skirts and robes that follow the legs, a mail hauberk, a
surcoat in the team's colour, helmets, a heater shield and swords."""

import math

from mathutils import Matrix, Vector

from . import human
from .geo import Part, Rand, blend, frame_for, smooth


def skirt_weights(B, z, th, hip_z=1.0, knee_z=0.52):
    """A skirt's vertex at height z (1.8 m figure) and angle th (0 forward,
    +pi/2 left): mostly the hips at the waist, more and more the legs below,
    shared between them by which side it hangs on."""
    side = math.sin(th)
    share_l = 0.5 + 0.5 * max(-1.0, min(1.0, side * 1.6))
    legs = 0.85 * smooth(hip_z - 0.05, hip_z - 0.42, z)
    out = {}
    for sf, share in (('L', share_l), ('R', 1 - share_l)):
        k = legs * share
        if k <= 0:
            continue
        below = smooth(knee_z + 0.08, knee_z - 0.12, z)
        out[B.n('thigh.' + sf)] = k * (1 - below)
        if below > 0:
            out[B.n('shin.' + sf)] = k * below * 0.5
            out[B.n('thigh.' + sf)] += k * below * 0.5
    out[B.n('hips')] = 1.0 - legs
    return {k: v for k, v in out.items() if v > 1e-4}


def folds(seed, n=9):
    r = Rand(seed)
    ph = [r.range(0, 6.283) for _ in range(4)]
    k = [n, int(n * 1.7) + 1, int(n * 0.5) + 1]

    def f(th, depth):
        return depth * (0.55 * math.sin(k[0] * th + ph[0]) + 0.3 * math.sin(k[1] * th + ph[1]) + 0.25 * math.sin(k[2] * th + ph[2]))
    return f


def skirt(part, B, mat, z_top, z_bottom, top, bottom, rows=9, n=28, fold_depth=0.06, seed=1, a=None, hip_z=1.0,
          cx_bottom=0.0, grow_pow=1.0, hem=None):
    """A skirt or robe from z_top to z_bottom; top and bottom: (half depth x,
    half width y). a: (a0, a1) for an open panel (angles, 0 forward)."""
    f = folds(seed)
    rings = []
    for j in range(rows + 1):
        t = j / rows
        z = z_top + (z_bottom - z_top) * t
        tt = t ** grow_pow
        hd = top[0] + (bottom[0] - top[0]) * tt
        hw = top[1] + (bottom[1] - top[1]) * tt
        depth = fold_depth * smooth(0.0, 0.7, t)
        ring = {'c': B.P(cx_bottom * tt, 0, z), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                'ru': B.R(hd), 'rv': B.R(hw),
                'w': (lambda th, z=z: skirt_weights(B, z, th, hip_z=hip_z)),
                'shape': (lambda th, depth=depth, t=t: 1.0 + f(th, depth) + (hem(th) * t ** 3 if hem else 0.0))}
        if a:
            ring['a0'], ring['a1'] = a
        rings.append(ring)
    return part.loft(rings, mat, n=n, cap0=False, cap1=False, closed=a is None, uv_scale=(4, 3))


def belt(part, B, mat, z, hw, hd, height=0.05, buckle=None):
    shape = human.superellipse(2.4)
    rings = []
    for k, dz in enumerate((-height / 2, -height / 2 + 0.008, height / 2 - 0.008, height / 2)):
        g = 0.0 if k in (0, 3) else 0.006
        rings.append({'c': B.P(0.004, 0, z + dz), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                      'ru': B.R(hd) + g, 'rv': B.R(hw) + g, 'w': B.w('hips', 0.6, 'spine', 0.4), 'shape': shape})
    part.loft(rings, mat, n=24, cap0=False, cap1=False)
    if buckle:
        part.box(B.P(hd + 0.012, 0, z), (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))),
                 (0.008 * B.s, 0.03 * B.s, 0.028 * B.s), buckle, B.w('hips', 0.6, 'spine', 0.4))


# ------------------------------------------------------------------ helmets

def great_helm(part, B, steel='steel', slit='eye', trim='gold'):
    """A knight's flat-topped great helm, eye slits and a brass cross on the face."""
    c, r = human.head_center(B), human.head_radii(B)
    w = B.w('head')
    rows = [(-1.12, 1.18, 1.16), (-0.9, 1.2, 1.18), (-0.4, 1.2, 1.17), (0.05, 1.19, 1.16), (0.12, 1.2, 1.17),
            (0.2, 1.2, 1.17), (0.6, 1.17, 1.14), (0.88, 1.1, 1.07), (1.02, 0.85, 0.82), (1.08, 0.4, 0.4)]
    rings = []
    for zz, kx, ky in rows:
        rings.append({'c': c + Vector((0.008 * B.s, 0, zz * r[2])), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                      'ru': r[0] * kx, 'rv': r[1] * ky * 1.08, 'w': w, 'shape': human.superellipse(2.2), 'dome': 0.2})

    def mat(j, s):
        th = s * 2 * math.pi
        front = math.cos(th)
        if j == 4 and front > 0.45:
            return slit
        if front > 0.996 and j < 4:
            return trim
        if j in (3, 5) and front > 0.4:
            return trim
        return steel
    part.loft(rings, mat, n=36)


def nasal_helm(part, B, steel='steel'):
    """A conical helm with a nasal, over a mail coif."""
    c, r = human.head_center(B), human.head_radii(B)
    w = B.w('head')
    rows = [(0.02, 1.13), (0.15, 1.15), (0.45, 1.1), (0.75, 0.9), (0.95, 0.6), (1.12, 0.22)]
    rings = []
    for zz, k in rows:
        rings.append({'c': c + Vector((-0.004 * B.s, 0, zz * r[2])), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                      'ru': r[0] * k * 1.02, 'rv': r[1] * k * 1.1, 'w': w, 'dome': 0.5})
    part.loft(rings, lambda j, s: 'gold' if j == 0 else steel, n=28)
    # The nasal.
    top = c + Vector((r[0] * 1.13, 0, 0.1 * r[2]))
    part.box(top + Vector((0.004, 0, -0.38 * r[2])), (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))),
             (0.006 * B.s, 0.012 * B.s, 0.4 * r[2]), steel, w)


def coif(part, B, mat='mail', open_face=False):
    """Mail from the head to the shoulders."""
    c, r = human.head_center(B), human.head_radii(B)
    rows = [(c.z - 0.06 * B.s, 1.12, 1.15, B.w('head')),
            (B.P(0, 0, 1.56).z, 0.9, 0.95, B.w('head', 0.5, 'neck', 0.5)),
            (B.P(0, 0, 1.52).z, 1.0, 1.25, B.w('neck', 0.5, 'chest', 0.5)),
            (B.P(0, 0, 1.47).z, 1.25, 2.05, B.w('chest')),
            (B.P(0, 0, 1.41).z, 1.3, 2.3, B.w('chest'))]
    rings = []
    for z, kx, ky, w in rows:
        rings.append({'c': Vector((c.x - 0.012 * B.s, 0, z)), 'u': Vector((1, 0, 0)), 'v': Vector((0, 1, 0)),
                      'ru': r[0] * kx, 'rv': r[1] * ky, 'w': w, 'shape': human.superellipse(2.3)})
    part.loft(rings, mat, n=24, cap0=False, cap1=False, uv_scale=(6, 2))


def halo(part, B, mat='halo', radius=0.15, tube=0.012):
    c, r = human.head_center(B), human.head_radii(B)
    centre = c + Vector((-0.07 * B.s, 0, r[2] * 1.05))
    axis = Vector((-0.45, 0, 1)).normalized()
    u, v = frame_for(axis, Vector((1, 0, 0)))
    pts = []
    for i in range(32):
        a = 2 * math.pi * i / 32
        pts.append(centre + (u * math.cos(a) + v * math.sin(a)) * radius * B.s)
    rings = []
    for i, p in enumerate(pts):
        tangent = (pts[(i + 1) % 32] - pts[i - 1]).normalized()
        a, b = frame_for(tangent, axis)
        rings.append({'c': p, 'u': a, 'v': b, 'ru': tube * B.s, 'rv': tube * B.s * 0.6, 'w': B.w('head')})
    rings.append(dict(rings[0]))
    part.loft(rings, mat, n=8, cap0=False, cap1=False)


# ------------------------------------------------------------------ weapons

def sword(part, B, side=-1, blade=0.8, width=0.026, guard='steel', grip_mat='leather_dark', blade_mat='blade', pommel='steel', guard_len=0.1):
    """A sword in the hand: its grip along +x through the fist (rest frame),
    the edge up and down."""
    sf = 'L' if side > 0 else 'R'
    w = B.w('hand.' + sf)
    g = human.grip_point(B, side)
    s = B.s
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    part.chain([g - X * 0.06 * s, g + X * 0.06 * s], [0.015 * s, 0.014 * s], grip_mat, [w, w], hint=Z, n=8)
    part.ellipsoid(g - X * 0.075 * s, (0.014 * s, 0.024 * s, 0.026 * s), pommel, w, seg=10, rings=6)
    gp = g + X * 0.068 * s
    part.chain([gp - Z * guard_len * s + X * 0.01 * s, gp, gp + Z * guard_len * s + X * 0.01 * s],
               [(0.011 * s, 0.009 * s), (0.014 * s, 0.012 * s), (0.011 * s, 0.009 * s)], guard, [w, w, w], hint=X, n=8)
    # The blade: a flattened diamond, tapering, then the point.
    first = len(part.verts)
    rows = []
    L = blade * s
    for k in range(9):
        t = k / 8
        x = 0.075 * s + L * t
        hw = width * s * (1.0 - 0.35 * t) if t < 0.86 else width * s * 0.65 * (1 - (t - 0.86) / 0.14) + 0.0006
        rows.append((x, hw, 0.0045 * s * (1 - 0.5 * t)))
    rings = []
    for x, hw, th in rows:
        rings.append({'c': g + X * x, 'u': Z, 'v': Y, 'ru': hw, 'rv': th, 'w': w,
                      'shape': lambda a: 1.0 / max(0.4, abs(math.cos(a)) + abs(math.sin(a)))})
    part.loft(rings, blade_mat, n=8, cap0=True, cap1=True)
    return first


def heater_shield(part, B, face='team', charge='robe', rim='steel_dark', back='wood', height=0.8, width=0.6, side=1):
    """A large heater shield on the forearm: at rest its face looks outward
    (+y for the left arm) and its top forward (+x); a pale cross on the field."""
    sf = 'L' if side > 0 else 'R'
    w = B.w('forearm.' + sf)
    s = B.s
    elbow, wrist = B.P(-0.01, 0.235 * side, 1.15), B.P(0.0, 0.263 * side, 0.915)
    centre = elbow.lerp(wrist, 0.5) + Vector((0, 0.075 * side * s, 0))
    hh, hw = height / 2 * s, width / 2 * s

    def outline(t):
        """Half width at t (0 the point, 1 the top)."""
        if t > 0.62:
            return hw
        u = (0.62 - t) / 0.62
        return hw * math.sqrt(max(0.0, 1 - u ** 1.9))

    def fn(si, t):
        b = (2 * si - 1) * outline(t)
        a = -hh + 2 * hh * t
        bulge = 0.05 * s * (1 - (b / hw) ** 2)
        return centre + Vector((a, 0, 0)) + Vector((0, 0, b)) + Vector((0, side * bulge, 0))

    def mat(si, t):
        edge = min(si, 1 - si) * 2 * outline(t) / max(1e-6, hw) if outline(t) > 1e-4 else 0
        b = (2 * si - 1) * outline(t)
        if t > 0.965 or (2 * outline(t)) * min(si, 1 - si) < 0.025 * s:
            return rim
        if abs(b) < 0.045 * s or abs(t - 0.66) < 0.06 * (height / 0.8):
            return charge
        return face
    first = part.slab(fn, 24, 30, 0.02 * s, mat, w, rim_mat=rim, back_mat=back)
    if side < 0:
        human._flip_since(part, first)
    # A boss at the cross.
    part.ellipsoid(fn(0.5, 0.66) + Vector((0, side * 0.012 * s, 0)), (0.05 * s, 0.025 * s, 0.05 * s), 'steel', w, seg=12, rings=6)
    return first


def sleeve(part, B, side, mat, flare=0.05, end=6, cuff=None):
    """A wide robe sleeve over the arm, open at the end (end: the arm section it reaches)."""
    P, R, ws = human.arm_rows(B, side)
    rows = list(range(1, end + 1))
    pts = [P[i] for i in rows]
    radii = []
    for k, i in enumerate(rows):
        t = k / (len(rows) - 1)
        g = 0.012 * B.s + flare * B.s * t ** 1.6
        radii.append((R[i][0] + g, R[i][1] + g))
    # The sleeve's end hangs below the arm: drop the last ring's lower side.
    first = part.chain(pts, radii, (lambda j, s: cuff if cuff and j == len(rows) - 2 else mat), [ws[i] for i in rows], hint=Vector((1, 0, 0)), n=14, cap0=False, cap1=False)
    return first


def cuirass(part, B, mat, grow=0.035, z_from=1.1, z_to=1.49):
    """A breastplate and backplate shaped to the chest."""
    first = len(part.verts)
    human.torso(part, B, mat, grow=grow, z_from=z_from, z_to=z_to)
    # Pectorals and the ridge of the sternum.
    c = B.P(0, 0, 0)
    for i in range(first, len(part.verts)):
        p = part.verts[i]
        z = (p.z - B.o.z) / B.s
        if p.x - c.x > 0.05 * B.s:
            k = math.exp(-((z - 1.36) / 0.06) ** 2) * math.exp(-((abs(p.y - c.y) / (B.s * B.b) - 0.08) / 0.06) ** 2)
            p.x += 0.018 * B.s * k
    return first


def pauldron(part, B, side, mat):
    sf = 'L' if side > 0 else 'R'
    w = B.w('upperarm.' + sf, 0.65, 'clavicle.' + sf, 0.35)
    c = B.P(-0.005, 0.19 * side, 1.43)

    def shape(q):
        return 1.0 if q.z > -0.25 else 0.5 + 0.5 * smooth(-0.8, -0.25, q.z)
    for k, (dz, r) in enumerate(((0.0, 1.0), (-0.035, 0.97), (-0.068, 0.92))):
        part.ellipsoid(c + Vector((0, 0.008 * side * k, dz * B.s)), (0.085 * B.s * r, 0.085 * B.s * r, 0.062 * B.s), mat, w, seg=16, rings=8, shape=shape)
