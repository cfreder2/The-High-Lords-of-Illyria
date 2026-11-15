// The Sounds page: every sound effect and voice line, what asks for it, what it measures, and a button to hear it
// as the game plays it. The playing is sound-core.js; the measurements are the sound audit's (sound-audit.json,
// made by import_sound_audit.py).
(() => {
  "use strict";
  const A = window.AuditSound, U = window.AuditSoundUi, el = U.el;
  const $ = (s, r = document) => r.querySelector(s);
  const state = { text: "", cat: "", flag: "", allEvents: false };
  const fix = (v, n = 1) => (typeof v === "number" ? v.toFixed(n) : "");
  const title = (id) => { const c = A.manifest.categories.find((x) => x.id === id); return c ? c.title : id; };
  const TRIGGER = { game: "the battle (sfx.cpp)", ambience: "the scene (an ambience loop)", page: "the page (sfx.js)", future: "nothing yet" };

  // ---------------------------------------------------------------- what plays, and what does not
  function summaryNote(rows) {
    const files = A.manifest.sounds.reduce((n, s) => n + s.files.length, 0), has = (key) => rows.filter((r) => r.flags.list.some((f) => f.key === key)).length;
    const cov = (A.audit && A.audit.coverage) || [], silent = cov.filter((c) => c.state.startsWith("silent")).length;
    let lines = 0, recorded = 0;
    for (const set of A.sets.values()) { const n = A.setCounts(set); lines += n.lines; recorded += n.recorded; }
    return el("div", { class: "callout" },
      el("b", {}, `${A.manifest.sounds.length} sounds in ${files} files; ${silent} of the ${cov.length} things a player expects to hear have no sound at all. `),
      `${has("never")} sounds are never triggered, ${has("bass")} are too bassy for a phone's speaker, ${has("quiet")} are too quiet or under target, ${has("tonal")} are near a pure tone, ${has("alike")} have variants that are near copies. `,
      `${recorded} of ${lines} voice lines are recorded. `,
      A.audit && A.audit.audited ? el("span", { class: "why" }, `Measurements and the events table: the sound audit of ${A.audit.audited.date} (commit ${A.audit.audited.commit}). The audit measured; it did not listen.`)
        : el("span", { class: "bad" }, "No measurements: run tools/audit/import_sound_audit.py."));
  }
  const refs = (text) => {  // `code` and (P0-1) as they are, lightly marked
    const out = [];
    text.split(/(`[^`]+`)/).forEach((part) => out.push(part.startsWith("`") ? el("code", {}, part.slice(1, -1)) : part));
    return out;
  };
  function events() {
    const cov = (A.audit && A.audit.coverage) || [], box = el("div", {});
    const draw = () => {
      const rows = cov.filter((c) => state.allEvents || c.state.indexOf("silent") >= 0);
      const cls = (s) => (s.startsWith("silent") ? "bad" : s.indexOf("silent") >= 0 || s.indexOf("speech") >= 0 || s.indexOf("unreachable") >= 0 ? "warn" : "");
      // Wholly silent first, then silent on one platform, then the rest, in the audit's order within each.
      const rank = (c) => (c.state.startsWith("silent") ? 0 : c.state.indexOf("silent") >= 0 ? 1 : 2);
      rows.sort((a, b) => rank(a) - rank(b));
      box.replaceChildren(
        el("p", { class: "note" }, `${cov.filter((c) => c.state.startsWith("silent")).length} events make no sound anywhere; ${cov.filter((c) => !c.state.startsWith("silent") && c.state.indexOf("silent") >= 0).length} more are silent on the desktop or in the Conquest. From the audit's coverage table. `,
          el("label", {}, el("input", { type: "checkbox", checked: state.allEvents, onchange: (e) => { state.allEvents = e.target.checked; draw(); } }), ` show all ${cov.length} events`)),
        el("table", { id: "events" }, el("thead", {}, el("tr", {}, ["#", "Event", "Where", "What plays", "Notes"].map((h) => el("th", {}, h)))),
          el("tbody", {}, rows.map((c, i) => el("tr", {}, el("td", { class: "num" }, i + 1), el("td", {}, el("b", {}, c.event)), el("td", { class: "why" }, c.group),
            el("td", { class: cls(c.state) }, c.state), el("td", { class: "why" }, refs(c.notes)))))));
    };
    draw();
    return box;
  }
  function worst(rows) {
    const head = ["#", "Sound", "Category", "Asked for by", "Files", "Seconds", "LUFS", "As played", "Under 300 Hz", "Mix", "What is wrong", ""];
    const numeric = new Set([0, 4, 5, 6, 7, 8, 9]);
    return el("table", { id: "worst" }, el("thead", {}, el("tr", {}, head.map((h, i) => el("th", { class: numeric.has(i) ? "num" : "" }, h)))),
      el("tbody", {}, rows.map((r, i) => {
        const s = r.s, c = A.code(s.id), lt = Math.max(-1, ...A.column(s, "pct_lt300")), played = Math.min(...A.column(s, "played_lufs_at_gain1"));
        return el("tr", { class: "row", "data-sound": s.id, onclick: () => { location.hash = "#s=" + s.id; } },
          el("td", { class: "num" }, i + 1), el("td", {}, el("a", {}, s.title), " ", el("span", { class: "why" }, s.id)), el("td", {}, title(s.category)),
          el("td", { class: s.emitted_by === "future" || (c && !c.asked.length) ? "bad" : "" }, c && !c.asked.length ? "nothing (in the enum)" : TRIGGER[s.emitted_by] || s.emitted_by),
          el("td", { class: "num" }, s.files.length), el("td", { class: "num" }, A.range(A.column(s, "seconds"), 2)), el("td", { class: "num" }, A.range(A.column(s, "lufs"), 1)),
          el("td", { class: "num" + (played <= -30 ? " warn" : "") }, isFinite(played) ? fix(played, 0) : ""), el("td", { class: "num " + (lt >= 70 ? "bad" : lt >= 40 ? "warn" : "") }, lt >= 0 ? Math.round(lt) + "%" : ""),
          el("td", { class: "num" }, (s.level || 0) + " dB"),
          el("td", {}, r.flags.list.filter((f) => f.cls !== "good").map((f) => el("span", { class: "badge " + ({ bad: "miss", warn: "warnb", info: "info" })[f.cls], title: f.title }, f.text))),
          el("td", {}, U.variants(s).firstChild));
      })));
  }

  // ---------------------------------------------------------------- the mix under load
  function stress() {
    const game = A.manifest.sounds.filter((s) => s.emitted_by === "game" && A.code(s.id) && A.code(s.id).asked.length);
    const ids = (f) => game.filter(f).map((s) => s.id);
    const sets = [
      ["A melee (swings, blows, falls)", ids((s) => /sword|heavy_swing|blunt|pierce|block|hit_bone|body_fall/.test(s.id))],
      ["A volley landing (bowstrings and arrows)", ids((s) => /bowstring|impact_arrow/.test(s.id))],
      ...A.manifest.categories.map((c) => [c.title, ids((s) => s.category === c.id)]).filter((x) => x[1].length && x[0] !== "Notifications"),
      ["Everything a battle sends", ids((s) => s.category !== "notifications")],
    ];
    const pick = el("select", {}, sets.map(([name, list], i) => el("option", { value: i }, `${name}: ${list.length} sounds`)));
    const rate = el("select", {}, [[0.12, "a skirmish (a blow every few ticks)"], [0.45, "two companies at grips"], [1, "300 men: every sound wanted every tick"]].map(([v, t], i) => el("option", { value: v, selected: i === 2 }, t)));
    const secs = el("select", {}, [5, 10, 20].map((v) => el("option", { value: v, selected: v === 10 }, v + " s")));
    const pool = el("span", { class: "pool" }, Array.from({ length: A.kMaxSounds }, () => el("i")));
    const read = el("div", { class: "read" }, "Fires the chosen sounds at battle rates through the game's throttle and its 24 voices, at the camera set above.");
    let running = false;
    const go = el("button", { id: "stress-go", onclick: async () => {
      if (running) { A.stopAll(); return; }
      running = true; go.textContent = "■ stop";
      const n = +secs.value;
      await A.stress(sets[+pick.value][1], n, +rate.value, (c, t) => {
        const busy = A.stripsBusy();
        [...pool.children].forEach((i, k) => i.classList.toggle("on", k < busy));
        pool.classList.toggle("full", busy >= A.kMaxSounds);
        const per = (v) => fix(v / Math.max(t, 0.1), 0);
        read.replaceChildren(`${fix(t, 1)} s · asked for ${c.offered} (${per(c.offered)}/s) · `, el("b", {}, `played ${c.played} (${per(c.played)}/s)`), " · ",
          el("span", { class: c.stolen ? "bad" : "" }, `cut off mid-sound ${c.stolen}`), " · ", el("span", {}, `faded out for a graver one ${c.faded || 0}`), " · ", el("span", { class: c.busy ? "warn" : "" }, `dropped, pool full ${c.busy}`),
          ` · merged or inside the sound's gap ${c.gap} · over 5 a tick or 24 a second ${c.capped} · out of earshot ${c.quiet}`);
      });
      running = false; go.textContent = "▶ stress";
      [...pool.children].forEach((i) => i.classList.remove("on"));
    } }, "▶ stress");
    return el("div", { class: "stress" }, el("div", { class: "kv" }, el("span", { class: "ctl" }, "Fire ", pick), el("span", { class: "ctl" }, "at ", rate), el("span", { class: "ctl" }, "for ", secs), go,
      el("span", { class: "ctl" }, "strips ", pool)), read,
      el("p", { class: "note", style: "margin-top:6px" }, "sfx.cpp lets each sound start once a tick and no sooner than its gap, five cues a tick by priority and 24 a second of real time (the gravest always); sfx.js plays 24 at once, and a full pool fades the slightest out over 12 ms to start an equal or graver one. \"Faded out\" counts those; \"cut off\" would be a hard stop, and there are none now. At a far camera most of it is dropped as out of earshot instead."));
  }

  // ---------------------------------------------------------------- every sound
  let seen = null;
  function picture(file, wave, spec, nums, m) {
    return async () => {
      try {
        const buf = await A.decode(file);
        A.drawWave(wave, buf);
        A.drawSpec(spec, buf);
        if (!m) nums.prepend(el("b", {}, fix(buf.duration, 2) + " s"), " ");
      } catch (e) { nums.replaceChildren(el("span", { class: "bad" }, "file missing")); }
    };
  }
  function card(r) {
    const s = r.s, c = A.code(s.id), kind = A.kindOf(s);
    const variants = s.files.map((file, i) => {
      const m = A.measured(file), dpr = Math.min(2, window.devicePixelRatio || 1);
      const wave = el("canvas", { width: 300 * dpr, height: 48 * dpr, style: "width:300px;height:48px", title: "waveform" });
      const spec = el("canvas", { width: 300 * dpr, height: 72 * dpr, style: "width:300px;height:72px", title: "spectrogram, 60 Hz to 16 kHz; the dashed line is 300 Hz" });
      const nums = el("div", { class: "nums" }, m ? [el("b", {}, fix(m.seconds, 2) + " s"), ` · ${fix(m.lufs)} LUFS (target ${fix(m.target_lufs, 0)}) · as played ${fix(m.played_lufs_at_gain1)} LUFS · peak ${fix(m.true_peak_db)} dBTP`, el("br"),
        el("span", { class: m.pct_lt300 >= 70 ? "bad" : "" }, `${fix(m.pct_lt300, 0)}% under 300 Hz`), ` · centre ${Math.round(m.centroid_hz)} Hz · `,
        el("span", { class: m.dominant_pct >= 50 ? "warn" : "" }, `${fix(m.dominant_pct, 0)}% at ${Math.round(m.dominant_hz)} Hz`), ` · level peaks ${Math.round(m.attack_ms)} ms in`,
        typeof m.closest_wave_corr === "number" ? [" · ", el("span", { class: m.closest_wave_corr >= 0.6 ? "warn" : "" }, `like variant ${String(m.closest_variant).replace("_", "")}: ${fix(m.closest_wave_corr, 2)}`)] : null,
        m.loop_seconds ? ` · loop ${fix(m.loop_seconds, 1)} s, ${fix(m.loop_level_range_db)} dB of movement in it` : null] : null);
      const b = U.button(`▶ ${i + 1}`, () => A.play(s.id, i), file);
      b.dataset.what = `${s.title} ${i + 1}`;
      const row = el("div", { class: "variant" }, b, wave, spec, nums);
      row._draw = picture(file, wave, spec, nums, m);
      seen.observe(row);
      return row;
    });
    const all = U.button("▶ all in turn", async () => {
      let total = 0;
      for (let i = 0; i < s.files.length; ++i) {
        const res = await A.play(s.id, i);
        if (!res.seconds) return res;
        total += res.seconds + 0.25;
        await new Promise((done) => setTimeout(done, (res.seconds + 0.25) * 1000));
      }
      return { text: `${s.files.length} variants, ${total.toFixed(1)} s`, seconds: 0.3 };
    }, "each variant, one after another");
    all.dataset.what = s.title;
    let loop = null;
    if (kind === "loop") {
      loop = el("button", { title: "sfx.js setLayer: looped round its loop points at level 1 x the mix level x 4 (dry: the file looped as it is)", onclick: async () => {
        const on = !A.layerOn(s.id);
        await A.layer(s.id, on ? 1 : 0);
        loop.classList.toggle("on", on);
        loop.textContent = on ? "■ looping" : "↻ loop";
        U.say(on ? `${s.title}: looping ${A.settings.mode === "dry" ? "dry" : `as the game does at full level (${(20 * Math.log10(4) + (s.level || 0)).toFixed(1)} dB on the ambience bus)`}` : `${s.title}: stopped`);
      } }, "↻ loop");
    }
    const gap = c ? (c.gapTicks ? `at most ${fix(30 / c.gapTicks, 0)} a second (gap ${c.gapTicks} ticks)` : "no gap") : null;
    return el("div", { class: "sound", id: "s-" + s.id, "data-sound": s.id },
      el("div", { class: "top" }, el("h3", {}, s.title), el("code", {}, s.id), el("span", { class: "badge info" }, title(s.category)),
        r.flags.list.map((f) => el("span", { class: "badge " + ({ bad: "miss", warn: "warnb", info: "info", good: "good" })[f.cls], title: f.title }, f.text))),
      el("div", { class: "when" }, el("b", {}, "When: "), s.when, s.future ? el("span", { class: "why" }, " · for: " + s.future) : null),
      el("div", { class: "kv" }, el("span", {}, "Mix level ", el("b", {}, (s.level || 0) + " dB")), el("span", {}, "Behaviour ", el("b", {}, s.behaviour)),
        c ? [el("span", {}, "Priority ", el("b", {}, c.priority + " of 5")), el("span", {}, "Throttle ", el("b", {}, gap)),
          el("span", {}, "Heard to ", el("b", {}, A.positional(s.id) ? (c.loud ? A.kLoudHearing : A.kHearing) + " m" : "anywhere (not positional)"))] : null,
        el("span", {}, "Asked for by ", el("b", { class: s.emitted_by === "future" || (c && !c.asked.length) ? "bad" : "" }, c && !c.asked.length ? "nothing" : TRIGGER[s.emitted_by] || s.emitted_by))),
      c && c.asked.length ? el("details", {}, el("summary", {}, `${c.asked.length} place${c.asked.length > 1 ? "s" : ""} in the code ask for it (Sound::${c.enum})`), c.asked.map((a) => el("code", {}, `${a.at}   ${a.code}`))) : null,
      el("div", { class: "kv", style: "margin:6px 0" }, U.variants(s).firstChild, all, loop),
      variants);
  }
  function applyFilter() {
    const text = state.text.toLowerCase();
    let shown = 0;
    for (const n of document.querySelectorAll("#cards .sound, #worst tbody tr")) {
      const r = rowsById.get(n.dataset.sound), s = r.s;
      const hay = `${s.id} ${s.title} ${s.when} ${s.category} ${r.flags.list.map((f) => f.text).join(" ")}`.toLowerCase();
      const ok = (!text || hay.indexOf(text) >= 0) && (!state.cat || s.category === state.cat) && (!state.flag || r.flags.list.some((f) => f.key === state.flag));
      n.hidden = !ok;
      if (ok && n.classList.contains("sound")) ++shown;
    }
    $("#shown").textContent = `${shown} of ${rowsById.size} sounds`;
  }
  const rowsById = new Map();
  function filterBar() {
    const cat = (id, label) => el("button", { class: state.cat === id ? "on" : "", onclick: (e) => {
      state.cat = id;
      for (const b of e.target.parentNode.children) b.classList.toggle("on", b === e.target);
      applyFilter();
    } }, label);
    return el("div", { class: "kv", style: "margin:6px 0 4px", id: "filter" },
      el("span", { class: "ctl" }, "Search ", el("input", { type: "search", placeholder: "arrow, gate, snd_gore…", style: "font:inherit;background:#2c3038;color:inherit;border:1px solid var(--line);border-radius:5px;padding:3px 8px;width:220px",
        oninput: (e) => { state.text = e.target.value; applyFilter(); } })),
      el("span", { class: "chips" }, cat("", "all"), A.manifest.categories.map((c) => cat(c.id, c.title))),
      el("span", { class: "ctl" }, "With ", el("select", { onchange: (e) => { state.flag = e.target.value; applyFilter(); } },
        [["", "any or no fault"], ["never", "never triggered"], ["bass", "too bassy for phones"], ["quiet", "too quiet"], ["tonal", "near a pure tone"], ["alike", "variants near copies"],
          ["peak", "peak or clipping"], ["late", "peaks late"], ["first", "first cue dropped"]].map(([v, t]) => el("option", { value: v }, t)))),
      el("span", { id: "shown", class: "why" }));
  }

  // ---------------------------------------------------------------- the voices
  function voices() {
    const sets = [...A.sets.values()];
    let lines = 0, rec = 0, ph = 0, sp = 0, gr = 0;
    for (const s of sets) { const n = A.setCounts(s); lines += n.lines; rec += n.recorded; ph += n.placeholder; sp += n.speech; gr += n.growl; }
    const voice = (s) => { const v = A.speechVoice(s); return v ? v.name : "(the device's default)"; };
    const table = el("table", {}, el("thead", {}, el("tr", {}, ["Voice set", "Units", "Lines", "Recorded", "Rendered placeholders", "Browser speech", "Noise growl", "Speech voice on this device"].map((h, i) => el("th", { class: i >= 2 && i <= 6 ? "num" : "" }, h)))),
      el("tbody", {}, sets.map((s) => {
        const n = A.setCounts(s);
        return el("tr", { class: "row", onclick: () => { const d = $("#set-" + s.id); d.open = true; d.scrollIntoView(); window.scrollBy(0, -110); } },
          el("td", {}, el("a", {}, s.name), " ", el("span", { class: "why" }, s.id)), el("td", {}, s.units.join(", ")), el("td", { class: "num" }, n.lines),
          el("td", { class: "num " + (n.recorded ? "ok" : "bad") }, n.recorded), el("td", { class: "num" }, n.placeholder), el("td", { class: "num" + (n.speech ? " warn" : "") }, n.speech || ""),
          el("td", { class: "num" + (n.growl ? " warn" : "") }, n.growl || ""), el("td", { class: "why" }, s.verbal ? voice(s) : "none: wordless"));
      }), el("tr", {}, el("td", {}, el("b", {}, "All")), el("td", {}), el("td", { class: "num" }, el("b", {}, lines)), el("td", { class: "num " + (rec ? "ok" : "bad") }, el("b", {}, rec)), el("td", { class: "num" }, ph),
        el("td", { class: "num warn" }, sp), el("td", { class: "num warn" }, gr), el("td", {}))));
    const details = sets.map((s) => {
      const d = el("details", { class: "set", id: "set-" + s.id }, el("summary", {}, el("b", {}, s.name), " ", el("span", { class: "why" }, s.units.join(", ")), " · ", U.voiceCounts(s)));
      d.addEventListener("toggle", () => {
        if (!d.open || d._built) return;
        d._built = true;
        d.append(el("p", { class: "note" }, el("b", {}, "Casting: "), s.casting, s.fallback ? ` Situations it lacks are spoken by the ${s.fallback} set.` : "",
          s.verbal ? ` Unrecorded lines are read by the browser's speech synthesiser at pitch ${s.placeholder.pitch}, rate ${s.placeholder.rate}.` : " Wordless: an unrecorded line is a burst of band-passed noise."), U.voiceLines(s));
      });
      return d;
    });
    return el("div", { id: "voices" },
      el("p", { class: "note" }, `What the game plays for a line: its recording if voice/available.json lists one (${rec} do), else a rendered placeholder (cries of pain and death), else the browser's speech synthesiser reading the words, else for a wordless set a noise growl. Each button says which. The speech voice is picked from the device's English voices by a hash of the set's name, so it differs from one device to the next.`),
      table, details);
  }

  // ---------------------------------------------------------------- the page
  function toHash() {
    const m = /^#s=(\w+)/.exec(location.hash), v = /^#set=(\w+)/.exec(location.hash);
    for (const n of document.querySelectorAll(".sound.target")) n.classList.remove("target");
    const node = m ? $("#s-" + m[1]) : v ? $("#set-" + v[1]) : location.hash === "#voices" ? $("#voices-h") : location.hash === "#events" ? $("#events-h") : null;
    if (!node) return;
    if (m) { node.hidden = false; node.classList.add("target"); }
    if (v) node.open = true;
    node.scrollIntoView();
    window.scrollBy(0, -($("#topbar").offsetHeight + 10));
  }
  async function start() {
    await A.load();
    U.sayTo($("#said"));
    $("#listen").replaceWith(U.controls());
    seen = new IntersectionObserver((list) => {
      for (const e of list) if (e.isIntersecting && e.target._draw) { e.target._draw(); e.target._draw = null; seen.unobserve(e.target); }
    }, { rootMargin: "300px" });
    const rows = A.manifest.sounds.map((s) => ({ s, flags: A.flags(s) }));
    for (const r of rows) rowsById.set(r.s.id, r);
    const ranked = [...rows].sort((a, b) => b.flags.score - a.flags.score);
    const byCat = A.manifest.categories.map((c) => [c, rows.filter((r) => r.s.category === c.id)]).filter((x) => x[1].length);
    $("#page").replaceChildren(
      summaryNote(rows),
      el("h2", { id: "events-h" }, "Silent: events with no sound"), events(),
      el("h2", {}, "Sounds, worst first"),
      el("p", { class: "note" }, "Never triggered first, then by how many faults and how much of the sound is under 300 Hz. \"As played\" is the loudness at full cue gain with the mix level and the default volume. Click a row for the sound."),
      filterBar(), worst(ranked),
      el("h2", {}, "The mix under load"), stress(),
      el("h2", {}, "Every sound"),
      el("div", { id: "cards" }, byCat.map(([c, list]) => [el("h3", { style: "margin-top:18px" }, `${c.title} (${list.length})`), list.map(card)])),
      el("h2", { id: "voices-h" }, "Voices"), voices());
    applyFilter();
    window.addEventListener("hashchange", toHash);
    toHash();
  }
  window.__sounds = { ready: start().catch((e) => { console.error(e); $("#page").replaceChildren(el("p", { class: "bad", style: "margin-top:20px" }, String(e.message || e))); throw e; }) };
})();
