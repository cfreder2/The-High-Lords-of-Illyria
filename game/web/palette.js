// The player's colour: a side's banner, dots, bars and flags are cosmetic, so the choice lives here, on the device
// (localStorage), and is handed to the engine before a match begins (illyria_set_side_colour). The engine
// (game/src/palette.cpp) decides what is drawn; this file draws the menus' picker before the engine has loaded, so
// its swatches and its check are the engine's, and tests/test_palette.cpp compares the table.
(function () {
  "use strict";
  const key = "illyria.colour.v1";
  const kApart = 25, kApartLand = 20;
  // swatches: the eight lords' own colours in the continent's order, then four more (game/src/palette.cpp)
  const swatches = [
    ["White", "#f2f2ea"], ["Gold", "#eabf26"], ["Red", "#cc261e"], ["Orange", "#ea7a1e"], ["Green", "#28993d"], ["Brown", "#99663d"],
    ["Blue", "#335bd1"], ["Black", "#191919"], ["Purple", "#7f38b2"], ["Pink", "#f47fb7"], ["Teal", "#199e9e"], ["Maroon", "#721933"],
  ];
  const land = ["#577838", "#8c804d", "#664d33", "#26598c", "#807a73", "#0d2e59"];
  const seats = [6, 2, 1, 4, 5, 3, 0, 7];  // a skirmish's teams: Blue, Red, Gold, Green ...
  const rgb = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
  const lin = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
  function lab(h) {
    const [r, g, b] = rgb(h).map(lin);
    const x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047, y = 0.2126 * r + 0.7152 * g + 0.0722 * b, z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    const f = (t) => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
    return [116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))];
  }
  const distance = (a, b) => Math.hypot(...lab(a).map((v, i) => v - lab(b)[i]));
  const same = (a, b) => a.toLowerCase() === b.toLowerCase();
  const valid = (h) => typeof h === "string" && /^#[0-9a-fA-F]{6}$/.test(h);

  // What a colour becomes for the lord led: the swatch itself; a colour apart from the other lords and from the land;
  // else the nearest swatch.
  function snap(h, faction) {
    h = h.toLowerCase();
    const hit = swatches.find((s) => same(s[1], h));
    if (hit) return hit[1];
    const apart = swatches.every((s, i) => i === faction || distance(h, s[1]) >= kApart) && land.every((l) => distance(h, l) >= kApartLand);
    if (apart) return h;
    let best = swatches[0][1], bestD = Infinity;
    for (const s of swatches) { const d = distance(h, s[1]); if (d < bestD) { bestD = d; best = s[1]; } }
    return best;
  }
  // Each lord's colour once `faction` is led in `choice` (null: everyone's own): the engine's swap.
  function table(faction, choice) {
    const t = swatches.slice(0, 8).map((s) => s[1]);
    if (!choice) return t;
    const mine = snap(choice, faction), gone = t[faction];
    for (let g = 0; g < 8; g++) if (g !== faction && distance(t[g], mine) < kApart) t[g] = gone;
    t[faction] = mine;
    return t;
  }

  function read() { try { const v = localStorage.getItem(key); return valid(v) ? v.toLowerCase() : null; } catch (e) { return null; } }
  function write(h) { try { if (h) localStorage.setItem(key, h); else localStorage.removeItem(key); } catch (e) { /* kept for this visit only */ } }
  let choice = read();

  const P = (window.IllyriaPalette = {
    swatches, snap, table, distance, valid,
    get choice() { return choice; },
    set(h) { choice = h && valid(h) ? h.toLowerCase() : null; write(choice); document.dispatchEvent(new CustomEvent("illyria-colour")); },
    // The colour of a team (0 Blue .. 3 Green) in a skirmish, where the player leads team 0's seat.
    team(i, led) { return table(seats[led == null ? 0 : led], choice)[seats[i & 3]]; },
    // Hand the choice to the engine (before a match begins); `api` is the page's wrapper.
    apply(api) { try { if (api && api.setSideColour) api.setSideColour(choice || ""); } catch (e) { /* cosmetic */ } },
    // The picker: the swatches and a colour of your own, for the lord `faction` (the player). `onChange(hex|null)`.
    // `lords`: the lords' names in the continent's order, for the note on who gives up a colour.
    picker(parent, faction, onChange, lords) {
      const wrap = document.createElement("div");
      wrap.className = "colour-pick";
      const shown = () => (choice ? snap(choice, faction) : null);
      let said = "";  // what the last pick of a colour of your own came to
      const pick = (h, own) => {
        const got = h && !same(h, own) ? snap(h, faction) : null;
        said = h && got && !same(got, h.toLowerCase()) ? "That was too close to another lord or to the land: the nearest clear colour is used." : "";
        P.set(got);
        draw();
        if (onChange) onChange(choice);
      };
      const draw = () => {
        const now = shown(), own = swatches[faction][1];
        const others = table(faction, choice);
        wrap.innerHTML = "";
        const row = document.createElement("div");
        row.className = "swatches";
        row.setAttribute("role", "radiogroup");
        row.setAttribute("aria-label", "Your colour");
        const make = (name, hex, on, pick) => {
          const b = document.createElement("button");
          b.type = "button";
          b.className = "swatch" + (on ? " on" : "");
          b.style.setProperty("--swatch", hex);
          b.title = name;
          b.setAttribute("role", "radio");
          b.setAttribute("aria-checked", on ? "true" : "false");
          b.setAttribute("aria-label", name);
          b.onclick = pick;
          return b;
        };
        swatches.forEach(([name, hex]) => {
          const b = make(name + (same(hex, own) ? " (your lord's own)" : ""), hex, now ? same(now, hex) : same(own, hex), () => { said = ""; pick(hex, own); });
          row.appendChild(b);
        });
        wrap.appendChild(row);
        const own2 = document.createElement("label");
        own2.className = "own";
        const input = document.createElement("input");
        input.type = "color";
        input.value = now || own;
        input.setAttribute("aria-label", "A colour of your own");
        input.onchange = () => pick(input.value, own);
        const note = document.createElement("small");
        note.textContent = "Or your own:";
        own2.append(note, input);
        wrap.appendChild(own2);
        const out = document.createElement("small");
        out.className = "colour-note";
        if (said) out.textContent = said;
        else if (choice) {
          const g = others.findIndex((c, i) => i !== faction && !same(c, swatches[i][1]));  // a lord that took the colour you gave up
          const name = lords && lords[g] ? lords[g] : "Another lord";
          out.textContent = g >= 0 ? `${name} is now ${swatches[faction][0].toLowerCase()}, the colour you gave up. Only you see the change.` : "Only you see the change: the lords keep their own colours.";
        } else out.textContent = "Your lord's own colour. Only you see the change.";
        wrap.appendChild(out);
      };
      draw();
      parent.appendChild(wrap);
      return { redraw: draw, set faction(f) { faction = f; draw(); } };
    },
  });
})();
