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
        if name == 'DC_STRUCK':
            p.update(sword_hand=(0.22, -0.50, 0.92), sword_elbow=(-0.10, -0.55, 1.15), blade=(0.55, -0.55, -0.62))
            p.update(shield_hand=(0.38, 0.28, 1.12), shield_elbow=(0.10, 0.62, 0.95))
        if name == 'DC_SLUMP':     # on his side: both arms flung out before him, not folded under a thick chest; the
            # lower (right) arm reaching out past his head as a man lies on his side, its armpit open, not under his ribs
            p.update(sword_hand=(0.50, -0.92, 0.10), sword_elbow=(0.30, -0.80, 0.30), shield_hand=(0.66, 0.05, 0.22), shield_elbow=(0.35, 0.30, 0.60))
        if name == 'DC_KNEEL':
            # Toppling from his knees onto his right side, a man puts that arm out to the ground: it goes out and
            # down before he lands on it, not through his own side as the chest rolls over it.
            P['DC_TOPPLE'] = dict(p, hips=(-0.02, -0.08, 0.42), hips_rot=lean(pitch=5, roll=-35), chest=lean(pitch=28, roll=-45),
                                  head=lean(pitch=30, roll=-35), sword_hand=(0.42, -0.80, 0.22), sword_elbow=(0.18, -0.75, 0.55),
                                  blade=(0.6, -0.6, -0.2), shield_hand=(0.30, 0.20, 0.55), shield_elbow=(0.0, 0.42, 0.60))
        if name in ('DC_KNEEL', 'BURN_KNEES'):
            p.update(sword_hand=(0.28, -0.52, 0.50), sword_elbow=(-0.05, -0.60, 0.80), blade=(0.70, -0.62, -0.35))
            p.update(shield_hand=(0.30, 0.42, 0.62), shield_elbow=(-0.05, 0.62, 0.70))
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
                                  (0.75, 'DC_KNEEL', 'linear'), (0.97, 'DC_TOPPLE', 'in'), (1.15, 'DC_SLUMP', 'out'), (1.5, 'DC_SLUMP', 'linear')],
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
                       times=[0.0, 0.125, 0.25, 0.3333, 0.4167, 0.5, 0.5833, 0.7083, 0.875], release=0.5, item_gone=0.5, swing=True),
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
    # Reared and leading, the elbow is out at his side and the hand above it, clear of his helmet and beard.
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
                       sword_hand=(-0.42, -0.40, 1.74), sword_elbow=(-0.30, -0.70, 1.48),
                       shield_hand=(0.48, 0.18, 1.45), shield_elbow=(0.15, 0.48, 1.20))
    P['T_LEAD'] = dict(T, feet=STEP1, hips=(0.08, 0.02, 0.94), hips_rot=lean(pitch=4, turn=5), chest=lean(pitch=2, turn=-15),
                       head=lean(pitch=-6), Right_lift=14,
                       sword_hand=(-0.10, -0.42, 1.90), sword_elbow=(0.15, -0.66, 1.58),
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
    # The cheer: the free fist punched up, a stamp either side; the satchel held out clear at his side (a sack on a
    # cord swung overhead would hang down through his own shoulder).
    C = dict(HOME, chest=lean(pitch=-6), head=lean(pitch=-18), fist={'Right': 65, 'Left': 65})
    P['C_UP'] = dict(C, hips=(0.0, 0.0, 1.03), Left_lift=12,
                     sword_hand=(0.14, -0.46, 1.00), sword_elbow=(-0.25, -0.60, 1.15),
                     shield_hand=(0.10, 0.32, 2.0), shield_elbow=(0.0, 0.60, 1.70))
    P['C_DOWN_L'] = dict(C, feet={'Left': (0.0, 0.19, 14), 'Right': (0.0, -0.19, -14, 0.10)}, hips=(0.0, 0.05, 0.93),
                         hips_rot=lean(roll=5), chest=lean(pitch=2, roll=6), head=lean(pitch=-8),
                         sword_hand=(0.12, -0.46, 0.96), sword_elbow=(-0.25, -0.60, 1.12),
                         shield_hand=(0.16, 0.36, 1.50), shield_elbow=(0.0, 0.65, 1.25))
    P['C_DOWN_R'] = dict(C, feet={'Left': (0.0, 0.19, 14, 0.10), 'Right': (0.0, -0.19, -14)}, hips=(0.0, -0.05, 0.93),
                         hips_rot=lean(roll=-5), chest=lean(pitch=2, roll=-6), head=lean(pitch=-8),
                         sword_hand=(0.12, -0.46, 0.96), sword_elbow=(-0.25, -0.60, 1.12),
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
                       release=0.75, item_gone=0.75, swing=True),
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


def _shuffle(period, step=1.0):
    """The advance in guard: the front foot goes forward a short step and the back foot is brought up after it; they
    never pass. Each foot treads its own ground (front and back of its reach), half a period apart."""
    return dict(period=period, swing=0.42, lift=0.07,
                feet={'Left': (0.34 * step, 0.10 * step, 0.16, 15, 0.55), 'Right': (-0.08 * step, -0.32 * step, -0.16, -40, 0.05)})


def hero(cfg):
    """The champion, sword and heater shield, as a man fights with them: always behind the shield. He stands in
    guard, left foot and shield leading, the sword held back and low at his right hip, point at the foe; advances
    with short guarded steps; charges with the shield before him; throws the cut over his shoulder past the
    shield's edge and through; and salutes for a cheer."""
    LOWSWORD = dict(sword_hand=(0.02, -0.34, 0.98), sword_elbow=(-0.35, -0.45, 1.10), blade=(0.90, 0.02, 0.42), edge=(0.3, 0, -1))
    HOME = dict(KP.GUARD, fist=GRIP, **LOWSWORD)
    P = {k: dict(getattr(KP, k), fist=GRIP) for k in ('WINDUP', 'STRIKE', 'FOLLOW', 'CHARGE_DOWN_L', 'CHARGE_DOWN_R', 'CHARGE_UP', 'CEL_UP', 'CEL_PUMP', 'SALUTE')}
    P['HOME'] = HOME
    P['WINDUP'] = dict(P['WINDUP'], sword_hand=(0.0, -0.36, 1.80), sword_elbow=(-0.20, -0.64, 1.50), blade=(-0.55, -0.32, 0.77), edge=(0.5, 0, 0.85))
    P['STRIKE'] = dict(P['STRIKE'], shield_hand=(0.36, 0.14, 1.18), shield_elbow=(0.26, 0.54, 1.02))   # the shield held off his chest
    P['FOLLOW'] = dict(P['FOLLOW'], shield_hand=(0.38, 0.24, 1.18), shield_elbow=(0.26, 0.58, 1.02))
    P['BREATHE'] = dict(HOME, hips=(0.0, 0.0, 0.96), chest=lean(pitch=8, turn=-10), shield_hand=(0.25, 0.03, 1.25))
    P['EASE'] = dict(HOME, hips=(0.0, 0.0, 0.97), chest=lean(pitch=8, turn=-4), head=lean(turn=30),
                     shield_hand=(0.26, 0.10, 1.16), shield_elbow=(0.22, 0.50, 1.02), shield_face=(1, 0.35, 0))
    P['WATCH'] = dict(HOME, chest=lean(pitch=10, turn=-16), head=lean(turn=-28), sword_hand=(0.04, -0.36, 1.0))
    P['ADV_L'] = dict(HOME, hips=(0.02, 0.0, 0.93), chest=lean(pitch=11, turn=-8), shield_hand=(0.27, 0.03, 1.26))
    P['ADV_UP'] = dict(HOME, hips=(0.0, 0.01, 0.96), chest=lean(pitch=9, turn=-10))
    P['ADV_R'] = dict(HOME, hips=(0.02, 0.0, 0.93), chest=lean(pitch=11, turn=-12), sword_hand=(0.05, -0.34, 0.98))
    P['ADV_UP2'] = dict(HOME, hips=(0.0, -0.01, 0.96), chest=lean(pitch=9, turn=-10))
    wT, rT = 0.72, 0.62
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.8, 'BREATHE', 'ease'), (1.6, 'EASE', 'ease'), (2.4, 'EASE', 'linear'),
                           (3.2, 'WATCH', 'ease'), (3.8, 'WATCH', 'linear'), (4.6, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'ADV_L', 'ADV_UP', 'ADV_R', 'ADV_UP2'), kind='cycle', frames=12, gait=_shuffle(wT)),
        'run': dict(keys=_cycle(rT, 'CHARGE_DOWN_L', 'CHARGE_UP', 'CHARGE_DOWN_R', 'CHARGE_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.64, 0.20, 0.50, -0.42, 0.12, 6, 0.38)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.28, 'WINDUP', 'ease'), (0.36, 'WINDUP', 'linear'),
                             (0.48, 'STRIKE', 'in'), (0.58, 'FOLLOW', 'out'), (0.70, 'FOLLOW', 'linear'), (1.05, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.0833, 0.1667, 0.25, 0.3333, 0.4167, 0.4583, 0.5, 0.5417, 0.5833, 0.6667, 0.7917, 0.9167]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.4, 'SALUTE', 'ease'), (0.8, 'SALUTE', 'linear'), (1.1, 'CEL_UP', 'out'), (1.4, 'CEL_PUMP', 'ease'),
                                (1.7, 'CEL_UP', 'out'), (2.0, 'CEL_PUMP', 'ease'), (2.3, 'CEL_UP', 'out'), (2.6, 'CEL_UP', 'linear'), (3.1, 'HOME', 'ease')],
                          kind='loop', frames=16),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    return P, {k: clips[k] for k in ORDER}


def spearman(cfg):
    """Light infantry: spear and round shield, fought from behind the shield. He stands in guard, the shield before
    him covering shoulder to thigh, the spear couched under his arm at the hip, its point out past the rim;
    advances in short steps without uncovering; runs with the spear levelled; and thrusts underarm from behind the
    shield, the back leg driving him onto the front one."""
    COUCH = dict(sword_hand=(0.06, -0.31, 1.06), sword_elbow=(-0.30, -0.46, 1.08), blade=(0.98, 0.03, 0.12), edge=(0, 0, -1))
    HOME = dict(KP.GUARD, fist=GRIP, **COUCH)
    P = {'HOME': HOME,
         'BREATHE': dict(HOME, hips=(0.0, 0.0, 0.96), chest=lean(pitch=8, turn=-10), shield_hand=(0.25, 0.03, 1.25)),
         'PEER': dict(HOME, hips=(0.0, 0.01, 0.96), chest=lean(pitch=9, turn=-4), head=lean(turn=32),
                      shield_hand=(0.26, 0.06, 1.20), blade=(0.97, 0.10, 0.16)),
         'SETTLE': dict(HOME, hips=(0.0, -0.01, 0.94), chest=lean(pitch=11, turn=-14), head=lean(turn=-25),
                        sword_hand=(0.02, -0.32, 1.05), blade=(0.98, -0.04, 0.10))}
    P['ADV_L'] = dict(HOME, hips=(0.02, 0.0, 0.93), chest=lean(pitch=11, turn=-8), shield_hand=(0.27, 0.03, 1.26))
    P['ADV_UP'] = dict(HOME, hips=(0.0, 0.01, 0.96), chest=lean(pitch=9, turn=-10))
    P['ADV_R'] = dict(HOME, hips=(0.02, 0.0, 0.93), chest=lean(pitch=11, turn=-12), sword_hand=(0.10, -0.31, 1.06))
    P['ADV_UP2'] = dict(HOME, hips=(0.0, -0.01, 0.96), chest=lean(pitch=9, turn=-10))
    # The run: the spear levelled at his hip, the shield forward, long low strides.
    R = dict(KP.GUARD, fist=GRIP, feet=KP.RELAXED['feet'], sword_hand=(0.02, -0.33, 1.04), sword_elbow=(-0.35, -0.45, 1.12),
             blade=(1, 0.0, 0.10), edge=(0, 0, -1), shield_hand=(0.38, 0.06, 1.24), shield_elbow=(0.30, 0.46, 1.08), shield_face=(1, 0.2, 0.05))
    P['R_DOWN_L'] = dict(R, hips=(0.06, 0.0, 0.87), hips_rot=lean(pitch=12, turn=-8), chest=lean(pitch=20, turn=6), head=lean(pitch=-6),
                         sword_hand=(0.10, -0.33, 1.04))
    P['R_UP'] = dict(R, hips=(0.06, 0.0, 0.95), hips_rot=lean(pitch=12), chest=lean(pitch=18), head=lean(pitch=-5))
    P['R_DOWN_R'] = dict(R, hips=(0.06, 0.0, 0.87), hips_rot=lean(pitch=12, turn=8), chest=lean(pitch=20, turn=-6), head=lean(pitch=-6),
                         sword_hand=(-0.06, -0.33, 1.04))
    P['DRAW'] = dict(KP.THRUST_CHAMBER, fist=GRIP, sword_hand=(-0.22, -0.32, 1.10), sword_elbow=(-0.55, -0.44, 1.18), blade=(1, 0.03, 0.03),
                     hips=(-0.08, 0.0, 0.93), hips_rot=lean(pitch=3, turn=-34), chest=lean(pitch=6, turn=-38))
    P['LUNGE'] = dict(KP.THRUST_LUNGE, fist=GRIP, feet={'Left': (0.46, 0.16, 8), 'Right': (-0.34, -0.16, -45)},
                      hips=(0.16, 0.0, 0.84), hips_rot=lean(pitch=10, turn=12), chest=lean(pitch=16, turn=24), head=lean(pitch=-8),
                      sword_hand=(0.80, -0.14, 1.20), sword_elbow=(0.50, -0.44, 1.05), blade=(1, 0.04, 0.02),
                      shield_hand=(0.32, 0.26, 1.32), shield_elbow=(0.20, 0.58, 1.14), shield_face=(0.8, 0.6, 0))
    P['C_UP'] = dict(HOME, hips=(0.0, 0.0, 1.0), hips_rot=lean(turn=-10), chest=lean(pitch=-6), head=lean(pitch=-18),
                     sword_hand=(0.12, -0.36, 1.92), sword_elbow=(0.0, -0.60, 1.65), blade=(0.05, -0.08, 1),
                     shield_hand=(0.22, 0.42, 1.50), shield_elbow=(0.0, 0.62, 1.30), shield_face=(0.6, 0.8, 0.2))
    P['C_DOWN'] = dict(P['C_UP'], hips=(0.0, 0.0, 0.95), head=lean(pitch=-8), sword_hand=(0.16, -0.38, 1.62), sword_elbow=(0.0, -0.62, 1.35),
                       shield_hand=(0.24, 0.40, 1.32))
    wT, rT = 0.66, 0.56
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.7, 'BREATHE', 'ease'), (1.4, 'PEER', 'ease'), (2.0, 'PEER', 'linear'),
                           (2.8, 'SETTLE', 'ease'), (3.5, 'SETTLE', 'linear'), (4.2, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'ADV_L', 'ADV_UP', 'ADV_R', 'ADV_UP2'), kind='cycle', frames=12, gait=_shuffle(wT, 1.05)),
        'run': dict(keys=_cycle(rT, 'R_DOWN_L', 'R_UP', 'R_DOWN_R', 'R_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.66, 0.22, 0.55, -0.45, 0.11, 4, 0.36)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.26, 'DRAW', 'ease'), (0.32, 'DRAW', 'linear'),
                             (0.42, 'LUNGE', 'in'), (0.56, 'LUNGE', 'linear'), (0.95, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.0833, 0.1667, 0.25, 0.3333, 0.375, 0.4167, 0.4583, 0.5417, 0.625, 0.7083, 0.8333]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.35, 'C_UP', 'out'), (0.65, 'C_DOWN', 'ease'), (0.95, 'C_UP', 'out'), (1.25, 'C_DOWN', 'ease'),
                                (1.55, 'C_UP', 'out'), (1.9, 'C_UP', 'linear'), (2.5, 'HOME', 'ease')], kind='loop', frames=14),
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
                         shield_hand=(0.42, 0.20, 1.28), shield_elbow=(0.10, 0.75, 1.00)),
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


def longsword(cfg):
    """The berserker's claymore as a two-handed sword is carried and used (the German longsword's stances). Low
    guard to stand in (left foot forward, knees bent, hands before the right hip, the point at the other man's
    face); on the right shoulder to walk and to run (one hand, the blade over the shoulder behind him, never across
    his body); the high guard at the right shoulder; and from it the cut: a passing step onto the left foot, the
    hips and chest coming round, the hands thrown out and down, the edge leading, through to the left hip, and back
    to the low guard. The pommel is always clear of his belly: the hands are held a forearm out from the body."""
    STANCE = {'Left': (0.20, 0.16, 12), 'Right': (-0.20, -0.17, -40)}
    BACK = {'Left': (0.14, 0.16, 12), 'Right': (-0.26, -0.17, -45)}
    PASS = {'Left': (0.40, 0.15, 6), 'Right': (-0.26, -0.17, -45)}
    TWO = dict(two_hands=0.17, fist={'Right': 70, 'Left': 70}, shield_hand=(0.3, 0.0, 0.9))
    LOW = dict(KP.RELAXED, feet=STANCE, hips=(0.0, 0.0, 0.95), hips_rot=lean(pitch=4, turn=-18), chest=lean(pitch=8, turn=-12), head=lean(pitch=-2),
               sword_hand=(0.56, -0.10, 1.02), sword_elbow=(0.05, -1.00, 1.05), blade=(0.80, 0.10, 0.59), edge=(0.5, 0, -0.8),
               shield_elbow=(0.05, 1.00, 1.00), **TWO)
    HIGH = dict(LOW, feet=BACK, hips=(-0.06, 0.0, 0.96), hips_rot=lean(turn=-30), chest=lean(pitch=-2, turn=-38), head=lean(turn=8),
                sword_hand=(0.34, -0.26, 1.60), sword_elbow=(0.0, -0.68, 1.28), blade=(-0.22, -0.06, 0.97), edge=(1, 0, 0.2),
                shield_elbow=(0.36, 0.52, 1.10))
    P = {'HOME': LOW,
         'BREATHE': dict(LOW, hips=(0.0, 0.0, 0.96), chest=lean(pitch=5, turn=-12), Left_lift=5, Right_lift=5, sword_hand=(0.56, -0.10, 1.04)),
         'SINK': dict(LOW, hips=(0.01, 0.0, 0.93), chest=lean(pitch=10, turn=-12), sword_hand=(0.57, -0.10, 1.00), blade=(0.82, 0.10, 0.56)),
         'EYE_L': dict(LOW, hips=(0.0, 0.02, 0.95), chest=lean(pitch=8, turn=-4), head=lean(turn=38, pitch=2),
                       sword_hand=(0.56, -0.05, 1.03), blade=(0.78, 0.22, 0.58)),
         'EYE_R': dict(LOW, hips=(0.0, -0.02, 0.95), chest=lean(pitch=8, turn=-22), head=lean(turn=-34, pitch=2),
                       sword_hand=(0.54, -0.16, 1.02), blade=(0.80, -0.04, 0.59)),
         'HIGH': HIGH}
    # On the shoulder: one hand on the grip before the right breast, the flat on the shoulder, the point up behind.
    CARRY = dict(KP.RELAXED, fist={'Right': 70, 'Left': 40}, sword_hand=(0.32, -0.34, 1.40), sword_elbow=(0.05, -0.62, 1.10),
                 blade=(-0.42, -0.24, 0.87), edge=(0, 0, -1), shield_elbow=(-0.25, 0.42, 1.10))
    P['W_DOWN_L'] = dict(CARRY, hips=(0.02, 0.02, 0.94), hips_rot=lean(pitch=3, turn=-9), chest=lean(pitch=6, turn=7, roll=2),
                         shield_hand=(-0.18, 0.36, 0.92))
    P['W_UP_L'] = dict(CARRY, hips=(0.0, 0.05, 1.0), hips_rot=lean(pitch=3, roll=4), chest=lean(pitch=4, roll=4), shield_hand=(0.05, 0.36, 0.88))
    P['W_DOWN_R'] = dict(CARRY, hips=(0.02, -0.02, 0.94), hips_rot=lean(pitch=3, turn=9), chest=lean(pitch=6, turn=-7, roll=-2),
                         shield_hand=(0.30, 0.30, 0.98))
    P['W_UP_R'] = dict(CARRY, hips=(0.0, -0.05, 1.0), hips_rot=lean(pitch=3, roll=-4), chest=lean(pitch=4, roll=-4), shield_hand=(0.05, 0.36, 0.88))
    # Running: the same carry, the hand a little higher and tighter in, his trunk forward, the free arm driving.
    RUN = dict(CARRY, fist={'Right': 70, 'Left': 65}, hips_rot=lean(pitch=12), chest=lean(pitch=20), head=lean(pitch=-12),
               sword_hand=(0.26, -0.31, 1.32), sword_elbow=(0.02, -0.62, 1.08), blade=(-0.36, -0.24, 0.90))
    P['R_DOWN_L'] = dict(RUN, hips=(0.10, 0.0, 0.86), chest=lean(pitch=22, turn=8), shield_hand=(-0.20, 0.32, 1.02), shield_elbow=(-0.50, 0.42, 1.30))
    P['R_UP'] = dict(RUN, hips=(0.10, 0.0, 0.95), shield_hand=(0.12, 0.30, 1.08), shield_elbow=(-0.30, 0.48, 1.12))
    P['R_DOWN_R'] = dict(RUN, hips=(0.10, 0.0, 0.86), chest=lean(pitch=22, turn=-8), shield_hand=(0.38, 0.22, 1.22), shield_elbow=(-0.05, 0.48, 1.00))
    # The cut from the high guard.
    P['CUT'] = dict(LOW, feet=PASS, hips=(0.14, 0.0, 0.90), hips_rot=lean(pitch=10, turn=12), chest=lean(pitch=20, turn=18), head=lean(pitch=-10),
                    sword_hand=(0.68, -0.04, 1.24), sword_elbow=(0.35, -0.50, 1.10), blade=(0.93, 0.14, 0.08), edge=(0.2, 0, -1),
                    shield_elbow=(0.36, 0.64, 0.95))
    P['THROUGH'] = dict(LOW, feet=PASS, hips=(0.17, 0.02, 0.86), hips_rot=lean(pitch=14, turn=26), chest=lean(pitch=28, turn=40), head=lean(pitch=-14),
                        sword_hand=(0.66, 0.22, 1.04), sword_elbow=(0.42, -0.35, 1.10), blade=(0.66, 0.40, -0.64), edge=(-0.4, 0.3, -0.85),
                        shield_elbow=(0.34, 0.70, 0.85))
    P['RECOVER'] = dict(LOW, feet=PASS, hips=(0.10, 0.0, 0.93), hips_rot=lean(pitch=6, turn=4), chest=lean(pitch=12, turn=6),
                        sword_hand=(0.60, 0.02, 1.06), blade=(0.84, 0.20, 0.50))
    # The cheer: the sword thrust up at arm's length out to his right, the left fist shaken.
    P['SKY'] = dict(KP.RELAXED, feet=STANCE, hips=(0.0, 0.0, 1.0), chest=lean(pitch=-8), head=lean(pitch=-20), fist={'Right': 70, 'Left': 70},
                    sword_hand=(0.08, -0.44, 1.82), sword_elbow=(-0.05, -0.62, 1.50), blade=(0.05, -0.22, 0.97), edge=(1, 0, 0),
                    shield_hand=(0.20, 0.36, 1.50), shield_elbow=(0.0, 0.62, 1.25))
    P['SKY_DOWN'] = dict(P['SKY'], hips=(0.0, 0.0, 0.95), head=lean(pitch=-10), sword_hand=(0.12, -0.46, 1.62), shield_hand=(0.24, 0.38, 1.28))
    wT, rT = 0.98, 0.56
    clips = {
        'idle': dict(keys=[(0.0, 'HOME', 'linear'), (0.6, 'BREATHE', 'ease'), (1.2, 'SINK', 'ease'), (1.8, 'EYE_L', 'ease'), (2.4, 'EYE_L', 'linear'),
                           (3.0, 'BREATHE', 'ease'), (3.6, 'EYE_R', 'ease'), (4.1, 'EYE_R', 'linear'), (4.8, 'HOME', 'ease')], kind='loop', frames=12),
        'walk': dict(keys=_cycle(wT, 'W_DOWN_L', 'W_UP_L', 'W_DOWN_R', 'W_UP_R'), kind='cycle', frames=12,
                     gait=_gait(wT, 0.40, 0.09, 0.40, -0.32, 0.15, 8, 0.62)),
        'run': dict(keys=_cycle(rT, 'R_DOWN_L', 'R_UP', 'R_DOWN_R', 'R_UP'), kind='cycle', frames=10,
                    gait=_gait(rT, 0.66, 0.22, 0.58, -0.46, 0.12, 5, 0.36)),
        'attack': dict(keys=[(0.0, 'HOME', 'linear'), (0.30, 'HIGH', 'ease'), (0.38, 'HIGH', 'linear'), (0.50, 'CUT', 'in'), (0.60, 'THROUGH', 'out'),
                             (0.72, 'THROUGH', 'linear'), (0.95, 'RECOVER', 'ease'), (1.2, 'HOME', 'ease')],
                       kind='still', times=[0.0, 0.0833, 0.1667, 0.25, 0.3333, 0.4167, 0.4583, 0.5, 0.5417, 0.5833, 0.6667, 0.7917, 0.9167, 1.0417, 1.125]),
        'celebrate': dict(keys=[(0.0, 'HOME', 'linear'), (0.45, 'SKY', 'out'), (0.75, 'SKY_DOWN', 'ease'), (1.05, 'SKY', 'out'), (1.35, 'SKY_DOWN', 'ease'),
                                (1.65, 'SKY', 'out'), (2.1, 'SKY', 'linear'), (2.8, 'HOME', 'ease')], kind='loop', frames=14),
    }
    clips.update(_deaths(P, frames=10, flinch=[0.0, 0.0417, 0.0833, 0.1667, 0.2917, 0.4167]))
    return P, {k: clips[k] for k in ORDER}


STYLES = {'longsword': longsword, 'minotaur': minotaur, 'thrower': thrower, 'dwarf_thrower': dwarf_thrower, 'hero': hero, 'spearman': spearman,
          'giant': giant, 'healer': healer}
