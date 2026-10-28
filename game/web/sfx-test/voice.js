// The soldiers' voices: plays the cues game/src/voice.cpp sends (through
// main.cpp) from the lines in voice/lines.json. Recorded lines (listed in
// voice/available.json by tools/voice/convert.sh) are fetched per voice the
// first time that voice speaks; a line not yet recorded is read aloud by the
// browser instead (speechSynthesis), or, for the wordless voices, a short
// growl is made up, until the real file exists. Cries of pain and death have
// rendered placeholders (tools/sfx/synth), played until they are recorded.
// Positional by pan and volume; at most kMaxVoices at once, the graver cues
// cutting in. The AudioContext is shared with the sound effects (audio.js).
(function () {
  "use strict";
  const kMaxVoices = 3;
  const kMinGapMs = 300;    // between starts, except grave cues
  const kGrave = 4;         // priority that cuts in and skips the gap
  const kOrder = 3;         // orders: the newest answer replaces the last
  const kEnemyRate = 0.88;  // the other side sounds darker
  const kLogSize = 300;
  const settingsKey = "illyria.voice.v1";
  let version = "";
  try { version = new URL(document.currentScript.src).searchParams.get("v") || ""; } catch (e) {}

  const settings = { on: true, volume: 0.8, speech: true };
  try {
    const saved = JSON.parse(localStorage.getItem(settingsKey));
    if (saved && typeof saved.on === "boolean") Object.assign(settings, saved);
  } catch (e) { /* private mode: defaults */ }
  function save() {
    try { localStorage.setItem(settingsKey, JSON.stringify(settings)); } catch (e) {}
  }

  let manifest = null, manifestLoading = null;
  let available = new Set();
  const sets = new Map();         // id -> set from the manifest
  const buffers = new Map();      // file -> AudioBuffer, or null while loading / failed
  const setsLoading = new Set();  // voices whose recordings were asked for
  const lastVariant = new Map();  // "set/situation" -> index last played
  const playing = [];             // {priority, end, stop}
  let speaking = null;            // {priority, end} of the speech placeholder
  let lastStart = -1e9;
  let ctx = null, master = null, noise = null;
  const log = [];

  function note(entry) {
    entry.t = +(performance.now() / 1000).toFixed(2);
    log.push(entry);
    if (log.length > kLogSize) log.shift();
  }

  function loadManifest() {
    if (manifestLoading) return manifestLoading;
    const v = version;
    manifestLoading = Promise.all([
      fetch(`voice/lines.json?v=${encodeURIComponent(v)}`).then((r) => r.json()),
      fetch(`voice/available.json?v=${encodeURIComponent(v)}`).then((r) => (r.ok ? r.json() : [])).catch(() => []),
    ]).then(([m, have]) => {
      manifest = m;
      for (const s of m.sets) sets.set(s.id, s);
      available = new Set(have);
    }).catch((e) => { console.warn("voices: no manifest", e); });
    return manifestLoading;
  }

  // Audio needs a user gesture on iOS: the shared context (audio.js) is made
  // (and woken) on the first touch.
  function unlock() {
    if (!settings.on) return;
    try {
      if (!ctx) {
        ctx = window.IllyriaAudio ? window.IllyriaAudio.context() : null;
        if (!ctx) return;
        master = ctx.createGain();
        master.connect(ctx.destination);
        applyVolume();
      }
      if (window.IllyriaAudio) window.IllyriaAudio.unlock();
    } catch (e) { /* no audio: voices stay quiet */ }
    if (settings.speech && window.speechSynthesis && !unlock.spoke) {
      try { const u = new SpeechSynthesisUtterance(" "); u.volume = 0; speechSynthesis.speak(u); unlock.spoke = true; } catch (e) {}
    }
    loadManifest();
  }
  for (const ev of ["pointerdown", "touchend", "keydown"]) document.addEventListener(ev, unlock, { capture: true, passive: true });

  function applyVolume() { if (master) master.gain.value = settings.on ? settings.volume : 0; }

  // A voice's recordings, fetched the first time it speaks.
  function loadSet(set) {
    if (setsLoading.has(set.id) || !ctx) return;
    setsLoading.add(set.id);
    const files = [];
    for (const s of set.situations) for (const l of s.lines) { const f = sourceOf(l); if (f) files.push(f); }
    let next = 0;
    const worker = () => {
      if (next >= files.length) return Promise.resolve();
      const file = files[next++];
      buffers.set(file, null);
      return fetch(file).then((r) => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); })
        .then((data) => new Promise((ok, bad) => ctx.decodeAudioData(data, ok, bad)))
        .then((buf) => buffers.set(file, buf))
        .catch(() => buffers.delete(file))
        .then(worker);
    };
    for (let i = 0; i < 4; i++) worker();
  }

  // What plays for a line: its recording, else its rendered placeholder, else nothing.
  function sourceOf(line) {
    if (available.has(line.file)) return line.file;
    return line.placeholder || null;
  }

  // The lines for a situation in this voice, or its fallback's.
  function linesFor(set, situation) {
    for (let s = set, hops = 0; s && hops < 3; s = sets.get(s.fallback), hops++) {
      const found = s.situations.find((x) => x.id === situation);
      if (found) return { set: s, sit: found };
    }
    return null;
  }

  function pickVariant(key, n) {
    const last = lastVariant.has(key) ? lastVariant.get(key) : -1;
    let i = Math.floor(Math.random() * n);
    if (n > 1 && i === last) i = (i + 1 + Math.floor(Math.random() * (n - 1))) % n;
    lastVariant.set(key, i);
    return i;
  }

  // Room for one more voice: true if there is (perhaps by cutting one off).
  function makeRoom(priority, now) {
    for (let i = playing.length - 1; i >= 0; i--) if (playing[i].end <= now) playing.splice(i, 1);
    if (playing.length < kMaxVoices) return true;
    let low = 0;
    for (let i = 1; i < playing.length; i++) if (playing[i].priority < playing[low].priority) low = i;
    if (playing[low].priority > priority || (playing[low].priority === priority && priority < kOrder)) return false;
    try { playing[low].stop(); } catch (e) {}
    playing.splice(low, 1);
    return true;
  }

  function playBuffer(buf, cue, gain) {
    const src = ctx.createBufferSource();
    src.buffer = buf;
    if (cue.enemy) src.playbackRate.value = kEnemyRate;
    const g = ctx.createGain();
    g.gain.value = gain;
    let out = g;
    if (ctx.createStereoPanner) {
      const p = ctx.createStereoPanner();
      p.pan.value = cue.pan;
      g.connect(p);
      out = p;
    }
    src.connect(g);
    out.connect(master);
    src.start();
    return { stop: () => src.stop(), seconds: buf.duration / src.playbackRate.value };
  }

  // The wordless voices' stand-in: a short filtered-noise growl, pitched by voice.
  function playGrowl(set, cue, gain, text) {
    if (!noise) {
      noise = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
      const d = noise.getChannelData(0);
      for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
    }
    const long = /roar|bellow|howl|wail|trumpet|scream|shriek|screech/.test(text);
    const seconds = (long ? 0.7 : 0.35) + Math.random() * 0.2;
    const src = ctx.createBufferSource();
    src.buffer = noise;
    const f = ctx.createBiquadFilter();
    f.type = "bandpass";
    const base = 220 * (set.placeholder ? set.placeholder.pitch : 1) * (cue.enemy ? 0.8 : 1);
    f.frequency.setValueAtTime(base * 1.6, ctx.currentTime);
    f.frequency.exponentialRampToValueAtTime(base * 0.7, ctx.currentTime + seconds);
    f.Q.value = 6;
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, ctx.currentTime);
    g.gain.exponentialRampToValueAtTime(gain * 2.5, ctx.currentTime + 0.05);
    g.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + seconds);
    src.connect(f); f.connect(g);
    let out = g;
    if (ctx.createStereoPanner) { const p = ctx.createStereoPanner(); p.pan.value = cue.pan; g.connect(p); out = p; }
    out.connect(master);
    src.start();
    src.stop(ctx.currentTime + seconds);
    return { stop: () => src.stop(), seconds };
  }

  let speechVoices = null;
  function speak(set, cue, gain, text, now) {
    if (!window.speechSynthesis) return "no speech";
    if (speaking && speaking.end > now) {
      const outranks = cue.priority > speaking.priority || (cue.priority === speaking.priority && cue.priority >= kOrder);
      if (!outranks) return "speech busy";
      speechSynthesis.cancel();
    }
    const words = text.replace(/\([^)]*\)/g, "").trim();
    if (!words) return "no words";
    const u = new SpeechSynthesisUtterance(words);
    if (!speechVoices || !speechVoices.length) {
      speechVoices = speechSynthesis.getVoices().filter((v) => /^en/i.test(v.lang));
    }
    if (speechVoices.length) {
      // A different stock voice per set, so they sound apart.
      let h = 0;
      for (const ch of set.id) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
      u.voice = speechVoices[h % speechVoices.length];
    }
    const ph = set.placeholder || { pitch: 1, rate: 1 };
    u.pitch = Math.max(0.1, Math.min(2, ph.pitch * (cue.enemy ? 0.75 : 1)));
    u.rate = Math.max(0.5, Math.min(2, ph.rate * (0.95 + Math.random() * 0.15)));
    u.volume = Math.max(0, Math.min(1, gain));
    const end = now + 400 + words.length * 70 / u.rate;
    speaking = { priority: cue.priority, end };
    u.onend = () => { if (speaking && speaking.end === end) speaking = null; };
    speechSynthesis.speak(u);
    return null;
  }

  // From the engine: one voice cue (see VoiceCue in game/include/illyria/game/voice.h).
  function cue(setId, situation, enemy, pan, gain, priority, speaker, tick) {
    const c = { set: setId, situation, enemy, pan, gain, priority, speaker, tick };
    if (!settings.on) { note({ ...c, mode: "off" }); return; }
    if (!manifest) { loadManifest(); note({ ...c, mode: "dropped: loading" }); return; }
    const set = sets.get(setId);
    const found = set && linesFor(set, situation);
    if (!found) { note({ ...c, mode: "dropped: no lines" }); return; }
    const now = performance.now();
    const volume = gain * settings.volume;
    if (volume < 0.02) { note({ ...c, mode: "dropped: too far" }); return; }
    if (priority < kOrder && now - lastStart < kMinGapMs) { note({ ...c, mode: "dropped: gap" }); return; }
    const lines = found.sit.lines;
    const line = lines[pickVariant(found.set.id + "/" + situation, lines.length)];
    if (ctx) loadSet(found.set);
    const buf = buffers.get(sourceOf(line));
    let mode = null;
    const src = sourceOf(line);
    if (!buf && src && line.placeholder && (!ctx || buffers.has(src))) { note({ ...c, line: line.id, mode: "dropped: loading" }); return; }
    if (buf && ctx) {
      if (!makeRoom(priority, now)) { note({ ...c, line: line.id, mode: "dropped: busy" }); return; }
      const p = playBuffer(buf, c, gain);
      playing.push({ priority, end: now + p.seconds * 1000, stop: p.stop });
      mode = available.has(line.file) ? "file" : "placeholder";
    } else if (!found.set.verbal && ctx && settings.speech) {
      if (!makeRoom(priority, now)) { note({ ...c, line: line.id, mode: "dropped: busy" }); return; }
      const p = playGrowl(found.set, c, gain, line.text);
      playing.push({ priority, end: now + p.seconds * 1000, stop: p.stop });
      mode = "growl";
    } else if (settings.speech) {
      const why = speak(found.set, c, volume, line.text, now);
      if (why) { note({ ...c, line: line.id, mode: "dropped: " + why }); return; }
      mode = "speech";
    } else {
      note({ ...c, line: line.id, mode: "dropped: unrecorded" });
      return;
    }
    lastStart = now;
    note({ ...c, line: line.id, text: line.text, mode });
  }

  // The ⚙ panel's controls.
  function bind() {
    const on = document.getElementById("voiceOn");
    const vol = document.getElementById("voiceVolume");
    const sp = document.getElementById("voiceSpeech");
    if (!on || !vol || !sp) return;
    const show = () => {
      on.checked = settings.on;
      vol.value = Math.round(settings.volume * 100);
      vol.disabled = sp.disabled = !settings.on;
      sp.checked = settings.speech;
    };
    on.onchange = () => {
      settings.on = on.checked;
      if (!settings.on && window.speechSynthesis) speechSynthesis.cancel();
      if (settings.on) unlock();
      applyVolume(); save(); show();
    };
    vol.oninput = () => { settings.volume = +vol.value / 100; applyVolume(); save(); };
    sp.onchange = () => { settings.speech = sp.checked; if (!sp.checked && window.speechSynthesis) speechSynthesis.cancel(); save(); };
    show();
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", bind);
  else bind();

  if (settings.on) loadManifest();

  // The sound test page: one line of a voice, by its index (loads that voice first).
  function playLine(setId, situation, index, pan, gain) {
    unlock();
    return loadManifest().then(() => {
      const set = sets.get(setId);
      const found = set && linesFor(set, situation);
      if (!found || !ctx) return "no line";
      loadSet(found.set);
      const line = found.sit.lines[Math.min(index, found.sit.lines.length - 1)];
      const file = sourceOf(line);
      const play = () => {
        const buf = buffers.get(file);
        if (buf) { playBuffer(buf, { pan: pan || 0, enemy: false }, gain == null ? 1 : gain); return "played"; }
        if (settings.speech) { speak(found.set, { priority: 5, enemy: false }, gain == null ? 1 : gain, line.text, performance.now()); return "speech"; }
        return "unrecorded";
      };
      if (!file || buffers.get(file)) return play();
      // Wait for its file to arrive.
      return new Promise((done) => {
        let tries = 0;
        const wait = () => (buffers.get(file) || !buffers.has(file) || ++tries > 40 ? done(play()) : setTimeout(wait, 100));
        wait();
      });
    });
  }

  window.IllyriaVoice = {
    cue,
    playLine,
    settings,
    log,
    // Test hook: the last n played or dropped cues.
    recent: (n) => log.slice(-(n || 20)),
    load: loadManifest,
    unlock,
  };
})();
