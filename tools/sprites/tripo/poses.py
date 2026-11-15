"""The knight's key poses, in his own space: x forward, y his left, z up, metres, origin between his feet.

feet:   side -> (x, y, toe yaw in degrees; + turns the toes to his left)
hips:   where the hips bone's head is (rest: 0, 0, 1.04)
hips_rot, chest, head: keyer.lean(pitch forward +, roll left +, turn left +)
sword_hand / shield_hand: the wrist; *_elbow: a point the elbow bends toward
blade:  which way the blade points; edge: which way its cutting edge leads
shield_face: which way the shield's face looks
"""
from mathutils import Vector
from keyer import lean, yaw  # noqa: E402 (unit.py puts this folder on the path)

RELAXED = dict(
    feet={'Left': (0.0, 0.15, 8), 'Right': (0.0, -0.15, -8)},
    hips=(0.0, 0.0, 1.025), hips_rot=lean(), chest=lean(pitch=3), head=lean(),
    sword_hand=(0.06, -0.31, 0.88), sword_elbow=(-0.25, -0.30, 1.10),
    blade=(0.35, -0.05, -0.94), edge=(1, 0, 0),
    shield_hand=(0.25, 0.27, 1.10), shield_elbow=(-0.15, 0.30, 0.85),
    shield_face=(0, 1, 0),
)

GUARD = dict(
    feet={'Left': (0.22, 0.16, 15), 'Right': (-0.20, -0.16, -40)},
    hips=(0.0, 0.0, 0.95), hips_rot=lean(pitch=5, turn=-20), chest=lean(pitch=10, turn=-10), head=lean(),
    sword_hand=(0.22, -0.30, 1.05), sword_elbow=(-0.10, -0.45, 0.90),
    blade=(0.75, 0.05, 0.65), edge=(0.3, 0, -1),
    shield_hand=(0.25, 0.03, 1.27), shield_elbow=(0.25, 0.45, 1.10),
    shield_face=(1, 0.15, 0),
)

# The overhead cut: wind up over the right shoulder, cut down through the target, follow through to his left hip.
WINDUP = dict(GUARD,
    hips=(-0.05, 0.0, 0.97), hips_rot=lean(turn=-35), chest=lean(pitch=-5, turn=-45), head=lean(turn=-5),
    sword_hand=(-0.10, -0.12, 1.88), sword_elbow=(-0.20, -0.45, 1.70),
    blade=(-0.7, 0.2, 0.3), edge=(0.5, 0, 1),
    shield_hand=(0.40, 0.05, 1.22), shield_elbow=(0.30, 0.45, 1.10),
)

STRIKE = dict(GUARD,
    hips=(0.08, 0.0, 0.93), hips_rot=lean(pitch=10), chest=lean(pitch=20, turn=15),
    sword_hand=(0.62, -0.08, 1.25), sword_elbow=(0.40, -0.40, 1.00),
    blade=(0.7, 0.2, -0.3), edge=(0.2, 0, -1),
    shield_hand=(0.22, 0.10, 1.15), shield_elbow=(0.20, 0.45, 1.00), shield_face=(1, 0.4, 0),
)

FOLLOW = dict(GUARD,
    hips=(0.10, 0.0, 0.91), hips_rot=lean(pitch=12, turn=10), chest=lean(pitch=25, turn=25),
    sword_hand=(0.45, 0.10, 0.85), sword_elbow=(0.40, -0.30, 1.10),
    blade=(0.3, 0.5, -0.8), edge=(-0.6, 0.4, -0.2),
    shield_hand=(0.25, 0.20, 1.15), shield_elbow=(0.20, 0.50, 1.00), shield_face=(1, 0.5, 0),
)

# Breathing and looking: small offsets for the idles.
GUARD_BREATHE = dict(GUARD, hips=(0.0, 0.0, 0.94), chest=lean(pitch=12, turn=-10),
                     sword_hand=(0.22, -0.30, 1.03), shield_hand=(0.25, 0.03, 1.25))
RELAXED_LOOK_L = dict(RELAXED, chest=lean(pitch=2, turn=6), head=lean(turn=35))
RELAXED_LOOK_R = dict(RELAXED, hips=(0.0, 0.0, 1.02), chest=lean(pitch=3, turn=-6), head=lean(turn=-35))

# The thrust: chamber the sword at the right hip, then a passing lunge, right foot forward, arm straight.
THRUST_CHAMBER = dict(GUARD,
    hips=(-0.04, 0.0, 0.94), hips_rot=lean(pitch=3, turn=-30), chest=lean(pitch=6, turn=-30),
    sword_hand=(0.0, -0.28, 1.10), sword_elbow=(-0.35, -0.40, 1.05), blade=(1, 0.05, 0.05), edge=(0, 0, -1),
)
THRUST_LUNGE = dict(GUARD,
    feet={'Left': (0.22, 0.16, 15), 'Right': (0.40, -0.14, -10)},
    hips=(0.22, -0.02, 0.88), hips_rot=lean(pitch=8, turn=10), chest=lean(pitch=12, turn=20),
    sword_hand=(0.72, -0.10, 1.28), sword_elbow=(0.50, -0.40, 1.10), blade=(1, 0.05, 0.05), edge=(0, 0, -1),
    shield_hand=(0.45, 0.10, 1.22), shield_elbow=(0.40, 0.50, 1.10), shield_face=(1, 0.3, 0),
)

# The sweep: drawn back to the right, flat through the front at waist height, out to his left; he turns his body
# with it but his feet stay put. The shield lifts out of the blade's way.
SWEEP_DRAW = dict(GUARD,
    hips=(-0.03, 0.0, 0.93), hips_rot=lean(turn=-40), chest=lean(pitch=5, turn=-55),
    sword_hand=(-0.20, -0.48, 1.10), sword_elbow=(-0.30, -0.50, 1.30), blade=(-0.6, -0.8, 0.05), edge=(0.8, -0.6, 0),
)
SWEEP_MID = dict(GUARD,
    hips=(0.06, 0.0, 0.91), hips_rot=lean(pitch=5, turn=-5), chest=lean(pitch=10, turn=5),
    sword_hand=(0.46, -0.18, 1.12), sword_elbow=(0.20, -0.50, 1.00), blade=(1, 0.25, 0), edge=(-0.25, 1, 0),
    shield_hand=(0.30, 0.15, 1.30), shield_elbow=(0.25, 0.50, 1.20), shield_face=(1, 0.4, 0),
)
SWEEP_THROUGH = dict(GUARD,
    hips=(0.08, 0.0, 0.91), hips_rot=lean(pitch=5, turn=25), chest=lean(pitch=8, turn=40),
    sword_hand=(0.40, 0.08, 1.05), sword_elbow=(0.30, -0.35, 0.95), blade=(0.3, 1, -0.1), edge=(-1, 0.3, 0),
    shield_hand=(0.20, 0.25, 1.40), shield_elbow=(0.10, 0.55, 1.25), shield_face=(1, 0.6, 0),
)

# Block: the shield snaps up before his face and the blow jolts him back.
BLOCK_UP = dict(GUARD,
    hips=(-0.02, 0.0, 0.94), chest=lean(pitch=2, turn=-18),
    shield_hand=(0.32, 0.0, 1.58), shield_elbow=(0.30, 0.42, 1.38), shield_face=(1, 0.1, 0.15),
)
BLOCK_JOLT = dict(BLOCK_UP, feet={'Left': (0.10, 0.16, 15), 'Right': (-0.20, -0.16, -40)},
    hips=(-0.15, 0.0, 0.92), chest=lean(pitch=-12, turn=-15), head=lean(pitch=-10),
    shield_hand=(0.20, 0.0, 1.55), shield_elbow=(0.18, 0.42, 1.35),
)

# Hit: struck in the chest, he rocks back half a step, arms thrown out.
HIT = dict(GUARD,
    feet={'Left': (0.10, 0.16, 15), 'Right': (-0.20, -0.16, -40)},
    hips=(-0.10, 0.0, 0.96), hips_rot=lean(pitch=-5, turn=-25), chest=lean(pitch=-18, turn=-15),
    head=lean(pitch=-20, turn=10),
    sword_hand=(0.05, -0.50, 1.05), sword_elbow=(-0.20, -0.50, 1.20), blade=(0.6, -0.4, 0.3), edge=(0, 0, -1),
    shield_hand=(0.10, 0.48, 1.12), shield_elbow=(-0.10, 0.50, 1.20), shield_face=(0.6, 1, 0),
)

# Brace: low and wide behind the shield, left shoulder in, sword low behind it.
BRACE = dict(GUARD,
    feet={'Left': (0.30, 0.18, 15), 'Right': (-0.30, -0.18, -45)},
    hips=(-0.02, 0.0, 0.82), hips_rot=lean(pitch=10, turn=-30), chest=lean(pitch=18, turn=-30), head=lean(pitch=-10),
    sword_hand=(0.10, -0.25, 0.92), sword_elbow=(-0.20, -0.45, 0.95), blade=(0.8, 0.1, 0.35), edge=(0, 0, -1),
    shield_hand=(0.42, 0.02, 1.12), shield_elbow=(0.35, 0.45, 1.00), shield_face=(1, 0.1, 0),
)
BRACE_BREATHE = dict(BRACE, hips=(-0.02, 0.0, 0.81), chest=lean(pitch=19, turn=-30))


def turned(p, deg):
    """Pose p turned in place by deg about the vertical (+ to his left): feet, hands, directions and leans go round;
    toe yaws keep counting past 360 so a full turn interpolates the long way round."""
    q = yaw(deg)
    rv = lambda v: tuple(q @ Vector(v))
    out = dict(p)
    out['feet'] = {s: tuple(q @ Vector((f[0], f[1], 0)))[:2] + (f[2] + deg,) + tuple(f[3:]) for s, f in p['feet'].items()}
    out['hips'] = rv(p['hips'])
    for k in ('hips_rot', 'chest', 'head'):
        if k in p:
            out[k] = q @ p[k]
    for k in ('sword_hand', 'sword_elbow', 'blade', 'edge', 'shield_hand', 'shield_elbow', 'shield_face'):
        if k in p:
            out[k] = rv(p[k])
    if 'knee' in p:
        out['knee'] = {s: rv(v) for s, v in p['knee'].items()}
    return out


# --- Walking and running, in place (the feet come from a treadmill gait in clips.py; these are the bodies) ---------
WALK_L = dict(RELAXED, hips=(0.0, 0.0, 0.95), hips_rot=lean(pitch=2, turn=-6), chest=lean(pitch=3, turn=4),
              sword_hand=(0.16, -0.31, 0.90), blade=(0.5, -0.05, -0.86), shield_hand=(0.20, 0.27, 1.10))
WALK_PASS = dict(RELAXED, hips=(0.0, 0.0, 0.985), hips_rot=lean(pitch=2), chest=lean(pitch=3))
WALK_R = dict(RELAXED, hips=(0.0, 0.0, 0.95), hips_rot=lean(pitch=2, turn=6), chest=lean(pitch=3, turn=-4),
              sword_hand=(-0.05, -0.30, 0.89), blade=(0.2, -0.05, -0.98), shield_hand=(0.29, 0.27, 1.11))

ADV_LOW = dict(GUARD, hips=(0.0, 0.0, 0.915))
ADV_HIGH = dict(GUARD, hips=(0.0, 0.0, 0.935), chest=lean(pitch=9, turn=-10))

CHARGE_FEET = {'Left': (0.30, 0.11, 8), 'Right': (-0.35, -0.11, -8, 0.15)}
CHARGE_DOWN_L = dict(GUARD, feet=CHARGE_FEET,
    hips=(0.0, 0.0, 0.86), hips_rot=lean(pitch=12), chest=lean(pitch=20), head=lean(pitch=-5),
    sword_hand=(-0.10, -0.33, 0.95), sword_elbow=(-0.30, -0.40, 1.10), blade=(0.9, 0.0, 0.35), edge=(0, 0, -1),
    shield_hand=(0.42, 0.04, 1.20), shield_elbow=(0.35, 0.45, 1.05), shield_face=(1, 0.15, 0.1))
CHARGE_DOWN_R = dict(CHARGE_DOWN_L, sword_hand=(0.02, -0.33, 0.96))
CHARGE_UP = dict(CHARGE_DOWN_L, hips=(0.0, 0.0, 0.92), sword_hand=(-0.04, -0.33, 0.97))

ROUT_DOWN_L = dict(RELAXED, feet=CHARGE_FEET,
    hips=(0.0, 0.0, 0.87), hips_rot=lean(pitch=15, turn=-8), chest=lean(pitch=25, turn=-15), head=lean(pitch=-10, turn=-70),
    sword_hand=(-0.08, -0.30, 0.90), sword_elbow=(-0.30, -0.30, 1.10), blade=(-0.6, -0.1, -0.6), edge=(0, 0, -1),
    shield_hand=(0.05, 0.40, 1.05), shield_elbow=(-0.20, 0.40, 1.00), shield_face=(0.3, 1, 0))
ROUT_DOWN_R = dict(ROUT_DOWN_L, hips_rot=lean(pitch=15, turn=8), chest=lean(pitch=25, turn=-5),
    sword_hand=(0.12, -0.30, 0.95))
ROUT_UP = dict(ROUT_DOWN_L, hips=(0.0, 0.0, 0.93), chest=lean(pitch=22, turn=-10))

# --- Intimidate: three heavy blows of the sword's flat on the shield, a point at the enemy, a roar ------------------
BANG_UP = dict(GUARD, hips=(0.0, 0.0, 0.99), hips_rot=lean(turn=-20), chest=lean(pitch=-4, turn=-15), head=lean(pitch=-5),
    sword_hand=(0.10, -0.32, 1.55), sword_elbow=(-0.15, -0.50, 1.40), blade=(-0.2, 0.1, 1), edge=(1, 0, 0))
BANG_HIT = dict(GUARD, hips=(0.06, 0.0, 0.93), chest=lean(pitch=14, turn=-5),
    sword_hand=(0.42, -0.02, 1.38), sword_elbow=(0.30, -0.45, 1.20), blade=(0.05, 0.85, 0.5), edge=(0, 0, 1))
POINT = dict(GUARD, hips=(0.04, 0.0, 0.95), chest=lean(pitch=8, turn=10),
    sword_hand=(0.55, -0.15, 1.50), sword_elbow=(0.30, -0.45, 1.40), blade=(1, 0.05, 0.15), edge=(0, 0, -1))
ROAR = dict(GUARD, hips=(-0.02, 0.0, 0.97), chest=lean(pitch=-10), head=lean(pitch=-20),
    sword_hand=(0.25, -0.45, 1.55), sword_elbow=(0.0, -0.55, 1.30), blade=(0.3, -0.2, 1), edge=(1, 0, 0),
    shield_hand=(0.20, 0.42, 1.25), shield_elbow=(0.0, 0.55, 1.10), shield_face=(1, 0.6, 0))

# --- Mock: shield down, arms wide, beckon with the sword hand, tap the chest, laugh -----------------------------------
MOCK_OPEN = dict(GUARD, hips=(0.0, 0.0, 0.98), hips_rot=lean(turn=-10), chest=lean(pitch=-8, turn=-5), head=lean(pitch=-5),
    sword_hand=(0.10, -0.62, 1.20), sword_elbow=(-0.10, -0.50, 1.00), blade=(0.3, -0.6, 0.7), edge=(0, 0, -1),
    shield_hand=(0.20, 0.38, 1.05), shield_elbow=(-0.10, 0.40, 0.95), shield_face=(0.2, 1, 0))
MOCK_IN = dict(MOCK_OPEN, sword_hand=(0.20, -0.25, 1.35), sword_elbow=(0.0, -0.50, 1.15), blade=(0.2, 0, 1), edge=(-1, 0, 0))
MOCK_OUT = dict(MOCK_OPEN, sword_hand=(0.42, -0.30, 1.30), sword_elbow=(0.20, -0.50, 1.10), blade=(0.5, 0, 0.85), edge=(-1, 0, 0))
MOCK_TAP = dict(MOCK_OPEN, chest=lean(pitch=-10, turn=-5),
    sword_hand=(0.22, -0.08, 1.38), sword_elbow=(0.10, -0.50, 1.20), blade=(0.3, -0.5, 0.8), edge=(1, 0, 0))
MOCK_LAUGH = dict(MOCK_OPEN, hips=(-0.05, 0.0, 0.95), chest=lean(pitch=25), head=lean(pitch=15),
    sword_hand=(0.25, -0.25, 0.95), sword_elbow=(0.0, -0.50, 1.10), blade=(0.6, -0.2, -0.75), edge=(1, 0, 0),
    shield_hand=(0.20, 0.36, 0.98))
MOCK_LAUGH2 = dict(MOCK_LAUGH, chest=lean(pitch=18), head=lean(pitch=8), hips=(-0.04, 0.0, 0.96))

# --- Celebrate, salute (from and back to RELAXED) -------------------------------------------------------------------
CEL_UP = dict(RELAXED, chest=lean(pitch=-6), head=lean(pitch=-20),
    sword_hand=(0.06, -0.24, 1.95), sword_elbow=(-0.05, -0.45, 1.75), blade=(0.1, -0.05, 1), edge=(1, 0, 0),
    shield_hand=(0.25, 0.27, 1.15))
CEL_PUMP = dict(CEL_UP, sword_hand=(0.10, -0.30, 1.55), head=lean(pitch=-10))
CEL_READY = dict(RELAXED, chest=lean(pitch=2),
    sword_hand=(0.15, -0.35, 1.60), sword_elbow=(-0.10, -0.50, 1.40), blade=(-0.2, 0, 1), edge=(1, 0, 0),
    shield_hand=(0.30, 0.10, 1.30), shield_elbow=(0.25, 0.45, 1.10), shield_face=(1, 0.3, 0))
CEL_BANG = dict(CEL_READY, chest=lean(pitch=10),
    sword_hand=(0.45, -0.02, 1.40), sword_elbow=(0.30, -0.45, 1.20), blade=(0.05, 0.85, 0.5), edge=(0, 0, 1))

SALUTE = dict(RELAXED, chest=lean(pitch=3), head=lean(pitch=8),
    sword_hand=(0.25, -0.06, 1.52), sword_elbow=(0.05, -0.40, 1.25), blade=(0.05, 0, 1), edge=(1, 0, 0))

# --- Dance: a heavy, clumsy jig; the shield drags him toward his left ------------------------------------------------
DANCE_ARMS = dict(sword_hand=(0.05, -0.62, 1.30), sword_elbow=(-0.10, -0.50, 1.50), blade=(0.3, -0.6, 0.6), edge=(0, 0, -1),
                  shield_hand=(0.05, 0.55, 1.25), shield_elbow=(-0.10, 0.50, 1.45), shield_face=(1, 0, 0.2))
DANCE_HOP_L = dict(RELAXED, **DANCE_ARMS, feet={'Left': (0.0, 0.15, 8), 'Right': (0.08, -0.15, -8, 0.22)},
    hips=(0.0, 0.04, 1.0), hips_rot=lean(roll=6), chest=lean(pitch=3, roll=-4))
DANCE_HOP_R = dict(RELAXED, **DANCE_ARMS, feet={'Left': (0.08, 0.15, 8, 0.22), 'Right': (0.0, -0.15, -8)},
    hips=(0.0, -0.04, 1.0), hips_rot=lean(roll=-6), chest=lean(pitch=3, roll=4))
DANCE_LAND = dict(RELAXED, **dict(DANCE_ARMS, sword_hand=(0.05, -0.55, 1.15), shield_hand=(0.05, 0.50, 1.12)),
    hips=(0.0, 0.0, 0.94), chest=lean(pitch=4))
DANCE_LURCH = dict(DANCE_LAND, feet={'Left': (0.0, 0.22, 8), 'Right': (0.0, -0.10, -8)},
    hips=(0.0, 0.10, 0.95), hips_rot=lean(roll=8), chest=lean(pitch=5, roll=12), shield_hand=(0.05, 0.50, 0.95))
DANCE_BANG = dict(DANCE_LAND, hips=(0.0, 0.0, 0.98), chest=lean(pitch=-4), head=lean(pitch=-15),
    sword_hand=(0.08, -0.12, 1.95), sword_elbow=(0.0, -0.45, 1.80), blade=(0.3, 0.7, 0.6), edge=(0, 0, -1),
    shield_hand=(0.10, 0.12, 1.92), shield_elbow=(0.0, 0.50, 1.80), shield_face=(0.3, 0, 1))
DANCE_STAMP_L = dict(DANCE_LAND, feet={'Left': (0.0, 0.15, 8, 0.12), 'Right': (0.0, -0.15, -8)})
DANCE_STAMP_R = dict(DANCE_LAND, feet={'Left': (0.0, 0.15, 8), 'Right': (0.0, -0.15, -8, 0.12)})
TURN90 = turned(DANCE_STAMP_L, 90)
TURN180 = turned(DANCE_STAMP_R, 180)
TURN270 = turned(DANCE_STAMP_L, 270)
TURN360 = turned(DANCE_LAND, 360)
RELAXED_360 = turned(RELAXED, 360)

# --- Deaths (from GUARD). Lying poses: feet hang off the shins (free), knees bend the way the body lies. ------------
UP, DOWN, FWD = (0, 0, 1), (0, 0, -1), (1, 0, 0)
FLAT = {'Left': UP, 'Right': UP}

# Struck in the chest: staggers back two steps and falls on his back.
DB_STRUCK = dict(GUARD, hips=(-0.08, 0.0, 0.95), hips_rot=lean(pitch=-8), chest=lean(pitch=-25), head=lean(pitch=-30),
    sword_hand=(-0.05, -0.55, 1.25), sword_elbow=(-0.30, -0.50, 1.20), blade=(0.3, -0.5, 0.8), edge=(0, 0, -1),
    shield_hand=(0.05, 0.50, 1.25), shield_elbow=(-0.20, 0.50, 1.10), shield_face=(0.5, 1, 0.3))
DB_STAGGER1 = dict(DB_STRUCK, feet={'Left': (-0.15, 0.18, 25), 'Right': (-0.20, -0.16, -40)},
    hips=(-0.25, 0.0, 0.90), hips_rot=lean(pitch=-12), chest=lean(pitch=-30))
DB_STAGGER2 = dict(DB_STRUCK, feet={'Left': (-0.15, 0.18, 25), 'Right': (-0.50, -0.16, -20)},
    hips=(-0.50, 0.0, 0.78), hips_rot=lean(pitch=-20), chest=lean(pitch=-35), head=lean(pitch=-35),
    sword_hand=(-0.40, -0.55, 1.20), shield_hand=(-0.35, 0.52, 1.20))
DB_FALLING = dict(DB_STAGGER2, hips=(-0.80, 0.0, 0.42), hips_rot=lean(pitch=-55), chest=lean(pitch=-65), head=lean(pitch=-40),
    free=0.3, sword_hand=(-1.0, -0.50, 0.95), sword_elbow=(-1.1, -0.5, 1.3), shield_hand=(-1.0, 0.50, 0.95),
    shield_elbow=(-1.1, 0.5, 1.3), shield_face=(0.2, 0.5, 1))
DB_IMPACT = dict(GUARD, feet={'Left': (-0.10, 0.20, 25), 'Right': (-0.15, -0.22, -25)}, free=1.0, knee=FLAT,
    hips=(-0.95, 0.0, 0.16), hips_rot=lean(pitch=-88), chest=lean(pitch=-90), head=lean(pitch=-85, turn=15),
    sword_hand=(-1.35, -0.68, 0.06), sword_elbow=(-1.4, -0.5, 0.4), blade=(0.3, -0.6, 0), edge=(0, 0, 1),
    shield_hand=(-1.30, 0.66, 0.08), shield_elbow=(-1.4, 0.5, 0.4), shield_face=UP)
DB_SETTLE = dict(DB_IMPACT, hips=(-0.97, 0.0, 0.15), head=lean(pitch=-90, turn=35))

# Struck from behind: thrown forward, a stumbling step, down on his face.
DF_STRUCK = dict(GUARD, hips=(0.08, 0.0, 0.95), hips_rot=lean(pitch=10), chest=lean(pitch=28), head=lean(pitch=-15),
    sword_hand=(-0.15, -0.50, 1.10), sword_elbow=(-0.10, -0.50, 1.40), blade=(-0.3, -0.5, 0.6), edge=(0, 0, -1),
    shield_hand=(-0.10, 0.50, 1.10), shield_elbow=(-0.05, 0.50, 1.40), shield_face=(0.3, 1, 0))
DF_STUMBLE = dict(DF_STRUCK, feet={'Left': (0.22, 0.16, 15), 'Right': (0.45, -0.15, -10)},
    hips=(0.30, 0.0, 0.82), hips_rot=lean(pitch=20), chest=lean(pitch=40), head=lean(pitch=10))
DF_FALLING = dict(DF_STUMBLE, hips=(0.55, 0.0, 0.45), hips_rot=lean(pitch=55), chest=lean(pitch=65), head=lean(pitch=40),
    knee={'Left': (1, 0, -0.5), 'Right': (1, 0, -0.5)}, free=0.3,
    sword_hand=(1.10, -0.35, 0.35), sword_elbow=(0.9, -0.6, 0.5), blade=(1, -0.2, -0.2),
    shield_hand=(1.05, 0.35, 0.35), shield_elbow=(0.9, 0.6, 0.5), shield_face=(1, 0, 0.5))
DF_IMPACT = dict(GUARD, feet={'Left': (-0.10, 0.16, 15), 'Right': (-0.12, -0.18, -15)}, free=1.0,
    knee={'Left': DOWN, 'Right': DOWN},
    hips=(0.80, 0.0, 0.17), hips_rot=lean(pitch=88), chest=lean(pitch=90), head=lean(pitch=88, turn=-20),
    sword_hand=(1.45, -0.55, 0.06), sword_elbow=(1.3, -0.6, 0.4), blade=(0.6, -0.6, 0), edge=(0, 0, 1),
    shield_hand=(1.40, 0.55, 0.12), shield_elbow=(1.3, 0.6, 0.5), shield_face=UP)
DF_SETTLE = dict(DF_IMPACT, hips=(0.82, 0.0, 0.16), head=lean(pitch=90, turn=-35))

# Cut down where he stands: knees buckle, he kneels, slumps onto his right side.
KNEEL_KNEES = {'Left': (1, 0, -0.3), 'Right': (1, 0, -0.3)}
DC_STRUCK = dict(GUARD, hips=(0.0, 0.0, 0.90), chest=lean(pitch=15, turn=-10), head=lean(pitch=20),
    sword_hand=(0.15, -0.32, 0.85), sword_elbow=(-0.10, -0.40, 1.10), blade=(0.4, -0.1, -0.9), edge=(1, 0, 0))
DC_KNEEL = dict(DC_STRUCK, feet={'Left': (-0.38, 0.16, 10), 'Right': (-0.40, -0.16, -10)}, knee=KNEEL_KNEES, free=0.8,
    hips=(-0.05, 0.0, 0.55), hips_rot=lean(pitch=-5), chest=lean(pitch=20), head=lean(pitch=35),
    sword_hand=(0.10, -0.32, 0.30), sword_elbow=(-0.1, -0.4, 0.6), blade=(0.6, -0.2, -0.2),
    shield_hand=(0.15, 0.32, 0.65), shield_elbow=(-0.10, 0.40, 0.60), shield_face=(0.4, 1, 0))
DC_SLUMP = dict(DC_KNEEL, feet={'Left': (-0.30, -0.05, 20, 0.15), 'Right': (-0.32, -0.25, 20)},
    knee={'Left': FWD, 'Right': FWD}, free=1.0,
    hips=(0.0, -0.15, 0.20), hips_rot=lean(pitch=15, roll=-80), chest=lean(pitch=35, roll=-85), head=lean(pitch=30, roll=-80),
    sword_hand=(0.40, -0.40, 0.08), sword_elbow=(0.3, -0.3, 0.4), blade=(0.6, -0.5, 0), edge=(0, 0, 1),
    shield_hand=(0.40, 0.05, 0.08), shield_elbow=(0.2, 0.1, 0.4), shield_face=UP)

# Burning: beats at the flames, twists, staggers, drops to his knees, collapses on his face.
BURN_A = dict(GUARD, feet={'Left': (0.15, 0.20, 30), 'Right': (-0.20, -0.16, -40)},
    hips=(0.0, 0.0, 0.93), chest=lean(pitch=10, turn=30, roll=10), head=lean(pitch=-20, turn=20),
    sword_hand=(0.25, -0.10, 1.50), sword_elbow=(0.0, -0.45, 1.30), blade=(-0.5, -0.3, 0.8), edge=(0, 0, 1),
    shield_hand=(0.20, 0.30, 1.35), shield_elbow=(0.0, 0.50, 1.20), shield_face=(1, 0.5, 0.3))
BURN_B = dict(BURN_A, feet={'Left': (0.15, 0.20, 30), 'Right': (-0.05, -0.22, -20)},
    chest=lean(pitch=8, turn=-30, roll=-10), head=lean(pitch=-15, turn=-25),
    sword_hand=(0.30, -0.35, 1.20), shield_hand=(0.30, 0.05, 1.45), shield_face=(1, 0, 0.3))
BURN_KNEES = dict(DC_KNEEL, chest=lean(pitch=10, turn=15), head=lean(pitch=-10),
    sword_hand=(0.18, -0.15, 1.05), sword_elbow=(0.0, -0.45, 0.85), blade=(-0.3, -0.3, 0.9), edge=(0, 0, 1),
    shield_hand=(0.18, 0.20, 1.05), shield_elbow=(0.0, 0.45, 0.85), shield_face=(1, 0, 0.2))
BURN_DOWN = dict(DF_IMPACT, feet={'Left': (-0.44, 0.16, 15), 'Right': (-0.46, -0.18, -15)},
    hips=(0.45, 0.0, 0.17), sword_hand=(1.05, -0.50, 0.06), sword_elbow=(0.9, -0.6, 0.4),
    shield_hand=(1.00, 0.50, 0.12), shield_elbow=(0.9, 0.6, 0.5))

# Knocked down and up again: thrown on his back, sits up, one knee, stands, steps back into guard.
KD_THROWN = dict(GUARD, feet={'Left': (0.05, 0.18, 15, 0.35), 'Right': (-0.05, -0.18, -15, 0.25)},
    hips=(-0.35, 0.0, 0.95), hips_rot=lean(pitch=-45), chest=lean(pitch=-55), head=lean(pitch=-30),
    sword_hand=(-0.40, -0.55, 1.45), sword_elbow=(-0.6, -0.6, 1.2), blade=(0.5, -0.5, 0.7), edge=(0, 0, -1),
    shield_hand=(-0.35, 0.52, 1.40), shield_elbow=(-0.6, 0.6, 1.2), shield_face=(0.5, 0.3, 1))
KD_LAND = dict(DB_IMPACT, feet={'Left': (0.05, 0.20, 25), 'Right': (0.0, -0.22, -25)},
    hips=(-0.75, 0.0, 0.16), hips_rot=lean(pitch=-85), chest=lean(pitch=-88), head=lean(pitch=-70),
    sword_hand=(-1.10, -0.62, 0.06), sword_elbow=(-1.2, -0.5, 0.4), blade=(0.6, -0.6, 0),
    shield_hand=(-1.05, 0.62, 0.08), shield_elbow=(-1.2, 0.5, 0.4))
KD_SIT = dict(GUARD, feet={'Left': (-0.25, 0.20, 20), 'Right': (-0.20, -0.20, -20)}, knee=FLAT, free=0.5,
    hips=(-0.70, 0.0, 0.16), hips_rot=lean(pitch=-15), chest=lean(pitch=15), head=lean(pitch=5),
    sword_hand=(-0.40, -0.40, 0.45), sword_elbow=(-0.7, -0.5, 0.5), blade=(1, 0, 0.2), edge=(0, 0, -1),
    shield_hand=(-0.85, 0.40, 0.12), shield_elbow=(-0.9, 0.6, 0.5), shield_face=UP)
KD_KNEEL = dict(GUARD, feet={'Left': (-0.10, 0.18, 15), 'Right': (-0.75, -0.16, -10)},
    knee={'Left': (0.6, 0, 0.5), 'Right': (1, 0, -0.5)}, free=0.3,
    hips=(-0.40, 0.0, 0.62), hips_rot=lean(pitch=15), chest=lean(pitch=15),
    sword_hand=(-0.25, -0.35, 0.75), sword_elbow=(-0.5, -0.5, 0.7), blade=(0.8, 0, 0.5), edge=(0, 0, -1),
    shield_hand=(-0.05, 0.10, 0.95), shield_elbow=(-0.10, 0.45, 0.85), shield_face=(1, 0.2, 0))
KD_RISE = dict(GUARD, feet={'Left': (-0.02, 0.17, 15), 'Right': (-0.45, -0.16, -35)}, hips=(-0.25, 0.0, 0.88))
