// The Sound Editor's signal work in the page: loudness (ITU-R BS.1770 K-weighting with the 400 ms gated
// blocks, implemented in full, not an approximation), a look-ahead limiter, trimming, fades, the loop seam,
// WAV writing. What is INSTALLED is rendered by ffmpeg on the server from the same edit; this is the preview
// and the read-outs.
(() => {
  "use strict";
  const D = {};
  D.dB = (v) => 20 * Math.log10(Math.max(v, 1e-9));
  D.lin = (d) => Math.pow(10, d / 20);
  D.TARGET = { oneshot: -18, loop: -24, voice: -16 };

  function biquad(x, b0, b1, b2, a1, a2) {
    const y = new Float64Array(x.length);
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) { const v = x[i], o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = v; y2 = y1; y1 = o; y[i] = o; }
    return y;
  }
  function kweight(x, sr) {   // high shelf (+4 dB, 1500 Hz, Q 0.707) then high pass (38 Hz, Q 0.5)
    const A = Math.pow(10, 4 / 40);
    let w = 2 * Math.PI * 1500 / sr, al = Math.sin(w) / (2 * Math.SQRT1_2), c = Math.cos(w), a0 = (A + 1) - (A - 1) * c + 2 * Math.sqrt(A) * al;
    const s = 2 * Math.sqrt(A) * al;
    const y = biquad(x, A * ((A + 1) + (A - 1) * c + s) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0, A * ((A + 1) + (A - 1) * c - s) / a0, 2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - s) / a0);
    w = 2 * Math.PI * 38 / sr; al = Math.sin(w) / (2 * 0.5); c = Math.cos(w); a0 = 1 + al;
    return biquad(y, (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0);
  }
  D.lufs = (chs, sr) => {
    let n = chs[0].length;
    if (n < sr * 0.005) return NaN;
    const need = Math.ceil(sr * 0.5);
    if (n < need) {             // a short sound: repeated, which gives its ungated level
      const reps = Math.ceil(need / n);
      chs = chs.map((c) => { const t = new Float32Array(n * reps); for (let r = 0; r < reps; r++) t.set(c, r * n); return t; });
      n *= reps;
    }
    const k = chs.map((c) => kweight(c, sr)), B = Math.round(0.4 * sr), H = Math.round(0.1 * sr), z = [];
    for (let i = 0; i + B <= n; i += H) {
      let e = 0;
      for (const ch of k) { let sum = 0; for (let j = i; j < i + B; j++) sum += ch[j] * ch[j]; e += sum / B; }
      z.push(e);
    }
    const L = (v) => -0.691 + 10 * Math.log10(v), mean = (a) => a.reduce((s, v) => s + v, 0) / a.length;
    let g = z.filter((v) => L(v) > -70);
    if (!g.length) return -Infinity;
    const rel = L(mean(g)) - 10;
    g = g.filter((v) => L(v) > rel);
    return g.length ? L(mean(g)) : -Infinity;
  };
  D.peak = (chs) => { let m = 0; for (const c of chs) for (let i = 0; i < c.length; i++) { const a = Math.abs(c[i]); if (a > m) m = a; } return m; };
  D.mono = (chans) => {
    if (chans.length === 1) return chans[0];
    const n = chans[0].length, m = new Float32Array(n);
    for (let i = 0; i < n; i++) m[i] = (chans[0][i] + chans[1][i]) / 2;
    return m;
  };
  // RMS of the quietest tenth of 50 ms blocks: the room and the microphone with nothing happening.
  D.noiseFloor = (m, sr) => {
    const B = Math.round(0.05 * sr), r = [];
    for (let i = 0; i + B <= m.length; i += B) { let s = 0; for (let j = i; j < i + B; j++) s += m[j] * m[j]; r.push(Math.sqrt(s / B)); }
    if (!r.length) return 0;
    r.sort((a, b) => a - b);
    return r[Math.floor(r.length * 0.1)];
  };
  function limit(chs, sr, ceilDb) {   // sliding 2 ms maximum, the gain smoothed over the same window
    const n = chs[0].length, w = Math.max(1, Math.round(0.001 * sr)), c = D.lin(ceilDb), env = new Float32Array(n);
    let need = false;
    for (let i = 0; i < n; i++) { let m = 0; for (const ch of chs) { const a = Math.abs(ch[i]); if (a > m) m = a; } env[i] = m; if (m > c) need = true; }
    if (!need) return;
    const slide = (src, max) => {
      const out = new Float32Array(n), q = new Int32Array(n);
      let h = 0, t = 0;
      for (let r = 0; r < n + w; r++) {
        if (r < n) { while (t > h && (max ? src[q[t - 1]] <= src[r] : src[q[t - 1]] >= src[r])) t--; q[t++] = r; }
        const i = r - w;
        if (i < 0) continue;
        while (q[h] < i - w) h++;
        out[i] = src[q[h]];
      }
      return out;
    };
    const mx = slide(env, true), g = new Float32Array(n);
    for (let i = 0; i < n; i++) g[i] = Math.min(1, c / Math.max(mx[i], 1e-9));
    const mn = slide(g, false), cs = new Float64Array(n + 1);
    for (let i = 0; i < n; i++) cs[i + 1] = cs[i] + mn[i];
    for (let i = 0; i < n; i++) { const a = Math.max(0, i - w), b = Math.min(n, i + w + 1), k = (cs[b] - cs[a]) / (b - a); for (const ch of chs) ch[i] *= k; }
  }

  // Where the sound starts and ends (seconds): above the room's noise, with a little air either side.
  D.autoTrim = (raw, kind) => {
    const m = D.mono(raw.chans), n = m.length, sr = raw.sr;
    if (kind === "loop") { const edge = Math.min(0.2, n / sr / 10); return { t0: edge, t1: n / sr - edge }; }
    let pk = 0;
    for (let i = 0; i < n; i++) if (Math.abs(m[i]) > pk) pk = Math.abs(m[i]);
    const thr = Math.max(pk * D.lin(-45), D.noiseFloor(m, sr) * 6);
    let a = 0, b = n - 1;
    while (a < n && Math.abs(m[a]) <= thr) a++;
    while (b > a && Math.abs(m[b]) <= thr * 0.6) b--;
    if (a >= n) return { t0: 0, t1: n / sr };
    const lead = kind === "voice" ? 0.02 : 0.005, tail = kind === "voice" ? 0.12 : 0.06;
    return { t0: Math.max(0, a / sr - lead), t1: Math.min(n / sr, b / sr + tail) };
  };
  D.defaults = (kind) => ({ fade_in: kind === "voice" ? 10 : 2, fade_out: kind === "loop" ? 5 : 30, gain: 0, target: D.TARGET[kind], channels: kind === "loop" ? 2 : 1, seam: true });

  // The edit applied: {chans, sr, lufs, before, peak, dur, rawPeak, clipped, noise}. kind: oneshot | loop | voice.
  D.build = (raw, edit, kind) => {
    const e = Object.assign(D.defaults(kind), edit || {}), sr = raw.sr, N = raw.chans[0].length;
    let a = Math.round((e.t0 || 0) * sr), b = Math.round((e.t1 == null ? N / sr : e.t1) * sr);
    a = Math.max(0, Math.min(N - 16, a)); b = Math.max(a + 16, Math.min(N, b));
    const src = raw.chans.map((c) => c.subarray(a, b)), stereo = kind !== "voice" && e.channels === 2;
    let out = stereo ? [Float32Array.from(src[0]), Float32Array.from(src[1] || src[0])] : [Float32Array.from(D.mono(src))];
    let n = out[0].length;
    if (kind === "loop" && e.seam && n > sr * 0.6) {      // the end is cross-faded into what came before the start
      const cf = Math.round(Math.min(1, n / sr * 0.15) * sr);
      out = out.map((x) => {
        const body = Float32Array.from(x.subarray(cf)), L = body.length;
        for (let k = 0; k < cf; k++) { const th = k / (cf - 1) * Math.PI / 2; body[L - cf + k] = body[L - cf + k] * Math.cos(th) + x[k] * Math.sin(th); }
        return body;
      });
      n = out[0].length;
    } else {
      const fi = Math.min(Math.round(e.fade_in / 1000 * sr), Math.floor(n / 3)), fo = Math.min(Math.round(e.fade_out / 1000 * sr), Math.floor(n / 3));
      for (const c of out) { for (let i = 0; i < fi; i++) c[i] *= i / fi; for (let i = 0; i < fo; i++) c[n - 1 - i] *= i / fo; }
    }
    const before = D.lufs(out, sr), want = e.target + e.gain;
    let l = before;
    for (let it = 0; it < 4 && isFinite(l) && Math.abs(want - l) > 0.1; it++) {
      const g = D.lin(want - l);
      for (const c of out) for (let i = 0; i < c.length; i++) c[i] *= g;
      limit(out, sr, kind === "voice" ? -1.5 : -1);
      l = D.lufs(out, sr);
    }
    const rawPk = D.peak(src);
    return { chans: out, sr, lufs: l, before, peak: D.dB(D.peak(out)), dur: n / sr, rawPeak: D.dB(rawPk), clipped: rawPk >= 0.99, noise: D.dB(D.noiseFloor(D.mono(raw.chans), sr)) };
  };
  D.info = (p) => ({ dur: +p.dur.toFixed(3), lufs: isFinite(p.lufs) ? +p.lufs.toFixed(1) : null, before: isFinite(p.before) ? +p.before.toFixed(1) : null,
    peak: +p.rawPeak.toFixed(1), noise: +p.noise.toFixed(1), clipped: p.clipped, sr: p.sr });

  // 32-bit float WAV: the recording exactly as captured.
  D.wavFloat = (chans, sr) => {
    const ch = chans.length, n = chans[0].length, buf = new ArrayBuffer(44 + n * ch * 4), v = new DataView(buf);
    const str = (o, s) => { for (let k = 0; k < s.length; k++) v.setUint8(o + k, s.charCodeAt(k)); };
    str(0, "RIFF"); v.setUint32(4, 36 + n * ch * 4, true); str(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 3, true); v.setUint16(22, ch, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * ch * 4, true); v.setUint16(32, ch * 4, true); v.setUint16(34, 32, true); str(36, "data"); v.setUint32(40, n * ch * 4, true);
    let o = 44;
    for (let i = 0; i < n; i++) for (let c = 0; c < ch; c++) { v.setFloat32(o, chans[c][i], true); o += 4; }
    return new Blob([buf], { type: "audio/wav" });
  };
  D.fromBuffer = (buf) => { const chans = []; for (let c = 0; c < Math.min(2, buf.numberOfChannels); c++) chans.push(buf.getChannelData(c)); return { chans, sr: buf.sampleRate }; };
  window.SoundDsp = D;
})();
