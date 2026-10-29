"""Small DSP kit for the rendered sound effects (numpy + scipy.signal).

Everything works on float64 arrays at SR samples a second; stereo is (n, 2).
Randomness always comes from a numpy Generator passed in, so every render is
deterministic for its seed.
"""
import numpy as np
from scipy import signal

SR = 48000


def t_axis(seconds):
    return np.arange(int(round(seconds * SR))) / SR


def silence(seconds):
    return np.zeros(int(round(seconds * SR)))


def white(rng, seconds):
    return rng.standard_normal(int(round(seconds * SR)))


def pink(rng, seconds):
    """1/f noise by spectral shaping."""
    n = int(round(seconds * SR))
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = f[1]
    spec /= np.sqrt(f)
    out = np.fft.irfft(spec, n)
    return out / (np.std(out) + 1e-12)


def brown(rng, seconds):
    x = np.cumsum(rng.standard_normal(int(round(seconds * SR))))
    x = signal.sosfilt(signal.butter(1, 15, "hp", fs=SR, output="sos"), x)
    return x / (np.std(x) + 1e-12)


def _sos(kind, freq, order=2):
    return signal.butter(order, freq, kind, fs=SR, output="sos")


def lowpass(x, f, order=2):
    return signal.sosfilt(_sos("lowpass", min(f, SR * 0.45), order), x, axis=0)


def highpass(x, f, order=2):
    return signal.sosfilt(_sos("highpass", f, order), x, axis=0)


def bandpass(x, lo, hi, order=2):
    return signal.sosfilt(_sos("bandpass", [lo, min(hi, SR * 0.45)], order), x, axis=0)


def resonator(x, f, q):
    """A two-pole resonant band-pass (a peaking filter of bandwidth f/q)."""
    b, a = signal.iirpeak(min(f, SR * 0.45), q, fs=SR)
    return signal.lfilter(b, a, x, axis=0)


def sweep_filter(x, f_start, f_end, q=2.0, curve="exp", block=256):
    """A band-pass whose centre moves from f_start to f_end over the sound
    (state-variable filter, coefficients updated per block)."""
    n = len(x)
    out = np.zeros(n)
    lo = bp = 0.0
    k = 1.0 / q
    nb = (n + block - 1) // block
    for i in range(nb):
        u = i / max(nb - 1, 1)
        f = f_start * (f_end / f_start) ** u if curve == "exp" else f_start + (f_end - f_start) * u
        g = 2 * np.sin(np.pi * min(f, SR * 0.2) / SR)
        seg = x[i * block:(i + 1) * block]
        o = np.empty(len(seg))
        for j, s in enumerate(seg):
            hp = s - lo - k * bp
            bp += g * hp
            lo += g * bp
            o[j] = bp
        out[i * block:i * block + len(seg)] = o
    return out


def svf_bandpass(x, freqs, q=2.0):
    """A band-pass whose centre follows the array `freqs` (one per sample);
    a fast vectorised approximation: filter in short blocks at each block's frequency."""
    n = len(x)
    block = 512
    out = np.zeros(n)
    zi = None
    for i in range(0, n, block):
        f = float(np.clip(np.mean(freqs[i:i + block]), 30, SR * 0.45))
        b, a = signal.iirpeak(f, q, fs=SR)
        if zi is None:
            zi = signal.lfilter_zi(b, a) * 0
        y, zi = signal.lfilter(b, a, x[i:i + block], zi=zi)
        out[i:i + block] = y
    return out


def env_ad(seconds, attack, decay_tau, curve=1.0):
    t = t_axis(seconds)
    a = np.clip(t / max(attack, 1e-4), 0, 1) ** curve
    d = np.exp(-np.maximum(t - attack, 0) / decay_tau)
    return a * d


def env_points(seconds, points):
    """Piecewise-linear envelope through (time, level) points."""
    t = t_axis(seconds)
    ts, vs = zip(*points)
    return np.interp(t, ts, vs)


def fade(x, fin=0.002, fout=0.01):
    x = x.copy()
    n_in, n_out = int(fin * SR), int(fout * SR)
    if n_in:
        x[:n_in] *= np.linspace(0, 1, n_in)[:, None] if x.ndim == 2 else np.linspace(0, 1, n_in)
    if n_out:
        x[-n_out:] *= np.linspace(1, 0, n_out)[:, None] if x.ndim == 2 else np.linspace(1, 0, n_out)
    return x


def place(dst, src, at_seconds, gain=1.0):
    """Mixes src into dst starting at a time (dst grows if needed)."""
    i = max(0, int(round(at_seconds * SR)))
    need = i + len(src)
    if need > len(dst):
        pad = np.zeros((need - len(dst),) + dst.shape[1:])
        dst = np.concatenate([dst, pad])
    dst[i:i + len(src)] += src * gain
    return dst


def sine_sweep(f0, f1, seconds, curve="exp"):
    t = t_axis(seconds)
    if curve == "exp" and f0 > 0 and f1 > 0:
        f = f0 * (f1 / f0) ** (t / seconds)
    else:
        f = f0 + (f1 - f0) * t / seconds
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def osc(freqs, kind="sine", phase=0.0):
    """An oscillator following a per-sample frequency array."""
    ph = phase + np.cumsum(freqs) / SR
    if kind == "sine":
        return np.sin(2 * np.pi * ph)
    if kind == "saw":
        return 2 * (ph - np.floor(ph + 0.5))
    if kind == "square":
        return np.sign(np.sin(2 * np.pi * ph))
    if kind == "tri":
        return 2 * np.abs(2 * (ph - np.floor(ph + 0.5))) - 1
    raise ValueError(kind)


def modal(excite, modes):
    """Modal synthesis: the excitation through a bank of decaying resonances.
    modes: list of (freq Hz, decay seconds (t60-ish), gain)."""
    out = np.zeros(len(excite))
    for f, t60, g in modes:
        if f >= SR * 0.45:
            continue
        r = np.exp(-6.91 / (t60 * SR))
        w = 2 * np.pi * f / SR
        b = [g * np.sin(w) * 0.25]
        a = [1, -2 * r * np.cos(w), r * r]
        out += signal.lfilter(b, a, excite)
    return out


def impulse(seconds, width=0.0005, rng=None):
    """A short click (a noise burst of `width`) at the start of `seconds` of silence."""
    x = silence(seconds)
    n = max(1, int(width * SR))
    if rng is None:
        x[:n] = np.hanning(n * 2)[n:]
    else:
        x[:n] = rng.standard_normal(n) * np.linspace(1, 0, n)
    return x


def karplus(rng, f0, seconds, damping=0.996, bright=0.5):
    """Karplus-Strong plucked string."""
    n = int(round(seconds * SR))
    period = SR / f0
    p = int(period)
    frac = period - p
    buf = rng.uniform(-1, 1, p + 2)
    buf = lowpass(buf, 2000 + 8000 * bright, 1)
    out = np.zeros(n)
    idx = 0
    line = list(buf)
    L = len(line)
    for i in range(n):
        a = line[idx % L]
        b = line[(idx + 1) % L]
        v = (1 - frac) * a + frac * b
        out[i] = v
        line[idx % L] = damping * 0.5 * (a + b)
        idx += 1
    return out


def grains(rng, seconds, rate, make, density_curve=None, jitter=True):
    """Scatters grains made by make(rng) over `seconds`, `rate` a second,
    optionally thinned by density_curve(u) in [0, 1]."""
    out = silence(seconds + 0.5)
    t = 0.0
    while True:
        t += rng.exponential(1 / rate) if jitter else 1 / rate
        if t >= seconds:
            break
        if density_curve is not None and rng.uniform() > density_curve(t / seconds):
            continue
        g = make(rng)
        out = place(out, g, t)
    return out[:int((seconds + 0.5) * SR)]


def click_grain(rng, lo, hi, dur=(0.002, 0.012), gain=(0.3, 1.0)):
    """A tiny resonant click: a decaying sine somewhere in [lo, hi] Hz."""
    d = rng.uniform(*dur)
    f = np.exp(rng.uniform(np.log(lo), np.log(hi)))
    t = t_axis(d * 4)
    return np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * np.exp(-t / d) * rng.uniform(*gain)


def reverb_ir(rng, seconds, decay=None, damp=6000, early=True, stereo=False):
    """A synthetic impulse response: early reflections and an exponentially
    decaying, darkening noise tail."""
    decay = decay or seconds / 3.0
    n = int(seconds * SR)
    chans = 2 if stereo else 1
    out = np.zeros((n, chans))
    t = t_axis(seconds)
    for c in range(chans):
        tail = rng.standard_normal(n) * np.exp(-t / decay)
        # Darker as it decays: a crossfade from bright to low-passed.
        dark = lowpass(tail, damp * 0.35, 1)
        mix = np.clip(t / seconds, 0, 1)
        tail = tail * (1 - mix) + dark * mix
        tail = lowpass(tail, damp, 1)
        if early:
            for _ in range(8):
                i = int(rng.uniform(0.004, 0.06) * SR)
                if i < n:
                    tail[i] += rng.uniform(0.3, 0.9) * rng.choice([-1, 1])
        out[:, c] = tail
    out /= np.sqrt(np.sum(out ** 2) / chans) + 1e-12
    return out if stereo else out[:, 0]


def convolve(x, ir, wet=0.3):
    """Dry/wet convolution reverb (mono x, mono or stereo ir)."""
    if ir.ndim == 1:
        w = signal.fftconvolve(x, ir)[:len(x) + len(ir) - 1]
        y = np.zeros(len(w))
        y[:len(x)] += x * (1 - wet)
        return y + w * wet * 0.5
    chans = []
    for c in range(ir.shape[1]):
        w = signal.fftconvolve(x, ir[:, c])
        y = np.zeros(len(w))
        y[:len(x)] += x * (1 - wet)
        chans.append(y + w * wet * 0.5)
    return np.stack(chans, axis=1)


def trim_tail(x, threshold_db=-60, keep=0.02):
    """Cuts the silent tail below threshold (relative to peak)."""
    mono = np.abs(x if x.ndim == 1 else x.max(axis=1))
    peak = mono.max() + 1e-12
    above = np.nonzero(mono > peak * 10 ** (threshold_db / 20))[0]
    end = min(len(x), (above[-1] if len(above) else 0) + int(keep * SR))
    return fade(x[:end], 0.0, min(0.03, end / SR / 4))


def saturate(x, drive=2.0):
    return np.tanh(x * drive) / np.tanh(drive)


def pan(x, p):
    """Mono to stereo, equal power, p in [-1, 1]."""
    a = (p + 1) * np.pi / 4
    return np.stack([x * np.cos(a), x * np.sin(a)], axis=1)


def loopify(x, cross=1.0):
    """A seamless loop: the end crossfaded (equal power) into the start."""
    n = int(cross * SR)
    body = x[:-n].copy()
    tail = x[-n:]
    w = np.linspace(0, 1, n)
    fi, fo = np.sqrt(w), np.sqrt(1 - w)
    if x.ndim == 2:
        fi, fo = fi[:, None], fo[:, None]
    body[:n] = body[:n] * fi + tail * fo
    return body


# Loudness (ITU-R BS.1770 K-weighting, gated integrated loudness).
def _kweight(x):
    # Stage 1: high shelf +4 dB above ~1.7 kHz; stage 2: RLB high-pass at 38 Hz.
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]
    a1 = [1.0, -1.69065929318241, 0.73248077421585]
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, -1.99004745483398, 0.99007225036621]
    y = signal.lfilter(b1, a1, x, axis=0)
    return signal.lfilter(b2, a2, y, axis=0)


def loudness(x):
    """Integrated loudness in LUFS (short sounds: over the whole sound)."""
    y = _kweight(x)
    if y.ndim == 1:
        y = y[:, None]
    block, hop = int(0.4 * SR), int(0.1 * SR)
    if len(y) < block:
        ms = np.mean(np.sum(y ** 2, axis=1))
        return -0.691 + 10 * np.log10(ms + 1e-12)
    zs = np.array([np.mean(np.sum(y[i:i + block] ** 2, axis=1)) for i in range(0, len(y) - block + 1, hop)])
    ls = -0.691 + 10 * np.log10(zs + 1e-12)
    zs = zs[ls > -70]
    if not len(zs):
        return -70.0
    rel = -0.691 + 10 * np.log10(np.mean(zs) + 1e-12) - 10
    zs2 = zs[(-0.691 + 10 * np.log10(zs + 1e-12)) > rel]
    return -0.691 + 10 * np.log10(np.mean(zs2 if len(zs2) else zs) + 1e-12)


def normalise(x, target_lufs, peak_db=-1.0, limit_db=6.0):
    """Scales to the target loudness; transients that would go past the peak
    ceiling are softly limited (up to limit_db of it), and beyond that the
    whole sound is turned down instead."""
    x = x - np.mean(x, axis=0)
    l = loudness(x)
    g = 10 ** ((target_lufs - l) / 20)
    ceiling = 10 ** (peak_db / 20)
    peak = np.max(np.abs(x)) * g
    over = ceiling * 10 ** (limit_db / 20)
    if peak > over:
        g *= over / peak
    y = x * g
    if peak > ceiling * 0.5:
        # A soft knee: linear well below the ceiling, rounding off into it.
        y = ceiling * np.tanh(y / ceiling)
    return y
