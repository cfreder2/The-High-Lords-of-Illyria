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
  // Where the voices and effects play into: the master gain, made with the context.
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
    settings,
    setMaster,
    unlock,
    unlocked: () => unlocked && !!ctx,
    // Runs cb(ctx) once audio is allowed (at once if it already is).
    onUnlock(cb) { if (unlocked && ctx) cb(ctx); else waiting.push(cb); },
  };
})();
