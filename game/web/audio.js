// The page's one AudioContext, shared by the voices (voice.js) and the sound
// effects (sfx.js). iOS only lets audio start from a user gesture: the context
// is made (or woken) on the first touch, click or key, a silent sample opens
// the gate on old iOS, and whoever waits for it is told.
(function () {
  "use strict";
  let ctx = null;
  const waiting = [];
  let unlocked = false;
  // The master volume over the voices and the sound effects (Settings, Sound).
  const settingsKey = "illyria.audio.v1";
  const settings = { master: 1 };
  try {
    const saved = JSON.parse(localStorage.getItem(settingsKey));
    if (saved && typeof saved.master === "number") Object.assign(settings, saved);
  } catch (e) { /* private mode: defaults */ }
  let out = null;
  // Where the voices and effects play into: the master gain, made with the context. Its way to the speakers is straight
  // while the camera is in the air, and through the underwater chain (below) while it is under water.
  function output() {
    const c = context();
    if (!c) return null;
    if (!out) {
      out = c.createGain();
      out.gain.value = settings.master;
      out.connect(c.destination);
    }
    return out;
  }

  // Under water (game/src/underwater.cpp makes the numbers, the one mapping both platforms use): everything the world plays goes
  // through two low-pass biquads (24 dB an octave), a gain and a delay of a few ms swung at 0.35 Hz (a slow wobble of pitch). The
  // chain is joined only while it matters, so the air costs nothing.
  let uw = null, uwOn = false;
  function uwChain(c) {
    if (uw) return uw;
    const lp1 = c.createBiquadFilter(), lp2 = c.createBiquadFilter(), gain = c.createGain(), delay = c.createDelay(0.05);
    lp1.type = lp2.type = "lowpass";
    lp1.Q.value = lp2.Q.value = -3.01;  // Butterworth: 1/sqrt(2), in dB
    delay.delayTime.value = 0.006;
    const lfo = c.createOscillator(), depth = c.createGain();
    lfo.frequency.value = 0.35;
    depth.gain.value = 0;
    lfo.connect(depth); depth.connect(delay.delayTime);
    lfo.start();
    delay.connect(lp1); lp1.connect(lp2); lp2.connect(gain); gain.connect(c.destination);
    uw = { lp1, lp2, gain, delay, depth };
    return uw;
  }
  // cutoff Hz, linear gain and wobble 0..1, already scaled by how far under it is (20000, 1, 0: the air).
  function setUnderwater(cutoff, gain, wobble) {
    const c = context();
    if (!c || !out) return;
    const active = cutoff < 19000 || wobble > 0.001 || gain < 0.999;
    if (active !== uwOn) {
      const chain = uwChain(c);
      try { out.disconnect(); } catch (e) {}
      out.connect(active ? chain.delay : c.destination);
      uwOn = active;
    }
    if (!uw) return;
    const t = c.currentTime;
    uw.lp1.frequency.setTargetAtTime(Math.min(cutoff, 20000), t, 0.03);
    uw.lp2.frequency.setTargetAtTime(Math.min(cutoff, 20000), t, 0.03);
    uw.gain.gain.setTargetAtTime(gain, t, 0.03);
    uw.depth.gain.setTargetAtTime(0.0025 * wobble, t, 0.1);
  }
  function setMaster(v) {
    settings.master = Math.max(0, Math.min(1, v));
    if (out) out.gain.value = settings.master;
    try { localStorage.setItem(settingsKey, JSON.stringify(settings)); } catch (e) {}
  }

  function context() {
    if (ctx) return ctx;
    try {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (AC) ctx = new AC({ latencyHint: "interactive" });
    } catch (e) { ctx = null; }
    return ctx;
  }

  function unlock() {
    const c = context();
    if (!c) return;
    try {
      if (c.state !== "running") c.resume();
      if (!unlocked) {
        const b = c.createBuffer(1, 1, 22050), src = c.createBufferSource();
        src.buffer = b; src.connect(c.destination); src.start(0);
      }
    } catch (e) { /* no audio */ }
    if (!unlocked) {
      unlocked = true;
      for (const cb of waiting.splice(0)) { try { cb(c); } catch (e) { console.warn(e); } }
    }
  }
  for (const ev of ["pointerdown", "touchend", "keydown", "click"]) document.addEventListener(ev, unlock, { capture: true, passive: true });

  window.IllyriaAudio = {
    context,
    output,
    setUnderwater,
    settings,
    setMaster,
    unlock,
    unlocked: () => unlocked && !!ctx,
    // Runs cb(ctx) once audio is allowed (at once if it already is).
    onUnlock(cb) { if (unlocked && ctx) cb(ctx); else waiting.push(cb); },
  };
})();
