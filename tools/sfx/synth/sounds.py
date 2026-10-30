"""The recipes: one function per sound id in game/web/sfx/sounds.json.

Each takes (rng, v), the variant's random generator and its index, and
returns a mono array (one-shots) or a stereo (n, 2) array (ambience loops) at
dsp.SR. Layered the way a sound designer would: a transient, a body, a tail.
"""
import numpy as np

from dsp import (SR, bandpass, brown, click_grain, convolve, env_ad, env_points, fade, grains, highpass, impulse,
                 karplus, loopify, lowpass, modal, osc, pan, pink, place, resonator, reverb_ir, saturate, silence,
                 sine_sweep, svf_bandpass, t_axis, trim_tail, white)

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
    ex = impulse(dur, 0.0015, rng) + 0.5 * np.roll(impulse(dur, 0.001, rng), int(rng.uniform(0.008, 0.02) * SR))
    ring = modal(ex, metal_modes(rng, base, 9, (0.3, 1.2)))
    scrape = highpass(white(rng, dur), 3500) * env_ad(dur, 0.0005, 0.025) * 0.6
    clank = thud(rng, 220, dur, drop=0.8, noise_lp=1500) * env_ad(dur, 0.0005, 0.03) * 0.4
    return trim_tail(ring * 2 + scrape + clank, -55)


@recipe("snd_weap_blunt_crush", space=(0.5, 0.15))
def blunt(rng, v):
    dur = 0.55
    body = thud(rng, rng.uniform(62, 85), dur, drop=0.5, noise_lp=700) * 1.2
    crunch = grains(rng, 0.09, 600, lambda r: click_grain(r, 250, 1800, (0.002, 0.008)))[:int(dur * SR)]
    crunch = np.pad(crunch, (0, int(dur * SR) - len(crunch)))
    smack = bandpass(white(rng, dur), 300, 2500) * env_ad(dur, 0.0005, 0.02)
    return trim_tail(saturate(body + crunch * 0.6 + smack * 0.8, 2.0), -50)


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
    ex = impulse(dur, 0.002, rng)
    wood = modal(ex, wood_modes(rng, rng.uniform(170, 230), 5, (0.05, 0.18)))
    knock = thud(rng, 110, dur, drop=0.7, noise_lp=1200) * 0.6
    return trim_tail(wood * 3 + knock, -50)


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
    # The creak: a jittered friction pulse train through a wooden resonance.
    cd = 0.35
    rate = rng.uniform(45, 75)
    pulses = np.zeros(int(cd * SR))
    tt = 0.0
    while tt < cd:
        i = int(tt * SR)
        pulses[i] = rng.uniform(0.5, 1)
        tt += 1 / (rate * rng.uniform(0.8, 1.25))
    creak = resonator(resonator(pulses, rng.uniform(500, 800), 8), rng.uniform(1200, 1800), 6)
    creak *= env_points(cd, [(0, 0), (0.05, 1), (0.3, 0.7), (0.35, 0)])
    # The arm into the crossbar: a heavy wooden thunk.
    ex = impulse(1.0, 0.004, rng)
    slam = modal(ex, wood_modes(rng, rng.uniform(80, 110), 6, (0.1, 0.4))) * 1.2 + thud(rng, 70, 1.0, 0.6) * 1.2
    twang = karplus(rng, rng.uniform(60, 80), 0.8, 0.995, 0.3) * env_ad(0.8, 0.001, 0.25) * 0.4
    x = silence(dur)
    x = place(x, creak * 40.0, 0)
    x = place(x, slam, cd)
    x = place(x, twang, cd)
    return trim_tail(x, -55)


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
    thock = modal(impulse(dur, 0.001, rng), wood_modes(rng, rng.uniform(600, 900), 5, (0.02, 0.08))) * 3
    # The shaft quivering in the wood.
    t = t_axis(dur)
    quiver = np.sin(2 * np.pi * rng.uniform(85, 140) * t) * np.exp(-t / 0.12) * (1 + 0.5 * np.sin(2 * np.pi * 22 * t)) * 0.25
    return trim_tail(thock + quiver, -50)


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
    ring = modal(impulse(dur, 0.0008, rng), metal_modes(rng, rng.uniform(1700, 2600), 7, (0.15, 0.5)))
    tick = highpass(white(rng, dur), 4000) * env_ad(dur, 0.0002, 0.003)
    return trim_tail(ring * 2 + tick * 0.7, -55)


@recipe("snd_impact_arrow_flesh", space=(0.3, 0.1))
def arrow_flesh(rng, v):
    dur = 0.3
    slap = bandpass(white(rng, dur), 800, 3500) * env_ad(dur, 0.0003, 0.008) * 1.2
    thwack = thud(rng, rng.uniform(140, 200), dur, 0.6, 1000) * 0.9
    wet = bandpass(white(rng, dur), 400, 1500) * env_ad(dur, 0.002, 0.04) * 0.6
    bits = wet_bits(rng, 0.08, 120)
    x = place(slap + thwack + wet, bits, 0.005, 0.6)
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


@recipe("snd_impact_stone_smash", space=(1.4, 0.3))
def stone_smash(rng, v):
    dur = 1.6
    boom = thud(rng, rng.uniform(45, 60), 0.8, 0.5, 400) * 1.6
    crunch = lowpass(white(rng, dur), 3000) * env_ad(dur, 0.001, 0.12)
    grit = grains(rng, 1.0, 220, lambda r: click_grain(r, 800, 5000, (0.001, 0.006), (0.05, 0.5)), density_curve=lambda u: (1 - u) ** 2)
    rocks = grains(rng, 0.8, 18, lambda r: modal(impulse(0.15, 0.001, r), wood_modes(r, r.uniform(300, 700), 3, (0.02, 0.06))) * 1.5,
                   density_curve=lambda u: 1 - u)
    x = place(crunch, boom, 0)
    x = place(x, grit, 0.02)
    x = place(x, rocks, 0.05)
    return trim_tail(saturate(x, 1.6), -55)


@recipe("snd_impact_gate_wood", space=(1.3, 0.3))
def gate_wood(rng, v):
    dur = 1.2
    boom = modal(impulse(dur, 0.004, rng), wood_modes(rng, rng.uniform(55, 75), 6, (0.2, 0.6))) * 5
    knock = thud(rng, 70, dur, 0.7, 900)
    rattle = grains(rng, 0.4, 40, lambda r: click_grain(r, 400, 2000, (0.003, 0.01), (0.1, 0.3)), density_curve=lambda u: 1 - u)
    return trim_tail(place(boom + knock, rattle, 0.03), -55)


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
    sub = sine_sweep(rng.uniform(70, 90), 28, 1.2) * env_ad(1.2, 0.003, 0.35) * 1.6
    crack = white(rng, dur) * env_ad(dur, 0.0002, 0.006) * 2.5
    cutoff = 300 + 6000 * np.exp(-t / 0.25)
    body = svf_bandpass(white(rng, dur), cutoff * 0.5, q=0.5) * env_ad(dur, 0.002, 0.45) * 1.4
    rumble = lowpass(brown(rng, dur), 150) * env_ad(dur, 0.05, 0.9) * 0.9
    debris = grains(rng, 2.0, 60, lambda r: click_grain(r, 400, 4000, (0.002, 0.01), (0.05, 0.4)), density_curve=lambda u: (1 - u) ** 1.5)
    x = place(crack + body + rumble, sub, 0)
    x = place(x, debris, 0.15)
    return trim_tail(saturate(x * 0.9, 2.5), -60)


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
    dur = 0.7
    first = thud(rng, rng.uniform(70, 95), 0.5, 0.6, 450) * 1.3
    second = thud(rng, rng.uniform(100, 140), 0.3, 0.7, 700) * 0.6
    cloth = bandpass(white(rng, dur), 800, 3000) * env_ad(dur, 0.01, 0.05) * 0.25
    x = place(first + np.pad(cloth[:len(first)], (0, max(0, len(first) - len(cloth)))), second, rng.uniform(0.08, 0.16))
    return trim_tail(x, -50)


@recipe("snd_gore_body_fall_armor", space=(0.7, 0.18))
def body_fall_armor(rng, v):
    x = body_fall(rng, v) * 1.1
    for k in range(rng.integers(3, 6)):
        ring = modal(impulse(0.5, 0.001, rng), metal_modes(rng, rng.uniform(900, 2200), 5, (0.05, 0.25)))
        x = place(x, ring * rng.uniform(0.3, 0.8), rng.uniform(0.0, 0.25))
    return trim_tail(x, -50)


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
    for f in (110.0, 130.81, 164.81, 220.0):  # A minor
        for d in (-0.15, 0.15):
            vib = 1 + 0.003 * np.sin(2 * np.pi * (4.5 + d) * t + rng.uniform(0, 6))
            x += osc(f * (1 + d / 100) * vib, "saw")
    x = lowpass(x, 900, 2) * amp * 0.3
    sub = osc(np.full(len(t), 55.0), "sine") * amp * 0.8
    return fade(x + sub, 0.05, 0.5)


@recipe("snd_state_war_horn", space=(2.5, 0.35))
def war_horn(rng, v):
    # A long low horn call, answered a fifth higher: the next wave sets out (Horde).
    x = silence(3.6)
    x = place(x, lowpass(brass(98.0, 1.4, rng, 0.5), 1800, 2), 0.0, 0.9)
    x = place(x, lowpass(brass(147.0, 1.9, rng, 0.5), 2000, 2) * 0.85, 1.5, 0.9)
    return trim_tail(x, -60)


@recipe("snd_state_alarm", space=(1.2, 0.25))
def alarm(rng, v):
    # Three quick rising blasts of a hand horn: the flag is in danger (capture the flag).
    x = silence(1.6)
    for k in range(3):
        x = place(x, brass(233.0 * (1 + 0.125 * k), 0.26, rng, 1.3), k * 0.34, 0.85)
    return trim_tail(x, -60)


# ---------------------------------------------------------------- UI

@recipe("snd_ui_click", space=(0.15, 0.05))
def ui_click(rng, v):
    x = modal(impulse(0.12, 0.0005, rng), wood_modes(rng, rng.uniform(1700, 2300), 3, (0.006, 0.02)))
    return fade(x[:int(0.08 * SR)], 0, 0.02)


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
    chans = []
    for c in range(2):
        g = gusts(rng, dur, 0.2, 0.65)
        sweep = lowpass(pink(rng, dur), 500, 2) * g
        rustle = bandpass(pink(rng, dur), 1500, 6000) * g ** 2 * 0.25
        grit = grains(rng, dur, 60, lambda r: click_grain(r, 3000, 9000, (0.0005, 0.002), (0.02, 0.12)))[:len(g)]
        chans.append(sweep + rustle + np.pad(grit, (0, len(g) - len(grit))) * g)
    return loopify(np.stack(chans, axis=1), 1.0)


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


def drum(rng, f0, dur=0.9, slap=0.4):
    t = t_axis(dur)
    pitch = f0 * (1 + 0.6 * np.exp(-t / 0.03))
    x = np.zeros(len(t))
    for ratio, decay, gain in [(1, 0.35, 1), (1.59, 0.2, 0.4), (2.14, 0.14, 0.25), (2.3, 0.12, 0.2), (2.65, 0.1, 0.12)]:
        x += osc(pitch * ratio, "sine") * np.exp(-t / decay) * gain
    x += lowpass(white(rng, dur), 1500) * np.exp(-t / 0.02) * slap
    return x


@recipe("snd_amb_war_drums", space=(0, 0), loop=4.8)
def war_drums(rng, v):
    # 100 beats a minute, two bars of four: BOOM . boom boom | BOOM . . boom (with a flam).
    beat = 0.6
    bars = 8
    dur = beat * bars
    pattern = [(0, 1.0, 62), (2, 0.6, 75), (3, 0.7, 75), (4, 1.0, 62), (7, 0.6, 75), (7.12, 0.35, 75)]
    x = silence(dur + 2)
    for at, g, f in pattern:
        x = place(x, drum(rng, f * rng.uniform(0.97, 1.03)) * g * rng.uniform(0.85, 1.0), at * beat + rng.uniform(-0.01, 0.01))
    # Far off: muffled, in a big space.
    x = lowpass(x, 700, 2)
    ir = reverb_ir(rng, 2.0, 0.6, 3000, stereo=True)
    wet = convolve(x, ir, 0.6)
    # Wrap the reverb tail round onto the start, so the loop is seamless.
    n = int(dur * SR)
    out = wet[:n].copy()
    tail = wet[n:]
    out[:len(tail)] += tail[:n]
    return out


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
    rumble = lowpass(brown(rng, dur), rng.uniform(120, 220), 2) * swells * 2
    crack = highpass(white(rng, dur), 800) * env_ad(dur, 0.002, 0.15) * (0.6 if v % 2 == 0 else 0.0)
    return fade(rumble + crack, 0.01, 0.6)


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
