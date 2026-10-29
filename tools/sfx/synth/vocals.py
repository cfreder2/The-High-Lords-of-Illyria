"""Placeholder cries for the voices (game/web/voice/lines.json): pain, death,
the choking moan, the grenadier's cackle, the undead groan. Formant
synthesis: a glottal pulse train with jitter, shimmer and breath, through
the formants of a vowel, under a pitch contour. Stand-ins until an actor
records them (docs/VOICE_LINES.md); spoken lines stay with the browser's speech.
"""
import numpy as np
from scipy import signal

from dsp import SR, bandpass, env_points, fade, highpass, lowpass, place, saturate, silence, t_axis, trim_tail, white

# Formants (Hz, bandwidth Hz, gain) of a few vowels, for a man's voice.
VOWELS = {
    "a": [(750, 90, 1.0), (1200, 110, 0.6), (2550, 160, 0.25), (3500, 250, 0.1)],
    "o": [(500, 80, 1.0), (850, 100, 0.5), (2450, 160, 0.15), (3400, 250, 0.08)],
    "u": [(330, 70, 1.0), (800, 90, 0.35), (2300, 150, 0.1), (3300, 250, 0.05)],
    "e": [(480, 80, 1.0), (1800, 120, 0.45), (2550, 160, 0.25), (3500, 250, 0.1)],
    "i": [(300, 70, 1.0), (2250, 130, 0.35), (3000, 180, 0.2), (3700, 250, 0.1)],
    "ae": [(650, 90, 1.0), (1700, 120, 0.5), (2500, 160, 0.25), (3500, 250, 0.1)],
}

# Each voice: pitch scale, formant scale (smaller bodies, higher formants), roughness, breath.
VOICES = {
    "soldier": (1.0, 1.0, 0.2, 0.15),
    "archer": (1.12, 1.04, 0.15, 0.2),
    "dwarf": (0.88, 0.92, 0.35, 0.15),
    "rider": (1.02, 1.0, 0.2, 0.15),
    "hero": (0.95, 0.98, 0.15, 0.1),
    "wizard": (1.15, 1.05, 0.25, 0.3),
    "berserker": (0.9, 0.96, 0.5, 0.2),
    "archon": (1.3, 1.08, 0.05, 0.25),
    "engine": (1.0, 1.0, 0.2, 0.15),
    "brute": (0.55, 0.72, 0.8, 0.2),
    "undead": (0.7, 0.85, 0.9, 0.6),
    "fiend": (2.2, 1.25, 0.7, 0.4),
    "beast": (2.6, 1.35, 0.5, 0.35),
}


def glottal(rng, f0, rough, shimmer=0.1):
    """A pulse train following f0 (per sample), with jitter and a
    subharmonic wobble for roughness."""
    n = len(f0)
    jit = 1 + rough * 0.03 * signal.sosfilt(signal.butter(1, 60, fs=SR, output="sos"), rng.standard_normal(n)) * 10
    f = f0 * jit
    # Subharmonics: every other period louder (a rough, strained voice).
    ph = np.cumsum(f) / SR
    frac = ph - np.floor(ph)
    # A Rosenberg-like pulse: a smooth rise and a sharp close.
    open_q = 0.6
    pulse = np.where(frac < open_q, 0.5 * (1 - np.cos(np.pi * frac / open_q)),
                     np.maximum(0, np.cos(np.pi * (frac - open_q) / (2 * (1 - open_q)))))
    src = np.diff(pulse, prepend=0) * SR / np.maximum(f, 40) * 0.05
    sub = 1 + rough * 0.6 * np.sign(np.sin(np.pi * np.floor(ph)))
    amp = 1 + shimmer * signal.sosfilt(signal.butter(1, 30, fs=SR, output="sos"), rng.standard_normal(n)) * 5
    return src * sub * amp


def formants(src, vowel_a, vowel_b, scale, morph=None):
    """Through the formants, morphing from one vowel to another."""
    n = len(src)
    morph = np.linspace(0, 1, n) if morph is None else morph
    out_a = np.zeros(n)
    out_b = np.zeros(n)
    for (fa, ba, ga), (fb, bb, gb) in zip(VOWELS[vowel_a], VOWELS[vowel_b]):
        for (f, bw, g), out in (((fa, ba, ga), out_a), ((fb, bb, gb), out_b)):
            f = min(f * scale, SR * 0.45)
            b, a = signal.iirpeak(f, f / (bw * scale), fs=SR)
            out += signal.lfilter(b, a, src) * g
    return out_a * (1 - morph) + out_b * morph


def utter(rng, voice, dur, contour, vowels=("a", "o"), loud=1.0, breath_extra=0.0, cut=False, gurgle=0.0):
    pitch, fscale, rough, breath = VOICES[voice]
    t = t_axis(dur)
    ts, fs = zip(*contour)
    f0 = np.interp(t, np.array(ts) * dur, np.array(fs) * pitch * 110)
    src = glottal(rng, f0, rough)
    noise = highpass(white(rng, dur), 500) * (breath + breath_extra) * 0.025
    x = formants(src + noise, vowels[0], vowels[1], fscale)
    amp = env_points(dur, [(0, 0), (0.03, 1), (dur * 0.6, 0.85), (dur, 0)] if not cut else
                     [(0, 0), (0.03, 1), (dur * 0.85, 0.8), (dur * 0.9, 0), (dur, 0)])
    if gurgle:
        amp *= 1 - gurgle * (0.5 + 0.5 * np.sin(2 * np.pi * rng.uniform(14, 24) * t))
    # A breathy onset: the "h" before the voice.
    onset = bandpass(white(rng, 0.06), 800, 4000) * np.linspace(0.6, 0, int(0.06 * SR)) * (0.3 + breath)
    x = place(x * amp, onset, 0, 0.3)
    return saturate(x * loud, 1.0 + rough * 2)


def pain(rng, voice, v):
    dur = rng.uniform(0.25, 0.5)
    hi = rng.uniform(1.3, 1.8)
    vow = [("a", "u"), ("u", "u"), ("ae", "a"), ("e", "u")][v % 4]
    return utter(rng, voice, dur, [(0, hi), (0.3, hi * 1.05), (1, hi * 0.75)], vow, 1.0)


def death(rng, voice, v):
    dur = rng.uniform(0.7, 1.2)
    a, peak, end = rng.uniform(1.4, 1.8), rng.uniform(2.3, 3.0), rng.uniform(1.0, 1.3)
    vow = [("a", "o"), ("ae", "a"), ("a", "u"), ("e", "a")][v % 4]
    x = utter(rng, voice, dur, [(0, a), (0.25, peak), (0.6, peak * 0.9), (1, end)], vow, 1.2, breath_extra=0.1)
    return x


def moan(rng, voice, v):
    dur = rng.uniform(0.45, 0.75)
    f = rng.uniform(0.9, 1.15)
    return utter(rng, voice, dur, [(0, f * 1.2), (0.4, f), (1, f * 0.8)], ("u", "o"), 0.8, breath_extra=0.3, cut=True, gurgle=0.6)


def cackle(rng, voice, v):
    x = silence(0.1)
    at = 0.0
    n = rng.integers(5, 8)
    for k in range(n):
        d = rng.uniform(0.07, 0.11)
        f = rng.uniform(2.0, 2.6) + k * 0.06
        syl = utter(rng, voice, d, [(0, f * 1.1), (1, f * 0.9)], ("ae", "a") if k % 2 else ("e", "ae"), 0.9, breath_extra=0.4)
        x = place(x, syl, at)
        at += d + rng.uniform(0.03, 0.06) * (1 - k / n * 0.5)
    return x


def groan(rng, voice, v):
    dur = rng.uniform(1.1, 1.8)
    x = utter(rng, voice, dur, [(0, 0.9), (0.5, 1.0), (1, 0.75)], ("o", "u"), 0.9, breath_extra=0.4)
    # The jaw clicking.
    for k in range(rng.integers(1, 4)):
        d = 0.004
        c = np.sin(2 * np.pi * rng.uniform(1500, 3000) * t_axis(d * 4)) * np.exp(-t_axis(d * 4) / d)
        x = place(x, c * 0.8, rng.uniform(0.05, dur))
    return x


def wordless(rng, voice, situation, v):
    """Brutes bellow, fiends shriek, beasts squeal, the dead hiss."""
    if voice == "undead":
        dur = rng.uniform(0.4, 0.9) if situation == "pain" else rng.uniform(1.0, 1.6)
        hiss = utter(rng, voice, dur, [(0, 1.0), (1, 0.7)], ("o", "u"), 0.6, breath_extra=1.5)
        rattle = np.zeros(1)
        for k in range(rng.integers(4, 9)):
            d = 0.005
            c = np.sin(2 * np.pi * rng.uniform(900, 2500) * t_axis(d * 4)) * np.exp(-t_axis(d * 4) / d)
            hiss = place(hiss, c * 0.6, rng.uniform(0, dur))
        return hiss
    if situation == "pain":
        dur = rng.uniform(0.3, 0.6)
        return utter(rng, voice, dur, [(0, 1.4), (0.3, 1.6), (1, 1.0)], ("a", "o"), 1.1, breath_extra=0.2)
    dur = rng.uniform(0.9, 1.5)
    return utter(rng, voice, dur, [(0, 1.3), (0.3, 1.8), (0.7, 1.5), (1, 0.8)], ("a", "u"), 1.2, breath_extra=0.25)


def render(rng, voice, situation, v):
    if voice in ("brute", "undead", "fiend", "beast") and situation in ("pain", "death"):
        x = wordless(rng, voice, situation, v)
    else:
        x = {"pain": pain, "death": death, "death_moan": moan, "cackle": cackle, "groan": groan}[situation](rng, voice, v)
    x = highpass(x, 70)
    return trim_tail(fade(x, 0.003, 0.03), -50)
