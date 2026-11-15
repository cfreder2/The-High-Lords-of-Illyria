"""How a unit fights, as key poses and timed clips: each style returns the poses (written for the knight: a man with
his hips at 1.04 m, poses.py's space) and the clips made of them. unit.py fits them to the model (fit.py) and bakes
them with the keyer. A new way of fighting is a new function here, named in the unit's config as "style".

A clip: keys [(seconds, pose, ease)], how it is drawn (kind: loop, cycle, still, death), the times its frames are
taken at, and what the game needs (stride from the gait, release, loop)."""
import poses as KP
from keyer import lean

FIST = {'Right': 65, 'Left': 30}


def _deaths(P, home='HOME', frames=8, flinch=None):
    """The flinch and the four deaths every man-shaped unit shares: the knight's, begun from the unit's own idle."""
    for name in ('HIT', 'DB_STRUCK', 'DB_STAGGER1', 'DB_STAGGER2', 'DB_FALLING', 'DB_IMPACT', 'DB_SETTLE',
                 'DF_STRUCK', 'DF_STUMBLE', 'DF_FALLING', 'DF_IMPACT', 'DF_SETTLE',
                 'DC_STRUCK', 'DC_KNEEL', 'DC_SLUMP', 'BURN_A', 'BURN_B', 'BURN_KNEES', 'BURN_DOWN'):
        p = dict(getattr(KP, name), fist={'Right': 30, 'Left': 30})
        if name in ('HIT', 'DB_STRUCK', 'DF_STRUCK', 'DC_STRUCK'):
            p['feet'] = dict(P[home]['feet'])     # struck where he stands
        P[name] = p
    return {
        'flinch': dict(keys=[(0.0, home, 'linear'), (0.10, 'HIT', 'out'), (0.18, 'HIT', 'linear'), (0.60, home, 'ease')],
                       kind='still', times=flinch or [0.0, 0.0833, 0.2083, 0.375], seconds=0.60),
        'die_back': dict(keys=[(0.0, home, 'linear'), (0.10, 'DB_STRUCK', 'out'), (0.35, 'DB_STAGGER1', 'ease'),
                               (0.55, 'DB_STAGGER2', 'ease'), (0.80, 'DB_FALLING', 'in'), (0.95, 'DB_IMPACT', 'in'),
                               (1.15, 'DB_SETTLE', 'out'), (1.5, 'DB_SETTLE', 'linear')], kind='death', frames=frames),
        'die_front': dict(keys=[(0.0, home, 'linear'), (0.10, 'DF_STRUCK', 'out'), (0.38, 'DF_STUMBLE', 'ease'),
                                (0.65, 'DF_FALLING', 'in'), (0.82, 'DF_IMPACT', 'in'), (1.0, 'DF_SETTLE', 'out'),
                                (1.4, 'DF_SETTLE', 'linear')], kind='death', frames=frames),
        'die_crumple': dict(keys=[(0.0, home, 'linear'), (0.12, 'DC_STRUCK', 'out'), (0.55, 'DC_KNEEL', 'in'),
                                  (0.75, 'DC_KNEEL', 'linear'), (1.15, 'DC_SLUMP', 'in'), (1.5, 'DC_SLUMP', 'linear')],
                            kind='death', frames=frames),
        'die_burn': dict(keys=[(0.0, home, 'linear'), (0.2, 'BURN_A', 'out'), (0.45, 'BURN_B', 'ease'), (0.7, 'BURN_A', 'ease'),
                               (0.95, 'BURN_B', 'ease'), (1.3, 'BURN_KNEES', 'in'), (1.45, 'BURN_KNEES', 'linear'),
                               (1.8, 'BURN_DOWN', 'in'), (2.2, 'BURN_DOWN', 'linear')], kind='death', frames=12, drop_at=4),
    }


def thrower(cfg):
    """A man who carries something in his right hand and throws it overhand (the dwarven grenadier's satchel)."""
    HOME = dict(KP.RELAXED, sword_hand=(0.05, -0.30, 0.86), sword_elbow=(-0.25, -0.32, 1.10),
                shield_hand=(0.05, 0.30, 0.86), shield_elbow=(-0.25, 0.32, 1.10), fist=FIST)
    P = {'HOME': HOME,
         'BREATHE': dict(HOME, hips=(0.0, 0.0, 1.012), chest=lean(pitch=6)),
         'LOOK_L': dict(HOME, hips=(0.0, 0.0, 1.02), chest=lean(pitch=2, turn=8), head=lean(turn=38, pitch=-4)),
         'LOOK_R': dict(HOME, hips=(0.0, 0.0, 1.015), chest=lean(pitch=3, turn=-8), head=lean(turn=-38, pitch=-4))}
    # Walking: the arms swing against the legs, the satchel hand less than the free one.
    W = dict(HOME, hips_rot=lean(pitch=2), chest=lean(pitch=4))
    P['WALK_PASS'] = dict(W, hips=(0.0, 0.0, 0.985))
    P['WALK_R'] = dict(W, hips=(0.0, 0.0, 0.95), hips_rot=lean(pitch=2, turn=6), chest=lean(pitch=4, turn=-5),
                       sword_hand=(-0.10, -0.31, 0.88), shield_hand=(0.26, 0.28, 0.92))
    P['WALK_L'] = dict(W, hips=(0.0, 0.0, 0.95), hips_rot=lean(pitch=2, turn=-6), chest=lean(pitch=4, turn=5),
                       sword_hand=(0.20, -0.29, 0.90), shield_hand=(-0.14, 0.31, 0.90))
    # Running: leaning in, elbows bent, fists pumping.
    R = dict(HOME, hips=(0.08, 0.0, 0.93), hips_rot=lean(pitch=10), chest=lean(pitch=18), head=lean(pitch=-8),
             sword_hand=(0.12, -0.27, 1.08), sword_elbow=(-0.25, -0.40, 1.15),
             shield_hand=(0.12, 0.27, 1.08), shield_elbow=(-0.25, 0.40, 1.15), fist={'Right': 65, 'Left': 60})
    P['RUN_UP'] = R
    P['RUN_R'] = dict(R, hips=(0.08, 0.0, 0.87), chest=lean(pitch=18, turn=-10),
                      sword_hand=(-0.12, -0.30, 1.02), sword_elbow=(-0.40, -0.40, 1.30),
                      shield_hand=(0.38, 0.20, 1.22), shield_elbow=(0.0, 0.42, 1.00))
    P['RUN_L'] = dict(R, hips=(0.08, 0.0, 0.87), chest=lean(pitch=18, turn=10),
                      sword_hand=(0.38, -0.20, 1.22), sword_elbow=(0.0, -0.42, 1.00),
                      shield_hand=(-0.12, 0.30, 1.02), shield_elbow=(-0.40, 0.40, 1.30))
    # The throw, right-handed and overhand: left foot forward, the satchel swung back and high, over, and through.
    STEP = {'Left': (0.24, 0.15, 10), 'Right': (-0.20, -0.16, -35)}
    T = dict(HOME, feet=STEP)
    P['THROW_BACK'] = dict(T, hips=(-0.06, 0.0, 0.99), hips_rot=lean(turn=-25), chest=lean(pitch=-10, turn=-45),
                           head=lean(pitch=-8),
                           sword_hand=(-0.38, -0.40, 1.62), sword_elbow=(-0.30, -0.60, 1.25),
                           shield_hand=(0.45, 0.16, 1.38), shield_elbow=(0.15, 0.45, 1.15))
    P['THROW_OVER'] = dict(T, hips=(0.05, 0.0, 0.98), hips_rot=lean(pitch=6, turn=5), chest=lean(pitch=10, turn=20),
                           head=lean(pitch=-8),
                           sword_hand=(0.38, -0.18, 1.92), sword_elbow=(0.10, -0.45, 1.60),
                           shield_hand=(0.15, 0.30, 1.05), shield_elbow=(-0.15, 0.45, 1.10))
    P['THROW_THROUGH'] = dict(T, hips=(0.10, 0.0, 0.93), hips_rot=lean(pitch=12, turn=20), chest=lean(pitch=28, turn=38),
                              head=lean(pitch=-12),
                              sword_hand=(0.48, 0.08, 0.95), sword_elbow=(0.30, -0.30, 1.15),
                              shield_hand=(-0.15, 0.32, 0.98), shield_elbow=(-0.35, 0.42, 1.15),
                              fist={'Right': 15, 'Left': 30})
    # A cheer: both fists up, pumped twice.
    C = dict(HOME, chest=lean(pitch=-6), head=lean(pitch=-18), fist={'Right': 65, 'Left': 65})
    P['CEL_UP'] = dict(C, hips=(0.0, 0.0, 1.03),
                       sword_hand=(0.08, -0.30, 1.98), sword_elbow=(0.0, -0.55, 1.70),
                       shield_hand=(0.08, 0.30, 1.98), shield_elbow=(0.0, 0.55, 1.70))
    P['CEL_PUMP'] = dict(C, hips=(0.0, 0.0, 0.97), head=lean(pitch=-8),
                         sword_hand=(0.14, -0.34, 1.60), sword_elbow=(0.0, -0.60, 1.30),
                         shield_hand=(0.14, 0.34, 1.60), shield_elbow=(0.0, 0.60, 1.30))
    step = cfg.get('step', 1.0)
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.7, 'BREATHE', 'ease'), (1.3, 'LOOK_L', 'ease'), (1.8, 'LOOK_L', 'linear'),
                           (2.5, 'LOOK_R', 'ease'), (2.9, 'LOOK_R', 'linear'), (3.4, 'HOME', 'ease')], kind='loop', frames=8),
        'walk': dict(keys=[(0.0, 'WALK_PASS', 'linear'), (0.2, 'WALK_R', 'linear'), (0.4, 'WALK_PASS', 'linear'),
                           (0.6, 'WALK_L', 'linear'), (0.8, 'WALK_PASS', 'linear')], kind='cycle', frames=8,
                     gait=dict(period=0.8, swing=0.40, lift=0.09,
                               feet={'Left': (0.36 * step, -0.29 * step, 0.15, 6, 0.6), 'Right': (0.36 * step, -0.29 * step, -0.15, -6, 0.1)})),
        'run': dict(keys=[(0.0, 'RUN_UP', 'linear'), (0.175, 'RUN_R', 'linear'), (0.3, 'RUN_UP', 'linear'),
                          (0.425, 'RUN_L', 'linear'), (0.5, 'RUN_UP', 'linear')], kind='cycle', frames=8,
                    gait=dict(period=0.5, swing=0.68, lift=0.20,
                              feet={'Left': (0.47 * step, -0.39 * step, 0.12, 6, 0.0), 'Right': (0.47 * step, -0.39 * step, -0.12, -6, 0.5)})),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.30, 'THROW_BACK', 'ease'), (0.38, 'THROW_BACK', 'linear'),
                             (0.48, 'THROW_OVER', 'in'), (0.58, 'THROW_THROUGH', 'out'), (0.70, 'THROW_THROUGH', 'linear'),
                             (1.05, 'HOME', 'ease')], kind='still',
                       times=[0.0, 0.125, 0.25, 0.3333, 0.4167, 0.5, 0.5833, 0.7083, 0.875], release=0.5, item_gone=0.5),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.35, 'CEL_UP', 'out'), (0.65, 'CEL_PUMP', 'ease'), (0.95, 'CEL_UP', 'out'),
                                (1.25, 'CEL_PUMP', 'ease'), (1.55, 'CEL_UP', 'out'), (1.9, 'CEL_UP', 'linear'), (2.4, 'HOME', 'ease')],
                          kind='loop', frames=12),
    }
    clips.update(_deaths(P))
    order = ['idle', 'walk', 'run', 'attack', 'flinch', 'die_back', 'die_front', 'die_crumple', 'die_burn', 'celebrate']
    return P, {k: clips[k] for k in order}


def dwarf_thrower(cfg):
    """The dwarven grenadier, keyed for a dwarf: a sturdy wide stance, a short heavy stride that rocks him over each
    planted foot with his shoulders rolling against his hips, and an overarm throw with his whole trunk in it."""
    WIDE = {'Left': (0.0, 0.19, 14), 'Right': (0.0, -0.19, -14)}
    HOME = dict(KP.RELAXED, feet=WIDE, hips=(0.0, 0.0, 1.0), chest=lean(pitch=4), head=lean(pitch=-2),
                sword_hand=(0.10, -0.34, 0.92), sword_elbow=(-0.25, -0.50, 1.10),
                shield_hand=(0.12, 0.34, 0.95), shield_elbow=(-0.25, 0.50, 1.10), fist={'Right': 65, 'Left': 45})
    P = {'HOME': HOME,
         # Breathing: the chest fills (up and back, the shoulders lifting), then empties and settles.
         'IN': dict(HOME, hips=(0.0, 0.0, 1.005), chest=lean(pitch=1), head=lean(pitch=-4), Left_lift=5, Right_lift=5,
                    sword_hand=(0.10, -0.36, 0.94), shield_hand=(0.12, 0.36, 0.97)),
         'OUT': dict(HOME, hips=(0.0, 0.0, 0.985), chest=lean(pitch=7), head=lean(pitch=0)),
         # His weight onto his left leg, a look to the left; then to the right, a hitch of the satchel.
         'SHIFT_L': dict(HOME, hips=(0.0, 0.05, 0.99), hips_rot=lean(roll=4), chest=lean(pitch=4, roll=-3, turn=10),
                         head=lean(turn=38, pitch=-3)),
         'SHIFT_R': dict(HOME, hips=(0.0, -0.05, 0.99), hips_rot=lean(roll=-4), chest=lean(pitch=5, roll=3, turn=-10),
                         head=lean(turn=-35, pitch=-3), Right_lift=6,
                         sword_hand=(0.12, -0.36, 0.98), sword_elbow=(-0.25, -0.55, 1.15))}
    # The walk. Down: a foot has just landed, he sinks onto it, that hip forward and the opposite shoulder with it.
    # Up: he rocks over the planted foot, his trunk tipping that way, the other foot swinging through.
    W = dict(HOME, hips_rot=lean(pitch=3), chest=lean(pitch=6), head=lean(pitch=-3))
    P['DOWN_L'] = dict(W, hips=(0.02, 0.02, 0.93), hips_rot=lean(pitch=3, turn=-10, roll=-2), chest=lean(pitch=8, turn=9, roll=2),
                       head=lean(pitch=-3), Right_shrug=10, Left_shrug=-8,
                       sword_hand=(0.26, -0.30, 0.98), sword_elbow=(-0.20, -0.50, 1.05),
                       shield_hand=(-0.16, 0.36, 0.94), shield_elbow=(-0.45, 0.50, 1.20))
    P['UP_L'] = dict(W, hips=(0.0, 0.06, 1.0), hips_rot=lean(pitch=3, roll=5), chest=lean(pitch=5, roll=7), head=lean(pitch=-3, roll=-3),
                     sword_hand=(0.08, -0.35, 0.92), shield_hand=(0.06, 0.37, 0.93))
    P['DOWN_R'] = dict(W, hips=(0.02, -0.02, 0.93), hips_rot=lean(pitch=3, turn=10, roll=2), chest=lean(pitch=8, turn=-9, roll=-2),
                       head=lean(pitch=-3), Left_shrug=10, Right_shrug=-8,
                       sword_hand=(-0.14, -0.36, 0.93), sword_elbow=(-0.45, -0.50, 1.20),
                       shield_hand=(0.28, 0.30, 1.0), shield_elbow=(-0.20, 0.50, 1.05))
    P['UP_R'] = dict(W, hips=(0.0, -0.06, 1.0), hips_rot=lean(pitch=3, roll=-5), chest=lean(pitch=5, roll=-7), head=lean(pitch=-3, roll=3),
                     sword_hand=(0.06, -0.37, 0.92), shield_hand=(0.08, 0.35, 0.93))
    # The run: a barrelling trot, head down, the free fist pumping, the satchel held in to his side.
    R = dict(HOME, hips_rot=lean(pitch=12), chest=lean(pitch=22), head=lean(pitch=-14), fist={'Right': 65, 'Left': 65})
    P['RDOWN_L'] = dict(R, hips=(0.10, 0.02, 0.86), hips_rot=lean(pitch=12, turn=-12), chest=lean(pitch=24, turn=12),
                        Right_shrug=12, Left_shrug=-10,
                        sword_hand=(0.30, -0.28, 1.12), sword_elbow=(-0.15, -0.50, 1.00),
                        shield_hand=(-0.22, 0.34, 1.05), shield_elbow=(-0.50, 0.45, 1.35))
    P['RUP_L'] = dict(R, hips=(0.10, 0.04, 0.97), hips_rot=lean(pitch=12, roll=3), chest=lean(pitch=20, roll=4),
                      sword_hand=(0.10, -0.32, 1.08), sword_elbow=(-0.30, -0.50, 1.15),
                      shield_hand=(0.10, 0.32, 1.10), shield_elbow=(-0.30, 0.50, 1.15))
    P['RDOWN_R'] = dict(R, hips=(0.10, -0.02, 0.86), hips_rot=lean(pitch=12, turn=12), chest=lean(pitch=24, turn=-12),
                        Left_shrug=12, Right_shrug=-10,
                        sword_hand=(-0.18, -0.34, 1.02), sword_elbow=(-0.50, -0.45, 1.30),
                        shield_hand=(0.34, 0.24, 1.22), shield_elbow=(-0.10, 0.50, 1.00))
    P['RUP_R'] = dict(R, hips=(0.10, -0.04, 0.97), hips_rot=lean(pitch=12, roll=-3), chest=lean(pitch=20, roll=-4),
                      sword_hand=(0.10, -0.32, 1.08), sword_elbow=(-0.30, -0.50, 1.15),
                      shield_hand=(0.10, 0.32, 1.10), shield_elbow=(-0.30, 0.50, 1.15))
    # The throw, overarm. He sinks and swings the satchel back and low (the free hand out at the mark), rears up with
    # it behind his head, steps onto his left foot as his hips and chest come round and the elbow leads the hand over,
    # lets go with the arm long and high, and follows through across his body, his right heel coming off the ground.
    STEP0 = {'Left': (0.10, 0.19, 12), 'Right': (-0.12, -0.19, -30)}
    STEP1 = {'Left': (0.30, 0.17, 6), 'Right': (-0.16, -0.19, -35)}
    STEP2 = {'Left': (0.30, 0.17, 6), 'Right': (-0.05, -0.19, -20, 0.06)}
    T = dict(HOME, fist={'Right': 65, 'Left': 20})
    P['T_SINK'] = dict(T, feet=STEP0, hips=(-0.06, -0.03, 0.90), hips_rot=lean(pitch=4, turn=-25), chest=lean(pitch=12, turn=-45),
                       head=lean(pitch=-6, turn=5), Right_shrug=-12,
                       sword_hand=(-0.40, -0.42, 0.95), sword_elbow=(-0.45, -0.60, 1.25),
                       shield_hand=(0.42, 0.22, 1.25), shield_elbow=(0.10, 0.50, 1.10))
    P['T_REAR'] = dict(T, feet=STEP0, hips=(-0.10, -0.04, 0.99), hips_rot=lean(pitch=-4, turn=-35), chest=lean(pitch=-14, turn=-60),
                       head=lean(pitch=-8, turn=5), Right_shrug=-15, Right_lift=10,
                       sword_hand=(-0.42, -0.30, 1.78), sword_elbow=(-0.30, -0.62, 1.50),
                       shield_hand=(0.48, 0.18, 1.45), shield_elbow=(0.15, 0.48, 1.20))
    P['T_LEAD'] = dict(T, feet=STEP1, hips=(0.08, 0.02, 0.94), hips_rot=lean(pitch=4, turn=5), chest=lean(pitch=2, turn=-15),
                       head=lean(pitch=-6), Right_lift=14,
                       sword_hand=(-0.10, -0.28, 1.95), sword_elbow=(0.18, -0.50, 1.62),
                       shield_hand=(0.30, 0.34, 1.12), shield_elbow=(-0.10, 0.52, 1.15))
    P['T_LOOSE'] = dict(T, feet=STEP1, hips=(0.16, 0.04, 0.93), hips_rot=lean(pitch=10, turn=22), chest=lean(pitch=18, turn=30),
                        head=lean(pitch=-14), Right_shrug=14, Right_lift=10, fist={'Right': 5, 'Left': 30},
                        sword_hand=(0.62, -0.10, 1.88), sword_elbow=(0.30, -0.40, 1.55),
                        shield_hand=(0.0, 0.36, 0.98), shield_elbow=(-0.30, 0.50, 1.15))
    P['T_THROUGH'] = dict(T, feet=STEP2, hips=(0.20, 0.05, 0.88), hips_rot=lean(pitch=16, turn=32), chest=lean(pitch=34, turn=48),
                          head=lean(pitch=-18), Right_shrug=16, fist={'Right': 10, 'Left': 30},
                          sword_hand=(0.50, 0.16, 0.86), sword_elbow=(0.36, -0.30, 1.12),
                          shield_hand=(-0.22, 0.36, 1.02), shield_elbow=(-0.45, 0.48, 1.25))
    P['T_BACK'] = dict(T, feet=STEP0, hips=(0.04, 0.0, 0.95), hips_rot=lean(pitch=6, turn=8), chest=lean(pitch=12, turn=10),
                       sword_hand=(0.22, -0.26, 0.95), sword_elbow=(-0.10, -0.50, 1.10), fist={'Right': 25, 'Left': 30})
    # The cheer: the satchel shaken overhead, the free fist punched up with it, a stamp either side.
    C = dict(HOME, chest=lean(pitch=-6), head=lean(pitch=-18), fist={'Right': 65, 'Left': 65})
    P['C_UP'] = dict(C, hips=(0.0, 0.0, 1.03), Left_lift=12, Right_lift=12,
                     sword_hand=(0.10, -0.32, 2.0), sword_elbow=(0.0, -0.60, 1.70),
                     shield_hand=(0.10, 0.32, 2.0), shield_elbow=(0.0, 0.60, 1.70))
    P['C_DOWN_L'] = dict(C, feet={'Left': (0.0, 0.19, 14), 'Right': (0.0, -0.19, -14, 0.10)}, hips=(0.0, 0.05, 0.93),
                         hips_rot=lean(roll=5), chest=lean(pitch=2, roll=6), head=lean(pitch=-8),
                         sword_hand=(0.16, -0.38, 1.55), sword_elbow=(0.0, -0.65, 1.25),
                         shield_hand=(0.16, 0.36, 1.50), shield_elbow=(0.0, 0.65, 1.25))
    P['C_DOWN_R'] = dict(C, feet={'Left': (0.0, 0.19, 14, 0.10), 'Right': (0.0, -0.19, -14)}, hips=(0.0, -0.05, 0.93),
                         hips_rot=lean(roll=-5), chest=lean(pitch=2, roll=-6), head=lean(pitch=-8),
                         sword_hand=(0.16, -0.36, 1.50), sword_elbow=(0.0, -0.65, 1.25),
                         shield_hand=(0.16, 0.38, 1.55), shield_elbow=(0.0, 0.65, 1.25))
    step = cfg.get('step', 1.0)
    wT, rT = 0.84, 0.52
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.5, 'IN', 'ease'), (1.0, 'OUT', 'ease'), (1.5, 'SHIFT_L', 'ease'), (2.0, 'SHIFT_L', 'linear'),
                           (2.4, 'IN', 'ease'), (2.9, 'SHIFT_R', 'ease'), (3.4, 'SHIFT_R', 'linear'), (4.0, 'HOME', 'ease')],
                     kind='loop', frames=12),
        # The left foot lands at 0, the right at half the period (a foot lands where its swing ends in the gait).
        'walk': dict(keys=[(0.0, 'DOWN_L', 'linear'), (wT * 0.28, 'UP_L', 'ease'), (wT * 0.5, 'DOWN_R', 'ease'),
                           (wT * 0.78, 'UP_R', 'ease'), (wT, 'DOWN_L', 'ease')], kind='cycle', frames=12,
                     gait=dict(period=wT, swing=0.38, lift=0.08,
                               feet={'Left': (0.33 * step, -0.27 * step, 0.19, 12, 0.62), 'Right': (0.33 * step, -0.27 * step, -0.19, -12, 0.12)})),
        'run': dict(keys=[(0.0, 'RDOWN_L', 'linear'), (rT * 0.28, 'RUP_L', 'ease'), (rT * 0.5, 'RDOWN_R', 'ease'),
                          (rT * 0.78, 'RUP_R', 'ease'), (rT, 'RDOWN_L', 'ease')], kind='cycle', frames=10,
                    gait=dict(period=rT, swing=0.62, lift=0.18,
                              feet={'Left': (0.45 * step, -0.38 * step, 0.15, 8, 0.38), 'Right': (0.45 * step, -0.38 * step, -0.15, -8, 0.88)})),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.25, 'T_SINK', 'ease'), (0.50, 'T_REAR', 'ease'), (0.56, 'T_REAR', 'linear'),
                             (0.66, 'T_LEAD', 'in'), (0.75, 'T_LOOSE', 'linear'), (0.87, 'T_THROUGH', 'out'), (1.0, 'T_THROUGH', 'linear'),
                             (1.25, 'T_BACK', 'ease'), (1.45, 'HOME', 'ease')], kind='still',
                       times=[0.0, 0.0833, 0.1667, 0.25, 0.375, 0.5, 0.5833, 0.6667, 0.7083, 0.75, 0.7917, 0.875, 1.0, 1.125, 1.25, 1.375],
                       release=0.75, item_gone=0.75),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.3, 'C_UP', 'out'), (0.6, 'C_DOWN_L', 'ease'), (0.9, 'C_UP', 'out'),
                                (1.2, 'C_DOWN_R', 'ease'), (1.5, 'C_UP', 'out'), (1.8, 'C_DOWN_L', 'ease'), (2.1, 'C_UP', 'out'),
                                (2.4, 'C_UP', 'linear'), (2.9, 'HOME', 'ease')], kind='loop', frames=16),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    order = ['idle', 'walk', 'run', 'attack', 'flinch', 'die_back', 'die_front', 'die_crumple', 'die_burn', 'celebrate']
    return P, {k: clips[k] for k in order}


def _gait(period, swing, lift, front, back, y, toe, left_at, step=1.0):
    return dict(period=period, swing=swing, lift=lift,
                feet={'Left': (front * step, back * step, y, toe, left_at), 'Right': (front * step, back * step, -y, -toe, (left_at + 0.5) % 1.0)})


def _cycle(T, a, b, c, d):
    """A stride's four keys: a foot down, over it, the other down, over that."""
    return [(0.0, a, 'linear'), (T * 0.28, b, 'ease'), (T * 0.5, c, 'ease'), (T * 0.78, d, 'ease'), (T, a, 'ease')]


ORDER = ['idle', 'walk', 'run', 'attack', 'flinch', 'die_back', 'die_front', 'die_crumple', 'die_burn', 'celebrate']
GRIP = {'Right': 70, 'Left': 30}


def hero(cfg):
    """The champion: sword and heater shield, the knight's own poses (he is the knight's build). He stands tall, his
    shield at his side and his point low; walks with a measured stride; charges behind his shield; cuts overhead
    through to his left hip; and salutes with his sword for a cheer."""
    P = {k: dict(getattr(KP, k), fist=GRIP) for k in (
        'RELAXED', 'RELAXED_LOOK_L', 'RELAXED_LOOK_R', 'GUARD', 'WINDUP', 'STRIKE', 'FOLLOW', 'WALK_L', 'WALK_PASS', 'WALK_R',
        'CHARGE_DOWN_L', 'CHARGE_DOWN_R', 'CHARGE_UP', 'CEL_UP', 'CEL_PUMP', 'SALUTE')}
    P['HOME'] = P['RELAXED']
    P['BREATHE'] = dict(P['HOME'], hips=(0.0, 0.0, 1.015), chest=lean(pitch=0), Left_lift=4, Right_lift=4)
    wT, rT = 1.04, 0.62
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.8, 'BREATHE', 'ease'), (1.6, 'RELAXED_LOOK_L', 'ease'), (2.2, 'RELAXED_LOOK_L', 'linear'),
                           (3.0, 'RELAXED_LOOK_R', 'ease'), (3.5, 'RELAXED_LOOK_R', 'linear'), (4.2, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'WALK_L', 'WALK_PASS', 'WALK_R', 'WALK_PASS'), kind='cycle', frames=12,
                     gait=_gait(wT, 0.40, 0.09, 0.36, -0.29, 0.15, 6, 0.62)),
        'run': dict(keys=_cycle(rT, 'CHARGE_DOWN_L', 'CHARGE_UP', 'CHARGE_DOWN_R', 'CHARGE_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.64, 0.20, 0.50, -0.42, 0.12, 6, 0.38)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.16, 'GUARD', 'ease'), (0.34, 'WINDUP', 'ease'), (0.40, 'WINDUP', 'linear'),
                             (0.50, 'STRIKE', 'in'), (0.60, 'FOLLOW', 'out'), (0.72, 'FOLLOW', 'linear'), (0.92, 'GUARD', 'ease'), (1.15, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.0833, 0.1667, 0.25, 0.3333, 0.4167, 0.4583, 0.5, 0.5417, 0.5833, 0.6667, 0.7917, 0.9167, 1.0417]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.4, 'SALUTE', 'ease'), (0.8, 'SALUTE', 'linear'), (1.1, 'CEL_UP', 'out'), (1.4, 'CEL_PUMP', 'ease'),
                                (1.7, 'CEL_UP', 'out'), (2.0, 'CEL_PUMP', 'ease'), (2.3, 'CEL_UP', 'out'), (2.6, 'CEL_UP', 'linear'), (3.1, 'HOME', 'ease')],
                          kind='loop', frames=16),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    return P, {k: clips[k] for k in ORDER}


def spearman(cfg):
    """Light infantry: a spear held at mid shaft and a round shield. Light on his feet: a quick springy step with
    the spear sloped at his shoulder, a run with it levelled, and an underarm thrust from behind the shield, his
    back foot driving him onto the front one."""
    UPRIGHT = dict(sword_hand=(0.16, -0.31, 1.08), sword_elbow=(-0.20, -0.42, 0.90), blade=(0.04, -0.03, 1), edge=(1, 0, 0))
    HOME = dict(KP.RELAXED, fist=GRIP, **UPRIGHT)
    SLOPE = dict(sword_hand=(0.14, -0.30, 1.10), sword_elbow=(-0.20, -0.42, 0.90), blade=(-0.22, -0.04, 0.97), edge=(1, 0, 0))
    P = {'HOME': HOME,
         'BREATHE': dict(HOME, hips=(0.0, 0.0, 1.012), chest=lean(pitch=1), Left_lift=4, Right_lift=4),
         'LOOK_L': dict(HOME, chest=lean(pitch=2, turn=8), head=lean(turn=40), hips=(0.0, 0.02, 1.02)),
         'LEAN': dict(HOME, hips=(0.02, -0.04, 1.01), hips_rot=lean(roll=-4), chest=lean(pitch=5, roll=3, turn=-8), head=lean(turn=-30),
                      sword_hand=(0.20, -0.34, 1.16), blade=(0.10, -0.06, 1))}
    # The walk: up on the ball of the planted foot at each passing, the spear sloped back, the shield swinging a little.
    W = dict(KP.RELAXED, fist=GRIP, **SLOPE)
    P['W_DOWN_L'] = dict(W, hips=(0.01, 0.01, 0.955), hips_rot=lean(pitch=3, turn=-7), chest=lean(pitch=5, turn=6),
                         shield_hand=(0.18, 0.28, 1.08), sword_hand=(0.20, -0.30, 1.10))
    P['W_UP_L'] = dict(W, hips=(0.0, 0.03, 1.0), hips_rot=lean(pitch=2, roll=2), chest=lean(pitch=4, roll=2))
    P['W_DOWN_R'] = dict(W, hips=(0.01, -0.01, 0.955), hips_rot=lean(pitch=3, turn=7), chest=lean(pitch=5, turn=-6),
                         shield_hand=(0.32, 0.26, 1.12), sword_hand=(0.08, -0.31, 1.09))
    P['W_UP_R'] = dict(W, hips=(0.0, -0.03, 1.0), hips_rot=lean(pitch=2, roll=-2), chest=lean(pitch=4, roll=-2))
    # The run: the spear levelled at his hip, the shield forward, long low strides.
    R = dict(KP.GUARD, fist=GRIP, feet=KP.RELAXED['feet'], sword_hand=(0.02, -0.32, 1.02), sword_elbow=(-0.35, -0.42, 1.10),
             blade=(1, 0.0, 0.10), edge=(0, 0, -1), shield_hand=(0.38, 0.06, 1.22), shield_elbow=(0.30, 0.45, 1.05), shield_face=(1, 0.2, 0.05))
    P['R_DOWN_L'] = dict(R, hips=(0.06, 0.0, 0.87), hips_rot=lean(pitch=12, turn=-8), chest=lean(pitch=20, turn=6), head=lean(pitch=-6),
                         sword_hand=(0.10, -0.32, 1.02))
    P['R_UP'] = dict(R, hips=(0.06, 0.0, 0.95), hips_rot=lean(pitch=12), chest=lean(pitch=18), head=lean(pitch=-5))
    P['R_DOWN_R'] = dict(R, hips=(0.06, 0.0, 0.87), hips_rot=lean(pitch=12, turn=8), chest=lean(pitch=20, turn=-6), head=lean(pitch=-6),
                         sword_hand=(-0.06, -0.32, 1.02))
    # The thrust: on guard behind the shield, the spear drawn back level at the hip, then the lunge: the back leg
    # straightens, the front knee bends, the arm goes long, and the shield comes round out of its way.
    P['ON_GUARD'] = dict(KP.GUARD, fist=GRIP, sword_hand=(0.10, -0.30, 1.08), sword_elbow=(-0.25, -0.45, 1.05), blade=(1, 0.04, 0.04), edge=(0, 0, -1))
    P['DRAW'] = dict(KP.THRUST_CHAMBER, fist=GRIP, sword_hand=(-0.22, -0.30, 1.10), sword_elbow=(-0.55, -0.42, 1.15), blade=(1, 0.03, 0.03),
                     hips=(-0.08, 0.0, 0.93), hips_rot=lean(pitch=3, turn=-34), chest=lean(pitch=6, turn=-38))
    P['LUNGE'] = dict(KP.THRUST_LUNGE, fist=GRIP, feet={'Left': (0.46, 0.16, 8), 'Right': (-0.34, -0.16, -45)},
                      hips=(0.16, 0.0, 0.84), hips_rot=lean(pitch=10, turn=12), chest=lean(pitch=16, turn=24), head=lean(pitch=-8),
                      sword_hand=(0.80, -0.12, 1.20), sword_elbow=(0.50, -0.42, 1.05), blade=(1, 0.04, 0.02),
                      shield_hand=(0.30, 0.22, 1.18), shield_elbow=(0.20, 0.52, 1.05), shield_face=(0.8, 0.6, 0))
    P['C_UP'] = dict(HOME, hips=(0.0, 0.0, 1.03), chest=lean(pitch=-6), head=lean(pitch=-18),
                     sword_hand=(0.12, -0.30, 1.95), sword_elbow=(0.0, -0.55, 1.70), blade=(0.05, 0, 1),
                     shield_hand=(0.20, 0.40, 1.50), shield_elbow=(0.0, 0.60, 1.30), shield_face=(0.6, 0.8, 0.2))
    P['C_DOWN'] = dict(P['C_UP'], hips=(0.0, 0.0, 0.97), head=lean(pitch=-8), sword_hand=(0.16, -0.32, 1.62), sword_elbow=(0.0, -0.60, 1.35),
                       shield_hand=(0.22, 0.38, 1.30))
    wT, rT = 0.92, 0.56
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.7, 'BREATHE', 'ease'), (1.4, 'LOOK_L', 'ease'), (2.0, 'LOOK_L', 'linear'),
                           (2.8, 'LEAN', 'ease'), (3.5, 'LEAN', 'linear'), (4.2, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'W_DOWN_L', 'W_UP_L', 'W_DOWN_R', 'W_UP_R'), kind='cycle', frames=12,
                     gait=_gait(wT, 0.42, 0.10, 0.38, -0.30, 0.14, 5, 0.58)),
        'run': dict(keys=_cycle(rT, 'R_DOWN_L', 'R_UP', 'R_DOWN_R', 'R_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.66, 0.22, 0.55, -0.45, 0.11, 4, 0.36)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.18, 'ON_GUARD', 'ease'), (0.36, 'DRAW', 'ease'), (0.42, 'DRAW', 'linear'),
                             (0.52, 'LUNGE', 'in'), (0.64, 'LUNGE', 'linear'), (0.84, 'ON_GUARD', 'ease'), (1.1, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.0833, 0.1667, 0.25, 0.3333, 0.4167, 0.4583, 0.5, 0.5417, 0.625, 0.7083, 0.8333, 0.9583]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.35, 'C_UP', 'out'), (0.65, 'C_DOWN', 'ease'), (0.95, 'C_UP', 'out'), (1.25, 'C_DOWN', 'ease'),
                                (1.55, 'C_UP', 'out'), (1.9, 'C_UP', 'linear'), (2.5, 'HOME', 'ease')], kind='loop', frames=14),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    return P, {k: clips[k] for k in ORDER}


def berserker(cfg):
    """A claymore in both hands: he stands with it across his body, walks with it on his shoulder, runs with it
    trailing, and cleaves from over his right shoulder down through to his left foot with his whole back."""
    WIDE = {'Left': (0.14, 0.20, 18), 'Right': (-0.12, -0.20, -30)}
    TWO = dict(two_hands=0.17, fist={'Right': 70, 'Left': 70}, shield_elbow=(0.05, 0.50, 0.95), shield_hand=(0.2, 0.1, 0.9))
    HOME = dict(KP.RELAXED, feet=WIDE, hips=(0.0, 0.0, 0.99), hips_rot=lean(turn=-12), chest=lean(pitch=6, turn=-8), head=lean(pitch=-2),
                sword_hand=(0.26, -0.08, 0.98), sword_elbow=(-0.05, -0.50, 1.00), blade=(0.30, 0.45, 0.84), edge=(1, 0, -0.3), **TWO)
    P = {'HOME': HOME,
         'HEAVE': dict(HOME, hips=(0.0, 0.0, 1.0), chest=lean(pitch=2, turn=-8), Left_lift=6, Right_lift=6, sword_hand=(0.26, -0.08, 1.01)),
         'SAG': dict(HOME, hips=(0.0, 0.0, 0.97), chest=lean(pitch=10, turn=-8)),
         'GLARE_L': dict(HOME, hips=(0.0, 0.03, 0.98), chest=lean(pitch=6, turn=6), head=lean(turn=40, pitch=4),
                         sword_hand=(0.24, 0.0, 1.0), blade=(0.25, 0.55, 0.80)),
         'GLARE_R': dict(HOME, hips=(0.0, -0.03, 0.98), chest=lean(pitch=8, turn=-22), head=lean(turn=-38, pitch=4),
                         sword_hand=(0.28, -0.16, 0.96), blade=(0.40, 0.30, 0.86))}
    # The walk: the claymore on his right shoulder in one hand, the free arm swinging wide, a rolling swagger.
    SH = dict(KP.RELAXED, fist={'Right': 70, 'Left': 35}, sword_hand=(0.16, -0.24, 1.36), sword_elbow=(0.05, -0.55, 1.10),
              blade=(-0.62, -0.04, 0.78), edge=(0, 0, -1), shield_elbow=(-0.25, 0.42, 1.10))
    P['W_DOWN_L'] = dict(SH, hips=(0.02, 0.02, 0.94), hips_rot=lean(pitch=3, turn=-10), chest=lean(pitch=6, turn=10, roll=2), Left_shrug=-8,
                         shield_hand=(-0.20, 0.36, 0.92))
    P['W_UP_L'] = dict(SH, hips=(0.0, 0.05, 1.0), hips_rot=lean(pitch=3, roll=4), chest=lean(pitch=4, roll=5), shield_hand=(0.05, 0.36, 0.88))
    P['W_DOWN_R'] = dict(SH, hips=(0.02, -0.02, 0.94), hips_rot=lean(pitch=3, turn=10), chest=lean(pitch=6, turn=-10, roll=-2), Left_shrug=10,
                         shield_hand=(0.30, 0.30, 0.98))
    P['W_UP_R'] = dict(SH, hips=(0.0, -0.05, 1.0), hips_rot=lean(pitch=3, roll=-4), chest=lean(pitch=4, roll=-5), shield_hand=(0.05, 0.36, 0.88))
    # The run: head down, the blade trailing low behind his right hip, the free fist driving.
    RN = dict(KP.RELAXED, fist={'Right': 70, 'Left': 65}, hips_rot=lean(pitch=14), chest=lean(pitch=26), head=lean(pitch=-16),
              sword_hand=(-0.10, -0.34, 0.92), sword_elbow=(-0.45, -0.45, 1.15), blade=(-0.85, -0.15, -0.30), edge=(0, 0, -1))
    P['R_DOWN_L'] = dict(RN, hips=(0.10, 0.0, 0.85), chest=lean(pitch=28, turn=10), shield_hand=(-0.20, 0.32, 1.02), shield_elbow=(-0.50, 0.42, 1.30))
    P['R_UP'] = dict(RN, hips=(0.10, 0.0, 0.95), shield_hand=(0.12, 0.30, 1.08), shield_elbow=(-0.30, 0.48, 1.12))
    P['R_DOWN_R'] = dict(RN, hips=(0.10, 0.0, 0.85), chest=lean(pitch=28, turn=-10), shield_hand=(0.40, 0.22, 1.25), shield_elbow=(-0.05, 0.48, 1.00))
    # The cleave.
    LUNGE = {'Left': (0.34, 0.18, 10), 'Right': (-0.28, -0.20, -40)}
    P['HEFT'] = dict(HOME, feet=LUNGE, hips=(-0.06, 0.0, 0.97), hips_rot=lean(turn=-30), chest=lean(pitch=-8, turn=-50), head=lean(pitch=-5, turn=10),
                     sword_hand=(-0.12, -0.20, 1.86), sword_elbow=(-0.25, -0.60, 1.55), blade=(-0.62, -0.30, 0.72), edge=(0.6, 0, 0.8),
                     shield_elbow=(0.10, 0.20, 1.50))
    P['CLEAVE'] = dict(HOME, feet=LUNGE, hips=(0.12, 0.0, 0.88), hips_rot=lean(pitch=12, turn=10), chest=lean(pitch=24, turn=15), head=lean(pitch=-12),
                       sword_hand=(0.58, -0.02, 1.12), sword_elbow=(0.35, -0.48, 1.05), blade=(0.80, 0.22, -0.30), edge=(0.3, 0, -1),
                       shield_elbow=(0.30, 0.50, 1.00))
    P['THROUGH'] = dict(HOME, feet=LUNGE, hips=(0.14, 0.02, 0.84), hips_rot=lean(pitch=16, turn=28), chest=lean(pitch=34, turn=48), head=lean(pitch=-18),
                        sword_hand=(0.36, 0.26, 0.66), sword_elbow=(0.36, -0.30, 0.95), blade=(0.25, 0.60, -0.76), edge=(-0.6, 0.4, -0.4),
                        shield_elbow=(0.20, 0.60, 0.90))
    P['ROAR'] = dict(HOME, hips=(-0.02, 0.0, 1.0), hips_rot=lean(), chest=lean(pitch=-12), head=lean(pitch=-24),
                     sword_hand=(0.10, -0.10, 1.72), sword_elbow=(0.0, -0.55, 1.45), blade=(0.50, 0.10, 0.86), edge=(1, 0, 0), shield_elbow=(0.0, 0.55, 1.45))
    P['ROAR_DOWN'] = dict(P['ROAR'], hips=(0.0, 0.0, 0.93), chest=lean(pitch=-4), head=lean(pitch=-12),
                          sword_hand=(0.16, -0.10, 1.48), sword_elbow=(0.0, -0.60, 1.25), shield_elbow=(0.0, 0.60, 1.25))
    wT, rT = 0.98, 0.54
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.5, 'HEAVE', 'ease'), (1.0, 'SAG', 'ease'), (1.5, 'GLARE_L', 'ease'), (2.1, 'GLARE_L', 'linear'),
                           (2.6, 'HEAVE', 'ease'), (3.1, 'GLARE_R', 'ease'), (3.6, 'GLARE_R', 'linear'), (4.2, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'W_DOWN_L', 'W_UP_L', 'W_DOWN_R', 'W_UP_R'), kind='cycle', frames=12,
                     gait=_gait(wT, 0.40, 0.09, 0.40, -0.32, 0.17, 10, 0.62)),
        'run': dict(keys=_cycle(rT, 'R_DOWN_L', 'R_UP', 'R_DOWN_R', 'R_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.66, 0.22, 0.58, -0.46, 0.12, 5, 0.36)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.30, 'HEFT', 'ease'), (0.36, 'HEFT', 'linear'), (0.46, 'CLEAVE', 'in'), (0.56, 'THROUGH', 'out'),
                             (0.70, 'THROUGH', 'linear'), (1.05, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.0833, 0.1667, 0.25, 0.3333, 0.375, 0.4167, 0.4583, 0.5, 0.5417, 0.625, 0.75, 0.875, 0.9583]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.35, 'ROAR', 'out'), (0.65, 'ROAR_DOWN', 'ease'), (0.95, 'ROAR', 'out'), (1.25, 'ROAR_DOWN', 'ease'),
                                (1.55, 'ROAR', 'out'), (2.0, 'ROAR', 'linear'), (2.6, 'HOME', 'ease')], kind='loop', frames=14),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    return P, {k: clips[k] for k in ORDER}


def giant(cfg):
    """A hill giant with a club: everything slow and heavy. He stands stooped with the club's head on the ground,
    lumbers with his whole trunk rocking over each foot, and brings the club up over his head in both the time a
    man takes to strike twice and down with his knees and back in it; then drags it back."""
    WIDE = {'Left': (0.0, 0.20, 16), 'Right': (0.0, -0.20, -16)}
    DRAG = dict(sword_hand=(0.14, -0.36, 0.86), sword_elbow=(-0.25, -0.52, 1.10), blade=(0.55, -0.08, -0.83), edge=(1, 0, 0))
    HOME = dict(KP.RELAXED, feet=WIDE, hips=(0.0, 0.0, 0.98), chest=lean(pitch=5), head=lean(pitch=-4), fist={'Right': 70, 'Left': 40},
                shield_hand=(0.14, 0.36, 0.88), shield_elbow=(-0.25, 0.52, 1.10), **DRAG)
    P = {'HOME': HOME,
         'IN': dict(HOME, hips=(0.0, 0.0, 0.99), chest=lean(pitch=1), Left_lift=7, Right_lift=7, head=lean(pitch=-7)),
         'OUT': dict(HOME, hips=(0.0, 0.0, 0.965), chest=lean(pitch=9), head=lean(pitch=0)),
         'SCRATCH': dict(HOME, hips=(0.0, 0.04, 0.975), hips_rot=lean(roll=4), chest=lean(pitch=6, roll=-3, turn=8), head=lean(turn=30, pitch=4),
                         shield_hand=(0.20, 0.10, 1.30), shield_elbow=(0.10, 0.55, 1.05)),
         'SWAY_R': dict(HOME, hips=(0.0, -0.05, 0.975), hips_rot=lean(roll=-4), chest=lean(pitch=6, roll=3, turn=-10), head=lean(turn=-32, pitch=2))}
    W = dict(HOME, hips_rot=lean(pitch=4), chest=lean(pitch=9), head=lean(pitch=-6))
    P['W_DOWN_L'] = dict(W, hips=(0.02, 0.03, 0.91), hips_rot=lean(pitch=4, turn=-12, roll=-3), chest=lean(pitch=12, turn=10, roll=3),
                         Right_shrug=10, Left_shrug=-10, sword_hand=(0.30, -0.32, 0.92), shield_hand=(-0.20, 0.38, 0.90), shield_elbow=(-0.50, 0.52, 1.20))
    P['W_UP_L'] = dict(W, hips=(0.0, 0.08, 0.99), hips_rot=lean(pitch=4, roll=6), chest=lean(pitch=7, roll=9), head=lean(pitch=-6, roll=-4),
                       sword_hand=(0.10, -0.37, 0.86), shield_hand=(0.06, 0.38, 0.88))
    P['W_DOWN_R'] = dict(W, hips=(0.02, -0.03, 0.91), hips_rot=lean(pitch=4, turn=12, roll=3), chest=lean(pitch=12, turn=-10, roll=-3),
                         Left_shrug=10, Right_shrug=-10, sword_hand=(-0.10, -0.38, 0.88), sword_elbow=(-0.45, -0.52, 1.20),
                         shield_hand=(0.32, 0.32, 0.95))
    P['W_UP_R'] = dict(W, hips=(0.0, -0.08, 0.99), hips_rot=lean(pitch=4, roll=-6), chest=lean(pitch=7, roll=-9), head=lean(pitch=-6, roll=4),
                       sword_hand=(0.08, -0.37, 0.86), shield_hand=(0.08, 0.38, 0.88))
    R = dict(HOME, hips_rot=lean(pitch=12), chest=lean(pitch=22), head=lean(pitch=-14), fist={'Right': 70, 'Left': 65},
             sword_hand=(-0.05, -0.36, 0.98), sword_elbow=(-0.40, -0.50, 1.20), blade=(-0.80, -0.10, 0.40), edge=(0, 0, -1))
    P['R_DOWN_L'] = dict(R, hips=(0.10, 0.03, 0.84), hips_rot=lean(pitch=12, turn=-12), chest=lean(pitch=25, turn=12),
                         shield_hand=(-0.22, 0.36, 1.02), shield_elbow=(-0.50, 0.48, 1.32))
    P['R_UP'] = dict(R, hips=(0.10, 0.0, 0.97), shield_hand=(0.10, 0.34, 1.08), shield_elbow=(-0.30, 0.50, 1.15))
    P['R_DOWN_R'] = dict(R, hips=(0.10, -0.03, 0.84), hips_rot=lean(pitch=12, turn=12), chest=lean(pitch=25, turn=-12),
                         shield_hand=(0.36, 0.26, 1.22), shield_elbow=(-0.08, 0.50, 1.00))
    # The smash.
    STEP = {'Left': (0.30, 0.20, 10), 'Right': (-0.22, -0.20, -30)}
    P['GATHER'] = dict(HOME, feet=STEP, hips=(-0.04, 0.0, 0.90), hips_rot=lean(pitch=6, turn=-15), chest=lean(pitch=14, turn=-25),
                       sword_hand=(-0.20, -0.40, 0.95), sword_elbow=(-0.50, -0.55, 1.25), blade=(-0.6, -0.2, 0.2), edge=(0, 0, -1),
                       shield_hand=(0.40, 0.25, 1.15), shield_elbow=(0.10, 0.55, 1.05))
    P['ALOFT'] = dict(HOME, feet=STEP, hips=(-0.08, 0.0, 1.0), hips_rot=lean(pitch=-4, turn=-20), chest=lean(pitch=-16, turn=-28), head=lean(pitch=-10),
                      Right_lift=14, sword_hand=(-0.16, -0.22, 1.98), sword_elbow=(-0.20, -0.62, 1.60), blade=(-0.50, -0.10, 0.86), edge=(1, 0, 0.5),
                      shield_hand=(0.45, 0.22, 1.40), shield_elbow=(0.15, 0.55, 1.20))
    P['SMASH'] = dict(HOME, feet=STEP, hips=(0.14, 0.0, 0.80), hips_rot=lean(pitch=18, turn=10), chest=lean(pitch=40, turn=14), head=lean(pitch=-22),
                      Right_shrug=14, sword_hand=(0.66, -0.12, 0.72), sword_elbow=(0.40, -0.50, 1.00), blade=(0.88, 0.05, -0.48), edge=(0.4, 0, -1),
                      shield_hand=(-0.10, 0.40, 0.95), shield_elbow=(-0.40, 0.52, 1.20))
    P['REBOUND'] = dict(P['SMASH'], hips=(0.12, 0.0, 0.83), chest=lean(pitch=36, turn=14), sword_hand=(0.62, -0.12, 0.80), blade=(0.90, 0.05, -0.42))
    P['HAUL'] = dict(HOME, feet=STEP, hips=(0.04, 0.0, 0.92), hips_rot=lean(pitch=8), chest=lean(pitch=16), sword_hand=(0.36, -0.30, 0.85),
                     blade=(0.75, -0.05, -0.66))
    P['BEAT_UP'] = dict(HOME, hips=(0.0, 0.0, 1.0), chest=lean(pitch=-10), head=lean(pitch=-22), Right_lift=12,
                        sword_hand=(0.05, -0.34, 1.98), sword_elbow=(0.0, -0.62, 1.65), blade=(0.0, -0.15, 1), edge=(1, 0, 0),
                        shield_hand=(0.26, 0.10, 1.34), shield_elbow=(0.10, 0.58, 1.10), fist={'Right': 70, 'Left': 70})
    P['BEAT_OUT'] = dict(P['BEAT_UP'], hips=(0.0, 0.0, 0.96), sword_hand=(0.10, -0.40, 1.80), shield_hand=(0.34, 0.30, 1.30), shield_elbow=(0.10, 0.62, 1.15))
    wT, rT = 1.5, 0.9
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.9, 'IN', 'ease'), (1.8, 'OUT', 'ease'), (2.6, 'SCRATCH', 'ease'), (3.4, 'SCRATCH', 'linear'),
                           (4.2, 'IN', 'ease'), (5.0, 'SWAY_R', 'ease'), (5.6, 'SWAY_R', 'linear'), (6.4, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'W_DOWN_L', 'W_UP_L', 'W_DOWN_R', 'W_UP_R'), kind='cycle', frames=12,
                     gait=_gait(wT, 0.36, 0.10, 0.36, -0.30, 0.20, 14, 0.64)),
        'run': dict(keys=_cycle(rT, 'R_DOWN_L', 'R_UP', 'R_DOWN_R', 'R_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.56, 0.18, 0.48, -0.40, 0.17, 10, 0.44)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.40, 'GATHER', 'ease'), (0.95, 'ALOFT', 'ease'), (1.10, 'ALOFT', 'linear'), (1.28, 'SMASH', 'in'),
                             (1.36, 'REBOUND', 'out'), (1.46, 'SMASH', 'ease'), (1.75, 'SMASH', 'linear'), (2.15, 'HAUL', 'ease'), (2.6, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.1667, 0.3333, 0.5, 0.6667, 0.8333, 1.0, 1.125, 1.1667, 1.2083, 1.25, 1.2917, 1.375, 1.4583, 1.75, 2.0, 2.25, 2.4583]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.6, 'BEAT_UP', 'ease'), (0.9, 'BEAT_OUT', 'ease'), (1.2, 'BEAT_UP', 'ease'), (1.5, 'BEAT_OUT', 'ease'),
                                (1.8, 'BEAT_UP', 'ease'), (2.1, 'BEAT_OUT', 'ease'), (2.5, 'BEAT_UP', 'ease'), (3.4, 'HOME', 'ease')], kind='loop', frames=14),
    }
    deaths = _deaths(P, frames=10, flinch=[0.0, 0.0833, 0.1667, 0.3333, 0.5833, 0.8333])
    # A giant falls slowly: every key of his flinch and deaths at twice the time.
    for c in deaths.values():
        c['keys'] = [(t * 2.0, n, e) for t, n, e in c['keys']]
        if 'seconds' in c:
            c['seconds'] *= 2.0
    clips.update(deaths)
    return P, {k: clips[k] for k in ORDER}


def healer(cfg):
    """The journeyman: a staff for a walking stick, short steps under his robe. His 'attack' is the laying on of
    hands: the staff lifted, his free hand raised open and brought down and out over the one he tends."""
    STAFF = dict(sword_hand=(0.22, -0.30, 1.18), sword_elbow=(-0.15, -0.46, 0.98), blade=(0.0, 0.0, 1), edge=(1, 0, 0))
    HOME = dict(KP.RELAXED, fist={'Right': 70, 'Left': 20}, chest=lean(pitch=6), head=lean(pitch=4),
                shield_hand=(0.12, 0.26, 0.92), shield_elbow=(-0.25, 0.36, 1.10), **STAFF)
    P = {'HOME': HOME,
         'BREATHE': dict(HOME, hips=(0.0, 0.0, 1.015), chest=lean(pitch=3), Left_lift=4, Right_lift=4),
         'LEAN_ON': dict(HOME, hips=(0.03, -0.04, 1.01), hips_rot=lean(roll=-3), chest=lean(pitch=10, turn=-6), head=lean(pitch=6, turn=-25),
                         sword_hand=(0.26, -0.28, 1.22), blade=(-0.06, 0.03, 1)),
         'LOOK_L': dict(HOME, chest=lean(pitch=5, turn=8), head=lean(turn=38, pitch=2))}
    # Walking with the staff: it is planted ahead as the left foot lands, he walks past it, lifts it and sets it on.
    W = dict(HOME, hips_rot=lean(pitch=2), chest=lean(pitch=8))
    P['W_DOWN_L'] = dict(W, hips=(0.0, 0.01, 0.97), hips_rot=lean(pitch=2, turn=-4), chest=lean(pitch=9, turn=4),
                         sword_hand=(0.34, -0.30, 1.16), blade=(0.16, 0.0, 0.99), shield_hand=(0.0, 0.28, 0.92))
    P['W_UP_L'] = dict(W, hips=(0.0, 0.03, 1.0), hips_rot=lean(pitch=2, roll=2), chest=lean(pitch=7, roll=2),
                       sword_hand=(0.20, -0.30, 1.19), blade=(0.0, 0.0, 1), shield_hand=(0.10, 0.27, 0.92))
    P['W_DOWN_R'] = dict(W, hips=(0.0, -0.01, 0.97), hips_rot=lean(pitch=2, turn=4), chest=lean(pitch=9, turn=-4),
                         sword_hand=(0.06, -0.31, 1.18), blade=(-0.16, 0.0, 0.99), shield_hand=(0.20, 0.26, 0.94))
    P['W_UP_R'] = dict(W, hips=(0.0, -0.03, 1.0), hips_rot=lean(pitch=2, roll=-2), chest=lean(pitch=7, roll=-2),
                       sword_hand=(0.22, -0.30, 1.26), blade=(0.10, 0.0, 0.99), shield_hand=(0.10, 0.27, 0.92))
    # Hurrying: the staff carried level at his side, his free hand gathering his robe.
    R = dict(HOME, hips_rot=lean(pitch=10), chest=lean(pitch=18), head=lean(pitch=-8), fist={'Right': 70, 'Left': 60},
             sword_hand=(0.10, -0.32, 0.98), sword_elbow=(-0.30, -0.45, 1.10), blade=(0.96, 0.0, 0.26), edge=(0, 0, -1),
             shield_hand=(0.22, 0.20, 0.98), shield_elbow=(-0.10, 0.48, 1.05))
    P['R_DOWN_L'] = dict(R, hips=(0.06, 0.0, 0.90), chest=lean(pitch=20, turn=5), sword_hand=(0.18, -0.32, 0.98))
    P['R_UP'] = dict(R, hips=(0.06, 0.0, 0.97))
    P['R_DOWN_R'] = dict(R, hips=(0.06, 0.0, 0.90), chest=lean(pitch=20, turn=-5), sword_hand=(0.02, -0.32, 0.98))
    # Healing.
    P['INVOKE'] = dict(HOME, hips=(-0.02, 0.0, 1.03), chest=lean(pitch=-8), head=lean(pitch=-20), Left_lift=10, fist={'Right': 70, 'Left': 0},
                       sword_hand=(0.26, -0.34, 1.50), sword_elbow=(0.0, -0.58, 1.25), blade=(0.0, -0.05, 1),
                       shield_hand=(0.20, 0.30, 1.95), shield_elbow=(0.05, 0.58, 1.60))
    P['BESTOW'] = dict(HOME, feet={'Left': (0.16, 0.15, 8), 'Right': (-0.06, -0.15, -12)}, hips=(0.06, 0.0, 0.97), hips_rot=lean(pitch=6),
                       chest=lean(pitch=20, turn=-6), head=lean(pitch=14), fist={'Right': 70, 'Left': 0},
                       sword_hand=(0.24, -0.34, 1.22), blade=(0.12, -0.03, 0.99),
                       shield_hand=(0.62, 0.12, 1.20), shield_elbow=(0.30, 0.50, 1.10))
    P['HOLD'] = dict(P['BESTOW'], hips=(0.07, 0.0, 0.96), shield_hand=(0.64, 0.10, 1.12), chest=lean(pitch=22, turn=-6))
    P['PRAISE'] = dict(P['INVOKE'], sword_hand=(0.20, -0.36, 1.85), sword_elbow=(0.0, -0.60, 1.55), hips=(0.0, 0.0, 1.03))
    P['PRAISE_DOWN'] = dict(P['PRAISE'], hips=(0.0, 0.0, 0.98), sword_hand=(0.22, -0.36, 1.62), shield_hand=(0.22, 0.32, 1.70), head=lean(pitch=-10))
    wT, rT = 1.1, 0.6
    step = cfg.get('step', 0.8)
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.9, 'BREATHE', 'ease'), (1.8, 'LEAN_ON', 'ease'), (2.8, 'LEAN_ON', 'linear'),
                           (3.6, 'LOOK_L', 'ease'), (4.2, 'LOOK_L', 'linear'), (5.0, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'W_DOWN_L', 'W_UP_L', 'W_DOWN_R', 'W_UP_R'), kind='cycle', frames=12,
                     gait=_gait(wT, 0.40, 0.07, 0.36, -0.29, 0.13, 6, 0.60, step)),
        'run': dict(keys=_cycle(rT, 'R_DOWN_L', 'R_UP', 'R_DOWN_R', 'R_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.60, 0.16, 0.46, -0.38, 0.11, 5, 0.40, step)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.40, 'INVOKE', 'ease'), (0.55, 'INVOKE', 'linear'), (0.85, 'BESTOW', 'ease'),
                             (1.15, 'HOLD', 'ease'), (1.6, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.8333, 0.9583, 1.0833, 1.25, 1.4167]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.5, 'PRAISE', 'ease'), (0.9, 'PRAISE_DOWN', 'ease'), (1.3, 'PRAISE', 'ease'),
                                (1.7, 'PRAISE_DOWN', 'ease'), (2.1, 'PRAISE', 'ease'), (2.5, 'PRAISE', 'linear'), (3.2, 'HOME', 'ease')],
                          kind='loop', frames=14),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    return P, {k: clips[k] for k in ORDER}


def minotaur(cfg):
    """The minotaur: the giant's weight at a bull's pace. His poses are the giant's (a stooped brute with a long
    haft in one hand); every clip runs in six tenths of the time, so the axe comes down like a charge, not a tree."""
    P, clips = giant(cfg)
    k = 0.62
    for c in clips.values():
        c['keys'] = [(t * k, n, e) for t, n, e in c['keys']]
        if 'times' in c:
            c['times'] = [round(t * k * 24) / 24 for t in c['times']]
            c['times'] = sorted(set(c['times']))
        if 'seconds' in c:
            c['seconds'] *= k
        if 'gait' in c:
            c['gait'] = dict(c['gait'], period=c['gait']['period'] * k)
    return P, clips


STYLES = {'minotaur': minotaur, 'thrower': thrower, 'dwarf_thrower': dwarf_thrower, 'hero': hero, 'spearman': spearman, 'berserker': berserker,
          'giant': giant, 'healer': healer}
