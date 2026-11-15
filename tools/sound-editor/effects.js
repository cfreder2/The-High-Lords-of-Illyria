// The Sound Editor, effects: every sound in sounds.json with the variants in the game now, the workspace's
// candidates and your own recordings beside them. Ticking a source as a variant installs it at once (the
// server renders it into game/web/sfx and refreshes the manifests, docs and credits); nothing is committed.
(async () => {
  "use strict";
  window.AUDIT_BASE = "../audit/";
  const A = window.AuditSound, U = window.AuditSoundUi, D = window.SoundDsp, SE = window.SoundEditor, el = U.el, fill = SE.fill;
  const $ = (s, r = document) => r.querySelector(s);
  const page = $("#page");
  try { await A.load(); } catch (e) { await SE.ready; page.append(SE.banner(), el("p", { class: "bad" }, e.message)); return; }
  await SE.ready;
  A.settings.camera = 15;
  let data = { sounds: [], events: {}, own: {}, extra: [] };
  const info = new Map();       // id -> the server's state of that sound
  const open = new Set(), borrowed = new Map(), editing = new Set();
  const load = async () => {
    if (SE.server) { try { data = await SE.get("/api/effects"); } catch (e) { SE.fail(e); } }
    info.clear();
    for (const s of data.sounds) info.set(s.id, s);
    const m = await fetch(A.root + "sfx/sounds.json").then((r) => r.json());   // the manifest may have changed under us
    A.manifest = m; A.sounds.clear();
    for (const s of m.sounds) A.sounds.set(s.id, s);
  };
  await load();
  const kindOf = (id) => ((info.get(id) && info.get(id).loop) || (data.events[id] && data.events[id].loop && !info.has(id)) ? "loop" : "oneshot");
  const originals = (id) => { const i = info.get(id); return Array.from({ length: i ? i.originals : A.sounds.get(id).files.length }, (_, k) => "game:" + (k + 1)); };
  const chosen = (id) => (info.get(id) ? info.get(id).chosen : originals(id));
  const cands = (id) => ((data.events[id] || {}).cands || []);
  const findSrc = (src) => {
    if (src.startsWith("cand:")) for (const e in data.events) { const c = data.events[e].cands.find((x) => x.src === src); if (c) return c; }
    if (src.startsWith("own:")) for (const e in data.own) { const t = data.own[e].find((x) => x.src === src); if (t) return t; }
    return null;
  };

  // ---------------------------------------------------------------- one source: its samples, its name, its sound
  const rawOf = async (id, src) => {
    if (src.startsWith("game:")) {
      const n = +src.slice(5), file = `sfx/${id}_${n}.mp3`;
      return SE.decodeUrl(SE.server ? "/api/original?file=" + encodeURIComponent(file) : A.root + file);
    }
    const x = findSrc(src);
    return SE.decodeUrl(src.startsWith("cand:") ? x.url_master : x.url);
  };
  const label = (id, src) => {
    if (src.startsWith("game:")) return { name: "The game's original " + src.slice(5), small: "rendered by tools/sfx/synth · the game's own", flag: "" };
    const x = findSrc(src);
    if (!x) return { name: src, small: "not in the workspace any more", flag: "missing" };
    if (src.startsWith("cand:")) return { name: x.name.replace(/^.*_cand/, "candidate ") + " · " + x.raw.split("/").pop(), small: `${x.source} · ${x.author} · ${x.licence}`, flag: x.flag, url: x.url };
    const i = x.info || {};
    return { name: "Your take " + x.take, small: `own recording${x.created ? " of " + x.created.slice(0, 10) : ""} · no attribution needed${i.dur ? " · " + i.dur.toFixed(2) + " s" : ""}${i.noise != null ? " · noise floor " + i.noise.toFixed(0) + " dBFS" : ""}${i.clipped ? " · CLIPPED" : ""}`, flag: i.clipped ? "It clipped when recorded." : "" };
  };
  // Played as it would be installed: its edit applied, then through the game's chain at the chosen camera (or dry).
  const playSrc = async (id, src, btn) => {
    const kind = kindOf(id);
    try {
      const raw = await rawOf(id, src);
      const built = src.startsWith("game:") ? { chans: raw.chans, sr: raw.sr } : D.build(raw, (findSrc(src) || {}).edit, kind);
      playBuilt(id, built, btn);
    } catch (e) { SE.fail(e); }
  };
  const playBuilt = (id, built, btn) => {
    if (kindOf(id) === "loop" || !A.sounds.has(id)) return SE.playDry(built, { btn, loop: kindOf(id) === "loop", gainDb: A.sounds.has(id) ? Math.min(0, (A.sounds.get(id).level || 0) + 12) : 0 });
    SE.stop();
    const r = A.playBuffer(id, SE.toAudioBuffer(built, A.unlock()));
    U.say(`${A.sounds.get(id).title}: ${r.text}`, r.seconds ? "" : "bad");
    if (btn && r.seconds) { btn.classList.add("sounding"); setTimeout(() => btn.classList.remove("sounding"), Math.min(4000, r.seconds * 1000)); }
  };

  // ---------------------------------------------------------------- choosing is installing
  let busy = false;
  const commit = async (id, srcs, quiet) => {
    if (!SE.server) return SE.toast("Start tools/sound-editor/serve.py to choose.", null, "bad");
    if (busy) return;
    const before = chosen(id).slice();
    busy = true; document.body.classList.add("busy");
    try {
      const j = await SE.post("/api/choose", { kind: "sfx", id, srcs });
      for (let n = 1; n <= 12; n++) A.forget(`sfx/${id}_${n}.mp3`);
      await load(); redraw(id); changes.refresh();
      if (!quiet) SE.toast(j.said, () => commit(id, before, true));
      else SE.toast("Undone. " + j.said);
    } catch (e) { SE.fail(e); }
    busy = false; document.body.classList.remove("busy");
  };
  const saveEdit = async (id, src, edit, inf) => {
    const x = findSrc(src);
    if (x) { x.edit = edit; if (src.startsWith("own:")) x.info = inf; }
    if (!SE.server) return;
    try {
      const j = await SE.post("/api/edit", { src, edit, info: src.startsWith("own:") ? inf : undefined });
      if (chosen(id).includes(src)) { for (let n = 1; n <= 12; n++) A.forget(`sfx/${id}_${n}.mp3`); await load(); changes.refresh(); SE.toast(j.said); }
    } catch (e) { SE.fail(e); }
  };

  // ---------------------------------------------------------------- one sound's body
  function editorFor(id, src, host) {
    const kind = kindOf(id);
    fill(host, el("span", { class: "why" }, "reading…"));
    rawOf(id, src).then((raw) => {
      const x = findSrc(src), s = A.sounds.get(id);
      const refs = [];
      if (s) s.files.forEach((f, i) => refs.push({ label: `In the game now: variant ${i + 1}`, lufs: (A.stats[f] || {}).lufs, play: (vol, btn) => { const a = new Audio(A.root + f + "?t=" + Date.now()); a.volume = Math.min(1, vol); a.play(); } }));
      cands(id).forEach((c) => refs.push({ label: c.name.replace(/^.*_cand/, "candidate "), lufs: c.lufs, play: (vol) => { const a = new Audio(c.url_mp3); a.volume = Math.min(1, vol); a.play(); } }));
      const ed = SE.editor({ kind, raw, edit: Object.keys(x.edit || {}).length ? x.edit : Object.assign(src.startsWith("own:") ? D.autoTrim(raw, kind) : {}, x.edit), refs,
        play: (built, btn) => playBuilt(id, built, btn), onChange: (edit, inf) => saveEdit(id, src, edit, inf) });
      fill(host, ed.node);
      requestAnimationFrame(ed.redraw);
    }).catch((e) => fill(host, el("span", { class: "bad" }, "could not read it: " + e.message)));
  }
  function sourceRow(id, src, slot) {
    const L = label(id, src), list = chosen(id), inGame = A.sounds.has(id), kind = kindOf(id), key = id + "|" + src;
    const play = el("button", { class: "play", title: "as it would be installed, " + (kind === "loop" ? "looped" : "through the game's chain at the camera above"), onclick: () => playSrc(id, src, play) }, "▶");
    const edHost = el("div", { class: "edhost" });
    const canEdit = !src.startsWith("game:") && findSrc(src);
    const row = el("div", { class: "srow" + (slot ? " chosen" : ""), "data-src": src, draggable: slot ? "true" : null },
      slot ? el("span", { class: "slot", title: "drag to reorder" }, "⠿ " + slot) : null, play,
      el("span", { class: "what" }, el("b", {}, L.name), " ", el("span", { class: "why" }, L.url ? el("a", { href: L.url, target: "_blank", rel: "noopener noreferrer", class: "dimlink" }, L.small) : L.small),
        L.flag ? el("div", { class: "warn" }, "⚠ check: " + L.flag) : null),
      el("span", { class: "tools" },
        slot ? [list.length > 1 ? el("button", { title: "move up", disabled: slot === 1, onclick: () => { const l = list.slice(); l.splice(slot - 2, 0, l.splice(slot - 1, 1)[0]); commit(id, l); } }, "▲") : null,
          list.length > 1 ? el("button", { title: "move down", disabled: slot === list.length, onclick: () => { const l = list.slice(); l.splice(slot, 0, l.splice(slot - 1, 1)[0]); commit(id, l); } }, "▼") : null,
          list.length > 1 ? el("button", { class: "unuse", title: "no longer a variant", onclick: () => commit(id, list.filter((_, i) => i !== slot - 1)) }, "✕ remove") : null]
          : inGame ? [el("button", { class: "use", title: "add as one more variant: installed at once", onclick: () => commit(id, list.concat([src])) }, "+ use as a variant"),
            el("button", { class: "only", title: "the sound's only variant: installed at once", onclick: () => commit(id, [src]) }, "use only this")] : null,
        canEdit ? el("button", { class: "edit", onclick: () => { if (editing.has(key)) { editing.delete(key); fill(edHost); } else { editing.add(key); editorFor(id, src, edHost); } } }, "✎ edit") : null,
        src.startsWith("own:") && !slot ? el("button", { title: "delete this take from the workspace", onclick: async () => { if (!confirm("Delete this take?")) return; try { await SE.post("/api/delete", { src }); await load(); redraw(id); } catch (e) { SE.fail(e); } } }, "delete") : null),
      edHost);
    if (editing.has(key) && canEdit) editorFor(id, src, edHost);
    if (slot) {
      row.addEventListener("dragstart", (e) => { e.dataTransfer.setData("text/plain", String(slot - 1)); e.dataTransfer.effectAllowed = "move"; });
      row.addEventListener("dragover", (e) => { e.preventDefault(); row.classList.add("over"); });
      row.addEventListener("dragleave", () => row.classList.remove("over"));
      row.addEventListener("drop", (e) => { e.preventDefault(); row.classList.remove("over"); const from = +e.dataTransfer.getData("text/plain"), to = slot - 1; if (from === to || isNaN(from)) return; const l = list.slice(); l.splice(to, 0, l.splice(from, 1)[0]); commit(id, l); });
    }
    return row;
  }
  function recordLine(id) {
    const meter = SE.meter(), status = el("span", { class: "why" }), kind = kindOf(id);
    let handle = null;
    const btn = el("button", { class: "rec", onclick: async () => {
      if (handle) {
        const h = handle; handle = null; btn.textContent = "● Record"; btn.classList.remove("on");
        const raw = await h.stop();
        if (!raw) return;
        const edit = Object.assign(D.defaults(kind), D.autoTrim(raw, kind)), built = D.build(raw, edit, kind);
        if (!SE.server) { fill(status, SE.downloadLink(raw, id + "_own.wav")); return; }
        try {
          const meta = { edit, info: D.info(built), device: raw.device + " · " + raw.how };
          const t = await SE.post(`/api/record?kind=sfx&event=${encodeURIComponent(id)}&meta=${encodeURIComponent(JSON.stringify(meta))}`, D.wavFloat(raw.chans, raw.sr), true);
          editing.add(id + "|" + t.src);
          await load(); redraw(id);
          SE.toast(`${t.said}${raw.clipped ? " It clipped: retake further from the microphone." : ""}${isFinite(built.before) ? "" : " It is SILENT: check the microphone chosen above."}`, null, isFinite(built.before) ? "" : "bad");
        } catch (e) { SE.fail(e); }
        return;
      }
      if (SE.recording()) return SE.toast("Another recording is running.", null, "bad");
      try { handle = await SE.record({ stereo: kind === "loop", onStatus: (t) => { status.textContent = t; }, onLevel: meter.set }); btn.textContent = "■ Stop"; btn.classList.add("on"); } catch (e) { status.textContent = e.message; }
    } }, "● Record");
    return el("div", { class: "recline" }, btn, meter.node, status);
  }
  function body(id) {
    const s = A.sounds.get(id), i = info.get(id), list = s ? chosen(id) : [], pristine = s && JSON.stringify(list) === JSON.stringify(originals(id));
    const offered = [], seen = new Set(list);
    if (s) for (const g of originals(id)) if (!seen.has(g)) offered.push(g);
    for (const e of [id].concat(borrowed.get(id) || [])) for (const c of cands(e)) if (!seen.has(c.src)) offered.push(c.src);
    for (const t of data.own[id] || []) if (!seen.has(t.src)) offered.push(t.src);
    const cats = A.manifest.categories;
    const meta = async (patch) => { try { const j = await SE.post("/api/meta", Object.assign({ id }, patch)); await load(); redraw(id); changes.refresh(); SE.toast(j.said); } catch (e) { SE.fail(e); } };
    const others = Object.keys(data.events).filter((e) => e !== id && data.events[e].cands.length && !(borrowed.get(id) || []).includes(e)).sort();
    return el("div", { class: "sbody" },
      s ? [el("div", { class: "when" }, el("b", {}, "When: "), s.when, " ", el("span", { class: "why" }, s.behaviour)),
        el("div", { class: "kv" }, el("span", {}, "In the game now "), U.variants(s),
          el("span", { class: "ctl" }, "mix level ", el("input", { type: "number", min: -40, max: 12, step: 1, value: s.level || 0, disabled: !SE.server, title: "sounds.json: decibels on top of the file's own loudness", onchange: (e) => meta({ level: +e.target.value }) }), " dB",
            i && i.head_level != null && i.head_level !== (s.level || 0) ? el("button", { title: "as git has it", onclick: () => meta({ level: i.head_level }) }, `back to ${i.head_level}`) : null),
          el("span", { class: "ctl" }, "category ", el("select", { disabled: !SE.server, onchange: (e) => meta({ category: e.target.value }) }, cats.map((c) => el("option", { value: c.id, selected: c.id === s.category }, c.title)))),
          el("a", { href: "../audit/sounds.html#s=" + id, title: "its waveform, spectrum, faults and what asks for it" }, "Inspect")),
        el("h4", {}, `Variants in the game (${list.length}) `, el("span", { class: "why" }, "the game plays one at random, never the same twice running. Drag to reorder. "),
          pristine ? el("span", { class: "why" }, "As git has them.") : el("button", { class: "original", onclick: () => commit(id, originals(id)) }, "Use the game's original")),
        el("div", { class: "chosenlist" }, list.map((src, k) => sourceRow(id, src, k + 1)))]
        : el("p", { class: "note warn" }, "Not in the game yet: there is no such sound in sounds.json, so nothing can be chosen. Record and audition here; once the game has the sound (an entry in sounds.json and in sfx.cpp), borrow these from its own section."),
      el("h4", {}, `Available (${offered.length}) `, el("span", { class: "why" }, s ? "candidates from the workspace and your takes; using one installs it at once" : "")),
      el("div", {}, offered.length ? offered.map((src) => sourceRow(id, src, 0)) : el("p", { class: "note" }, SE.server ? "Nothing else yet: record one, or borrow another event's candidates." : "Candidates and takes are listed when the editor's server runs.")),
      recordLine(id),
      others.length ? el("div", { class: "kv" }, el("select", { onchange: (e) => { if (!e.target.value) return; borrowed.set(id, (borrowed.get(id) || []).concat([e.target.value])); redraw(id); } },
        el("option", { value: "" }, "Show another event's candidates here…"), others.map((e) => el("option", { value: e }, `${data.events[e].title} (${e}, ${data.events[e].cands.length})`)))) : null);
  }
  function summary(id) {
    const s = A.sounds.get(id), ex = data.extra.find((x) => x.id === id), list = s ? chosen(id) : [], changed = s && JSON.stringify(list) !== JSON.stringify(originals(id));
    const nc = cands(id).length, nt = (data.own[id] || []).length;
    return [el("b", {}, s ? s.title : ex ? ex.title : id), " ", el("code", {}, id),
      kindOf(id) === "loop" ? el("span", { class: "badge info" }, "loop") : null,
      changed ? el("span", { class: "badge good" }, "changed: " + list.map((x) => (x.startsWith("game:") ? "g" + x.slice(5) : x.startsWith("own:") ? "take" : "cand")).join(" ")) : null,
      nc ? el("span", { class: "badge info" }, nc + " candidates") : null, nt ? el("span", { class: "badge good" }, nt + (nt > 1 ? " takes" : " take")) : null];
  }
  function card(id) {
    const d = el("details", { class: "sound set", id: "s-" + id, "data-sound": id, "data-text": (id + " " + ((A.sounds.get(id) || {}).title || "")).toLowerCase() }, el("summary", {}, summary(id)));
    d.open = open.has(id);
    if (d.open) d.append(body(id));
    d.addEventListener("toggle", () => { if (d.open) { open.add(id); if (!$(".sbody", d)) d.append(body(id)); } else open.delete(id); });
    return d;
  }
  function redraw(id) { const old = $("#s-" + id); if (old) old.replaceWith(card(id)); else build(); }

  // ---------------------------------------------------------------- the page
  const changes = SE.changesPanel(async () => { await load(); build(); });
  const HOWTO = el("details", { class: "set" }, el("summary", {}, "How to record these well"),
    el("ul", { class: "howto" },
      ["Blades: two kitchen knives or a knife on a steel ruler; a pan lid struck with a metal bar and held loosely so it rings (armour).",
        "Blows: a wooden spoon on a cutting board (shield, gate); a fist in a leather glove on a cushion (blunt, body).",
        "Flesh and bone: a head of celery or lettuce twisted and snapped; a knife into a cabbage; a wet cloth slapped on a board.",
        "Cloth and leather: a leather belt folded and snapped apart (a whip of cloth, a banner); a jacket shaken.",
        "Footsteps: gloves or shoes on your hands, walked on a table, a tray of gravel, a tray of earth, a wet towel (mud).",
        "Water: a bucket or a bath. A hand slapped in for a splash, stirred for wading, a pebble dropped for an arrow.",
        "Wood: a creaky door or chair for gates and engines; dry sticks snapped; an old crate broken.",
        "The room: quiet, soft. Under a duvet or in a wardrobe full of clothes beats a bare kitchen. Fridge off, window shut.",
        "The microphone: 20 to 40 cm away and a little to one side; closer for small sounds. A phone's microphone is usually cleaner than a laptop's (the laptop hears its own fan).",
        "Watch the meter: the loudest moment in the upper third, never at the end. If it says CLIPPED, move back and do it again.",
        "Leave a second of silence before and after, and make several takes at different strengths: each can be a variant, and the game plays them at random."].map((t) => el("li", {}, t))));
  const filter = el("input", { type: "search", placeholder: "filter by name or id", style: "width:220px", oninput: () => applyFilter() });
  const only = el("select", { onchange: () => applyFilter() }, [["", "all sounds"], ["cand", "with candidates"], ["takes", "with my takes"], ["changed", "changed from git"]].map(([v, t]) => el("option", { value: v }, t)));
  function applyFilter() {
    const q = filter.value.trim().toLowerCase(), f = only.value;
    let n = 0;
    for (const d of document.querySelectorAll("details.sound")) {
      const id = d.dataset.sound, s = A.sounds.get(id);
      const ok = (!q || d.dataset.text.includes(q)) && (!f || (f === "cand" && cands(id).length) || (f === "takes" && (data.own[id] || []).length) || (f === "changed" && s && JSON.stringify(chosen(id)) !== JSON.stringify(originals(id))));
      d.style.display = ok ? "" : "none";
      if (ok) n++;
    }
    $("#shown").textContent = n + " shown";
  }
  const newName = el("input", { placeholder: "e.g. hooves_gallop", style: "width:180px" });
  const newBox = el("span", { class: "ctl" }, "Record a new sound: new_", newName, el("button", { onclick: () => {
    const id = "new_" + newName.value.trim().toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
    if (id.length < 5) return;
    if (!A.sounds.has(id) && !data.extra.find((x) => x.id === id)) data.extra.push({ id, title: id.slice(4).replace(/_/g, " "), loop: false, note: "An event of your own." });
    open.add(id); build(); location.hash = "#s=" + id; toHash();
  } }, "add"));
  function build() {
    const byCat = A.manifest.categories.map((c) => [c, A.manifest.sounds.filter((s) => s.category === c.id)]).filter(([, l]) => l.length);
    fill($("#cards"), byCat.map(([c, l]) => [el("h2", {}, `${c.title} (${l.length})`), l.map((s) => card(s.id))]),
      data.extra.length ? [el("h2", {}, `Not in the game yet (${data.extra.length})`), el("p", { class: "note" }, "Events the workspace has candidates or takes for and sounds.json does not list. Audition and record; borrow them from a game sound's section to install."), data.extra.map((x) => card(x.id))] : null);
    applyFilter();
  }
  function toHash() {
    const m = /^#s=(\w+)/.exec(location.hash), u = /^#ids=([\w,]+)/.exec(location.hash);
    const ids = m ? [m[1]] : u ? u[1].split(",") : [];
    ids.forEach((id) => { if (!open.has(id) && $("#s-" + id)) { open.add(id); redraw(id); } });
    if (u) { filter.value = ""; for (const d of document.querySelectorAll("details.sound")) d.style.display = ids.includes(d.dataset.sound) ? "" : "none"; $("#shown").textContent = ids.length + " shown: this unit's sounds"; }
    if (ids.length && $("#s-" + ids[0])) $("#s-" + ids[0]).scrollIntoView();
  }
  U.sayTo($("#said"));
  $("#listen").append(U.controls());
  page.append(SE.banner(), el("div", { class: "kv", style: "margin:10px 0" }, SE.recBar(), newBox), changes.node, HOWTO,
    el("div", { class: "kv", style: "margin:12px 0 0" }, filter, only, el("span", { id: "shown", class: "why" }), el("button", { onclick: () => { SE.stop(); } }, "■ stop")),
    el("p", { class: "note" }, "▶ on a source plays it as it would be installed: trimmed, at the game's loudness, through the game's own chain at the camera chosen above (loops play dry, looped). The loudness and the preview are worked out in the page; the installed file is rendered by ffmpeg from the same edit."),
    el("div", { id: "cards" }));
  build();
  toHash();
  window.addEventListener("hashchange", toHash);
  window.EffectsPage = { data: () => data, chosen, commit };
})();
