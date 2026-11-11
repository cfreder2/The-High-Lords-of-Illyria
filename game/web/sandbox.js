// The sandbox's panel (game/src/mode_sandbox.cpp): set any squad down for any
// side, choose the side commanded here, let each side fight on its own or
// stand, run time slow, fast or a tick at a time, and make the selected
// soldiers play any clip. The title screen (title.js) finds "sandbox" in
// window.IllyriaModes and opens the setup page first (showSetup): where on
// the map the field is, and how far it reaches.
// Test addresses: ?mode=sandbox starts it at once (&place=N, or &x=..&y=..&radius=..);
// ?mode=sandbox&setup opens the setup page.
(function () {
  "use strict";
  const css = `
  #sbPanel { position: fixed; z-index: 6; top: calc(56px + env(safe-area-inset-top,0px)); right: calc(8px + env(safe-area-inset-right,0px));
    width: 252px; max-height: calc(100vh - 72px); overflow-y: auto; padding: 8px 10px; background: rgba(20,17,13,0.9);
    border: 1px solid rgba(214,180,110,0.35); border-radius: 12px; color: #f3ead8; font-size: 12px;
    backdrop-filter: blur(6px); -webkit-backdrop-filter: blur(6px); }
  #sbPanel.sb-hidden, .sb-hidden { display: none !important; }
  #sbPanel h3 { margin: 0; color: #d6b46e; font-size: 15px; font-weight: 600; letter-spacing: .04em; display: flex; justify-content: space-between; align-items: center; }
  #sbPanel h4 { margin: 8px 0 3px; font-size: 11px; color: #c9bc9f; font-weight: 400; text-transform: uppercase; letter-spacing: .08em; }
  #sbPanel select { width: 100%; min-height: 30px; background: rgba(255,255,255,.08); color: #f3ead8; border: 1px solid rgba(214,180,110,.35); border-radius: 6px; }
  #sbPanel select option { background: #2a241c; }
  .sb-seg { display: flex; gap: 4px; flex-wrap: wrap; margin-top: 3px; }
  .sb-seg button { flex: 1 1 0; min-width: 0; min-height: 28px; padding: 2px 4px; font-size: 12px; }
  .sb-seg button.on { border-color: #8fd0ff; box-shadow: 0 0 0 2px rgba(143,208,255,.45) inset; }
  .sb-side { display: inline-block; width: 9px; height: 9px; border-radius: 2px; margin-right: 3px; vertical-align: -1px; }
  .sb-place.on { background: rgba(60,110,40,.85) !important; border-color: #9fd77a !important; }
  .sb-row { display: flex; gap: 4px; align-items: center; margin-top: 3px; }
  .sb-row label { flex: 1; display: flex; align-items: center; gap: 4px; }
  .sb-men { color: #c9bc9f; font-size: 11px; }
  .sb-clips { display: grid; grid-template-columns: repeat(4, 1fr); gap: 4px; margin-top: 3px; }
  .sb-clips button { min-height: 28px; padding: 2px; font-size: 11px; }
  .sb-clips button.sb-standin { opacity: 0.6; }
  .sb-clips button small { display: block; font-size: 9px; }
  #sbCatch { position: fixed; inset: 0; z-index: 3; cursor: crosshair; }
  #sbPanel .sb-small { min-height: 24px; padding: 0 8px; font-size: 11px; }
  .sb-tune { display: grid; grid-template-columns: 1fr 64px 40px; gap: 3px 6px; align-items: center; margin-top: 3px; }
  .sb-tune input { width: 64px; min-height: 24px; background: rgba(255,255,255,.08); color: #f3ead8; border: 1px solid rgba(214,180,110,.35); border-radius: 5px; padding: 0 4px; }
  .sb-tune input.changed { border-color: #8fd0ff; }
  .sb-tune small { color: #8d826b; font-size: 10px; text-align: right; }
  #sbSetup { position: fixed; z-index: 6; left: calc(8px + env(safe-area-inset-left,0px)); right: calc(8px + env(safe-area-inset-right,0px));
    bottom: calc(8px + env(safe-area-inset-bottom,0px)); max-height: 88vh; overflow-y: auto; padding: 12px; max-width: 420px; margin: 0 auto;
    background: rgba(20,17,13,0.92); border: 1px solid rgba(214,180,110,0.35); border-radius: 12px; color: #f3ead8;
    backdrop-filter: blur(6px); -webkit-backdrop-filter: blur(6px); }
  #sbSetup h3 { margin: 0 0 4px; color: #d6b46e; font-size: 18px; font-weight: 600; letter-spacing: .04em; }
  #sbSetup p { margin: 0 0 8px; font-size: 13px; color: #c9bc9f; }
  .sb-map { display: block; width: min(100%, 300px, calc(50vh * 0.8)); margin: 0 auto; border-radius: 8px;
    border: 1px solid rgba(214,180,110,.35); cursor: crosshair; touch-action: manipulation; }
  .sb-where { text-align: center; margin: 6px 0 2px; font-size: 13px; }
  .sb-where b { color: #d6b46e; font-weight: 600; }
  .sb-radius { display: flex; gap: 8px; align-items: center; margin-top: 6px; font-size: 13px; color: #c9bc9f; }
  .sb-radius input { flex: 1; }
  .sb-radius output { min-width: 52px; text-align: right; color: #f3ead8; }
  .sb-buttons { display: flex; gap: 8px; margin-top: 12px; }
  .sb-buttons button { flex: 1; min-height: 46px; font-weight: 600; }
  .sb-go { background: rgba(60,110,40,.85) !important; border-color: #9fd77a !important; }`;
  const style = document.createElement("style");
  style.textContent = css;
  document.head.appendChild(style);

  // Four sides, in the match colours (mode.h teamColour).
  const SIDES = ["Blue", "Red", "Gold", "Green"].map((n, i) => [n, window.IllyriaPalette ? IllyriaPalette.team(i) : ["#4a78e6", "#d9402f", "#e0b43a", "#4caf50"][i]]);
  const SLOW = [[0.1, "⅒"], [0.25, "¼"], [0.5, "½"], [1, "1×"], [2, "2×"], [4, "4×"]];
  // The numbers the sandbox tunes (SandboxMode::statsJson): key, label, step.
  const TUNE = [["hp", "Health", 1], ["melee", "Melee damage", 1], ["swing", "Seconds a swing", 0.05], ["reach", "Reach (m)", 0.1],
    ["block", "Shield blocks (%)", 1], ["speed", "Speed (m/s)", 0.1], ["arrow", "Missile damage", 1], ["reload", "Reload (s)", 0.1],
    ["range", "Range (m)", 1], ["aggro", "Engages within (m)", 1]];
  const state = { type: 1, side: 1, count: 12, command: 0, placing: false, loop: true, pace: 1 };
  let panel = null, catcher = null, types = [], active = false, timer = 0;

  function call(name, ret, argTypes, args) {
    return typeof Module !== "undefined" && Module.ccall ? Module.ccall(name, ret, argTypes || [], args || []) : null;
  }
  function el(tag, attrs, ...kids) {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (k.startsWith("on")) e[k] = v;
      else if (k === "class") e.className = v;
      else e.setAttribute(k, v);
    }
    for (const k of kids) e.append(k);
    return e;
  }
  function swatch(i) { return el("span", { class: "sb-side", style: `background:${SIDES[i][1]}` }); }
  const pretty = (s) => s.replace(/_/g, " ").replace(/^./, (c) => c.toUpperCase());

  function seg(items, isOn, pick) {
    const row = el("div", { class: "sb-seg" });
    items.forEach((it, i) => {
      const b = el("button", { class: isOn(i) ? "on" : "", onclick: () => { pick(i); render(); } }, ...[].concat(it));
      row.append(b);
    });
    return row;
  }

  function render() {
    if (!panel) return;
    panel.textContent = "";
    panel.append(el("h3", {}, "Sandbox", el("button", { class: "sb-small", onclick: leave }, "Leave")));

    panel.append(el("h4", {}, "Add a squad"));
    const pick = el("select", { onchange: (e) => { state.type = +e.target.value; render(); } });
    // A retired type comes with no name: not offered (the numbers stay the sim's).
    types.forEach((t, i) => { if (t) pick.append(el("option", i === state.type ? { value: i, selected: "" } : { value: i }, pretty(t))); });
    panel.append(pick);
    panel.append(seg(SIDES.map((s, i) => [swatch(i), s[0]]), (i) => i === state.side, (i) => { state.side = i; }));
    panel.append(seg([1, 6, 12, 24].map(String), (i) => [1, 6, 12, 24][i] === state.count, (i) => { state.count = [1, 6, 12, 24][i]; }));
    panel.append(el("div", { class: "sb-seg" }, el("button", { class: "sb-place" + (state.placing ? " on" : ""), onclick: () => placing(!state.placing) },
      state.placing ? "Click the field to set them down (Esc: done)" : "Place on the field")));

    panel.append(el("h4", {}, "Command"));
    panel.append(seg(SIDES.map((s, i) => [swatch(i), s[0]]), (i) => i === state.command, (i) => {
      state.command = i;
      call("illyria_sandbox_side", null, ["number"], [i]);
    }));

    panel.append(el("h4", {}, "Fight on their own"));
    const fights = el("div", { class: "sb-row" });
    SIDES.forEach((s, i) => {
      const box = el("input", { type: "checkbox", onchange: (e) => call("illyria_sandbox_fight", null, ["number", "number"], [i, e.target.checked ? 1 : 0]) });
      box.checked = !!(hud.fights && hud.fights[i]);
      fights.append(el("label", {}, box, swatch(i), el("span", { class: "sb-men", id: `sbMen${i}` }, String((hud.sides || [])[i] || 0))));
    });
    panel.append(fights);

    panel.append(el("h4", {}, "Time"));
    panel.append(el("div", { class: "sb-seg" },
      el("button", { onclick: () => { api("pause"); setTimeout(poll, 50); } }, hud.paused ? "▶ Play" : "❚❚ Pause"),
      el("button", { onclick: () => { if (!hud.paused) api("pause"); call("illyria_sandbox_step"); setTimeout(poll, 50); } }, "Step a tick")));
    panel.append(seg(SLOW.map((s) => s[1]), (i) => SLOW[i][0] === state.pace, (i) => {
      state.pace = SLOW[i][0];
      call("illyria_sandbox_slow", null, ["number"], [Math.min(1, state.pace)]);
      call("illyria_sandbox_speed", null, ["number"], [Math.max(1, state.pace)]);
    }));

    panel.append(el("h4", {}, `Tune ${pretty(types[state.type] || "")}`));
    let stats = {};
    try { stats = JSON.parse(call("illyria_sandbox_stats", "string", ["number"], [state.type]) || "{}"); } catch (e) {}
    const sheet = stats.sheet || {};
    const grid = el("div", { class: "sb-tune" });
    for (const [k, label, step] of TUNE) {
      if (!(k in stats)) continue;
      const input = el("input", { type: "number", step: String(step), min: "0", value: String(stats[k]),
        class: stats[k] !== sheet[k] ? "changed" : "",
        onchange: (e) => { call("illyria_sandbox_tune", null, ["number", "string"], [state.type, JSON.stringify({ [k]: +e.target.value })]); render(); } });
      grid.append(el("span", {}, label), input, el("small", { title: "the unit sheet's" }, String(sheet[k])));
    }
    panel.append(grid);
    panel.append(el("div", { class: "sb-seg" },
      el("button", { onclick: () => { call("illyria_sandbox_tune", null, ["number", "string"], [state.type, "{}"]); render(); } }, "Sheet values"),
      el("button", { onclick: () => copyChanges(stats) }, "Copy changes")));

    panel.append(el("h4", {}, "Selected soldiers play"));
    const clips = el("div", { class: "sb-clips" });
    // Every clip the game plays (illyria_sandbox_clips); one a selected sheet lacks is dimmed, its stand-in said.
    const label = (n) => n.replace("die_", "† ").replace(/_/g, " ");
    clipNames.forEach((c) => clips.append(el("button", {
      class: c.as ? "sb-standin" : "", title: c.as ? `not on this sheet: plays its ${label(c.as)}` : "",
      onclick: () => call("illyria_sandbox_play", null, ["string", "number"], [c.name, state.loop ? 1 : 0]) },
    label(c.name), c.as ? el("small", {}, `= ${label(c.as)}`) : "")));
    clips.append(el("button", { onclick: () => call("illyria_sandbox_play", null, ["string", "number"], ["", 0]) }, "as before"));
    panel.append(clips);
    const loop = el("input", { type: "checkbox", onchange: (e) => { state.loop = e.target.checked; } });
    loop.checked = state.loop;
    panel.append(el("div", { class: "sb-row" }, el("label", {}, loop, "over and over")));
  }

  // The tuned numbers against the unit sheet's, as a line to paste where the stats live (engine/sim/src/units.cpp).
  function copyChanges(stats) {
    const sheet = stats.sheet || {};
    const diffs = TUNE.filter(([k]) => k in stats && stats[k] !== sheet[k]).map(([k, label]) => `${label} ${sheet[k]} → ${stats[k]}`);
    const text = `${types[state.type]}: ${diffs.length ? diffs.join(", ") : "as the unit sheet"}`;
    if (navigator.clipboard) navigator.clipboard.writeText(text).catch(() => {});
    if (window.IllyriaGame && IllyriaGame.toast) IllyriaGame.toast(text);
  }

  // Placing: a sheet over the field takes the clicks instead of the battle.
  function placing(on) {
    state.placing = on;
    if (on && !catcher) {
      catcher = el("div", { id: "sbCatch" });
      catcher.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        const ok = call("illyria_sandbox_spawn", "number", ["number", "number", "number", "number", "number"], [state.type, state.side, state.count, e.clientX, e.clientY]);
        if (!ok && window.IllyriaGame && IllyriaGame.toast) IllyriaGame.toast("Inside the stakes, on open ground.");
        setTimeout(poll, 50);
      });
      document.body.append(catcher);
    } else if (!on && catcher) {
      catcher.remove();
      catcher = null;
    }
    render();
  }
  document.addEventListener("keydown", (e) => { if (active && e.key === "Escape" && state.placing) placing(false); });

  function api(what) {
    const a = window.IllyriaGame && IllyriaGame.api();
    if (a && a[what]) a[what]();
  }

  let hud = {}, clipNames = [];
  function poll() {
    if (!active) return;
    try { hud = JSON.parse(call("illyria_mode_hud", "string") || "{}"); } catch (e) { hud = {}; }
    if (hud.mode !== "sandbox") return;
    // The clips the selection's sheets play, by name (their own, for a sheet that lists them).
    try { clipNames = JSON.parse(call("illyria_sandbox_clips", "string") || "[]"); } catch (e) { clipNames = []; }
    const key = JSON.stringify([hud.paused, hud.fights, hud.side, clipNames]);
    if (key !== poll.key) { poll.key = key; render(); }
    (hud.sides || []).forEach((n, i) => { const m = document.getElementById(`sbMen${i}`); if (m) m.textContent = String(n); });
  }

  function start(options) {
    if (!window.IllyriaGame) return;
    if (!IllyriaGame.startMode("sandbox", options || {})) IllyriaGame.toast && IllyriaGame.toast(call("illyria_mode_error", "string") || "The sandbox could not start.");
  }
  function started() {
    active = true;
    if (!types.length) { try { types = JSON.parse(call("illyria_sandbox_types", "string") || "[]"); } catch (e) { types = []; } }
    state.command = 0;
    if (!panel) { panel = el("div", { id: "sbPanel" }); document.body.append(panel); }
    panel.classList.remove("sb-hidden");
    document.body.classList.add("sandbox-on");  // the page's compass moves out of the panel's way
    poll.key = "";
    poll();
    render();
    clearInterval(timer);
    timer = setInterval(poll, 300);
  }
  function stopped() {
    active = false;
    clearInterval(timer);
    placing(false);
    if (panel) panel.classList.add("sb-hidden");
    document.body.classList.remove("sandbox-on");
  }
  function leave() {
    if (window.IllyriaGame) IllyriaGame.toMenu();
  }

  // ---- Before it starts: where on the map, and how far the field reaches ----
  // The continent (illyria_continent_map) with the battle places on it, the
  // recommended ones gold. A tap picks the field's middle (near a place, that
  // place), the ring is its edge to scale, and the camera behind the panel
  // goes to look. Untouched, it is the first recommended place.
  const MAP_W = 300;
  const pick = { x: 0, y: 0, radius: 260, place: -1, chosen: false };
  let setupEl = null, mapImage = null, extent = null, places = null, redraw = null;
  // Drawn a band of rows at a time, top first, so the page opens at once.
  function loadMap() {
    if (mapImage) return;
    const [w, h] = (call("illyria_continent_extent", "string") || "1,1").split(",").map(Number);
    extent = { w, h };
    const mh = Math.round(MAP_W * h / w), BAND = 24;
    mapImage = document.createElement("canvas");
    mapImage.width = MAP_W;
    mapImage.height = mh;
    mapImage.getContext("2d").fillStyle = "#2b4f6e";
    mapImage.getContext("2d").fillRect(0, 0, MAP_W, mh);
    const band = (j0) => {
      const p = call("illyria_continent_map", "number", ["number", "number", "number"], [MAP_W, j0, j0 + BAND]);
      const heap = typeof HEAPU8 !== "undefined" ? HEAPU8 : Module.HEAPU8;
      const rows = Math.min(BAND, mh - j0);
      if (p && heap) {
        const at = p + j0 * MAP_W * 4;
        mapImage.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(heap.subarray(at, at + rows * MAP_W * 4)), MAP_W, rows), 0, j0);
      }
      if (redraw) redraw();
      if (j0 + BAND < mh) setTimeout(() => band(j0 + BAND), 0);
    };
    band(0);
  }
  function nearestPlace(x, y, within, named) {
    let best = null, d = within * within;
    for (const p of places) {
      if (named && !p.name) continue;
      const q = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
      if (q < d) { d = q; best = p; }
    }
    return best;
  }
  function drawSetupMap(cv) {
    const g = cv.getContext("2d"), s = cv.width / extent.w, dot = cv.width / MAP_W;
    const at = (x, y) => [x * s, (extent.h - y) * s];
    g.imageSmoothingEnabled = true;
    g.drawImage(mapImage, 0, 0, cv.width, cv.height);
    for (const p of places) {
      const [px, py] = at(p.x, p.y);
      g.fillStyle = p.recommended ? "#d6b46e" : "rgba(243,234,216,.75)";
      g.beginPath();
      g.arc(px, py, (p.recommended ? 2.6 : 1.8) * dot, 0, 7);
      g.fill();
    }
    const [cx, cy] = at(pick.x, pick.y);
    g.strokeStyle = "#8fd0ff";
    g.lineWidth = 2 * dot;
    g.beginPath();
    g.arc(cx, cy, Math.max(4 * dot, pick.radius * s), 0, 7);
    g.stroke();
    g.fillStyle = "#8fd0ff";
    g.beginPath();
    g.arc(cx, cy, 2 * dot, 0, 7);
    g.fill();
  }
  function showSetup() {
    if (!window.IllyriaGame) return;
    if (!places) { try { places = JSON.parse(call("illyria_mode_places", "string") || "[]"); } catch (e) { places = []; } }
    loadMap();
    const first = places.find((p) => p.recommended) || places[0];
    if (!pick.chosen && first) Object.assign(pick, { x: first.x, y: first.y, place: first.i });
    if (!setupEl) { setupEl = el("div", { id: "sbSetup", role: "dialog" }); document.body.append(setupEl); }
    setupEl.innerHTML = "";
    const cv = el("canvas", { class: "sb-map", "aria-label": "Map: tap where the field should be" });
    cv.width = MAP_W * 2;
    cv.height = Math.round(MAP_W * 2 * extent.h / extent.w);
    const where = el("div", { class: "sb-where" });
    const out = el("output");
    const range = el("input", { type: "range", min: "80", max: "480", step: "20", "aria-label": "Field radius" });
    range.value = String(pick.radius);
    const refresh = () => {
      const at = pick.place >= 0 ? places.find((p) => p.i === pick.place) : null;
      const near = nearestPlace(pick.x, pick.y, 1500, true);
      const kind = (k) => k.charAt(0).toUpperCase() + k.slice(1);
      where.innerHTML = at ? `<b>${at.name || kind(at.kind)}</b>` : near ? `Open ground near <b>${near.name}</b>` : "<b>Open ground</b>";
      out.textContent = `${pick.radius} m`;
      drawSetupMap(cv);
    };
    cv.onclick = (e) => {
      const r = cv.getBoundingClientRect();
      const x = (e.clientX - r.left) / r.width * extent.w, y = extent.h - (e.clientY - r.top) / r.height * extent.h;
      // Near a place's dot, that place.
      const snap = nearestPlace(x, y, 8 / r.width * extent.w);
      Object.assign(pick, snap ? { x: snap.x, y: snap.y, place: snap.i } : { x, y, place: -1 }, { chosen: true });
      call("illyria_mode_preview_at", null, ["number", "number"], [Math.round(pick.x), Math.round(pick.y)]);
      refresh();
    };
    range.oninput = () => { pick.radius = +range.value; refresh(); };
    redraw = () => drawSetupMap(cv);
    const back = el("button", {}, "Back"), go = el("button", { class: "sb-go" }, "Start");
    back.onclick = () => { hideSetup(); leave(); };
    go.onclick = () => {
      hideSetup();
      start(pick.place >= 0 ? { place: pick.place, radius: pick.radius } : { x: Math.round(pick.x), y: Math.round(pick.y), radius: pick.radius });
    };
    setupEl.append(el("h3", {}, "Sandbox"),
                   el("p", {}, "Tap the map to choose the field. The ring is its edge: soldiers who stray past it are hurt."),
                   cv, where,
                   el("label", { class: "sb-radius" }, "Field radius", range, out),
                   el("div", { class: "sb-buttons" }, back, go));
    setupEl.classList.remove("sb-hidden");
    document.body.classList.remove("at-title");  // the title screen makes way
    document.body.classList.add("mode-setup");
    call("illyria_mode_preview_at", null, ["number", "number"], [Math.round(pick.x), Math.round(pick.y)]);
    refresh();
  }
  function hideSetup() {
    if (setupEl) setupEl.classList.add("sb-hidden");
    document.body.classList.remove("mode-setup");
  }

  window.IllyriaModes = window.IllyriaModes || {};
  window.IllyriaModes.sandbox = { available: true, menu: showSetup, started, stopped };

  // ?mode=sandbox starts at once (&place=N, or &x=..&y=.. in metres; &radius=..);
  // &setup opens the page that chooses them instead.
  const prevReady = window.illyriaReady;
  window.illyriaReady = function () {
    if (prevReady) prevReady();
    if (!/[?&]mode=sandbox\b/.test(location.search)) return;
    if (/[?&]setup\b/.test(location.search)) { showSetup(); return; }
    const num = (k) => { const m = new RegExp(`[?&]${k}=(-?\\d+)`).exec(location.search); return m ? +m[1] : null; };
    const options = {};
    for (const k of ["place", "x", "y", "radius"]) if (num(k) !== null) options[k] = num(k);
    start(options);
  };
})();
