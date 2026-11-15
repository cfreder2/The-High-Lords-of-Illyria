// The sprite audit page: every unit's sheet as the game draws it. Nothing here is the engine's code, so each rule
// is copied from where the game keeps it, and says where:
//   - loading (game/web/platform_web.cpp js_request_sheet): the .webp decoded unpremultiplied, read back through a
//     2D canvas, rows flipped (renderer_sheets.cpp loadSheet), cells passed as they are;
//   - the texture (renderer_sheets.cpp uploadSheet): MAG nearest, MIN linear-mipmap-linear, three mip levels;
//   - the cell and the mirror (shaders.h kUnitVs): cell = view * framesPerView + frame, facings 5-7 are views
//     3, 2, 1 mirrored; body parts follow the five views' cells;
//   - the pixel (shaders.h kUnitFs): alpha under 0.5 dropped, alpha 0.68-0.88 (code 191) dyed by the cloth colour
//     (figures.cpp cloth: the side's colour a quarter greyed), the rest as painted (code 153 the outline);
//   - the clips (sprite_layout.h, sprite_clips.h/.cpp, renderer_sheets.cpp playedFrame): the fixed layout or a
//     sheet's own list, their lengths and fallbacks;
//   - the camera (camera.h, camera.cpp, closeup.h): 45 degree field of view, 2 m the closest, pitch by distance.
// Not drawn: the sun's shade on a soldier (x0.72 to 1), fog, the burnt tint, the pull toward the viewer.
// A unit's sounds (its voice, its weapon, its fall) are sound-core.js's, with the Sounds page.
(() => {
  "use strict";

  // ---------------------------------------------------------------- the game's numbers
  // sim::UnitType order: height (m, engine/sim/src/units.cpp), figures::attackSeconds, pre-rendered fliers.
  const UNITS = [
    ["Light infantry", 1.8, 0.78], ["Heavy infantry", 1.9, 0.9], ["Archers", 1.8, 0.9], ["Pikemen", 1.8, 0.9],
    ["Light cavalry", 2.5, 0.72], ["Heavy cavalry", 2.7, 0.9], ["Dwarves", 1.35, 0.9], ["Wolf riders", 1.9, 0.66],
    ["Giants", 5.5, 0.9], ["Minotaurs", 3.0, 0.9], ["Spiders", 1.3, 0.6], ["Griffins", 2.2, 0.72, true],
    ["Pegasi", 2.3, 0.72, true], ["Elephants", 4.0, 0.9], ["Unicorns", 2.2, 0.78], ["Wizards", 1.9, 0.8],
    ["Dragons", 4.5, 0.8, true], ["Devils", 3.0, 0.9], ["Demons", 3.6, 0.9], ["Undead", 1.8, 0.84],
    ["Archons", 2.4, 0.72, true], ["Hero", 2.0, 0.66], ["Dwarf grenadier", 1.3, 0.8], ["Journeyman", 1.8, 0.72],
    ["Warrior", 1.9, 0.72], ["Berserker", 1.9, 0.57], ["Catapult", 2.5, 1.0], ["Hyena-folk", 1.55, 0.6],
  ];
  const VIEWS = 5;
  const FIXED = [["idle", 4], ["walk", 8], ["attack", 6], ["flinch", 2], ["die_back", 6], ["die_front", 6], ["die_crumple", 6], ["die_burn", 6]];
  const KNOWN = ["idle", "walk", "run", "attack", "throw", "pickup", "flinch", "celebrate", "die_back", "die_front", "die_crumple", "die_burn"];
  const FALLBACK = { run: "walk", celebrate: "idle", die_front: "die_back", die_crumple: "die_back", die_burn: "die_crumple", throw: "attack", pickup: "attack", slash: "attack" };
  const DEATHS = ["die_front", "die_crumple", "die_burn"];
  const GIBS = ["head", "torso", "arm", "leg", "chunk", "weapon", "shield", "mount_head", "mount_leg", "wing"];
  const GIB_CHAIN = { head: ["mount_head", "chunk"], torso: ["chunk"], arm: ["wing", "mount_leg", "leg", "chunk"], leg: ["mount_leg", "chunk"], chunk: [],
    weapon: ["chunk"], shield: ["chunk"], mount_head: ["head", "chunk"], mount_leg: ["leg", "chunk"], wing: ["arm", "chunk"] };
  const FACINGS = ["front", "front-right", "right", "back-right", "back", "back-left", "left", "front-left"];
  // The palette (game/src/palette.cpp; game/web/palette.js is read over it when it can be reached).
  let SWATCHES = [["White", "#f2f2ea"], ["Gold", "#eabf26"], ["Red", "#cc261e"], ["Orange", "#ea7a1e"], ["Green", "#28993d"], ["Brown", "#99663d"],
    ["Blue", "#335bd1"], ["Black", "#191919"], ["Purple", "#7f38b2"], ["Pink", "#f47fb7"], ["Teal", "#199e9e"], ["Maroon", "#721933"]];
  const SEATS = [6, 2, 1, 4, 5, 3, 0, 7];  // a skirmish's teams: Blue, Red, Gold, Green ... (factions.h kSkirmishSeats)

  // The camera (camera.h, camera.cpp, closeup.h).
  const FOV_Y = 0.785398, MIN_DISTANCE = 2, ELEVATION = 0.52;
  const MAX_MAGNIFICATION = 2.0, LOAD_ABOVE = 1.15;  // closeup.h
  const FLAG_ABOVE = 1.5;  // this page's red line
  const CAMERAS = [
    { key: "far", d: 120, label: "Far", why: "120 m: where a battle opens" },
    { key: "mid", d: 60, label: "Mid", why: "60 m: where the sandbox opens" },
    { key: "close", d: 15, label: "Close", why: "15 m: a company fills the screen" },
    { key: "lowest", d: MIN_DISTANCE, label: "Lowest", why: "2 m: Camera::kMinDistance" },
  ];
  const pxPerMetre = (screen, d) => screen / (2 * Math.tan(FOV_Y / 2) * Math.max(d, 1e-3));
  function pitch(d) {
    const t = Math.min(1, Math.max(0, Math.log(d / 6) / Math.log(3000 / 6)));
    return 0.33 + (1.31 - 0.33) * (t * t * (3 - 2 * t));
  }
  // A billboard stands upright, 1/cos(kElevation) its picture's height, and is seen from the camera's pitch.
  const stretchAt = (d) => Math.cos(pitch(d)) / Math.cos(ELEVATION);

  // ---------------------------------------------------------------- state
  const dpr = window.devicePixelRatio || 1;
  const state = {
    base: "", index: {}, indexHd: {}, units: [], team: "#335bd1", mode: 0, screen: 1080,
    gridClock: 0, detail: { t: 0, t0: 0, paused: false, speed: 1, loop: true, step: 0, scale: 96, hd: false, unit: null,
      zoomClip: "idle", zoomFacing: 1, focus: 0.75, halved: false, custom: 8, sync: "off", victim: "flesh" },
    compare: { a: "archers", b: "heavy_infantry", clip: "idle", facing: 1, scale: 96, hd: true },
    grid: { clip: "cycle", facing: 1, same: false },
    errors: [],
  };
  const $ = (s, r = document) => r.querySelector(s);
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
  const fix = (v, n = 1) => (Math.round(v * 10 ** n) / 10 ** n).toFixed(n);
  const magClass = (m) => (m > FLAG_ABOVE ? "bad" : m > 1.0 ? "warn" : "ok");

  // ---------------------------------------------------------------- clips
  function clipsOf(meta, unit) {
    if (meta.clips && meta.clips.length) return meta.clips.map((c) => ({ ...c, own: true }));
    const fpv = meta.framesPerView || 26, out = [];
    let first = 0;
    for (const [name, frames] of FIXED) {
      if (first + frames <= fpv) out.push({ name, first, frames, loop: name === "idle" || name === "walk" });
      first += frames;
    }
    // A pre-rendered flier's walk holds two cycles (sprite_clips.cpp fixedLayoutFrame): 0-3 on foot, 4-7 a wingbeat.
    if (unit.flier) {
      const w = out.findIndex((c) => c.name === "walk");
      out.splice(w, 1, { name: "walk", first: 4, frames: 4, loop: true, note: "on foot" }, { name: "walk (air)", first: 8, frames: 4, loop: true, seconds: 1 / 1.1, note: "a wingbeat" });
    }
    return out;
  }
  // Once through, as renderer_sheets.cpp playedFrame has it: what the clip says, else the game's own.
  function clipLength(c, unit) {
    if (c.seconds > 0) return c.seconds;
    const n = c.name;
    if (n === "die_burn") return 1.5;
    if (n.startsWith("die_")) return 0.9;
    if (n === "attack" || n === "throw" || n === "pickup" || n === "slash") return unit.attack;
    if (n === "walk" || n === "run") return 1.0;
    if (n === "flinch") return 0.3;
    return c.own ? 2.2 : 1 / 0.45;  // an idle breath (the fixed layout's: gameSeconds * 0.45 loops)
  }
  // sprite_clips.cpp SheetClip::frameAt.
  function frameAt(c, t, len, looped) {
    if (c.frames <= 1 || !(len > 0)) return 0;
    if (looped) t -= Math.floor(t / len) * len;
    if (!(t > 0)) t = 0;
    if (c.times && c.times.length === c.frames) {
      let i = 0;
      while (i < c.frames && c.times[i] <= t) ++i;
      return Math.min(c.frames - 1, Math.max(0, i - 1));
    }
    if (t >= len) return c.frames - 1;
    return Math.min(c.frames - 1, Math.max(0, Math.floor((t / len) * c.frames)));
  }
  function playClip(clips, name) {
    for (let k = 0; k < 8 && name; ++k, name = FALLBACK[name]) {
      const c = clips.find((x) => x.name === name);
      if (c) return c;
    }
    return null;
  }
  // The frame of a clip on a clock that loops (falls held 0.6 s on the body) or plays once and holds.
  function frameOn(c, unit, clock) {
    const len = clipLength(c, unit);
    let f;
    if (clock.loop) {
      const period = c.loop ? len : len + 0.6;
      const t = clock.t - Math.floor(clock.t / period) * period;
      f = c.loop ? frameAt(c, t, len, true) : frameAt(c, Math.min(t, len), len, false);
    } else {
      f = frameAt(c, clock.t - clock.t0, len, false);
    }
    if (clock.paused && clock.step) f = (((f + clock.step) % c.frames) + c.frames) % c.frames;
    return f;
  }
  // kUnitVs: facing 0-7 to a drawn view and whether it is mirrored.
  const viewOf = (facing) => (facing > 4 ? { view: 8 - facing, mirror: true } : { view: facing, mirror: false });

  // ---------------------------------------------------------------- sheets
  const sheets = new Map();
  let gl = null, prog = null, loading = 0;
  const queue = [];
  function sheet(name, halved) {
    const key = name + (halved ? "|half" : "");
    let s = sheets.get(key);
    if (!s) {
      s = { key, name, halved: !!halved, ready: false, failed: false };
      sheets.set(key, s);
      queue.push(s);
      pump();
    }
    return s;
  }
  function pump() {
    while (loading < 3 && queue.length) {
      const s = queue.shift();
      ++loading;
      loadSheet(s).catch((e) => { s.failed = true; state.errors.push(s.key + ": " + e); console.warn("sheet " + s.key + ": " + e); })
        .finally(() => { --loading; pump(); status(); });
    }
    status();
  }
  // A file of the sprites folder; a dropped connection (a small local server, many files at once) is tried again.
  async function get(file, as) {
    for (let tries = 0; ; ++tries) {
      try {
        const r = await fetch(state.base + file);
        if (!r.ok) throw new Error(r.status + " " + file);
        return await r[as]();
      } catch (e) {
        if (tries >= 2 || /^\d{3} /.test(e.message)) throw e;
        await new Promise((done) => setTimeout(done, 300));
      }
    }
  }
  const metas = new Map();
  function meta(name) {
    if (!metas.has(name)) metas.set(name, get(name + ".json", "json"));
    return metas.get(name);
  }
  // renderer_sheets.cpp halveSheet: a sheet at half its size, the alpha codes kept whole (rows already flipped).
  function halve(px, width, height, cells) {
    const w2 = (width + 1) >> 1, h2 = (height + 1) >> 1, out = new Uint8Array(w2 * h2 * 4);
    for (let y = 0; y < h2; ++y)
      for (let x = 0; x < w2; ++x) {
        let r = 0, g = 0, b = 0, solid = 0, team = 0, outline = -1;
        for (let dy = 0; dy < 2; ++dy)
          for (let dx = 0; dx < 2; ++dx) {
            const p = (Math.min(height - 1, 2 * y + dy) * width + Math.min(width - 1, 2 * x + dx)) * 4, a = px[p + 3];
            if (a > 170) { r += px[p]; g += px[p + 1]; b += px[p + 2]; ++solid; if (a < 223) ++team; }
            else if (a > 100) outline = p;
          }
        const o = (y * w2 + x) * 4;
        if (solid) { out[o] = r / solid; out[o + 1] = g / solid; out[o + 2] = b / solid; out[o + 3] = 2 * team > solid ? 191 : 255; }
        else if (outline >= 0) { out[o] = px[outline]; out[o + 1] = px[outline + 1]; out[o + 2] = px[outline + 2]; out[o + 3] = px[outline + 3]; }
      }
    const table = cells.map((c) => {
      if (c[2] <= 0) return c;
      const x0 = Math.floor(c[0] / 2), y0 = Math.floor(c[1] / 2), x1 = Math.ceil((c[0] + c[2]) / 2), y1 = Math.ceil((c[1] + c[3]) / 2);
      return [x0, y0, Math.max(1, x1 - x0), Math.max(1, y1 - y0), c[4], c[5], c[6], c[7]];
    });
    return { px: out, width: w2, height: h2, cells: table };
  }
  async function loadSheet(s) {
    const m = await meta(s.name);
    const blob = await get(s.name + ".webp", "blob");
    const bmp = await createImageBitmap(blob, { premultiplyAlpha: "none", colorSpaceConversion: "none" });
    const cv = document.createElement("canvas");
    cv.width = bmp.width, cv.height = bmp.height;
    const g = cv.getContext("2d", { willReadFrequently: true });
    g.drawImage(bmp, 0, 0);
    const src = g.getImageData(0, 0, bmp.width, bmp.height).data;
    let w = bmp.width, h = bmp.height;
    bmp.close();
    // GL keeps the bottom row first.
    let px = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; ++y) px.set(src.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
    let cells = m.cells;
    if (!s.halved) s.stats = pixelStats(px, w, h, m);
    if (s.halved) ({ px, width: w, height: h, cells } = halve(px, w, h, cells));
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, px);
    gl.generateMipmap(gl.TEXTURE_2D);
    // As the game's (uploadSheet): crisp close up, averaged far off.
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, 2);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    Object.assign(s, { tex, w, h, cells, meta: m, ready: true });
    if (s.stats) noteStats(s);
  }
  function release(keep) {
    for (const [key, s] of sheets) {
      if (!(s.name.endsWith("_hd") || s.halved) || keep.has(s.name)) continue;
      if (s.tex) gl.deleteTexture(s.tex);
      const q = queue.indexOf(s);
      if (q >= 0) queue.splice(q, 1);
      if (s.ready || q >= 0) sheets.delete(key);
    }
  }
  // What the pixels say: the alpha codes used, and cells with nothing in them.
  function pixelStats(px, w, h, m) {
    const hist = new Uint32Array(256);
    for (let i = 3; i < px.length; i += 4) ++hist[px[i]];
    let stray = 0;
    const strays = [];
    for (let a = 1; a < 255; ++a) if (hist[a] && a !== 153 && a !== 191) { stray += hist[a]; strays.push(a); }
    const fpv = m.framesPerView || 26, empty = [];
    for (let c = 0; c < Math.min(m.cells.length, VIEWS * fpv); ++c) {
      const [x, y, cw, ch] = m.cells[c];
      if (cw < 1 || ch < 1 || x + cw > w || y + ch > h) continue;
      let any = false;
      for (let j = y; j < y + ch && !any; ++j) for (let i = x; i < x + cw; ++i) if (px[(j * w + i) * 4 + 3] >= 128) { any = true; break; }
      if (!any) empty.push(c);
    }
    const figure = hist[255] + hist[191] + hist[153];
    return { team: hist[191], outline: hist[153], opaque: hist[255], stray, strays, empty, teamShare: figure ? hist[191] / figure : 0, fill: figure / (w * h) };
  }

  // ---------------------------------------------------------------- the units and what is wrong with them
  function bounds(cells, from, to) {
    let ax = 0, lo = 0, hi = 0;
    for (let c = from; c < to; ++c) {
      const e = cells[c];
      if (!e || e[2] <= 0) continue;
      ax = Math.max(ax, Math.abs(e[4]), Math.abs(e[4] + e[6]));
      lo = Math.min(lo, e[5]);
      hi = Math.max(hi, e[5] + e[7]);
    }
    return { ax, lo, hi };
  }
  function clipBounds(m, c) {
    const fpv = m.framesPerView || 26;
    let b = { ax: 0, lo: 0, hi: 0 };
    for (let v = 0; v < VIEWS; ++v) {
      const x = bounds(m.cells, v * fpv + c.first, v * fpv + c.first + c.frames);
      b = { ax: Math.max(b.ax, x.ax), lo: Math.min(b.lo, x.lo), hi: Math.max(b.hi, x.hi) };
    }
    return b;
  }
  function checkJson(u) {
    const out = [], m = u.meta, fpv = u.fpv, views = VIEWS * fpv;
    if (m.cells.length !== views && m.cells.length !== views + 20) out.push(`cell table has ${m.cells.length} cells, not ${views} or ${views + 20}`);
    if (m.clips) {
      let at = 0;
      for (const c of m.clips) { if (c.first !== at) out.push(`clip ${c.name} starts at ${c.first}, expected ${at}`); at += c.frames; }
      if (at !== fpv) out.push(`clips hold ${at} frames a view, the sheet says ${fpv}`);
    }
    let badRect = 0, offPpm = 0;
    for (let c = 0; c < Math.min(views, m.cells.length); ++c) {
      const [x, y, w, h, , , wm, hm] = m.cells[c];
      if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > m.size[0] || y + h > m.size[1] || !(wm > 0) || !(hm > 0)) { ++badRect; continue; }
      if (Math.abs(w / wm / m.ppm - 1) > 0.06 && w > 12) ++offPpm;
    }
    if (badRect) out.push(`${badRect} view cells empty or outside the atlas`);
    if (offPpm) out.push(`${offPpm} cells are not at the sheet's ${m.ppm} px a metre`);
    for (const c of u.clips) {
      if (c.frames < 2) continue;
      const seen = new Set();
      for (let f = 0; f < c.frames; ++f) seen.add(m.cells[c.first + f].slice(0, 4).join(","));
      if (seen.size * 2 <= c.frames) out.push(`${c.name}: only ${seen.size} different pictures in ${c.frames} frames (front view)`);
    }
    if (u.hd) {
      const h = u.hd;
      if ((h.framesPerView || 26) !== fpv) out.push(`close-up sheet has ${h.framesPerView || 26} frames a view, the normal ${fpv}`);
      if (h.cells.length !== m.cells.length) out.push(`close-up sheet has ${h.cells.length} cells, the normal ${m.cells.length}`);
      if (JSON.stringify((h.clips || []).map((c) => [c.name, c.first, c.frames])) !== JSON.stringify((m.clips || []).map((c) => [c.name, c.first, c.frames]))) out.push("close-up sheet lists different clips");
      const listed = state.indexHd[u.type];
      if (listed && (listed.size[0] !== h.size[0] || listed.size[1] !== h.size[1] || listed.ppm !== h.ppm)) out.push("index_hd.json size or ppm differs from the close-up sheet's own");
      // The same cell should cover the same ground in both, or the figure jumps as the close-up sheet comes in.
      const where = (c) => {
        if (c >= views) return `part ${GIBS[(c - views) >> 1]} ${"ab"[(c - views) & 1]}`;
        const f = c % fpv, clip = u.clips.find((k) => f >= k.first && f < k.first + k.frames);
        return `view ${Math.floor(c / fpv)} ${clip ? `${clip.name} ${f - clip.first + 1}/${clip.frames}` : "frame " + f}`;
      };
      let drift = 0, worst = 0, worstAt = "";
      const lost = [], gained = [];
      for (let c = 0; c < Math.min(h.cells.length, m.cells.length); ++c) {
        const a = m.cells[c], b = h.cells[c];
        if ((a[2] > 0) !== (b[2] > 0)) { (a[2] > 0 ? lost : gained).push(where(c)); continue; }
        if (a[2] <= 0) continue;
        const far = Math.max(Math.abs(a[4] - b[4]), Math.abs(a[5] - b[5]), Math.abs(a[6] - b[6]), Math.abs(a[7] - b[7]));
        if (far <= 0.1) continue;
        ++drift;
        if (far > worst) { worst = far; worstAt = `${where(c)}: ${fix(a[6], 2)}x${fix(a[7], 2)} m in the normal sheet, ${fix(b[6], 2)}x${fix(b[7], 2)} m in the close-up`; }
      }
      if (lost.length) out.push(`the close-up sheet lacks ${lost.join(", ")}, which the normal sheet has`);
      if (gained.length) out.push(`the close-up sheet has ${gained.join(", ")}, which the normal sheet lacks`);
      if (drift) out.push(`${drift} cells cover different ground in the close-up sheet (over 10 cm apart; worst ${worstAt})`);
      if (Math.abs(h.ppm / m.ppm - 2) > 0.05) out.push(`close-up sheet is ${fix(h.ppm / m.ppm, 2)}x the normal's pixels a metre, not 2x`);
    }
    return out;
  }
  function noteStats(s) {
    const hd = s.name.endsWith("_hd"), u = state.units.find((x) => (hd ? x.hdName : x.sheet) === s.name);
    if (!u) return;
    const st = s.stats, which = hd ? "close-up sheet: " : "", out = [];
    if (st.stray) out.push(`${which}${st.stray} pixels with alpha codes other than 0/153/191/255 (${st.strays.slice(0, 6).join(", ")}${st.strays.length > 6 ? ", ..." : ""})`);
    if (!st.team) out.push(`${which}no team-colour pixels (alpha 191): nothing on it takes the side's colour`);
    if (!st.outline) out.push(`${which}no outline pixels (alpha 153)`);
    if (st.empty.length) out.push(`${which}${st.empty.length} cells with no figure in them (first: cell ${st.empty[0]})`);
    u[hd ? "pixHd" : "pix"] = st;
    u[hd ? "pixIssuesHd" : "pixIssues"] = out;
    refreshIssues(u);
  }
  const issuesOf = (u) => [...u.issues, ...(u.pixIssues || []), ...(u.pixIssuesHd || [])];
  function refreshIssues(u) {
    for (const n of document.querySelectorAll(`[data-issues="${u.sheet}"]`)) renderIssues(n, u);
  }
  function renderIssues(n, u) {
    const all = issuesOf(u), checked = !!u.pix && (!u.hd || !!u.pixHd);
    n.replaceChildren();
    if (n.dataset.kind === "count") {
      n.append(all.length ? el("span", { class: "warn", title: all.join("\n") }, all.length) : el("span", { class: checked ? "ok" : "", title: checked ? "" : "pixels not read yet" }, checked ? "0" : "…"));
    } else if (all.length) n.append(el("ul", { class: "issues" }, all.map((t) => el("li", {}, t))));
    else n.append(el("span", { class: "ok" }, checked ? "Nothing wrong found in the tables or the pixels." : "Nothing wrong in the tables; pixels still loading."));
  }

  function buildUnit(type, name, m, hd) {
    const d = UNITS[type] || [name, 1.8, 0.8];
    const u = { type, sheet: name, name: d[0], height: d[1], attack: d[2], flier: !!d[3] && !(m.clips && m.clips.length), meta: m, hd, hdName: hd ? state.indexHd[type].name : null };
    u.fpv = m.framesPerView || 26;
    u.clips = clipsOf(m, u);
    u.own = !!(m.clips && m.clips.length);
    u.source = u.own ? "Tripo / import, own clips" : m.source === "import" ? "import, drawn pictures" : "old script-built (Blender)";
    u.old = m.source !== "import";
    const gibs = m.cells.length === VIEWS * u.fpv + 20 ? m.cells.slice(VIEWS * u.fpv) : [];
    u.gibs = gibs;
    u.parts = gibs.filter((c) => c[2] > 0).length;
    u.partKinds = GIBS.filter((_, i) => gibs[2 * i]?.[2] > 0 || gibs[2 * i + 1]?.[2] > 0);
    u.missing = DEATHS.filter((n) => !u.clips.some((c) => c.name === n));
    u.extra = ["run", "celebrate"].filter((n) => !u.clips.some((c) => c.name === n));
    u.best = hd ? Math.max(hd.ppm, m.ppm) : m.ppm;
    u.box = bounds(m.cells, 0, VIEWS * u.fpv);
    u.stand = bounds(m.cells, 0, u.clips[0].frames);
    // The living clips' room (the falls lie far wider and lower than a man stands).
    u.live = { ax: 0, lo: 0, hi: 0 };
    for (const c of u.clips.filter((c) => !c.name.startsWith("die_"))) {
      const b = clipBounds(m, c);
      u.live = { ax: Math.max(u.live.ax, b.ax), lo: Math.min(u.live.lo, b.lo), hi: Math.max(u.live.hi, b.hi) };
    }
    u.figurePx = m.cells[0][3];
    u.frames = u.clips.reduce((n, c) => n + c.frames, 0);
    u.issues = checkJson(u);
    return u;
  }
  const magAt = (ppm, d) => pxPerMetre(state.screen, d) / ppm;
  const sharpTo = (ppm) => state.screen / (2 * Math.tan(FOV_Y / 2) * FLAG_ABOVE * ppm);
  function ranked() {
    return [...state.units].sort((a, b) => magAt(b.best, MIN_DISTANCE) - magAt(a.best, MIN_DISTANCE) || a.clips.length - b.clips.length || (a.hd ? 1 : 0) - (b.hd ? 1 : 0)
      || a.parts - b.parts || a.type - b.type);
  }
  function badges(u) {
    const out = [];
    if (!u.hd) out.push(el("span", { class: "badge miss" }, "no HD sheet"));
    for (const n of u.missing) out.push(el("span", { class: "badge miss" }, "no " + n));
    if (!u.parts) out.push(el("span", { class: "badge miss" }, "no parts"));
    else if (u.gibs.length) out.push(el("span", { class: "badge info", title: "body-part cells: " + u.partKinds.join(", ") }, u.parts + " part cells"));
    return out;
  }

  // ---------------------------------------------------------------- drawing
  const VS = `#version 300 es
in vec2 aCorner;
uniform vec4 uRect;       // the quad in clip space: x, y, w, h
uniform vec4 uPlace;      // the cell in the sheet (px): x, y, w, h
uniform vec2 uSheetSize;  // px
uniform float uMirror;
out vec2 vUv;
void main() {
    vec2 inCell = vec2(uMirror > 0.5 ? 1.0 - aCorner.x : aCorner.x, aCorner.y) * uPlace.zw;  // kUnitVs
    vUv = (uPlace.xy + inCell) / uSheetSize;
    gl_Position = vec4(uRect.xy + aCorner * uRect.zw, 0.0, 1.0);
}`;
  const FS = `#version 300 es
precision highp float;
uniform sampler2D uSheet;
uniform vec3 uCloth;
uniform float uMode;
in vec2 vUv;
out vec4 oColor;
void main() {
    vec4 t = texture(uSheet, vUv);
    if (t.a < 0.5) discard;                                        // kUnitFs
    vec3 c = t.a > 0.68 && t.a < 0.88 ? t.rgb * uCloth : t.rgb;    // kUnitFs
    if (uMode > 0.5) {
        float g = dot(t.rgb, vec3(0.3, 0.59, 0.11));
        if (t.a > 0.96) c = vec3(g) * 0.75;
        else if (t.a > 0.735 && t.a < 0.765) c = vec3(1.0, 0.1, 0.9) * (0.35 + 0.65 * g);
        else if (t.a > 0.585 && t.a < 0.615) c = vec3(0.0, 0.9, 1.0);
        else c = vec3(1.0, 0.0, 0.0);
    }
    oColor = vec4(c, 1.0);
}`;
  function initGl() {
    const cv = $("#gl");
    gl = cv.getContext("webgl2", { alpha: true, premultipliedAlpha: true, antialias: false, preserveDrawingBuffer: false });
    if (!gl) throw new Error("WebGL2 is needed");
    const sh = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    const p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, VS));
    gl.attachShader(p, sh(gl.FRAGMENT_SHADER, FS));
    gl.bindAttribLocation(p, 0, "aCorner");
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    prog = { id: p };
    for (const n of ["uRect", "uPlace", "uSheetSize", "uMirror", "uCloth", "uMode", "uSheet"]) prog[n] = gl.getUniformLocation(p, n);
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.useProgram(p);
    gl.uniform1i(prog.uSheet, 0);
    gl.disable(gl.BLEND);
    gl.disable(gl.DEPTH_TEST);
  }
  // A tile: a box of the page the canvas draws sprites into. items(w, h) (device px) says what, each frame:
  // [{sheet, cell, mirror, s (screen px a metre), sy (how much taller than wide), fx, fy (the feet, from the box's
  // bottom left)}].
  const tiles = new Set();
  function tile(w, h, items, cls) {
    const e = el("div", { class: "tile" + (cls ? " " + cls : ""), style: `width:${w / dpr}px;height:${h / dpr}px` });
    tiles.add({ el: e, items });
    return e;
  }
  let last = 0;
  function frame(now) {
    const dt = Math.min(0.1, (now - last) / 1000 || 0);
    last = now;
    state.gridClock += dt;
    const dc = state.detail;
    const before = dc.t;
    if (!dc.paused) dc.t += dt * dc.speed;
    if (dc.unit && dc.sync !== "off" && dc.t !== before) attackSound(unitBy(dc.unit), before, dc.t);
    const cv = gl.canvas, vw = document.documentElement.clientWidth, vh = window.innerHeight;
    const W = Math.round(vw * dpr), H = Math.round(vh * dpr);
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; cv.style.width = vw + "px"; cv.style.height = vh + "px"; }
    gl.disable(gl.SCISSOR_TEST);
    gl.viewport(0, 0, W, H);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.SCISSOR_TEST);
    const t = state.team, team = [1, 3, 5].map((i) => parseInt(t.slice(i, i + 2), 16) / 255), grey = (team[0] + team[1] + team[2]) / 3;
    gl.uniform3f(prog.uCloth, team[0] * 0.75 + grey * 0.25, team[1] * 0.75 + grey * 0.25, team[2] * 0.75 + grey * 0.25);  // figures.cpp cloth
    gl.uniform1f(prog.uMode, state.mode);
    for (const tl of tiles) {
      if (!tl.el.isConnected) { tiles.delete(tl); continue; }
      const r = tl.el.getBoundingClientRect();
      if (!r.width || r.bottom <= 0 || r.top >= vh || r.right <= 0 || r.left >= vw) continue;
      const x = Math.round(r.left * dpr), y = Math.round(r.top * dpr), w = Math.round(r.width * dpr), h = Math.round(r.height * dpr);
      const list = tl.items(w, h);
      if (!list) continue;
      gl.viewport(x, H - y - h, w, h);
      gl.scissor(x, H - y - h, w, h);
      for (const it of list) {
        const s = it.sheet;
        if (!s || !s.ready) continue;
        const c = s.cells[it.cell];
        if (!c || c[2] <= 0) continue;
        const sy = it.sy || 1, x0 = it.mirror ? -(c[4] + c[6]) : c[4];
        const left = Math.round(it.fx + x0 * it.s), bottom = Math.round(it.fy + c[5] * it.s * sy);
        gl.bindTexture(gl.TEXTURE_2D, s.tex);
        gl.uniform4f(prog.uRect, (left / w) * 2 - 1, (bottom / h) * 2 - 1, ((c[6] * it.s) / w) * 2, ((c[7] * it.s * sy) / h) * 2);
        gl.uniform4f(prog.uPlace, c[0], c[1], c[2], c[3]);
        gl.uniform2f(prog.uSheetSize, s.w, s.h);
        gl.uniform1f(prog.uMirror, it.mirror ? 1 : 0);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      }
    }
    requestAnimationFrame(frame);
  }

  // ---------------------------------------------------------------- the home page
  function cameraNote() {
    const low = pxPerMetre(state.screen, MIN_DISTANCE);
    return el("div", { class: "callout" },
      el("b", {}, `At the lowest camera (${MIN_DISTANCE} m) a ${state.screen} px high screen shows ${fix(low, 0)} px a metre. `),
      `A 48 px/m sheet is then drawn at ${fix(low / 48)}x, a 96 px/m close-up sheet at ${fix(low / 96)}x (nearest-neighbour: every sheet pixel a ${fix(low / 96, 0)} to ${fix(low / 48, 0)} px square). `,
      `closeup.h allows ${MAX_MAGNIFICATION}x; this page flags more than ${FLAG_ABOVE}x in red. A 48 px/m sheet stays under ${FLAG_ABOVE}x down to ${fix(sharpTo(48))} m, a 96 px/m one down to ${fix(sharpTo(96))} m.`);
  }
  function summary() {
    const head = ["#", "Unit", "Source", "Sheet px/m", "HD px/m", "Clips", "Frames a view", "Man in sheet", `x at ${MIN_DISTANCE} m, normal`, `x at ${MIN_DISTANCE} m, best`,
      `Sharp (<=${FLAG_ABOVE}x) down to`, "Missing", "Faults"];
    const numeric = new Set([0, 3, 4, 5, 6, 7, 8, 9, 10, 12]);
    const rows = ranked().map((u, i) => {
      const mn = magAt(u.meta.ppm, MIN_DISTANCE), mb = magAt(u.best, MIN_DISTANCE);
      const iss = el("span", { "data-issues": u.sheet, "data-kind": "count" });
      renderIssues(iss, u);
      return el("tr", { class: "row", onclick: () => go(u.sheet) },
        el("td", { class: "num" }, i + 1), el("td", {}, el("a", {}, u.name), " ", el("span", { style: "color:var(--dim)" }, u.sheet)),
        el("td", {}, u.source), el("td", { class: "num" + (u.meta.ppm < 47 ? " warn" : "") }, fix(u.meta.ppm)),
        el("td", { class: "num" }, u.hd ? fix(u.hd.ppm, 0) : el("span", { class: "bad" }, "none")),
        el("td", { class: "num" + (u.clips.length <= 5 ? " warn" : "") }, u.clips.length), el("td", { class: "num" }, u.fpv),
        el("td", { class: "num", title: "height in sheet pixels of the first idle picture, front view" }, u.figurePx + " px"),
        el("td", { class: "num " + magClass(mn) }, fix(mn) + "x"), el("td", { class: "num " + magClass(mb) }, fix(mb) + "x"),
        el("td", { class: "num" }, fix(sharpTo(u.best)) + " m"), el("td", {}, badges(u)), el("td", { class: "num" }, iss));
    });
    return el("table", {}, el("thead", {}, el("tr", {}, head.map((h, i) => el("th", { class: numeric.has(i) ? "num" : "" }, h)))), el("tbody", {}, rows));
  }
  function unitSelect(value, onchange) {
    return el("select", { onchange: (e) => onchange(e.target.value) }, state.units.map((u) => el("option", { value: u.sheet, selected: u.sheet === value }, u.name)));
  }
  function facingSelect(value, onchange) {
    return el("select", { onchange: (e) => onchange(+e.target.value) }, FACINGS.map((f, i) => el("option", { value: i, selected: i === value }, `${i} ${f}${i > 4 ? " (mirrored)" : ""}`)));
  }
  const unitBy = (sheetName) => state.units.find((u) => u.sheet === sheetName);
  const sheetOf = (u, hd, halved) => sheet(hd && u.hd ? u.hdName : u.sheet, halved);
  function cellOf(u, clipName, facing, clock) {
    const c = playClip(u.clips, clipName) || u.clips[0], v = viewOf(facing);
    return { cell: v.view * u.fpv + c.first + frameOn(c, u, clock), mirror: v.mirror, clip: c };
  }
  const gridClock = () => ({ t: state.gridClock, t0: 0, loop: true, paused: false, step: 0 });

  function compare() {
    const c = state.compare, box = el("div", {});
    const build = () => {
      const a = unitBy(c.a), b = unitBy(c.b), s = c.scale;
      const hi = Math.max(a.stand.hi, b.stand.hi, a.height, b.height) + 0.4, lo = Math.min(a.stand.lo, b.stand.lo) - 0.3;
      const half = Math.max(a.stand.ax, b.stand.ax, 1) + 0.5;
      const w = Math.ceil(half * 4 * s), h = Math.ceil((hi - lo) * s), fy = Math.round(-lo * s);
      const t = tile(w, h, () => [a, b].map((u, i) => {
        const k = cellOf(u, c.clip, c.facing, gridClock());
        return { sheet: sheetOf(u, c.hd), cell: k.cell, mirror: k.mirror, s, fx: Math.round(w * (i ? 0.75 : 0.25)), fy };
      }));
      // A line every metre up from the ground.
      t.append(el("div", { class: "rule", style: `bottom:${fy / dpr}px;top:0;background:repeating-linear-gradient(to top,#ffffff40 0 1px,transparent 1px ${s / dpr}px)` }),
        el("div", { class: "ground", style: `bottom:${fy / dpr}px` }));
      const cap = (u) => el("div", { class: "cap", style: `width:${w / 2 / dpr}px` }, el("b", {}, u.name), ` · ${fix(u.height, 2)} m in the sim · ${u.source} · sheet ${fix(u.meta.ppm)} px/m` + (u.hd ? `, HD ${u.hd.ppm}` : ", no HD")
        + ` · drawn from ${c.hd && u.hd ? "the HD sheet" : "the normal sheet"} at ${fix(s / (c.hd && u.hd ? u.hd.ppm : u.meta.ppm), 2)}x`);
      box.replaceChildren(t, el("div", { style: "display:flex" }, cap(a), cap(b)));
    };
    const clipNames = KNOWN;
    const ctl = el("div", { class: "kv", style: "margin:6px 0 10px" },
      el("span", { class: "ctl" }, "A ", unitSelect(c.a, (v) => { c.a = v; build(); })), el("span", { class: "ctl" }, "B ", unitSelect(c.b, (v) => { c.b = v; build(); })),
      el("span", { class: "ctl" }, "Clip ", el("select", { onchange: (e) => { c.clip = e.target.value; } }, clipNames.map((n) => el("option", { selected: n === c.clip }, n)))),
      el("span", { class: "ctl" }, "Facing ", facingSelect(c.facing, (v) => { c.facing = v; })),
      el("span", { class: "ctl" }, "Scale ", el("select", { onchange: (e) => { c.scale = +e.target.value; build(); } }, [48, 96, 144, 192, 288].map((n) => el("option", { value: n, selected: n === c.scale }, `${n} px a metre`)))),
      el("label", { class: "ctl" }, el("input", { type: "checkbox", checked: c.hd, onchange: (e) => { c.hd = e.target.checked; build(); } }), " HD sheet where there is one"));
    build();
    return el("div", {}, ctl, box);
  }

  function gridCards() {
    const g = state.grid, wrap = el("div", { id: "grid" }), W = Math.round(170 * dpr), H = Math.round(170 * dpr);
    for (const u of state.units) {
      const playing = el("div", { class: "playing" });
      // One pass of each clip in turn (an idle loop first), or one clip.
      const order = u.clips.map((c) => ({ c, len: clipLength(c, u) * (c.loop && c.name !== "idle" ? 2 : 1) + (c.loop ? 0 : 0.5) }));
      const total = order.reduce((n, o) => n + o.len, 0);
      let shown = "";
      const t = tile(W, H, (w, h) => {
        let c, f;
        if (g.clip === "cycle") {
          let at = state.gridClock % total, o = order[0];
          for (o of order) { if (at < o.len) break; at -= o.len; }
          c = o.c;
          f = frameAt(c, Math.min(at, c.loop ? Infinity : clipLength(c, u)), clipLength(c, u), c.loop);
        } else {
          c = playClip(u.clips, g.clip) || u.clips[0];
          f = frameOn(c, u, gridClock());
        }
        const label = `${c.name} ${f + 1}/${c.frames}`;
        if (label !== shown) playing.textContent = shown = label;
        // Fitted to the living clips; a fall is drawn smaller if it would not fit.
        const b = g.same ? { ax: 2.4, lo: -0.6, hi: 6.6 } : c.name.startsWith("die_") ? u.box : u.live, v = viewOf(g.facing);
        const s = Math.min((w - 8) / (2 * b.ax), (h - 8) / (b.hi - b.lo));
        return [{ sheet: sheet(u.sheet), cell: v.view * u.fpv + c.first + f, mirror: v.mirror, s, fx: w / 2, fy: 4 - b.lo * s }];
      });
      wrap.append(el("div", { class: "card", onclick: () => go(u.sheet) },
        el("h3", {}, u.name, " ", el("span", { style: "color:var(--dim);font-weight:400" }, `#${u.type} ${u.sheet}`)),
        t, playing,
        el("div", { class: "meta" }, `${u.source}`, el("br"), `${fix(u.meta.ppm)} px/m · ${u.fpv} frames a view · ${u.clips.length} clips`, el("br"),
          u.hd ? el("span", { class: "ok" }, `HD sheet ${u.hd.ppm} px/m`) : el("span", { class: "bad" }, "no HD sheet")),
        el("div", {}, badges(u))));
    }
    const ctl = el("div", { class: "kv", style: "margin:6px 0 10px" },
      el("span", { class: "ctl" }, "Play ", el("select", { onchange: (e) => { g.clip = e.target.value; } }, ["cycle", ...KNOWN].map((n) => el("option", { value: n, selected: n === g.clip }, n === "cycle" ? "every clip in turn" : n)))),
      el("span", { class: "ctl" }, "Facing ", facingSelect(g.facing, (v) => { g.facing = v; })),
      el("label", { class: "ctl" }, el("input", { type: "checkbox", checked: g.same, onchange: (e) => { g.same = e.target.checked; } }), " all at one scale (metres)"));
    return el("div", {}, ctl, wrap);
  }
  function home() {
    const m = $("#home");
    m.replaceChildren(
      cameraNote(),
      el("h2", {}, "Summary, worst first"),
      el("p", { class: "note" }, `Sorted by how far the best sheet a unit has is magnified at the lowest camera, then fewest clips, then no HD sheet. "x" is screen pixels per sheet pixel across; red is over ${FLAG_ABOVE}x. Click a row for the unit.`),
      summary(),
      el("h2", {}, "Compare two units at one scale"),
      el("p", { class: "note" }, "Both stand on one ground line; the lines are a metre apart."),
      compare(),
      el("h2", {}, `All ${state.units.length} units`),
      gridCards());
  }

  // ---------------------------------------------------------------- a unit
  function detail(u) {
    const d = state.detail, m = $("#detail");
    if (d.unit !== u.sheet) d.scale = u.height * 96 <= 300 ? 96 : 48;
    d.unit = u.sheet;
    if (!u.hd) d.hd = false;
    const useHd = () => d.hd && !!u.hd;
    const iss = el("div", { "data-issues": u.sheet, "data-kind": "list" });
    renderIssues(iss, u);
    if (u.hd) sheet(u.hdName);  // its pixels are checked too
    const head = el("div", {},
      el("h2", {}, u.name, " ", el("span", { style: "color:var(--dim);font-weight:400" }, `#${u.type} · ${u.sheet}.webp`)),
      el("div", { class: "kv" },
        el("span", {}, "Source ", el("b", {}, u.source)), el("span", {}, "Sheet ", el("b", {}, `${u.meta.size[0]}x${u.meta.size[1]}, ${fix(u.meta.ppm)} px/m`)),
        el("span", {}, "HD sheet ", u.hd ? el("b", {}, `${u.hd.size[0]}x${u.hd.size[1]}, ${u.hd.ppm} px/m`) : el("b", { class: "bad" }, "none")),
        el("span", {}, "Frames a view ", el("b", {}, u.fpv)), el("span", {}, "Clips ", el("b", {}, u.clips.length)), el("span", {}, "Height in the sim ", el("b", {}, u.height + " m")),
        el("span", {}, "Body-part cells ", el("b", { class: u.parts ? "" : "bad" }, u.parts ? `${u.parts} (${u.partKinds.join(", ")})` : "none"))),
      el("div", { style: "margin-top:6px" }, badges(u), u.extra.map((n) => el("span", { class: "badge info" }, `no ${n} (plays ${FALLBACK[n]})`))),
      iss);

    // The zoom: the game's camera.
    const zoomBox = el("div", {});
    const buildZoom = () => {
      const cams = [...CAMERAS, { key: "custom", d: d.custom, label: "Your distance", why: `${fix(d.custom)} m` }];
      const rows = [{ label: "Normal sheet", hd: false, half: false, ppm: u.meta.ppm }];
      if (u.hd) rows.push({ label: "HD sheet", hd: true, half: false, ppm: u.hd.ppm });
      if (d.halved) rows.push({ label: "Normal sheet, halved (a battle of many types: over the 26 M texel budget)", hd: false, half: true, ppm: u.meta.ppm / 2 });
      const W = Math.round(250 * dpr), H = Math.round(330 * dpr);
      const grid = el("div", { class: "zoomgrid", style: `grid-template-columns: 120px repeat(${cams.length}, ${W / dpr}px)` });
      grid.append(el("div", {}));
      for (const c of cams) grid.append(el("div", { class: "colhead" }, c.label, el("span", {}, `${c.why} · ${fix(pxPerMetre(state.screen, c.d), 0)} px/m on screen`)));
      for (const r of rows) {
        grid.append(el("div", { class: "rowlab" }, r.label, el("div", { class: "cap", style: "text-align:left" }, `${fix(r.ppm)} px/m`)));
        for (const c of cams) {
          const s = pxPerMetre(state.screen, c.d), sy = stretchAt(c.d), mag = s / r.ppm;
          // closeup.cpp choose: the close-up sheet once the normal one is magnified past kLoadAbove.
          const gameHd = !!u.hd && s / u.meta.ppm > LOAD_ABOVE;
          const drawn = !r.half && r.hd === gameHd;
          const t = tile(W, H, (w, h) => {
            const k = cellOf(u, d.zoomClip, d.zoomFacing, d), tall = u.height * s * sy;
            const fy = tall <= h * 0.86 ? (h - tall) / 2 : h / 2 - d.focus * tall;
            return [{ sheet: sheetOf(u, r.hd, r.half), cell: k.cell, mirror: k.mirror, s, sy, fx: w / 2, fy }];
          }, drawn ? "drawn" : "");
          grid.append(el("div", { class: "cell" }, t, el("div", { class: "cap" },
            el("b", { class: magClass(mag) }, `${fix(mag, 2)}x`), ` · ${fix(s, 0)} / ${fix(r.ppm)} · a ${u.height} m figure ${fix(u.height * s * sy, 0)} px tall`,
            drawn ? el("div", { style: "color:#8ab4ff" }, "the game draws this one") : null)));
        }
      }
      zoomBox.replaceChildren(grid);
    };
    const zoomCtl = el("div", { class: "kv", style: "margin:6px 0 10px" },
      el("span", { class: "ctl" }, "Clip ", el("select", { onchange: (e) => { d.zoomClip = e.target.value; } }, u.clips.map((c) => el("option", { selected: c.name === d.zoomClip }, c.name)))),
      el("span", { class: "ctl" }, "Facing ", facingSelect(d.zoomFacing, (v) => { d.zoomFacing = v; })),
      el("span", { class: "ctl" }, "Look at: feet ", el("input", { type: "range", min: 0, max: 1, step: 0.01, value: d.focus, oninput: (e) => { d.focus = +e.target.value; } }), " head"),
      el("span", { class: "ctl" }, "Your distance ", el("input", { type: "range", min: Math.log(2), max: Math.log(160), step: 0.01, value: Math.log(d.custom),
        onchange: (e) => { d.custom = Math.exp(+e.target.value); buildZoom(); } })),
      el("label", { class: "ctl" }, el("input", { type: "checkbox", checked: d.halved, onchange: (e) => { d.halved = e.target.checked; buildZoom(); } }), " also the halved sheet"));
    if (!u.clips.some((c) => c.name === d.zoomClip)) d.zoomClip = u.clips[0].name;
    buildZoom();

    // Every clip, in all eight facings.
    const clipBox = el("div", {});
    const buildClips = () => {
      const s = d.scale, src = useHd() ? u.hd : u.meta, rows = [];
      for (const c of u.clips) {
        const b = clipBounds(u.meta, c), w = Math.ceil(2 * b.ax * s) + 8, h = Math.ceil((b.hi - b.lo) * s) + 8, len = clipLength(c, u);
        const fr = el("span", { class: "f" });
        let shown = -1;
        const cells = FACINGS.map((name, facing) => {
          const v = viewOf(facing);
          const t = tile(w, h, () => {
            const f = frameOn(c, u, d);
            if (facing === 0 && f !== shown) { shown = f; fr.textContent = `frame ${f + 1} of ${c.frames} (sheet frame ${c.first + f})`; }
            return [{ sheet: sheetOf(u, useHd()), cell: v.view * u.fpv + c.first + f, mirror: v.mirror, s, fx: Math.round(w / 2), fy: Math.round(4 - b.lo * s) }];
          });
          return el("div", { class: "cell" }, t, el("div", { class: "cap" }, `${facing} ${name}${v.mirror ? " ⇋" + v.view : ""}`));
        });
        rows.push(el("div", { class: "cliprow" },
          el("div", { class: "head" }, el("h3", {}, c.name), el("div", { class: "f" },
            `${c.frames} frames · ${fix(len, 2)} s${c.seconds > 0 ? "" : " (the game's)"} · ${fix(c.frames / len)} fps`, el("br"),
            c.loop ? "loops" : "once", c.stride ? ` · stride ${c.stride} m` : "", c.release >= 0 ? ` · release ${c.release} s` : "", c.times ? " · own frame times" : "", c.note ? ` · ${c.note}` : ""), fr),
          el("div", { class: "facings" }, cells)));
      }
      const absent = KNOWN.filter((n) => !u.clips.some((c) => c.name === n));
      clipBox.replaceChildren(
        el("p", { class: "note" }, `Drawn at ${s} screen px a metre from the ${useHd() ? "HD" : "normal"} sheet (${fix(src.ppm)} px/m): ${fix(s / src.ppm, 2)}x, the game's size at ${fix(state.screen / (2 * Math.tan(FOV_Y / 2) * s))} m on a ${state.screen} px screen. Facings 5 to 7 are views 3, 2, 1 mirrored (⇋).`),
        ...rows,
        absent.length ? el("p", { class: "note" }, "Not in this sheet: ", absent.map((n) => { const p = playClip(u.clips, n); return `${n} (plays ${p ? p.name : "nothing"})`; }).join(", "), ".") : null);
    };
    buildClips();

    // The body parts.
    const gibBox = el("div", { class: "gibs" });
    const buildGibs = () => {
      gibBox.replaceChildren();
      if (!u.gibs.length) { gibBox.append(el("p", { class: "note bad" }, "This sheet has no body-part cells: a body blown apart throws the old 3D parts.")); return; }
      const s = d.scale, big = Math.max(0.3, ...u.gibs.map((c) => Math.max(c[6], c[7]))), side = Math.ceil(big * s) + 10, base = VIEWS * u.fpv;
      u.gibs.forEach((c, i) => {
        const name = GIBS[i >> 1] + " " + "ab"[i & 1];
        if (c[2] <= 0) {
          const stand = [GIBS[i >> 1], ...GIB_CHAIN[GIBS[i >> 1]]].find((g) => { const k = GIBS.indexOf(g) * 2; return u.gibs[k][2] > 0 || u.gibs[k + 1][2] > 0; });
          gibBox.append(el("div", { class: "cell" }, el("div", { class: "tile empty", style: `width:${side / dpr}px;height:${side / dpr}px` }),
            el("div", { class: "cap" }, el("b", {}, name), el("br"), stand ? `none: draws ${stand}` : "none")));
          return;
        }
        const t = tile(side, side, (w, h) => [{ sheet: sheetOf(u, useHd()), cell: base + i, mirror: false, s, fx: Math.round(w / 2 - (c[4] + c[6] / 2) * s), fy: Math.round(h / 2 - (c[5] + c[7] / 2) * s) }]);
        gibBox.append(el("div", { class: "cell" }, t, el("div", { class: "cap" }, el("b", {}, name), el("br"), `${c[2]}x${c[3]} px · ${fix(c[6], 2)}x${fix(c[7], 2)} m`)));
      });
    };
    buildGibs();

    const rebuild = () => { buildClips(); buildGibs(); };
    const btn = (label, on, click, title) => el("button", { class: on ? "on" : "", onclick: click, title }, label);
    const bar = el("div", { class: "bar sub" });
    const drawBar = () => bar.replaceChildren(
      el("a", { onclick: () => go(null) }, "← all units"),
      el("div", { class: "ctl" }, btn(d.paused ? "▶ play" : "❚❚ pause", false, () => { d.paused = !d.paused; d.step = 0; drawBar(); }),
        btn("◀ frame", false, () => { d.paused = true; --d.step; drawBar(); }, "back a frame"), btn("frame ▶", false, () => { d.paused = true; ++d.step; drawBar(); }, "on a frame"),
        btn("↺ restart", false, () => { d.t0 = d.t; d.step = 0; })),
      el("div", { class: "ctl" }, "Speed ", [0.1, 0.25, 0.5, 1, 2].map((v) => btn(v + "x", d.speed === v, () => { d.speed = v; drawBar(); }))),
      el("div", { class: "ctl" }, btn("loop", d.loop, () => { d.loop = !d.loop; d.t0 = d.t; drawBar(); }, "off: every clip plays once and holds its last frame")),
      el("div", { class: "ctl" }, "Sheet ", btn("normal", !useHd(), () => { d.hd = false; drawBar(); rebuild(); }), u.hd ? btn("HD", useHd(), () => { d.hd = true; drawBar(); rebuild(); }) : el("span", { class: "bad" }, "no HD")),
      el("div", { class: "ctl" }, "Clips at ", el("select", { onchange: (e) => { d.scale = +e.target.value; rebuild(); } },
        [48, 96, 144, 192, 288, 384].map((n) => el("option", { value: n, selected: n === d.scale }, `${n} px a metre`)))),
      el("div", { class: "ctl" }, "Unit ", unitSelect(u.sheet, (v) => go(v))));
    drawBar();

    m.replaceChildren(bar, head,
      el("h2", {}, "Zoom: the game's camera"),
      el("p", { class: "note" }, `Each box is a ${250}x${330} px window of the screen, the figure at the size the camera shows it there (a ${state.screen} px high screen, 45 degree field of view) and through the game's filter: nearest-neighbour when magnified, trilinear over three mip levels when shrunk. Figures stand a little taller than wide as the camera drops (the billboard is upright, the pictures were rendered from 30 degrees). The number is screen pixels per sheet pixel across.`),
      zoomCtl, zoomBox,
      el("h2", { id: "sounds-h" }, "Sounds"), soundsPanel(u, drawBar),
      el("h2", {}, "Clips"), clipBox,
      el("h2", {}, "Body parts"),
      el("p", { class: "note" }, "The 20 part cells after the views' (10 parts, two looks each), as stored; the game spins them in the picture's plane."),
      gibBox);
    window.scrollTo(0, 0);
  }

  // ---------------------------------------------------------------- a unit's sounds
  const S = window.AuditSound, SU = window.AuditSoundUi;
  let soundsReady = false;
  // When in its attack clip the game sends the sound, and when the picture shows the blow.
  //   The game: Battle::attackTick is the tick the blow was struck or the missile loosed, and the attack clip starts
  //   there (renderer_sheets.cpp listedFrame, sprite_clips.cpp fixedLayoutFrame), as do the cues (sfx.cpp: the swing
  //   at once, what it met 70 ms on). A missile clip that names its `release` is played so that frame meets the shot.
  //   The picture: docs/SPRITE_SPEC.md: the blow lands on frame 3 of 6, the missile has left on frame 4 of 6.
  function attackTiming(u, mode) {
    const c = playClip(u.clips, "attack"), k = S.unitSounds(u.type);
    if (!c) return null;
    const len = clipLength(c, u), ranged = !!k.launch;
    const shown = c.release >= 0 ? c.release : len * (ranged ? 4 / 6 : 3 / 6);
    const at = mode === "game" ? (ranged && c.release >= 0 ? c.release : 0) : shown;
    return { c, len, at, shown, ranged, k };
  }
  function attackSound(u, t0, t1) {
    const d = state.detail, a = u && soundsReady && attackTiming(u, d.sync);
    if (!a) return;
    const period = d.loop ? (a.c.loop ? a.len : a.len + 0.6) : Infinity;
    const local = (t) => (d.loop ? t - Math.floor(t / period) * period : t - d.t0);
    const crossed = (when) => { const x = local(t0), y = local(t1); return x <= y ? x < when && when <= y : when <= y || when > x; };
    if (a.ranged) {
      if (crossed(Math.max(a.at, 1e-3))) S.play(a.k.launch, -1).then((r) => SU.say(`with the attack clip: ${S.sounds.get(a.k.launch).title}: ${r.text}`, r.seconds ? "" : "bad"));
      return;
    }
    // The swing, then 70 ms on what it met; on the blow frame, the swing 70 ms before it.
    const swing = d.sync === "game" ? a.at : Math.max(0, a.at - 0.07);
    if (crossed(Math.max(swing, 1e-3))) {
      if (a.k.swing) S.play(a.k.swing, -1).then((r) => SU.say(`with the attack clip: ${S.sounds.get(a.k.swing).title}, then ${S.sounds.get(a.k.met[d.victim]).title}: ${r.text}`, r.seconds ? "" : "bad"));
      const met = a.k.met[d.victim];
      if (met) S.play(met, -1, { delay: (d.sync === "game" ? 0.07 : a.at - swing) / Math.max(d.speed, 0.1) });
    }
  }
  function soundsPanel(u, redraw) {
    const d = state.detail;
    if (!soundsReady) return el("p", { class: "note bad" }, "The sounds could not be read (game/web/sfx/sounds.json).");
    const k = S.unitSounds(u.type), set = S.sets.get(k.voice), box = el("div", {}), said = el("span", { class: "said", id: "said" }, "Nothing plays until you press a button.");
    SU.sayTo(said);
    const t = attackTiming(u, "game"), blow = attackTiming(u, "blow");
    const group = (name, ...kids) => el("div", { style: "margin:8px 0" }, el("h3", {}, name), kids);
    const weapon = { slash: "a cut (sword, sabre, claw)", pierce: "a thrust (pike, lance, fang, horn)", cleave: "a cleaving blow (axe, great sword, claw)", blunt: "a blunt blow (club, staff, hoof)" }[k.melee];
    const sync = el("select", { id: "sync", onchange: (e) => { d.sync = e.target.value; if (d.sync !== "off") S.unlock(); } },
      [["off", "off"], ["game", "when the game sends it"], ["blow", "on the frame that shows the blow"]].map(([v, text]) => el("option", { value: v, selected: v === d.sync }, text)));
    const victim = el("select", { onchange: (e) => { d.victim = e.target.value; } }, [["flesh", "flesh"], ["armour", "mail and plate"], ["bone", "the walking dead"], ["shield", "a shield (blocked)"]].map(([v, text]) => el("option", { value: v, selected: v === d.victim }, text)));
    const lines = (only) => SU.voiceLines(set, only);
    const more = el("details", { class: "set" }, el("summary", {}, `Every line of the ${set.name} voice`));
    more.addEventListener("toggle", () => { if (more.open && !more._built) { more._built = true; more.append(lines(null)); } });
    box.append(
      el("div", { class: "kv" }, SU.controls(), said),
      t ? group("The attack clip and its sound",
        el("div", { class: "kv" }, el("span", { class: "ctl" }, "Play the attack's sound with the clip ", sync), k.ranged ? null : el("span", { class: "ctl" }, "striking ", victim)),
        el("p", { class: "note" }, `The game starts the attack clip and sends ${k.ranged ? "the launch sound" : "the swing (and 70 ms later what it met)"} in the same tick, the one the ${k.ranged ? "missile is loosed" : "blow is struck"}: ${fix(t.at, 2)} s into this ${fix(t.len, 2)} s clip. `,
          Math.abs(t.at - blow.shown) > 0.05 ? el("span", { class: "warn" }, `The picture shows the ${k.ranged ? "missile leaving" : "blow landing"} about ${fix(blow.shown, 2)} s in (${u.own && t.c.release >= 0 ? "the clip's release" : u.own ? "taken as " + (k.ranged ? "two thirds" : "half") + " way through: the clip does not say" : k.ranged ? "frame 5 of 6" : "frame 4 of 6"}): the sound comes ${fix(blow.shown - t.at, 2)} s before it.`)
            : el("span", { class: "ok" }, "The picture shows it at the same moment."), " Set the clips looping below and choose one to hear both.")) : null,
      group(`Its weapon: ${k.ranged ? { arrow: "a bow", fireball: "a fireball", molotov: "a thrown firebomb", stone: "a hurled stone" }[k.ranged] + (k.swing ? ", and " + weapon + " at arm's length" : "") : weapon}`,
        k.launch ? SU.soundLine(k.launch, "as the missile leaves") : null, k.lands.map((id) => SU.soundLine(id, "where it lands")),
        k.swing ? [SU.soundLine(k.swing, "the swing"), SU.soundLine(k.met.flesh, "70 ms on, into flesh"), k.met.armour !== k.met.flesh ? SU.soundLine(k.met.armour, "on mail and plate") : null,
          SU.soundLine(k.met.bone, "on the walking dead"), SU.soundLine(k.met.shield, "blocked by a shield")] : null,
        k.heals ? SU.soundLine(k.heals, "his healing") : null),
      group(`When it is struck (${k.bone ? "bone" : u.type === 26 ? "timber" : k.armoured ? "armoured" : "flesh"})`, k.struck.map((id) => SU.soundLine(id, id.indexOf("impact_arrow") >= 0 ? "an arrow" : "a blade")), lines(["pain"])),
      group("Its death", SU.soundLine(k.fall, "the body, 0.6 s after the blow"), SU.soundLine("snd_gore_dismember_snap", "a limb or the head off"), SU.soundLine("snd_gore_gib_burst", "blown apart"),
        k.bone ? null : SU.soundLine("snd_gore_flesh_burn", "burned"), lines(["death", "death_moan"])),
      group("Moving", el("p", { class: "bad", style: "font-weight:400" }, `No sound: nothing that moves makes one (no footsteps, hooves, wings or wheels in the Sound enum). The ${playClip(u.clips, "walk") ? "walk" : "move"} clips play in silence.`)),
      group(`Its voice: ${set.name}`, el("p", { class: "note" }, `Shared with ${set.units.join(", ")}. `, SU.voiceCounts(set)), lines(["selected", "move", "attack"]), more));
    return box;
  }

  // ---------------------------------------------------------------- the page
  function go(sheetName) { location.hash = sheetName ? "#u=" + sheetName : ""; }
  function route() {
    const want = /^#u=([a-z_]+)/.exec(location.hash), u = want && unitBy(want[1]);
    for (const t of tiles) tiles.delete(t);
    const keep = new Set();
    if (u && u.hd) keep.add(u.hdName), keep.add(u.sheet);
    if (!u) for (const k of [state.compare.a, state.compare.b]) { const c = unitBy(k); if (c && c.hd) keep.add(c.hdName); }
    release(keep);
    $("#home").hidden = !!u;
    $("#detail").hidden = !u;
    if (u) { $("#home").replaceChildren(); detail(u); }
    else { $("#detail").replaceChildren(); state.detail.unit = null; home(); }
    document.documentElement.style.setProperty("--bar-h", $("#topbar").offsetHeight + "px");
  }
  function status() {
    const n = [...sheets.values()].filter((s) => !s.ready && !s.failed).length;
    $("#status").textContent = (n ? `loading ${n} sheet${n > 1 ? "s" : ""}… ` : "") + (state.errors.length ? `${state.errors.length} failed: ${state.errors[0]}` : "");
  }
  function drawTeams() {
    const order = [...SEATS.map((i) => SWATCHES[i]), ...SWATCHES.slice(8)];
    $("#teams").replaceChildren(...order.map(([name, hex]) => el("button", { class: "sw" + (hex.toLowerCase() === state.team ? " on" : ""), style: `background:${hex}`, title: name,
      onclick: () => { state.team = hex.toLowerCase(); $("#team-own").value = state.team; drawTeams(); } })));
  }
  async function findBase() {
    // Beside the web build's sprites (/sprites-audit/), or in the repository served from its root.
    const bases = ["../sprites/", "../../game/web/sprites/"];
    if (location.pathname.includes("/tools/audit")) bases.reverse();
    for (const base of bases) {
      try {
        const r = await fetch(base + "index.json");
        if (r.ok) return { base, index: await r.json() };
      } catch (e) { /* the next */ }
    }
    throw new Error("sprites/index.json not found: serve the repository's root (tools/audit/serve.sh)");
  }
  async function start() {
    initGl();
    const found = await findBase();
    state.base = found.base;
    state.index = found.index;
    state.indexHd = await fetch(state.base + "index_hd.json").then((r) => (r.ok ? r.json() : {})).catch(() => ({}));
    // The game's own palette, when the page can reach it.
    await new Promise((done) => {
      const s = document.createElement("script");
      s.src = state.base + "../palette.js";
      s.onload = s.onerror = done;
      document.head.append(s);
    });
    if (window.IllyriaPalette && window.IllyriaPalette.swatches) SWATCHES = window.IllyriaPalette.swatches;
    state.team = SWATCHES[6][1].toLowerCase();
    const types = Object.keys(state.index).map(Number).sort((a, b) => a - b);
    state.units = await Promise.all(types.map(async (t) => {
      const name = state.index[t], hd = state.indexHd[t];
      return buildUnit(t, name, await meta(name), hd ? await meta(hd.name).catch(() => null) : null);
    }));
    // The sounds, for a unit's Sounds panel (the sprites work without them).
    try { await S.load(); S.settings.camera = 15; soundsReady = true; } catch (e) { console.warn("sounds: " + e.message); }
    drawTeams();
    $("#team-own").oninput = (e) => { state.team = e.target.value.toLowerCase(); drawTeams(); };
    $("#bg").onchange = (e) => { document.body.dataset.bg = e.target.value; };
    $("#mode").onchange = (e) => { state.mode = +e.target.value; };
    const setScreen = (v) => { state.screen = Math.min(4320, Math.max(300, Math.round(v) || 1080)); $("#screen").value = state.screen; route(); };
    $("#screen").onchange = (e) => setScreen(+e.target.value);
    $("#screen-mine").onclick = () => setScreen(window.innerHeight * Math.min(dpr, 2));
    $("#home-link").onclick = () => go(null);
    window.addEventListener("hashchange", route);
    route();
    requestAnimationFrame(frame);
    // The faults column needs every sheet's pixels: read them once the page is up.
    setTimeout(() => window.__audit.checkAll(), 1200);
  }

  // For scripts (tools/audit/shots.mjs): the numbers behind the page.
  window.__audit = {
    state,
    ready: null,
    // Every sheet's pixels read (the close-up ones too), a few at a time.
    async checkAll() {
      for (const u of state.units) {
        for (const name of [u.sheet, u.hdName]) {
          if (!name) continue;
          const s = sheet(name);
          while (!s.ready && !s.failed) await new Promise((r) => setTimeout(r, 50));
        }
        release(new Set([state.detail.unit && unitBy(state.detail.unit)?.hdName, unitBy(state.compare.a)?.hdName, unitBy(state.compare.b)?.hdName]));
      }
    },
    report() {
      return ranked().map((u) => ({
        type: u.type, sheet: u.sheet, name: u.name, source: u.source, ppm: u.meta.ppm, hdPpm: u.hd ? u.hd.ppm : null, size: u.meta.size, hdSize: u.hd ? u.hd.size : null,
        framesPerView: u.fpv, clips: u.clips.map((c) => c.name), frames: u.frames, figurePx: u.figurePx, height: u.height,
        magLowestNormal: magAt(u.meta.ppm, MIN_DISTANCE), magLowestBest: magAt(u.best, MIN_DISTANCE), sharpTo: sharpTo(u.best),
        missing: u.missing, parts: u.parts, partKinds: u.partKinds, issues: issuesOf(u), teamShare: u.pix ? u.pix.teamShare : null, fill: u.pix ? u.pix.fill : null,
      }));
    },
    camera: { pxPerMetre, pitch, stretchAt, MIN_DISTANCE, FLAG_ABOVE },
  };
  window.__audit.ready = start().catch((e) => {
    console.error(e);
    $("#home").replaceChildren(el("p", { class: "bad", style: "margin-top:20px" }, String(e.message || e)));
    throw e;
  });
})();
