"""People of every kind on one rig and one set of motions: a figure is built
from human.py's body with outfit.py and gear.py's things on it, then keyed
by animate() for the way it fights (figures.cpp's Strike: a swing, an
overhead blow, a thrust, a bow shot, a throw, a spell).

Two-handed weapons keep the left hand on the haft with an IK constraint to a
bone on the weapon; things that leave the hand (an arrow on the string, a
thrown charge, a spell's light) hang on bones scaled to nothing when gone."""

import math

import bpy
from mathutils import Vector

from . import geo, human, mats, rig
from .human import X, Y, Z, Stance, keyed
from .rig import Bone

# How far along the haft (metres on a 1.8 m man, + toward the head) the left hand holds it.
SECOND_HAND = {'pike': 0.5, 'spear': 0.4, 'trident': 0.4, 'greatsword': -0.13, 'club': -0.1, 'axe2': -0.15}


class Kit:
    def __init__(self, weapon='sword', shield=None, strike='swing', two_hands=None, robe=False, heavy=1.0, speed=1.0):
        self.weapon, self.shield, self.strike = weapon, shield, strike
        self.two_hands = SECOND_HAND.get(weapon) if two_hands is None else two_hands
        self.robe, self.heavy, self.speed = robe, heavy, speed


def extra_bones(B, kit):
    """Bones beyond the body's: the second hand's target and pole, the arrow, the orb, the thrown thing."""
    out = []
    g = human.grip_point(B, -1)
    if kit.two_hands is not None:
        d = kit.two_hands * B.s
        out.append(Bone(B.n('grip2'), g + Vector((d, 0, 0)), g + Vector((d + 0.05 * B.s, 0, 0)), B.n('hand.R'), (0, 0, 1)))
        p = B.P(-0.15, 0.6, 0.95)
        out.append(Bone(B.n('pole.L'), p, p + Vector((0, 0, 0.05)), B.n('chest'), (1, 0, 0)))
    if kit.strike == 'shoot':
        gl = human.grip_point(B, 1)
        out.append(Bone(B.n('arrow'), gl, gl + Vector((0, 0, 0.1)), B.n('hand.L'), (1, 0, 0)))
    if kit.strike == 'throw':
        out.append(Bone(B.n('held'), g, g + Vector((0, 0, -0.1)), B.n('hand.R'), (1, 0, 0)))
    if kit.strike == 'cast':
        gl = human.grip_point(B, 1)
        out.append(Bone(B.n('orb'), gl, gl + Vector((0, 0, -0.05)), B.n('hand.L'), (1, 0, 0)))
    return out


def setup(B, kit, more=()):
    """The rig: body, the kit's bones and any more (wings, tail...). Returns (collection, armature, materials)."""
    col = bpy.context.scene.collection
    L = mats.library()
    bones = human.skeleton(B) + extra_bones(B, kit) + list(more)
    arm = rig.build('rig', bones, col)
    if kit.two_hands is not None:
        rig.ik(arm, B.n('forearm.L'), B.n('grip2'), B.n('pole.L'), chain=2, pole_angle=-90)
    return col, arm, L


def make(col, arm, L, parts):
    for part, subdiv, solid in parts:
        part.make(col, arm, subdiv=subdiv, materials=L, solidify=solid)


def _cycle(t):
    return math.sin(2 * math.pi * t), math.cos(2 * math.pi * t)


# ------------------------------------------------------------------ stances

def stance(kit):
    """The figure at rest, ready: by weapon and shield."""
    s = Stance()
    s.thigh_out = [4, 4]
    s.knee = [8, 8]
    w = kit.weapon
    # The right (weapon) arm, index 1.
    if w in ('pike', 'spear', 'trident'):
        s.shoulder[1], s.shoulder_out[1], s.elbow[1], s.wrist[1] = 12, 5, 80, 0
    elif w in ('greatsword', 'club', 'axe2'):
        s.shoulder[1], s.shoulder_out[1], s.elbow[1], s.wrist[1] = 35, 18, 75, 10
        s.twist = 15
    elif w == 'staff':
        s.shoulder[1], s.shoulder_out[1], s.elbow[1], s.wrist[1] = 18, 14, 78, 0
    elif w == 'bow':
        s.shoulder[1], s.shoulder_out[1], s.elbow[1] = 8, 10, 20
    elif w == 'satchel':
        s.shoulder[1], s.shoulder_out[1], s.elbow[1], s.wrist[1] = 15, 12, 45, -10
    else:  # one-handed: sword, axe, sickle
        s.shoulder[1], s.shoulder_out[1], s.elbow[1], s.wrist[1] = 22, 14, 75, -25
    # The left arm, index 0.
    if kit.shield == 'large':
        s.shoulder[0], s.shoulder_out[0], s.arm_twist[0], s.elbow[0] = 32, 24, 78, 95
        s.lean, s.twist = 4, s.twist - 6
        s.thigh = [10, -6]
    elif kit.shield == 'round':
        s.shoulder[0], s.shoulder_out[0], s.arm_twist[0], s.elbow[0] = 28, 22, 70, 88
        s.thigh = [8, -5]
    elif w == 'bow':
        s.shoulder[0], s.shoulder_out[0], s.elbow[0], s.wrist[0] = 18, 12, 35, 0
    elif kit.two_hands is None:
        s.shoulder[0], s.shoulder_out[0], s.elbow[0] = 8, 12, 25
    if kit.robe:
        s.thigh_out = [2, 2]
    return s


def attack_keys(kit, base):
    """Keys of the blow (time, Stance), 0 to 1."""
    k = kit.strike
    up, hit, follow = base.copy(), base.copy(), base.copy()
    if k == 'swing':
        up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1], up.arm_twist[1] = 150, 45, 110, 35, -20
        up.twist, up.lean = base.twist - 22, 0
        hit.shoulder[1], hit.shoulder_out[1], hit.elbow[1], hit.wrist[1] = 75, 18, 5, -20
        hit.twist, hit.lean, hit.thigh, hit.knee = base.twist + 26, 16, [22, -12], [22, 8]
        follow = hit.copy()
        follow.shoulder[1], follow.shoulder_out[1], follow.elbow[1], follow.wrist[1], follow.arm_twist[1] = 35, -10, 10, -30, 40
        follow.twist, follow.lean = base.twist + 32, 18
        return [(0, base), (0.25, up), (0.55, hit), (0.8, follow), (1.0, human.mix(follow, base, 0.6))]
    if k == 'overhead':
        up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1] = 165, 15, 70, 40
        up.twist, up.lean, up.nod = 0, -8, -10
        hit.shoulder[1], hit.shoulder_out[1], hit.elbow[1], hit.wrist[1] = 65, 10, 8, -15
        hit.twist, hit.lean, hit.thigh, hit.knee = 5, 24, [26, -14], [30, 18]
        follow = hit.copy()
        follow.shoulder[1], follow.elbow[1], follow.wrist[1] = 30, 8, -35
        follow.lean = 28
        if kit.two_hands is None:  # a one-handed overhead (a shield on the other arm)
            pass
        return [(0, base), (0.3, up), (0.55, hit), (0.8, follow), (1.0, human.mix(follow, base, 0.5))]
    if k == 'thrust':
        up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1], up.arm_twist[1] = 10, 22, 45, -50, 0
        up.twist, up.lean, up.thigh = -18, -4, [10, -10]
        hit.shoulder[1], hit.shoulder_out[1], hit.elbow[1], hit.wrist[1] = 55, 10, 8, -45
        hit.twist, hit.lean, hit.thigh, hit.knee = 12, 18, [30, -14], [30, 10]
        follow = human.mix(hit, up, 0.3)
        return [(0, base), (0.3, up), (0.55, hit), (0.8, follow), (1.0, human.mix(follow, base, 0.6))]
    if k == 'shoot':
        up.shoulder[0], up.shoulder_out[0], up.elbow[0], up.wrist[0], up.arm_twist[0] = 82, 5, 3, 5, 0
        up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1] = 82, 15, 80, -20
        up.twist, up.lean, up.head_turn, up.thigh = -40, 2, 30, [12, -12]
        hit = up.copy()
        hit.shoulder[1], hit.shoulder_out[1], hit.elbow[1], hit.wrist[1] = 92, 40, 135, -35
        follow = hit.copy()
        follow.shoulder[1], follow.shoulder_out[1], follow.elbow[1], follow.wrist[1] = 70, 60, 60, 10
        return [(0, base), (0.25, up), (0.5, hit), (0.68, hit), (0.8, follow), (1.0, human.mix(follow, base, 0.5))]
    if k == 'throw':
        up.shoulder[1], up.shoulder_out[1], up.elbow[1], up.wrist[1] = -45, 25, 55, 10
        up.shoulder[0], up.shoulder_out[0], up.elbow[0] = 60, 15, 20
        up.twist, up.lean, up.thigh, up.knee = -35, -10, [18, -12], [14, 20]
        hit.shoulder[1], hit.shoulder_out[1], hit.elbow[1], hit.wrist[1] = 155, 15, 25, 20
        hit.shoulder[0], hit.elbow[0] = 10, 20
        hit.twist, hit.lean, hit.thigh, hit.knee = 28, 16, [28, -16], [26, 10]
        follow = hit.copy()
        follow.shoulder[1], follow.elbow[1], follow.wrist[1] = 80, 10, -20
        follow.lean = 22
        return [(0, base), (0.3, up), (0.55, hit), (0.8, follow), (1.0, human.mix(follow, base, 0.5))]
    if k == 'cast':
        up.shoulder[0], up.shoulder_out[0], up.elbow[0], up.wrist[0] = 160, 25, 25, 20
        up.shoulder[1], up.elbow[1] = 30, 70
        up.lean, up.nod, up.twist = -10, -15, -10
        hit.shoulder[0], hit.shoulder_out[0], hit.elbow[0], hit.wrist[0] = 88, 8, 2, 50
        hit.shoulder[1], hit.elbow[1] = 45, 60
        hit.lean, hit.twist, hit.thigh, hit.knee = 14, 20, [20, -10], [20, 8]
        follow = human.mix(hit, base, 0.3)
        return [(0, base), (0.35, up), (0.6, hit), (0.8, follow), (1.0, base)]
    raise ValueError(k)


def flinch_keys(base):
    k = base.copy()
    k.lean -= 12
    k.nod -= 14
    k.knee = [x + 12 for x in base.knee]
    k.shoulder_out = [x + 10 for x in base.shoulder_out]
    k.root = Vector((0, 0, -0.03))
    return [(0, k), (1, human.mix(base, k, 0.45))]


def die_keys(base, arm_fling=60.0):
    """Hit, knees go, falling backward, down."""
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
    return [(0, base), (0.2, k1), (0.45, k2), (0.7, k3), (1.0, k4)]


def walk_legs(st, t, swing=24.0, knee=48.0, bob=0.018):
    a, c = _cycle(t)
    st.thigh = [st.thigh[0] * 0.3 + swing * a, st.thigh[1] * 0.3 - swing * a]
    st.knee = [6 + knee * max(0.0, math.cos(2 * math.pi * t + 0.6)) ** 1.5, 6 + knee * max(0.0, -math.cos(2 * math.pi * t + 0.6)) ** 1.5]
    st.ankle = [8 * max(0.0, -a), 8 * max(0.0, a)]
    st.root = Vector((0, 0, -bob * (1 - abs(math.cos(2 * math.pi * t))) + bob * 0.3))
    st.hip_twist = 5 * a
    st.twist = st.twist - 8 * a
    return st


def stance_at(kit, base, anim, i, n, walk_frames=None):
    """The Stance for frame i of n of anim (people on foot)."""
    t = i / n
    if anim == 'idle':
        st = base.copy()
        b = math.sin(2 * math.pi * t)
        st.lean += 1.2 * b
        st.shoulder = [st.shoulder[0] + 2 * b, st.shoulder[1] + 2.5 * b]
        st.nod += 2 * b
        st.root = Vector((0, 0, -0.004 * (1 + b)))
        return st
    if anim == 'walk':
        st = base.copy()
        tt = (i % walk_frames) / walk_frames if walk_frames else t
        sw = 24.0 / kit.heavy ** 0.3 * (0.7 if kit.robe else 1.0)
        walk_legs(st, tt, swing=sw)
        a, _ = _cycle(tt)
        swing_l = 4 if kit.shield or kit.two_hands is not None or kit.weapon == 'bow' else 18
        st.shoulder = [st.shoulder[0] - swing_l * a, st.shoulder[1] + (4 if kit.two_hands is not None else 8) * a]
        st.lean = base.lean + 5
        return st
    if anim == 'attack':
        return keyed(attack_keys(kit, base), i / (n - 1))
    if anim == 'flinch':
        return keyed(flinch_keys(base), i / (n - 1))
    return keyed(die_keys(base), i / (n - 1))


def animate(arm, B, kit, base=None, extra=None, walk_frames=None):
    """Keys the 26 frames. extra(p, anim, i, n, st): more of the pose (wings, tail...)."""
    base = base or stance(kit)

    def pose_of(anim, i, n):
        st = stance_at(kit, base, anim, i, n, walk_frames)
        p = human.to_pose(st, B)
        u = i / (n - 1)
        # Things that leave the hand.
        if kit.strike == 'shoot':
            p.scale(B.n('arrow'), 1.0 if anim == 'attack' and 0.1 < u < 0.75 else 0.0)
        if kit.strike == 'throw':
            p.scale(B.n('held'), 0.0 if anim == 'attack' and 0.6 < u < 0.95 else (0.0 if anim == 'die' and u > 0.4 else 1.0))
        if kit.strike == 'cast':
            glow = {0: 0.0, 1: 0.6, 2: 1.0, 3: 1.4, 4: 0.0, 5: 0.0}[i] if anim == 'attack' else 0.0
            p.scale(B.n('orb'), glow)
        if extra:
            extra(p, anim, i, n, st)
        return p

    rig.key_all(arm, pose_of)
    return arm
