// What both Sound Editor pages share: the server's calls, the banner without it, the toast with Undo, dry
// playback, the microphone (raw PCM through an AudioWorklet, a ScriptProcessor where there is none), the
// take editor (waveform, trim handles, fades, level) and the "Installed changes" panel.
(() => {
  "use strict";
  const A = window.AuditSound, U = window.AuditSoundUi, D = window.SoundDsp, el = U.el;
  const SE = { server: null, el };
  // A node's children replaced by these (nested lists flattened, nothing for null or false).
  const fill = (node, ...kids) => { node.replaceChildren(...kids.flat(Infinity).filter((k) => k != null && k !== false).map((k) => (k.nodeType ? k : document.createTextNode(String(k))))); return node; };
  SE.fill = fill;
  const store = (k, v) => { try { if (v === undefined) return localStorage.getItem("sound-editor." + k); localStorage.setItem("sound-editor." + k, v); } catch (e) { /* private window */ } return null; };
  SE.store = store;

  // ---------------------------------------------------------------- the server
  SE.get = (path) => fetch(path).then(async (r) => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; });
  SE.post = (path, body, raw) => fetch(path, { method: "POST", headers: { "X-Sound-Editor": "1", "Content-Type": raw ? "audio/wav" : "application/json" }, body: raw ? body : JSON.stringify(body) })
    .then(async (r) => { let j = {}; try { j = await r.json(); } catch (e) { /* not JSON */ } if (!r.ok) throw new Error(j.error || "the server answered " + r.status); return j; });
  SE.ready = (location.protocol === "file:" ? Promise.resolve(null) : fetch("/api/status").then((r) => (r.ok ? r.json() : null)).catch(() => null)).then((s) => {
    SE.server = s && s.ok ? s : null;
    return SE.server;
  });
  SE.banner = () => {
    if (SE.server && SE.server.ffmpeg) return el("p", { class: "note" }, `Choosing installs into the working tree of ${SE.server.repo} (branch ${SE.server.branch}); nothing is committed. Workspace: ${SE.server.workspace}.`,
      SE.server.candidates ? "" : " No candidates there (no data.js): only the game's sounds and your own recordings.");
    if (SE.server) return el("div", { class: "callout" }, el("b", {}, "ffmpeg was not found: you can audition and record, but nothing can be installed. "), SE.server.ffmpeg_help);
    return el("div", { class: "callout" }, el("b", {}, "Start tools/sound-editor/serve.py to choose and record. "),
      location.protocol === "file:" ? "Opened as a file, a page cannot read the game's sounds at all: run  python3 tools/sound-editor/serve.py  in the repository and open the address it prints."
        : "This server only shows files: you can listen to what is in the game; candidates, recording into the workspace, choosing and undo need the editor's own server (python3 tools/sound-editor/serve.py).");
  };
  SE.nav = (here) => el("nav", {}, [["../audit/index.html", "Audit"], ["../audit/sprites.html", "Sprites"], ["index.html", "Sound Editor"], ["../audit/sounds.html", "Inspect"], ["effects.html", "Effects"], ["voices.html", "Voices"]]
    .map(([href, text]) => el("a", { href, class: text === here ? "here" : "" }, text)));

  // ---------------------------------------------------------------- the toast (what was just done, and Undo)
  let toastNode = null, toastTimer = 0;
  SE.toast = (text, undo, cls) => {
    if (!toastNode) { toastNode = el("div", { id: "toast" }); document.body.append(toastNode); }
    clearTimeout(toastTimer);
    toastNode.className = cls || "";
    fill(toastNode, el("span", {}, text), undo ? el("button", { id: "toast-undo", onclick: () => { toastNode.style.display = "none"; undo(); } }, "Undo") : null,
      el("button", { title: "dismiss", onclick: () => { toastNode.style.display = "none"; } }, "×"));
    toastNode.style.display = "";
    toastTimer = setTimeout(() => { toastNode.style.display = "none"; }, undo ? 20000 : 7000);
  };
  SE.fail = (e) => SE.toast(String(e && e.message ? e.message : e), null, "bad");

  // ---------------------------------------------------------------- dry playback of something built in the page
  let ctx = null, now = null, verb = null;
  SE.context = () => { ctx = ctx || new (window.AudioContext || window.webkitAudioContext)(); if (ctx.state === "suspended") ctx.resume(); return ctx; };
  SE.stop = () => { if (now) { try { now.src.stop(); } catch (e) { /* ended */ } if (now.btn) now.btn.classList.remove("sounding"); now = null; } A.stopAll(); };
  SE.toAudioBuffer = (p, c) => { const b = (c || SE.context()).createBuffer(p.chans.length, p.chans[0].length, p.sr); p.chans.forEach((x, i) => b.copyToChannel(x, i)); return b; };
  // o: {loop, gainDb, rate, reverb, btn, then}
  SE.playDry = (p, o) => {
    o = o || {};
    const same = now && o.btn && now.btn === o.btn;
    SE.stop();
    if (same) return;
    const c = SE.context(), src = c.createBufferSource(), g = c.createGain();
    src.buffer = SE.toAudioBuffer(p, c); src.loop = !!o.loop; src.playbackRate.value = o.rate || 1; g.gain.value = D.lin(o.gainDb || 0) * 0.8;
    src.connect(g); g.connect(c.destination);
    if (o.reverb) {
      if (!verb) {   // a small hall, for the preview only
        verb = c.createConvolver();
        const n = Math.floor(c.sampleRate * 1.6), b = c.createBuffer(2, n, c.sampleRate);
        for (let ch = 0; ch < 2; ch++) { const d = b.getChannelData(ch); for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / n, 3) * 0.5; }
        verb.buffer = b; verb.connect(c.destination);
      }
      const send = c.createGain(); send.gain.value = 0.35; g.connect(send); send.connect(verb);
    }
    const me = now = { src, btn: o.btn };
    if (o.btn) o.btn.classList.add("sounding");
    src.onended = () => { if (now === me) { if (me.btn) me.btn.classList.remove("sounding"); now = null; if (o.then) o.then(); } };
    src.start();
  };
  const decoded = new Map();
  SE.decodeUrl = (url) => {   // a workspace sound, as samples
    if (!decoded.has(url)) decoded.set(url, fetch(url).then((r) => { if (!r.ok) throw new Error(r.status + " " + url); return r.arrayBuffer(); }).then((d) => SE.context().decodeAudioData(d)).then(D.fromBuffer));
    return decoded.get(url);
  };
  SE.forgetUrl = (url) => decoded.delete(url);

  // ---------------------------------------------------------------- the microphone
  const WORKLET = "class Cap extends AudioWorkletProcessor{process(i){var c=i[0];if(c&&c[0])this.port.postMessage(c.map(function(x){return x.slice()}));return true}}registerProcessor('cap',Cap);";
  SE.canRecord = () => !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia) && window.isSecureContext !== false;
  let recording = null;
  SE.recording = () => recording;
  function fillDevices(sel) {
    if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) return;
    navigator.mediaDevices.enumerateDevices().then((ds) => {
      const cur = sel.value || store("device") || "";
      fill(sel, el("option", { value: "" }, "Default microphone"), ds.filter((d) => d.kind === "audioinput" && d.deviceId && d.deviceId !== "default").map((d, i) => el("option", { value: d.deviceId }, d.label || "Microphone " + (i + 1))));
      sel.value = cur;
      if (sel.selectedIndex < 0) sel.value = "";
    }).catch(() => {});
  }
  // The recording settings every Record button uses: the input, the count-in.
  SE.recBar = () => {
    const dev = el("select", { id: "recdev", title: "Which microphone. Names appear once the browser has been allowed to use one.", onchange: () => store("device", dev.value) });
    const count = el("input", { type: "checkbox", id: "reccount", checked: store("count") !== "0", onchange: () => store("count", count.checked ? "1" : "0") });
    fillDevices(dev);
    try { navigator.mediaDevices.addEventListener("devicechange", () => fillDevices(dev)); } catch (e) { /* none */ }
    SE.refreshDevices = () => fillDevices(dev);
    return el("span", { class: "ctl recbar" }, "Microphone ", dev, el("label", {}, count, " 3-2-1 count-in"),
      SE.canRecord() ? null : el("span", { class: "bad" }, " The microphone needs a page served from 127.0.0.1: start tools/sound-editor/serve.py."));
  };
  SE.meter = () => {
    const fill = el("div", { class: "fill" }), node = el("div", { class: "meterbox", title: "Input level. Keep the loudest moment in the upper third and never at the end." }, fill);
    return { node, set: (db) => { fill.style.width = Math.max(0, Math.min(100, (db + 60) / 60 * 100)) + "%"; fill.className = "fill" + (db > -1 ? " clip" : db > -6 ? " hot" : ""); } };
  };
  // Starts after a click only. o: {stereo, onStatus(text), onLevel(dB), autoStop: seconds of silence after sound, or 0}.
  // Resolves to a handle {stop()} whose stop() resolves to {chans, sr, clipped, how, device} or null.
  SE.record = async (o) => {
    if (recording) throw new Error("already recording");
    if (!SE.canRecord()) throw new Error("The microphone is not available here. Start tools/sound-editor/serve.py and open the address it prints.");
    SE.stop();
    const devSel = document.getElementById("recdev"), dev = devSel ? devSel.value : "", say = o.onStatus || (() => {});
    const r = recording = { chunks: [], on: false, len: 0, peak: 0, clipped: false, done: null };
    say("Asking for the microphone…");
    let stream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({ audio: { deviceId: dev ? { exact: dev } : undefined, channelCount: { ideal: o.stereo ? 2 : 1 }, echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    } catch (e) { recording = null; throw new Error("No microphone: " + e.message); }
    if (SE.refreshDevices) SE.refreshDevices();
    const c = new (window.AudioContext || window.webkitAudioContext)(), src = c.createMediaStreamSource(stream), mute = c.createGain();
    mute.gain.value = 0; mute.connect(c.destination);
    const track = stream.getAudioTracks()[0], set = track.getSettings ? track.getSettings() : {}, nch = Math.max(1, Math.min(o.stereo ? 2 : 1, set.channelCount || 1)), sr = c.sampleRate;
    let noise = Infinity, heard = false, quiet = 0, calm = 0, auto = null;
    const chunk = (chs) => {
      let m = 0, sq = 0;
      const x = chs[0];
      for (let i = 0; i < x.length; i++) { const a = Math.abs(x[i]); if (a > m) m = a; sq += a * a; }
      const rms = Math.sqrt(sq / x.length);
      r.peak = Math.max(m, r.peak * 0.8);
      if (o.onLevel) o.onLevel(D.dB(r.peak));
      if (!r.on) return;
      if (m >= 0.99) r.clipped = true;
      r.chunks.push(chs.slice(0, nch)); r.len += x.length;
      say("Recording " + (r.len / sr).toFixed(1) + " s" + (r.clipped ? " · CLIPPED: move back or turn the input down" : ""));
      if (o.autoStop > 0) {       // stop once there has been sound and then silence for that long
        calm += x.length;
        if (calm < sr * 0.25) noise = Math.min(noise, rms);
        const gate = Math.max((isFinite(noise) ? noise : 0) * 4, 0.006);
        if (rms > gate * 1.5) { heard = true; quiet = 0; } else if (heard) { quiet += x.length; if (quiet >= o.autoStop * sr && !auto) { auto = true; if (o.onAuto) o.onAuto(); } }
      }
    };
    let node, how = "AudioWorklet";
    try {
      await c.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: "application/javascript" })));
      node = new AudioWorkletNode(c, "cap", { numberOfInputs: 1, numberOfOutputs: 1, channelCount: nch, channelCountMode: "explicit" });
      node.port.onmessage = (e) => chunk(e.data);
    } catch (e) {
      how = "ScriptProcessor";
      node = c.createScriptProcessor(4096, nch, 1);
      node.onaudioprocess = (e) => { const out = []; for (let k = 0; k < nch; k++) out.push(new Float32Array(e.inputBuffer.getChannelData(k))); chunk(out); };
    }
    src.connect(node); node.connect(mute);
    let cancelled = false;
    const countIn = document.getElementById("reccount"), n0 = countIn && countIn.checked ? 3 : 0;
    (function tick(n) { if (cancelled) return; if (n > 0) { say("Get ready… " + n); setTimeout(() => tick(n - 1), 700); } else { r.on = true; say("Recording"); } })(n0);
    return {
      stop: async () => {
        cancelled = true; recording = null;
        try { node.disconnect(); stream.getTracks().forEach((t) => t.stop()); await c.close(); } catch (e) { /* closed */ }
        if (o.onLevel) o.onLevel(-90);
        if (!r.chunks.length || r.len < sr * 0.05) { say("Nothing recorded."); return null; }
        const chans = [];
        for (let k = 0; k < nch; k++) { const out = new Float32Array(r.len); let off = 0; for (const ch of r.chunks) { out.set(ch[k] || ch[0], off); off += ch[0].length; } chans.push(out); }
        return { chans, sr, clipped: r.clipped, how, device: track.label || "" };
      },
    };
  };
  // A fallback when the editor's server is not there: the take as a file to keep.
  SE.downloadLink = (raw, name) => el("a", { href: URL.createObjectURL(D.wavFloat(raw.chans, raw.sr)), download: name }, "Download " + name + " (the server is not running, so it was not saved)");

  // ---------------------------------------------------------------- the take editor
  // o: {kind, raw: {chans, sr}, edit, fx (voices of things that are not men), onChange(edit, info), play(built, btn), refs: [{label, lufs, play(volume, btn)}]}
  SE.editor = (o) => {
    const kind = o.kind, raw = o.raw, mono = D.mono(raw.chans), total = mono.length / raw.sr;
    const edit = Object.assign(D.defaults(kind), { t0: 0, t1: total }, o.edit || {});
    let built = null, drag = 0, timer = 0;
    const cv = el("canvas", { class: "wave", height: 90 }), info = el("div", { class: "why readout" });
    const numIn = (key, min, max, step, title) => el("input", { type: "number", min, max, step, value: edit[key], title, onchange: (e) => { const v = parseFloat(e.target.value); if (isFinite(v)) { edit[key] = Math.max(min, Math.min(max, v)); changed(); } } });
    const check = (key, text, title) => el("label", { title }, el("input", { type: "checkbox", checked: !!edit[key], onchange: (e) => { edit[key] = e.target.checked; changed(); } }), " " + text);
    function draw() {
      const w = cv.clientWidth || 640, h = cv.height, g = cv.getContext("2d"), n = mono.length;
      if (cv.width !== w) cv.width = w;
      g.clearRect(0, 0, w, h);
      g.fillStyle = "#101216"; g.fillRect(0, 0, w, h);
      g.fillStyle = "#2c3038"; g.fillRect(0, 0, edit.t0 / total * w, h); g.fillRect(edit.t1 / total * w, 0, w, h);
      g.fillStyle = "#c9d1dc";
      const step = Math.max(1, Math.floor(n / w / 64));
      for (let i = 0; i < w; i++) {
        let lo = 0, hi = 0;
        for (let j = Math.floor(i / w * n), b = Math.floor((i + 1) / w * n); j < b; j += step) { if (mono[j] < lo) lo = mono[j]; if (mono[j] > hi) hi = mono[j]; }
        g.fillRect(i, h / 2 - hi * h / 2, 1, Math.max(1, (hi - lo) * h / 2));
      }
      g.fillStyle = "#f0b340"; g.fillRect(edit.t0 / total * w - 1, 0, 3, h); g.fillRect(edit.t1 / total * w - 2, 0, 3, h);
    }
    function rebuild() {
      built = D.build(raw, edit, kind);
      const p = built, f = (v) => (isFinite(v) ? v.toFixed(1) : "n/a");
      info.textContent = `Kept ${(edit.t1 - edit.t0).toFixed(2)} of ${total.toFixed(2)} s · loudness as recorded ${f(p.before)} → ${f(p.lufs)} LUFS (target ${edit.target}${edit.gain ? (edit.gain > 0 ? " +" : " ") + edit.gain : ""}) · peak ${p.peak.toFixed(1)} dBFS · recorded peak ${p.rawPeak.toFixed(1)} dBFS · noise floor ${p.noise.toFixed(0)} dBFS${p.noise > -50 ? " (noisy: aim below -50)" : ""}${p.clipped ? " · CLIPPED when recorded: retake further from the microphone" : ""} · ${raw.sr} Hz, ${raw.chans.length} ch`;
      info.className = "readout " + (p.clipped || p.noise > -50 ? "warn" : "why");
      draw();
      return built;
    }
    function changed() { SE.stop(); rebuild(); clearTimeout(timer); timer = setTimeout(() => o.onChange && o.onChange(Object.assign({}, edit), D.info(built)), 500); }
    const pos = (e) => { const r = cv.getBoundingClientRect(); return Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * total; };
    cv.onpointerdown = (e) => { const p = pos(e); drag = Math.abs(p - edit.t0) < Math.abs(p - edit.t1) ? 1 : 2; cv.setPointerCapture(e.pointerId); cv.onpointermove(e); };
    cv.onpointermove = (e) => { if (!drag) return; const p = pos(e); if (drag === 1) edit.t0 = Math.min(p, edit.t1 - 0.02); else edit.t1 = Math.max(p, edit.t0 + 0.02); draw(); };
    cv.onpointerup = cv.onpointercancel = () => { if (drag) { drag = 0; changed(); } };
    const playB = el("button", { class: "play", onclick: () => o.play(built || rebuild(), playB) }, "▶ as edited");
    const rawB = el("button", { class: "play", title: "the whole recording, untouched", onclick: () => SE.playDry({ chans: raw.chans, sr: raw.sr }, { btn: rawB }) }, "▶ untrimmed");
    const refSel = o.refs && o.refs.length ? el("select", {}, o.refs.map((r, i) => el("option", { value: i }, r.label))) : null;
    const match = () => { const r = o.refs[+refSel.value], a = built.lufs, b = r.lufs != null && isFinite(r.lufs) ? r.lufs : a, m = Math.min(a, b); return { r, take: m - a, vol: D.lin(m - b) }; };
    const aB = refSel ? el("button", { class: "play", onclick: () => SE.playDry(built, { btn: aB, gainDb: match().take }) }, "▶ A: this") : null;
    const bB = refSel ? el("button", { class: "play", onclick: () => { SE.stop(); const m = match(); m.r.play(m.vol, bB); } }, "▶ B") : null;
    const node = el("div", { class: "editor" }, cv, info,
      el("div", { class: "kv" }, playB, rawB,
        el("button", { title: "find the sound again: above the room's noise, with a little air either side", onclick: () => { Object.assign(edit, D.autoTrim(raw, kind)); changed(); } }, "Auto-trim"),
        kind === "loop" ? check("seam", "seam crossfade", "the end is cross-faded into the start, so the loop wraps without a click") : [el("span", { class: "ctl" }, "fade in ", numIn("fade_in", 0, 2000, 1, "milliseconds"), " ms"), el("span", { class: "ctl" }, "out ", numIn("fade_out", 0, 5000, 1, "milliseconds"), " ms")],
        el("span", { class: "ctl" }, "loudness ", numIn("target", -40, -6, 0.5, "the target in LUFS: -18 one-shots, -24 loops, -16 voices, as the game's converters set them"), " LUFS"),
        el("span", { class: "ctl" }, "gain ", numIn("gain", -24, 12, 0.5, "added after the loudness target, for a sound that should sit above or below the rest"), " dB"),
        kind === "voice" ? null : el("span", { class: "ctl" }, el("select", { title: "one-shots are mono in the game (it places them); loops are stereo", onchange: (e) => { edit.channels = +e.target.value; changed(); } },
          el("option", { value: 1, selected: edit.channels === 1 }, "mono"), el("option", { value: 2, selected: edit.channels === 2 }, "stereo")))),
      o.fx ? el("div", { class: "kv" }, el("span", { class: "ctl" }, "Effects for a voice that is not a man's: pitch ", numIn("pitch", -12, 12, 1, "semitones; down is negative. It slows the sound as it lowers it, as the game's own enemy pitch does."), " semitones"),
        check("reverb", "reverb", "a short echo"), check("bake_fx", "bake into the installed file", "Unticked, these are heard in this preview only and the installed file is the plain recording."),
        el("span", { class: "why" }, edit.bake_fx ? "Baked in: the installed file carries them." : "Preview only: the installed file is the plain recording.")) : null,
      refSel ? el("div", { class: "kv" }, el("span", { class: "why" }, "A/B at matched loudness (the louder is turned down):"), aB, refSel, bB) : null);
    setTimeout(rebuild, 0);
    return { node, edit, built: () => built || rebuild(), redraw: draw };
  };

  // ---------------------------------------------------------------- what differs from git
  SE.changesPanel = (onUndo) => {
    const list = el("div", {}), sum = el("summary", {}, "Installed changes"), box = el("details", { class: "set", id: "changes" }, sum, list);
    const load = async () => {
      if (!SE.server) { sum.textContent = "Installed changes: needs the editor's server"; return; }
      let rows = [];
      try { rows = (await SE.get("/api/changes")).changes; } catch (e) { sum.textContent = "Installed changes: " + e.message; return; }
      const audio = rows.filter((r) => r.audio);
      sum.textContent = audio.length ? `Installed changes: ${audio.length} sound file${audio.length > 1 ? "s" : ""} differ from git (not committed)` : "Installed changes: none; the game's sounds are as git has them";
      fill(list, el("p", { class: "note" }, "What differs from the last commit in game/web/sfx, game/web/voice and their generated docs. Undo brings the file back from git; the manifests, docs and credits follow. To keep the changes: git add and commit them yourself."),
        el("table", {}, el("tbody", {}, rows.map((r) => el("tr", {}, el("td", {}, el("code", {}, r.path)), el("td", { class: r.audio ? "warn" : "why" }, r.state),
          el("td", { class: "why" }, r.credit ? `${r.credit.source} · ${r.credit.author} · ${r.credit.licence}` : ""),
          el("td", {}, r.audio && r.state !== "deleted" ? el("button", { class: "play", onclick: (e) => { SE.stop(); const a = new Audio(r.url + "?t=" + Date.now()); a.play(); } }, "▶") : null,
            r.audio ? el("button", { class: "undo", onclick: async () => { try { const j = await SE.post("/api/undo", { path: r.path }); SE.toast(j.said); await load(); if (onUndo) onUndo(r.path); } catch (e) { SE.fail(e); } } }, "Undo") : null))))));
    };
    box.addEventListener("toggle", () => { if (box.open) load(); });
    load();
    return { node: box, refresh: load };
  };
  window.SoundEditor = SE;
})();
