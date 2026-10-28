"""The roster on foot, continued: dwarves, giants, minotaurs, wizards,
devils and demons, the undead, the hero, journeymen, warriors, berserkers."""

import math

from mathutils import Vector

from . import gear, human, outfit, people, rig
from .geo import Part, blend, smooth
from .human import X, Y, Z
from .people import Kit
from .roster import body, boots, head
from .units import unit

AX = (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))


# ---------------------------------------------------------------- shared pieces

def robe(part, B, mat, trim=None, z_bottom=0.07, cuff=None, seed=11):
    """A long robe from the waist to the ankles, wide sleeves."""
    outfit.skirt(part, B, (lambda j, s: trim if trim and j == 11 else mat), 1.0, z_bottom, (0.11, 0.17), (0.25, 0.31), rows=12, n=36,
                 fold_depth=0.06, seed=seed, grow_pow=1.3, hem=lambda th: 0.03 * math.sin(th * 5))
    for side in (1, -1):
        outfit.sleeve(part, B, side, mat, flare=0.06, end=6, cuff=cuff or trim)


def tail_bones(B, n, start, end, parent='hips', droop=0.0):
    """A tail of n bones from start to end (sagging by droop at the middle)."""
    out = []
    prev = B.n(parent)
    pts = [start.lerp(end, k / n) + Vector((0, 0, -droop * math.sin(math.pi * k / n))) for k in range(n + 1)]
    for k in range(n):
        out.append(rig.Bone(B.n('tail%d' % k), pts[k], pts[k + 1], prev, (0, 0, 1)))
        prev = B.n('tail%d' % k)
    return out, pts


def tail_mesh(part, B, pts, mat, r0, r1, tip=None, tuft=None):
    n = len(pts) - 1
    ws = []
    for k in range(n + 1):
        a = B.n('tail%d' % min(k, n - 1))
        b = B.n('tail%d' % max(0, k - 1))
        ws.append(blend({a: 1.0}, {b: 1.0}, 0.5) if 0 < k < n else {a: 1.0})
    ws[0] = blend(B.w('hips'), {B.n('tail0'): 1.0}, 0.5)
    radii = [r0 + (r1 - r0) * k / n for k in range(n + 1)]
    part.chain(pts, radii, mat, ws, hint=Vector((0, 0, 1)), n=10)
    end = pts[-1]
    d = (pts[-1] - pts[-2]).normalized()
    last = {B.n('tail%d' % (n - 1)): 1.0}
    if tip:  # a spade at the tip
        gear.blade_profile(part, end - d * 0.02, d, Vector((0, 0, 1)), 0.18 * B.s, 0.07 * B.s, 0.012 * B.s, tip, last, point=0.45, taper=0.0)
    if tuft:
        side = Vector((0, 1, 0))
        part.ellipsoid(end + d * 0.05 * B.s, (0.09 * B.s, 0.05 * B.s, 0.05 * B.s), tuft, last, axes=(d, side, d.cross(side)), seg=10, rings=6)


def tail_sway(p, B, n, anim, i, nn, amount=10.0, lift=0.0):
    ph = 2 * math.pi * i / nn
    for k in range(n):
        sway = amount * math.sin(ph + k * 0.7) * (1.0 if anim in ('idle', 'walk') else 0.5)
        p.turn(B.n('tail%d' % k), Z, sway).turn(B.n('tail%d' % k), Y, -lift / n)


def bare_body(B, skin, legs, feet, hands=None):
    """A bare-chested body: skin above the waist, legs in legs (a function or a material)."""
    return body(B, skin, skin, legs, feet=feet, hands=hands or skin, skin=skin, sleeve=0.0, leg_sleeve=0.0, torso_grow=0.0)


# ---------------------------------------------------------------- dwarves

@unit(6, 'dwarves')
def dwarves():
    """A dwarf of the legions: short and broad, mail to the knees under a
    tabard in the side's colour, a gilt-banded helm, a great red beard, an
    axe and a round shield in the side's colour."""
    B = human.Body(1.35, 1.45, legs=0.62, head=1.22)
    kit = Kit('axe', shield='round', heavy=1.3)
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'mail', 'mail', boots('mail', 'leather', 4), feet='leather_dark', hands='leather', sleeve=0.018, leg_sleeve=0.014)
    h = head(B)
    gear.dwarf_helm(h, B)
    gear.beard(h, B, 'hair_red', length=0.3, width=1.15)
    m = Part('mail')
    outfit.coif(m, B)
    outfit.skirt(m, B, 'mail', 1.0, 0.5, (0.105, 0.17), (0.14, 0.2), fold_depth=0.02, seed=5)
    tb = Part('tabard')
    gear.tabard(tb, B, 'team', z_bottom=0.55)
    outfit.belt(tb, B, 'leather_dark', 1.02, 0.172, 0.118, height=0.08, buckle='gold')
    sh = Part('shield')
    gear.round_shield(sh, B, radius=0.36, band='steel')
    ax = Part('axe')
    gear.axe(ax, B, side=-1, haft=0.65, size=1.25)
    people.make(col, arm, L, parts + [(h, 2, 0), (m, 2, 0.005), (tb, 2, 0.007), (sh, 1, 0), (ax, 1, 0)])
    return people.animate(arm, B, kit)


@unit(22, 'dwarf_grenadier')
def dwarf_grenadier():
    """A dwarven grenadier: leather jerkin and cap, a bandolier and pouches of
    charges, a satchel charge in the fist with its fuse alight (thrown)."""
    B = human.Body(1.3, 1.4, legs=0.62, head=1.22)
    kit = Kit('satchel', strike='throw', heavy=1.3)
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'leather', 'gambeson', boots('hose', 'leather', 4), feet='leather_dark', hands='skin', sleeve=0.016, leg_sleeve=0.01)
    h = head(B)
    gear.dwarf_helm(h, B, steel='leather', guards=False, goggles=True)
    gear.beard(h, B, 'hair_brown', length=0.28, width=1.1, forked=True)
    tu = Part('tunic')
    human.torso(tu, B, 'team', grow=0.02, z_from=0.93, z_to=1.44)
    outfit.skirt(tu, B, 'team', 1.0, 0.6, (0.12, 0.18), (0.15, 0.22), fold_depth=0.03, seed=51, n=22, rows=5)
    outfit.belt(tu, B, 'leather_dark', 1.02, 0.172, 0.118, height=0.07, buckle='steel')
    gear.bandolier(tu, B, charges='charge', n=5)
    gear.pouches(tu, B, n=4, z=1.0, fill='charge')
    sat = Part('satchel')
    gear.satchel(sat, B, side=-1)
    sat.weights = [{B.n('held'): 1.0} for _ in sat.weights]
    people.make(col, arm, L, parts + [(h, 2, 0), (tu, 2, 0.007), (sat, 1, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- giants

@unit(8, 'giants')
def giants():
    """A giant: huge, shaggy, a fur over the shoulders and a kilt in the side's
    colour, an uprooted club swung in both hands."""
    B = human.Body(5.5, 1.3, head=1.05)
    kit = Kit('club', strike='overhead', heavy=2.5)
    col, arm, L = people.setup(B, kit)
    parts = bare_body(B, 'skin_giant', boots('skin_giant', 'fur', 6), 'leather')
    h = head(B, 'skin_giant')
    gear.wild_hair(h, B, 'hair_brown', length=0.3)
    gear.beard(h, B, 'hair_brown', length=0.26, width=1.1)
    k = Part('kilt')
    gear.kilt(k, B, 'team', z_bottom=0.58, depth=0.07)
    outfit.belt(k, B, 'leather', 1.02, 0.17, 0.115, height=0.07, buckle='bronze')
    pelt = Part('pelt')
    human.torso(pelt, B, 'fur', grow=0.02, z_from=1.22, z_to=1.49)
    cl = Part('club')
    gear.club(cl, B, side=-1, length=1.2, thick=0.075)
    people.make(col, arm, L, parts + [(h, 2, 0), (k, 2, 0.008), (pelt, 2, 0.01), (cl, 1, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- minotaurs

def bull_head(part, B, hide='hide_bull', horn='ivory'):
    """A bull's head on a man's neck: a broad skull, a long muzzle, horns sweeping out and forward."""
    c, r = human.head_center(B), human.head_radii(B)
    w = B.w('head')
    part.ellipsoid(c + Vector((-0.01 * B.s, 0, 0.02 * B.s)), (r[0] * 1.05, r[1] * 1.3, r[2] * 0.95), hide, w, seg=20, rings=12)
    a = c + Vector((r[0] * 0.5, 0, -r[2] * 0.05))
    b = c + Vector((r[0] * 1.9, 0, -r[2] * 0.75))
    part.chain([a, a.lerp(b, 0.5), b], [(r[2] * 0.75, r[1] * 1.0), (r[2] * 0.62, r[1] * 0.85), (r[2] * 0.55, r[1] * 0.8)], hide, [w] * 3, hint=Vector((0, 0, 1)), n=14, dome=0.6)
    part.ellipsoid(b + Vector((r[0] * 0.15, 0, -r[2] * 0.05)), (r[0] * 0.35, r[1] * 0.75, r[2] * 0.45), 'horn_dark', w, seg=12, rings=6)
    for side in (1, -1):
        part.ellipsoid(c + Vector((r[0] * 0.55, side * r[1] * 0.95, r[2] * 0.15)), (r[0] * 0.12, r[1] * 0.12, r[2] * 0.12), 'eye_red', w, seg=8, rings=5)
        # Ears out to the side, horns above them sweeping out, up and forward.
        part.chain([c + Vector((-r[0] * 0.1, side * r[1] * 1.2, r[2] * 0.2)), c + Vector((-r[0] * 0.2, side * r[1] * 2.0, r[2] * 0.0))],
                   [(r[2] * 0.2, r[1] * 0.1), (r[2] * 0.08, r[1] * 0.04)], hide, [w, w], hint=Vector((1, 0, 0)), n=8)
        gear.horn(part, B, side, length=0.42, spread=1.0, rise=0.15, curl=0.9, back=-0.1, r0=0.05, mat=horn,
                  at=c + Vector((0.0, side * r[1] * 1.05, r[2] * 0.55)))


@unit(9, 'minotaurs')
def minotaurs():
    """A minotaur: a bull's head and horns on a huge furred body, hooves, a
    tail, a kilt in the side's colour and a double-bitted axe."""
    B = human.Body(3.0, 1.45, legs=0.95, head=1.1)
    kit = Kit('axe2', strike='overhead', heavy=1.8)
    tb, tpts = tail_bones(B, 4, B.P(-0.16, 0, 0.98), B.P(-0.4, 0, 0.35), droop=0.05)
    col, arm, L = people.setup(B, kit, tb)
    parts = bare_body(B, 'hide_bull', 'hide_bull', 'horn_dark')
    h = Part('head')
    bull_head(h, B)
    k = Part('kilt')
    gear.kilt(k, B, 'team', z_bottom=0.6, depth=0.06)
    outfit.belt(k, B, 'leather_dark', 1.02, 0.17, 0.115, height=0.07, buckle='bronze')
    t = Part('tail')
    tail_mesh(t, B, tpts, 'hide_bull', 0.03 * B.s, 0.012 * B.s, tuft='fur_dark')
    ax = Part('axe')
    gear.axe(ax, B, side=-1, haft=1.05, butt=0.25, double=True, size=1.6, mat='iron')
    people.make(col, arm, L, parts + [(h, 2, 0), (k, 2, 0.008), (t, 2, 0), (ax, 1, 0)])
    return people.animate(arm, B, kit, extra=lambda p, anim, i, n, st: tail_sway(p, B, 4, anim, i, n))


# ---------------------------------------------------------------- wizards

@unit(15, 'wizards')
def wizards():
    """A wizard: a robe in the side's colour, gold at the hem, a tall dark
    hat, a long white beard, a staff crowned with a glowing stone; the spell
    gathers as light in the other hand."""
    B = human.Body(1.9, 0.95)
    kit = Kit('staff', strike='cast', robe=True)
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'team', 'team', 'hose', feet='leather_dark', hands='skin_pale', skin='skin_pale', grip=(False, True))
    h = head(B, 'skin_pale')
    human.hair_shell(h, B, 'hair_white', long=0.15 * B.s)
    gear.wizard_hat(h, B)
    gear.beard(h, B, 'hair_white', length=0.42, width=1.0)
    rb = Part('robe')
    robe(rb, B, 'team', trim='gold')
    outfit.belt(rb, B, 'gold', 1.04, 0.17, 0.12, height=0.04)
    st = Part('staff')
    gear.staff(st, B, side=-1)
    orb = Part('orb')
    gl = human.grip_point(B, 1)
    orb.ellipsoid(gl + Vector((0.03 * B.s, -0.02 * B.s, -0.03 * B.s)), (0.1 * B.s,) * 3, 'glow', {B.n('orb'): 1.0}, seg=12, rings=8)
    people.make(col, arm, L, parts + [(h, 2, 0), (rb, 2, 0.006), (st, 1, 0), (orb, 1, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- devils and demons

@unit(17, 'devils')
def devils():
    """A devil: red-skinned and horned, cloven hooves, a spade-tipped tail, a
    kilt in the side's colour, a trident."""
    B = human.Body(3.0, 1.25, head=1.05)
    kit = Kit('trident', strike='thrust', heavy=1.5)
    tb, tpts = tail_bones(B, 5, B.P(-0.15, 0, 0.98), B.P(-0.75, 0, 0.3), droop=0.15)
    col, arm, L = people.setup(B, kit, tb)
    parts = bare_body(B, 'skin_devil', 'skin_devil', 'horn_dark')
    h = head(B, 'skin_devil', eye='eye_red')
    human.hair_shell(h, B, 'hair_black')
    for side in (1, -1):
        gear.horn(h, B, side, length=0.3, spread=0.45, rise=1.0, curl=-0.5, back=0.35, r0=0.045, mat='horn')
    gear.pointed_ears(h, B, 'skin_devil', length=0.1)
    k = Part('kilt')
    gear.kilt(k, B, 'team', z_bottom=0.62, depth=0.05)
    outfit.belt(k, B, 'leather_dark', 1.02, 0.17, 0.115, height=0.06, buckle='gold')
    t = Part('tail')
    tail_mesh(t, B, tpts, 'skin_devil', 0.028 * B.s, 0.01 * B.s, tip='skin_devil')
    tr = Part('trident')
    gear.spear(tr, B, side=-1, length=2.2, butt=0.7, head=0.24, trident=True, mat='iron', haft_mat='iron')
    people.make(col, arm, L, parts + [(h, 2, 0), (k, 2, 0.008), (t, 2, 0), (tr, 1, 0)])
    return people.animate(arm, B, kit, extra=lambda p, anim, i, n, st: tail_sway(p, B, 5, anim, i, n, 14, lift=20))


def bat_wings_pose(bw, anim, i, n, rest=(0.55, 25, 25), walk=None):
    """Wings for a creature on foot: half folded at rest, flared in the blow, limp in death."""
    t = i / n
    u = i / (n - 1)
    f0, l0, s0 = rest
    if anim == 'idle':
        b = math.sin(2 * math.pi * t)
        fold, lift, sweep = f0 + 0.05 * b, l0 + 4 * b, s0
    elif anim == 'walk':
        b = math.sin(2 * math.pi * t)
        fold, lift, sweep = f0 + 0.05, l0 + 5 * b, s0 + 5
    elif anim == 'attack':
        fold = [f0, 0.15, 0.05, 0.1, 0.3, f0 - 0.1][i]
        lift = [l0, 40, 45, -10, 0, l0 - 10][i]
        sweep = [s0, 5, 0, 5, 15, s0 - 5][i]
    elif anim == 'flinch':
        fold, lift, sweep = [0.3, 0.45][i], [40, 30][i], [10, 20][i]
    else:
        fold, lift, sweep = f0 - 0.3 * smooth(0, 0.6, u), l0 - 45 * smooth(0.2, 1.0, u), s0 - s0 * u
    return fold, lift, sweep


@unit(18, 'demons')
def demons():
    """A demon: tall and purple-black, ram's horns, great bat wings, a tail, a
    kilt in the side's colour and a two-handed sword."""
    from .batwing import BatWing
    B = human.Body(3.6, 1.35, head=1.05)
    kit = Kit('greatsword', strike='overhead', heavy=1.8)
    tb, tpts = tail_bones(B, 5, B.P(-0.15, 0, 0.98), B.P(-0.8, 0, 0.25), droop=0.12)
    bw = [BatWing('', side, B.P(-0.12, 0.08 * side, 1.38), span=1.5 * B.s, body_attach=B.P(-0.14, 0.15 * side, 1.02),
                  membrane='membrane_dark', bone_mat='skin_demon') for side in (1, -1)]
    more = tb + [b for w in bw for b in w.bones()]
    col, arm, L = people.setup(B, kit, more)
    parts = bare_body(B, 'skin_demon', 'skin_demon', 'horn_dark')
    h = head(B, 'skin_demon', eye='eye_red')
    for side in (1, -1):
        gear.horn(h, B, side, length=0.32, spread=1.0, rise=0.4, curl=-0.9, back=0.6, r0=0.045, mat='horn')
    gear.pointed_ears(h, B, 'skin_demon', length=0.1)
    k = Part('kilt')
    gear.kilt(k, B, 'team', z_bottom=0.58, depth=0.06)
    outfit.belt(k, B, 'leather_dark', 1.02, 0.17, 0.115, height=0.07, buckle='iron')
    t = Part('tail')
    tail_mesh(t, B, tpts, 'skin_demon', 0.03 * B.s, 0.01 * B.s, tip='skin_demon')
    mem, wb = Part('membrane'), Part('wing_bones')
    for w in bw:
        w.build(mem, wb)
    sw = Part('sword')
    gear.greatsword(sw, B, side=-1, blade=1.2)
    people.make(col, arm, L, parts + [(h, 2, 0), (k, 2, 0.008), (t, 2, 0), (mem, 1, 0.006), (wb, 1, 0), (sw, 0, 0)])

    def extra(p, anim, i, n, st):
        tail_sway(p, B, 5, anim, i, n, 12, lift=15)
        fold, lift, sweep = bat_wings_pose(bw, anim, i, n)
        for w in bw:
            w.pose(p, fold, lift, sweep, twist=10)
    return people.animate(arm, B, kit, extra=extra)


# ---------------------------------------------------------------- undead

def skeleton_body(B, mat='bone'):
    """Bare bones: a spine and ribcage, a pelvis, thin limbs with knobbed joints."""
    b = Part('bones')
    P, w = B.P, B.w
    b.chain([P(-0.03, 0, 0.98), P(-0.04, 0, 1.15), P(-0.04, 0, 1.32), P(-0.02, 0, 1.5), P(0.0, 0, 1.6)],
            [B.R(0.025)] * 5, mat, [w('hips'), w('spine'), w('spine', 0.5, 'chest', 0.5), w('chest'), w('neck')], n=8)
    for k in range(6):
        z = 1.2 + 0.05 * k
        hw, hd = 0.12 + 0.03 * math.sin(math.pi * (k + 1) / 7), 0.085
        pts = []
        for j in range(11):
            a = -2.75 + 5.5 * j / 10
            pts.append(P(-0.01 + hd * math.cos(a), hw * math.sin(a), z - 0.03 * abs(math.sin(a))))
        b.chain(pts, [B.R(0.011)] * len(pts), mat, [w('chest') if z > 1.3 else w('spine', 0.4, 'chest', 0.6)] * len(pts), hint=Vector((0, 0, 1)), n=6)
    b.box(P(0.08, 0, 1.34), AX, (B.R(0.012), B.R(0.02), 0.09 * B.s), mat, w('chest'))
    b.ellipsoid(P(-0.005, 0, 0.98), (B.R(0.07), B.R(0.15), 0.06 * B.s), mat, w('hips'), seg=14, rings=8)
    for side in (1, -1):
        sf = 'L' if side > 0 else 'R'
        b.chain([P(0.04, 0.02 * side, 1.46), P(0.0, 0.18 * side, 1.46)], [B.R(0.014)] * 2, mat, [w('chest'), w('clavicle.' + sf)], n=6)
        AP, _, AW = human.arm_rows(B, side)
        b.chain([AP[1], AP[4]], [B.R(0.022), B.R(0.02)], mat, [AW[1], AW[4]], n=8)
        b.ellipsoid(AP[4], (B.R(0.028),) * 3, mat, AW[4], seg=8, rings=5)
        b.chain([AP[4], AP[7]], [B.R(0.018), B.R(0.016)], mat, [AW[4], AW[7]], n=8)
        y = 0.1 * side
        lw = [w('hips', 0.5, 'thigh.' + sf, 0.5), w('thigh.' + sf), w('thigh.' + sf, 0.5, 'shin.' + sf, 0.5), w('shin.' + sf), w('shin.' + sf, 0.5, 'foot.' + sf, 0.5)]
        lp = [P(0.0, y * 0.9, 0.95), P(0.01, y, 0.74), P(0.016, y, 0.52), P(0.0, y, 0.3), P(-0.01, y * 1.05, 0.09)]
        b.chain(lp[:3], [B.R(0.03), B.R(0.027), B.R(0.025)], mat, lw[:3], n=8)
        b.ellipsoid(lp[2] + Vector((0.02 * B.s, 0, 0)), (B.R(0.035),) * 3, mat, lw[2], seg=8, rings=5)
        b.chain(lp[2:], [B.R(0.025), B.R(0.02), B.R(0.022)], mat, lw[2:], n=8)
    e = Part('ends')
    for side in (1, -1):
        human.hand(e, B, side, mat, grip=True)
        human.foot(e, B, side, mat)
    return [(b, 1, 0), (e, 2, 0)]


@unit(19, 'undead')
def undead():
    """A risen skeleton: bare bones, a grinning skull with green fire in its
    eyes, a rusted helm, rags of a tabard in the side's colour, a notched
    sword and a round shield in the side's colour."""
    B = human.Body(1.8, 0.85)
    kit = Kit('sword', shield='round')
    col, arm, L = people.setup(B, kit)
    parts = skeleton_body(B)
    h = Part('skull')
    human.head(h, B, 'bone', eye_mat='eye_glow', ears=False)
    c, r = human.head_center(B), human.head_radii(B)
    for side in (1, -1):
        h.ellipsoid(c + Vector((r[0] * 0.78, side * r[1] * 0.33, r[2] * 0.12)), (r[0] * 0.14, r[1] * 0.2, r[2] * 0.14), 'mouth', B.w('head'), seg=8, rings=5)
    h.box(c + Vector((r[0] * 0.85, 0, -r[2] * 0.42)), AX, (r[0] * 0.12, r[1] * 0.45, r[2] * 0.06), 'teeth', B.w('head'))
    gear.kettle_hat(h, B, steel='rust', brim=1.5)
    rag = Part('rags')
    human.torso(rag, B, 'team', grow=0.035, z_from=1.22, z_to=1.49)
    outfit.skirt(rag, B, 'team', 1.0, 0.55, (0.12, 0.17), (0.15, 0.21), fold_depth=0.06, seed=61, n=20, rows=6,
                 hem=lambda th: 0.35 * (0.5 + 0.5 * math.sin(th * 7)) - 0.15)
    outfit.belt(rag, B, 'leather_dark', 1.0, 0.16, 0.1, buckle='rust')
    sh = Part('shield')
    gear.round_shield(sh, B, radius=0.3, rim='rust', boss='rust', band='wood')
    sw = Part('sword')
    outfit.sword(sw, B, side=-1, blade=0.72, blade_mat='rust', guard='rust', pommel='rust')
    people.make(col, arm, L, parts + [(h, 2, 0), (rag, 1, 0.006), (sh, 1, 0), (sw, 0, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- hero

@unit(21, 'hero')
def hero():
    """A hero: plate over mail, a gold crown on his helm, a long cloak and a
    heater shield in the side's colour, a broad sword."""
    B = human.Body(2.0, 1.08)
    kit = Kit('sword', shield='large')
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'mail', 'mail', boots('mail', 'steel', 4), hands='steel', feet='steel', sleeve=0.02, leg_sleeve=0.016)
    h = head(B)
    gear.sallet(h, B)
    gear.crown(h, B)
    m = Part('mail')
    outfit.coif(m, B)
    outfit.skirt(m, B, 'mail', 1.0, 0.56, (0.105, 0.17), (0.15, 0.21), fold_depth=0.02, seed=3)
    pl = Part('plate')
    outfit.cuirass(pl, B, 'steel', grow=0.035)
    for side in (1, -1):
        outfit.pauldron(pl, B, side, 'steel')
        sf = 'L' if side > 0 else 'R'
        pl.ellipsoid(B.P(0.05, 0.1 * side, 0.53), (0.035 * B.s, 0.05 * B.s, 0.06 * B.s), 'steel', B.w('shin.' + sf, 0.5, 'thigh.' + sf, 0.5), seg=12, rings=8)
    outfit.belt(pl, B, 'gold', 1.02, 0.172, 0.118, height=0.06)
    cl = Part('cloak')
    gear.cape(cl, B, 'team', z_bottom=0.15, width=1.1)
    sh = Part('shield')
    outfit.heater_shield(sh, B, height=0.8, width=0.58, charge='gold')
    sw = Part('sword')
    outfit.sword(sw, B, side=-1, blade=0.9, width=0.032, guard='gold', pommel='gold', guard_len=0.12)
    people.make(col, arm, L, parts + [(h, 2, 0), (m, 2, 0.005), (pl, 2, 0), (cl, 2, 0.008), (sh, 1, 0), (sw, 0, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- journeyman

@unit(23, 'journeyman')
def journeyman():
    """A journeyman healer: a brown robe, a hooded mantle in the side's colour,
    a satchel of herbs, a sickle."""
    B = human.Body(1.8, 0.96)
    kit = Kit('sickle', robe=True)
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'robe_brown', 'robe_brown', 'hose', feet='leather', grip=(False, True))
    h = head(B)
    human.hair_shell(h, B, 'hair_brown')
    gear.hood(h, B, 'team', point=0.16)
    rb = Part('robe')
    robe(rb, B, 'robe_brown', trim=None, cuff='robe_brown', seed=13)
    outfit.belt(rb, B, 'rope', 1.03, 0.17, 0.12, height=0.03)
    bag = Part('bag')
    bag.ellipsoid(B.P(0.02, 0.2, 0.9), (B.R(0.07), B.R(0.04), 0.09 * B.s), 'leather', B.w('hips'), seg=10, rings=6)
    bag.chain([B.P(0.0, 0.2, 0.97), B.P(0.1, 0.1, 1.25), B.P(0.07, -0.12, 1.45)], [B.R(0.01)] * 3, 'leather_dark', [B.w('hips'), B.w('spine'), B.w('chest')], n=6)
    sk = Part('sickle')
    gear.sickle(sk, B, side=-1)
    people.make(col, arm, L, parts + [(h, 2, 0), (rb, 2, 0.006), (bag, 1, 0), (sk, 1, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- warrior

@unit(24, 'warrior')
def warrior():
    """A warrior: mail, a nasal helm with a plume in the side's colour, a
    short surcoat and a heater shield in the side's colour, a sword."""
    B = human.Body(1.9, 1.05)
    kit = Kit('sword', shield='large')
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'mail', 'mail', boots('mail', 'leather', 5), hands='leather', sleeve=0.018, leg_sleeve=0.012)
    h = head(B)
    outfit.nasal_helm(h, B)
    gear.plume(h, B, 'team', back=0.3)
    m = Part('mail')
    outfit.coif(m, B)
    coat = Part('surcoat')
    human.torso(coat, B, 'team', grow=0.024, z_from=0.93, z_to=1.49)
    outfit.skirt(coat, B, 'team', 1.0, 0.62, (0.125, 0.19), (0.16, 0.23), fold_depth=0.05, seed=71, n=24, rows=6)
    outfit.belt(coat, B, 'leather_dark', 1.02, 0.172, 0.118, buckle='gold')
    sh = Part('shield')
    outfit.heater_shield(sh, B, height=0.74, width=0.56, charge='wood_pale')
    sw = Part('sword')
    outfit.sword(sw, B, side=-1, blade=0.8)
    people.make(col, arm, L, parts + [(h, 2, 0), (m, 2, 0.005), (coat, 2, 0.007), (sh, 1, 0), (sw, 0, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- berserker

@unit(25, 'berserker')
def berserker():
    """A berserker: bare-chested, wild red hair and beard, a kilt in the side's
    colour, fur boots, a claymore swung in both hands."""
    B = human.Body(1.9, 1.12)
    kit = Kit('greatsword', strike='overhead', speed=1.3)
    col, arm, L = people.setup(B, kit)
    parts = bare_body(B, 'skin_woad', boots('skin_woad', 'fur', 5), 'fur')
    h = head(B, 'skin_woad')
    gear.wild_hair(h, B, 'hair_red', length=0.22)
    gear.beard(h, B, 'hair_red', length=0.2, width=1.05, forked=True)
    k = Part('kilt')
    gear.kilt(k, B, 'team', z_bottom=0.55, depth=0.06)
    outfit.belt(k, B, 'leather', 1.02, 0.17, 0.115, height=0.06, buckle='bronze')
    sw = Part('sword')
    gear.greatsword(sw, B, side=-1, blade=1.15)
    people.make(col, arm, L, parts + [(h, 2, 0), (k, 2, 0.008), (sw, 0, 0)])
    return people.animate(arm, B, kit)
