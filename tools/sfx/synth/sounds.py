"""The recipes: one function per sound id in game/web/sfx/sounds.json.

Each takes (rng, v), the variant's random generator and its index, and
returns a mono array (one-shots) or a stereo (n, 2) array (ambience loops) at
dsp.SR. Layered the way a sound designer would: a transient, a body, a tail.
"""
import numpy as np

from dsp import (SR, bandpass, brown, click_grain, convolve, env_ad, env_points, fade, grains, highpass, impulse,
                 karplus, loopify, lowpass, modal, osc, pan, pink, place, resonator, reverb_ir, saturate, silence,
                 sine_sweep, svf_bandpass, t_axis, tilt, trim_tail, white)

RECIPES = {}
# How each is finished: "loop" sounds are made seamless; "space" is the
# outdoor reverb they get (seconds of tail, wet share).
FINISH = {}


def recipe(id, space=(0.5, 0.18), loop=None):
    def wrap(f):
        RECIPES[id] = f
        FINISH[id] = {"space": space, "loop": loop}
        return f
    return wrap


def jitter(rng, x, amount):
    return x * (1 + rng.uniform(-amount, amount))


def wet_bits(rng, seconds, rate, lo=250, hi=1400):
    """Squelchy little bubbles and drips (wet flesh)."""
    def bubble(r):
        d = r.uniform(0.006, 0.025)
        f0 = np.exp(r.uniform(np.log(lo), np.log(hi)))
        s = sine_sweep(f0, f0 * r.uniform(1.4, 2.6), d * 3) * np.exp(-t_axis(d * 3) / d)
        return s * r.uniform(0.3, 1)
    return grains(rng, seconds, rate, bubble, density_curve=lambda u: 1 - u)


def thud(rng, f0, seconds, drop=0.55, noise_lp=500):
    t = t_axis(seconds)
    body = sine_sweep(f0, f0 * drop, seconds) * np.exp(-t / (seconds / 4))
    n = lowpass(white(rng, seconds), noise_lp, 2) * np.exp(-t / (seconds / 7)) * 0.8
    return body + n


def metal_modes(rng, base, count=9, decay=(0.25, 1.1), spread=0.03):
    ratios = [1.0, 2.32, 3.73, 4.9, 6.1, 7.7, 9.3, 11.2, 13.6, 15.9]
    modes = []
    for i in range(count):
        f = base * ratios[i] * (1 + rng.uniform(-spread, spread))
        modes.append((f, rng.uniform(*decay) / (1 + i * 0.15), rng.uniform(0.35, 1.0) / (1 + i * 0.25)))
    return modes


def wood_modes(rng, base, count=5, decay=(0.04, 0.16)):
    ratios = [1.0, 2.31, 3.89, 5.43, 7.2, 9.1]
    return [(base * ratios[i] * (1 + rng.uniform(-0.05, 0.05)), rng.uniform(*decay) / (1 + i * 0.3),
             rng.uniform(0.4, 1.0) / (1 + i * 0.4)) for i in range(count)]


# ---------------------------------------------------------------- small speakers
#
# A phone's speaker carries next to nothing below 300 Hz. So a sound is built
# from layers given a share of its energy (mixe) and, where it helps, has its
# three bands (under 300 Hz, 300 Hz to 1 kHz, above) re-weighted (tilt): the
# low body stays for those who can hear it, and the slap, crack, rattle and
# cloth that say what the sound is sit where every speaker can play them.

def fit(x, n):
    return x[:n] if len(x) >= n else np.pad(x, (0, n - len(x)))


def mixe(dur, *layers, peak=1.0):
    """Mixes layers by their share of the sound's energy: (signal, share) or
    (signal, share, at seconds). Peak 1 unless peak is None (unit energy)."""
    n = int(round(dur * SR))
    out = np.zeros(n)
    for layer in layers:
        s = place(np.zeros(n), layer[0], layer[2] if len(layer) > 2 else 0.0)[:n]
        e = np.sum(s ** 2)
        if e > 1e-12:
            out += s * np.sqrt(layer[1] / e)
    if peak:
        out *= peak / (np.max(np.abs(out)) + 1e-12)
    return out


def nz(rng, dur, lo, hi, order=2):
    """A band of noise (lo None: everything under hi)."""
    w = white(rng, dur)
    return bandpass(w, lo, hi, order) if lo else lowpass(w, hi, order)


def ragged(rng, dur, rate=14.0, power=1.0):
    """An uneven swell in [0, 1]: cloth, leaves, falling spray, a crowd."""
    am = np.abs(lowpass(white(rng, dur), rate, 1)) ** power
    return am / (am.max() + 1e-12)


def thump(rng, f0, dur, drop=0.55, tau=None, attack=0.003):
    """A low body: a falling sine from a random phase, so no two are alike."""
    t = t_axis(dur)
    f = f0 * drop ** (t / dur)
    return np.sin(2 * np.pi * np.cumsum(f) / SR + rng.uniform(0, 6.283)) * env_ad(dur, attack, tau or dur / 4)


METAL = [1.0, 2.32, 3.73, 4.9, 6.1, 7.7, 9.3, 11.2, 13.6, 15.9]


def ring(rng, base, dur, count=8, tau=(0.1, 0.4), spread_db=2.0, slope=0.4):
    """Struck metal as partials of nearly even energy, so none sings alone."""
    t = t_axis(dur)
    out = np.zeros(len(t))
    for i in range(count):
        f = base * METAL[i] * (1 + rng.uniform(-0.03, 0.03))
        tk = rng.uniform(*tau) / (1 + 0.12 * i)
        e = 10 ** (rng.uniform(-spread_db, spread_db) / 10) / (1 + i) ** slope
        if f < 12000:
            out += np.sqrt(e / tk) * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.283)) * np.exp(-t / tk)
    return out * np.clip(t / 0.0004, 0, 1)


def creak(rng, dur, f1, f2, rate=(45, 75)):
    """Friction: a jittered pulse train through two resonances (timber, leather, an axle)."""
    n = int(round(dur * SR))
    pulses = np.zeros(n)
    r = rng.uniform(*rate)
    tt = 0.0
    while tt < dur:
        pulses[min(n - 1, int(tt * SR))] = rng.uniform(0.5, 1)
        tt += 1 / (r * rng.uniform(0.8, 1.25))
    x = resonator(resonator(pulses, f1, 8), f2, 6)
    return x * env_points(dur, [(0, 0), (dur * 0.15, 1), (dur * 0.85, 0.7), (dur, 0)])


def scatter(rng, dur, n, make, t0=0.0, t1=None):
    """n events made by make(rng) at random times in [t0, t1]."""
    x = silence(dur)
    for at in np.sort(rng.uniform(t0, dur * 0.8 if t1 is None else t1, n)):
        x = place(x, make(rng), at)
    return x[:int(round(dur * SR))]


def knock(r, lo, hi, d=0.15, decay=(0.02, 0.06)):
    """A stone or a billet of wood struck."""
    return modal(impulse(d, 0.001, r), wood_modes(r, r.uniform(lo, hi), 3, decay))


def droplet(r):
    return sine_sweep(r.uniform(900, 2500), r.uniform(2000, 4500), 0.02) * env_ad(0.02, 0.001, 0.005) * r.uniform(0.1, 0.5)


def blub(r, lo=200, hi=700):
    d = r.uniform(0.02, 0.05)
    f0 = r.uniform(lo, hi)
    return sine_sweep(f0, f0 * r.uniform(1.5, 2.5), d * 2) * env_ad(d * 2, 0.002, d / 2) * r.uniform(0.3, 0.8)


def clicks(rng, dur, rate, lo, hi, d=(0.001, 0.004), gain=(0.1, 0.6), curve=None):
    return grains(rng, dur, rate, lambda r: click_grain(r, lo, hi, d, gain), density_curve=curve)


def wrap(x, n):
    """Folds whatever runs past n samples back onto the start (a loop's tail)."""
    out = x[:n].copy()
    k = n
    while k < len(x):
        seg = x[k:k + n]
        out[:len(seg)] += seg
        k += n
    return out


def tilt_loop(x, low, mid, high):
    """tilt for a loop: done on three laps, the middle one kept, so the seam stays whole."""
    n = len(x)
    return tilt(np.concatenate([x, x, x]), low, mid, high)[n:2 * n]


# ---------------------------------------------------------------- weapons

@recipe("snd_weap_arrow_bowstring_release", space=(0.4, 0.12))
def bowstring(rng, v):
    pitch = 1 + rng.uniform(-0.05, 0.05)
    dur = 0.42
    # The string: a short pluck, low and quickly damped by the archer's fingers.
    f0 = 118 * pitch * (1 + 0.04 * v)
    string = karplus(rng, f0, dur, damping=0.985, bright=0.35) * env_ad(dur, 0.001, 0.09)
    # Its second, slapping harmonic as the string hits the bracer.
    slap = karplus(rng, f0 * 2.01, dur, damping=0.97, bright=0.7) * env_ad(dur, 0.0005, 0.03) * 0.5
    # The snap: a bright click and a puff of noise.
    snap = bandpass(white(rng, dur), 2200, 7000, 2) * env_ad(dur, 0.0003, 0.006) * 1.4
    # The limbs' thump.
    body = sine_sweep(95 * pitch, 60, dur) * env_ad(dur, 0.002, 0.03) * 0.6
    x = string + slap + snap + body
    return trim_tail(x, -55)


@recipe("snd_weap_arrow_whistle", space=(0.6, 0.15))
def whistle(rng, v):
    dur = rng.uniform(0.5, 0.7)
    t = t_axis(dur)
    peak = rng.uniform(0.45, 0.6)
    # Doppler: the pitch falls as it passes.
    f = np.where(t < peak * dur, 3300, 1900) * 1.0
    f = 1900 + 1500 / (1 + np.exp((t - peak * dur) * 40)) * rng.uniform(0.85, 1.15)
    amp = np.exp(-((t - peak * dur) / (dur * 0.18)) ** 2)
    hiss = svf_bandpass(white(rng, dur), f, q=3.5) * amp
    # The fletching's flutter.
    flutter = 1 + 0.35 * np.sin(2 * np.pi * rng.uniform(35, 55) * t)
    tone = osc(f * 0.62, "sine") * amp * 0.12
    return fade((hiss * flutter + tone), 0.01, 0.05)


@recipe("snd_weap_sword_swing", space=(0.3, 0.1))
def swing(rng, v):
    dur = rng.uniform(0.22, 0.32)
    t = t_axis(dur)
    mid = rng.uniform(0.45, 0.6)
    f = 700 + 2200 * np.exp(-((t / dur - mid) / 0.22) ** 2)
    amp = np.exp(-((t / dur - mid) / 0.25) ** 2)
    x = svf_bandpass(white(rng, dur), f, q=1.6) * amp
    return fade(x, 0.005, 0.03)


@recipe("snd_weap_heavy_swing", space=(0.4, 0.12))
def heavy_swing(rng, v):
    dur = rng.uniform(0.38, 0.5)
    t = t_axis(dur)
    mid = rng.uniform(0.5, 0.65)
    f = 260 + 900 * np.exp(-((t / dur - mid) / 0.25) ** 2)
    amp = np.exp(-((t / dur - mid) / 0.28) ** 2)
    x = svf_bandpass(white(rng, dur), f, q=1.2) * amp + lowpass(white(rng, dur), 200) * amp * 0.6
    return fade(x, 0.005, 0.04)


@recipe("snd_weap_sword_flesh", space=(0.35, 0.1))
def sword_flesh(rng, v):
    dur = 0.4
    t = t_axis(dur)
    # The cut: a tearing band of noise, rough with fast flutter.
    tear = bandpass(white(rng, dur), 900, 4200, 2) * env_points(dur, [(0, 0), (0.004, 1), (0.05, 0.5), (0.16, 0)])
    tear *= 1 + 0.6 * np.sign(np.sin(2 * np.pi * rng.uniform(60, 110) * t)) * rng.uniform(0.4, 1)
    # The wet: squelch and bubbles.
    squelch = lowpass(white(rng, dur), 900, 2) * env_points(dur, [(0, 0), (0.01, 0.8), (0.12, 0.2), (0.3, 0)])
    bits = wet_bits(rng, 0.18, 140)[:len(t)] * 0.7
    body = thud(rng, 130, dur, drop=0.6) * 0.5
    x = tear * 0.9 + squelch * 0.7 + np.pad(bits, (0, max(0, len(t) - len(bits))))[:len(t)] + body
    return trim_tail(saturate(x * 0.8, 1.5), -50)


@recipe("snd_weap_sword_armor", space=(0.8, 0.2))
def sword_armor(rng, v):
    dur = 1.3
    base = rng.uniform(780, 1350)
    # Two bodies ring, the blade and the plate it lands on, their partials kept even.
    blade = ring(rng, base, dur, 8, (0.25, 0.7))
    plate = ring(rng, base * rng.uniform(1.25, 1.6), dur, 7, (0.08, 0.3))
    bounce = ring(rng, base * rng.uniform(0.9, 1.1), dur, 6, (0.05, 0.2))
    scrape = highpass(white(rng, dur), 3500) * env_ad(dur, 0.0005, 0.025)
    clank = thud(rng, 220, dur, drop=0.8, noise_lp=1500) * env_ad(dur, 0.0005, 0.03)
    x = mixe(dur, (blade, 0.4), (plate, 0.27), (bounce, 0.08, rng.uniform(0.008, 0.02)), (scrape, 0.15), (clank, 0.1))
    return trim_tail(x, -55)


@recipe("snd_weap_blunt_crush", space=(0.5, 0.15))
def blunt(rng, v):
    dur = 0.55
    n = int(dur * SR)
    f0 = [68, 92, 56, 80][v % 4] * rng.uniform(0.95, 1.05)
    low = thump(rng, f0, dur, rng.uniform(0.45, 0.6), dur / rng.uniform(3.5, 5)) + nz(rng, dur, None, 250) * env_ad(dur, 0.002, 0.05) * 0.4
    # What a small speaker hears: the smack, bone giving, a crack.
    smack = nz(rng, dur, 300, 1200) * env_ad(dur, 0.001, rng.uniform(0.02, 0.04))
    crunch = fit(clicks(rng, rng.uniform(0.1, 0.18), 400, 350, 1500, (0.002, 0.008), (0.3, 1.0), lambda u: 1 - u), n)
    crack = fit(clicks(rng, rng.uniform(0.08, 0.16), 400, 1500, 5500, (0.001, 0.004), (0.3, 1.0), lambda u: 1 - u), n)
    snap = nz(rng, dur, 1200, 6000) * env_ad(dur, 0.0005, rng.uniform(0.02, 0.04))
    x = mixe(dur, (low, 0.42), (smack, 0.18), (crunch, 0.12), (crack, 0.13), (snap, 0.15))
    return trim_tail(tilt(saturate(x, 1.5), 0.30, 0.32, 0.38), -50)


@recipe("snd_weap_pierce_flesh", space=(0.35, 0.1))
def pierce(rng, v):
    dur = 0.35
    t = t_axis(dur)
    f = 500 + 1600 * np.exp(-t / 0.06)
    push = svf_bandpass(white(rng, dur), f, q=1.5) * env_points(dur, [(0, 0), (0.008, 1), (0.08, 0.3), (0.2, 0)])
    bits = wet_bits(rng, 0.12, 120, 300, 1100)
    bits = np.pad(bits, (0, max(0, len(t) - len(bits))))[:len(t)]
    body = thud(rng, 150, dur, drop=0.6) * 0.5
    return trim_tail(push + bits * 0.8 + body, -50)


@recipe("snd_weap_block_shield", space=(0.45, 0.15))
def block_shield(rng, v):
    dur = 0.45
    n = int(dur * SR)
    base = [185, 240, 160, 212][v % 4] * rng.uniform(0.96, 1.04)
    wood = modal(impulse(dur, 0.002, rng), wood_modes(rng, base, 5, (0.05, 0.18)))
    boards = modal(impulse(dur, 0.001, rng), wood_modes(rng, base * rng.uniform(2.6, 3.4), 4, (0.03, 0.09)))
    body = thump(rng, rng.uniform(90, 130), dur, 0.7, 0.04)
    # The blade on the rim and the iron boss; the straps and the arm behind.
    crack = nz(rng, dur, 1500, 7000) * env_ad(dur, 0.0003, rng.uniform(0.006, 0.015))
    boss = ring(rng, rng.uniform(1400, 2400), dur, 5, (0.03, 0.09))
    straps = fit(clicks(rng, 0.15, 80, 1200, 5000, curve=lambda u: 1 - u), n)
    x = mixe(dur, (wood, 0.36), (boards, 0.16), (body, 0.12), (crack, 0.13), (boss, 0.14), (straps, 0.09))
    return trim_tail(tilt(x, 0.38, 0.30, 0.32), -50)


@recipe("snd_weap_hit_bone", space=(0.4, 0.12))
def hit_bone(rng, v):
    dur = 0.4
    crack = grains(rng, 0.03, 300, lambda r: click_grain(r, 1500, 5000, (0.001, 0.004)))
    rattle = grains(rng, 0.25, 80, lambda r: click_grain(r, 700, 2600, (0.002, 0.01), (0.2, 0.6)), density_curve=lambda u: 1 - u)
    knock = modal(impulse(dur, 0.001, rng), wood_modes(rng, rng.uniform(500, 800), 4, (0.02, 0.07)))
    x = silence(dur)
    x = place(x, crack, 0)
    x = place(x, rattle, 0.015, 0.8)
    x = place(x, knock * 2, 0)
    return trim_tail(x, -50)


@recipe("snd_weap_satchel_throw", space=(0.4, 0.12))
def satchel_throw(rng, v):
    dur = 0.7
    t = t_axis(dur)
    f = 350 + 600 * np.exp(-((t / dur - 0.35) / 0.2) ** 2)
    whoosh = svf_bandpass(white(rng, dur), f, q=1.0) * np.exp(-((t / dur - 0.35) / 0.25) ** 2)
    def blub(r):
        d = r.uniform(0.02, 0.05)
        f0 = r.uniform(250, 600)
        return sine_sweep(f0, f0 * r.uniform(1.5, 2.5), d * 2) * env_ad(d * 2, 0.002, d / 2) * r.uniform(0.3, 0.8)
    slosh = grains(rng, 0.4, 30, blub)
    gravel = grains(rng, 0.3, 200, lambda r: click_grain(r, 1200, 5000, (0.001, 0.003), (0.05, 0.2)))
    x = whoosh * 0.8
    x = place(x, slosh, 0.1)
    x = place(x, gravel, 0.05)
    return trim_tail(x, -50)


@recipe("snd_weap_satchel_fuse", space=(0.2, 0.08))
def fuse(rng, v):
    dur = rng.uniform(1.3, 1.7)
    t = t_axis(dur)
    hiss = highpass(white(rng, dur), 3000) * (0.6 + 0.4 * np.abs(lowpass(white(rng, dur), 12)) * 3)
    pops = grains(rng, dur, 25, lambda r: click_grain(r, 800, 6000, (0.0008, 0.003), (0.3, 1.0)))[:len(t)]
    x = hiss * 0.4 + np.pad(pops, (0, len(t) - len(pops)))
    return fade(x * env_points(dur, [(0, 0), (0.05, 1), (dur - 0.3, 0.9), (dur, 0)]), 0.01, 0.1)


def fire_roar(rng, dur, cutoff=900, crackle=40):
    t = t_axis(dur)
    roar = lowpass(white(rng, dur), cutoff, 2) * (0.7 + 0.3 * np.sin(2 * np.pi * rng.uniform(3, 6) * t + rng.uniform(0, 6)))
    roar += lowpass(brown(rng, dur), 200) * 0.6
    pops = grains(rng, dur, crackle, lambda r: click_grain(r, 600, 5000, (0.0008, 0.004), (0.2, 1.0)))[:len(t)]
    return roar + np.pad(pops, (0, len(t) - len(pops))) * 0.8


@recipe("snd_weap_fireball_cast", space=(0.7, 0.2))
def fireball_cast(rng, v):
    dur = 1.2
    t = t_axis(dur)
    build = grains(rng, 0.7, 120, lambda r: click_grain(r, 1500, 7000, (0.0008, 0.003)), density_curve=lambda u: u)
    swell = svf_bandpass(white(rng, dur), 300 + 2500 * (t / dur) ** 2, q=0.8) * env_points(dur, [(0, 0), (0.7, 0.8), (0.85, 1.0), (1.2, 0)])
    roar = fire_roar(rng, dur, 1200, 30) * env_points(dur, [(0, 0), (0.6, 0.3), (0.8, 1), (1.2, 0)])
    x = swell + roar * 0.7
    x = place(x, build, 0)
    return trim_tail(x, -50)


@recipe("snd_weap_catapult_release", space=(1.2, 0.25))
def catapult(rng, v):
    dur = 1.4
    # The creak of the frame as the arm goes, then the arm into the crossbar.
    cd = 0.35
    groan = creak(rng, cd, rng.uniform(500, 800), rng.uniform(1200, 1800))
    base = [86, 108, 72][v % 3] * rng.uniform(0.95, 1.05)
    slam = modal(impulse(1.0, 0.004, rng), wood_modes(rng, base, 6, (0.1, 0.4)))
    boom = thump(rng, base * 0.75, 1.0, 0.6, rng.uniform(0.18, 0.3))
    twang = karplus(rng, rng.uniform(60, 80), 0.8, 0.995, 0.3) * env_ad(0.8, 0.001, 0.25)
    # The arm through the air, the sling's snap, timber cracking and the frame shaking after.
    wd = 0.24
    t = t_axis(wd)
    whip = svf_bandpass(white(rng, wd), 500 + 2200 * (t / wd) ** 2, q=1.2) * (t / wd) ** 2
    sling = nz(rng, 0.15, 1500, 7000) * env_ad(0.15, 0.0005, rng.uniform(0.01, 0.02))
    split = clicks(rng, rng.uniform(0.04, 0.08), 500, 800, 5000, (0.001, 0.005), (0.3, 1.0))
    frame = grains(rng, 0.7, 40, lambda r: knock(r, 400, 1200, 0.12) * r.uniform(0.3, 1), density_curve=lambda u: (1 - u) ** 1.5)
    x = mixe(dur, (groan, 0.13), (whip, 0.07, cd - wd), (slam, 0.28, cd), (boom, 0.16, cd), (twang, 0.06, cd),
             (split, 0.08, cd), (frame, 0.12, cd + 0.02), (sling, 0.10, cd + 0.015))
    return trim_tail(tilt(x, 0.42, 0.28, 0.30), -55)


@recipe("snd_weap_spear_scream", space=(0.8, 0.2))
def spear_scream(rng, v):
    dur = 1.0
    t = t_axis(dur)
    scrape = modal(highpass(white(rng, 0.25), 2000) * env_ad(0.25, 0.01, 0.08), metal_modes(rng, 1800, 6, (0.1, 0.4)))
    f = 4000 * np.exp(-t / 0.5) + 900
    scream = svf_bandpass(white(rng, dur), f, q=6) * env_points(dur, [(0, 0), (0.2, 1), (0.7, 0.6), (1.0, 0)])
    x = place(scream, scrape * 2, 0)
    return trim_tail(x, -50)


@recipe("snd_weap_lightning", space=(2.0, 0.3))
def lightning(rng, v):
    dur = 2.2
    t = t_axis(dur)
    gate = (lowpass(white(rng, dur), 40) > 0).astype(float)
    crackle = highpass(white(rng, dur), 2500) * gate * env_points(dur, [(0, 0), (0.6, 0.7), (0.7, 0), (dur, 0)])
    snap = np.zeros(len(t))
    i = int(0.7 * SR)
    snap[i:i + int(0.01 * SR)] = rng.standard_normal(int(0.01 * SR)) * 3
    boom = lowpass(brown(rng, dur), 300) * env_points(dur, [(0, 0), (0.7, 0), (0.72, 1.5), (dur, 0)])
    return trim_tail(crackle * 0.6 + snap + boom, -55)


# ---------------------------------------------------------------- impacts

@recipe("snd_impact_arrow_dirt", space=(0.3, 0.1))
def arrow_dirt(rng, v):
    dur = 0.25
    body = lowpass(white(rng, dur), rng.uniform(300, 500), 2) * env_ad(dur, 0.001, 0.03) * 1.5
    tick = bandpass(white(rng, dur), 1500, 4000) * env_ad(dur, 0.0003, 0.004) * 0.6
    soil = grains(rng, 0.1, 150, lambda r: click_grain(r, 1000, 4000, (0.001, 0.004), (0.05, 0.2)))
    x = place(body + tick, soil, 0.005)
    return trim_tail(x, -50)


@recipe("snd_impact_arrow_wood", space=(0.4, 0.12))
def arrow_wood(rng, v):
    dur = 0.5
    t = t_axis(dur)
    base = [640, 860, 740, 980][v % 4] * rng.uniform(0.95, 1.05)
    thock = modal(impulse(dur, 0.001, rng), wood_modes(rng, base, 5, (0.02, 0.08)))
    # The shaft quivering in the wood: a low hum, and its buzz against the hole
    # (which is what a small speaker has of it).
    fq, ph = rng.uniform(85, 140), rng.uniform(0, 6.283)
    quiver = np.sin(2 * np.pi * fq * t + ph) * env_ad(dur, 0.004, rng.uniform(0.08, 0.13)) * (1 + 0.5 * np.sin(2 * np.pi * 22 * t))
    buzz = nz(rng, dur, 1200, 5000) * np.abs(np.sin(2 * np.pi * fq * t + ph)) ** 4 * np.exp(-t / rng.uniform(0.06, 0.1))
    tick = nz(rng, dur, 2000, 8000) * env_ad(dur, 0.0002, 0.004)
    split = clicks(rng, 0.05, 400, 900, 4000, (0.001, 0.004), (0.3, 1.0))
    x = mixe(dur, (thock, 0.44), (quiver, 0.15), (buzz, 0.22), (tick, 0.1), (split, 0.09))
    return trim_tail(x, -50)


@recipe("snd_impact_arrow_stone", space=(0.4, 0.12))
def arrow_stone(rng, v):
    x = silence(0.3)
    x = place(x, click_grain(rng, 3000, 6000, (0.002, 0.005), (0.9, 1.0)) * 1.5, 0)
    for k in range(rng.integers(1, 4)):
        x = place(x, click_grain(rng, 2000, 5000, (0.001, 0.004), (0.2, 0.5)), 0.03 + k * rng.uniform(0.03, 0.07))
    x = place(x, bandpass(white(rng, 0.05), 1500, 6000) * env_ad(0.05, 0.0003, 0.006), 0)
    return trim_tail(x, -50)


@recipe("snd_impact_arrow_shield", space=(0.45, 0.15))
def arrow_shield(rng, v):
    dur = 0.45
    knock = modal(impulse(dur, 0.0015, rng), wood_modes(rng, rng.uniform(300, 450), 5, (0.03, 0.12))) * 3
    splinter = grains(rng, 0.07, 700, lambda r: click_grain(r, 1500, 7000, (0.0005, 0.002)))
    x = place(knock, splinter, 0.002, 0.8)
    return trim_tail(place(x, thud(rng, 120, dur, 0.7) * 0.4, 0), -50)


@recipe("snd_impact_arrow_armor", space=(0.6, 0.18))
def arrow_armor(rng, v):
    dur = 0.7
    base = rng.uniform(1200, 1900)
    # The plate and the one beside it ring together, partials kept even; the shaft skitters off.
    plate = ring(rng, base, dur, 7, (0.08, 0.3))
    other = ring(rng, base * rng.uniform(1.3, 1.7), dur, 6, (0.04, 0.15))
    tick = highpass(white(rng, dur), 3000) * env_ad(dur, 0.0002, 0.004)
    glance = nz(rng, dur, 1500, 6000) * env_ad(dur, 0.001, rng.uniform(0.012, 0.03))
    return trim_tail(mixe(dur, (plate, 0.4), (other, 0.3), (tick, 0.15), (glance, 0.15)), -55)


@recipe("snd_impact_arrow_flesh", space=(0.3, 0.1))
def arrow_flesh(rng, v):
    dur = 0.3
    t = t_axis(dur)
    f0 = [150, 205, 125, 180, 235][v % 5] * rng.uniform(0.95, 1.05)
    thwack = thump(rng, f0, dur, 0.6, dur / rng.uniform(4, 6))
    wet = nz(rng, dur, 350, 1300) * env_ad(dur, 0.002, rng.uniform(0.025, 0.05))
    slap = nz(rng, dur, 1000, 5000) * env_ad(dur, 0.0003, rng.uniform(0.006, 0.014))
    bits = wet_bits(rng, 0.1, 140, 500, 3000)
    fletch = nz(rng, dur, 2000, 6000) * np.abs(np.sin(2 * np.pi * rng.uniform(60, 110) * t)) * np.exp(-t / 0.04)
    x = mixe(dur, (thwack, 0.34), (wet, 0.27), (slap, 0.2), (bits, 0.11, 0.005), (fletch, 0.08))
    return trim_tail(saturate(x, 1.4), -50)


@recipe("snd_impact_arrow_water", space=(0.4, 0.1))
def arrow_water(rng, v):
    dur = 0.45
    plop = sine_sweep(rng.uniform(500, 800), rng.uniform(1400, 2200), 0.05) * env_ad(0.05, 0.001, 0.015)
    splash = bandpass(white(rng, dur), 1000, 6000) * env_ad(dur, 0.002, 0.05) * 0.6
    drops = grains(rng, 0.3, 40, lambda r: sine_sweep(r.uniform(900, 2500), r.uniform(2000, 4000), 0.02) * env_ad(0.02, 0.001, 0.005) * r.uniform(0.1, 0.4))
    x = place(splash, plop, 0)
    x = place(x, drops, 0.03)
    return trim_tail(x, -50)


def splash_body(rng, dur, lo=300, hi=5000):
    """Water thrown up and falling back: a burst of band noise that drops in pitch, and the drops after it."""
    t = t_axis(dur)
    burst = svf_bandpass(white(rng, dur), hi * np.exp(-t / (dur / 3)) + lo, q=0.6) * env_ad(dur, 0.004, dur / 4)
    drops = grains(rng, dur, 70, lambda r: sine_sweep(r.uniform(700, 2200), r.uniform(1800, 4200), 0.025) * env_ad(0.025, 0.001, 0.006) * r.uniform(0.1, 0.5),
                   density_curve=lambda u: (1 - u) ** 1.5)
    return place(burst, drops, dur * 0.12, 0.7)


@recipe("snd_water_wade", space=(0.4, 0.1))
def water_wade(rng, v):
    dur = 0.5
    slosh = bandpass(white(rng, dur), rng.uniform(350, 600), rng.uniform(2200, 3500)) * env_points(dur, [(0, 0), (0.04, 1), (0.18, 0.45), (dur, 0)])
    gulp = sine_sweep(rng.uniform(260, 380), rng.uniform(500, 800), 0.07) * env_ad(0.07, 0.004, 0.025) * 0.5
    drips = grains(rng, 0.35, 28, lambda r: sine_sweep(r.uniform(900, 2400), r.uniform(2000, 4000), 0.02) * env_ad(0.02, 0.001, 0.005) * r.uniform(0.08, 0.3))
    x = place(slosh * 0.8, gulp, rng.uniform(0.01, 0.05))
    x = place(x, drips, 0.1)
    return trim_tail(x, -50)


@recipe("snd_water_plop", space=(0.4, 0.1))
def water_plop(rng, v):
    dur = 0.5
    plop = sine_sweep(rng.uniform(300, 480), rng.uniform(900, 1500), 0.08) * env_ad(0.08, 0.002, 0.03)
    splash = bandpass(white(rng, dur), 700, 5000) * env_ad(dur, 0.003, 0.06) * 0.5
    drops = grains(rng, 0.35, 30, lambda r: sine_sweep(r.uniform(900, 2500), r.uniform(2000, 4000), 0.02) * env_ad(0.02, 0.001, 0.005) * r.uniform(0.1, 0.4))
    x = place(splash, plop, 0)
    x = place(x, drops, 0.05)
    return trim_tail(x, -50)


@recipe("snd_impact_stone_water", space=(1.4, 0.3))
def stone_water(rng, v):
    dur = 2.4
    boom = thud(rng, rng.uniform(50, 65), 0.9, 0.45, 350) * 1.5
    column = splash_body(rng, dur, 250, 6500) * 1.2
    rain = grains(rng, 1.4, 160, lambda r: click_grain(r, 1500, 7000, (0.001, 0.006), (0.05, 0.4)), density_curve=lambda u: np.sin(np.pi * u) ** 2)
    x = place(column, boom, 0)
    x = place(x, rain, 0.7, 0.7)
    return trim_tail(saturate(x, 1.5), -55)


@recipe("snd_water_steam", space=(0.9, 0.2))
def water_steam(rng, v):
    dur = 2.4
    t = t_axis(dur)
    hiss = highpass(white(rng, dur), rng.uniform(2500, 3500)) * env_points(dur, [(0, 0), (0.03, 1), (0.4, 0.6), (dur, 0)])
    boil = bandpass(white(rng, dur), 300, 1500) * (0.4 + 0.6 * np.abs(lowpass(white(rng, dur), 14)) * 3) * np.exp(-t / 0.5) * 0.6
    spit = grains(rng, 1.2, 45, lambda r: click_grain(r, 800, 6000, (0.001, 0.006), (0.1, 0.7)), density_curve=lambda u: (1 - u) ** 2)
    x = place(hiss * 0.8 + boil, spit, 0.02, 0.6)
    return fade(trim_tail(x, -52), 0.004, 0.2)


@recipe("snd_impact_stone_smash", space=(1.4, 0.3))
def stone_smash(rng, v):
    dur = 1.6
    f0 = [52, 66, 44][v % 3] * rng.uniform(0.95, 1.05)
    boom = thump(rng, f0, 0.8, 0.5, rng.uniform(0.16, 0.24)) + nz(rng, 0.8, None, 400) * env_ad(0.8, 0.002, 0.1) * 0.6
    crunch = nz(rng, dur, 250, 3000) * env_ad(dur, 0.001, rng.uniform(0.09, 0.15))
    crack = nz(rng, 0.3, 800, 7000) * env_ad(0.3, 0.0004, rng.uniform(0.012, 0.025))
    grit = clicks(rng, 1.0, 220, 800, 5000, (0.001, 0.006), (0.05, 0.5), lambda u: (1 - u) ** 2)
    rocks = grains(rng, 0.9, 20 + 4 * v, lambda r: knock(r, 300, 900) * r.uniform(0.4, 1), density_curve=lambda u: 1 - u)
    x = mixe(dur, (boom, 0.44), (crunch, 0.2), (crack, 0.1), (grit, 0.12, 0.02), (rocks, 0.14, 0.05))
    return trim_tail(tilt(saturate(x, 1.4), 0.42, 0.28, 0.30), -55)


@recipe("snd_impact_gate_wood", space=(1.3, 0.3))
def gate_wood(rng, v):
    dur = 1.2
    base = [58, 76, 49, 67][v % 4] * rng.uniform(0.96, 1.04)
    boom = modal(impulse(dur, 0.004, rng), wood_modes(rng, base, 6, (0.2, 0.6)))
    body = thump(rng, base * rng.uniform(1.0, 1.3), dur, 0.7, rng.uniform(0.06, 0.1))
    # The planks themselves, a crack along one, and the iron on it: bands, bar and hinges.
    timber = modal(impulse(dur, 0.002, rng), wood_modes(rng, rng.uniform(280, 460), 5, (0.04, 0.14)))
    crack = clicks(rng, rng.uniform(0.05, 0.1), 450, 700, 5000, (0.001, 0.005), (0.3, 1.0))
    slap = nz(rng, dur, 800, 4000) * env_ad(dur, 0.0005, 0.02)
    iron = scatter(rng, dur, 3 + v % 3, lambda r: ring(r, r.uniform(900, 2200), 0.4, 5, (0.03, 0.15)) * r.uniform(0.4, 1), 0.0, 0.25)
    rattle = clicks(rng, 0.5, 45, 1200, 5000, (0.002, 0.008), (0.1, 0.4), lambda u: 1 - u)
    x = mixe(dur, (boom, 0.3), (body, 0.15), (timber, 0.25), (crack, 0.08), (iron, 0.1), (rattle, 0.06, 0.03), (slap, 0.06))
    return trim_tail(tilt(x, 0.45, 0.30, 0.25), -55)


@recipe("snd_impact_gate_break", space=(1.8, 0.3))
def gate_break(rng, v):
    dur = 2.4
    x = silence(dur)
    for k in range(4):
        at = k * rng.uniform(0.06, 0.15)
        crack = grains(rng, 0.12, 500, lambda r: click_grain(r, 600, 6000, (0.001, 0.005)))
        x = place(x, crack, at, 1 - k * 0.15)
        x = place(x, modal(impulse(0.6, 0.003, rng), wood_modes(rng, rng.uniform(70, 160), 5, (0.1, 0.3))) * 3, at)
    crash = thud(rng, 50, 1.2, 0.5, 600) * 1.5
    x = place(x, crash, 0.45)
    debris = grains(rng, 1.4, 30, lambda r: modal(impulse(0.2, 0.002, r), wood_modes(r, r.uniform(200, 600), 3, (0.03, 0.1))) * 1.2,
                    density_curve=lambda u: (1 - u))
    x = place(x, debris, 0.5)
    return trim_tail(saturate(x, 1.5), -55)


# ---------------------------------------------------------------- explosions

@recipe("snd_expl_satchel", space=(2.5, 0.35))
def satchel_blast(rng, v):
    dur = 3.2
    t = t_axis(dur)
    sub = sine_sweep(rng.uniform(70, 90) * [1.0, 0.85, 1.15, 0.93][v % 4], 28, 1.2) * env_ad(1.2, 0.003, 0.35)
    rumble = lowpass(brown(rng, dur), 150) * env_ad(dur, 0.05, 0.9)
    crack = white(rng, dur) * env_ad(dur, 0.0002, 0.006)
    cutoff = 300 + 6000 * np.exp(-t / 0.25)
    body = svf_bandpass(white(rng, dur), cutoff * 0.5, q=0.5) * env_ad(dur, 0.002, 0.45)
    # The air torn, then what it threw: clods and splinters landing, dirt hissing down.
    tear = nz(rng, dur, 900, 6000) * env_ad(dur, 0.002, rng.uniform(0.1, 0.16)) * (0.4 + 0.6 * ragged(rng, dur, 40))
    debris = clicks(rng, 2.0, 60, 400, 4000, (0.002, 0.01), (0.05, 0.4), lambda u: (1 - u) ** 1.5)
    clods = grains(rng, 1.6, 14, lambda r: knock(r, 250, 900) * r.uniform(0.3, 1), density_curve=lambda u: np.sin(np.pi * u))
    dirt = nz(rng, 2.2, 1500, 7000) * env_points(2.2, [(0, 0), (0.3, 1), (2.2, 0)]) * ragged(rng, 2.2, 10)
    x = mixe(dur, (sub, 0.26), (rumble, 0.14), (crack, 0.06), (body, 0.24), (tear, 0.1), (debris, 0.07, 0.15),
             (clods, 0.06, 0.3), (dirt, 0.07, 0.35))
    return trim_tail(tilt(saturate(x * 1.3, 2.0), 0.46, 0.27, 0.27), -60)


@recipe("snd_expl_fireball", space=(1.8, 0.3))
def fireball_blast(rng, v):
    dur = 2.6
    t = t_axis(dur)
    # Glass: many short, high, resonant clicks, dense at first.
    glass = grains(rng, 0.45, 900, lambda r: click_grain(r, 2500, 11000, (0.003, 0.03), (0.2, 1.0)), density_curve=lambda u: (1 - u) ** 3)
    tinkle = grains(rng, 0.9, 30, lambda r: click_grain(r, 3000, 9000, (0.01, 0.05), (0.05, 0.3)), density_curve=lambda u: 1 - u)
    # The fuel catching: a whoosh into a roaring, crackling fire.
    whoosh = svf_bandpass(white(rng, dur), 200 + 1800 * np.exp(-((t - 0.25) / 0.2) ** 2), q=0.7) * env_points(dur, [(0, 0), (0.1, 0.6), (0.3, 1.0), (0.8, 0.4), (dur, 0)])
    roar = fire_roar(rng, dur, 1100, 50) * env_points(dur, [(0, 0), (0.15, 0.4), (0.4, 1.0), (1.5, 0.6), (dur, 0)])
    thump = thud(rng, 80, 0.6, 0.5) * 0.8
    x = whoosh + roar * 0.8
    x = place(x, glass, 0, 0.9)
    x = place(x, tinkle, 0.2)
    x = place(x, thump, 0)
    return trim_tail(saturate(x, 1.5), -55)


# ---------------------------------------------------------------- gore and deaths

@recipe("snd_gore_dismember_snap", space=(0.45, 0.12))
def dismember(rng, v):
    dur = 0.5
    crack = silence(0.1)
    for k in range(rng.integers(2, 4)):
        crack = place(crack, click_grain(rng, 900, 4000, (0.001, 0.004), (0.7, 1.0)) * 1.5, k * rng.uniform(0.006, 0.015))
    snap = modal(impulse(dur, 0.0015, rng), wood_modes(rng, rng.uniform(280, 420), 5, (0.02, 0.07))) * 2.5
    t = t_axis(dur)
    tear = bandpass(white(rng, dur), 600, 2600) * env_points(dur, [(0, 0), (0.01, 1), (0.1, 0.4), (0.25, 0)])
    tear *= 1 + 0.7 * np.sign(np.sin(2 * np.pi * rng.uniform(40, 80) * t))
    bits = wet_bits(rng, 0.2, 90)
    x = place(snap + tear * 0.6, crack, 0)
    x = place(x, bits, 0.02, 0.7)
    return trim_tail(saturate(x, 1.6), -50)


@recipe("snd_gore_gib_burst", space=(0.8, 0.2))
def gib(rng, v):
    dur = 0.9
    t = t_axis(dur)
    burst = lowpass(white(rng, dur), rng.uniform(700, 1000), 3) * env_ad(dur, 0.002, 0.12) * 2
    low = sine_sweep(rng.uniform(55, 70), 32, dur) * env_ad(dur, 0.002, 0.15) * 1.4
    squelch = svf_bandpass(white(rng, dur), 250 + 500 * np.exp(-t / 0.1), q=2) * env_ad(dur, 0.004, 0.18) * 1.2
    bits = wet_bits(rng, 0.4, 160, 150, 900)
    x = place(burst + low + squelch, bits, 0.01, 0.8)
    return trim_tail(saturate(x, 2.2), -50)


@recipe("snd_gore_blood_rain", space=(0.5, 0.12))
def blood_rain(rng, v):
    dur = 1.4
    def drop(r):
        if r.uniform() < 0.15:  # a bit of something heavier
            return thud(r, r.uniform(150, 260), 0.08, 0.7, 900) * r.uniform(0.2, 0.5)
        d = r.uniform(0.003, 0.01)
        t = t_axis(d * 4)
        return bandpass(r.standard_normal(len(t)), 700, 3500) * np.exp(-t / d) * r.uniform(0.1, 0.6)
    x = grains(rng, dur, 70, drop, density_curve=lambda u: np.exp(-u * 3))
    return trim_tail(x, -50)


@recipe("snd_gore_body_fall", space=(0.5, 0.15))
def body_fall(rng, v):
    dur = 0.75
    f0 = [78, 104, 64, 92, 118][v % 5] * rng.uniform(0.94, 1.06)
    # Knees, hip, shoulder: each variant lands in its own order and weight.
    hits = [0.0] + sorted(rng.uniform(0.07, 0.3, size=1 + v % 3))
    low, mid, slap = silence(dur), silence(dur), silence(dur)
    for k, at in enumerate(hits):
        g = 1.0 if k == 0 else rng.uniform(0.35, 0.7)
        d = rng.uniform(0.25, 0.42)
        low = place(low, thump(rng, f0 * (1 + 0.35 * k), d, rng.uniform(0.5, 0.7), d / rng.uniform(3, 5)) * g, at)
        mid = place(mid, nz(rng, 0.25, 280, 1100) * env_ad(0.25, 0.003, rng.uniform(0.02, 0.05)) * g, at)
        slap = place(slap, nz(rng, 0.2, 1000, 4500) * env_ad(0.2, 0.001, rng.uniform(0.008, 0.02)) * g, at)
    # Cloth and gear settling: a jerkin, a belt, a scabbard.
    cloth = nz(rng, 0.45, 900, 5000) * ragged(rng, 0.45, 18) * env_points(0.45, [(0, 0.3), (0.05, 1), (0.45, 0)])
    gear = clicks(rng, 0.35, 45, 1500, 6000, (0.001, 0.005), (0.1, 0.6), lambda u: 1 - u)
    x = mixe(dur, (low, 0.42), (mid, 0.26), (slap, 0.13), (cloth, 0.13), (gear, 0.06, 0.02))
    return trim_tail(x, -50)


@recipe("snd_gore_body_fall_armor", space=(0.7, 0.18))
def body_fall_armor(rng, v):
    dur = 0.95
    body = body_fall(rng, v)
    plates = scatter(rng, dur, 3 + v % 4, lambda r: ring(r, r.uniform(700, 2200), 0.5, 6, (0.04, 0.2)) * r.uniform(0.3, 1), 0.0, 0.3)
    mail = clicks(rng, 0.5, 140, 3000, 9000, (0.0008, 0.003), (0.05, 0.4), lambda u: 1 - u)
    return trim_tail(mixe(dur, (body, 0.72), (plates, 0.20), (mail, 0.08)), -50)


@recipe("snd_gore_bones_collapse", space=(0.6, 0.15))
def bones(rng, v):
    x = grains(rng, 0.8, 45, lambda r: modal(impulse(0.15, 0.0008, r), wood_modes(r, r.uniform(500, 1500), 3, (0.01, 0.05))) * 1.5,
               density_curve=lambda u: (1 - u) ** 0.7)
    x = place(x, thud(rng, 120, 0.3, 0.7, 900) * 0.5, 0.05)
    return trim_tail(x, -50)


@recipe("snd_gore_flesh_burn", space=(0.4, 0.12))
def flesh_burn(rng, v):
    dur = 2.2
    t = t_axis(dur)
    sizzle = highpass(white(rng, dur), 2500) * (0.5 + 0.5 * np.abs(lowpass(white(rng, dur), 8)) * 3) * 0.5
    pops = grains(rng, dur, 35, lambda r: click_grain(r, 500, 5000, (0.001, 0.005), (0.2, 1.0)))[:len(t)]
    roar = lowpass(white(rng, dur), 500) * 0.3
    x = (sizzle + np.pad(pops, (0, len(t) - len(pops))) + roar) * env_points(dur, [(0, 0), (0.1, 1), (1.2, 0.7), (dur, 0)])
    return fade(x, 0.01, 0.2)


@recipe("snd_magic_heal", space=(1.5, 0.35))
def heal(rng, v):
    dur = 1.6
    t = t_axis(dur)
    base = rng.uniform(820, 940)
    x = np.zeros(len(t))
    for k, ratio in enumerate([1, 1.26, 1.5, 2.0, 2.52]):
        start = k * 0.07
        f = base * ratio * (1 + 0.01 * np.sin(2 * np.pi * 5 * t))
        x += osc(f, "sine") * env_points(dur, [(0, 0), (start, 0), (start + 0.15, 1), (dur, 0)]) / (1 + k * 0.3)
    shimmer = highpass(white(rng, dur), 6000) * env_points(dur, [(0, 0), (0.3, 0.02), (dur, 0)])
    return fade(x * 0.5 + shimmer, 0.01, 0.2)


# ---------------------------------------------------------------- notifications

def brass(freq, dur, rng, bright=1.0):
    t = t_axis(dur)
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.clip(t / 0.3, 0, 1)
    amp = env_points(dur, [(0, 0), (0.04, 1), (0.12, 0.8), (dur - 0.08, 0.75), (dur, 0)])
    x = np.zeros(len(t))
    for h in range(1, 14):
        # The brighter the louder: higher harmonics follow the envelope's square.
        x += osc(freq * h * vib, "sine") * (amp ** (1 + h * 0.25 / bright)) / h ** 0.9
    return x


@recipe("snd_state_victory_fanfare", space=(2.0, 0.3))
def victory(rng, v):
    # C - G - C - E . G (dotted), over a held chord: a short brass call.
    C4 = 261.63
    notes = [(0.0, 0.22, 1.0), (0.24, 0.12, 1.5), (0.38, 0.22, 2.0), (0.62, 0.22, 2.52), (0.88, 1.6, 3.0)]
    x = silence(3.4)
    for at, d, r in notes:
        x = place(x, brass(C4 * r * 0.5, d, rng), at, 0.8)
        x = place(x, brass(C4 * r, d, rng) * 0.5, at + 0.005)
    for r in (1.0, 1.26, 1.5):
        x = place(x, brass(C4 * r * 0.5, 1.6, rng, 0.6) * 0.35, 0.88)
    x = place(x, thud(rng, 60, 1.0, 0.8) * 0.6, 0.88)
    return trim_tail(x, -60)


@recipe("snd_state_defeat_drone", space=(2.5, 0.35))
def defeat(rng, v):
    dur = 6.5
    t = t_axis(dur)
    amp = env_points(dur, [(0, 0), (1.2, 1), (4.5, 0.8), (dur, 0)])
    x = np.zeros(len(t))
    # A minor, low strings and the octave above them so the chord is there on any speaker.
    for f, g in ((110.0, 1), (130.81, 1), (164.81, 1), (220.0, 1), (261.63, 0.5), (329.63, 0.5), (440.0, 0.35)):
        for d in (-0.15, 0.15):
            vib = 1 + 0.003 * np.sin(2 * np.pi * (4.5 + d) * t + rng.uniform(0, 6))
            x += osc(f * (1 + d / 100) * vib, "saw") * g
    x = lowpass(x, 2600, 2) * amp
    sub = osc(np.full(len(t), 55.0), "sine") * amp
    air = nz(rng, dur, 1200, 5000) * amp ** 2 * (0.5 + 0.5 * ragged(rng, dur, 0.6))
    x = mixe(dur, (x, 0.72), (sub, 0.2), (air, 0.08))
    return fade(tilt(x, 0.44, 0.34, 0.22), 0.05, 0.5)


def horn(freqs, amp, bright=1.0, harmonics=14):
    """A horn whose pitch follows an array; amp in [0, 1] shapes loudness and brightness."""
    x = np.zeros(len(freqs))
    top = float(np.max(freqs))
    for h in range(1, harmonics + 1):
        if top * h < 9000:
            x += osc(freqs * h, "sine") * (amp ** (1 + h * 0.25 / bright)) / h ** 0.9
    return x


def horn_note(freq, dur, bright=1.0, harmonics=30):
    t = t_axis(dur)
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.clip(t / 0.3, 0, 1)
    amp = env_points(dur, [(0, 0), (0.04, 1), (0.12, 0.85), (dur - 0.1, 0.8), (dur, 0)])
    return horn(freq * vib, amp, bright, harmonics)


@recipe("snd_state_war_horn", space=(2.5, 0.35))
def war_horn(rng, v):
    # A long low horn call, answered a fifth higher: the next wave sets out (Horde).
    # The low note is doubled an octave up and given its rasp and breath, so the
    # call is still a horn where the fundamental cannot be heard.
    x = silence(3.6)
    for at, f, d in ((0.0, 98.0, 1.4), (1.5, 147.0, 1.9)):
        note = horn_note(f, d, 1.6, 40) + 0.45 * horn_note(f * 2, d, 1.2, 20)
        breath = nz(rng, d, 900, 4500) * env_points(d, [(0, 0), (0.05, 1), (d - 0.1, 0.6), (d, 0)])
        x = place(x, mixe(d, (note, 0.95), (breath, 0.05)), at)
    x = resonator(x, 520, 1.2) + resonator(x, 1500, 1.5) * 0.7 + x * 0.6
    return trim_tail(tilt(x, 0.42, 0.34, 0.24), -60)


@recipe("snd_state_alarm", space=(1.2, 0.25))
def alarm(rng, v):
    # Three quick rising blasts of a hand horn: the flag is in danger (capture the flag).
    x = silence(1.6)
    for k in range(3):
        x = place(x, brass(233.0 * (1 + 0.125 * k), 0.26, rng, 1.3), k * 0.34, 0.85)
    return trim_tail(x, -60)


# ---------------------------------------------------------------- UI

def tock(rng, base, dur, tau, body=0.25, attack=0.0003):
    """A small dry knock on wood: two sets of partials of even weight and a
    little noise, so it is a knock and not a ping."""
    t = t_axis(dur)
    x = np.zeros(len(t))
    for r in (1.0, 1.47, 2.31, 2.9, 3.89):
        tk = tau * rng.uniform(0.7, 1.2) / r ** 0.5
        x += np.sqrt(rng.uniform(0.6, 1.4) / r ** 0.6 / tk) * np.sin(2 * np.pi * base * r * rng.uniform(0.97, 1.03) * t + rng.uniform(0, 6.283)) * np.exp(-t / tk)
    noise = nz(rng, dur, max(600.0, base * 0.6), min(6000.0, base * 3)) * np.exp(-t / (tau * 0.5))
    return mixe(dur, (x, 1 - body), (noise, body)) * np.clip(t / attack, 0, 1)


@recipe("snd_ui_click", space=(0, 0))
def ui_click(rng, v):
    # A wooden bead on a table: 45 ms, dry.
    dur = 0.045
    return fade(tock(rng, [1150, 1400, 1650][v % 3] * rng.uniform(0.96, 1.04), dur, 0.009), 0.0, 0.015)


@recipe("snd_ui_page_turn", space=(0.2, 0.05))
def page_turn(rng, v):
    dur = 0.45
    t = t_axis(dur)
    swoosh = bandpass(white(rng, dur), 1200, 7000) * env_points(dur, [(0, 0), (0.08, 0.6), (0.25, 1.0), (dur, 0)])
    crinkle = grains(rng, 0.35, 300, lambda r: click_grain(r, 2000, 8000, (0.0005, 0.002), (0.1, 0.5)), density_curve=lambda u: np.sin(np.pi * u))
    return fade(place(swoosh * 0.5, crinkle, 0.03), 0.005, 0.05)


# ---------------------------------------------------------------- ambience (stereo loops)

LOOP = 10.0


def gusts(rng, dur, rate=0.15, depth=0.6):
    m = lowpass(white(rng, dur), rate, 1)
    m = (m - m.min()) / (m.max() - m.min() + 1e-9)
    return 1 - depth + depth * m


@recipe("snd_amb_wind_mountain", space=(0, 0), loop=LOOP)
def wind_mountain(rng, v):
    dur = LOOP + 1.0
    t = t_axis(dur)
    chans = []
    for c in range(2):
        f = 520 + 260 * np.sin(2 * np.pi * rng.uniform(0.05, 0.09) * t + rng.uniform(0, 6)) + 120 * lowpass(white(rng, dur), 0.3, 1) * 30
        howl = svf_bandpass(white(rng, dur), np.clip(f, 250, 1500), q=12)
        whistle = svf_bandpass(white(rng, dur), np.clip(f * 2.3, 600, 4000), q=20) * 0.5
        air = highpass(pink(rng, dur), 400) * 0.012
        chans.append((howl + whistle + air) * gusts(rng, dur, 0.12, 0.7))
    return loopify(np.stack(chans, axis=1), 1.0)


@recipe("snd_amb_wind_valley", space=(0, 0), loop=LOOP)
def wind_valley(rng, v):
    dur = LOOP + 1.0
    t = t_axis(dur)
    chans = []
    for c in range(2):
        g = gusts(rng, dur, 0.2, 0.65)
        sweep = bandpass(pink(rng, dur), 45, 500, 2) * g
        # The gust in the grass and through the reeds, leaves, and grit carried on it.
        grass = bandpass(pink(rng, dur), 350, 2200) * g ** 1.5
        f = 900 + 350 * np.sin(2 * np.pi * rng.uniform(0.06, 0.11) * t + rng.uniform(0, 6))
        reeds = svf_bandpass(white(rng, dur), f, q=3) * g ** 2
        leaves = bandpass(pink(rng, dur), 1500, 7000) * g ** 2 * (0.5 + 0.5 * ragged(rng, dur, 5))
        grit = fit(clicks(rng, dur, 60, 3000, 9000, (0.0005, 0.002), (0.02, 0.12)), len(g)) * g
        chans.append(mixe(dur, (sweep, 0.40), (grass, 0.28), (reeds, 0.08), (leaves, 0.19), (grit, 0.05), peak=None))
    return loopify(tilt(np.stack(chans, axis=1), 0.42, 0.33, 0.25), 1.0)


def cricket(rng, dur):
    f = rng.uniform(3800, 5200)
    rate = rng.uniform(1.2, 2.5)
    pulses = rng.integers(3, 6)
    out = silence(dur)
    t0 = rng.uniform(0, 1 / rate)
    while t0 < dur - 0.3:
        for k in range(pulses):
            d = 0.012
            p = np.sin(2 * np.pi * f * t_axis(d)) * np.hanning(int(round(d * SR)))
            out = place(out, p, t0 + k * 0.03)
        t0 += 1 / rate * rng.uniform(0.9, 1.1)
    return out[:int(dur * SR)]


@recipe("snd_amb_swamp_insects", space=(0, 0), loop=LOOP)
def insects(rng, v):
    dur = LOOP + 1.0
    t = t_axis(dur)
    mix = np.zeros((len(t), 2))
    for k in range(7):
        c = cricket(rng, dur) * rng.uniform(0.2, 0.6)
        c = np.pad(c, (0, len(t) - len(c)))
        mix += pan(c, rng.uniform(-0.9, 0.9))
    # The cicadas' constant shrill buzz, and the hum of gnats.
    for c in range(2):
        buzz = bandpass(white(rng, dur), 5500, 7500) * (0.5 + 0.5 * np.sin(2 * np.pi * rng.uniform(90, 130) * t)) * gusts(rng, dur, 0.3, 0.5)
        hum = osc(np.full(len(t), rng.uniform(380, 460)) * (1 + 0.02 * np.sin(2 * np.pi * 0.7 * t)), "saw")
        mix[:, c] += buzz * 0.12 + lowpass(hum, 1200) * 0.03
    return loopify(mix, 1.0)


@recipe("snd_amb_swamp_frogs", space=(0.6, 0.2))
def frog(rng, v):
    croaks = rng.integers(1, 4)
    x = silence(0.1)
    f0 = rng.uniform(18, 32)
    for k in range(croaks):
        d = rng.uniform(0.18, 0.32)
        t = t_axis(d)
        pulses = osc(np.full(len(t), f0 * rng.uniform(0.95, 1.05)) * (1 + 0.15 * t / d), "saw") > 0.7
        src = pulses.astype(float) * env_points(d, [(0, 0), (0.03, 1), (d - 0.05, 0.8), (d, 0)])
        voice = resonator(src, rng.uniform(380, 520), 6) + resonator(src, rng.uniform(1000, 1300), 8) * 0.5
        x = place(x, voice, k * rng.uniform(0.3, 0.45))
    return trim_tail(x, -50)


def fly(rng, dur):
    t = t_axis(dur)
    walk = np.cumsum(rng.standard_normal(len(t))) / np.sqrt(SR) * 40
    walk = lowpass(walk, 3, 1)
    f = rng.uniform(190, 290) + walk
    buzz = osc(f, "saw") + 0.5 * osc(f * 2.01, "saw")
    buzz = lowpass(buzz, 2600, 2)
    # Darting near and away.
    near = gusts(rng, dur, rng.uniform(0.3, 0.8), 0.9) ** 2
    return buzz * near


@recipe("snd_amb_carrion_flies", space=(0, 0), loop=LOOP)
def flies(rng, v):
    dur = LOOP + 1.0
    t = t_axis(dur)
    mix = np.zeros((len(t), 2))
    for k in range(6):
        p = np.sin(2 * np.pi * rng.uniform(0.05, 0.2) * t + rng.uniform(0, 6)) * 0.8
        f = fly(rng, dur)
        a = (p + 1) * np.pi / 4
        mix[:, 0] += f * np.cos(a)
        mix[:, 1] += f * np.sin(a)
    return loopify(mix * 0.3, 1.0)


def war_hit(rng, f0, dur=0.9, shares=(0.46, 0.30, 0.24)):
    """One stroke on a big drum: the note, the shell and the skin (which is
    what is left of it on a small speaker)."""
    t = t_axis(dur)
    pitch = f0 * (1 + 0.6 * np.exp(-t / 0.03))
    note = np.zeros(len(t))
    for ratio, decay, gain in [(1, 0.35, 1), (1.59, 0.2, 0.4), (2.14, 0.14, 0.25), (2.3, 0.12, 0.2), (2.65, 0.1, 0.12)]:
        note += osc(pitch * ratio, "sine", rng.uniform(0, 1)) * np.exp(-t / (decay * dur / 0.9)) * gain
    note *= np.clip(t / 0.002, 0, 1)
    shell = modal(impulse(dur, 0.002, rng), wood_modes(rng, f0 * rng.uniform(4.5, 6.0), 4, (0.08, 0.2)))
    shell += nz(rng, dur, 300, 1000) * env_ad(dur, 0.001, 0.045) * np.max(np.abs(shell)) * 0.5
    skin = nz(rng, dur, 1000, 5000) * env_ad(dur, 0.0005, rng.uniform(0.022, 0.035)) + nz(rng, dur, 2500, 7000) * env_ad(dur, 0.0002, 0.004)
    return mixe(dur, (note, shares[0]), (shell, shares[1]), (skin, shares[2]))


@recipe("snd_amb_war_drums", space=(0, 0), loop=9.6)
def war_drums(rng, v):
    # 100 beats a minute, four bars of four, three patterns (the first comes back third):
    # BOOM . boom boom | BOOM . . boom (flam) | BOOM . boom boom | BOOM boom . tap-tap, and a pick-up into the loop.
    beat = 0.6
    dur = beat * 16
    lo, hi, tom = 62, 78, 132
    a = [(0, 1.0, lo), (2, 0.6, hi), (3, 0.7, hi)]
    b = [(0, 1.0, lo), (3, 0.6, hi), (3.2, 0.35, hi)]
    c = [(0, 1.0, lo), (1, 0.65, hi), (2.5, 0.45, tom), (3, 0.55, tom), (3.5, 0.5, hi), (3.75, 0.6, hi)]
    x = silence(dur + 2.5)
    for bar, pattern in enumerate((a, b, a, c)):
        for at, g, f in pattern:
            hit = war_hit(rng, f * rng.uniform(0.97, 1.03), 0.9 if f < 100 else 0.6)
            x = place(x, hit * g * rng.uniform(0.85, 1.0), (bar * 4 + at) * beat + rng.uniform(0, 0.012))
    # Far off, in a big space; the reverb's tail wrapped round onto the start so the loop is seamless.
    x = lowpass(x, 4500, 1)
    wet = convolve(x, reverb_ir(rng, 2.0, 0.6, 4500, stereo=True), 0.5)
    return tilt_loop(wrap(wet, int(dur * SR)), 0.46, 0.30, 0.24)


def droplets(rng, dur, rate, chans=2):
    mix = np.zeros((int(dur * SR), chans))
    for c in range(chans):
        g = grains(rng, dur, rate, lambda r: click_grain(r, 1500, 7000, (0.0008, 0.004), (0.05, 0.6)))[:len(mix)]
        mix[:len(g), c] += g
    return mix


@recipe("snd_amb_rain_light", space=(0, 0), loop=LOOP)
def rain_light(rng, v):
    dur = LOOP + 1.0
    bed = np.stack([bandpass(pink(rng, dur), 400, 6000) * 0.12 for _ in range(2)], axis=1)
    return loopify(bed + droplets(rng, dur, 90) * 0.6, 1.0)


@recipe("snd_amb_rain_heavy", space=(0, 0), loop=LOOP)
def rain_heavy(rng, v):
    dur = LOOP + 1.0
    bed = np.stack([lowpass(pink(rng, dur), 7000) * 0.35 + lowpass(brown(rng, dur), 200) * 0.2 for _ in range(2)], axis=1)
    return loopify(bed + droplets(rng, dur, 500) * 0.5, 1.0)


@recipe("snd_amb_thunder", space=(2.5, 0.35))
def thunder(rng, v):
    dur = rng.uniform(5.5, 8.0)
    t = t_axis(dur)
    swells = np.zeros(len(t))
    for k in range(rng.integers(3, 6)):
        c = rng.uniform(0.1, dur * 0.6)
        w = rng.uniform(0.3, 1.2)
        swells += np.exp(-((t - c) / w) ** 2) * rng.uniform(0.4, 1.0)
    swells *= np.exp(-t / (dur * 0.5))
    rumble = lowpass(brown(rng, dur), rng.uniform(120, 220), 2) * swells
    # The roll has a body above the rumble, and the sky tears before it and with each swell.
    roll = nz(rng, dur, 250, 1200) * swells * (0.5 + 0.5 * ragged(rng, dur, 7))
    first = env_ad(dur, 0.003, rng.uniform(0.12, 0.3)) * (1.0 if v % 2 == 0 else 0.45)
    crackle = bandpass(white(rng, dur), 900, 6500) * (first + 0.5 * swells ** 2) * ragged(rng, dur, 30, 2)
    return fade(mixe(dur, (rumble, 0.5), (roll, 0.27), (crackle, 0.23)), 0.01, 0.6)


@recipe("snd_amb_cave_drip", space=(2.0, 0.55))
def cave_drip(rng, v):
    f = rng.uniform(900, 2400)
    d = 0.06
    x = sine_sweep(f, f * rng.uniform(1.4, 2.0), d) * env_ad(d, 0.001, 0.015)
    return place(silence(0.2), x, 0)


@recipe("snd_amb_subterranean_rumble", space=(0, 0), loop=LOOP)
def rumble(rng, v):
    dur = LOOP + 1.0
    chans = [lowpass(brown(rng, dur), 80, 3) * gusts(rng, dur, 0.15, 0.6) for _ in range(2)]
    return loopify(np.stack(chans, axis=1), 1.0)
# ---------------------------------------------------------------- movement
#
# Retriggered about twice a second for each squad on the move, so each is a
# short texture that overlaps itself: a soft start, nothing sharp at 0.

def footfall(r):
    d = 0.14
    return mixe(d, (thump(r, r.uniform(70, 125), d, 0.6, 0.025, 0.006), 0.45),
                (nz(r, d, 300, 2200) * env_ad(d, 0.006, r.uniform(0.015, 0.04)), 0.55)) * r.uniform(0.4, 1)


@recipe("snd_move_foot_march", space=(0.2, 0.08))
def foot_march(rng, v):
    dur = 0.66
    feet = scatter(rng, dur, 6 + v + int(rng.integers(0, 2)), footfall, 0.03, dur - 0.2)
    cloth = nz(rng, dur, 700, 4500) * ragged(rng, dur, 12)
    leather = creak(rng, dur, rng.uniform(700, 1100), rng.uniform(1600, 2400), (30, 60))
    grit = fit(clicks(rng, dur, 60, 1500, 6000, (0.001, 0.003), (0.05, 0.3)), int(dur * SR))
    x = mixe(dur, (feet, 0.62), (cloth, 0.22), (leather, 0.06), (grit, 0.10))
    return fade(tilt(x, 0.30, 0.36, 0.34), 0.04, 0.15)


@recipe("snd_move_foot_armor", space=(0.2, 0.08))
def foot_armor(rng, v):
    dur = 0.66
    feet = scatter(rng, dur, 6 + v + int(rng.integers(0, 2)), footfall, 0.03, dur - 0.2)
    clink = scatter(rng, dur, 5 + v, lambda r: ring(r, r.uniform(1800, 4200), 0.12, 4, (0.01, 0.04)) * r.uniform(0.3, 1), 0.04, dur - 0.2)
    mail = fit(clicks(rng, dur, 130, 3500, 9000, (0.0008, 0.003), (0.05, 0.3)), int(dur * SR)) * (0.4 + 0.6 * ragged(rng, dur, 6))
    cloth = nz(rng, dur, 700, 4500) * ragged(rng, dur, 12)
    leather = creak(rng, dur, rng.uniform(700, 1100), rng.uniform(1600, 2400), (30, 60))
    x = mixe(dur, (feet, 0.47), (clink, 0.22), (mail, 0.13), (cloth, 0.10), (leather, 0.08))
    return fade(tilt(x, 0.25, 0.30, 0.45), 0.04, 0.15)


def squelch(r):
    d = r.uniform(0.1, 0.16)
    t = t_axis(d)
    f0 = r.uniform(180, 420)
    plop = sine_sweep(f0, f0 * r.uniform(1.6, 2.6), d) * env_ad(d, 0.008, d / 3)
    splash = nz(r, d, 700, 5000) * env_ad(d, 0.008, r.uniform(0.02, 0.05))
    suck = svf_bandpass(white(r, d), 300 + 900 * (t / d) ** 2, q=3) * env_points(d, [(0, 0), (d * 0.5, 1), (d, 0)])
    return mixe(d, (plop, 0.3), (splash, 0.4), (suck, 0.3)) * r.uniform(0.4, 1)


@recipe("snd_move_foot_wet", space=(0.2, 0.08))
def foot_wet(rng, v):
    dur = 0.7
    feet = scatter(rng, dur, 5 + v + int(rng.integers(0, 2)), squelch, 0.03, dur - 0.25)
    drops = fit(grains(rng, dur, 35, droplet), int(dur * SR))
    wash = nz(rng, dur, 500, 3500) * ragged(rng, dur, 8)
    x = mixe(dur, (feet, 0.7), (drops, 0.14), (wash, 0.16))
    return fade(tilt(x, 0.2, 0.4, 0.4), 0.04, 0.15)


def hoofbeat(r):
    d = 0.12
    return mixe(d, (thump(r, r.uniform(85, 140), d, 0.6, 0.022, 0.003), 0.4), (knock(r, 480, 1000, d, (0.015, 0.04)), 0.38),
                (nz(r, d, 300, 1800) * env_ad(d, 0.003, 0.02), 0.22)) * r.uniform(0.5, 1)


@recipe("snd_move_hooves", space=(0.2, 0.08))
def hooves(rng, v):
    dur = 0.68
    x = silence(dur)
    # Two to four horses, each a stride of three or four beats, out of step with one another.
    for h in range(2 + v % 3):
        at = rng.uniform(0.03, 0.22)
        while at < dur - 0.2:
            for b in range(int(rng.integers(3, 5))):
                if at < dur - 0.14:
                    x = place(x, hoofbeat(rng), at)
                at += rng.uniform(0.055, 0.1)
            at += rng.uniform(0.12, 0.2)
    x = x[:int(dur * SR)]
    tack = fit(clicks(rng, dur, 90, 2500, 8000, (0.0008, 0.003), (0.05, 0.3)), len(x)) * (0.4 + 0.6 * ragged(rng, dur, 8))
    turf = nz(rng, dur, 400, 3000) * ragged(rng, dur, 16)
    x = mixe(dur, (x, 0.8), (tack, 0.1), (turf, 0.1))
    return fade(tilt(x, 0.30, 0.38, 0.32), 0.03, 0.14)


@recipe("snd_move_giant_tread", space=(0.25, 0.1))
def giant_tread(rng, v):
    dur = 0.8
    at = 0.07
    f0 = [50, 62, 43][v % 3] * rng.uniform(0.95, 1.05)
    air = nz(rng, at + 0.03, 200, 1500) * np.linspace(0, 1, int(round((at + 0.03) * SR))) ** 2
    low = thump(rng, f0, 0.7, 0.5, rng.uniform(0.11, 0.16), 0.008) + nz(rng, 0.7, None, 180) * env_ad(0.7, 0.01, 0.08) * 0.5
    slap = nz(rng, 0.5, 350, 2200) * env_ad(0.5, 0.006, rng.uniform(0.035, 0.06))
    crunch = clicks(rng, rng.uniform(0.15, 0.3), 300, 400, 3500, (0.002, 0.008), (0.2, 1.0), lambda u: 1 - u)
    stones = clicks(rng, 0.5, 25, 1500, 6000, (0.002, 0.006), (0.2, 0.8), lambda u: (1 - u) ** 1.5)
    x = mixe(dur, (air, 0.03), (low, 0.45, at), (slap, 0.24, at), (crunch, 0.2, at + 0.005), (stones, 0.08, at + 0.03))
    return fade(tilt(x, 0.42, 0.30, 0.28), 0.03, 0.1)


@recipe("snd_move_wheels", space=(0.25, 0.1))
def wheels(rng, v):
    dur = 0.9
    n = int(dur * SR)
    t = t_axis(dur)
    # The axle: friction pulses through a pitch that wanders as the wheel turns.
    pulses = np.zeros(n)
    rate = rng.uniform(60, 110)
    tt = 0.0
    while tt < dur:
        pulses[min(n - 1, int(tt * SR))] = rng.uniform(0.5, 1)
        tt += 1 / (rate * rng.uniform(0.75, 1.3))
    f = rng.uniform(450, 800) * (1 + 0.15 * np.sin(2 * np.pi * rng.uniform(1, 2.5) * t + rng.uniform(0, 6)))
    turn = 0.4 + 0.6 * np.abs(np.sin(2 * np.pi * rng.uniform(0.8, 1.6) * t + rng.uniform(0, 6)))
    axle = (svf_bandpass(pulses, f, q=10) + svf_bandpass(pulses, f * 2.4, q=8) * 0.5) * turn
    grind = nz(rng, dur, 150, 1400) * (0.5 + 0.5 * ragged(rng, dur, 20))
    bumps = scatter(rng, dur, 5 + 2 * v, footfall, 0.05, dur - 0.2)
    low = lowpass(brown(rng, dur), 160)
    rattle = fit(grains(rng, dur, 30, lambda r: knock(r, 500, 1500, 0.1) * r.uniform(0.2, 1)), n)
    x = mixe(dur, (axle, 0.2), (grind, 0.25), (bumps, 0.25), (low, 0.18), (rattle, 0.12))
    return fade(tilt(x, 0.33, 0.37, 0.30), 0.06, 0.2)


@recipe("snd_move_wings", space=(0.25, 0.1))
def wings(rng, v):
    dur = 0.78
    t = t_axis(dur)
    centres = [0.2, 0.52] if v % 2 == 0 else [rng.uniform(0.24, 0.32)]
    whoosh, whump, snap, flutter = (np.zeros(len(t)) for _ in range(4))
    for c in centres:
        c += rng.uniform(-0.02, 0.02)
        w = rng.uniform(0.07, 0.1)
        g = np.exp(-((t - c) / w) ** 2)
        f = 250 + rng.uniform(700, 1100) * np.exp(-((t - c) / (w * 0.8)) ** 2)
        whoosh += svf_bandpass(white(rng, dur), f, q=0.9) * g
        whump += (nz(rng, dur, None, 140) * 3 + np.sin(2 * np.pi * rng.uniform(55, 80) * t + rng.uniform(0, 6))) * g
        snap = place(snap, nz(rng, 0.08, 1200, 5000) * env_ad(0.08, 0.004, 0.02), c + w * 0.6)[:len(t)]
        flutter += nz(rng, dur, 1500, 6000) * g * np.abs(np.sin(2 * np.pi * rng.uniform(28, 40) * t))
    x = mixe(dur, (whoosh, 0.4), (whump, 0.3), (snap, 0.14), (flutter, 0.16))
    return fade(tilt(x, 0.35, 0.33, 0.32), 0.03, 0.1)


@recipe("snd_weap_satchel_lay", space=(0.3, 0.1))
def satchel_lay(rng, v):
    dur = 0.9
    bag = thump(rng, [110, 135, 95][v % 3] * rng.uniform(0.95, 1.05), 0.25, 0.6, 0.05, 0.004)
    cloth = nz(rng, 0.25, 300, 2200) * env_ad(0.25, 0.005, rng.uniform(0.03, 0.05))
    shift = clicks(rng, 0.2, 150, 1200, 5000, (0.001, 0.003), (0.05, 0.2))
    # The flint: a rasp, sparks, and the slow match beginning to hiss.
    ts = rng.uniform(0.28, 0.36)
    sd = 0.11
    t = t_axis(sd)
    scratch = nz(rng, sd, 2500, 9000) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * rng.uniform(140, 220) * t)))
    scratch *= env_points(sd, [(0, 0), (0.01, 1), (0.08, 0.8), (sd, 0)])
    spark = clicks(rng, 0.15, 120, 2500, 8000, (0.0005, 0.002), (0.2, 1.0))
    hiss = highpass(white(rng, 0.4), 3000) * env_points(0.4, [(0, 0), (0.05, 1), (0.4, 0)]) * (0.5 + 0.5 * ragged(rng, 0.4, 12))
    return mixe(dur, (bag, 0.25), (cloth, 0.2), (shift, 0.07, 0.02), (scratch, 0.25, ts), (spark, 0.08, ts + 0.09), (hiss, 0.15, ts + 0.1))


# ---------------------------------------------------------------- the player's own feedback (dry, 2D)

@recipe("snd_ui_order_confirm", space=(0, 0))
def order_confirm(rng, v):
    dur = 0.18
    f0 = [200, 235, 175][v % 3] * rng.uniform(0.97, 1.03)
    tap = thump(rng, f0, dur, 0.7, 0.03) + 0.5 * thump(rng, f0 * 1.59, dur, 0.8, 0.02)
    skin = nz(rng, dur, 800, 4000) * env_ad(dur, 0.001, 0.012)
    shell = modal(impulse(dur, 0.001, rng), wood_modes(rng, rng.uniform(600, 900), 4, (0.03, 0.08)))
    strap = creak(rng, 0.12, rng.uniform(900, 1300), rng.uniform(1800, 2600))
    return fade(mixe(dur, (tap, 0.2), (shell, 0.28), (skin, 0.32), (strap, 0.2, 0.04)), 0.001, 0.03)


@recipe("snd_ui_order_refuse", space=(0, 0))
def order_refuse(rng, v):
    dur = 0.26
    hi, lo = [(520, 390), (470, 340)][v % 2]
    x = silence(dur)
    for at, f, g in ((0.0, hi, 1.0), (0.11, lo, 0.85)):
        k = modal(impulse(0.14, 0.001, rng), wood_modes(rng, f * rng.uniform(0.98, 1.02), 4, (0.03, 0.07)))
        dull = nz(rng, 0.14, 500, 2200) * env_ad(0.14, 0.001, 0.01)
        x = place(x, mixe(0.14, (k, 0.7), (dull, 0.3)) * g, at)
    return fade(lowpass(x[:int(dur * SR)], 3500, 1), 0.001, 0.03)


@recipe("snd_ui_toggle", space=(0, 0))
def ui_toggle(rng, v):
    dur = 0.12
    a, b = [(1350, 1000), (1150, 1500)][v % 2]
    x = silence(dur)
    for at, f in ((0.0, a), (0.052, b)):
        x = place(x, mixe(0.065, (tock(rng, f * rng.uniform(0.98, 1.02), 0.065, 0.008), 0.85), (ring(rng, 2400, 0.03, 3, (0.004, 0.01)), 0.15)), at)
    return fade(x[:int(dur * SR)], 0.0005, 0.02)


@recipe("snd_ui_select", space=(0, 0))
def ui_select(rng, v):
    dur = 0.065
    return fade(tock(rng, [1500, 1800, 1300][v % 3] * rng.uniform(0.97, 1.03), dur, 0.007, 0.32, 0.001), 0.0, 0.02)


# ---------------------------------------------------------------- calls and the Conquest

def call(rng, notes, bright=1.3, under=0.4):
    """A horn phrase: notes are (at, seconds, Hz), doubled an octave down."""
    x = silence(max(at + d for at, d, _ in notes))
    for at, d, f in notes:
        x = place(x, brass(f, d, rng, bright), at)
        x = place(x, brass(f / 2, d, rng, bright) * under, at + 0.004)
    return x


def bell(rng, f, dur, tau=1.0, soft=0.002):
    """A cast bell: hum, prime, tierce, quint, nominal and the strike's upper partials."""
    t = t_axis(dur)
    x = np.zeros(len(t))
    for r, d, g in [(0.5, 1.6, 0.45), (1.0, 1.0, 1.0), (1.19, 0.8, 0.7), (1.5, 0.6, 0.5), (2.0, 0.5, 0.8), (2.51, 0.35, 0.5),
                    (3.01, 0.3, 0.4), (4.1, 0.2, 0.35), (5.4, 0.15, 0.3), (6.8, 0.1, 0.25), (8.3, 0.08, 0.2)]:
        if f * r < 11000:
            x += g * np.sin(2 * np.pi * f * r * (1 + rng.uniform(-0.004, 0.004)) * t + rng.uniform(0, 6.283)) * np.exp(-t / (tau * d))
    x *= np.clip(t / soft, 0, 1)
    return x + nz(rng, dur, 1000, 5000) * env_ad(dur, 0.0005, 0.004) * 0.6 * np.clip(0.004 / soft, 0, 1)


@recipe("snd_state_rally_horn", space=(0.5, 0.18))
def rally_horn(rng, v):
    lo, hi = [(392.0, 523.25), (440.0, 587.33)][v % 2]
    return trim_tail(tilt(call(rng, [(0.0, 0.24, lo), (0.26, 0.58, hi)], 1.4), 0.06, 0.58, 0.36), -50)


@recipe("snd_state_deploy_begin", space=(0.6, 0.18))
def deploy_begin(rng, v):
    x = silence(1.3)
    # A roll quickening and swelling, one great stroke, and the horn on top of it.
    at, k = 0.0, 0
    while at < 0.7:
        u = at / 0.7
        x = place(x, war_hit(rng, 150 * rng.uniform(0.97, 1.03), 0.25, (0.25, 0.35, 0.40)) * (0.25 + 0.75 * u ** 1.5), at)
        at += 0.095 - 0.045 * u
        k += 1
    x = place(x, war_hit(rng, 70, 0.7) * 1.6, 0.76)
    stab = call(rng, [(0.0, 0.45, 261.63)], 1.5) + call(rng, [(0.0, 0.45, 392.0)], 1.5) * 0.8 + call(rng, [(0.0, 0.45, 523.25)], 1.4) * 0.6
    stab = tilt(stab, 0.12, 0.55, 0.33)
    x = place(x, stab * (np.max(np.abs(x)) / (np.max(np.abs(stab)) + 1e-12)) * 0.9, 0.77)
    return trim_tail(x, -50)


@recipe("snd_state_results", space=(0.5, 0.15))
def results(rng, v):
    dur = 0.95
    ud = 0.5
    t = t_axis(ud)
    crinkle = clicks(rng, ud, 350, 2000, 8000, (0.0005, 0.002), (0.1, 0.6), lambda u: np.sin(np.pi * u))
    swoosh = nz(rng, ud, 1200, 6000) * np.exp(-((t - 0.24) / 0.12) ** 2)
    sheet = nz(rng, ud, 400, 1500) * np.exp(-((t - 0.26) / 0.14) ** 2)
    stroke = lowpass(war_hit(rng, 75, 0.55, (0.5, 0.3, 0.2)), 3000, 1)
    return mixe(dur, (crinkle, 0.2), (swoosh, 0.17), (sheet, 0.08), (stroke, 0.55, 0.4))


@recipe("snd_conq_town_captured", space=(1.0, 0.25))
def town_captured(rng, v):
    dur = 2.0
    # A round on four bells, twice over, and a rising call ending on the chord.
    peal = silence(dur)
    for k, f in enumerate([783.99, 659.25, 587.33, 523.25] * 2 + [783.99, 523.25]):
        peal = place(peal, bell(rng, f, 0.9, 0.45) * rng.uniform(0.8, 1.0), k * 0.14 + rng.uniform(0, 0.01))
    fan = call(rng, [(0.0, 0.14, 392.0), (0.15, 0.14, 523.25), (0.30, 0.14, 659.25), (0.45, 0.85, 783.99)], 1.4)
    chord = call(rng, [(0.45, 0.85, 523.25)], 1.0, 0.6) * 0.5
    fan = place(fan, chord, 0)
    x = mixe(dur, (peal[:int(dur * SR)], 0.45), (fan, 0.55, 0.5))
    return trim_tail(fade(x, 0.0, 0.25), -55)


@recipe("snd_conq_town_lost", space=(0.8, 0.22))
def town_lost(rng, v):
    dur = 2.1
    toll = silence(dur)
    for at, g in ((0.0, 1.0), (1.05, 0.85)):
        toll = place(toll, bell(rng, 196.0, 1.6, 1.3, 0.004) * g, at)
    # A horn in the minor: A and the C above it, falling to the E below.
    d = 1.7
    t = t_axis(d)
    amp = env_points(d, [(0, 0), (0.08, 1), (0.9, 0.8), (d, 0)])
    glide = np.where(t < 0.95, 1.0, 1.0 - 0.25 * np.clip((t - 0.95) / 0.3, 0, 1))  # A down to E
    mourn = horn(220.0 * glide, amp, 0.9, 24) + horn(261.63 * glide, amp, 0.8, 20) * 0.7
    x = mixe(dur, (toll[:int(dur * SR)], 0.5), (lowpass(mourn, 3500, 2), 0.5, 0.3))
    return trim_tail(fade(tilt(x, 0.38, 0.38, 0.24), 0.0, 0.3), -55)


@recipe("snd_conq_day_tick", space=(0.6, 0.2))
def day_tick(rng, v):
    dur = 0.85
    f = [1046.5, 880.0][v % 2]
    x = bell(rng, f, dur, 0.4, 0.006) + bell(rng, f * 1.5, dur, 0.25, 0.008) * 0.25
    return fade(x, 0.0, 0.2)


@recipe("snd_conq_recruit", space=(0.4, 0.12))
def recruit(rng, v):
    dur = 0.75
    tap = war_hit(rng, [170, 200][v % 2] * rng.uniform(0.97, 1.03), 0.3, (0.3, 0.35, 0.35))
    coins = scatter(rng, 0.4, 3 + v, lambda r: ring(r, r.uniform(3500, 6000), 0.08, 4, (0.01, 0.03)) * r.uniform(0.5, 1), 0.0, 0.25)
    steel = ring(rng, rng.uniform(1400, 2000), 0.42, 7, (0.08, 0.25))
    return mixe(dur, (tap, 0.38), (coins, 0.24, 0.13), (steel, 0.38, 0.3))


@recipe("snd_conq_lord_fallen", space=(1.2, 0.3))
def lord_fallen(rng, v):
    dur = 2.7
    d = 2.5
    t = t_axis(d)
    # The note held, then let fall a fourth, dying as it goes.
    f = 196.0 * (0.749 ** np.clip((t - 0.5) / 1.7, 0, 1))
    amp = env_points(d, [(0, 0), (0.08, 1), (0.6, 0.85), (d, 0)])
    fall = horn(f, amp, 1.1, 30) + horn(f * 2, amp, 0.8, 14) * 0.3
    drums = silence(dur)
    for at, g in ((0.0, 1.0), (0.95, 0.7), (1.9, 0.45)):
        drums = place(drums, lowpass(war_hit(rng, 60 * rng.uniform(0.97, 1.03), 0.8, (0.5, 0.32, 0.18)), 1800, 1) * g, at)
    x = mixe(dur, (fall, 0.6, 0.05), (drums[:int(dur * SR)], 0.4))
    return trim_tail(tilt(x, 0.40, 0.34, 0.26), -55)


# ---------------------------------------------------------------- more ambience (stereo loops)

def crowd_voice(rng, dur, f_lo=110, f_hi=230, rate=(0.5, 1.2)):
    """One throat in a crowd: a rough buzz and breath through a shouting mouth, coming and going."""
    w = lowpass(white(rng, dur), 1.5, 1)
    f = rng.uniform(f_lo, f_hi) * (1 + 0.08 * w / (np.std(w) + 1e-12))
    src = osc(f, "saw") * 0.5 + white(rng, dur) * 0.35
    out = np.zeros(len(src))
    for F, g in ((650, 1.0), (1150, 0.6), (2500, 0.3)):
        out += resonator(src, F * rng.uniform(0.9, 1.1), 5) * g
    return out * gusts(rng, dur, rng.uniform(*rate), 1.0) ** 3


@recipe("snd_amb_battle_far", space=(0, 0), loop=12.0)
def battle_far(rng, v):
    dur = 13.0
    n = int(dur * SR)
    chans = []
    for c in range(2):
        steel = fit(grains(rng, dur, 45, lambda r: ring(r, r.uniform(900, 2600), 0.12, 4, (0.015, 0.06)) * r.uniform(0.3, 1)), n)
        thuds = fit(grains(rng, dur, 30, lambda r: thump(r, r.uniform(140, 380), 0.1, 0.6, 0.02) * r.uniform(0.3, 1)), n)
        crowd = sum(crowd_voice(rng, dur) for _ in range(9))
        bed = bandpass(pink(rng, dur), 300, 2500) * gusts(rng, dur, 0.1, 0.4)
        ch = mixe(dur, (steel, 0.22), (thuds, 0.18), (crowd, 0.38), (bed, 0.22), peak=None)
        # Heard from far above: every blow smeared into the next, no one of them distinct.
        ch = fit(convolve(ch, reverb_ir(rng, 1.2, 0.35, 4000), 0.75), n)
        chans.append(lowpass(highpass(ch, 280, 2), 4500, 1) * gusts(rng, dur, 0.12, 0.3))
    return loopify(np.stack(chans, axis=1), 1.0)


@recipe("snd_amb_march_column", space=(0, 0), loop=9.6)
def march_column(rng, v):
    dur, pace = 9.6, 0.48
    n = int(dur * SR)
    chans = []
    for c in range(2):
        feet, jingle = silence(dur + 1), silence(dur + 1)
        for k in range(int(round(dur / pace))):
            heavy = 1.0 if k % 2 == 0 else 0.8
            for _ in range(14):
                feet = place(feet, footfall(rng) * heavy, (k * pace + rng.normal(0, 0.035)) % dur)
            for _ in range(6):
                jingle = place(jingle, click_grain(rng, 3000, 8000, (0.001, 0.004), (0.05, 0.3)), (k * pace + rng.normal(0.02, 0.06)) % dur)
        bed = loopify(bandpass(pink(rng, dur + 1), 400, 3000), 1.0)
        ch = mixe(dur, (wrap(feet, n), 0.62), (wrap(jingle, n), 0.14), (bed, 0.24), peak=None)
        far = convolve(lowpass(ch, 4000, 1), reverb_ir(rng, 1.0, 0.3, 4000), 0.45)
        chans.append(wrap(far, n))
    return tilt_loop(np.stack(chans, axis=1), 0.30, 0.40, 0.30)


def bird(rng, kind):
    if kind == "chirp":
        x = silence(0.4)
        f = rng.uniform(4200, 5000)
        for k in range(int(rng.integers(3, 6))):
            x = place(x, sine_sweep(f, f * 0.66, 0.04) * np.hanning(int(0.04 * SR)), k * rng.uniform(0.065, 0.08))
        return x
    if kind == "trill":
        x = silence(0.7)
        f = rng.uniform(5000, 5600)
        k = int(rng.integers(12, 19))
        for i in range(k):
            x = place(x, np.sin(2 * np.pi * f * t_axis(0.022)) * np.hanning(int(round(0.022 * SR))) * np.sin(np.pi * (i + 0.5) / k), i * 0.04)
        return x
    if kind == "two":
        a, b = 0.15, 0.22
        x = place(silence(0.5), np.sin(2 * np.pi * 3200 * t_axis(a)) * np.hanning(int(round(a * SR))), 0)
        return place(x, np.sin(2 * np.pi * 2600 * t_axis(b)) * np.hanning(int(round(b * SR))), 0.2)
    if kind == "warble":
        d = 0.55
        t = t_axis(d)
        f = 3800 + 600 * np.sin(2 * np.pi * rng.uniform(14, 20) * t) + 400 * t / d
        return osc(f, "sine") * np.hanning(len(t))
    # A crow, far off.
    x = silence(0.7)
    for k in range(2):
        d = rng.uniform(0.16, 0.22)
        src = osc(np.full(int(d * SR), rng.uniform(430, 520)), "saw") * (0.6 + 0.4 * white(rng, d)[:int(d * SR)])
        caw = (resonator(src, 1300, 4) + resonator(src, 1900, 5) * 0.6) * np.hanning(len(src))
        x = place(x, caw, k * 0.3)
    return x


@recipe("snd_amb_bed_day", space=(0, 0), loop=20.0)
def bed_day(rng, v):
    dur = 20.0
    n = int(dur * SR)
    # Very light air, each ear its own; then a few birds, no call returning inside seven seconds.
    air = []
    for c in range(2):
        a = bandpass(pink(rng, dur + 1), 250, 3500) * gusts(rng, dur + 1, 0.08, 0.35)
        a += highpass(pink(rng, dur + 1), 3000) * 0.15 * gusts(rng, dur + 1, 0.1, 0.4)
        air.append(a)
    air = loopify(np.stack(air, axis=1), 1.0)
    birds = np.zeros((n + 3 * SR, 2))
    for kind, at, p, g in (("chirp", 1.5, -0.6, 1.0), ("trill", 4.8, 0.7, 0.7), ("two", 8.2, -0.2, 0.9), ("warble", 11.5, 0.5, 0.8),
                           ("chirp", 14.0, 0.3, 0.6), ("crow", 16.6, -0.8, 0.8)):
        b = lowpass(bird(rng, kind), 7000, 1)
        b = convolve(b / (np.sqrt(np.mean(b ** 2)) + 1e-12), reverb_ir(rng, 0.8, 0.25, 5000), 0.4) * g
        i = int((at + rng.uniform(-0.3, 0.3)) * SR)
        birds[i:i + len(b)] += pan(b, p)
    birds = wrap(birds, n)
    ea, eb = np.sum(air ** 2), np.sum(birds ** 2)
    return air * np.sqrt(0.84 / ea) + birds * np.sqrt(0.16 / eb)


@recipe("snd_amb_fire", space=(0, 0), loop=LOOP)
def fire_loop(rng, v):
    dur = LOOP + 1.0
    n = int(dur * SR)
    chans = []
    for c in range(2):
        lick = gusts(rng, dur, 4, 0.5)
        low = lowpass(brown(rng, dur), 220) * gusts(rng, dur, 0.4, 0.4)
        roar = nz(rng, dur, 300, 1500) * lick
        crackle = fit(clicks(rng, dur, 45, 700, 6000, (0.0008, 0.004), (0.2, 1.0)), n)
        snaps = fit(grains(rng, dur, 2.5, lambda r: click_grain(r, 500, 2500, (0.004, 0.012), (0.8, 1.5))), n)
        hiss = bandpass(white(rng, dur), 2000, 7000) * gusts(rng, dur, 0.5, 0.5)
        chans.append(mixe(dur, (low, 0.30), (roar, 0.26), (crackle, 0.24), (snaps, 0.08), (hiss, 0.12), peak=None))
    return loopify(np.stack(chans, axis=1), 1.0)


# ---------------------------------------------------------------- hooks for features being built

@recipe("snd_water_splash_body", space=(0.6, 0.15))
def water_splash_body(rng, v):
    dur = 1.2
    f0 = [85, 110, 70][v % 3] * rng.uniform(0.95, 1.05)
    plunge = thump(rng, f0, 0.4, 0.5, 0.1, 0.006) + nz(rng, 0.4, None, 350) * env_ad(0.4, 0.008, 0.07) * 0.7
    smack = nz(rng, 0.5, 400, 2500) * env_ad(0.5, 0.004, rng.uniform(0.035, 0.06))
    splash = nz(rng, 1.0, 800, 7000) * env_ad(1.0, 0.02, rng.uniform(0.15, 0.25)) * (0.5 + 0.5 * ragged(rng, 1.0, 20))
    bubbles = grains(rng, 0.6, 35, blub, density_curve=lambda u: 1 - u)
    drops = grains(rng, 0.8, 60, droplet, density_curve=lambda u: 1 - u)
    x = mixe(dur, (plunge, 0.3), (smack, 0.15), (splash, 0.3), (bubbles, 0.1, 0.08), (drops, 0.15, 0.25))
    return trim_tail(x, -50)


@recipe("snd_boulder_launch", space=(0.6, 0.15))
def boulder_launch(rng, v):
    dur = 1.5
    cracks = silence(0.8)
    for at in (0.0, rng.uniform(0.12, 0.25)):
        c = mixe(0.4, (clicks(rng, 0.08, 500, 700, 6000, (0.001, 0.005), (0.3, 1.0)), 0.5),
                 (modal(impulse(0.4, 0.003, rng), wood_modes(rng, rng.uniform(140, 300), 5, (0.06, 0.2))), 0.5))
        cracks = place(cracks, c * rng.uniform(0.7, 1), at)
    gd = 1.25
    grind = nz(rng, gd, 120, 1600) * ragged(rng, gd, rng.uniform(25, 45), 1.5) * env_points(gd, [(0, 0), (0.15, 1), (0.9, 0.8), (gd, 0)])
    grit = clicks(rng, 1.1, 120, 1500, 6000, (0.001, 0.004), (0.05, 0.4))
    low = lowpass(brown(rng, gd), 120) * env_points(gd, [(0, 0), (0.4, 0.5), (1.1, 1), (gd, 0)])
    x = mixe(dur, (cracks, 0.3), (grind, 0.3, 0.2), (grit, 0.15, 0.2), (low, 0.25, 0.2))
    return trim_tail(tilt(x, 0.35, 0.35, 0.30), -55)


def rock_knock(r):
    d = 0.15
    return mixe(d, (thump(r, r.uniform(60, 120), d, 0.6, 0.035, 0.004), 0.5), (knock(r, 350, 900, d), 0.5)) * r.uniform(0.4, 1)


@recipe("snd_boulder_rumble", space=(0.3, 0.1))
def boulder_rumble(rng, v):
    dur = 1.05
    n = int(dur * SR)
    t = t_axis(dur)
    low = lowpass(brown(rng, dur), 140, 2) * (0.6 + 0.4 * np.abs(np.sin(2 * np.pi * rng.uniform(3.5, 6) * t + rng.uniform(0, 6))))
    knocks = scatter(rng, dur, 7 + 2 * v, rock_knock, 0.03, dur - 0.2)
    clatter = fit(clicks(rng, dur, 30, 900, 4000, (0.002, 0.006), (0.1, 0.6)), n)
    grind = nz(rng, dur, 200, 1500) * (0.4 + 0.6 * ragged(rng, dur, 18))
    x = mixe(dur, (low, 0.3), (knocks, 0.3), (clatter, 0.2), (grind, 0.2))
    return fade(tilt(x, 0.40, 0.33, 0.27), 0.12, 0.25)


@recipe("snd_boulder_impact", space=(1.4, 0.3))
def boulder_impact(rng, v):
    dur = 1.7
    f0 = [46, 58, 38][v % 3] * rng.uniform(0.95, 1.05)
    boom = thump(rng, f0, 0.9, 0.5, rng.uniform(0.17, 0.25)) + nz(rng, 0.9, None, 300) * env_ad(0.9, 0.002, 0.1) * 0.6
    crack = nz(rng, 0.3, 500, 6000) * env_ad(0.3, 0.0005, rng.uniform(0.015, 0.03))
    crunch = nz(rng, 1.2, 300, 3000) * env_ad(1.2, 0.002, rng.uniform(0.09, 0.15))
    rocks = grains(rng, 1.1, 22, lambda r: knock(r, 300, 900) * r.uniform(0.4, 1), density_curve=lambda u: 1 - u)
    grit = clicks(rng, 1.2, 200, 1000, 6000, (0.001, 0.006), (0.05, 0.5), lambda u: (1 - u) ** 2)
    x = mixe(dur, (boom, 0.42), (crack, 0.1), (crunch, 0.2), (rocks, 0.16, 0.06), (grit, 0.12, 0.03))
    return trim_tail(tilt(saturate(x, 1.6), 0.42, 0.28, 0.30), -55)


@recipe("snd_boulder_crush", space=(0.5, 0.15))
def boulder_crush(rng, v):
    dur = 0.9
    f0 = [70, 88, 58][v % 3] * rng.uniform(0.95, 1.05)
    low = thump(rng, f0, 0.5, 0.5, 0.12) + nz(rng, 0.5, None, 800, 3) * env_ad(0.5, 0.002, 0.08) * 0.8
    wet = nz(rng, 0.5, 300, 1400) * env_ad(0.5, 0.004, rng.uniform(0.07, 0.12))
    bones = clicks(rng, rng.uniform(0.1, 0.2), 300, 900, 4500, (0.001, 0.005), (0.3, 1.0))
    bits = wet_bits(rng, 0.4, 150, 200, 1500)
    metal = scatter(rng, 0.6, 2 + v, lambda r: ring(r, r.uniform(800, 2200), 0.3, 5, (0.03, 0.15)) * r.uniform(0.4, 1), 0.0, 0.3)
    stone = scatter(rng, 0.5, 2, lambda r: knock(r, 300, 800), 0.0, 0.3)
    x = mixe(dur, (low, 0.35), (wet, 0.2), (bones, 0.15), (bits, 0.1, 0.02), (metal, 0.12), (stone, 0.08, 0.1))
    return trim_tail(saturate(x, 1.5), -50)


@recipe("snd_call_help", space=(0.4, 0.16))
def call_help(rng, v):
    lo, hi = [(329.63, 440.0), (349.23, 466.16)][v % 2]
    return trim_tail(tilt(call(rng, [(0.0, 0.18, lo), (0.2, 0.46, hi)], 1.6), 0.06, 0.56, 0.38), -50)


@recipe("snd_call_answer", space=(0.35, 0.16))
def call_answer(rng, v):
    f = [220.0, 196.0][v % 2]
    x = brass(f, 0.5, rng, 1.0) + brass(f * 2, 0.5, rng, 0.9) * 0.35 + brass(f / 2, 0.5, rng, 1.0) * 0.3
    return trim_tail(tilt(x, 0.30, 0.44, 0.26), -50)


@recipe("snd_charge_impact", space=(0.5, 0.18))
def charge_impact(rng, v):
    dur = 1.0

    def body(r):
        d = 0.35
        return mixe(d, (thump(r, r.uniform(55, 115), d, 0.55, 0.08), 0.6), (nz(r, d, 300, 1500) * env_ad(d, 0.002, 0.04), 0.4)) * r.uniform(0.5, 1)

    def lance(r):
        d = 0.25
        return mixe(d, (clicks(r, 0.05, 600, 900, 6000, (0.001, 0.004), (0.3, 1.0)), 0.5),
                    (modal(impulse(d, 0.001, r), wood_modes(r, r.uniform(260, 520), 4, (0.03, 0.1))), 0.5)) * r.uniform(0.6, 1)

    bodies = scatter(rng, dur, 4 + v, body, 0.0, 0.3)
    lances = scatter(rng, dur, 2 + v % 2, lance, 0.0, 0.25)
    harness = scatter(rng, dur, 5 + v, lambda r: ring(r, r.uniform(900, 2600), 0.4, 6, (0.04, 0.2)) * r.uniform(0.3, 1), 0.0, 0.5)
    mail = clicks(rng, 0.7, 150, 3500, 9000, (0.0008, 0.003), (0.05, 0.4), lambda u: 1 - u)
    scuffle = nz(rng, 0.9, 300, 2500) * env_points(0.9, [(0, 0.6), (0.3, 1), (0.9, 0)]) * ragged(rng, 0.9, 16)
    x = mixe(dur, (bodies, 0.36), (lances, 0.2), (harness, 0.2), (mail, 0.08), (scuffle, 0.16))
    return trim_tail(tilt(saturate(x, 1.5), 0.36, 0.30, 0.34), -55)


@recipe("snd_morale_rout", space=(0.6, 0.2))
def morale_rout(rng, v):
    dur = 1.75
    cry = silence(dur)
    # Voices as noise: a dozen throats breaking at once, each falling in pitch and failing.
    for k in range(9 + 2 * v):
        d = rng.uniform(0.6, 1.1)
        t = t_axis(d)
        f = rng.uniform(190, 320) * rng.uniform(0.5, 0.7) ** (t / d) * (1 + 0.02 * np.sin(2 * np.pi * rng.uniform(5, 8) * t))
        src = osc(f, "saw") * 0.45 + white(rng, d) * 0.45
        voice = np.zeros(len(t))
        for F, g in ((700, 1.0), (1100, 0.6), (2600, 0.3)):
            voice += resonator(src, F * rng.uniform(0.88, 1.12), 5) * g
        voice *= env_points(d, [(0, 0), (rng.uniform(0.05, 0.12), 1), (d * 0.5, 0.7), (d, 0)]) * (0.6 + 0.4 * ragged(rng, d, 9))
        cry = place(cry, voice * rng.uniform(0.5, 1), rng.uniform(0.0, 0.6))
    arms = scatter(rng, dur, 5 + v, lambda r: ring(r, r.uniform(700, 2200), 0.4, 6, (0.05, 0.25)) * r.uniform(0.4, 1), 0.45, 1.3)
    shields = scatter(rng, dur, 3, lambda r: knock(r, 200, 400, 0.25, (0.05, 0.12)), 0.45, 1.3)
    scuffle = nz(rng, dur, 300, 2000) * ragged(rng, dur, 10) * env_points(dur, [(0, 0), (0.5, 1), (dur, 0.2)])
    x = mixe(dur, (cry[:int(dur * SR)], 0.55), (arms, 0.22), (shields, 0.1), (scuffle, 0.13))
    return trim_tail(fade(x, 0.02, 0.3), -55)
