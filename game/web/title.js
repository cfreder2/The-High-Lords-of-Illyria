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
    if (st.day > savedDay && !st.winner) autosave(false);
  }
  function startCampaign(k, hard, player, enemies) {
    // The island's ground (every wall, gate and tree, for the fights) is made now, once, a few seconds:
    // let the page draw the notice first.
    busy("Laying out the island", "Every wall, gate and tree, for the fights");
    setTimeout(() => {
      const ok = game().startMode("campaign", { hard: hard ? 1 : 0, player, enemies, speed: prefs.speed });
      unbusy();
      if (!ok) return failed("campaign");
      slot = k;
      savedDay = -1;
      lastState = null;
      if (k) saveTo(k, true);
      closeAll();
    }, 60);
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
    { id: "ctf", name: "Capture the Flag", blurb: "Take their flag where it stands, and hold your own." },
    { id: "sandbox", name: "Sandbox", blurb: "Set any army down, let them fight, play any animation." },
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
  // The eight lords, in the continent's order (engine/sim/src/continent.cpp), and what each brings.
  const lords = [
    { name: "The Aurelian Throne", hero: "Kingshold", note: "knights and engines" },
    { name: "The Highcrag Giants", hero: "Highcrag", note: "heavy foot and pikes" },
    { name: "The Ironhold Dwarves", hero: "Deepforge", note: "rich, with grenadiers and engines" },
    { name: "The Orcs of Grath", hero: "Grathmaw", note: "wolf riders and warriors" },
    { name: "The Sylvaran Elves", hero: "Sylvaris", note: "archers and light horse" },
    { name: "The Sunreach Riders", hero: "Sunreach", note: "cavalry" },
    { name: "The Moonward Order", hero: "Moonhaven", note: "archers and pikes" },
    { name: "Lord Malvoren", hero: "Dreadspire", note: "foot, wolves and engines" },
  ];
  // A new campaign: who you are, which of the other lords are in the war (any, from none to all; every lord left
  // out is not in the game, its towns plain neutral places), and how well those that are play.
  function chooseLords(body, k) {
    let hard = prefs.difficulty ? 1 : 0;
    let player = 0;
    let enemies = 0xff & ~1;  // all the others
    const html = () =>
      `<p class="lead">Slot ${k}: who will you be?</p>` +
      `<div class="choice wrap" data-players>` + lords.map((l, i) => `<button data-player="${i}"><b>${l.name}</b><small>${l.hero} · ${l.note}</small></button>`).join("") + `</div>` +
      `<p class="lead" style="margin-top:14px">Your colour</p><div data-colour></div>` +
      `<p class="lead" style="margin-top:14px">Which lords will you face? <small data-count></small></p>` +
      `<div class="choice wrap" data-foes>` + lords.map((l, i) => `<button data-foe="${i}"><b>${l.name}</b><small data-foesub="${i}"></small></button>`).join("") + `</div>` +
      `<div class="choice"><button data-all="1">All</button><button data-all="0">None</button></div>` +
      `<p class="note">A lord you do not face is not in the war: their towns are neutral places like any other.</p>` +
      `<p class="lead" style="margin-top:14px">How well should the lords you face play?</p>` +
      `<div class="choice"><button data-hard="0"><b>Easy</b><small>Knights: slow, careless, short of gold</small></button>` +
      `<button data-hard="1"><b>Hard</b><small>Warlords: every trick, no mercy</small></button></div>` +
      `<p class="note">The computer plays by the same rules as you on either. The default is in Settings, Gameplay.</p>` +
      `<button class="big primary" data-start="1" style="margin-top:14px"><span>Raise your banner</span><small>Day 1</small></button>`;
    body.innerHTML = html();
    const sync = () => {
      enemies &= ~(1 << player);
      body.querySelectorAll("[data-hard]").forEach((b) => b.classList.toggle("on", +b.dataset.hard === hard));
      body.querySelectorAll("[data-player]").forEach((b) => b.classList.toggle("on", +b.dataset.player === player));
      let n = 0;
      body.querySelectorAll("[data-foe]").forEach((b) => {
        const i = +b.dataset.foe, you = i === player, on = !you && (enemies >> i & 1) === 1;
        n += on;
        b.classList.toggle("on", on);
        b.disabled = you;
        b.querySelector("small").textContent = you ? "You" : on ? "At war with you" : "Not in the war";
      });
      body.querySelector("[data-count]").textContent = n === 0 ? "(nobody: a free game with no end)" : `(${n} of 7)`;
    };
    body.querySelectorAll("[data-hard]").forEach((b) => { b.onclick = () => { hard = +b.dataset.hard; sync(); }; });
    body.querySelectorAll("[data-player]").forEach((b) => { b.onclick = () => { player = +b.dataset.player; sync(); }; });
    body.querySelectorAll("[data-foe]").forEach((b) => { b.onclick = () => { enemies ^= 1 << +b.dataset.foe; sync(); }; });
    body.querySelectorAll("[data-all]").forEach((b) => { b.onclick = () => { enemies = +b.dataset.all ? 0xff : 0; sync(); }; });
    sync();
    const colourSlot = body.querySelector("[data-colour]");
    const picker = window.IllyriaPalette ? IllyriaPalette.picker(colourSlot, player, null, lords.map((l) => l.name)) : null;
    body.querySelectorAll("[data-player]").forEach((b) => { const was = b.onclick; b.onclick = () => { was(); if (picker) picker.faction = player; }; });
    body.querySelector("[data-start]").onclick = () => startCampaign(k, hard, player, enemies & ~(1 << player));
    focusFirst(body.parentElement);
  }
  render.skirmish = (el) => {
    el.querySelector(".body").innerHTML = `<p class="lead">One battle, fought out in full. Nothing carries over.</p>` +
      `<button class="big" data-s="0"><span>Oakhaven Ford<br><small style="text-align:left;display:block">Hold the ford against the Red Hand</small></span><small>Battle</small></button>` +
      `<div style="height:8px"></div>` +
      `<button class="big" data-s="1"><span>The siege<br><small style="text-align:left;display:block">Storm a neutral town and take its courtyard</small></span><small>Siege</small></button>` +
      `<h3>Your colour</h3><div data-colour></div>` +
      `<p class="note">Only you see it. The sides keep their own colours, except one that clashes with yours, which takes the one you gave up.</p>`;
    if (window.IllyriaPalette) IllyriaPalette.picker(el.querySelector("[data-colour]"), 6, null, lords.map((l) => l.name));  // (a skirmish leads the Moonward seat)
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
    pad: g('<rect x="4" y="4" width="44" height="32" rx="5"/><circle cx="21" cy="27" r="3" fill="currentColor"/><circle cx="31" cy="27" r="3" fill="currentColor"/>' + arrow(26, 20, 26, 9)),
    padTap: g('<rect x="4" y="4" width="44" height="32" rx="5"/><circle cx="21" cy="24" r="3" fill="currentColor"/><circle cx="31" cy="24" r="3" fill="currentColor"/><path d="M26 10v4M17 13l2 2M35 13l-2 2"/>'),
    compass: g('<circle cx="26" cy="20" r="15"/><path d="M26 7l4 13h-8z" fill="currentColor"/><path d="M26 33l4-13h-8z"/>'),
  };
  const row = (p, title, text) => `<div class="ctl">${pic[p]}<b>${title}</b><span>${text}</span></div>`;
  const k = (...keys) => keys.map((x) => `<kbd class="k">${x}</kbd>`).join("");
  const phoneHtml = () =>
    `<h3>The camera</h3>` +
    row("drag", "Drag", "Pan across the land.") +
    row("pinch", "Pinch", "Zoom from the gods to the mud: from the whole continent down to a single soldier.") +
    row("twist", "Twist two fingers", "Turn the view. Two fingers dragged together pan as well.") +
    row("compass", "The compass", "Under the buttons at the top right, its needle points north; tap it and the view turns north up.") +
    `<h3>Choosing who</h3>` +
    row("tap", "Tap a soldier", "Selects him alone.") +
    row("dtap", "Tap him again, or double-tap", "Every soldier of his kind the camera can see.") +
    row("card", "Tap a hero's card", "The heroes' gold cards lead the row: selects him; tap it again and the camera goes to him.") +
    row("box", "Hold on your men, then drag", "Draws a box: everyone of yours inside it. With nothing selected, a hold anywhere draws a box.") +
    `<h3>Orders</h3>` +
    row("tap", "Tap the ground", "March there as they stand, each in his place, facing as they do. On the way they fight back if struck, and take on foes who come close.") +
    row("tap", "Tap an enemy, or a gate", "Go after him (archers shoot from range); at a gate, engines and bombers batter it down.") +
    row("hold", "Hold where they should go, then drag", "A gold ghost of them appears there; drag the way they should face, the whole of it turning, and let go.") +
    row("attack", "⚔ Attack, then tap", "Attack whatever you tap next: any soldier (even one of ours), a gate, a wall, house or tree (only engines, grenadiers and wizards can hurt those), or the ground.") +
    row("march", "Hold ⚔ Attack: » March", "A force march: tap where to go and they ignore the enemy on the way, to pull out of a fight or run past one.") +
    row("tap", "The lines on the ground", "Your own selected men show the way they will walk (never a foe's), one line for the men who go together. Dashes with chevrons and a ring: they fight what they meet. A solid line with arrowheads and a cross-hair: a hunt, after one foe and deaf to the rest. Dots and a filled spot: a force march.") +
    row("formation", "Formation", "The picker: Line, Double line, Box (hollow, facing out), Wedge, Loose, Encircle and V, each sketched; or As they stand, the arrangement they are in, as every selection starts.") +
    row("scatter", "Scatter", "Everyone selected runs outward, the moment a bomb or fireball is in the air.") +
    row("attack", "Raid", "Shown with hyena-folk selected: they are let loose at once to raid from where they stand, taking what lies about and throwing it at the soft and the stragglers they can see. Any other order ends it.") +
    `<div class="ctl">${pic.card}<b>Hold · Find</b><span>Hold stops them where they stand; Find brings the camera to them.</span></div>` +
    `<h3>Cards and groups</h3>` +
    row("card", "+ New group", "Appears when the selection is not already a card: one tap makes it a group, with its own card and number. Only the heroes and the groups you make have cards.") +
    row("longcard", "Hold a group card", "Puts the current selection in that group, or forgets the group when it is the selection.") +
    `<h3>Along the top</h3>` +
    `<table class="keys"><tr><td>Charge</td><td>Every squad at the nearest enemy.</td></tr><tr><td>Fight!</td><td>The camera to the fiercest fighting (on the campaign map too).</td></tr>` +
    `<tr><td>Pause · 1×</td><td>Stop the battle (or the campaign's days); how fast time passes. Tap for 2× and 4× (the campaign's days up to 8×); hold it in a battle for slow motion, ½×, and hold again for ¼×.</td></tr>` +
    `<tr><td>☰</td><td>Restart, Save, Settings, Controls, the Manual, Main menu.</td></tr></table>` +
    campaignHtml();
  const campaignHtml = () =>
    `<h3>On the campaign map</h3>` +
    row("banner", "Tap your soldiers, or box them", "Selects that stack (hold and drag, or shift and drag, a box round them); its armies show as cards. Then tap the ground to march, enemy soldiers or a town to attack. The minimap at the top right takes the camera anywhere.") +
    row("city", "Tap one of your towns' squares", "Its panel: what it trains, where new armies go, its walls, its boats. It stays until you select something else.") +
    `<table class="keys"><tr><td>Fight!</td><td>Armies that meet fight where they stand, on the map, with the same rules as any battle: there is nothing to choose and no dice. This takes the camera to the fiercest fighting; zoom in on it to see the men, and out to see every man as a dot that flashes while he fights.</td></tr>` +
    `<tr><td>Search · Visit</td><td>A hero at a ruin or a sage, anyone at a temple.</td></tr>` +
    `<tr><td>Disband, twice</td><td>Sends the armies you have selected home: no more upkeep (one tap, if Settings say so). It is under the bar at the top.</td></tr>` +
    `<tr><td>Embark · Land</td><td>Board a boat in a port; step ashore from the sea.</td></tr></table>`;
  const desktopHtml = () =>
    `<h3>The mouse</h3>` +
    row("mouseL", "Left-drag · wheel", "Pan · zoom from the gods to the mud.") +
    row("mouseR", "Right-drag", "Turn the view.") +
    row("mouseL", "Click", "Select, march, attack: the cursor shows what a click would do (a hand, a sword, a target, a crosshair, a double chevron).") +
    row("mouseR", "Right-click", "An order for those selected, as a click would give it (march there, go after the enemy clicked), but never a change of who is selected: on one of yours, they march to him.") +
    row("mouseL", "Double-click a soldier", "Every soldier of his kind in view (clicking him again does the same).") +
    row("hold", "Hold the button where they should go, then drag", "Move there and face that way.") +
    `<table class="keys">` +
    `<tr><td>${k("Shift")} + click</td><td>Add or take away one man.</td></tr>` +
    `<tr><td>${k("Shift")} + drag</td><td>Box-select at once.</td></tr>` +
    `<tr><td>${k("Ctrl")}/${k("⌘")} + click</td><td>Force attack: a friend, the ground, a wall, house or tree.</td></tr>` +
    `<tr><td>${k("Alt")} + click</td><td>Force march there.</td></tr>` +
    `<tr><td>${k("Shift")} + right-click</td><td>Queue the order: it begins when the one before is over. A plain order replaces the queue; ${k("Space")} clears it. At most eight.</td></tr></table>` +
    `<h3>A trackpad</h3>` +
    row("pad", "Swipe two fingers", "Pan across the land.") +
    row("pinch", "Pinch", "Zoom, about the pointer. In Safari a twist turns the view as well.") +
    row("padTap", "Tap with two fingers", "As a right-click: an order for those selected.") +
    `<h3>The camera keys (the left hand on W A S D)</h3>` +
    `<table class="keys">` +
    `<tr><td>${k("W")}${k("A")}${k("S")}${k("D")}</td><td>Pan while held (or the arrows): faster the higher the camera is.</td></tr>` +
    `<tr><td>${k("Q")} · ${k("E")}</td><td>Turn the view left · right (or ${k("Shift")} + ${k("←")}${k("→")}).</td></tr>` +
    `<tr><td>${k("R")} · ${k("F")}</td><td>Zoom in · out while held (or the wheel, or pinch).</td></tr>` +
    `<tr><td>${k("N")}</td><td>North up (or click the compass under the top right buttons).</td></tr>` +
    `<tr><td>${k("M")}</td><td>Show or hide the map in the corner (the island, or the battlefield). Click or drag on it to move the view.</td></tr>` +
    `<tr><td>${k("C")}</td><td>The camera to the selection.</td></tr></table>` +
    `<h3>Keys in battle</h3>` +
    `<table class="keys">` +
    `<tr><td>${k("1")}…${k("9")} ${k("0")}</td><td>Recall that group; twice quickly, the camera goes to it.</td></tr>` +
    `<tr><td>${k("Alt")}/${k("Ctrl")} + number</td><td>Make the selection that group.</td></tr>` +
    `<tr><td>${k("G")}</td><td>A new group of the selection, on the next free number.</td></tr>` +
    `<tr><td>${k("U")}</td><td>Ungroup: forget every group the selection holds whole (the soldiers stay selected).</td></tr>` +
    `<tr><td>${k("Ctrl")}/${k("⌘")} + ${k("A")}</td><td>Select every soldier of yours on the screen.</td></tr>` +
    `<tr><td>${k("Shift")} + ${k("1")}…${k("8")}, keypad</td><td>Formation: 1 Line, 2 Double line, 3 Box, 4 Wedge, 5 Loose, 6 Encircle, 7 V; 8 as they stand.</td></tr>` +
    `<tr><td>${k("Z")}</td><td>Attack mode: the next click attacks (a soldier, ours too, or shoots the ground). Or hold ${k("Ctrl")} as you click.</td></tr>` +
    `<tr><td>${k("V")}</td><td>Force-march mode: the next click, and they run there deaf to the enemy.</td></tr>` +
    `<tr><td>${k("T")}</td><td>The special action of those selected: the hyena-folk raid at once from where they stand; grenadiers lay satchels where the next click is, one each.</td></tr>` +
    `<tr><td>${k("X")}, ${k("Shift")}+${k("0")}, keypad ${k("0")}</td><td>Scatter.</td></tr>` +
    `<tr><td>${k("Space")}, ${k("B")}</td><td>Halt: stop the march.</td></tr>` +
    `<tr><td>${k("H")}</td><td>Select the heroes.</td></tr>` +
    `<tr><td>${k("Tab")}, ${k("Shift")} + ${k("Tab")}</td><td>Select the next, or the previous, group of units: the saved groups in number order, then the squads in no group. Wraps round; the camera follows if they are off screen.</td></tr>` +
    `<tr><td>${k("`")}</td><td>Planning: orders you give are held (drawn dashed in violet, numbered, for every man that has any) until you press ${k("`")} again, which sends them all at once, in the order given. ${k("Backspace")} drops them. ${k("Space")} still halts at once. On a phone, the Plan button.</td></tr>` +
    `<tr><td>${k("Esc")}</td><td>Cancel the mode, else let go of the selection; closes pop-ups.</td></tr>` +
    `<tr><td>${k("P")}</td><td>Pause the battle.</td></tr>` +
    `<tr><td>${k("-")} · ${k("=")}</td><td>Slower · faster: ¼×, ½×, 1×, 2×, 4×. The speed button too: a click for 2× and 4×, held for slow motion.</td></tr></table>` +
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
