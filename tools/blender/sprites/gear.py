"""Arms, headgear and clothes for the whole roster, fitted to human.py's body
like outfit.py's: weapons held in the fist (their haft along +x at rest, as
outfit.sword), shields on the forearm, helmets, hoods and hats on the head
bone, beards, horns, capes and kilts.

Readability at a sprite's size comes first: heads of weapons a little large,
plumes and crests in the side's colour, strong dark-light contrasts."""

import math

from mathutils import Matrix, Vector

from . import human
from .geo import Part, Rand, blend, frame_for, smooth

X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
AXES = (X, Y, Z)


def _hand(B, side):
    sf = 'L' if side > 0 else 'R'
    return B.w('hand.' + sf), human.grip_point(B, side), B.s


def shaft(part, B, side, back, front, r, mat='wood', r_end=None, n=8):
    """A haft through the fist from back metres behind it to front ahead."""
    w, g, s = _hand(B, side)
    pts = [g - X * back * s, g + X * front * s]
    r1 = r_end if r_end is not None else r
    return part.chain(pts, [r * s, r1 * s], mat, [w, w], hint=Z, n=n)


def blade_profile(part, c, axis, width_dir, length, width, thick, mat, w, rows=8, point=0.2, taper=0.3):
    """A flat leaf or blade from c along axis (a spear's head, a sickle's tip)."""
    rings = []
    for k in range(rows + 1):
        t = k / rows
        hw = width * (math.sin(math.pi * min(1.0, 0.25 + t * 0.9)) if t < 1 - point else (1 - t) / point * (1 - taper) + 0.02)
        rings.append({'c': c + axis * (length * t), 'u': width_dir, 'v': width_dir.cross(axis).normalized(), 'ru': max(0.0008, hw), 'rv': thick * (1 - 0.6 * t) + 0.0006,
                      'w': w, 'shape': lambda a: 1.0 / max(0.4, abs(math.cos(a)) + abs(math.sin(a)))})
    return part.loft(rings, mat, n=8)


def axe(part, B, side=-1, haft=0.75, butt=0.08, double=False, size=1.0, mat='steel', haft_mat='wood'):
    """A war axe: the bit's edge faces -z at rest (forward once raised)."""
    w, g, s = _hand(B, side)
    shaft(part, B, side, butt, haft, 0.016, haft_mat)
    top = g + X * (haft - 0.07 * size) * s
    for sgn in ((1, -1) if double else (-1,)):
        def fn(a, b, sgn=sgn):
            # a: out from the haft (0) to the edge (1); b: along the haft, flaring at the edge.
            half = (0.045 + 0.1 * a ** 1.6) * size * s
            return top + X * ((b - 0.5) * 2 * half) + Z * (sgn * (0.015 + 0.15 * a) * size * s)
        first = part.slab(fn, 6, 6, 0.012 * s * size, mat, w, rim_mat='blade')
        # Centre the slab on the haft's plane.
        for i in range(first, len(part.verts)):
            part.verts[i] += Y * (0.006 * s * size)
    part.ellipsoid(top, (0.035 * s * size, 0.022 * s, 0.03 * s * size), 'steel_dark', w, seg=8, rings=5)
    if not double:
        # A spike on the back for weight and a clear shape.
        part.chain([top + Z * 0.02 * s, top + Z * 0.09 * s * size], [0.014 * s, 0.002 * s], 'steel_dark', [w, w], hint=X, n=6)


def spear(part, B, side=-1, length=2.2, butt=0.6, head=0.28, width=0.04, mat='steel', haft_mat='wood', trident=False, pennon=None):
    """A spear or pike (or a trident, three tines) along the haft."""
    w, g, s = _hand(B, side)
    shaft(part, B, side, butt, length - butt - head * 0.6, 0.017, haft_mat, r_end=0.014)
    tip = g + X * (length - butt - head * 0.75) * s
    part.ellipsoid(tip, (0.03 * s, 0.022 * s, 0.022 * s), 'steel_dark', w, seg=8, rings=5)
    if trident:
        part.chain([tip - Z * 0.12 * s, tip + Z * 0.12 * s], [0.014 * s] * 2, mat, [w, w], hint=X, n=6)
        for dz in (-0.12, 0.0, 0.12):
            base = tip + Z * dz * s
            blade_profile(part, base, X, Z, head * s * (1.15 if dz == 0 else 0.9), 0.022 * s, 0.01 * s, mat, w, point=0.35)
    else:
        blade_profile(part, tip, X, Z, head * s, width * s, 0.011 * s, mat, w)
    if pennon:
        # A swallow-tailed pennon in the side's colour, below the head.
        a = tip - X * 0.08 * s
        pennon_cloth(part, a, -X, -Z, 0.18 * s, 0.45 * s, w, pennon)


def pennon_cloth(part, root, along, out, height, length, w, mat, wave=0.05):
    """A forked pennon on a staff: root at the staff, height along it, length
    streaming out (both unit directions), rippling."""
    side = along.cross(out).normalized()

    def fn(u, v):
        # u along the streamer (0 at the staff), v down the staff.
        p = root + out * (length * u) + along * (height * v * (1 - 0.35 * u))
        p += side * (wave * height * math.sin(u * 7.0) * u)
        return p
    part.grid(fn, 10, 4, mat, w)


def lance(part, B, side=-1, length=3.0, butt=0.7, pennon='team'):
    """A knight's lance: a tapering shaft, a vamplate before the hand, a pennon."""
    w, g, s = _hand(B, side)
    pts = [g - X * butt * s, g, g + X * 0.3 * s, g + X * (length - butt) * s]
    part.chain(pts, [0.02 * s, 0.03 * s, 0.026 * s, 0.006 * s], 'wood_pale', [w] * 4, hint=Z, n=8)
    # The vamplate: a steel cone guarding the hand.
    part.chain([g + X * 0.06 * s, g + X * 0.2 * s], [0.075 * s, 0.025 * s], 'steel', [w, w], hint=Z, n=12, cap0=True)
    tip = g + X * (length - butt - 0.12) * s
    blade_profile(part, tip, X, Z, 0.16 * s, 0.02 * s, 0.012 * s, 'steel', w)
    if pennon:
        pennon_cloth(part, g + X * (length - butt - 0.25) * s, -X, -Z, 0.16 * s, 0.5 * s, w, pennon)


def greatsword(part, B, side=-1, blade=1.25):
    from .outfit import sword
    w, g, s = _hand(B, side)
    # A long grip for two hands, then a sword's guard and blade.
    part.chain([g - X * 0.2 * s, g + X * 0.06 * s], [0.016 * s, 0.015 * s], 'leather_dark', [w, w], hint=Z, n=8)
    return sword(part, B, side=side, blade=blade, width=0.034, guard_len=0.15)


def club(part, B, side=-1, length=1.1, thick=0.06, knots=True, mat='wood_rough'):
    """A cudgel or a giant's uprooted club: thick at the head, knotted."""
    w, g, s = _hand(B, side)
    pts = [g - X * 0.12 * s + Z * 0, g + X * (length * 0.3) * s, g + X * (length * 0.7) * s, g + X * length * s]
    part.chain(pts, [thick * 0.45 * s, thick * 0.6 * s, thick * 0.95 * s, thick * 1.1 * s], mat, [w] * 4, hint=Z, n=10, dome=0.6)
    if knots:
        r = Rand(31)
        for k in range(6):
            t = 0.45 + 0.5 * k / 5
            a = r.range(0, 6.28)
            c = g + X * (length * t) * s + (Y * math.cos(a) + Z * math.sin(a)) * thick * 0.85 * s
            part.ellipsoid(c, (0.03 * s, 0.03 * s, 0.03 * s), mat, w, seg=8, rings=5)


def staff(part, B, side=-1, length=1.85, butt=0.75, orb='glow', crook=False):
    """A wizard's staff, a glowing stone in a forked head."""
    w, g, s = _hand(B, side)
    top = g + X * (length - butt) * s
    r = Rand(5)
    pts = [g - X * butt * s] + [g + X * (length - butt) * s * t + Z * r.range(-0.01, 0.01) * s for t in (0.0, 0.35, 0.7, 1.0)]
    part.chain(pts, [0.016 * s, 0.018 * s, 0.017 * s, 0.017 * s, 0.022 * s], 'wood_dark', [w] * 5, hint=Z, n=8)
    if orb:
        for a in (0, 2.1, 4.2):
            d = (Y * math.cos(a) + Z * math.sin(a))
            part.chain([top - X * 0.02 * s, top + d * 0.05 * s + X * 0.06 * s, top + d * 0.03 * s + X * 0.15 * s], [0.012 * s, 0.01 * s, 0.004 * s], 'wood_dark', [w] * 3, hint=X, n=6)
        part.ellipsoid(top + X * 0.09 * s, (0.05 * s, 0.05 * s, 0.05 * s), orb, w, seg=12, rings=8)


def sickle(part, B, side=-1):
    """A healer's sickle: a short handle, a crescent blade curving forward-down."""
    w, g, s = _hand(B, side)
    shaft(part, B, side, 0.06, 0.12, 0.014, 'wood')
    base = g + X * 0.12 * s
    pts, radii = [], []
    for k in range(9):
        a = math.pi * 0.95 * k / 8
        pts.append(base + (X * math.sin(a) * 0.16 + Z * (-(1 - math.cos(a)) * 0.13)) * s + X * 0.02 * s)
        radii.append((0.022 * (1 - 0.8 * k / 8) * s + 0.002, 0.004 * s))
    part.chain(pts, radii, 'blade', [w] * len(pts), hint=Y, n=6)


def satchel(part, B, side=-1, mat='leather', fuse=True):
    """A satchel charge in the fist: a bound leather bundle with a lit fuse."""
    w, g, s = _hand(B, side)
    c = g + Z * 0.0 + X * 0.0 - Z * 0.07 * s
    part.ellipsoid(c, (0.075 * s, 0.07 * s, 0.085 * s), mat, w, seg=12, rings=8)
    for dz in (-0.03, 0.03):
        part.chain([c + Z * dz * s - X * 0.08 * s + Y * 0, c + Z * dz * s + X * 0.08 * s], [0.006 * s] * 2, 'rope', [w, w], hint=Y, n=5)
    if fuse:
        part.chain([c + Z * -0.08 * s, c + Z * -0.13 * s + X * 0.03 * s], [0.006 * s] * 2, 'rope', [w, w], hint=X, n=5)
        part.ellipsoid(c + Z * -0.14 * s + X * 0.035 * s, (0.016 * s,) * 3, 'fire', w, seg=8, rings=5)


def bow(part, B, side=1, length=1.3, mat='wood_dark'):
    """A longbow in the fist, the stave along the haft line (upright once the
    arm is raised forward), its string toward the archer (+z at rest)."""
    w, g, s = _hand(B, side)
    half = length / 2 * s
    pts, radii = [], []
    for k in range(9):
        t = (k / 8) * 2 - 1
        pts.append(g + X * (t * half) + Z * (0.12 * s * t * t))
        rr = 0.016 * s * (1 - 0.55 * abs(t)) + 0.003
        radii.append((rr, rr * 1.2))
    part.chain(pts, radii, mat, [w] * 9, hint=Z, n=8)
    part.chain([pts[0], pts[-1]], [0.0025 * s] * 2, 'string', [w, w], hint=Z, n=4)
    part.chain([g - X * 0.05 * s, g + X * 0.05 * s], [0.02 * s] * 2, 'leather', [w, w], hint=Z, n=8)


def arrow(part, B, bone, at, along, length=0.75):
    """An arrow on its own bone (scaled to nothing when not on the string)."""
    w = {bone: 1.0}
    s = B.s
    end = at + along * length * s
    part.chain([at, end], [0.005 * s] * 2, 'wood_pale', [w, w], hint=Z, n=5)
    blade_profile(part, end, along, Z, 0.05 * s, 0.012 * s, 0.004 * s, 'steel_dark', w)
    up = frame_for(along, Z)[0]
    for k in range(3):
        a = 2 * math.pi * k / 3
        d = up * math.cos(a) + along.cross(up) * math.sin(a)
        part.grid(lambda u, v, d=d: at + along * ((0.02 + 0.11 * u) * s) + d * ((0.003 + 0.022 * v * (1 - 0.6 * u)) * s), 3, 1, 'feather_red', w)


def quiver(part, B, mat='leather', side=-1):
    """A quiver on the back, fletchings showing over the shoulder."""
    w = B.w('chest')
    a = B.P(-0.17, 0.06 * side, 1.08)
    b = B.P(-0.13, -0.1 * side, 1.62)
    part.chain([a, a.lerp(b, 0.5), b], [B.R(0.05), B.R(0.055), B.R(0.06)], mat, [B.w('spine', 0.5, 'chest', 0.5), w, w], hint=X, n=10, cap1=False)
    d = (b - a).normalized()
    r = Rand(17)
    for k in range(6):
        c = b + d * (0.04 + r.range(0, 0.06)) * B.s + Y * r.range(-0.035, 0.035) * B.s + X * r.range(-0.03, 0.03) * B.s
        part.ellipsoid(c + d * 0.05 * B.s, (0.012 * B.s, 0.025 * B.s, 0.06 * B.s), 'feather_red', w, axes=(X, Y, d), seg=6, rings=4)
    # The strap across the chest.
    strap = []
    for k in range(9):
        t = k / 8
        ang = math.pi * (0.15 + 0.7 * t)
        strap.append(B.P(0.13 * math.cos(ang) * 1.05 - 0.01, 0.2 * side * math.cos(math.pi * t) * 0.9, 1.05 + 0.42 * t))
    part.chain(strap, [B.R(0.012)] * 9, 'leather_dark', [B.w('spine', 0.5, 'chest', 0.5)] + [w] * 8, hint=Z, n=6)


def round_shield(part, B, face='team', rim='steel_dark', boss='steel', radius=0.32, side=1, band=None):
    """A round shield on the forearm, its face outward (+y for the left arm)."""
    sf = 'L' if side > 0 else 'R'
    w = B.w('forearm.' + sf)
    s = B.s
    elbow, wrist = B.P(-0.01, 0.235 * side, 1.15), B.P(0.0, 0.263 * side, 0.915)
    centre = elbow.lerp(wrist, 0.55) + Vector((0, 0.07 * side * s, 0))
    R = radius * s

    def fn(si, t):
        a = 2 * math.pi * si
        rr = R * t
        dome = 0.05 * s * (1 - t * t)
        return centre + X * (rr * math.cos(a)) + Z * (rr * math.sin(a)) + Y * (side * dome)

    def mat(si, t):
        if t > 0.9:
            return rim
        if band and abs(math.sin(2 * math.pi * si)) * t < 0.12 and t > 0.25:
            return band
        return face
    first = part.slab(fn, 24, 8, 0.018 * s, mat, w, rim_mat=rim, back_mat='wood')
    if side < 0:
        human._flip_since(part, first)
    part.ellipsoid(centre + Y * (side * 0.05 * s), (0.07 * s, 0.035 * s, 0.07 * s), boss, w, seg=12, rings=6)
    return first


# ------------------------------------------------------------------ heads

def _head(B):
    return human.head_center(B), human.head_radii(B), B.w('head')


def kettle_hat(part, B, steel='steel', brim=1.75):
    """An iron hat with a broad brim: the foot soldier's helm."""
    c, r, w = _head(B)
    rings = []
    for zz, k in ((0.0, brim), (0.04, brim), (0.06, 1.12), (0.4, 1.1), (0.75, 0.9), (1.0, 0.55), (1.12, 0.15)):
        rings.append({'c': c + Vector((-0.004 * B.s, 0, (zz + 0.08) * r[2])), 'u': X, 'v': Y, 'ru': r[0] * k * 1.02, 'rv': r[1] * k * 1.16, 'w': w, 'dome': 0.4})
    part.loft(rings, lambda j, s: 'steel_dark' if j == 0 else steel, n=28)


def sallet(part, B, steel='steel'):
    """A rounded helm drawn out into a tail over the nape, a visor slit."""
    c, r, w = _head(B)
    rows = [(-0.55, 1.15, 0.0), (-0.2, 1.17, 0.0), (0.15, 1.18, 0.0), (0.45, 1.14, 0.0), (0.8, 0.95, 0.0), (1.05, 0.6, 0.0), (1.15, 0.2, 0.0)]
    rings = []
    for zz, k, _ in rows:
        def shape(th, zz=zz):
            back = max(0.0, -math.cos(th))
            front = max(0.0, math.cos(th))
            tail = 0.35 * back ** 2 * smooth(0.0, -0.55, zz)
            cut = -0.25 * front ** 3 * smooth(-0.1, -0.5, zz)
            return 1.0 + tail + cut
        rings.append({'c': c + Vector((-0.01 * B.s, 0, zz * r[2])), 'u': X, 'v': Y, 'ru': r[0] * k, 'rv': r[1] * k * 1.08, 'w': w, 'shape': shape, 'dome': 0.4})

    def mat(j, s):
        th = s * 2 * math.pi
        if j == 2 and math.cos(th) > 0.5:
            return 'eye'
        return steel
    part.loft(rings, mat, n=28, cap0=False)


def plume(part, B, mat='team', length=0.42, rise=0.16, back=0.36, width=0.05, at=None, seed=3):
    """A plume or crest of feathers from the top of the helm, curving back."""
    c, r, w = _head(B)
    root = at if at is not None else c + Vector((0.0, 0, r[2] * 1.12))
    pts, radii = [], []
    for k in range(8):
        t = k / 7
        pts.append(root + Vector((-back * t * B.s, 0, (rise * math.sin(math.pi * t * 0.8) - 0.18 * t * t * length) * B.s)))
        rr = width * B.s * (0.5 + 1.4 * math.sin(math.pi * min(1.0, 0.1 + t)) ** 0.7)
        radii.append((rr * 0.55, rr * 1.4))
    part.chain(pts, radii, mat, [w] * 8, hint=Y, n=10, shape=lambda i, th: 1.0 + 0.12 * math.sin(th * 7 + i))


def crest(part, B, mat='team', height=0.1):
    """A comb crest front to back over the helm (a fan of horsehair)."""
    c, r, w = _head(B)

    def fn(u, v):
        a = math.pi * (0.1 + 0.95 * u)
        rad = Vector((math.cos(a) * r[0] * 1.15, 0, math.sin(a) * r[2] * 1.1))
        return c + rad * (1 + v * height * B.s / r[2]) + Vector((0, 0, 0.01 * B.s))
    first = part.slab(fn, 10, 2, 0.025 * B.s, mat, w)
    for i in range(first, len(part.verts)):
        part.verts[i] += Y * 0.0125 * B.s


def hood(part, B, mat='team', cowl=True, point=0.12):
    """A hood over the head, open at the face, its cowl over the shoulders."""
    c, r, w = _head(B)

    def shape(q):
        face = smooth(0.25, 0.6, q.x) * smooth(0.5, 0.05, q.z) * smooth(0.95, 0.4, abs(q.y))
        return 1.2 - 0.32 * face + 0.05 * smooth(0.0, -0.8, q.x)
    part.ellipsoid(c + Vector((-0.01 * B.s, 0, 0.012 * B.s)), r, mat, w, seg=28, rings=18, shape=shape)
    if point:
        part.chain([c + Vector((-r[0] * 0.9, 0, r[2] * 0.7)), c + Vector((-r[0] * 1.5, 0, r[2] * 0.4)), c + Vector((-r[0] * 1.9, 0, -r[2] * 0.2))],
                   [r[1] * 0.5, r[1] * 0.3, r[1] * 0.05], mat, [w] * 3, hint=Y, n=8)
    if cowl:
        rows = [(c.z - r[2] * 0.95, 0.85, 1.05, B.w('head', 0.6, 'neck', 0.4)),
                (B.P(0, 0, 1.55).z, 1.15, 1.35, B.w('neck', 0.6, 'chest', 0.4)),
                (B.P(0, 0, 1.49).z, 1.45, 2.3, B.w('chest')),
                (B.P(0, 0, 1.4).z, 1.5, 2.45, B.w('chest'))]
        rings = []
        for z, kx, ky, ww in rows:
            rings.append({'c': Vector((c.x - 0.02 * B.s, 0, z)), 'u': X, 'v': Y, 'ru': r[0] * kx, 'rv': r[1] * ky, 'w': ww, 'shape': human.superellipse(2.3)})
        part.loft(rings, mat, n=24, cap0=False, cap1=False)


def wizard_hat(part, B, mat='wizard_hat', band='gold', height=0.42, brim=2.2):
    """A tall pointed hat, its tip bent back, a broad brim."""
    c, r, w = _head(B)
    base = c + Vector((-0.005 * B.s, 0, r[2] * 0.45))
    rings = [{'c': base - Z * 0.01 * B.s, 'u': X, 'v': Y, 'ru': r[0] * brim, 'rv': r[1] * brim * 1.25, 'w': w, 'shape': lambda th: 1 + 0.04 * math.sin(th * 5)},
             {'c': base + Z * 0.008 * B.s, 'u': X, 'v': Y, 'ru': r[0] * brim * 0.97, 'rv': r[1] * brim * 1.22, 'w': w},
             {'c': base + Z * 0.012 * B.s, 'u': X, 'v': Y, 'ru': r[0] * 1.15, 'rv': r[1] * 1.35, 'w': w}]
    part.loft(rings, mat, n=24, cap0=True, cap1=False)
    pts, radii = [], []
    for k in range(7):
        t = k / 6
        pts.append(base + Vector((-0.12 * t ** 2 * B.s - 0.08 * smooth(0.6, 1.0, t) * B.s, 0, height * B.s * (t - 0.12 * smooth(0.6, 1.0, t)))))
        radii.append((r[0] * 1.12 * (1 - t) ** 1.1 + 0.004, r[1] * 1.32 * (1 - t) ** 1.1 + 0.004))
    part.chain(pts, radii, lambda j, s: band if j == 0 else mat, [w] * 7, hint=X, n=16)


def crown(part, B, mat='gold', points=6):
    c, r, w = _head(B)
    base = c + Vector((-0.004 * B.s, 0, r[2] * 0.5))
    rings = []
    for dz, k in ((0.0, 1.13), (0.035, 1.14), (0.04, 1.08), (0.0, 1.07)):
        rings.append({'c': base + Z * dz * B.s, 'u': X, 'v': Y, 'ru': r[0] * k, 'rv': r[1] * k * 1.08, 'w': w})
    part.loft(rings, mat, n=24, cap0=False, cap1=False)
    for k in range(points):
        a = 2 * math.pi * k / points
        p = base + Vector((math.cos(a) * r[0] * 1.12, math.sin(a) * r[1] * 1.2, 0.03 * B.s))
        part.chain([p, p + Z * 0.06 * B.s], [0.012 * B.s, 0.002 * B.s], mat, [w, w], hint=X, n=5)
        part.ellipsoid(p + Z * 0.065 * B.s, (0.009 * B.s,) * 3, 'ruby', w, seg=6, rings=4)


def dwarf_helm(part, B, steel='steel', horns=False, guards=True, goggles=False):
    """A round dwarven helm with a brow band, nasal and cheek guards."""
    c, r, w = _head(B)
    rows = [(0.0, 1.2), (0.05, 1.2), (0.08, 1.16), (0.4, 1.12), (0.75, 0.92), (1.0, 0.55), (1.1, 0.2)]
    rings = []
    for zz, k in rows:
        rings.append({'c': c + Vector((-0.004 * B.s, 0, (zz + 0.05) * r[2])), 'u': X, 'v': Y, 'ru': r[0] * k * 1.03, 'rv': r[1] * k * 1.12, 'w': w, 'dome': 0.45})
    part.loft(rings, lambda j, s: 'gold' if j <= 1 else steel, n=28)
    if guards:
        part.box(c + Vector((r[0] * 1.18, 0, -0.25 * r[2])), AXES, (0.008 * B.s, 0.014 * B.s, 0.32 * r[2]), steel, w)
    for side in (1, -1):
        if guards:
            part.ellipsoid(c + Vector((0.2 * r[0], side * r[1] * 1.08, -0.35 * r[2])), (r[0] * 0.45, r[1] * 0.12, r[2] * 0.42), steel, w, seg=10, rings=6)
        if goggles:
            g = c + Vector((r[0] * 0.95, side * r[1] * 0.38, r[2] * 0.42))
            part.ellipsoid(g, (r[0] * 0.14, r[1] * 0.24, r[2] * 0.17), 'bronze', w, seg=10, rings=6)
            part.ellipsoid(g + Vector((r[0] * 0.1, 0, 0)), (r[0] * 0.06, r[1] * 0.17, r[2] * 0.12), 'glow', w, seg=8, rings=5)
        if horns:
            horn(part, B, side, length=0.16, spread=0.9, rise=0.6, curl=-0.2, r0=0.02, mat='horn')


def horn(part, B, side, length=0.18, spread=1.0, rise=0.8, curl=0.3, back=0.2, r0=0.025, mat='horn', at=None):
    """A horn from the temple: out, up, and curling (curl > 0: forward at the tip)."""
    c, r, w = _head(B)
    root = at if at is not None else c + Vector((0.1 * r[0], side * r[1] * 0.75, 0.55 * r[2]))
    d0 = Vector((-back, side * spread, rise)).normalized()
    pts, radii = [], []
    p = root
    d = d0
    seg = length * B.s / 6
    for k in range(7):
        pts.append(p.copy())
        radii.append(r0 * B.s * (1 - 0.9 * k / 6) + 0.0015)
        # Bend toward up-then-forward as it grows.
        d = (d + Vector((curl * 0.35, 0, 0.12 * (1 if rise > 0 else -1))) * 0.5).normalized()
        p = p + d * seg
    part.chain(pts, radii, mat, [w] * 7, hint=X, n=8)


def beard(part, B, mat, length=0.22, width=1.0, forked=False):
    """A beard from the jaw down over the chest (and moustache)."""
    c, r, w = _head(B)
    rings = []
    for k in range(7):
        t = k / 6
        z = c.z - r[2] * 0.35 - length * B.s * t
        x = c.x + r[0] * (0.72 + 0.3 * t - 0.15 * t * t)
        ww = blend(w, B.w('chest'), smooth(0.25, 0.9, t))
        rings.append({'c': Vector((x, c.y, z)), 'u': X, 'v': Y, 'ru': r[0] * (0.55 - 0.3 * t), 'rv': r[1] * width * (0.95 - 0.5 * t ** 1.5) + 0.004, 'w': ww,
                      'shape': lambda th, t=t: 1.0 + 0.07 * math.sin(th * 8 + t * 5) - (0.35 * t * max(0.0, math.cos(th * 2)) if forked else 0)})
    part.loft(rings, mat, n=16, cap0=False, cap1=True)
    # Cheeks and moustache.
    for side in (1, -1):
        part.ellipsoid(c + Vector((r[0] * 0.62, side * r[1] * 0.68, -r[2] * 0.32)), (r[0] * 0.38, r[1] * 0.35, r[2] * 0.42), mat, w, seg=10, rings=6)
        part.chain([c + Vector((r[0] * 1.05, side * 0.02 * B.s, -r[2] * 0.34)), c + Vector((r[0] * 0.92, side * r[1] * 0.55, -r[2] * 0.5))],
                   [r[2] * 0.1, r[2] * 0.06], mat, [w, w], hint=Z, n=6)


def wild_hair(part, B, mat, length=0.25, spikes=14, seed=9):
    """Shaggy hair: a shell and locks falling about the shoulders."""
    human.hair_shell(part, B, mat, long=length * 0.7 * B.s)
    c, r, w = _head(B)
    rnd = Rand(seed)
    for k in range(spikes):
        a = math.pi * (0.35 + 1.3 * k / (spikes - 1))
        d = Vector((math.cos(a), math.sin(a), 0))
        root = c + Vector((d.x * r[0] * 0.9, d.y * r[1] * 0.95, r[2] * rnd.range(0.2, 0.7)))
        L = length * B.s * rnd.range(0.6, 1.0)
        tip = root + d * 0.06 * B.s + Vector((-0.04 * B.s, 0, -L))
        mid = root.lerp(tip, 0.5) + d * 0.04 * B.s
        ww = [w, blend(w, B.w('chest'), 0.4), blend(w, B.w('chest'), 0.7)]
        part.chain([root, mid, tip], [r[1] * 0.22, r[1] * 0.16, r[1] * 0.03], mat, ww, hint=Z, n=6)


def pointed_ears(part, B, skin, length=0.12):
    c, r, w = _head(B)
    for side in (1, -1):
        root = c + Vector((-0.1 * r[0], side * r[1] * 0.9, 0.0))
        part.chain([root, root + Vector((-0.02, side * 0.6, 0.35)).normalized() * length * 0.5 * B.s, root + Vector((-0.05, side * 0.75, 0.55)).normalized() * length * B.s],
                   [(r[2] * 0.22, r[1] * 0.08), (r[2] * 0.16, r[1] * 0.06), (0.003, 0.002)], skin, [w] * 3, hint=X, n=8)


# ------------------------------------------------------------------ clothes

def cape(part, B, mat='team', z_bottom=0.3, width=1.0, seed=19):
    """A cloak from the shoulders down the back, following the legs below."""
    from .outfit import folds, skirt_weights
    f = folds(seed, n=6)
    rings = []
    rows = 10
    for j in range(rows + 1):
        t = j / rows
        z = 1.5 + (z_bottom - 1.5) * t
        hd = 0.12 + 0.11 * t
        hw = (0.2 + 0.1 * t) * width
        if z > 1.05:
            ww = (lambda th, z=z: B.w('chest', smooth(1.05, 1.35, z), 'spine', 1 - smooth(1.05, 1.35, z)))
        else:
            ww = (lambda th, z=z: blend(B.w('spine'), skirt_weights(B, z, th), smooth(1.05, 0.75, z)))
        rings.append({'c': B.P(-0.03 - 0.04 * t, 0, z), 'u': X, 'v': Y, 'ru': B.R(hd), 'rv': B.R(hw), 'w': ww, 'a0': math.pi * 0.42, 'a1': math.pi * 1.58,
                      'shape': lambda th, t=t: 1.0 + f(th, 0.08 * t)})
    return part.loft(rings, mat, n=20, cap0=False, cap1=False, closed=False, uv_scale=(3, 4))


def kilt(part, B, mat, z_bottom=0.6, seed=29, n=26, depth=0.05, grow=1.0):
    from .outfit import skirt
    return skirt(part, B, mat, 1.0, z_bottom, (0.11 * grow, 0.17 * grow), (0.15 * grow, 0.22 * grow), rows=6, n=n, fold_depth=depth, seed=seed)


def tabard(part, B, mat='team', z_bottom=0.6, seed=37):
    """A tabard: front and back panels over the body, open at the sides."""
    from .outfit import skirt
    human.torso(part, B, mat, grow=0.02, z_from=0.93, z_to=1.49)
    for a0 in (-1.0, math.pi - 1.0):
        skirt(part, B, mat, 1.0, z_bottom, (0.125, 0.18), (0.16, 0.22), fold_depth=0.03, seed=seed + int(a0), a=(a0, a0 + 2.0), n=14, rows=6)


def pouches(part, B, mat='leather', n=4, z=0.99, fill='leather'):
    """Pouches round the belt, front and sides."""
    for k in range(n):
        a = -1.1 + 2.2 * k / max(1, n - 1)
        p = B.P(0.15 * math.cos(a) * 1.15, 0.2 * math.sin(a) * 1.05, z - 0.06)
        part.ellipsoid(p, (B.R(0.035), B.R(0.04), 0.05 * B.s), fill, B.w('hips'), seg=10, rings=6)


def bandolier(part, B, mat='leather_dark', charges='charge', n=5, side=1):
    """A strap from shoulder to hip, charges along it."""
    w = B.w('chest')
    pts = []
    for k in range(9):
        t = k / 8
        ang = math.pi * (0.1 + 0.8 * t)
        pts.append(B.P(0.14 * math.sin(ang) * 1.05 + 0.005, 0.19 * side * math.cos(math.pi * t), 1.48 - 0.46 * t))
    ws = [w if t < 5 else B.w('spine') for t in range(9)]
    part.chain(pts, [B.R(0.016)] * 9, mat, ws, hint=X, n=6)
    for k in range(n):
        p = pts[1 + k]
        part.ellipsoid(p + X * B.R(0.025), (B.R(0.026), B.R(0.03), 0.034 * B.s), charges, ws[1 + k], seg=8, rings=5)
