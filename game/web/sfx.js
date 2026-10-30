// The sound effects: plays the cues game/src/sfx.cpp sends (through main.cpp)
// from the files listed in sfx/sounds.json (rendered by tools/sfx/synth, or
// recordings dropped in their place), and fades the ambience loops to the
// levels the game works out. Positional: panned, quieter, duller and more
// reverberant with distance; arrows passing near swoop in pitch. At most
// kMaxSounds at once from a pool of reused channel strips, the gravest cues
// taking the place of the slightest. Files load lazily, one sound's variants
// the first time it is asked for. Shares the AudioContext with the voices
// (audio.js). A missing file plays a tiny stand-in blip so nothing is silent.
(function () {
  "use strict";
  const kMaxSounds = 12;
  const kLogSize = 400;
  const settingsKey = "illyria.sfx.v1";
  let version = "";
  try { version = new URL(document.currentScript.src).searchParams.get("v") || ""; } catch (e) {}

  const settings = { on: true, volume: 0.8, ambience: true, ambienceVolume: 1 };
  try {
    const saved = JSON.parse(localStorage.getItem(settingsKey));
    if (saved && typeof saved.on === "boolean") Object.assign(settings, saved);
  } catch (e) { /* private mode: defaults */ }
  function save() { try { localStorage.setItem(settingsKey, JSON.stringify(settings)); } catch (e) {} }

  let manifest = null, loading = null;
  const sounds = new Map();     // id -> manifest entry
  let stats = {};               // file -> {seconds, loop_start, loop_seconds, bytes}
  const buffers = new Map();    // file -> AudioBuffer | null while loading
  const failed = new Set();
  const lastVariant = new Map();
  const log = [];
  let ctx = null, master = null, sfxBus = null, ambBus = null, reverb = null, blip = null;
  const strips = [];
  // The sounds a battle plays most, fetched as soon as audio is allowed.
  const kCore = ["snd_weap_arrow_bowstring_release", "snd_weap_sword_swing", "snd_weap_sword_flesh", "snd_weap_sword_armor",
                 "snd_impact_arrow_dirt", "snd_impact_arrow_flesh", "snd_impact_arrow_shield", "snd_gore_body_fall",
                 "snd_weap_block_shield", "snd_ui_click", "snd_ui_page_turn"];

  function note(entry) {
    entry.t = +(performance.now() / 1000).toFixed(2);
    log.push(entry);
    if (log.length > kLogSize) log.shift();
  }

  function loadManifest() {
    if (loading) return loading;
    const v = encodeURIComponent(version);
    loading = Promise.all([
      fetch(`sfx/sounds.json?v=${v}`).then((r) => r.json()),
      fetch(`sfx/stats.json?v=${v}`).then((r) => (r.ok ? r.json() : {})).catch(() => ({})),
    ]).then(([m, s]) => {
      manifest = m;
      stats = s;
      for (const x of m.sounds) sounds.set(x.id, x);
    }).catch((e) => { console.warn("sfx: no manifest", e); });
    return loading;
  }

  // The mix: strips -> sfx bus -> master; ambience -> its bus -> master;
  // every strip also sends to one shared reverb (more the farther it is).
  function build(c) {
    if (ctx) return;
    ctx = c;
    master = ctx.createGain();
    const limiter = ctx.createDynamicsCompressor();
    limiter.threshold.value = -8; limiter.knee.value = 6; limiter.ratio.value = 8;
    limiter.attack.value = 0.003; limiter.release.value = 0.2;
    master.connect(limiter); limiter.connect((window.IllyriaAudio && window.IllyriaAudio.output()) || ctx.destination);
    sfxBus = ctx.createGain(); sfxBus.connect(master);
    ambBus = ctx.createGain(); ambBus.connect(master);
    reverb = ctx.createConvolver();
    reverb.buffer = impulse(2.2);
    const wet = ctx.createGain(); wet.gain.value = 0.55;
    reverb.connect(wet); wet.connect(sfxBus);
    for (let i = 0; i < kMaxSounds; i++) strips.push(makeStrip());
    applyVolume();
  }

  // A synthetic outdoor impulse response: decaying, darkening noise.
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

  function makeStrip() {
    const filter = ctx.createBiquadFilter();
    filter.type = "lowpass";
    const gain = ctx.createGain();
    const panner = ctx.createStereoPanner ? ctx.createStereoPanner() : null;
    const send = ctx.createGain();
    filter.connect(gain);
    if (panner) { gain.connect(panner); panner.connect(sfxBus); } else gain.connect(sfxBus);
    gain.connect(send); send.connect(reverb);
    return { filter, gain, panner, send, end: 0, priority: -1, src: null };
  }

  function applyVolume() {
    if (!master) return;
    master.gain.value = settings.on ? settings.volume : 0;
    ambBus.gain.setTargetAtTime(settings.on && settings.ambience ? settings.ambienceVolume : 0, ctx.currentTime, 0.3);
  }

  function unlock(c) {
    if (!settings.on) return;
    build(c);
    loadManifest().then(() => kCore.forEach(loadSound));
  }
  if (window.IllyriaAudio) window.IllyriaAudio.onUnlock(unlock);

  function fetchBuffer(file) {
    if (buffers.has(file) || failed.has(file) || !ctx) return;
    buffers.set(file, null);
    fetch(`${file}?v=${encodeURIComponent(version)}`)
      .then((r) => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); })
      .then((data) => new Promise((ok, bad) => ctx.decodeAudioData(data, ok, bad)))
      .then((buf) => buffers.set(file, buf))
      .catch(() => { buffers.delete(file); failed.add(file); });
  }
  function loadSound(id) {
    const s = sounds.get(id);
    if (s) s.files.forEach(fetchBuffer);
  }

  function pickVariant(s) {
    const ready = [];
    s.files.forEach((f, i) => { if (buffers.get(f)) ready.push(i); });
    if (!ready.length) return -1;
    const last = lastVariant.has(s.id) ? lastVariant.get(s.id) : -1;
    let i = ready[Math.floor(Math.random() * ready.length)];
    if (ready.length > 1 && i === last) i = ready[(ready.indexOf(i) + 1) % ready.length];
    lastVariant.set(s.id, i);
    return i;
  }

  // A free strip, or the slightest one playing if this cue outranks it.
  function strip(priority, now) {
    let best = null;
    for (const s of strips) if (s.end <= now) return s;
    for (const s of strips) if (!best || s.priority < best.priority || (s.priority === best.priority && s.end < best.end)) best = s;
    if (best.priority > priority) return null;
    try { best.src.stop(); } catch (e) {}
    return best;
  }

  function dB(x) { return Math.pow(10, x / 20); }

  // Plays a sound through a strip: pan -1..1, dist 0 (near) .. 1 (the edge of hearing).
  function play(s, variant, o) {
    const now = ctx.currentTime;
    const st = strip(o.priority || 0, now);
    if (!st) return "dropped: busy";
    const file = variant >= 0 ? s.files[variant] : null;
    const buf = file ? buffers.get(file) : blipBuffer();
    const src = ctx.createBufferSource();
    src.buffer = buf;
    const when = now + Math.max(0, o.delay || 0);
    let rate = 1 + (Math.random() * 2 - 1) * (s.id.indexOf("bowstring") >= 0 ? 0.05 : 0.035);
    if (o.doppler) {
      // An arrow passing: higher while it closes, sliding down as it goes by.
      const k = Math.max(-0.35, Math.min(0.35, o.doppler / 120));
      src.playbackRate.setValueAtTime(rate * (1 + k), when);
      src.playbackRate.linearRampToValueAtTime(rate * (1 - k * 0.8), when + buf.duration * 0.9);
    } else {
      src.playbackRate.value = rate;
    }
    const dist = Math.max(0, Math.min(1, o.dist || 0));
    st.filter.frequency.setValueAtTime(800 + 17000 * Math.pow(1 - dist, 2), when);
    st.gain.gain.setValueAtTime((o.gain == null ? 1 : o.gain) * dB(s.level || 0), when);
    st.send.gain.setValueAtTime(0.08 + 0.55 * dist, when);
    if (st.panner) st.panner.pan.setValueAtTime(Math.max(-1, Math.min(1, o.pan || 0)), when);
    src.connect(st.filter);
    src.start(when);
    st.src = src;
    st.priority = o.priority || 0;
    st.end = when + buf.duration / rate + 0.05;
    return file ? "file" : "fallback";
  }

  // A tiny stand-in for a file that could not be had.
  function blipBuffer() {
    if (blip) return blip;
    const n = Math.floor(ctx.sampleRate * 0.06);
    blip = ctx.createBuffer(1, n, ctx.sampleRate);
    const d = blip.getChannelData(0);
    for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * Math.exp(-i / (n / 6)) * 0.3;
    return blip;
  }

  // From the engine: one cue (SfxCue in game/include/illyria/game/sfx.h).
  function cue(id, gain, pan, dist, delay, doppler, priority, count, tick) {
    const c = { id, gain: +gain.toFixed(2), pan: +pan.toFixed(2), dist: +dist.toFixed(2), delay, doppler, priority, count, tick };
    if (!settings.on) { note({ ...c, mode: "off" }); return; }
    if (!ctx) { note({ ...c, mode: "dropped: no audio yet" }); return; }
    if (!manifest) { loadManifest(); note({ ...c, mode: "dropped: loading" }); return; }
    const s = sounds.get(id);
    if (!s) { note({ ...c, mode: "dropped: unknown" }); return; }
    if (gain * settings.volume < 0.01) { note({ ...c, mode: "dropped: too quiet" }); return; }
    loadSound(id);
    const v = pickVariant(s);
    if (v < 0 && !s.files.every((f) => failed.has(f))) { note({ ...c, mode: "dropped: loading" }); return; }
    const mode = play(s, v, c);
    note({ ...c, variant: v + 1, mode });
  }

  // The ambience loops, faded to the levels the game sends (sfx.h Ambience).
  const layers = new Map();  // id -> {gain, panner, src, level}
  function layer(id) {
    if (layers.has(id)) return layers.get(id);
    const g = ctx.createGain(); g.gain.value = 0;
    const p = ctx.createStereoPanner ? ctx.createStereoPanner() : null;
    if (p) { g.connect(p); p.connect(ambBus); } else g.connect(ambBus);
    const l = { gain: g, panner: p, src: null, level: 0 };
    layers.set(id, l);
    return l;
  }
  function setLayer(id, level, panTo) {
    if (!ctx || !manifest) return;
    const s = sounds.get(id);
    if (!s) return;
    const l = layer(id);
    if (level > 0.01 && settings.ambience && settings.on) {
      loadSound(id);
      const file = s.files[0];
      const buf = buffers.get(file);
      if (buf && !l.src) {
        const src = ctx.createBufferSource();
        src.buffer = buf;
        src.loop = true;
        const st = stats[file];
        if (st && st.loop_seconds) {
          src.loopStart = st.loop_start;
          src.loopEnd = Math.min(buf.duration, st.loop_start + st.loop_seconds);
        }
        src.connect(l.gain);
        src.start(0, st && st.loop_start ? st.loop_start : 0);
        l.src = src;
      }
    }
    l.level = level;
    l.gain.gain.setTargetAtTime(level * dB(s.level || 0) * 4, ctx.currentTime, 1.2);
    if (l.panner && panTo != null) l.panner.pan.setTargetAtTime(panTo, ctx.currentTime, 1.0);
    // Silent long enough: stop it, to spare a phone the work.
    if (level <= 0.01 && l.src) {
      clearTimeout(l.stopTimer);
      l.stopTimer = setTimeout(() => { if (l.level <= 0.01 && l.src) { try { l.src.stop(); } catch (e) {} l.src = null; } }, 6000);
    }
  }
  let lastAmbience = null;
  function ambience(windMountain, windValley, swamp, swampPan, flies, fliesPan, drums, drumsPan, rain) {
    lastAmbience = { windMountain, windValley, swamp, swampPan, flies, fliesPan, drums, drumsPan, rain };
    if (!ctx || !manifest) { if (settings.on) loadManifest(); return; }
    setLayer("snd_amb_wind_mountain", windMountain, 0);
    setLayer("snd_amb_wind_valley", windValley, 0);
    setLayer("snd_amb_swamp_insects", swamp, swampPan);
    setLayer("snd_amb_carrion_flies", flies, fliesPan);
    setLayer("snd_amb_war_drums", drums, drumsPan);
    setLayer("snd_amb_rain_light", rain > 0 ? Math.min(1, rain / 0.5) * (rain < 0.6 ? 1 : 1 - (rain - 0.6) / 0.4 * 0.7) : 0, 0);
    setLayer("snd_amb_rain_heavy", Math.max(0, (rain - 0.45) / 0.55), 0);
  }
  function stopAmbience() { for (const id of layers.keys()) setLayer(id, 0, 0); }

  // The page's own sounds: taps and panels.
  function ui(id) {
    if (!settings.on || !ctx || !manifest) return;
    const s = sounds.get(id);
    if (!s) return;
    loadSound(id);
    const v = pickVariant(s);
    if (v >= 0) play(s, v, { gain: 1, priority: 1 });
  }
  document.addEventListener("click", (e) => { if (e.target.closest && e.target.closest("button")) ui("snd_ui_click"); }, { passive: true });
  function watchPanels() {
    const seen = new WeakMap();
    const obs = new MutationObserver((list) => {
      for (const m of list) {
        const el = m.target;
        const shown = !el.classList.contains("hidden");
        if (shown && seen.get(el) === false) ui("snd_ui_page_turn");
        seen.set(el, shown);
      }
    });
    for (const el of document.querySelectorAll(".popup, #cityPanel, #levelPrompt")) {
      seen.set(el, !el.classList.contains("hidden"));
      obs.observe(el, { attributes: true, attributeFilter: ["class"] });
    }
  }

  // The ⚙ panel's controls.
  function bind() {
    watchPanels();
    const on = document.getElementById("sfxOn"), vol = document.getElementById("sfxVolume"), amb = document.getElementById("ambienceOn");
    const ambVol = document.getElementById("ambienceVolume");
    if (!on || !vol || !amb) return;
    const show = () => {
      on.checked = settings.on;
      vol.value = Math.round(settings.volume * 100);
      amb.checked = settings.ambience;
      vol.disabled = amb.disabled = !settings.on;
      if (ambVol) { ambVol.value = Math.round(settings.ambienceVolume * 100); ambVol.disabled = !settings.on || !settings.ambience; }
    };
    if (ambVol) ambVol.oninput = () => { settings.ambienceVolume = +ambVol.value / 100; applyVolume(); save(); };
    on.onchange = () => {
      settings.on = on.checked;
      if (settings.on && window.IllyriaAudio) { window.IllyriaAudio.unlock(); window.IllyriaAudio.onUnlock(unlock); }
      applyVolume(); save(); show();
      if (settings.on && lastAmbience) { const a = lastAmbience; ambience(a.windMountain, a.windValley, a.swamp, a.swampPan, a.flies, a.fliesPan, a.drums, a.drumsPan, a.rain); }
    };
    vol.oninput = () => { settings.volume = +vol.value / 100; applyVolume(); save(); };
    amb.onchange = () => { settings.ambience = amb.checked; applyVolume(); save(); show(); if (!amb.checked) stopAmbience(); };
    show();
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", bind);
  else bind();

  window.IllyriaSfx = {
    cue,
    ambience,
    ui,
    settings,
    log,
    // Test hook: the last n played or dropped cues.
    recent: (n) => log.slice(-(n || 20)),
    load: loadManifest,
    // The sound test page (sfx-test.html): play one variant (-1: any) through the game's chain.
    audition(id, variant, o) {
      if (window.IllyriaAudio) window.IllyriaAudio.unlock();
      return loadManifest().then(() => new Promise((done) => {
        if (window.IllyriaAudio && !ctx) window.IllyriaAudio.onUnlock(unlock);
        const s = sounds.get(id);
        if (!s || !ctx) return done("no audio");
        loadSound(id);
        let tries = 0;
        const go = () => {
          const want = variant >= 0 ? s.files[variant] : null;
          const ready = want ? buffers.get(want) : s.files.some((f) => buffers.get(f));
          if (!ready && ++tries < 60 && !(want && failed.has(want))) return setTimeout(go, 100);
          const v = variant >= 0 ? (buffers.get(want) ? variant : -1) : pickVariant(s);
          done(play(s, v, Object.assign({ gain: 1, priority: 5 }, o || {})));
        };
        go();
      }));
    },
    // The test page's ambience toggles: one loop at a level (0 stops it).
    layer(id, level, panTo) {
      if (window.IllyriaAudio) window.IllyriaAudio.unlock();
      return loadManifest().then(() => {
        if (window.IllyriaAudio && !ctx) window.IllyriaAudio.onUnlock(unlock);
        loadSound(id);
        const tryStart = (n) => {
          setLayer(id, level, panTo || 0);
          const l = layers.get(id);
          if (level > 0 && (!l || !l.src) && n < 60) setTimeout(() => tryStart(n + 1), 100);
        };
        tryStart(0);
      });
    },
    manifest: () => manifest,
    stats: () => stats,
  };
})();
