// The audit pages' sound: the game's files played through a copy of the game's own chain, so what is heard here is
// what a battle does to a sound. Nothing here is the engine's code; each rule is copied from where the game keeps
// it, and says where:
//   - where a cue is heard from (game/src/sfx.cpp hear): a reach of 160 m + 1.1 x the camera's distance (420 + 1.5 x
//     for the "loud" sounds), gain (1 - off/reach)^2 x a level that the camera's height takes down to a floor (0.22;
//     0.4 loud) by 600 and toward nothing after; under 0.04 it is not played at all;
//   - the voice (game/web/sfx.js play): low-pass and reverb send from the manifest's "mix" block, the mix level, a pan,
//     the pitch 6% and the level 1.5 dB either way;
//   - the pool (sfx.js room): mix.strips at once (24), a full pool fades out the slightest one over mix.fade_ms for an
//     equal or graver cue: never a hard cut;
//   - the bus (sfx.js build): a compressor (-10 dB, 6:1), its make-up taken back, then a clip at -1 dB;
//   - the throttle (sfx.cpp flush): a sound once a tick, its gap in ticks, five cues a tick by priority then gain, and
//     24 a second of real time with a burst of 8 (priority 4 and 5 always);
//   - the ambience (sfx.js setLayer): a loop at level x the mix level x 4, round its loop points;
//   - the voices (game/web/voice.js): a recording, else a rendered placeholder, else the browser's speech
//     synthesiser (the voice picked by a hash of the set from the device's English voices), else for the wordless
//     a band-passed noise growl.
// No sound starts, and no AudioContext is made, before a click: files are decoded for their pictures with an
// OfflineAudioContext, which needs none.
(() => {
  "use strict";
  const kHearing = 160, kLoudHearing = 420, kMinGain = 0.04, kTps = 30, kMaxPerTick = 5, kCuesPerSecond = 24, kBurst = 8;
  let kMaxSounds = 24;
  const mix = { strips: 24, fade_ms: 12, lowpass_near_hz: 18000, lowpass_far_hz: 1400, reverb_near: 0.04, reverb_far: 0.34, loop_gain: 4 };
  const NON_POSITIONAL = new Set(["snd_state_victory_fanfare", "snd_state_defeat_drone", "snd_amb_thunder", "snd_state_war_horn", "snd_state_alarm"]);
  const CAMERAS = [[2, "lowest"], [15, "close"], [60, "sandbox opens"], [120, "a battle opens"], [300, "pulled back"], [583, "Conquest opens"]];

  const A = {
    root: "", manifest: null, stats: {}, audit: null, lines: null, available: new Set(), sounds: new Map(), sets: new Map(),
    settings: { volume: 0.8, mode: "game", camera: 120, off: 0, voiceVolume: 0.8 },
    counters: { played: 0, stolen: 0, faded: 0, busy: 0, quiet: 0, offered: 0, gap: 0, capped: 0 },
    CAMERAS, kHearing, kLoudHearing, kMaxSounds,
  };
  const dB = (x) => Math.pow(10, x / 20);
  const toDb = (g) => (g > 0 ? 20 * Math.log10(g) : -Infinity);

  // ---------------------------------------------------------------- the data
  let loading = null;
  A.load = () => loading || (loading = (async () => {
    // In the repository (tools/audit/, served from its root) or beside the web build (/audit/).
    const roots = location.pathname.includes("/tools/audit") ? ["../../game/web/", "../"] : ["../", "../../game/web/"];
    for (const r of roots) {
      try {
        const res = await fetch(r + "sfx/sounds.json");
        if (res.ok) { A.root = r; A.manifest = await res.json(); break; }
      } catch (e) { /* the next */ }
    }
    if (!A.manifest) throw new Error("sfx/sounds.json not found: serve the repository's root (tools/audit/serve.sh)");
    const json = (url, or) => fetch(url).then((r) => (r.ok ? r.json() : or)).catch(() => or);
    const [stats, lines, available, audit] = await Promise.all([json(A.root + "sfx/stats.json", {}), json(A.root + "voice/lines.json", null),
      json(A.root + "voice/available.json", []), json((window.AUDIT_BASE || "") + "sound-audit.json", null)]);
    A.stats = stats; A.lines = lines; A.available = new Set(available); A.audit = audit;
    for (const s of A.manifest.sounds) A.sounds.set(s.id, s);
    if (lines) for (const s of lines.sets) A.sets.set(s.id, s);
    return A;
  })());
  A.code = (id) => (A.audit && A.audit.code.sounds[id]) || null;
  A.measured = (file) => (A.audit && A.audit.files[file]) || null;
  A.positional = (id) => !NON_POSITIONAL.has(id);
  A.kindOf = (s) => (A.audit && A.audit.code.layers.includes(s.id) ? "loop" : s.emitted_by === "page" ? "ui" : "cue");

  // ---------------------------------------------------------------- decoding (no gesture needed)
  const buffers = new Map();
  let offline = null;
  A.decode = (file) => {
    if (!buffers.has(file)) {
      buffers.set(file, fetch(A.root + file).then((r) => { if (!r.ok) throw new Error(r.status + " " + file); return r.arrayBuffer(); })
        .then((data) => { offline = offline || new OfflineAudioContext(1, 1, 48000); return offline.decodeAudioData(data); }));
    }
    return buffers.get(file);
  };

  // ---------------------------------------------------------------- the game's chain
  let ctx = null, master = null, sfxBus = null, ambBus = null, reverb = null, dry = null, voiceMaster = null;
  const strips = [];
  A.context = () => ctx;
  // Called from a click only.
  A.unlock = () => {
    if (!ctx) {
      ctx = new (window.AudioContext || window.webkitAudioContext)();
      master = ctx.createGain();
      const limiter = ctx.createDynamicsCompressor();
      limiter.threshold.value = -8; limiter.knee.value = 6; limiter.ratio.value = 8; limiter.attack.value = 0.003; limiter.release.value = 0.2;
      master.connect(limiter); limiter.connect(ctx.destination);
      sfxBus = ctx.createGain(); sfxBus.connect(master);
      ambBus = ctx.createGain(); ambBus.connect(master);
      reverb = ctx.createConvolver();
      reverb.buffer = impulse(2.2);
      const wet = ctx.createGain(); wet.gain.value = 0.55;
      reverb.connect(wet); wet.connect(sfxBus);

      dry = ctx.createGain(); dry.connect(ctx.destination);
      voiceMaster = ctx.createGain(); voiceMaster.connect(ctx.destination);  // (voice.js: round the limiter)
      A.applyVolume();
    }
    if (ctx.state === "suspended") ctx.resume();
    return ctx;
  };
  A.applyVolume = () => {
    if (!ctx) return;
    master.gain.value = A.settings.volume;
    dry.gain.value = A.settings.volume;
    voiceMaster.gain.value = A.settings.voiceVolume * (A.settings.volume / 0.8);
  };
  function impulse(seconds) {
    const n = Math.floor(ctx.sampleRate * seconds), b = ctx.createBuffer(2, n, ctx.sampleRate);
    for (let ch = 0; ch < 2; ch++) {
      const d = b.getChannelData(ch);
      let lp = 0;
      for (let i = 0; i < n; i++) {
        const t = i / n, k = 0.15 + 0.8 * t;
        lp += (Math.random() * 2 - 1 - lp) * (1 - k);
        d[i] = lp * Math.pow(1 - t, 3) * 0.6;
      }
    }
    return b;
  }
  // sfx.js room: the sounds playing now; a full pool fades the slightest (the one ending soonest among those) for an
  // equal or graver cue. `stolen` counts hard cuts (none now), `faded` those faded out to make room.
  function room(priority, now) {
    if (A.manifest && A.manifest.mix) { Object.assign(mix, A.manifest.mix); kMaxSounds = mix.strips; }
    for (let i = strips.length - 1; i >= 0; i--) if (strips[i].end <= now) strips.splice(i, 1);
    if (strips.length < kMaxSounds) return true;
    let low = 0;
    for (let i = 1; i < strips.length; i++) if (strips[i].priority < strips[low].priority || (strips[i].priority === strips[low].priority && strips[i].end < strips[low].end)) low = i;
    if (strips[low].priority > priority || (strips[low].priority === priority && strips[low].end - now > 0.25)) return false;
    const v = strips[low];
    try { v.gain.gain.cancelScheduledValues(now); v.gain.gain.setTargetAtTime(0, now, mix.fade_ms / 3000); v.src.stop(now + mix.fade_ms * 1.5 / 1000); } catch (e) { /* ended */ }
    strips.splice(low, 1);
    ++A.counters.faded;
    return true;
  }
  A.stripsBusy = () => (ctx ? strips.filter((s) => s.end > ctx.currentTime).length : 0);

  // sfx.cpp place: a cue `off` metres from the view centre, the camera `camera` from it.
  A.place = (id, camera, off) => {
    if (!A.positional(id)) return { gain: 1, dist: 0, pan: 0, positional: false, dropped: false, hearing: 0 };
    const c = A.code(id), loud = !!(c && c.loud), horiz = Math.abs(off);
    const reach = (loud ? kLoudHearing : kHearing) + camera * (loud ? 1.5 : 1.1);
    const lat = Math.min(1, Math.max(0, 1 - horiz / reach)), up = Math.pow(Math.min(1, Math.max(0, (camera - 60) / 540)), 0.7);
    let level = 1 - (1 - (loud ? 0.4 : 0.22)) * up;
    if (camera > 600) level *= 1 - Math.min(1, (camera - 600) / (loud ? 3000 : 900)) * (loud ? 0.6 : 0.85);
    const gain = lat * lat * level, pan = Math.max(-0.9, Math.min(0.9, off / Math.max(camera * 0.6, 12)));
    return { gain, dist: Math.min(1, Math.max(horiz / reach, up * 0.75)), pan, positional: true, dropped: gain < kMinGain, hearing: reach };
  };
  // What the strip does at that distance, in words.
  A.describe = (p, level) => {
    if (!p.positional) return "not positional: full gain at any camera";
    if (p.dropped) return `out of earshot (gain ${p.gain.toFixed(3)} is under ${kMinGain}): the game does not play it`;
    return `${toDb(p.gain * dB(level || 0)).toFixed(1)} dB, low-pass ${Math.round(mix.lowpass_far_hz + (mix.lowpass_near_hz - mix.lowpass_far_hz) * Math.pow(1 - p.dist, 2))} Hz, reverb send ${(mix.reverb_near + (mix.reverb_far - mix.reverb_near) * p.dist).toFixed(2)}${p.pan ? `, pan ${p.pan.toFixed(2)}` : ""}`;
  };

  // sfx.js play: one buffer through a strip.
  function playStrip(s, buf, o) {
    const now = ctx.currentTime;
    if (!room(o.priority || 0, now)) { ++A.counters.busy; return null; }
    const src = ctx.createBufferSource();
    src.buffer = buf;
    const when = now + Math.max(0, o.delay || 0);
    const rate = 1 + (Math.random() * 2 - 1) * (s.id.indexOf("bowstring") >= 0 ? 0.08 : 0.06);
    src.playbackRate.value = rate;
    const dist = Math.max(0, Math.min(1, o.dist || 0));
    const filter = ctx.createBiquadFilter(), gain = ctx.createGain(), panner = ctx.createStereoPanner(), send = ctx.createGain();
    filter.type = "lowpass";
    filter.frequency.value = mix.lowpass_far_hz + (mix.lowpass_near_hz - mix.lowpass_far_hz) * Math.pow(1 - dist, 2);
    gain.gain.value = (o.gain == null ? 1 : o.gain) * dB(s.level || 0) * dB((Math.random() * 2 - 1) * 1.5);
    send.gain.value = mix.reverb_near + (mix.reverb_far - mix.reverb_near) * dist;
    panner.pan.value = Math.max(-1, Math.min(1, o.pan || 0));
    src.connect(filter); filter.connect(gain); gain.connect(panner); panner.connect(sfxBus);
    gain.connect(send); send.connect(reverb);
    src.start(when);
    strips.push({ src, gain, priority: o.priority || 0, end: when + buf.duration / rate + 0.05 });
    ++A.counters.played;
    return { src, seconds: buf.duration / rate + (o.delay || 0) };
  }
  const drySources = new Set();
  function playDry(buf, loop, loopAt) {
    const src = ctx.createBufferSource();
    src.buffer = buf;
    if (loop) { src.loop = true; if (loopAt) { src.loopStart = loopAt[0]; src.loopEnd = loopAt[1]; } }
    src.connect(dry);
    src.start(0, loop && loopAt ? loopAt[0] : 0);
    drySources.add(src);
    src.onended = () => drySources.delete(src);
    return { src, seconds: buf.duration };
  }
  const lastVariant = new Map();
  A.pickVariant = (s) => {  // sfx.js pickVariant: any but the last
    const n = s.files.length, last = lastVariant.has(s.id) ? lastVariant.get(s.id) : -1;
    let i = Math.floor(Math.random() * n);
    if (n > 1 && i === last) i = (i + 1) % n;
    lastVariant.set(s.id, i);
    return i;
  };
  // One sound, as the page is set (the game's chain at the chosen camera, or dry), or as `o` says:
  // {mode, camera, off, delay, variant}. Resolves to what happened: {mode, text, seconds} (seconds 0: nothing played).
  A.play = async (id, variant, o) => {
    o = o || {};
    A.unlock();
    const s = A.sounds.get(id);
    if (!s) return { mode: "unknown", text: "no such sound", seconds: 0 };
    const v = variant >= 0 ? variant : A.pickVariant(s);
    let buf;
    try { buf = await A.decode(s.files[v]); } catch (e) { return { mode: "missing", text: "file missing: " + s.files[v], seconds: 0 }; }
    const mode = o.mode || A.settings.mode;
    if (mode === "dry") { const p = playDry(buf); return { mode, variant: v, text: "dry: the file as it is", seconds: p.seconds }; }
    const place = A.place(id, o.camera == null ? A.settings.camera : o.camera, o.off == null ? A.settings.off : o.off);
    const text = A.describe(place, s.level);
    if (place.dropped) { ++A.counters.quiet; return { mode: "dropped", variant: v, text, seconds: 0 }; }
    const c = A.code(id), p = playStrip(s, buf, { gain: place.gain * (o.boost || 1), dist: place.dist, pan: place.pan, delay: o.delay, priority: o.priority != null ? o.priority : c ? c.priority : 5 });
    return p ? { mode, variant: v, text, seconds: p.seconds } : { mode: "busy", variant: v, text: "dropped: the 12 strips are busy with graver sounds", seconds: 0 };
  };

  // For the Sound Editor (tools/sound-editor): a buffer that is not in the game yet, played as sound `id` would be
  // (its mix level, priority and reach), or dry; and a file's decoded copy dropped after the file was replaced.
  A.playBuffer = (id, buf, o) => {
    o = o || {};
    A.unlock();
    const s = A.sounds.get(id) || { id, level: 0 }, mode = o.mode || A.settings.mode;
    if (mode === "dry" || !A.sounds.has(id)) { const p = playDry(buf); return { mode: "dry", text: "dry: the sound as it is", seconds: p.seconds }; }
    const place = A.place(id, A.settings.camera, A.settings.off), text = A.describe(place, s.level);
    if (place.dropped) return { mode: "dropped", text, seconds: 0 };
    const c = A.code(id), p = playStrip(s, buf, { gain: place.gain, dist: place.dist, pan: place.pan, priority: c ? c.priority : 5 });
    return p ? { mode, text, seconds: p.seconds } : { mode: "busy", text: "dropped: the strips are busy", seconds: 0 };
  };
  A.forget = (file) => { buffers.delete(file); };

  // sfx.js setLayer: an ambience loop at a level (0 stops it); dry: the file looped as it is.
  const layers = new Map();
  A.layer = async (id, level, mode) => {
    A.unlock();
    const s = A.sounds.get(id);
    let l = layers.get(id);
    if (l) { try { l.src.stop(); } catch (e) { /* ended */ } layers.delete(id); }
    if (!(level > 0) || !s) return null;
    const file = s.files[0], buf = await A.decode(file), st = A.stats[file];
    const loopAt = st && st.loop_seconds ? [st.loop_start, Math.min(buf.duration, st.loop_start + st.loop_seconds)] : null;
    if ((mode || A.settings.mode) === "dry") {
      l = playDry(buf, true, loopAt);
    } else {
      const src = ctx.createBufferSource(), g = ctx.createGain();
      src.buffer = buf; src.loop = true;
      if (loopAt) { src.loopStart = loopAt[0]; src.loopEnd = loopAt[1]; }
      g.gain.value = level * dB(s.level || 0) * mix.loop_gain;
      src.connect(g); g.connect(ambBus);
      src.start(0, loopAt ? loopAt[0] : 0);
      l = { src };
    }
    layers.set(id, l);
    return l;
  };
  A.layerOn = (id) => layers.has(id);
  A.stopAll = () => {
    for (const id of [...layers.keys()]) A.layer(id, 0);
    for (const s of strips) { try { s.src.stop(); } catch (e) { /* none */ } s.end = 0; }
    for (const s of [...drySources]) { try { s.stop(); } catch (e) { /* ended */ } }
    if (window.speechSynthesis) speechSynthesis.cancel();
    stress.stop = true;
  };

  // A fight's worth of cues for `seconds`: every tick each of `ids` is wanted with likelihood `rate` (by 1 to 5 men
  // at once), then the game's throttle (sfx.cpp flush) and the pool. onTick(counters, second) to show it.
  const stress = { stop: true };
  A.stress = async (ids, seconds, rate, onTick) => {
    A.unlock();
    const list = ids.map((id) => A.sounds.get(id)).filter((s) => s && A.code(s.id));
    await Promise.all(list.flatMap((s) => s.files.map((f) => A.decode(f).catch(() => null))));
    Object.keys(A.counters).forEach((k) => { A.counters[k] = 0; });
    stress.stop = false;
    const last = new Map(), t0 = performance.now();
    let tick = 0, budget = kBurst;
    return new Promise((done) => {
      const timer = setInterval(() => {
        const due = Math.floor((performance.now() - t0) / 1000 * kTps);
        for (; tick < due; ++tick) {
          const ready = [];
          for (const s of list) {
            if (Math.random() > rate) continue;
            const c = A.code(s.id), count = 1 + Math.floor(Math.random() * 5 * rate);
            A.counters.offered += count;
            // The loudest of its kind stands for the rest: the nearest of `count` men within 40 m of the view centre.
            let off = 40;
            for (let k = 0; k < count; ++k) off = Math.min(off, Math.random() * 40);
            const p = A.place(s.id, A.settings.camera, off * (Math.random() < 0.5 ? -1 : 1));
            if (p.dropped) { A.counters.quiet += count; continue; }
            if (last.has(s.id) && tick - last.get(s.id) < c.gapTicks) { A.counters.gap += count; continue; }
            ready.push({ s, c, p, count, gain: Math.min(1, p.gain * Math.min(2, 1 + 0.26 * Math.log2(count))) });
          }
          ready.sort((a, b) => (a.c.priority !== b.c.priority ? b.c.priority - a.c.priority : b.gain - a.gain));
          budget = Math.min(kBurst, budget + kCuesPerSecond / kTps);
          ready.forEach((r, k) => {
            if (r.c.priority < 4 && (k >= kMaxPerTick || budget < 1)) { A.counters.capped += r.count; return; }
            if (r.c.priority < 4) budget -= 1;
            last.set(r.s.id, tick);
            A.decode(r.s.files[A.pickVariant(r.s)]).then((b) => playStrip(r.s, b, { gain: r.gain, dist: r.p.dist, pan: r.p.pan, priority: r.c.priority }), () => null);
          });
        }
        const t = (performance.now() - t0) / 1000;
        if (onTick) onTick(A.counters, t);
        if (t >= seconds || stress.stop) { clearInterval(timer); stress.stop = true; done(A.counters); }
      }, 1000 / kTps);
    });
  };

  // ---------------------------------------------------------------- the voices
  // What plays for a line today (voice.js sourceOf, cue).
  A.lineSource = (set, line) => {
    if (A.available.has(line.file)) return { kind: "recorded", file: line.file };
    if (line.placeholder) return { kind: "placeholder", file: line.placeholder };
    return { kind: set.verbal ? "speech" : "growl" };
  };
  A.setCounts = (set) => {
    const n = { lines: 0, recorded: 0, placeholder: 0, speech: 0, growl: 0 };
    for (const s of set.situations) for (const l of s.lines) { ++n.lines; ++n[A.lineSource(set, l).kind]; }
    return n;
  };
  let speechVoices = null, noise = null;
  A.speechVoice = (set) => {
    if (!window.speechSynthesis) return null;
    if (!speechVoices || !speechVoices.length) speechVoices = speechSynthesis.getVoices().filter((v) => /^en/i.test(v.lang));
    if (!speechVoices.length) return null;
    let h = 0;
    for (const ch of set.id) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
    return speechVoices[h % speechVoices.length];
  };
  function speak(set, text) {
    if (!window.speechSynthesis) return "this browser has no speech synthesis";
    const words = text.replace(/\([^)]*\)/g, "").trim();
    if (!words) return "no words to say";
    speechSynthesis.cancel();
    const u = new SpeechSynthesisUtterance(words), v = A.speechVoice(set), ph = set.placeholder || { pitch: 1, rate: 1 };
    if (v) u.voice = v;
    u.pitch = Math.max(0.1, Math.min(2, ph.pitch));
    u.rate = Math.max(0.5, Math.min(2, ph.rate * (0.95 + Math.random() * 0.15)));
    u.volume = Math.max(0, Math.min(1, A.settings.voiceVolume));  // (the game's speech ignores the master volume)
    speechSynthesis.speak(u);
    return `browser speech${v ? ": " + v.name : ""}, pitch ${u.pitch}, rate ${ph.rate}`;
  }
  function growl(set, text) {
    if (!noise) {
      noise = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
      const d = noise.getChannelData(0);
      for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
    }
    const long = /roar|bellow|howl|wail|trumpet|scream|shriek|screech/.test(text), seconds = (long ? 0.7 : 0.35) + Math.random() * 0.2;
    const src = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain();
    src.buffer = noise;
    f.type = "bandpass";
    const base = 220 * (set.placeholder ? set.placeholder.pitch : 1);
    f.frequency.setValueAtTime(base * 1.6, ctx.currentTime);
    f.frequency.exponentialRampToValueAtTime(base * 0.7, ctx.currentTime + seconds);
    f.Q.value = 6;
    g.gain.setValueAtTime(0.0001, ctx.currentTime);
    g.gain.exponentialRampToValueAtTime(2.5, ctx.currentTime + 0.05);
    g.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + seconds);
    src.connect(f); f.connect(g); g.connect(voiceMaster);
    src.start(); src.stop(ctx.currentTime + seconds);
    return `noise growl, ${seconds.toFixed(2)} s about ${Math.round(base)} Hz (the game's stand-in for a wordless voice)`;
  }
  A.playLine = async (set, line) => {
    A.unlock();
    const src = A.lineSource(set, line);
    if (src.file) {
      try {
        const buf = await A.decode(src.file), s = ctx.createBufferSource();
        s.buffer = buf; s.connect(voiceMaster); s.start();
        drySources.add(s); s.onended = () => drySources.delete(s);
        return `${src.kind}: ${src.file}`;
      } catch (e) { return "file missing: " + src.file; }
    }
    return src.kind === "speech" ? speak(set, line.text) : growl(set, line.text);
  };

  // ---------------------------------------------------------------- what is wrong with a sound
  const range = (vals, digits) => {
    const v = vals.filter((x) => typeof x === "number");
    if (!v.length) return "";
    const lo = Math.min(...v), hi = Math.max(...v);
    return lo.toFixed(digits) === hi.toFixed(digits) ? lo.toFixed(digits) : `${lo.toFixed(digits)} to ${hi.toFixed(digits)}`;
  };
  A.range = range;
  A.column = (s, key) => s.files.map((f) => (A.measured(f) || {})[key]).filter((x) => typeof x === "number");
  // [{text, cls: bad | warn | info, key, title}], the gravest first, and a score to sort by.
  A.flags = (s) => {
    const out = [], c = A.code(s.id), col = (k) => A.column(s, k), max = (k) => Math.max(-Infinity, ...col(k)), min = (k) => Math.min(Infinity, ...col(k));
    const listed = (A.audit && A.audit.flags[s.id]) || [], add = (cls, key, text, title) => out.push({ cls, key, text, title: title || "" });
    if (s.emitted_by === "future") add("bad", "never", "never triggered", "The manifest marks it for a feature not built: nothing in the code asks for it.");
    else if (c && !c.asked.length) add("bad", "never", "never triggered", `Sound::${c.enum} is in the enum and kSpecs, and nothing calls want() for it.`);
    else if (listed.includes("unreachable")) add("bad", "never", "unreachable", "Played only by a developer hook: no player can make it happen.");
    if (col("clipped_samples").some((n) => n > 0)) add("bad", "peak", "clips", `${max("clipped_samples")} clipped samples`);
    if (max("pct_lt300") >= 70) add("bad", "bass", `too bassy for phones: ${Math.round(max("pct_lt300"))}% under 300 Hz`, "Small speakers reproduce little below about 300 Hz.");
    if (min("played_lufs_at_gain1") <= -30) add("warn", "quiet", `too quiet as played: ${Math.round(min("played_lufs_at_gain1"))} LUFS`, "Loudness at cue gain 1 with its mix level and the default volume.");
    if (min("lufs_err") < -1.5) add("warn", "quiet", `${(-min("lufs_err")).toFixed(1)} dB under its loudness target`, "");
    if (max("dominant_pct") >= 50) {
      const f = s.files.map((x) => A.measured(x)).filter(Boolean).sort((a, b) => b.dominant_pct - a.dominant_pct)[0];
      add("warn", "tonal", `near a pure tone: ${Math.round(f.dominant_pct)}% at ${Math.round(f.dominant_hz)} Hz`, "");
    }
    if (max("closest_wave_corr") >= 0.6) add("warn", "alike", `variants near copies (${max("closest_wave_corr").toFixed(2)})`, "Waveform correlation between two variants; 1 is identical.");
    if (listed.includes("uneven variants")) add("warn", "quiet", "variants uneven in level", "");
    if (max("true_peak_db") > -1 && !out.some((f) => f.text === "clips")) add("warn", "peak", `peak ${max("true_peak_db").toFixed(2)} dBTP`, "Over the -1 dB ceiling after encoding.");
    if (s.category === "weapons" && max("attack_ms") > 100) add("warn", "late", `peaks late: ${Math.round(max("attack_ms"))} ms in`, "The cue is sent as the blow lands or the missile leaves (sfx.cpp); the sound's level peaks this long after.");
    if (c && c.asked.length && A.audit && !A.audit.code.core.includes(s.id) && s.emitted_by === "game") add("info", "first", "first cue of a session dropped", "Not in sfx.js kCore: its file is fetched when first asked for, and that cue is dropped while it loads.");
    const recorded = A.audit && A.audit.code.recorded.some((r) => r.indexOf(s.id) >= 0);
    add(recorded ? "good" : "info", "synth", recorded ? "recording" : "placeholder synth", recorded ? "" : "Rendered by tools/sfx/synth: not a recording.");
    const weight = { bad: 10, warn: 3, info: 0, good: 0 };
    let score = out.reduce((n, f) => n + weight[f.cls], 0);
    if (out.some((f) => f.key === "never")) score += 30;
    if (max("pct_lt300") > 0) score += max("pct_lt300") / 25;
    return { list: out, score };
  };

  // ---------------------------------------------------------------- pictures
  // The waveform (the loudest of both ways a pixel) and the spectrogram (60 Hz to 16 kHz, log; a line at 300 Hz,
  // under which a phone has nothing; 70 dB of range).
  A.drawWave = (canvas, buf) => {
    const g = canvas.getContext("2d"), w = canvas.width, h = canvas.height, d = buf.getChannelData(0), per = d.length / w;
    g.clearRect(0, 0, w, h);
    g.fillStyle = "#8ab4ff";
    for (let x = 0; x < w; ++x) {
      let lo = 0, hi = 0;
      for (let i = Math.floor(x * per), e = Math.min(d.length, Math.ceil((x + 1) * per)); i < e; ++i) { if (d[i] < lo) lo = d[i]; if (d[i] > hi) hi = d[i]; }
      const y0 = (1 - hi) * h / 2, y1 = (1 - lo) * h / 2;
      g.fillRect(x, y0, 1, Math.max(1, y1 - y0));
    }
    g.fillStyle = "#ffffff30";
    g.fillRect(0, h / 2, w, 1);
  };
  function fft(re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; ++i) {
      let bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { const t = re[i]; re[i] = re[j]; re[j] = t; }
    }
    for (let len = 2; len <= n; len <<= 1) {
      const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
      for (let i = 0; i < n; i += len) {
        let cr = 1, ci = 0;
        for (let k = 0; k < len / 2; ++k) {
          const a = i + k, b = a + len / 2, xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
          re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
          const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
        }
      }
    }
  }
  const HEAT = [[8, 10, 24], [44, 28, 110], [140, 40, 120], [230, 90, 50], [255, 200, 70], [255, 255, 220]];
  A.drawSpec = (canvas, buf) => {
    const g = canvas.getContext("2d"), w = canvas.width, h = canvas.height, d = buf.getChannelData(0), N = 2048, rate = buf.sampleRate;
    const img = g.createImageData(w, h), re = new Float32Array(N), im = new Float32Array(N), win = new Float32Array(N);
    for (let i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));
    const fLo = 60, fHi = Math.min(16000, rate / 2), mags = new Float32Array(N / 2);
    for (let x = 0; x < w; ++x) {
      const at = Math.floor((x + 0.5) / w * d.length - N / 2);
      for (let i = 0; i < N; ++i) { const k = at + i; re[i] = k >= 0 && k < d.length ? d[k] * win[i] : 0; im[i] = 0; }
      fft(re, im);
      for (let i = 0; i < N / 2; ++i) mags[i] = Math.hypot(re[i], im[i]) / (N / 4);
      for (let y = 0; y < h; ++y) {
        const f0 = fLo * Math.pow(fHi / fLo, (h - 1 - y) / h), f1 = fLo * Math.pow(fHi / fLo, (h - y) / h);
        let m = 0;
        for (let b = Math.max(1, Math.floor(f0 * N / rate)), e = Math.max(b, Math.min(N / 2 - 1, Math.ceil(f1 * N / rate))); b <= e; ++b) if (mags[b] > m) m = mags[b];
        const t = Math.min(1, Math.max(0, (20 * Math.log10(m + 1e-9) + 78) / 70)) * (HEAT.length - 1), k = Math.min(HEAT.length - 2, Math.floor(t)), u = t - k, o = (y * w + x) * 4;
        for (let c = 0; c < 3; ++c) img.data[o + c] = HEAT[k][c] + (HEAT[k + 1][c] - HEAT[k][c]) * u;
        img.data[o + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
    const y300 = Math.round(h - 1 - Math.log(300 / fLo) / Math.log(fHi / fLo) * h);
    g.fillStyle = "#ffffffb0";
    for (let x = 0; x < w; x += 6) g.fillRect(x, y300, 3, 1);
  };

  // ---------------------------------------------------------------- a unit's sounds (for the sprite pages)
  // By sim::UnitType: its voice (game/src/voice.cpp), its weapon (engine/sim/src/units.cpp meleeDamageType and
  // the ranged stats), and what it is to strike (sfx.cpp armoured, big; `bleeds` in units.cpp).
  const VOICE = ["soldier", "soldier", "archer", "soldier", "rider", "rider", "dwarf", "rider", "brute", "brute", "beast", "beast", "beast", "beast", "beast",
    "wizard", "beast", "fiend", "fiend", "undead", "archon", "hero", "dwarf", "wizard", "soldier", "berserker", "engine", "beast"];
  const MELEE = { pierce: [3, 5, 10, 14], cleave: [6, 9, 16, 17, 18, 25], blunt: [8, 13, 15, 22, 23] };
  const RANGED = { 2: "arrow", 15: "fireball", 16: "fireball", 22: "molotov", 26: "stone" };
  const ARMOURED = [1, 5, 6, 21, 24, 20], BIG = [8, 9, 13, 16, 17, 18];
  A.unitSounds = (type) => {
    const melee = Object.keys(MELEE).find((k) => MELEE[k].includes(type)) || "slash", ranged = RANGED[type] || null;
    const armoured = ARMOURED.includes(type), big = BIG.includes(type), bone = type === 19;
    const heavy = melee === "blunt" || melee === "cleave";
    // sfx.cpp, a hand blow: the swing, then 70 ms on what it met (by the victim).
    const met = { flesh: melee === "blunt" ? "snd_weap_blunt_crush" : melee === "pierce" ? "snd_weap_pierce_flesh" : "snd_weap_sword_flesh",
      armour: melee === "blunt" ? "snd_weap_blunt_crush" : "snd_weap_sword_armor", bone: "snd_weap_hit_bone", shield: "snd_weap_block_shield" };
    const launch = { arrow: "snd_weap_arrow_bowstring_release", fireball: "snd_weap_fireball_cast", molotov: "snd_weap_satchel_throw", stone: "snd_weap_catapult_release" }[ranged] || null;
    const lands = { arrow: ["snd_impact_arrow_flesh", "snd_impact_arrow_armor", "snd_impact_arrow_shield", "snd_impact_arrow_dirt"], fireball: ["snd_expl_fireball"], molotov: ["snd_expl_fireball"], stone: ["snd_impact_stone_smash"] }[ranged] || [];
    const struck = type === 26 ? ["snd_impact_arrow_wood"] : bone ? ["snd_weap_hit_bone"] : armoured ? ["snd_weap_sword_armor", "snd_impact_arrow_armor"] : ["snd_weap_sword_flesh", "snd_impact_arrow_flesh"];
    const fall = bone ? "snd_gore_bones_collapse" : armoured || big ? "snd_gore_body_fall_armor" : "snd_gore_body_fall";
    return { voice: VOICE[type] || "soldier", melee, ranged, heavy, armoured, big, bone, swing: type === 26 ? null : heavy ? "snd_weap_heavy_swing" : "snd_weap_sword_swing", met, launch, lands, struck, fall,
      heals: type === 23 ? "snd_magic_heal" : null };
  };

  window.AuditSound = A;
})();
