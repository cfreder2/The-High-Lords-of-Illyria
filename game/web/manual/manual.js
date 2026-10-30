// The manual's book reader. The chapters are Markdown (docs/manual/*.md,
// listed in book.json), turned into one long flow of HTML (md.js), laid out
// in CSS columns one page wide and the screen's height, and shown a page at a
// time: two "leaves" each show the same flow shifted to a page, and turning
// a page rotates the top leaf away about its spine (or brings the previous
// one back), following the finger. Contents, the index and cross references
// get their page numbers once the pages are laid out.
(function () {
  "use strict";
  const MD = window.IllyriaMarkdown;
  const $ = (id) => document.getElementById(id);
  const BOOK = "book/";
  MD.base = BOOK;  // pictures in the text are in the book's folder

  const store = {
    get(k) { try { return localStorage.getItem("illyria.manual." + k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem("illyria.manual." + k, v); } catch (e) { /* private mode: forget it */ } },
  };

  // ---- Turning Markdown into the book's HTML ----------------------------

  let units = {};        // UNITS.md rows by unit name, for ::: stats
  let unitsText = "";    // and the sheet itself, for ::: sheet
  let chapterNo = 0, sectionNo = 0, subNo = 0, numbered = false;
  const headings = [];   // every heading: {level, id, text, number, chapter}
  let autoId = 0;
  const usedIds = new Set();

  const roman = (n) => {
    let out = "";
    for (const [v, r] of [[10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"]]) while (n >= v) { out += r; n -= v; }
    return out;
  };
  const ones = ["", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Eleven", "Twelve", "Thirteen",
    "Fourteen", "Fifteen", "Sixteen", "Seventeen", "Eighteen", "Nineteen"];
  const word = (n) => (n < 20 ? ones[n] : ["Twenty", "Thirty"][Math.floor(n / 10) - 2] + (n % 10 ? "-" + ones[n % 10] : ""));
  const slug = (s) => s.toLowerCase().replace(/<[^>]+>/g, "").replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");

  const ctx = {
    heading(level, text, id, html) {
      text = text.replace(/<[^>]+>/g, "").trim();
      id = id || `h-${slug(text) || ++autoId}`;
      while (usedIds.has(id)) id += `-${++autoId}`;  // two sections may share a title
      usedIds.add(id);
      let number = "";
      if (level === 1) {
        if (numbered) { ++chapterNo; sectionNo = subNo = 0; }
        const kicker = numbered ? `<div class="kicker">Chapter ${word(chapterNo) || chapterNo}</div>` : "";
        headings.push({ level, id, text, number: numbered ? String(chapterNo) : "", chapter: true });
        return `<header class="opener${numbered ? "" : " plain"}" data-anchor="${id}" data-chapter="${MD.attr(text)}">` +
          `${numbered ? `<div class="numeral">${roman(chapterNo)}</div>` : ""}${kicker}<h1>${html}</h1><div class="orn">❦</div></header>`;
      }
      if (numbered && level === 2) { ++sectionNo; subNo = 0; number = `${chapterNo}.${sectionNo}`; }
      else if (numbered && level === 3) { ++subNo; number = `${chapterNo}.${sectionNo}.${subNo}`; }
      headings.push({ level, id, text, number, chapter: false });
      return `<h${level} data-anchor="${id}">${number ? `<span class="no">${number}</span> ` : ""}${html}</h${level}>`;
    },
    figure(src, caption, cls, captionHtml) {
      return `<figure class="${MD.attr(cls)}"><img src="${MD.attr(BOOK + src)}" alt="${MD.attr(caption)}">` +
        (caption ? `<figcaption>${captionHtml}</figcaption>` : "") + `</figure>`;
    },
    block(name, args, body, parse) {
      switch (name) {
        case "tip": case "lore": case "note": case "warn": {
          const label = args.replace(/^"|"$/g, "") || { tip: "Hint", lore: "", note: "Note", warn: "Coming soon" }[name];
          return `<aside class="${name}">${label ? `<div class="label">${MD.inline(label)}</div>` : ""}${parse(body)}</aside>`;
        }
        case "break": return `<div class="pagebreak"></div>`;
        case "keep": return `<div class="keep">${parse(body)}</div>`;
        case "cover": return `<section class="cover">${parse(body)}</section>`;
        case "contents": return `<nav class="toc" data-toc="1"></nav>`;
        case "index": return `<nav class="index" data-index="1"></nav>`;
        case "stats": return statsCard(args);
        case "sheet": {
          // A table of UNITS.md as it is: "::: sheet Ground".
          const m = new RegExp(`^## ${args}\\s*$([\\s\\S]*?)(?=^## |$(?![\\s\\S]))`, "m").exec(unitsText);
          if (!m) return `<div class="stats missing">(no "${MD.esc(args)}" in the unit sheet)</div>`;
          const table = m[1].split("\n").filter((l) => /^\|/.test(l)).join("\n");
          return `<div class="small">${parse(table)}</div>`;
        }
        case "roster": return roster();
        case "unit": {
          // ::: unit Heavy Infantry | heavy_infantry
          const [title, pic] = args.split("|").map((s) => s.trim());
          const head = ctx.heading(3, title, "", MD.inline(title));
          return `<section class="unit">${head}<div class="unit-top">` +
            (pic ? `<img class="sprite" src="${BOOK}images/units/${MD.attr(pic)}.png" alt="${MD.attr(title)}">` : "") +
            statsCard(title) + `</div>${parse(body)}</section>`;
        }
        default: return `<div class="${MD.attr(name)}">${parse(body)}</div>`;
      }
    },
  };

  // UNITS.md is generated from the code (tools/unitsheet.cpp): read its tables
  // so the compendium's numbers are always the game's.
  function readUnits(text) {
    const rows = {};
    let table = null, cols = [];
    for (const line of text.split("\n")) {
      const h = /^## (.*)/.exec(line);
      if (h) { table = h[1].trim(); continue; }
      if (!/^\|/.test(line) || /^\|\s*-/.test(line)) { if (!/^\|/.test(line)) cols = []; continue; }
      const cells = line.trim().replace(/^\||\|$/g, "").split("|").map((c) => c.trim());
      if (!cols.length) { cols = cells; continue; }
      const rec = {};
      cols.forEach((c, k) => { rec[c] = cells[k]; });
      if (table === "Ground") {
        for (const u of (rec.Units || "").split(",")) (rows[u.trim()] = rows[u.trim()] || {}).gait = rec;
      } else {
        const r = (rows[cells[0]] = rows[cells[0]] || {});
        r[table] = rec;
      }
    }
    return rows;
  }

  function statsCard(name) {
    const u = units[name];
    if (!u || !u["Body and melee"]) return `<div class="stats missing">(no stats for ${MD.esc(name)})</div>`;
    const b = u["Body and melee"], m = u["Missile troops"], f = u.Fliers, g = u.gait;
    const row = (k, v) => (v && v !== "0" && v !== "0.0" && v !== "0.00 s" ? `<div><dt>${k}</dt><dd>${v}</dd></div>` : "");
    let h = `<dl class="stats"><div class="str"><dt>Strength</dt><dd>${b.Strength}</dd></div>` +
      row("Hit points", b.HP) + row("Speed", `${b.Speed} m/s`) + row("Height", `${b.Height} m`) + row("Mass", b.Mass) +
      row("Blow", b["Melee dmg"] !== "0" ? `${b["Melee dmg"]} ${b.Blow} every ${b.Every}` : "") + row("Reach", b.Reach !== "0.0" ? `${b.Reach} m` : "") +
      row("Shield", b.Shield) + row("Knockback", b.Knockback) + row("Morale", b.Morale === "never" ? "never breaks" : `${b.Morale} of a man's shocks`);
    if (m) h += row("Missile", `${m.Missile}: ${m.Damage || m["Blast damage"] + " blast, " + m["Blast radius"]}, every ${m.Reload}`) +
      row("Range", `${m["Max range"]} (shoots unbidden at ${m["Auto range"]}${m["Min range"] ? `, not inside ${m["Min range"]}` : ""})`);
    if (f) h += row("Flight", `${f["Air speed"]} at ${f["Cruise height"]}, ${f.Endurance} aloft`);
    if (g) h += row("Gait", `${g.Gait}: road ${g.Road}, forest ${g.Forest}, swamp ${g.Swamp}`);
    const notes = (b.Notes || "").replace(/;\s*$/, "").trim();
    if (notes) h += row("Notes", notes);
    return h + "</dl>";
  }

  // Every unit in one short table (the unit sheet's first table, cut down to fit a page).
  function roster() {
    let h = `<div class="table small"><table><thead><tr><th>Unit</th><th>Str</th><th>HP</th><th>Speed</th><th>Blow</th><th>Shield</th><th>Morale</th></tr></thead><tbody>`;
    for (const [name, u] of Object.entries(units)) {
      const b = u["Body and melee"];
      if (!b) continue;
      const blow = b["Melee dmg"] !== "0" ? `${b["Melee dmg"]} ${b.Blow} / ${b.Every.replace(" s", "s")}` : "–";
      h += `<tr><td>${MD.esc(name)}</td><td>${b.Strength}</td><td>${b.HP}</td><td>${b.Speed}</td><td>${blow}</td><td>${b.Shield || "–"}</td><td>${b.Morale}</td></tr>`;
    }
    return h + "</tbody></table></div>";
  }

  // ---- Layout -------------------------------------------------------------

  const stage = $("stage");
  let leaves = [];       // two .leaf elements, each with a flow
  let flow = null;       // the first leaf's flow: the one measured
  let stride = 0;        // one page across, in px
  let pages = 1, page = 0;
  let chapterStarts = [];// [{page, title}]
  let blocksList = [];   // the numbered blocks, for remembering where we are
  let source = "";       // the book's HTML

  function buildLeaf() {
    const leaf = document.createElement("div");
    leaf.className = "leaf";
    leaf.innerHTML = `<div class="paper"><div class="head"></div><div class="body"><div class="flow"></div></div><div class="foot"></div></div><div class="shade"></div>`;
    return leaf;
  }

  let layoutGen = 0;
  async function layout(keepBlock) {
    const my = ++layoutGen;
    stage.innerHTML = "";
    // Whole pixels: a page a fraction of a pixel off its columns drifts a little
    // more with every page turned.
    stage.style.width = stage.style.height = "";
    const box = stage.getBoundingClientRect();
    stage.style.width = `${Math.floor(box.width)}px`;
    stage.style.height = `${Math.floor(box.height)}px`;
    leaves = [buildLeaf(), buildLeaf()];
    leaves.forEach((l) => stage.appendChild(l));
    const body = leaves[0].querySelector(".body");
    const w = body.clientWidth, h = body.clientHeight;
    const gap = 40;
    stride = w + gap;
    document.documentElement.style.setProperty("--pagew", `${w}px`);
    document.documentElement.style.setProperty("--pageh", `${h}px`);
    leaves.forEach((l, k) => {
      const f = l.querySelector(".flow");
      f.style.width = `${w}px`;
      f.style.height = `${h}px`;
      f.style.columnWidth = `${w}px`;
      f.style.columnGap = `${gap}px`;
      f.innerHTML = source;
      if (k > 0) f.querySelectorAll("[data-anchor]").forEach((e) => e.removeAttribute("data-anchor"));
    });
    flow = leaves[0].querySelector(".flow");
    // Measure only once every picture in both leaves has its size.
    const imgs = [...stage.querySelectorAll("img")];
    await Promise.all(imgs.map((i) => (i.complete ? null : new Promise((res) => { i.onload = i.onerror = res; }))));
    if (my !== layoutGen) return;  // a newer layout has begun
    // Filling in the page numbers changes the text a little, so measure, fill
    // in, and measure again (a second pass settles it).
    let blockPage = [], anchorPage = {};
    for (let pass = 0; pass < 2; ++pass) {
      const fr = flow.getBoundingClientRect();
      const pageOf = (el) => Math.max(0, Math.floor((el.getBoundingClientRect().left - fr.left + 2) / stride));
      pages = pageOf(flow.querySelector(".end")) + 1;
      chapterStarts = [...flow.querySelectorAll("header.opener, section.cover")].map((e) => ({ page: pageOf(e), title: e.dataset.chapter || "", cover: e.classList.contains("cover") }));
      // Every block, numbered, with its page: for search and for keeping our place.
      blocksList = [...flow.querySelectorAll("p, li, h1, h2, h3, h4, td, th, figcaption, dt, dd")];
      blockPage = blocksList.map(pageOf);
      // Page numbers in the contents, the index and the cross references.
      anchorPage = {};
      flow.querySelectorAll("[data-anchor]").forEach((e) => { anchorPage[e.dataset.anchor] = pageOf(e) + 1; });
      const termPages = {};
      flow.querySelectorAll(".term").forEach((e) => { (termPages[e.dataset.term] = termPages[e.dataset.term] || new Set()).add(pageOf(e) + 1); });
      leaves.forEach((l) => {
        const f = l.querySelector(".flow");
        f.querySelectorAll(".pg[data-ref]").forEach((e) => { e.textContent = anchorPage[e.dataset.ref] || ""; });
        f.querySelectorAll("a.xref").forEach((e) => {
          const hd = headings.find((x) => x.id === e.dataset.ref);
          e.textContent = hd ? `${hd.number ? "§" + hd.number : hd.text}, p. ${anchorPage[hd.id] || "?"}` : "§?";
        });
        f.querySelectorAll(".ix[data-term]").forEach((e) => {
          const set = new Set([...(termPages[e.dataset.term] || [])]);
          if (e.dataset.ref && anchorPage[e.dataset.ref]) set.add(anchorPage[e.dataset.ref]);
          e.innerHTML = [...set].sort((x, y) => x - y).map((n) => `<a class="pgl" data-page="${n}" href="#">${n}</a>`).join(", ");
        });
      });
    }
    state.blockPage = blockPage;
    state.anchorPage = anchorPage;
    page = Math.min(pages - 1, keepBlock >= 0 && keepBlock < blockPage.length ? blockPage[keepBlock] : page);
    show(page);
  }

  const state = { blockPage: [], anchorPage: {} };

  function chapterAt(p) {
    let c = null;
    for (const s of chapterStarts) if (s.page <= p) c = s;
    return c;
  }

  function paint(leaf, p) {
    leaf.dataset.page = p;
    leaf.querySelector(".flow").style.transform = `translate3d(${-p * stride}px,0,0)`;
    const c = chapterAt(p);
    const opener = chapterStarts.some((s) => s.page === p);
    leaf.classList.toggle("cover-page", !!(c && c.cover && c.page === p));
    leaf.querySelector(".head").textContent = opener || !c ? "" : c.title;
    leaf.querySelector(".foot").textContent = c && c.cover && c.page === p ? "" : String(p + 1);
  }

  function firstBlockOn(p) {
    const k = state.blockPage.findIndex((x) => x >= p);
    return k;
  }

  function show(p) {
    page = Math.max(0, Math.min(pages - 1, p));
    paint(leaves[0], page);
    leaves[0].style.transform = "";
    leaves[0].style.zIndex = 2;
    leaves[1].style.zIndex = 1;
    leaves[1].style.visibility = "hidden";
    updateBar();
    store.set("block", String(firstBlockOn(page)));
  }

  function updateBar() {
    $("counter").textContent = `${page + 1} / ${pages}`;
    const c = chapterAt(page);
    $("where").textContent = c && !c.cover ? c.title : "The High Lords of Illyria";
    $("prev").disabled = page <= 0;
    $("next").disabled = page >= pages - 1;
    $("progress").style.width = `${pages > 1 ? (100 * page) / (pages - 1) : 0}%`;
  }

  // ---- Turning pages --------------------------------------------------------

  // A forward turn lifts the top leaf (this page) away about its spine to show
  // the next beneath; a backward turn brings the previous page's leaf back down
  // over this one. `t` is how far (0 to 1); the shade darkens as the leaf rises.
  const MAX_DEG = 100;
  let turn = null;  // {dir, leaf, t}
  const reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  function setLeaf(leaf, t) {
    leaf.style.transform = `perspective(${Math.max(1400, stage.clientWidth * 3)}px) rotateY(${MAX_DEG * t}deg)`;
    leaf.querySelector(".shade").style.opacity = String(Math.min(1, t * 1.6) * 0.55);
  }

  function beginTurn(dir) {
    const target = page + dir;
    if (target < 0 || target >= pages || turn) return null;
    const [top, under] = leaves;
    if (dir > 0) {
      paint(under, target);
      under.style.visibility = "visible";
      under.style.zIndex = 1;
      top.style.zIndex = 2;
      turn = { dir, leaf: top, t: 0, target };
    } else {
      // The previous page comes back on top, from lifted.
      paint(under, target);
      under.style.visibility = "visible";
      under.style.zIndex = 3;
      under.style.transition = "none";
      setLeaf(under, 1);
      turn = { dir, leaf: under, t: 1, target };
    }
    turn.leaf.style.transition = "none";
    stage.classList.add("turning");
    return turn;
  }

  function finishTurn(complete) {
    if (!turn) return;
    const tr = turn;
    const end = tr.dir > 0 ? (complete ? 1 : 0) : (complete ? 0 : 1);
    const ms = reduced ? 0 : Math.round(420 * Math.max(0.35, Math.abs(end - tr.t)));
    tr.leaf.style.transition = ms ? `transform ${ms}ms cubic-bezier(.3,.7,.3,1)` : "none";
    tr.leaf.querySelector(".shade").style.transition = ms ? `opacity ${ms}ms` : "none";
    requestAnimationFrame(() => {
      setLeaf(tr.leaf, end);
      const done = () => {
        tr.leaf.style.transition = "none";
        tr.leaf.querySelector(".shade").style.transition = "none";
        stage.classList.remove("turning");
        if (complete) leaves.reverse();  // the leaf showing the new page goes on top
        turn = null;
        const [top, under] = leaves;
        top.style.transform = "";
        top.querySelector(".shade").style.opacity = "0";
        top.style.visibility = "visible";
        under.style.transform = "";
        under.querySelector(".shade").style.opacity = "0";
        show(complete ? tr.target : page);
      };
      if (!ms) done();
      else setTimeout(done, ms + 30);
    });
  }

  function go(dir) {
    if (turn) return;
    if (!beginTurn(dir)) return;
    finishTurn(true);
  }

  function goto(p) {
    if (!Number.isFinite(p)) return;
    p = Math.max(0, Math.min(pages - 1, p));
    if (p === page || turn) return;
    if (Math.abs(p - page) === 1) return go(p - page);
    // A jump: a quick turn to the far page.
    const dir = p > page ? 1 : -1;
    const [, under] = leaves;
    if (!beginTurn(dir)) return;
    turn.target = p;
    paint(under, p);
    finishTurn(true);
  }

  // Swipes and taps on the page. A drag sideways turns the page under the
  // finger; a tap on the outer thirds turns it; a tap in the middle shows or
  // hides the bar. Links inside the page win over all of that.
  let drag = null;
  stage.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 || turn) return;
    drag = { x: e.clientX, y: e.clientY, t: e.timeStamp, id: e.pointerId, moved: false, turning: false, lastX: e.clientX, lastT: e.timeStamp, v: 0 };
  });
  stage.addEventListener("pointermove", (e) => {
    if (!drag || e.pointerId !== drag.id) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    if (!drag.moved && Math.hypot(dx, dy) > 8) {
      drag.moved = true;
      if (Math.abs(dx) > Math.abs(dy)) {
        drag.turning = !!beginTurn(dx < 0 ? 1 : -1);
        if (drag.turning) try { stage.setPointerCapture(e.pointerId); } catch (err) { /* old Safari */ }
      }
    }
    if (drag.turning && turn) {
      const w = stage.clientWidth;
      const v = (e.clientX - drag.lastX) / Math.max(1, e.timeStamp - drag.lastT);
      drag.v = drag.v * 0.6 + v * 0.4;
      drag.lastX = e.clientX;
      drag.lastT = e.timeStamp;
      // The leaf's edge under the finger: its width on screen is w cos(angle).
      const edge = turn.dir > 0 ? Math.max(0, Math.min(w, w + dx)) : Math.max(0, Math.min(w, dx));
      const ang = Math.acos(Math.max(-1, Math.min(1, edge / w))) * 180 / Math.PI;
      turn.t = Math.max(0, Math.min(1, ang / MAX_DEG));
      setLeaf(turn.leaf, turn.t);
      e.preventDefault();
    }
  });
  const endDrag = (e) => {
    if (!drag || e.pointerId !== drag.id) return;
    const d = drag;
    drag = null;
    if (d.turning && turn) {
      const flick = turn.dir > 0 ? d.v < -0.35 : d.v > 0.35;
      const back = turn.dir > 0 ? d.v > 0.35 : d.v < -0.35;
      const far = turn.dir > 0 ? turn.t > 0.3 : turn.t < 0.7;
      finishTurn(!back && (flick || far));
      return;
    }
    if (d.moved || e.type === "pointercancel") return;
    // A tap.
    const link = e.target.closest && e.target.closest("a");
    if (link) return;  // its click handler deals with it
    const w = stage.clientWidth, x = e.clientX - stage.getBoundingClientRect().left;
    if (x < w * 0.3) go(-1);
    else if (x > w * 0.7) go(1);
    else toggleBar();
  };
  stage.addEventListener("pointerup", endDrag);
  stage.addEventListener("pointercancel", endDrag);
  // iOS: keep the page from scrolling or zooming under a swipe.
  stage.addEventListener("touchmove", (e) => { if (drag && drag.turning) e.preventDefault(); }, { passive: false });

  stage.addEventListener("click", (e) => {
    const a = e.target.closest && e.target.closest("a");
    if (!a) return;
    if (a.dataset.page) { e.preventDefault(); goto(+a.dataset.page - 1); return; }
    if (a.dataset.ref) {
      e.preventDefault();
      const p = state.anchorPage[a.dataset.ref];
      if (p) { pushBack(); goto(p - 1); }
    }
  });

  // "Back" after following a reference.
  let backStack = [];
  function pushBack() {
    backStack.push(page);
    $("back").classList.remove("hidden");
  }
  $("back").addEventListener("click", () => {
    if (!backStack.length) return;
    goto(backStack.pop());
    if (!backStack.length) $("back").classList.add("hidden");
  });

  document.addEventListener("keydown", (e) => {
    if (e.target.tagName === "INPUT") { if (e.key === "Escape") closeSearch(); return; }
    switch (e.key) {
      case "ArrowRight": case "PageDown": case " ": go(1); break;
      case "ArrowLeft": case "PageUp": go(-1); break;
      case "Home": goto(0); break;
      case "End": goto(pages - 1); break;
      case "t": case "T": goto((state.anchorPage.contents || 1) - 1); break;
      case "/": openSearch(); break;
      case "Escape": closeSearch(); break;
      default: return;
    }
    e.preventDefault();
  });
  // A mouse wheel or trackpad: a page a notch.
  let wheelAt = 0;
  stage.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (e.timeStamp - wheelAt < 350) return;
    const d = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
    if (Math.abs(d) < 4) return;
    wheelAt = e.timeStamp;
    go(d > 0 ? 1 : -1);
  }, { passive: false });

  // ---- The bar: contents, search, theme, type size ------------------------------

  let barTimer = 0;
  function toggleBar(force) {
    const bar = document.body.classList.toggle("bar-on", force);
    clearTimeout(barTimer);
    if (bar) barTimer = setTimeout(() => { if (!document.body.classList.contains("searching")) document.body.classList.remove("bar-on"); }, 6000);
  }
  $("prev").addEventListener("click", () => go(-1));
  $("next").addEventListener("click", () => go(1));
  $("tocButton").addEventListener("click", () => goto((state.anchorPage.contents || 1) - 1));
  $("theme").addEventListener("click", () => {
    const dark = !document.documentElement.classList.contains("dark");
    document.documentElement.classList.toggle("dark", dark);
    store.set("theme", dark ? "dark" : "light");
  });
  const sizes = [15, 16, 17, 18, 20, 22];
  let sizeIx = 2;
  function setSize(k, relayout) {
    sizeIx = Math.max(0, Math.min(sizes.length - 1, k));
    document.documentElement.style.setProperty("--fs", `${sizes[sizeIx]}px`);
    store.set("size", String(sizeIx));
    if (relayout) layout(firstBlockOn(page));
  }
  $("smaller").addEventListener("click", () => setSize(sizeIx - 1, true));
  $("bigger").addEventListener("click", () => setSize(sizeIx + 1, true));

  function openSearch() {
    document.body.classList.add("searching", "bar-on");
    $("search").classList.remove("hidden");
    $("q").focus();
  }
  function closeSearch() {
    document.body.classList.remove("searching");
    $("search").classList.add("hidden");
  }
  $("find").addEventListener("click", () => ($("search").classList.contains("hidden") ? openSearch() : closeSearch()));
  $("q").addEventListener("input", () => {
    const q = $("q").value.trim().toLowerCase();
    const out = $("results");
    if (q.length < 2) { out.innerHTML = ""; return; }
    const hits = [];
    const seen = new Set();
    blocksList.forEach((el, k) => {
      if (hits.length >= 40 || el.closest(".toc, .index")) return;
      const text = el.textContent;
      const at = text.toLowerCase().indexOf(q);
      const p = state.blockPage[k];
      if (at < 0 || seen.has(p + "|" + text.slice(0, 30))) return;
      seen.add(p + "|" + text.slice(0, 30));
      const a = Math.max(0, at - 40);
      const snip = (a ? "…" : "") + MD.esc(text.slice(a, at)) + "<mark>" + MD.esc(text.slice(at, at + q.length)) + "</mark>" + MD.esc(text.slice(at + q.length, at + q.length + 60)) + "…";
      hits.push(`<button data-p="${p}"><span>${snip}</span><b>${p + 1}</b></button>`);
    });
    out.innerHTML = hits.length ? hits.join("") : `<div class="none">Nothing found.</div>`;
  });
  $("results").addEventListener("click", (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    pushBack();
    goto(+b.dataset.p);
    closeSearch();
  });

  // ---- Loading the book ---------------------------------------------------

  async function load() {
    const theme = store.get("theme");
    const dark = theme ? theme === "dark" : !(window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches);
    document.documentElement.classList.toggle("dark", dark);
    const s = parseInt(store.get("size") || "", 10);
    setSize(Number.isFinite(s) ? s : (window.innerWidth < 500 ? 2 : 3), false);

    const book = await (await fetch(BOOK + "book.json", { cache: "no-cache" })).json();
    try {
      unitsText = await (await fetch(BOOK + "UNITS.md", { cache: "no-cache" })).text();
      units = readUnits(unitsText);
    } catch (e) { units = {}; }
    const texts = await Promise.all(book.chapters.map((c) => fetch(BOOK + c.file, { cache: "no-cache" }).then((r) => r.text())));
    let html = "";
    book.chapters.forEach((c, k) => {
      numbered = !!c.numbered;
      html += `<div class="chapter ${numbered ? "numbered" : ""} ${MD.attr(c.class || "")}">${MD.blocks(texts[k], ctx)}</div>`;
    });
    html += `<div class="end"></div>`;
    // The contents and the index, with places for their page numbers.
    const toc = headings.filter((h) => h.level === 1 || (h.level === 2 && h.number)).map((h) =>
      `<a class="${h.level === 1 ? "c1" : "c2"}" data-ref="${h.id}" href="#${h.id}"><span class="t">${h.level === 1 && h.number ? `<span class="no">${roman(+h.number)}</span>` : h.number ? `<span class="no">${h.number}</span>` : ""}${MD.esc(h.text)}</span><span class="dots"></span><span class="pg" data-ref="${h.id}">000</span></a>`).join("");
    html = html.replace(`<nav class="toc" data-toc="1"></nav>`, `<nav class="toc">${toc}</nav>`);
    const terms = new Map();
    for (const h of headings) if (h.level >= 2 && h.level <= 3) terms.set(h.text.replace(/<[^>]+>/g, ""), h.id);
    for (const m of html.matchAll(/data-term="([^"]+)"/g)) if (!terms.has(m[1])) terms.set(m[1], "");
    const bare = (k) => k.replace(/^["'“‘(]+/, "").replace(/^(the|a|an) /i, "");
    const keys = [...terms.keys()].sort((a, b) => bare(a).localeCompare(bare(b), "en", { sensitivity: "base" }));
    let letter = "", ix = "";
    for (const k of keys) {
      const l = bare(k)[0].toUpperCase();
      if (l !== letter) { letter = l; ix += `<div class="ixl">${letter}</div>`; }
      ix += `<div class="ixe"><span>${MD.esc(k)}</span> <span class="ix" data-term="${MD.attr(k)}" data-ref="${terms.get(k)}">000</span></div>`;
    }
    html = html.replace(`<nav class="index" data-index="1"></nav>`, `<nav class="index">${ix}</nav>`);
    source = html;

    // Lay the pages out once every picture has its size.
    const probe = document.createElement("div");
    probe.innerHTML = html;
    await Promise.all([...probe.querySelectorAll("img")].map((im) => new Promise((res) => {
      const i = new Image();
      i.onload = i.onerror = res;
      i.src = im.getAttribute("src");
    })));
    if (document.fonts && document.fonts.ready) await document.fonts.ready;
    const saved = parseInt(store.get("block") || "", 10);
    await layout(Number.isFinite(saved) ? saved : -1);
    document.body.classList.remove("loading");
    toggleBar(true);
  }

  let resizeTimer = 0;
  window.addEventListener("resize", () => {
    clearTimeout(resizeTimer);
    const keep = firstBlockOn(page);
    resizeTimer = setTimeout(() => layout(keep), 200);
  });

  load().catch((err) => {
    $("loadingText").textContent = "The manual could not be opened: " + err.message;
  });

  // For tests and screenshots.
  window.manual = { go, goto, get page() { return page; }, get pages() { return pages; }, pageOf: (id) => state.anchorPage[id], setLeaf: (t) => { if (!turn) beginTurn(1); if (turn) setLeaf(turn.leaf, t); } };
})();
