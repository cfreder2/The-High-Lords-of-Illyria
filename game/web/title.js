// The title screen and the menu's pages: the campaign's three save slots,
// skirmishes, settings (graphics, sound, gameplay, controls), the controls
// reference and the credits. Modes start through IllyriaGame.startMode (the
// page's script in index.html), which calls the engine's one entry for every
// mode, illyria_start_mode (docs/GAME_DESIGN.md §4.6).
(function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const game = () => window.IllyriaGame;
  const api = () => (window.IllyriaGame ? window.IllyriaGame.api() : null);
  const version = (document.querySelector(".title-foot span") || { textContent: "" }).textContent.replace(/^v/, "");
  const store = {
    get(key) { try { return localStorage.getItem(key); } catch (e) { return null; } },
    set(key, value) { try { localStorage.setItem(key, value); return true; } catch (e) { return false; } },
    remove(key) { try { localStorage.removeItem(key); } catch (e) { /* nothing to do */ } },
  };
  const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

  // ---- Gameplay preferences (Settings, Gameplay) ----
  const prefKey = "illyria.settings.v1";
  const prefs = { speed: 1, difficulty: 1, autosave: true, confirmDisband: true, hints: true };
  try { Object.assign(prefs, JSON.parse(store.get(prefKey)) || {}); } catch (e) { /* defaults */ }
  const savePrefs = () => store.set(prefKey, JSON.stringify(prefs));

  // ---- Save slots ----
  // A slot holds the engine's saved campaign (Campaign::save, a few tens of
  // kilobytes of text) and what the slot shows: the day, gold, cities, lords.
  const kSlots = 3;
  const slotKey = (k) => `illyria.save.${k}`;
  let slot = 0;           // the slot the campaign being played saves into (0: none yet)
  let savedDay = -1;      // the last day saved, so each dawn saves once
  let lastState = null;   // the campaign's last state, for saving from inside its 3D battle
  function readSlot(k) {
    const text = store.get(slotKey(k));
    if (!text) return null;
    try {
      const d = JSON.parse(text);
      return d && typeof d.game === "string" ? d : { bad: true };
    } catch (e) { return { bad: true }; }
  }
  function latestSlot() {
    let best = 0, when = -1;
    for (let k = 1; k <= kSlots; k++) {
      const d = readSlot(k);
      if (d && !d.bad && d.saved > when) { best = k; when = d.saved; }
    }
    return best;
  }
  function snapshot() {
    const a = api();
    if (!a) return null;
    const text = a.campSave();
    if (!text) return null;
    const st = (game().campState && game().campState()) || lastState || {};
    return { v: 1, version, game: text, day: st.day || 0, gold: st.gold || 0, cities: st.cities || 0, hard: !!st.hard, saved: Date.now() };
  }
  function saveTo(k, quiet) {
    const d = snapshot();
    if (!d) return false;
    if (!store.set(slotKey(k), JSON.stringify(d))) {
      toast("Could not save: this browser's storage is full or switched off");
      return false;
    }
    slot = k;
    savedDay = d.day;
    if (!quiet) toast(`Saved in slot ${k}: day ${d.day}`);
    return true;
  }
  // At each dawn (Settings: Autosave every day), and on the way to the menu.
  function autosave(force) {
    if (!slot || (!force && !prefs.autosave)) return;
    saveTo(slot, true);
  }
  function campaignTick(st) {
    lastState = st;
    if (!st || typeof st.day !== "number") return;
    if (savedDay < 0) savedDay = st.day;
    if (st.day > savedDay && !st.pending && !st.winner) autosave(false);
  }
  function startCampaign(k, hard) {
    if (!game().startMode("campaign", { hard: hard ? 1 : 0, speed: prefs.speed })) return failed("campaign");
    slot = k;
    savedDay = -1;
    lastState = null;
    if (k) saveTo(k, true);
    closeAll();
  }
  function loadSlot(k) {
    const d = readSlot(k);
    if (!d || d.bad) return;
    busy("Marching the armies back", `Slot ${k} · day ${d.day}`);
    // Let the page draw the notice before the engine reads the save.
    setTimeout(() => {
      const ok = game().startMode("campaign", { save: d.game, speed: prefs.speed });
      unbusy();
      if (!ok) {
        notice("This save cannot be read", (api() && api().modeError()) || "It may be from an older version of the game.");
        return;
      }
      slot = k;
      savedDay = d.day;
      lastState = null;
      closeAll();
      toast(`Day ${d.day}: the war goes on`);
    }, 60);
  }
  function failed(id) {
    notice("That would not start", (api() && api().modeError()) || `The ${id} could not begin.`);
  }

  // ---- Small things over everything ----
  const toast = (t) => game() && game().toast(t);
  let confirmYes = null;
  function confirm(title, text, yes, onYes) {
    $("confirmTitle").textContent = title;
    $("confirmText").textContent = text || "";
    $("confirmYes").textContent = yes || "Yes";
    $("confirmNo").classList.remove("hidden");
    $("confirmNo").textContent = "Cancel";
    confirmYes = onYes;
    $("confirmBox").classList.remove("hidden");
    $("confirmNo").focus();
  }
  function notice(title, text) {
    confirm(title, text, "OK", null);
    $("confirmNo").classList.add("hidden");
    $("confirmYes").focus();
  }
  function closeConfirm() { $("confirmBox").classList.add("hidden"); confirmYes = null; }
  $("confirmNo").onclick = closeConfirm;
  $("confirmYes").onclick = () => { const f = confirmYes; closeConfirm(); if (f) f(); };
  function busy(title, text) {
    $("busyTitle").textContent = title + "…";
    $("busyText").textContent = text || "";
    $("busy").classList.remove("hidden");
  }
  const unbusy = () => $("busy").classList.add("hidden");

  // ---- Pages ----
  const pages = { campaign: $("campaignScreen"), skirmish: $("skirmishScreen"), settings: $("settingsScreen"),
                  controls: $("controlsScreen"), credits: $("creditsScreen") };
  const open = [];  // the pages open, the top one last
  function openPage(name, arg) {
    const el = pages[name];
    if (!el) return;
    if (game()) game().closePopups();
    render[name](el, arg);
    const at = open.indexOf(name);
    if (at >= 0) open.splice(at, 1);
    open.push(name);
    for (const n of Object.keys(pages)) pages[n].style.zIndex = String(20 + Math.max(0, open.indexOf(n)));
    el.classList.remove("hidden");
    document.body.classList.add("screen-open");
    focusFirst(el);
  }
  function back() {
    const name = open.pop();
    if (name) pages[name].classList.add("hidden");
    if (!open.length) {
      document.body.classList.remove("screen-open");
      if (document.body.classList.contains("at-title")) focusFirst($("titleScreen"));
    }
  }
  function closeAll() { while (open.length) back(); }
  for (const el of Object.values(pages)) el.querySelector(".back").onclick = back;
  function focusFirst(root) {
    const b = root.querySelector(".body button, .title-menu button, button:not(.back)");
    if (b && matchMedia("(pointer: fine)").matches) b.focus({ preventScroll: true });
  }

  // ---- The title screen ----
  // Horde and Capture the Flag come from another part of the game: until the
  // engine has them (illyria_mode_available), they are marked "soon".
  const extraModes = [
    { id: "horde", name: "Horde", blurb: "Wave on wave of the Red Hand against your line. How long can you hold?" },
    { id: "ctf", name: "Capture the Flag", blurb: "Carry their banner home while keeping your own." },
  ];
  function available(id) {
    const a = api();
    const m = window.IllyriaModes && window.IllyriaModes[id];
    return !!(a && a.modeAvailable(id)) || !!(m && m.available);
  }
  function startExtra(mode) {
    if (!available(mode.id)) {
      notice(`${mode.name}: coming soon`, mode.blurb + " It is being built now.");
      return;
    }
    const m = window.IllyriaModes && window.IllyriaModes[mode.id];
    // A mode may show its own page of options first, then call IllyriaGame.startMode itself.
    if (m && m.menu) { m.menu(); return; }
    if (!game().startMode(mode.id, {})) failed(mode.name);
    else { slot = 0; closeAll(); }
  }
  function lordsName(hard) { return hard ? "Hard lords" : "Easy lords"; }
  function renderTitle() {
    const root = $("titleMenu");
    const k = latestSlot(), d = k ? readSlot(k) : null;
    let html = "";
    if (d) html += `<button class="big primary" data-act="continue"><span>Continue</span><small>Day ${d.day} · ${d.cities} ${d.cities === 1 ? "city" : "cities"} · ${lordsName(d.hard)}</small></button>`;
    html += `<button class="big${d ? "" : " primary"}" data-act="campaign"><span>Campaign</span><small>${d ? "New game · load" : "The war for all Illyria"}</small></button>`;
    html += `<button class="big" data-act="skirmish"><span>Skirmish</span><small>Oakhaven Ford · a siege</small></button>`;
    for (const m of extraModes) {
      html += `<button class="big" data-mode="${m.id}"><span>${m.name}</span>${available(m.id) ? "<small>New</small>" : '<span class="soon">soon</span>'}</button>`;
    }
    html += `<div class="row"><button data-act="manual">Manual</button><button data-act="settings">Settings</button>` +
            `<button data-act="controls">Controls</button><button data-act="credits">About</button></div>`;
    root.innerHTML = html;
    for (const b of root.querySelectorAll("button")) {
      b.onclick = () => {
        const act = b.dataset.act;
        if (b.dataset.mode) startExtra(extraModes.find((m) => m.id === b.dataset.mode));
        else if (act === "continue") loadSlot(k);
        else if (act === "manual") manual();
        else openPage(act);
      };
    }
  }
  function show() {
    closeAll();
    renderTitle();
    focusFirst($("titleScreen"));
  }

  // The manual: a book of its own, beside the game (manual.html).
  function manual() {
    fetch("manual.html", { method: "HEAD" }).then((r) => {
      if (!r.ok) throw new Error("missing");
      window.open("manual.html", "_blank", "noopener");
    }).catch(() => notice("The manual", "The manual is still being written. Until it is, Controls shows every tap, key and click."));
  }

  // ---- Page contents ----
  const render = {};
  const when = (ms) => {
    try { return new Date(ms).toLocaleString(undefined, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }); } catch (e) { return ""; }
  };
  // The campaign: three slots, each a war of its own. saving: the ☰ menu's Save, choosing where.
  render.campaign = (el, saving) => {
    const body = el.querySelector(".body");
    el.querySelector("h2").textContent = saving ? "Save the campaign" : "Campaign";
    let html = `<p class="lead">${saving ? "Choose a slot for this campaign. It saves there every dawn from now on." :
      "Eight lords, eighty cities, one continent. Each slot keeps a war of its own, saved every dawn in this browser."}</p>`;
    for (let k = 1; k <= kSlots; k++) {
      const d = readSlot(k);
      const playing = k === slot && game().inCampaign();
      if (!d) {
        html += `<div class="slot panel empty"><span class="name">Slot ${k}</span><span class="when"></span><div class="facts">Empty</div>` +
          `<div class="acts">${saving ? `<button class="go" data-save="${k}">Save here</button>` : `<button class="go" data-new="${k}">New game</button>`}</div></div>`;
      } else if (d.bad) {
        html += `<div class="slot panel bad"><span class="name">Slot ${k}</span><span class="when"></span><div class="facts">This slot is damaged.</div>` +
          `<div class="acts">${saving ? `<button data-save="${k}">Save over it</button>` : `<button data-new="${k}">New game</button>`}<button class="del" data-del="${k}">Delete</button></div></div>`;
      } else {
        html += `<div class="slot panel"><span class="name">Slot ${k}${playing ? " · playing" : ""}</span><span class="when">${esc(when(d.saved))}</span>` +
          `<div class="facts">Day <b>${d.day}</b> · <b>${d.gold}</b> gold · <b>${d.cities}</b> ${d.cities === 1 ? "city" : "cities"} · ${lordsName(d.hard)}</div>` +
          `<div class="acts">` + (saving ? `<button class="go" data-save="${k}">Save over it</button>` :
            `<button class="go" data-load="${k}">${playing ? "Reload" : "Continue"}</button><button data-new="${k}">New game</button>`) +
          `<button class="del" data-del="${k}">Delete</button></div></div>`;
      }
    }
    if (!saving) html += `<p class="note">Saves stay in this browser on this device. Clearing the browser's data clears them.</p>`;
    body.innerHTML = html;
    body.querySelectorAll("[data-load]").forEach((b) => { b.onclick = () => loadSlot(+b.dataset.load); });
    body.querySelectorAll("[data-save]").forEach((b) => {
      b.onclick = () => {
        const k = +b.dataset.save, d = readSlot(k);
        const go = () => { if (saveTo(k, false)) back(); };
        if (d && !d.bad) confirm(`Save over slot ${k}?`, `Day ${d.day} of that campaign will be lost.`, "Save over it", go);
        else go();
      };
    });
    body.querySelectorAll("[data-del]").forEach((b) => {
      b.onclick = () => {
        const k = +b.dataset.del;
        confirm(`Delete slot ${k}?`, "That campaign is gone for good.", "Delete", () => {
          store.remove(slotKey(k));
          if (slot === k) slot = 0;
          render.campaign(el, saving);
          if (document.body.classList.contains("at-title")) renderTitle();
        });
      };
    });
    body.querySelectorAll("[data-new]").forEach((b) => {
      b.onclick = () => {
        const k = +b.dataset.new, d = readSlot(k);
        const go = () => chooseLords(body, k);
        if (d && !d.bad) confirm(`Start over in slot ${k}?`, `Day ${d.day} of the campaign there will be lost.`, "New game", go);
        else go();
      };
    });
  };
  // A new campaign: how well the other seven lords play.
  function chooseLords(body, k) {
    let hard = prefs.difficulty ? 1 : 0;
    body.innerHTML = `<p class="lead">Slot ${k}: how well should the seven other lords of Illyria play?</p>` +
      `<div class="choice"><button data-hard="0"><b>Easy</b><small>Knights: slow, careless, short of gold</small></button>` +
      `<button data-hard="1"><b>Hard</b><small>Warlords: every trick, no mercy</small></button></div>` +
      `<p class="note">The computer plays by the same rules as you on either. The default is in Settings, Gameplay.</p>` +
      `<button class="big primary" data-start="1" style="margin-top:14px"><span>Raise your banner</span><small>Day 1</small></button>`;
    const sync = () => body.querySelectorAll("[data-hard]").forEach((b) => b.classList.toggle("on", +b.dataset.hard === hard));
    body.querySelectorAll("[data-hard]").forEach((b) => { b.onclick = () => { hard = +b.dataset.hard; sync(); }; });
    sync();
    body.querySelector("[data-start]").onclick = () => startCampaign(k, hard);
    focusFirst(body.parentElement);
  }
  render.skirmish = (el) => {
    el.querySelector(".body").innerHTML = `<p class="lead">One battle, fought out in full. Nothing carries over.</p>` +
      `<button class="big" data-s="0"><span>Oakhaven Ford<br><small style="text-align:left;display:block">Hold the ford against the Red Hand</small></span><small>Battle</small></button>` +
      `<div style="height:8px"></div>` +
      `<button class="big" data-s="1"><span>The siege<br><small style="text-align:left;display:block">Storm a neutral town and take its courtyard</small></span><small>Siege</small></button>`;
    el.querySelectorAll("[data-s]").forEach((b) => {
      b.onclick = () => {
        busy("Raising the banners", b.dataset.s === "1" ? "The siege" : "Oakhaven Ford");
        setTimeout(() => {
          const ok = game().startMode("skirmish", { scenario: +b.dataset.s });
          unbusy();
          if (!ok) return failed("battle");
          slot = 0;
          closeAll();
        }, 60);
      };
    });
  };
  let settingsTab = "graphics";
  render.settings = (el, tab) => {
    settingsTab = tab || settingsTab;
    for (const b of el.querySelectorAll("[data-tab]")) {
      b.classList.toggle("on", b.dataset.tab === settingsTab);
      b.setAttribute("aria-selected", b.dataset.tab === settingsTab ? "true" : "false");
      b.onclick = () => { settingsTab = b.dataset.tab; render.settings(el); };
    }
    for (const p of el.querySelectorAll("[data-pane]")) p.classList.toggle("hidden", p.dataset.pane !== settingsTab);
    if (settingsTab === "controls") renderControls(el.querySelector('[data-pane="controls"]'));
    // The sound and gameplay controls show what is kept.
    const audio = window.IllyriaAudio;
    if (audio) $("masterVolume").value = Math.round(audio.settings.master * 100);
    $("optSpeed").value = String(prefs.speed);
    $("optDifficulty").value = String(prefs.difficulty);
    $("optAutosave").checked = prefs.autosave;
    $("optConfirmDisband").checked = prefs.confirmDisband;
    $("optHints").checked = prefs.hints;
  };
  $("masterVolume").oninput = (e) => window.IllyriaAudio && window.IllyriaAudio.setMaster(+e.target.value / 100);
  $("optSpeed").onchange = (e) => {
    prefs.speed = +e.target.value;
    savePrefs();
    const a = api();
    if (a && game().inCampaign()) a.campSetSpeed(prefs.speed);
  };
  $("optDifficulty").onchange = (e) => { prefs.difficulty = +e.target.value; savePrefs(); };
  $("optAutosave").onchange = (e) => { prefs.autosave = e.target.checked; savePrefs(); };
  $("optConfirmDisband").onchange = (e) => { prefs.confirmDisband = e.target.checked; savePrefs(); };
  $("optHints").onchange = (e) => { prefs.hints = e.target.checked; savePrefs(); };

  // ---- Controls: every gesture, click and key the game has ----
  const fing = (x, y) => `<circle cx="${x}" cy="${y}" r="4.5" fill="currentColor"/><circle cx="${x}" cy="${y}" r="8" opacity="0.5"/>`;
  const arrow = (x1, y1, x2, y2) => {
    const a = Math.atan2(y2 - y1, x2 - x1), h = 5;
    return `<path d="M${x1} ${y1}L${x2} ${y2}M${x2} ${y2}l${(-h * Math.cos(a - 0.5)).toFixed(1)} ${(-h * Math.sin(a - 0.5)).toFixed(1)}M${x2} ${y2}l${(-h * Math.cos(a + 0.5)).toFixed(1)} ${(-h * Math.sin(a + 0.5)).toFixed(1)}"/>`;
  };
  const g = (body) => `<svg viewBox="0 0 52 40" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
  const pic = {
    tap: g(fing(26, 22) + '<path d="M26 6v4M14 10l3 3M38 10l-3 3"/>'),
    dtap: g(fing(26, 22) + '<circle cx="26" cy="22" r="13" opacity="0.35"/><path d="M26 3v3"/>'),
    drag: g(fing(14, 22) + arrow(22, 22, 44, 22)),
    pinch: g(fing(18, 26) + fing(34, 14) + arrow(13, 31, 5, 38) + arrow(39, 9, 47, 2)),
    twist: g(fing(16, 20) + fing(36, 20) + '<path d="M12 9a16 12 0 0 1 28 0"/><path d="M40 9l-1-5M40 9l-5 1"/><path d="M40 31a16 12 0 0 1-28 0"/><path d="M12 31l1 5M12 31l5-1"/>'),
    hold: g(fing(16, 24) + '<circle cx="16" cy="24" r="12" stroke-dasharray="3 3"/>' + arrow(28, 24, 46, 12)),
    box: g('<rect x="8" y="8" width="34" height="24" rx="2" stroke-dasharray="4 3"/>' + fing(42, 32)),
    card: g('<rect x="6" y="10" width="40" height="20" rx="4"/><path d="M12 24h18"/>' + fing(36, 28)),
    longcard: g('<rect x="6" y="10" width="40" height="20" rx="4"/><path d="M12 24h18"/>' + fing(36, 28) + '<circle cx="36" cy="28" r="12" stroke-dasharray="3 3"/>'),
    attack: g('<rect x="4" y="9" width="44" height="22" rx="5"/><path d="M15 14l10 10M23 26l5-5M26 26l3 3"/><path d="M33 20h9"/>'),
    march: g('<rect x="4" y="9" width="44" height="22" rx="5"/><path d="M14 14l6 6-6 6M21 14l6 6-6 6"/><path d="M33 20h9"/>'),
    formation: g('<rect x="4" y="9" width="44" height="22" rx="5"/><g fill="currentColor" stroke="none"><circle cx="14" cy="16" r="2"/><circle cx="20" cy="16" r="2"/><circle cx="26" cy="16" r="2"/><circle cx="17" cy="23" r="2"/><circle cx="23" cy="23" r="2"/></g><path d="M34 18l4 4 4-4"/>'),
    scatter: g('<circle cx="26" cy="20" r="2.5" fill="currentColor"/>' + arrow(22, 17, 12, 9) + arrow(30, 17, 40, 9) + arrow(22, 23, 12, 31) + arrow(30, 23, 40, 31)),
    banner: g('<path d="M16 36V6"/><path d="M16 7h20l-5 6 5 6H16"/>' + fing(36, 30)),
    city: g('<path d="M8 32V16l6-4 6 4v-6h12v6l6-4 6 4v16z"/><path d="M22 32v-7h8v7"/>'),
    mouseL: g('<rect x="16" y="4" width="20" height="32" rx="10"/><path d="M26 4v12M16 16h20"/><path d="M17 15V11a9 9 0 0 1 8-7v11z" fill="currentColor" stroke="none"/>'),
    mouseR: g('<rect x="16" y="4" width="20" height="32" rx="10"/><path d="M26 4v12M16 16h20"/><path d="M35 15V11a9 9 0 0 0-8-7v11z" fill="currentColor" stroke="none"/>'),
    wheel: g('<rect x="16" y="4" width="20" height="32" rx="10"/><path d="M26 9v7"/>' + arrow(44, 22, 44, 12) + arrow(44, 26, 44, 36)),
  };
  const row = (p, title, text) => `<div class="ctl">${pic[p]}<b>${title}</b><span>${text}</span></div>`;
  const k = (...keys) => keys.map((x) => `<kbd class="k">${x}</kbd>`).join("");
  const phoneHtml = () =>
    `<h3>The camera</h3>` +
    row("drag", "Drag", "Pan across the land.") +
    row("pinch", "Pinch", "Zoom from the gods to the mud: from the whole continent down to a single soldier.") +
    row("twist", "Twist two fingers", "Turn the view. Two fingers dragged together pan as well.") +
    `<h3>Choosing who</h3>` +
    row("tap", "Tap a soldier", "Selects him alone.") +
    row("dtap", "Tap him again, or double-tap", "Every soldier of his kind the camera can see.") +
    row("card", "Tap a squad card", "Selects that squad; tap it again and the camera goes to it. Heroes (gold) and special troops come first.") +
    row("box", "Hold on your men, then drag", "Draws a box: everyone of yours inside it. With nothing selected, a hold anywhere draws a box.") +
    `<h3>Orders</h3>` +
    row("tap", "Tap the ground", "March there in formation. On the way they fight back if struck, and take on foes who come close.") +
    row("tap", "Tap an enemy, or a gate", "Go after him (archers shoot from range); at a gate, engines and bombers batter it down.") +
    row("hold", "Hold where they should go, then drag", "A gold ghost of the formation appears; drag the way they should face, and let go.") +
    row("attack", "⚔ Attack, then tap", "Attack whatever you tap next: any soldier (even one of ours), a gate, a wall, house or tree (only engines, grenadiers and wizards can hurt those), or the ground.") +
    row("march", "Hold ⚔ Attack: » March", "A force march: tap where to go and they ignore the enemy on the way, to pull out of a fight or run past one.") +
    row("formation", "Formation", "The picker: Line, Short line, Staggered, Box, Wedge, Column, Loose, Encircle and V, each sketched.") +
    row("scatter", "Scatter", "Everyone selected runs outward, the moment a bomb or fireball is in the air.") +
    `<div class="ctl">${pic.card}<b>Hold · Find</b><span>Hold stops them where they stand; Find brings the camera to them.</span></div>` +
    `<h3>Cards and groups</h3>` +
    row("card", "+ Save group", "Appears when the selection is not already a card: saves it as a group card, with its own number.") +
    row("longcard", "Hold a group card", "Puts the current selection in that group, or forgets the group when it is the selection.") +
    `<h3>Along the top</h3>` +
    `<table class="keys"><tr><td>Charge</td><td>Every squad at the nearest enemy.</td></tr><tr><td>Fight!</td><td>The camera to the fiercest fighting.</td></tr>` +
    `<tr><td>Pause · 1×</td><td>Stop the battle (or the campaign's days); how fast time passes, up to 8×.</td></tr>` +
    `<tr><td>☰</td><td>Restart, Save, Settings, Controls, the Manual, Main menu.</td></tr></table>` +
    campaignHtml();
  const campaignHtml = () =>
    `<h3>On the campaign map</h3>` +
    row("banner", "Tap one of your banners", "Selects that stack; its armies show as cards. Then tap the ground to march, an enemy banner or town to attack it.") +
    row("city", "Tap one of your towns", "Its panel: what it trains, where new armies go, its walls, its boats.") +
    `<table class="keys"><tr><td>Fight it · Resolve</td><td>A battle: fight it in 3D, or let the dice settle it. Withdraw leaves a 3D battle.</td></tr>` +
    `<tr><td>Search · Visit</td><td>A hero at a ruin or a sage, anyone at a temple.</td></tr>` +
    `<tr><td>An army's card, twice</td><td>Disbands it: no more upkeep (one tap, if Settings say so).</td></tr>` +
    `<tr><td>Embark · Land</td><td>Board a boat in a port; step ashore from the sea.</td></tr></table>`;
  const desktopHtml = () =>
    `<h3>The mouse</h3>` +
    row("mouseL", "Left-drag · wheel", "Pan · zoom from the gods to the mud.") +
    row("mouseR", "Right-drag", "Turn the view.") +
    row("mouseL", "Click", "Select, march, attack: the cursor shows what a click would do (a hand, a sword, a target, a crosshair, a double chevron).") +
    row("mouseL", "Double-click a soldier", "Every soldier of his kind in view (clicking him again does the same).") +
    row("hold", "Hold the button at the spot, then drag", "Move and face that way.") +
    `<table class="keys">` +
    `<tr><td>${k("Shift")} + click</td><td>Add or take away one man.</td></tr>` +
    `<tr><td>${k("Shift")} + drag</td><td>Box-select at once.</td></tr>` +
    `<tr><td>${k("Ctrl")}/${k("⌘")} + click</td><td>Force attack: a friend, the ground, a wall, house or tree.</td></tr>` +
    `<tr><td>${k("Alt")} + click</td><td>Force march there.</td></tr></table>` +
    `<h3>Keys in battle</h3>` +
    `<table class="keys">` +
    `<tr><td>${k("1")}…${k("9")} ${k("0")}</td><td>Recall that group; twice quickly, the camera goes to it.</td></tr>` +
    `<tr><td>${k("Alt")}/${k("Ctrl")} + number</td><td>Save the selection as that group.</td></tr>` +
    `<tr><td>${k("Shift")} + ${k("1")}…${k("9")}, keypad</td><td>Formation: 1 Line, 2 Short line, 3 Staggered, 4 Box, 5 Wedge, 6 Column, 7 Loose, 8 Encircle, 9 V.</td></tr>` +
    `<tr><td>${k("S")}, ${k("Shift")}+${k("0")}, keypad ${k("0")}</td><td>Scatter.</td></tr>` +
    `<tr><td>${k("A")}</td><td>Attack mode: the next click attacks.</td></tr>` +
    `<tr><td>${k("M")}</td><td>Force-march mode.</td></tr>` +
    `<tr><td>${k("H")} · ${k("F")}</td><td>Hold · find (the camera to the selection).</td></tr>` +
    `<tr><td>${k("`")}</td><td>Select the heroes.</td></tr>` +
    `<tr><td>${k("Esc")}</td><td>Cancel the mode, else let go of the selection; closes pop-ups.</td></tr>` +
    `<tr><td>${k("Space")}</td><td>Pause the battle.</td></tr></table>` +
    `<h3>Menus</h3>` +
    `<table class="keys"><tr><td>${k("↑")}${k("↓")}${k("←")}${k("→")}</td><td>Move between buttons.</td></tr>` +
    `<tr><td>${k("Enter")}</td><td>Choose.</td></tr><tr><td>${k("Esc")}</td><td>Back.</td></tr></table>` +
    campaignHtml().replace(/Tap/g, "Click").replace(/tap/g, "click");
  let controlsView = matchMedia("(pointer: coarse)").matches ? "phone" : "desktop";
  function renderControls(root) {
    root.innerHTML = `<div class="tabs"><button data-v="phone">Phone and tablet</button><button data-v="desktop">Mouse and keys</button></div>` +
      (controlsView === "phone" ? phoneHtml() : desktopHtml()) +
      `<p class="note">Everything here works the same in every battle; the Manual tells the rest.</p>`;
    for (const b of root.querySelectorAll("[data-v]")) {
      b.classList.toggle("on", b.dataset.v === controlsView);
      b.onclick = () => { controlsView = b.dataset.v; renderControls(root); };
    }
  }
  render.controls = (el) => renderControls(el.querySelector(".body"));
  render.credits = (el) => {
    el.querySelector(".body").innerHTML =
      `<p class="lead">The High Lords of Illyria, version ${esc(version)}.</p>` +
      `<p>Real-time battles in the spirit of <i>Myth: The Fallen Lords</i> (Bungie, 1997), on a campaign continent after <i>Warlords</i> (SSG, 1990): ` +
      `every soldier simulated, every army named, every dead man left where he fell.</p>` +
      `<p>A custom C++ engine compiled to WebAssembly, drawn with WebGL 2. The simulation is deterministic: the same orders always give the same war, ` +
      `on a phone or a desktop, on any graphics setting.</p>` +
      `<h3>Links</h3><p><a href="https://github.com/cfreder2/The-High-Lords-of-Illyria" target="_blank" rel="noopener">The source and the design document ›</a><br>` +
      `<a href="sfx-test.html" target="_blank" rel="noopener">The sound test page ›</a></p>` +
      `<h3>Your saves</h3><p class="note">Three campaign slots and your settings live in this browser only (local storage). Private windows forget them.</p>`;
  };

  // ---- Keys: arrows move between the buttons in view, Enter chooses, Esc goes back ----
  function layer() {
    if (!$("confirmBox").classList.contains("hidden")) return $("confirmBox");
    if (open.length) return pages[open[open.length - 1]];
    if (document.body.classList.contains("at-title")) return $("titleScreen");
    return null;
  }
  document.addEventListener("keydown", (e) => {
    const root = layer();
    if (!root || !$("busy").classList.contains("hidden")) return;
    if (e.code === "Escape") {
      e.preventDefault();
      if (root === $("confirmBox")) closeConfirm();
      else if (open.length) back();
      return;
    }
    const arrows = { ArrowUp: -1, ArrowLeft: -1, ArrowDown: 1, ArrowRight: 1 };
    if (!(e.code in arrows)) return;
    const t = e.target;
    if (t && (t.tagName === "SELECT" || (t.tagName === "INPUT" && t.type === "range"))) return;  // their own arrows
    const items = [...root.querySelectorAll("button, select, input")].filter((b) => b.offsetParent !== null && !b.disabled);
    if (!items.length) return;
    e.preventDefault();
    const at = items.indexOf(document.activeElement);
    const next = at < 0 ? 0 : (at + arrows[e.code] + items.length) % items.length;
    items[next].focus();
    items[next].scrollIntoView({ block: "nearest" });
  }, true);

  window.IllyriaTitle = {
    show,
    open: openPage,
    back,
    closeAll,
    confirm,
    notice,
    manual,
    autosave,
    campaignTick,
    // The ☰ menu's Save: into this campaign's slot, or choose one.
    saveNow() {
      if (slot) saveTo(slot, false);
      else openPage("campaign", true);
    },
    pref: (name) => prefs[name],
    slot: () => slot,
  };
})();
