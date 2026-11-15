// The Sound Editor, voices: every line of game/web/voice/lines.json, by voice set and situation, with a
// recording studio for getting through a thousand of them. A take is saved into the workspace by the server;
// keeping a take puts it in the game at once (game/web/voice/<set>/<situation>_<n>.mp3, -16 LUFS mono, the
// index and the checklist refreshed by tools/voice/script.py). Nothing is committed.
(async () => {
  "use strict";
  window.AUDIT_BASE = "../audit/";
  const A = window.AuditSound, U = window.AuditSoundUi, D = window.SoundDsp, SE = window.SoundEditor, el = U.el, fill = SE.fill;
  const $ = (s, r = document) => r.querySelector(s);
  const page = $("#page");
  try { await A.load(); } catch (e) { await SE.ready; page.append(SE.banner(), el("p", { class: "bad" }, e.message)); return; }
  await SE.ready;
  if (!A.lines) { page.append(SE.banner(), el("p", { class: "bad" }, "game/web/voice/lines.json could not be read.")); return; }
  const L = A.lines, SITS = new Map(L.situations.map((x) => [x.id, x]));
  // Heard most: the three order answers and the cries; then the gravest news first (the situations' priorities).
  const MOST = ["selected", "move", "attack", "pain", "death", "death_moan"];
  const rank = (sid) => { const i = MOST.indexOf(sid); return i >= 0 ? i : 10 + (5 - ((SITS.get(sid) || {}).priority || 0)); };
  let data = { own: {}, chosen: {}, credits: {} };
  const load = async () => {
    if (SE.server) { try { data = await SE.get("/api/voices"); } catch (e) { SE.fail(e); } }
    const have = await fetch(A.root + "voice/available.json").then((r) => (r.ok ? r.json() : [])).catch(() => []);
    A.available = new Set(have);
  };
  await load();
  const keyOf = (line) => line.file.slice(6, -4);                    // soldier/selected_1
  const takes = (line) => data.own[keyOf(line)] || [];
  const inGame = (line) => A.available.has(line.file);
  const allLines = (set) => set.situations.flatMap((x) => x.lines.map((l) => ({ set, sit: x, line: l })));
  const counts = (set) => { const all = allLines(set); return { n: all.length, game: all.filter((r) => inGame(r.line)).length, taken: all.filter((r) => takes(r.line).length).length }; };
  const notMan = (set) => !set.verbal || set.id === "archon";
  const state = { set: SE.store("voice.set") || L.sets[0].id, todo: false, sit: "", text: "", fxPitch: false, fxVerb: false };
  if (!A.sets.has(state.set)) state.set = L.sets[0].id;
  const editing = new Set();
  const HOURS = (n) => { const m = Math.round(n * 3 * 25 / 60 / 5) * 5; return m >= 60 ? `about ${(m / 60).toFixed(1)} hours` : `about ${m} minutes`; };

  // ---------------------------------------------------------------- playing and choosing
  const playTake = async (set, t, btn) => {
    try {
      const raw = await SE.decodeUrl(t.url), e = t.edit || {}, built = D.build(raw, e, "voice");
      const semis = e.pitch || (state.fxPitch && notMan(set) ? -3 : 0);
      SE.playDry(built, { btn, rate: Math.pow(2, semis / 12), reverb: e.reverb || (state.fxVerb && notMan(set)) });
    } catch (err) { SE.fail(err); }
  };
  let busy = false;
  const choose = async (line, src, quiet) => {
    if (!SE.server) return SE.toast("Start tools/sound-editor/serve.py to choose.", null, "bad");
    if (busy) return false;
    busy = true; document.body.classList.add("busy");
    const before = data.chosen[line.id] || null;
    let ok = false;
    try {
      const j = await SE.post("/api/choose", { kind: "voice", line: line.id, src });
      A.forget(line.file);
      await load(); redrawLine(line); progress(); changes.refresh(); ok = true;
      SE.toast(quiet ? "Undone. " + j.said : j.said, quiet ? null : () => choose(line, before, true));
    } catch (e) { SE.fail(e); }
    busy = false; document.body.classList.remove("busy");
    return ok;
  };
  const saveEdit = async (line, t, edit, inf) => {
    t.edit = edit; t.info = inf;
    if (!SE.server) return;
    try { const j = await SE.post("/api/edit", { src: t.src, edit, info: inf }); if (data.chosen[line.id] === t.src) { A.forget(line.file); SE.toast(j.said); changes.refresh(); } } catch (e) { SE.fail(e); }
  };
  // One take from the microphone into the workspace. Resolves to the saved take, or null.
  const upload = async (line, raw) => {
    const edit = Object.assign(D.defaults("voice"), D.autoTrim(raw, "voice")), built = D.build(raw, edit, "voice");
    if (!SE.server) return { local: true, raw, edit };
    const meta = { edit, info: D.info(built), device: raw.device + " · " + raw.how };
    const t = await SE.post(`/api/record?kind=voice&line=${encodeURIComponent(line.id)}&meta=${encodeURIComponent(JSON.stringify(meta))}`, D.wavFloat(raw.chans, raw.sr), true);
    (data.own[keyOf(line)] = data.own[keyOf(line)] || []).push(t);
    return t;
  };

  // ---------------------------------------------------------------- one line in the list
  function lineRow(set, sit, line) {
    const ts = takes(line), pick = data.chosen[line.id], meter = SE.meter(), status = el("span", { class: "why" });
    let handle = null;
    const rec = el("button", { class: "rec", title: "record a take of this line", onclick: async () => {
      if (handle) {
        const h = handle; handle = null; rec.textContent = "●"; rec.classList.remove("on"); meter.node.style.display = "none";
        const raw = await h.stop();
        if (!raw) return;
        try { const t = await upload(line, raw); if (t.local) { fill(status, SE.downloadLink(raw, keyOf(line).replace("/", "_") + ".wav")); return; } redrawLine(line); progress(); SE.toast(t.said + (raw.clipped ? " It clipped." : "")); } catch (e) { SE.fail(e); }
        return;
      }
      if (SE.recording()) return;
      try { handle = await SE.record({ onStatus: (x) => { status.textContent = x; }, onLevel: meter.set }); rec.textContent = "■"; rec.classList.add("on"); meter.node.style.display = ""; } catch (e) { status.textContent = e.message; }
    } }, "●");
    meter.node.style.display = "none";
    const src = A.lineSource(set, line);
    return el("div", { class: "vline" + (inGame(line) ? " done" : ts.length ? " taken" : ""), id: "l-" + line.id.replace(/\./g, "-"), "data-line": line.id },
      el("div", { class: "vtop" }, rec, U.lineButton(set, line),
        el("span", { class: "vtext" + (set.verbal ? "" : " wordless") }, line.text), el("code", {}, keyOf(line) + ".wav"),
        el("span", { class: "badge " + (inGame(line) ? "good" : ts.length ? "info" : "warnb") }, inGame(line) ? "in the game" : ts.length ? `${ts.length} take${ts.length > 1 ? "s" : ""}, none kept` : src.kind === "placeholder" ? "placeholder" : "not recorded"),
        line.note ? el("span", { class: "why" }, "· " + line.note) : null,
        el("button", { class: "studio1", title: "this line in the studio", onclick: () => studio.open(set, line) }, "studio"), meter.node, status),
      ts.map((t) => {
        const on = pick === t.src, i = t.info || {}, key = t.src, host = el("div", { class: "edhost" });
        const pb = el("button", { class: "play", onclick: () => playTake(set, t, pb) }, "▶ take " + t.take);
        const open = () => SE.decodeUrl(t.url).then((raw) => { const ed = SE.editor({ kind: "voice", raw, edit: t.edit, fx: notMan(set), play: (b, btn) => SE.playDry(b, { btn, rate: Math.pow(2, (t.edit.pitch || 0) / 12), reverb: t.edit.reverb }),
          refs: inGame(line) ? [{ label: "in the game now", lufs: -16, play: (vol) => { const a = new Audio(A.root + line.file + "?t=" + Date.now()); a.volume = Math.min(1, vol); a.play(); } }] : [],
          onChange: (edit, inf) => saveEdit(line, t, edit, inf) }); fill(host, ed.node); requestAnimationFrame(ed.redraw); }).catch((e) => SE.fail(e));
        if (editing.has(key)) open();
        return el("div", { class: "take" + (on ? " on" : "") }, pb,
          on ? el("span", { class: "badge good" }, "in the game") : el("button", { class: "use", title: "this take is the line: installed at once", onclick: () => choose(line, t.src) }, "keep: put it in the game"),
          el("span", { class: "why" }, `${i.dur ? i.dur.toFixed(2) + " s" : ""}${i.before != null ? " · recorded at " + i.before.toFixed(1) + " LUFS" : ""}${i.noise != null ? " · noise " + i.noise.toFixed(0) + " dBFS" : ""}${i.clipped ? " · CLIPPED" : ""}${t.edit && t.edit.bake_fx ? " · effects baked in" : ""}`),
          el("button", { onclick: () => { if (editing.has(key)) { editing.delete(key); fill(host); } else { editing.add(key); open(); } } }, "✎ edit"),
          on ? null : el("button", { onclick: async () => { if (!confirm("Delete take " + t.take + "?")) return; try { await SE.post("/api/delete", { src: t.src }); await load(); redrawLine(line); progress(); } catch (e) { SE.fail(e); } } }, "delete"), host);
      }),
      pick || inGame(line) ? el("div", { class: "take" }, el("button", { class: "original", title: "the file as git has it: none yet, so the placeholder plays again", onclick: () => choose(line, null) }, "Use the game's original")) : null);
  }
  function redrawLine(line) {
    const node = document.getElementById("l-" + line.id.replace(/\./g, "-"));
    if (!node) return;
    const set = A.sets.get(line.id.split(".")[0]), sit = set.situations.find((x) => x.lines.includes(line));
    node.replaceWith(lineRow(set, sit, line));
    applyFilter();
  }

  // ---------------------------------------------------------------- the set: who he is, how far along, how even
  function bar(done, part, n) { return el("span", { class: "pbar", title: `${done} in the game, ${part} with a take, of ${n}` }, el("i", { class: "g", style: `width:${done / n * 100}%` }), el("i", { class: "t", style: `width:${Math.max(0, part - done) / n * 100}%` })); }
  function progress() {
    let n = 0, g = 0, t = 0;
    for (const s of L.sets) { const c = counts(s); n += c.n; g += c.game; t += c.taken; const chip = $("#chip-" + s.id); if (chip) fill(chip, s.name + " ", el("span", { class: "why" }, `${c.game}/${c.n}`), bar(c.game, c.taken, c.n)); }
    fill($("#overall"), el("b", {}, `${g} of ${n} lines are in the game`), `; ${t} have at least one take. `, bar(g, t, n));
    const set = A.sets.get(state.set), c = counts(set);
    fill($("#setprog"), `${set.name}: ${c.game} of ${c.n} in the game, ${c.taken} with a take. `, bar(c.game, c.taken, c.n), ` A whole set in one sitting: ${HOURS(c.n)} (three takes of each).`);
    spread();
  }
  function spread() {     // how loud each kept take was RECORDED: a set should sit together unless a line is meant to be shouted
    const set = A.sets.get(state.set), rows = [];
    for (const r of allLines(set)) { const t = takes(r.line).find((x) => x.src === data.chosen[r.line.id]); if (t && t.info && t.info.before != null) rows.push({ r, v: t.info.before }); }
    const box = $("#spread");
    if (rows.length < 2) { fill(box, el("span", { class: "why" }, "Level across the set: shown once two lines of it are in the game.")); return; }
    const vs = rows.map((x) => x.v).sort((a, b) => a - b), med = vs[Math.floor(vs.length / 2)], lo = Math.min(-40, vs[0] - 2), hi = Math.max(-8, vs[vs.length - 1] + 2);
    const out = rows.filter((x) => Math.abs(x.v - med) > 6);
    fill(box, el("span", { class: "why" }, `Level across the set, as recorded (every installed line is then set to -16 LUFS): median ${med.toFixed(1)} LUFS, from ${vs[0].toFixed(1)} to ${vs[vs.length - 1].toFixed(1)}. `),
      el("span", { class: "strip" }, el("i", { class: "med", style: `left:${(med - lo) / (hi - lo) * 100}%` }), rows.map((x) => el("i", { class: "dot" + (Math.abs(x.v - med) > 6 ? " out" : ""), style: `left:${(x.v - lo) / (hi - lo) * 100}%`, title: `${keyOf(x.r.line)}: ${x.v.toFixed(1)} LUFS · ${x.r.sit.note}` }))),
      out.length ? el("div", { class: "warn" }, `${out.length} more than 6 LU from the rest: ${out.map((x) => `${keyOf(x.r.line)} (${x.v > med ? "+" : ""}${(x.v - med).toFixed(0)}; direction: ${x.r.sit.note})`).join("; ")}. A shouted line may be meant to be; a whispered order answer is a retake, or raise its gain in ✎ edit.`) : el("span", { class: "ok" }, " All within 6 LU."));
  }
  function applyFilter() {
    const q = state.text.trim().toLowerCase();
    let n = 0;
    for (const b of document.querySelectorAll(".sitblock")) {
      let any = 0;
      for (const v of b.querySelectorAll(".vline")) {
        const id = v.dataset.line, ok = (!state.todo || !v.classList.contains("done")) && (!q || v.textContent.toLowerCase().includes(q)) && (!state.sit || id.split(".")[1] === state.sit);
        v.style.display = ok ? "" : "none";
        if (ok) { any++; n++; }
      }
      b.style.display = any ? "" : "none";
    }
    $("#shown").textContent = n + " lines shown";
  }
  function drawSet() {
    const set = A.sets.get(state.set);
    SE.store("voice.set", set.id);
    for (const c of document.querySelectorAll(".chip")) c.classList.toggle("on", c.id === "chip-" + set.id);
    const sitSel = $("#sitsel");
    fill(sitSel, el("option", { value: "" }, "every situation"), set.situations.map((x) => el("option", { value: x.id, selected: x.id === state.sit }, (SITS.get(x.id) || {}).title || x.id)));
    if (!set.situations.find((x) => x.id === state.sit)) state.sit = "";
    fill($("#sethead"), el("h2", {}, set.name, " ", el("code", {}, set.id + "/")),
      el("p", { class: "casting" }, el("b", {}, "Who this is: "), set.casting),
      el("p", { class: "note" }, el("b", {}, "Used by: "), set.units.join(", "), ". ", set.fallback ? `Anything not listed is said in the ${set.fallback} voice. ` : "",
        set.verbal ? "" : el("b", { class: "warn" }, "No words: these are sounds to perform. The text in brackets says what is wanted: growl, moan, hiss, roar. Layering two takes is fine. "),
        el("a", { href: "../audit/sounds.html#set=" + set.id }, "What plays for each line today (Inspect)")),
      notMan(set) ? el("p", { class: "note" }, el("label", {}, el("input", { type: "checkbox", checked: state.fxPitch, onchange: (e) => { state.fxPitch = e.target.checked; } }), " preview a slight pitch-down (3 semitones)"), " ",
        el("label", {}, el("input", { type: "checkbox", checked: state.fxVerb, onchange: (e) => { state.fxVerb = e.target.checked; } }), " preview a reverb"),
        el("span", { class: "why" }, " · PREVIEW ONLY, for playing takes on this page: nothing is changed in any file. To put an effect into a line, open its take's ✎ edit and tick “bake into the installed file”.")) : null,
      el("div", { id: "setprog", class: "note" }), el("div", { id: "spread", class: "note" }));
    const order = set.situations.slice().sort((a, b) => rank(a.id) - rank(b.id));
    fill($("#lines"), order.map((x) => { const s = SITS.get(x.id) || { title: x.id, when: "", priority: 0 };
      return el("div", { class: "sitblock" }, el("div", { class: "sithead" }, el("b", {}, s.title), " ", el("code", {}, x.id), el("span", { class: "why" }, ` · ${s.when} · priority ${s.priority}`)),
        el("div", { class: "direction" }, el("b", {}, set.verbal ? "Direction: " : "The sound wanted: "), x.note), x.lines.map((l) => lineRow(set, x, l))); }));
    progress(); applyFilter();
  }

  // ---------------------------------------------------------------- the studio: one line at a time
  const studio = (() => {
    let set = null, queue = [], at = 0, handle = null, last = null, node = null, meter = null;
    const opts = { auto: SE.store("voice.auto") !== "0", silence: parseFloat(SE.store("voice.silence")) || 1.0 };
    const say = (t, cls) => { const s = $("#st-status"); if (s) { s.textContent = t; s.className = cls || ""; } };
    const makeQueue = (all) => allLines(set).filter((r) => all || !inGame(r.line)).sort((a, b) => rank(a.sit.id) - rank(b.sit.id));   // Record next: what is not in the game yet
    function show() {
      const r = queue[at];
      if (!r) { fill($("#st-body"), el("p", { class: "sttext" }, "Nothing left in this queue."), el("p", { class: "why" }, "Every line of this set that the filter shows is in the game. Close (Esc) and pick another set.")); return; }
      SE.store("voice.at." + set.id, r.line.id);
      const s = SITS.get(r.sit.id) || { title: r.sit.id, when: "" }, ts = takes(r.line), pick = data.chosen[r.line.id];
      fill($("#st-body"), 
        el("div", { class: "why" }, `${set.name} · ${at + 1} of ${queue.length} in the queue · `, el("code", {}, keyOf(r.line) + ".wav"), inGame(r.line) ? el("span", { class: "badge good" }, "in the game") : null),
        el("div", { class: "stsit" }, el("b", {}, s.title), " · ", s.when),
        el("div", { class: "stdir" }, el("b", {}, set.verbal ? "Direction: " : "The sound wanted (no words): "), r.line.note || r.sit.note),
        el("div", { class: "sttext" + (set.verbal ? "" : " wordless"), id: "st-text" }, r.line.text),
        el("div", { class: "sttakes" }, ts.length ? ts.map((t) => { const b = el("button", { class: "play" + (t.src === pick ? " kind-recorded" : ""), onclick: () => playTake(set, t, b) }, `▶ take ${t.take}${t.src === pick ? " (in the game)" : ""}`); return b; }) : el("span", { class: "why" }, "no take yet")));
      last = ts.length ? ts[ts.length - 1] : null;
    }
    async function stop() {
      if (!handle) return;
      const h = handle, r = queue[at]; handle = null;
      $("#st-rec").textContent = "● Record (space)"; $("#st-rec").classList.remove("on");
      const raw = await h.stop();
      if (!raw) return say("Nothing recorded.", "bad");
      try {
        const t = await upload(r.line, raw);
        if (t.local) { fill($("#st-status"), SE.downloadLink(raw, keyOf(r.line).replace("/", "_") + ".wav")); return; }
        show(); redrawLine(r.line); progress();
        const i = t.info || {};
        if (i.before == null) return say(`Take ${t.take} is SILENT: check the microphone chosen on the page. R does it again.`, "bad");
        say(`Take ${t.take}: ${i.dur.toFixed(2)} s, recorded at ${i.before != null ? i.before.toFixed(1) : "?"} LUFS, noise ${i.noise.toFixed(0)} dBFS${raw.clipped ? " · CLIPPED: move back and redo (R)" : i.noise > -50 ? " · noisy room" : ""}. Enter keeps it and goes on; R does it again.`, raw.clipped ? "bad" : "");
        playTake(set, t, null);
      } catch (e) { say(e.message, "bad"); }
    }
    async function record() {
      if (handle) return stop();
      if (!queue[at] || SE.recording()) return;
      try {
        handle = await SE.record({ onStatus: (t) => say(t), onLevel: meter.set, autoStop: opts.auto ? opts.silence : 0, onAuto: () => stop() });
        $("#st-rec").textContent = "■ Stop (space)"; $("#st-rec").classList.add("on");
      } catch (e) { say(e.message, "bad"); }
    }
    async function keep() {
      const r = queue[at];
      if (!r || handle) return;
      if (!last) return say("Record a take first (space), or skip (S).", "bad");
      if (data.chosen[r.line.id] !== last.src && !(await choose(r.line, last.src))) return;
      move(1);
    }
    async function redo() {
      const r = queue[at];
      if (!r || handle) return;
      if (last && data.chosen[r.line.id] !== last.src) { try { await SE.post("/api/delete", { src: last.src }); await load(); redrawLine(r.line); } catch (e) { return say(e.message, "bad"); } }
      show(); record();
    }
    function move(d) { if (handle) return; SE.stop(); at = Math.max(0, Math.min(queue.length, at + d)); show(); say(""); }
    function key(e) {
      if (!node || node.style.display === "none" || /INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
      const k = e.key;
      if (k === " ") record(); else if (k === "Enter") keep(); else if (k === "r" || k === "R") redo(); else if (k === "s" || k === "S" || k === "ArrowRight") move(1);
      else if (k === "ArrowLeft") move(-1); else if (k === "p" || k === "P") { if (last) playTake(set, last, null); } else if (k === "Escape") close(); else return;
      e.preventDefault();
    }
    function close() { if (handle) { handle.stop(); handle = null; } SE.stop(); node.style.display = "none"; drawSet(); }
    function build() {
      meter = SE.meter();
      const auto = el("input", { type: "checkbox", checked: opts.auto, onchange: (e) => { opts.auto = e.target.checked; SE.store("voice.auto", opts.auto ? "1" : "0"); } });
      const sil = el("input", { type: "range", min: 0.4, max: 3, step: 0.1, value: opts.silence, style: "width:90px", oninput: (e) => { opts.silence = +e.target.value; silN.textContent = opts.silence.toFixed(1) + " s"; SE.store("voice.silence", String(opts.silence)); } });
      const silN = el("b", {}, opts.silence.toFixed(1) + " s");
      node = el("div", { id: "studio" }, el("div", { class: "stbox" },
        el("div", { class: "kv" }, el("b", {}, "Recording studio"), el("span", { class: "ctl" }, el("label", {}, auto, " stop by itself after "), sil, silN, " of silence"), el("span", { style: "flex:1" }), el("button", { id: "st-close", onclick: close }, "Close (Esc)")),
        el("div", { id: "st-body" }),
        el("div", { class: "kv stctl" }, el("button", { id: "st-rec", class: "rec big", onclick: record }, "● Record (space)"), meter.node,
          el("button", { id: "st-keep", class: "big", onclick: keep }, "Keep and next (Enter)"), el("button", { id: "st-redo", onclick: redo }, "Redo (R)"), el("button", { onclick: () => { if (last) playTake(set, last, null); } }, "Play (P)"),
          el("button", { id: "st-prev", onclick: () => move(-1) }, "←"), el("button", { id: "st-skip", onclick: () => move(1) }, "Skip (S or →)")),
        el("div", { id: "st-status" }),
        el("p", { class: "why" }, "Keep puts the last take in the game at once and moves on; the queue runs the most-heard lines first (selected, move, attack, the cries), then by the situations' priority. Every take stays in the workspace.")));
      document.body.append(node);
      window.addEventListener("keydown", key);
    }
    return {
      open: (s, line) => {
        set = s;
        if (!node) build();
        queue = makeQueue(!!line);
        const want = line ? line.id : SE.store("voice.at." + s.id);
        at = Math.max(0, queue.findIndex((r) => r.line.id === want));
        node.style.display = ""; show(); say(SE.canRecord() ? "" : "The microphone needs a page served from 127.0.0.1: start tools/sound-editor/serve.py.", "bad");
      },
      api: { record, keep, redo, move, at: () => (queue[at] ? queue[at].line.id : null) },
    };
  })();

  // ---------------------------------------------------------------- the page
  const changes = SE.changesPanel(async () => { await load(); drawSet(); });
  const total = L.sets.reduce((n, s) => n + counts(s).n, 0), nsit = L.sets.reduce((n, s) => n + s.situations.length, 0);
  const HOWTO = el("details", { class: "set" }, el("summary", {}, "How to record voices"),
    el("ul", { class: "howto" },
      ["One voice per set, kept the same: the same microphone, the same distance (about 30 cm, a hand and a half), the same room. Do a whole set in one sitting if you can; a voice recorded on two days sounds like two men.",
        "A pop filter, or the microphone a little to the side of the mouth, so P and B do not thump. A soft room: a wardrobe full of clothes, or a duvet over your head.",
        "Warm up: read the set's casting note aloud, hum, then shout a few lines before recording any.",
        "These are barks across a battlefield, not speeches: short, projected from the chest, standing up. Not theatrical. Shout the shouted lines for real and stand back a pace for them; watch the meter.",
        "Do not move closer for quiet lines: every installed line is set to the same loudness (-16 LUFS, the game's voice target, from tools/voice/convert.sh).",
        "What is heard most: selected, move and attack (every order), then pain and death. The studio's queue starts with those; five good, different readings of each matter more than anything further down.",
        "Vary the takes (pitch, speed, which word leans); the game picks a different line each time and plays the enemy's lower.",
        `There are ${total} lines in ${L.sets.length} sets. At three takes a line with the studio stopping by itself, a set of 100 is a little over two hours; the wordless sets are shorter.`,
        "Leave half a second of silence before you speak: the studio measures the room from it and stops when you have been quiet for the time set."].map((t) => el("li", {}, t))));
  U.sayTo($("#said"));
  page.append(SE.banner(), el("div", { class: "kv", style: "margin:10px 0" }, SE.recBar()),
    el("p", { class: "note", id: "overall" }),
    el("p", { class: "note" }, `${L.sets.length} voice sets, ${nsit} situations across them (${L.situations.length} kinds), ${total} lines, read from game/web/voice/lines.json as it is now.`),
    changes.node, HOWTO,
    el("div", { class: "chips" }, L.sets.map((s) => el("button", { class: "chip", id: "chip-" + s.id, onclick: () => { state.set = s.id; state.sit = ""; location.hash = "#set=" + s.id; drawSet(); } }, s.name))),
    el("div", { id: "sethead" }),
    el("div", { class: "kv", style: "margin:8px 0" }, el("button", { class: "rec big", id: "next", onclick: () => studio.open(A.sets.get(state.set)) }, "● Record next"),
      el("label", {}, el("input", { type: "checkbox", id: "todo", onchange: (e) => { state.todo = e.target.checked; applyFilter(); } }), " only lines not in the game"),
      el("select", { id: "sitsel", onchange: (e) => { state.sit = e.target.value; applyFilter(); } }),
      el("input", { type: "search", placeholder: "search the lines", oninput: (e) => { state.text = e.target.value; applyFilter(); } }), el("span", { id: "shown", class: "why" }),
      el("button", { onclick: () => SE.stop() }, "■ stop")),
    el("div", { id: "lines" }));
  const toHash = () => { const m = /^#set=(\w+)/.exec(location.hash); if (m && A.sets.has(m[1])) state.set = m[1]; drawSet(); };
  toHash();
  window.addEventListener("hashchange", toHash);
  window.VoicesPage = { studio: studio.api, counts: () => ({ sets: L.sets.length, situations: nsit, lines: total, rows: null }), data: () => data };
})();
