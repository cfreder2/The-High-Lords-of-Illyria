// The matches' pages (game/src/mode_*.cpp): the setup panel (where to fight,
// seats, difficulty, the army), Horde's HUD and its reinforcements between
// waves, capture the flag's teams and alerts, and the end of a match. Kept
// apart from the title screen: it starts a match with
// Module._illyria_start_mode(id, optionsJson) ("horde" or "ctf"); without a
// "place" that opens this setup panel. When the player leaves a match the page
// hears the "illyria:mode-exit" event (and window.illyriaReturnToMenu is called).
// Test addresses: ?mode=horde or ?mode=ctf open the setup; &place=N&go starts at once.
(function () {
  "use strict";
  const css = `
  .md-panel { position: fixed; z-index: 6; background: rgba(20,17,13,0.9); border: 1px solid rgba(214,180,110,0.35); border-radius: 12px;
    color: #f3ead8; backdrop-filter: blur(6px); -webkit-backdrop-filter: blur(6px); }
  .md-hidden { display: none !important; }
  #mdSetup { left: calc(8px + env(safe-area-inset-left,0px)); right: calc(8px + env(safe-area-inset-right,0px));
    bottom: calc(8px + env(safe-area-inset-bottom,0px)); max-height: 72vh; overflow-y: auto; padding: 12px; max-width: 520px; margin: 0 auto; }
  #mdSetup h3 { margin: 0 0 6px; color: #d6b46e; font-size: 18px; font-weight: 600; letter-spacing: .04em; }
  #mdSetup h4 { margin: 10px 0 4px; font-size: 12px; color: #c9bc9f; font-weight: 400; text-transform: uppercase; letter-spacing: .08em; }
  #mdSetup p { margin: 0 0 6px; font-size: 13px; color: #c9bc9f; }
  .md-places { display: flex; flex-direction: column; gap: 4px; max-height: 30vh; overflow-y: auto; }
  .md-places button { text-align: left; display: flex; justify-content: space-between; align-items: center; min-height: 40px; }
  .md-places button.on, .md-seg button.on, .md-offer.on { border-color: #8fd0ff; box-shadow: 0 0 0 2px rgba(143,208,255,.45) inset; }
  .md-places small { color: #c9bc9f; font-size: 11px; text-transform: capitalize; }
  .md-places .rec { color: #d6b46e; }
  .md-seg { display: flex; gap: 6px; flex-wrap: wrap; }
  .md-seg button { flex: 1 1 0; min-width: 60px; }
  .md-row { display: flex; gap: 8px; margin-top: 12px; }
  .md-row button { flex: 1; min-height: 46px; font-weight: 600; }
  .md-go { background: rgba(60,110,40,.85) !important; border-color: #9fd77a !important; }
  .md-offers { display: grid; grid-template-columns: repeat(auto-fill, minmax(100px, 1fr)); gap: 6px; }
  .md-offer { min-height: 54px; padding: 4px 6px; display: flex; flex-direction: column; align-items: flex-start; justify-content: center; font-size: 12px; text-align: left; position: relative; }
  .md-offer b { font-size: 13px; }
  .md-offer i { font-style: normal; color: #d6b46e; }
  .md-offer .n { position: absolute; right: 6px; top: 4px; color: #8fd0ff; font-weight: 700; }
  .md-offer:disabled { opacity: .4; }
  #mdHud { top: calc(56px + env(safe-area-inset-top,0px)); left: 50%; transform: translateX(-50%); padding: 6px 10px; min-width: 220px;
    max-width: calc(100vw - 16px); text-align: center; font-size: 13px; pointer-events: none; z-index: 4; }
  #mdHud .line b { color: #d6b46e; }
  #mdHud .bar { height: 7px; border-radius: 4px; background: rgba(217,64,47,.55); overflow: hidden; margin-top: 4px; }
  #mdHud .bar i { display: block; height: 100%; background: #4a78e6; transition: width .25s; }
  #mdHud .warn { color: #ff8a70; font-weight: 600; }
  #mdHud .teams { display: flex; gap: 6px; justify-content: center; flex-wrap: wrap; margin-top: 4px; }
  #mdHud .team { display: inline-flex; align-items: center; gap: 4px; padding: 1px 6px; border-radius: 6px; background: rgba(255,255,255,.06); }
  #mdHud .team.out { opacity: .45; text-decoration: line-through; }
  #mdHud .team.me { box-shadow: 0 0 0 1px #8fd0ff inset; }
  #mdHud .team svg { width: 14px; height: 14px; }
  #mdAlert { top: calc(176px + env(safe-area-inset-top,0px)); left: 50%; transform: translateX(-50%); padding: 8px 14px; font-weight: 700;
    color: #fff; background: rgba(150,24,16,.9); border-color: #ff8a70; z-index: 5; pointer-events: none; animation: mdPulse 1s infinite; white-space: nowrap; }
  @keyframes mdPulse { 50% { opacity: .55; } }
  #mdReinforce { left: calc(8px + env(safe-area-inset-left,0px)); right: calc(8px + env(safe-area-inset-right,0px));
    bottom: calc(66px + env(safe-area-inset-bottom,0px)); padding: 10px; max-width: 520px; margin: 0 auto; }
  #mdReinforce h3 { margin: 0 0 2px; color: #d6b46e; font-size: 16px; font-weight: 600; }
  #mdReinforce p { margin: 0 0 8px; font-size: 12px; color: #c9bc9f; }
  #mdReinforce .md-row { margin-top: 8px; }
  #mdEnd { left: 50%; top: 50%; transform: translate(-50%,-50%); padding: 18px; width: min(92vw, 420px); text-align: center; z-index: 7; }
  #mdEnd h2 { margin: 0 0 6px; color: #d6b46e; font-size: 28px; font-weight: 600; letter-spacing: .06em; }
  #mdEnd p { margin: 4px 0; color: #c9bc9f; }
  body.mode-on #overlay { display: none !important; }
  body.mode-on #scenario, body.mode-on #campaign { display: none; }
  body.mode-setup #bottom, body.mode-setup #mdHud { display: none; }
  `;
  const style = document.createElement("style");
  style.textContent = css;
  document.head.appendChild(style);

  const el = (tag, attrs, html) => {
    const e = document.createElement(tag);
    for (const k in attrs || {}) e.setAttribute(k, attrs[k]);
    if (html !== undefined) e.innerHTML = html;
    return e;
  };
  const setup = el("div", { id: "mdSetup", class: "md-panel md-hidden", role: "dialog" });
  const hud = el("div", { id: "mdHud", class: "md-panel md-hidden", role: "status" });
  const alertBox = el("div", { id: "mdAlert", class: "md-panel md-hidden", role: "alert" }, "⚑ Your flag is under attack!");
  const reinforce = el("div", { id: "mdReinforce", class: "md-panel md-hidden", role: "dialog" });
  const end = el("div", { id: "mdEnd", class: "md-panel md-hidden", role: "dialog" });
  document.addEventListener("DOMContentLoaded", () => {});
  const mount = () => { for (const e of [setup, hud, alertBox, reinforce, end]) document.body.appendChild(e); };
  if (document.body) mount(); else document.addEventListener("DOMContentLoaded", mount);

  const flagIcon = (c) => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 21V4"/><path d="M5 4h12l-3 4 3 4H5" fill="${c}" stroke="${c}"/></svg>`;
  const fmt = (s) => `${Math.floor(s / 60)}:${String(Math.max(0, s) % 60).padStart(2, "0")}`;
  const call = (name, ret, types, args) => Module.ccall(name, ret, types, args);
  const json = (name) => { try { return JSON.parse(call(name, "string", [], [])); } catch (e) { return {}; } };

  let places = null, offers = null;
  const state = { id: "horde", place: 0, difficulty: 1, waves: 10, allies: 0, teams: 2, budget: 36, time: 900, teamAlly: false, army: [] };
  let lastOptions = null;
  const M = (window.IllyriaModes = { active: false });

  function seg(label, items, get, set) {
    const h = el("h4", {}, label), row = el("div", { class: "md-seg" });
    for (const [value, text] of items) {
      const b = el("button", {}, text);
      b.classList.toggle("on", get() === value);
      b.onclick = () => { set(value); render(); };
      row.appendChild(b);
    }
    return [h, row];
  }
  const armyCost = () => state.army.reduce((n, i) => n + offers[i].cost, 0);
  const teamBudget = () => (state.teamAlly ? Math.ceil(state.budget / 2) : state.budget);

  function render() {
    const horde = state.id === "horde";
    setup.innerHTML = "";
    setup.appendChild(el("h3", {}, horde ? "Horde: hold the hill" : "Capture the Flag"));
    setup.appendChild(el("p", {}, horde
      ? "Your hero and a small army hold the high ground. Waves come from the edges of the field, each worse than the last; between them, rest and buy reinforcements. Lose the hill, or every man, and it is over."
      : "Each team guards a flag. An enemy soldier standing on your flag with none of yours beside it takes it, and your army breaks. The last flag standing wins."));
    setup.appendChild(el("h4", {}, "Where to fight · tap to look"));
    const list = el("div", { class: "md-places" });
    places.forEach((p) => {
      const b = el("button", {}, `<span>${p.recommended ? '<span class="rec">★</span> ' : ""}${p.name}</span><small>${p.kind}</small>`);
      b.classList.toggle("on", p.i === state.place);
      b.onclick = () => { state.place = p.i; call("illyria_mode_preview", null, ["number"], [p.i]); render(); };
      list.appendChild(b);
    });
    setup.appendChild(list);
    const parts = horde ? [
      ...seg("Difficulty", [[0, "Easy"], [1, "Normal"], [2, "Hard"]], () => state.difficulty, (v) => (state.difficulty = v)),
      ...seg("Waves", [[5, "5"], [10, "10"], [20, "20"], [0, "Endless"]], () => state.waves, (v) => (state.waves = v)),
      ...seg("Allies on the hill (the computer)", [[0, "None"], [1, "One"], [2, "Two"], [3, "Three"]], () => state.allies, (v) => (state.allies = v)),
    ] : [
      ...seg("Teams", [[2, "Two"], [3, "Three"], [4, "Four"]], () => state.teams, (v) => { state.teams = v; if (v === 4) state.teamAlly = false; }),
      ...seg("Your team", [[false, "You alone"], [true, "You + a computer ally"]], () => state.teamAlly, (v) => { state.teamAlly = v; state.army = []; }),
      ...seg("Army points (each team)", [[24, "24"], [36, "36"], [48, "48"]], () => state.budget, (v) => { state.budget = v; state.army = []; }),
      ...seg("Time limit", [[600, "10 min"], [900, "15 min"], [0, "None"]], () => state.time, (v) => (state.time = v)),
    ];
    for (const p of parts) setup.appendChild(p);
    if (!horde) {
      const left = teamBudget() - armyCost();
      setup.appendChild(el("h4", {}, `Your army · ${left} of ${teamBudget()} points left${state.army.length ? "" : " (none picked: a balanced army)"}`));
      const grid = el("div", { class: "md-offers" });
      offers.forEach((o) => {
        const n = state.army.filter((i) => i === o.i).length;
        const b = el("button", { class: "md-offer" + (n ? " on" : "") }, `<b>${o.type}</b><span>${o.count} men · <i>${o.cost} pts</i></span>${n ? `<span class="n">×${n}</span>` : ""}`);
        b.disabled = o.cost > left;
        b.onclick = () => { state.army.push(o.i); render(); };
        grid.appendChild(b);
      });
      setup.appendChild(grid);
      if (state.army.length) {
        const clear = el("button", { style: "margin-top:6px" }, "Clear the army");
        clear.onclick = () => { state.army = []; render(); };
        setup.appendChild(clear);
      }
    }
    const row = el("div", { class: "md-row" });
    const back = el("button", {}, "Back");
    back.onclick = () => { closeSetup(); leave(); };
    const go = el("button", { class: "md-go" }, "Begin");
    go.onclick = () => begin();
    row.append(back, go);
    setup.appendChild(row);
  }

  function options() {
    if (state.id === "horde") {
      const seats = ["human", "empty", "empty", "empty"];
      for (let k = 1; k <= state.allies; ++k) seats[k] = "computer";
      return { place: state.place, difficulty: state.difficulty, waves: state.waves, seats };
    }
    const seats = ["human"], seatTeams = [0];
    if (state.teamAlly) { seats.push("computer"); seatTeams.push(0); }
    for (let t = 1; t < state.teams && seats.length < 4; ++t) { seats.push("computer"); seatTeams.push(t); }
    return { place: state.place, teams: state.teams, budget: state.budget, time: state.time, seats, seatTeams, army: state.army };
  }
  function begin(opts) {
    closeSetup();
    lastOptions = opts || options();
    call("illyria_start_mode", "number", ["string", "string"], [state.id, JSON.stringify(lastOptions)]);
  }
  function closeSetup() {
    setup.classList.add("md-hidden");
    document.body.classList.remove("mode-setup");
  }
  function leave() {
    end.classList.add("md-hidden");
    if (M.active) call("illyria_mode_exit", null, [], []);
    else document.dispatchEvent(new CustomEvent("illyria:mode-exit", { detail: { id: state.id, result: null } }));
    stop();
  }
  function stop() {
    M.active = false;
    document.body.classList.remove("mode-on");
    for (const e of [hud, alertBox, reinforce, end]) e.classList.add("md-hidden");
  }

  // Called by illyria_start_mode: without a place, the setup panel asks.
  M.setup = function (id, optionsJson) {
    state.id = id === "ctf" ? "ctf" : "horde";
    if (!places) places = json("illyria_mode_places");
    if (!offers) offers = json("illyria_mode_offers");
    let given = {};
    try { given = JSON.parse(optionsJson || "{}"); } catch (e) {}
    for (const k of ["difficulty", "waves", "teams", "budget", "time"]) if (typeof given[k] === "number") state[k] = given[k];
    const first = places.find((p) => p.recommended && (state.id === "ctf" || p.kind === "hill")) || places[0];
    if (first && !places.some((p) => p.i === state.place && state.place !== 0)) state.place = first.i;
    stop();
    document.body.classList.add("mode-setup");
    setup.classList.remove("md-hidden");
    call("illyria_mode_preview", null, ["number"], [state.place]);
    render();
  };
  M.started = function (id) {
    state.id = id;
    M.active = true;
    document.body.classList.add("mode-on");
    end.classList.add("md-hidden");
    lastKey = "";
    update();
  };
  M.start = (id, opts) => call("illyria_start_mode", "number", ["string", "string"], [id, JSON.stringify(opts || {})]);
  M.leave = leave;

  // The HUD, a few times a second.
  let lastKey = "", alertUntil = 0;
  function update() {
    if (!M.active || typeof Module === "undefined" || !Module.ccall) return;
    const s = json("illyria_mode_hud");
    if (!s.mode) { stop(); return; }
    hud.classList.remove("md-hidden");
    if (s.mode === "horde") updateHorde(s); else updateCtf(s);
    if (s.outcome && end.classList.contains("md-hidden")) showEnd(s);
  }
  function updateHorde(s) {
    const of = s.waves ? `/${s.waves}` : "";
    let line;
    if (s.phase === "muster") line = `<b>${s.place}</b> · first wave in <b>${fmt(s.toWave)}</b>`;
    else if (s.phase === "break") line = `Wave ${s.survived}${of} beaten · next in <b>${fmt(s.toWave)}</b>`;
    else line = `<b>Wave ${s.wave}${of}</b>${s.boss ? " · <span class=warn>BOSS</span>" : s.elite ? " · elite" : ""} · ${s.enemies} of the horde left`;
    const h = s.hill, safe = Math.max(0, 1 - h.contested / h.lose);
    const hillText = h.lost ? `<span class=warn>The hill is lost</span>`
      : h.contested > 0 ? `<span class=warn>Retake the hill! ${Math.ceil(h.lose - h.contested)} s</span>`
      : `Hill: ${h.holders} of ours${h.foes ? `, ${h.foes} of theirs` : ""}`;
    hud.innerHTML = `<div class="line">${line}</div><div class="line">${hillText} · ${s.kills} slain · ${fmt(s.held)} held</div><div class="bar"><i style="width:${safe * 100}%"></i></div>`;
    // Between waves: the reinforcements on offer for this seat's points.
    const open = s.phase === "break" && !s.ready;
    reinforce.classList.toggle("md-hidden", !open);
    if (!open) return;
    const key = `${s.survived}|${s.points}|${s.toWave}`;
    if (key === lastKey) return;
    lastKey = key;
    reinforce.innerHTML = `<h3>Reinforcements · ${s.points} points</h3><p>The wounded mend${s.heroLevel > 0 ? `; your hero rises to level ${s.heroLevel}` : s.heroLevel < 0 ? "; your hero lies where he fell" : ""}. The next wave comes in ${fmt(s.toWave)}.</p>`;
    const grid = el("div", { class: "md-offers" });
    for (const o of s.offers) {
      const b = el("button", { class: "md-offer" }, `<b>${o.type}</b><span>${o.count} men · <i>${o.cost} pts</i></span>`);
      b.disabled = o.cost > s.points;
      b.onclick = () => { call("illyria_mode_act", null, ["number", "number"], [0, o.i]); lastKey = ""; setTimeout(update, 80); };
      grid.appendChild(b);
    }
    reinforce.appendChild(grid);
    const row = el("div", { class: "md-row" });
    const ready = el("button", { class: "md-go" }, "Ready: bring them on");
    ready.onclick = () => { call("illyria_mode_act", null, ["number", "number"], [1, 0]); reinforce.classList.add("md-hidden"); };
    row.appendChild(ready);
    reinforce.appendChild(row);
  }
  function updateCtf(s) {
    const teams = s.teams.map((t) => `<span class="team${t.flag ? "" : " out"}${t.local ? " me" : ""}">${flagIcon(t.colour)}${t.name}${t.local ? " (you)" : ""} ${t.alive}</span>`).join("");
    const clock = s.limit ? `${fmt(Math.max(0, s.limit - s.time))} left` : fmt(s.time);
    const me = s.teams.find((t) => t.local);
    hud.innerHTML = `<div class="line"><b>${s.place}</b> · ${clock}${me && me.flag ? ` · flag guard ${me.defenders}` : ""}</div><div class="teams">${teams}</div>`;
    if (s.alert) alertUntil = Date.now() + 2500;
    alertBox.classList.toggle("md-hidden", !(Date.now() < alertUntil) || !!s.outcome);
  }
  function showEnd(s) {
    reinforce.classList.add("md-hidden");
    alertBox.classList.add("md-hidden");
    let title, lines;
    if (s.mode === "horde") {
      title = s.outcome === 1 ? "The hill holds" : "The hill has fallen";
      lines = [`${s.survived} wave${s.survived === 1 ? "" : "s"} survived${s.waves ? ` of ${s.waves}` : ""}`, `${s.kills} of the horde slain`, `${fmt(s.held)} on the hill`];
    } else {
      const w = s.teams.find((t) => t.team === s.winner);
      title = s.outcome === 1 ? "Victory: your flag stands" : s.draw ? "A draw" : "Defeat";
      lines = [w ? `${w.name} holds the field` : "No flag stands alone", `${fmt(s.time)} of battle`];
    }
    end.innerHTML = `<h2>${title}</h2>${lines.map((l) => `<p>${l}</p>`).join("")}`;
    const row = el("div", { class: "md-row" });
    const again = el("button", { class: "md-go" }, "Play again");
    again.onclick = () => { end.classList.add("md-hidden"); begin(lastOptions || options()); };
    const menu = el("button", {}, "Menu");
    menu.onclick = () => leave();
    row.append(again, menu);
    end.appendChild(row);
    end.classList.remove("md-hidden");
  }
  setInterval(update, 250);

  // ?mode=horde / ?mode=ctf until the title screen starts them (&place=N&go: at once).
  const prevReady = window.illyriaReady;
  window.illyriaReady = function () {
    if (prevReady) prevReady();
    const m = /[?&]mode=(horde|ctf)\b/.exec(location.search);
    if (!m) return;
    const place = /[?&]place=(\d+)/.exec(location.search);
    if (place && /[?&]go\b/.test(location.search)) {
      state.id = m[1];
      begin({ place: +place[1], seats: m[1] === "ctf" ? ["human", "computer", "computer", "computer"] : ["human", "empty", "empty", "empty"],
              teams: m[1] === "ctf" ? (+(/[?&]teams=(\d)/.exec(location.search) || [0, 2])[1]) : undefined });
    } else {
      call("illyria_start_mode", "number", ["string", "string"], [m[1], "{}"]);
    }
  };
})();
