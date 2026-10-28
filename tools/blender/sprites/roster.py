"""The rest of the roster on foot: each UnitType's figure (figures.cpp's
figureOf and docs/UNITS.md say what each carries), built on people.py.

Every figure has the side's colour somewhere large enough to read at a
sprite's size: a tunic, tabard, cloak, shield face, kilt or plume."""

import math

from mathutils import Vector

from . import gear, geo, human, outfit, people, rig, wings
from .geo import Part, blend, smooth
from .human import X, Y, Z
from .people import Kit
from .units import unit


def boots(mat_leg, mat_boot, from_ring=5):
    return lambda j, s: mat_boot if j >= from_ring else mat_leg


def body(B, torso, arms, legs, feet='leather_dark', hands='skin', skin='skin', sleeve=0.01, leg_sleeve=0.006, grip=(True, True), torso_grow=0.004):
    """The figure's body in its clothes: trunk, neck, arms, legs, hands and feet."""
    b = Part('body')
    human.torso(b, B, torso, grow=torso_grow)
    human.neck(b, B, skin)
    for side in (1, -1):
        human.arm(b, B, side, arms, sleeve=sleeve)
        human.leg(b, B, side, legs, sleeve=leg_sleeve)
    e = Part('ends')
    for k, side in enumerate((1, -1)):
        human.hand(e, B, side, hands, grip=grip[k])
        human.foot(e, B, side, feet)
    return [(b, 2, 0), (e, 2, 0)]


def head(B, skin='skin', ears=True, eye='eye'):
    h = Part('head')
    human.head(h, B, skin, eye_mat=eye, ears=ears)
    return h


# ---------------------------------------------------------------- light infantry

@unit(0, 'light_infantry')
def light_infantry():
    """A levy in a quilted coat and kettle hat, the side's colour on his
    tunic and round shield, a short sword."""
    B = human.Body(1.8, 1.0)
    kit = Kit('sword', shield='round')
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'gambeson', 'gambeson', boots('hose', 'leather', 5), sleeve=0.016)
    h = head(B)
    human.hair_shell(h, B, 'hair_brown')
    gear.kettle_hat(h, B)
    coat = Part('tunic')
    human.torso(coat, B, 'team', grow=0.022, z_from=0.93, z_to=1.49)
    outfit.skirt(coat, B, 'team', 1.0, 0.62, (0.125, 0.185), (0.15, 0.22), fold_depth=0.04, seed=41, n=24, rows=6)
    outfit.belt(coat, B, 'leather', 1.02, 0.17, 0.118, buckle='steel')
    sh = Part('shield')
    gear.round_shield(sh, B, radius=0.3, band='wood_pale')
    sw = Part('sword')
    outfit.sword(sw, B, side=-1, blade=0.62)
    people.make(col, arm, L, parts + [(h, 2, 0), (coat, 2, 0.007), (sh, 1, 0), (sw, 0, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- archers

@unit(2, 'archers')
def archers():
    """An elven archer: slender, long-haired, a hood and tunic in the side's
    colour over a green cloak, a longbow and a quiver of red-fletched arrows."""
    B = human.Body(1.85, 0.9)
    kit = Kit('bow', strike='shoot')
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'cloak_green', 'cloak_green', boots('cloak_green', 'leather', 5), feet='leather', skin='skin_pale', hands='leather')
    h = head(B, 'skin_pale')
    human.hair_shell(h, B, 'hair_gold', long=0.2 * B.s)
    gear.hood(h, B, 'team', point=0.1)
    gear.pointed_ears(h, B, 'skin_pale', length=0.09)
    coat = Part('tunic')
    human.torso(coat, B, 'team', grow=0.018, z_from=0.93, z_to=1.44)
    outfit.skirt(coat, B, 'team', 1.0, 0.6, (0.12, 0.175), (0.15, 0.21), fold_depth=0.04, seed=43, n=24, rows=6)
    outfit.belt(coat, B, 'leather', 1.02, 0.165, 0.115, buckle='gold')
    gear.quiver(coat, B, 'leather')
    cl = Part('cloak')
    gear.cape(cl, B, 'cloak_green', z_bottom=0.45, width=0.95)
    bw = Part('bow')
    gear.bow(bw, B, side=1, length=1.45)
    ar = Part('arrow')
    # On the bow hand: from the string at the cheek to the bow once drawn.
    g = human.grip_point(B, 1)
    tail = g + Vector((0.02 * B.s, -0.16 * B.s, 0.7 * B.s))
    tip = g + Vector((0.0, -0.035 * B.s, -0.06 * B.s))
    gear.arrow(ar, B, B.n('arrow'), tail, (tip - tail).normalized(), length=(tip - tail).length / B.s)
    people.make(col, arm, L, parts + [(h, 2, 0), (coat, 2, 0.006), (cl, 2, 0.006), (bw, 1, 0), (ar, 0, 0)])
    return people.animate(arm, B, kit)


# ---------------------------------------------------------------- pikemen

@unit(3, 'pikemen')
def pikemen():
    """A pikeman: mail, a sallet with a plume in the side's colour, a tabard,
    a long pike."""
    B = human.Body(1.8, 1.02)
    kit = Kit('pike', strike='thrust')
    col, arm, L = people.setup(B, kit)
    parts = body(B, 'mail', 'mail', boots('hose', 'leather', 5), hands='leather', sleeve=0.016)
    h = head(B)
    gear.sallet(h, B)
    gear.plume(h, B, 'team', back=0.25, rise=0.1)
    m = Part('mail')
    outfit.coif(m, B)
    tb = Part('tabard')
    gear.tabard(tb, B, 'team', z_bottom=0.6)
    outfit.belt(tb, B, 'leather_dark', 1.02, 0.172, 0.118, buckle='steel')
    pk = Part('pike')
    gear.spear(pk, B, side=-1, length=3.6, butt=1.1, head=0.26, width=0.032)
    people.make(col, arm, L, parts + [(h, 2, 0), (m, 2, 0.005), (tb, 2, 0.007), (pk, 1, 0)])
    return people.animate(arm, B, kit)
