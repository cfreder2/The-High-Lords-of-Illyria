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


STYLES = {'thrower': thrower, 'dwarf_thrower': dwarf_thrower}
