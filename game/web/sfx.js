// The sound effects: plays the cues game/src/sfx.cpp sends (through main.cpp)
// from the files listed in sfx/sounds.json (rendered by tools/sfx/synth, or
// recordings dropped in their place), and fades the ambience loops to the
// levels the game works out. Positional: panned, quieter, duller and more
// reverberant with distance; arrows passing near swoop in pitch. At most
// mix.strips at once, the gravest cues taking the place of the slightest with
// a short fade. The sounds that matter are fetched as soon as audio is
// allowed, the rest soon after; a cue whose file is still coming is held and
// played when it arrives. Shares the AudioContext with the voices
// (audio.js). A missing file plays a tiny stand-in blip so nothing is silent.
(function () {
  "use strict";
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

  // The mix (numbers from sounds.json "mix", the one table both players read): voices -> sfx bus -> master -> limiter;
  // ambience -> its bus -> master; every voice also sends to one shared reverb (more the farther it is). The player's own
  // sounds (snd_ui_*) go by their own bus: no distance filter, no reverb, and they take no channel from the battle.
  const mix = { strips: 24, fade_ms: 12, lowpass_near_hz: 18000, lowpass_far_hz: 1400, reverb_near: 0.04, reverb_far: 0.34, loop_gain: 4 };
  let uiBus = null, paused = false;
  function build(c) {
    if (ctx) return;
    ctx = c;
    master = ctx.createGain();
    // A compressor for the body of the mix (its make-up gain taken back out), then a clip at -1 dB for what its attack lets by.
    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -10; comp.knee.value = 8; comp.ratio.value = 6;
    comp.attack.value = 0.002; comp.release.value = 0.15;
    const trim = ctx.createGain(); trim.gain.value = dB(-2.5);
    const clip = ctx.createWaveShaper();
    const curve = new Float32Array(1025), top = dB(-1), knee = 0.7;
    for (let i = 0; i < 1025; i++) {
      const x = (i - 512) / 512, ax = Math.abs(x);
      const y = ax <= knee ? ax : knee + (top - knee) * Math.tanh((ax - knee) / (top - knee));
      curve[i] = Math.sign(x) * y;
    }
    clip.curve = curve;
    master.connect(comp); comp.connect(trim); trim.connect(clip);
    clip.connect((window.IllyriaAudio && window.IllyriaAudio.output()) || ctx.destination);
    sfxBus = ctx.createGain(); sfxBus.connect(master);
    ambBus = ctx.createGain(); ambBus.connect(master);
    uiBus = ctx.createGain(); uiBus.connect(master);
    reverb = ctx.createConvolver();
    reverb.buffer = impulse(1.8);
    const wet = ctx.createGain(); wet.gain.value = 0.55;
    reverb.connect(wet); wet.connect(sfxBus);
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

  function applyVolume() {
    if (!master) return;
    master.gain.value = settings.on ? settings.volume : 0;
    ambBus.gain.setTargetAtTime(settings.on && settings.ambience ? settings.ambienceVolume * (paused ? 0.35 : 1) : 0, ctx.currentTime, 0.3);
  }

  // What is fetched as soon as audio is allowed (during the title and the deployment): what a battle plays most, what must
  // not be late (the horns, the fanfare, the blasts, the gate) and the player's own sounds. Then, one at a time, the rest.
  function unlock(c) {
    if (!settings.on) return;
    build(c);
    loadManifest().then(() => {
      if (manifest && manifest.mix) Object.assign(mix, manifest.mix);
      kCore.forEach(loadSound);
      for (const x of manifest ? manifest.sounds : []) {
        if (/^snd_(ui|state|expl|conq|move)_|gate_|stone_smash|catapult|fireball|satchel|body_fall|bed_day/.test(x.id)) loadSound(x.id);
      }
      const rest = manifest ? manifest.sounds.filter((x) => x.emitted_by === "game" || x.emitted_by === "hook" || x.emitted_by === "ambience").map((x) => x.id) : [];
      const next = () => { const id = rest.shift(); if (!id) return; loadSound(id); setTimeout(next, 150); };
      setTimeout(next, 1500);
    });
  }
  if (window.IllyriaAudio) window.IllyriaAudio.onUnlock(unlock);

  const arrivals = new Map();  // file -> callbacks waiting for it
  function fetchBuffer(file) {
    if (buffers.has(file) || failed.has(file) || !ctx) return;
    buffers.set(file, null);
    const done = () => { const w = arrivals.get(file); arrivals.delete(file); if (w) w.forEach((f) => f()); };
    fetch(`${file}?v=${encodeURIComponent(version)}`)
      .then((r) => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); })
      .then((data) => new Promise((ok, bad) => ctx.decodeAudioData(data, ok, bad)))
      .then((buf) => { buffers.set(file, buf); done(); })
      .catch(() => { buffers.delete(file); failed.add(file); done(); });
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

  function dB(x) { return Math.pow(10, x / 20); }

  // The sounds playing now: {priority, end, gain, src}. Room for one more, perhaps by fading out the slightest (the one
  // ending soonest among those): never a hard cut.
  const voices = [];
  function room(priority, now) {
    for (let i = voices.length - 1; i >= 0; i--) if (voices[i].end <= now) voices.splice(i, 1);
    if (voices.length < mix.strips) return true;
    let low = 0;
    for (let i = 1; i < voices.length; i++) {
      const v = voices[i], b = voices[low];
      if (v.priority < b.priority || (v.priority === b.priority && v.end < b.end)) low = i;
    }
    if (voices[low].priority > priority) return false;
    fadeOut(voices[low], now, mix.fade_ms / 1000);
    voices.splice(low, 1);
    return true;
  }
  function fadeOut(v, now, seconds) {
    try {
      v.gain.gain.cancelScheduledValues(now);
      v.gain.gain.setTargetAtTime(0, now, seconds / 3);
      v.src.stop(now + seconds * 1.5);
    } catch (e) {}
  }

  // Plays a sound: pan -1..1, dist 0 (near) .. 1 (the edge of hearing). Each has its own little chain, so one stolen
  // fades without touching its successor.
  function play(s, variant, o) {
    const now = ctx.currentTime;
    const isUi = s.id.indexOf("snd_ui_") === 0;
    if (!isUi && !room(o.priority || 0, now)) return "dropped: busy";
    const file = variant >= 0 ? s.files[variant] : null;
    const buf = file ? buffers.get(file) : blipBuffer();
    const src = ctx.createBufferSource();
    src.buffer = buf;
    const when = now + Math.max(0, o.delay || 0);
    const spread = isUi ? 0.02 : s.id.indexOf("bowstring") >= 0 ? 0.08 : /^snd_(state|conq)_/.test(s.id) ? 0 : 0.06;
    let rate = 1 + (Math.random() * 2 - 1) * spread;
    if (o.doppler) {
      // An arrow passing: higher while it closes, sliding down as it goes by.
      const k = Math.max(-0.35, Math.min(0.35, o.doppler / 120));
      src.playbackRate.setValueAtTime(rate * (1 + k), when);
      src.playbackRate.linearRampToValueAtTime(rate * (1 - k * 0.8), when + buf.duration * 0.9);
    } else {
      src.playbackRate.value = rate;
    }
    const level = (o.gain == null ? 1 : o.gain) * dB(s.level || 0) * (spread ? dB((Math.random() * 2 - 1) * 1.5) : 1);
    const gain = ctx.createGain();
    gain.gain.setValueAtTime(level, now);
    if (isUi) {
      src.connect(gain); gain.connect(uiBus);
      src.start(when);
      return file ? "file" : "fallback";
    }
    const dist = Math.max(0, Math.min(1, o.dist || 0));
    const filter = ctx.createBiquadFilter();
    filter.type = "lowpass";
    filter.frequency.value = mix.lowpass_far_hz + (mix.lowpass_near_hz - mix.lowpass_far_hz) * Math.pow(1 - dist, 2);
    const send = ctx.createGain();
    send.gain.value = mix.reverb_near + (mix.reverb_far - mix.reverb_near) * dist;
    src.connect(filter); filter.connect(gain);
    if (ctx.createStereoPanner) {
      const panner = ctx.createStereoPanner();
      panner.pan.value = Math.max(-1, Math.min(1, o.pan || 0));
      gain.connect(panner); panner.connect(sfxBus);
    } else gain.connect(sfxBus);
    gain.connect(send); send.connect(reverb);
    src.start(when);
    voices.push({ priority: o.priority || 0, end: when + buf.duration / rate + 0.05, gain, src });
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

  // A cue whose files are still on their way is held, and played when one arrives if it is still timely: within its own
  // delay and kLate more (the fanfare's 1.2 s is time enough for its file). Never dropped for being first.
  const kLate = 0.4;
  function hold(s, c) {
    const asked = performance.now();
    let done = false;
    const go = () => {
      if (done) return;
      const v = pickVariant(s);
      if (v < 0) {
        if (s.files.every((f) => failed.has(f))) { done = true; note({ ...c, mode: play(s, -1, c) }); }
        return;
      }
      done = true;
      const waited = (performance.now() - asked) / 1000;
      if (paused || waited > (c.delay || 0) + kLate) { note({ ...c, mode: "dropped: late", waited: +waited.toFixed(2) }); return; }
      const mode = play(s, v, { ...c, delay: Math.max(0, (c.delay || 0) - waited) });
      note({ ...c, variant: v + 1, mode: mode === "file" ? "held" : mode, waited: +waited.toFixed(2) });
    };
    for (const f of s.files) {
      if (failed.has(f)) continue;
      if (!arrivals.has(f)) arrivals.set(f, []);
      arrivals.get(f).push(go);
    }
  }

  // From the engine: one cue (SfxCue in game/include/illyria/game/sfx.h).
  const early = [];  // cues that came before the manifest: played when it is read, if still timely
  function cue(id, gain, pan, dist, delay, doppler, priority, count, tick) {
    const c = { id, gain: +gain.toFixed(2), pan: +pan.toFixed(2), dist: +dist.toFixed(2), delay, doppler, priority, count, tick };
    if (!settings.on) { note({ ...c, mode: "off" }); return; }
    if (!ctx) { note({ ...c, mode: "dropped: no audio yet" }); return; }
    if (paused && id.indexOf("snd_ui_") !== 0) { note({ ...c, mode: "dropped: paused" }); return; }
    if (!manifest) {
      const asked = performance.now();
      loadManifest().then(() => {
        const s = sounds.get(id);
        if (!s || (performance.now() - asked) / 1000 > (delay || 0) + kLate) { note({ ...c, mode: "dropped: late" }); return; }
        loadSound(id);
        if (pickVariant(s) >= 0) cue(id, gain, pan, dist, delay, doppler, priority, count, tick); else hold(s, c);
      });
      return;
    }
    const s = sounds.get(id);
    if (!s) { note({ ...c, mode: "dropped: unknown" }); return; }
    if (gain * settings.volume < 0.01) { note({ ...c, mode: "dropped: too quiet" }); return; }
    loadSound(id);
    const v = pickVariant(s);
    if (v < 0 && !s.files.every((f) => failed.has(f))) { hold(s, c); return; }
    const mode = play(s, v, c);
    note({ ...c, variant: v + 1, mode });
  }

  // Paused: what is in flight fades, nothing new starts, the ambience drops low (platform.h sfxPaused).
  function setPaused(on) {
    paused = !!on;
    if (!ctx) return;
    const now = ctx.currentTime;
    if (paused) { for (const v of voices.splice(0)) fadeOut(v, now, 0.08); }
    applyVolume();
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
    l.gain.gain.setTargetAtTime(level * dB(s.level || 0) * mix.loop_gain, ctx.currentTime, 1.2);
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
  // One more loop by name (platform.h sfxLayer): the far battle, the columns, the day's air, fire.
  const gameLayers = new Map();
  function gameLayer(id, level, pan) {
    gameLayers.set(id, { level, pan });
    if (!ctx || !manifest) { if (settings.on) loadManifest(); return; }
    setLayer(id, level, pan);
  }
  function stopAmbience() { for (const id of layers.keys()) setLayer(id, 0, 0); }

  // Under water (game/app/platform.h sfxUnderwater): the filter and wobble on the page's whole output (audio.js), and the sounds
  // made for it, synthesised by the game (game/src/underwater_audio.cpp: the one recipe the desktop plays too) and handed over
  // as stereo float arrays: a loop (the water's own, not filtered), and a cue for the plunge and one for the surfacing.
  const synthPending = new Map(), synthBuffers = new Map();
  let uwNode = null, uwSrc = null, uwLevel = 0;
  function synth(id, stereo, rate, loop) {
    synthPending.set(id, { stereo, rate, loop });
    makeSynth();
  }
  function makeSynth() {
    if (!ctx) return;
    for (const [id, s] of synthPending) {
      const n = s.stereo.length / 2, b = ctx.createBuffer(2, n, s.rate);
      const l = b.getChannelData(0), r = b.getChannelData(1);
      for (let i = 0; i < n; i++) { l[i] = s.stereo[i * 2]; r[i] = s.stereo[i * 2 + 1]; }
      synthBuffers.set(id, b);
    }
    synthPending.clear();
  }
  function underwater(cutoff, gain, wobble, ambience) {
    if (!ctx) return;
    makeSynth();
    if (window.IllyriaAudio) window.IllyriaAudio.setUnderwater(cutoff, gain, wobble);
    const on = settings.on && settings.ambience;
    const level = on ? ambience : 0;
    const buf = synthBuffers.get("underwater_loop");
    if (level > 0.002 && buf && !uwSrc) {
      uwNode = uwNode || ctx.createGain();
      uwNode.gain.value = 0;
      uwNode.connect(ctx.destination);
      uwSrc = ctx.createBufferSource();
      uwSrc.buffer = buf; uwSrc.loop = true;
      uwSrc.connect(uwNode);
      uwSrc.start();
    }
    if (uwNode) uwNode.gain.setTargetAtTime(level * 0.55 * settings.volume * (window.IllyriaAudio ? window.IllyriaAudio.settings.master : 1), ctx.currentTime, 0.12);
    if (level <= 0.002 && uwSrc && uwLevel <= 0.002) { try { uwSrc.stop(); } catch (e) {} uwSrc = null; }
    uwLevel = level;
  }
  function underwaterCue(entering) {
    if (!ctx || !settings.on) return;
    const b = synthBuffers.get(entering ? "underwater_in" : "underwater_out");
    if (!b) return;
    const src = ctx.createBufferSource(), g = ctx.createGain();
    src.buffer = b;
    g.gain.value = 0.5 * settings.volume * (window.IllyriaAudio ? window.IllyriaAudio.settings.master : 1);
    src.connect(g); g.connect(ctx.destination);  // (after the filter: the crossing itself)
    src.start();
  }

  // The page's own sounds: taps and panels.
  function ui(id) {
    if (!settings.on || !ctx || !manifest) return;
    const s = sounds.get(id);
    if (!s) return;
    loadSound(id);
    const v = pickVariant(s);
    if (v >= 0) play(s, v, { gain: 1, priority: 1 });
  }
  // Anything pressed that is a control: a button, a link, a checkbox or slider, a card or a row marked as one.
  document.addEventListener("click", (e) => {
    if (e.target.closest && e.target.closest("button, a[href], input, select, summary, [role=button], [data-click], .card, .row")) ui("snd_ui_click");
  }, { passive: true });
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
      if (settings.on) for (const [id, g] of gameLayers) setLayer(id, g.level, g.pan);
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
    gameLayer,
    paused: setPaused,
    playing: () => voices.length,
    mix,
    synth,
    underwater,
    underwaterCue,
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
