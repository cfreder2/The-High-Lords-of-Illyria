// The pieces of page both audit pages build from AuditSound (sound-core.js): the listening controls, a sound's
// play buttons, a voice set's lines.
(() => {
  "use strict";
  const A = window.AuditSound;
  function el(tag, attrs, ...kids) {
    const e = document.createElement(tag);
    for (const k in attrs || {}) {
      const v = attrs[k];
      if (k === "class") e.className = v;
      else if (k === "style") e.style.cssText = v;
      else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
      else if (v !== false && v != null) e.setAttribute(k, v === true ? "" : v);
    }
    for (const k of kids.flat(Infinity)) if (k != null && k !== false) e.append(k.nodeType ? k : document.createTextNode(String(k)));
    return e;
  }
  const U = { el };

  // Where the last thing played is told (one line, set by each page).
  let said = null;
  U.sayTo = (node) => { said = node; };
  U.say = (text, cls) => { if (said) { said.textContent = text; said.className = "said " + (cls || ""); } };

  // A button that plays, lit while it sounds. play() resolves to {text, seconds} or a string.
  U.button = (label, play, title, cls) => {
    const b = el("button", { class: "play " + (cls || ""), title, onclick: async (e) => {
      e.stopPropagation();
      const r = await play();
      const text = typeof r === "string" ? r : r ? r.text : "";
      const silent = r && typeof r !== "string" && !r.seconds;
      U.say(`${b.dataset.what || label}: ${text}`, silent ? "bad" : "");
      if (silent) { b.classList.add("mute"); setTimeout(() => b.classList.remove("mute"), 900); return; }
      b.classList.add("sounding");
      setTimeout(() => b.classList.remove("sounding"), Math.min(4000, ((r && r.seconds) || 0.5) * 1000));
    } }, label);
    return b;
  };
  // One sound's buttons: any variant (as the game picks: never the last again), then each.
  U.variants = (s, o) => {
    o = o || {};
    const row = el("span", { class: "variants" });
    const any = U.button("▶", () => A.play(s.id, -1, o.play), "any variant, as the game picks");
    any.dataset.what = s.title;
    row.append(any);
    s.files.forEach((f, i) => {
      const b = U.button(String(i + 1), () => A.play(s.id, i, o.play), f);
      b.dataset.what = `${s.title} ${i + 1}`;
      row.append(b);
    });
    return row;
  };
  // A sound in one line, for a list: its buttons, its name, when it plays.
  U.soundLine = (id, why, o) => {
    const s = A.sounds.get(id);
    if (!s) return el("div", { class: "soundline" }, el("span", { class: "bad" }, id + ": not in sounds.json"));
    const flags = A.flags(s).list.filter((f) => f.cls === "bad" || f.cls === "warn");
    return el("div", { class: "soundline" }, U.variants(s, o), el("b", {}, s.title), " ", el("a", { class: "dimlink", href: "sounds.html#s=" + s.id, title: "on the Sounds page" }, s.id),
      why ? el("span", { class: "why" }, " · " + why) : null, flags.map((f) => el("span", { class: "badge " + (f.cls === "bad" ? "miss" : "warnb"), title: f.title }, f.text)));
  };

  // The listening controls: the volume, dry or as the game plays it, and from where.
  U.controls = (onChange) => {
    const st = A.settings, out = el("span", { class: "ctl listen" });
    const tell = el("span", { class: "tell" });
    const show = () => {
      const p = A.place("snd_weap_sword_flesh", st.camera, st.off), loud = A.place("snd_expl_satchel", st.camera, st.off);
      tell.textContent = st.mode === "dry" ? "the files as they are" : `a blow: ${A.describe(p, 0)} · a blast: ${loud.dropped ? "not played" : (20 * Math.log10(loud.gain)).toFixed(1) + " dB"}`;
      tell.className = "tell" + (st.mode !== "dry" && p.dropped ? " bad" : "");
      if (onChange) onChange();
    };
    const cam = el("input", { type: "range", min: Math.log(2), max: Math.log(700), step: 0.01, value: Math.log(st.camera), title: "the camera's distance from what it looks at" });
    const camN = el("b", {}, Math.round(st.camera) + " m");
    const setCam = (v) => { st.camera = v; cam.value = Math.log(v); camN.textContent = Math.round(v) + " m"; show(); };
    cam.oninput = () => setCam(Math.exp(+cam.value));
    const off = el("input", { type: "range", min: 0, max: 150, step: 1, value: st.off, style: "width:70px", title: "how far to the right of the view's centre the sound is made" });
    const offN = el("b", {}, st.off + " m");
    off.oninput = () => { st.off = +off.value; offN.textContent = st.off + " m"; show(); };
    const mode = (m, label, title) => el("button", { class: st.mode === m ? "on" : "", title, onclick: (e) => {
      st.mode = m;
      for (const b of e.target.parentNode.querySelectorAll("button")) b.classList.toggle("on", b === e.target);
      show();
    } }, label);
    const vol = el("input", { type: "range", min: 0, max: 1, step: 0.01, value: st.volume, style: "width:80px", title: "the game's effects volume (0.8 as it ships)" });
    vol.oninput = () => { st.volume = +vol.value; A.applyVolume(); };
    out.append("Volume ", vol, " ", el("span", {}, mode("game", "as the game plays it", "through the game's distance gain, low-pass, reverb send, pan and 12-strip pool"), mode("dry", "dry", "the file as it is")),
      " Camera ", cam, camN, " ", el("select", { onchange: (e) => { if (e.target.value) setCam(+e.target.value); e.target.value = ""; } }, el("option", { value: "" }, "set to…"),
        A.CAMERAS.map(([d, name]) => el("option", { value: d }, `${d} m: ${name}`))),
      " Off centre ", off, offN, " ", el("button", { onclick: () => { A.stopAll(); U.say("stopped"); } }, "■ stop all"), tell);
    show();
    return out;
  };

  // A voice set: who uses it, and each situation's lines with what plays for each today.
  const KIND = { recorded: ["recorded", "good"], placeholder: ["rendered placeholder", "info"], speech: ["browser speech", "warnb"], growl: ["noise growl", "warnb"] };
  U.lineButton = (set, line) => {
    const src = A.lineSource(set, line), k = KIND[src.kind];
    const b = U.button(`▶ ${k[0]}`, () => A.playLine(set, line), src.file || (src.kind === "speech" ? "No file: the game has the browser's speech synthesiser read it." : "No file: the game makes a noise burst."), "kind-" + src.kind);
    b.dataset.what = `${set.name} "${line.text}"`;
    return b;
  };
  U.voiceCounts = (set) => {
    const n = A.setCounts(set);
    return el("span", {}, el("b", {}, n.lines), " lines: ", el("span", { class: n.recorded ? "ok" : "bad" }, `${n.recorded} recorded`), `, ${n.placeholder} rendered placeholders, `,
      el("span", { class: n.speech + n.growl ? "warn" : "" }, set.verbal ? `${n.speech} browser speech` : `${n.growl} noise growl`));
  };
  // The lines. only: situation ids to show (all otherwise).
  U.voiceLines = (set, only) => {
    const situations = A.lines.situations, box = el("div", { class: "voicelines" });
    for (const sit of set.situations) {
      if (only && !only.includes(sit.id)) continue;
      const info = situations.find((x) => x.id === sit.id) || { title: sit.id, when: "" };
      box.append(el("div", { class: "situation" },
        el("div", { class: "sithead" }, el("b", {}, info.title), " ", el("span", { class: "why" }, `${sit.id} · ${info.when}${sit.note ? " · direction: " + sit.note : ""}`)),
        el("div", { class: "lines" }, sit.lines.map((l) => el("span", { class: "line" }, U.lineButton(set, l), el("span", { class: "text" }, l.text))))));
    }
    return box;
  };

  window.AuditSoundUi = U;
})();
