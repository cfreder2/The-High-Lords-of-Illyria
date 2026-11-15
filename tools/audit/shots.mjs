// Screenshots of the audit pages and the numbers behind them, from headless Chrome.
//   tools/audit/serve.sh &
//   NODE_PATH="$(npm root -g)/@mermaid-js/mermaid-cli/node_modules" node tools/audit/shots.mjs OUT_DIR [sprites|sounds|all] [unit ...]
// sprites: OUT_DIR/summary.png, grid.png, <unit>.png, <unit>-clips.png, <unit>-parts.png, <unit>-sounds.png for each unit
// named (default: giants, dwarf_grenadier), and report.json. sounds: hub.png, sounds.png, sounds-events.png,
// sounds-worst.png, sounds-cards.png, sounds-stress.png, sounds-voices.png. Exits 1 on a page error.
import { createRequire } from "node:module";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const require = createRequire(import.meta.url);
const puppeteer = require("puppeteer");
const out = process.argv[2] || "sprite-audit";
const what = process.argv[3] || "all";
const units = process.argv.slice(4).length ? process.argv.slice(4) : ["giants", "dwarf_grenadier"];
const base = process.env.AUDIT_URL || "http://localhost:8765/tools/audit/";
mkdirSync(out, { recursive: true });

// (headless Chrome needs the flag to make a sound without a real gesture; the pages themselves wait for a click)
const browser = await puppeteer.launch({ headless: "new", args: ["--enable-unsafe-swiftshader", "--ignore-gpu-blocklist", "--enable-webgl", "--autoplay-policy=no-user-gesture-required"] });
const page = await browser.newPage();
const errors = [];
const optional = (url) => /\/_candidates\/|\/_sound-audit\/|favicon\.ico/.test(url);  // the hub's links to folders that may not be there
page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
page.on("console", (m) => { if ((m.type() === "error" || m.type() === "warning") && !optional(m.location().url || "")) errors.push(m.type() + ": " + m.text() + " " + (m.location().url || "")); });
page.on("requestfailed", (r) => { if (!optional(r.url())) errors.push("failed: " + r.url()); });
await page.setViewport({ width: 1680, height: 1050, deviceScaleFactor: 1 });
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const shot = (name) => page.screenshot({ path: join(out, name) });
const toHeading = async (root, text, up = 100) => {
  await page.evaluate((root, text) => [...document.querySelectorAll(root + " h2")].find((h) => h.textContent.startsWith(text)).scrollIntoView(), root, text);
  await page.evaluate((up) => window.scrollBy(0, -up), up);
};

if (what === "all" || what === "sprites") {
  const settle = async () => {
    await page.waitForFunction(() => window.__audit && document.querySelector("#status").textContent.indexOf("loading") < 0, { timeout: 180000 });
    await wait(700);
  };
  // The old address must still open the page.
  await page.goto(base.replace("/tools/audit/", "/tools/sprites/audit/"), { waitUntil: "networkidle0" });
  if (!page.url().endsWith("/tools/audit/sprites.html")) errors.push("the old address did not lead to sprites.html: " + page.url());
  await page.evaluate(() => window.__audit.ready);
  await page.evaluate(() => window.__audit.checkAll());
  await settle();
  await shot("summary.png");
  await page.evaluate(() => document.querySelector("#grid").scrollIntoView());
  await settle();
  await shot("grid.png");
  for (const u of units) {
    await page.evaluate((u) => { location.hash = "#u=" + u; }, u);
    await settle();
    await shot(u + ".png");
    await toHeading("#detail", "Sounds");
    await settle();
    // The attack's sound with its clip: choose it (a real click), let two loops pass, and see that it played.
    await page.select("#sync", "game");
    await wait(2600);
    const said = await page.evaluate(() => ({ played: window.AuditSound.counters.played, state: window.AuditSound.context() && window.AuditSound.context().state }));
    if (!said.played) errors.push(`${u}: the attack clip played no sound (${JSON.stringify(said)})`);
    await shot(u + "-sounds.png");
    await page.select("#sync", "off");
    await toHeading("#detail", "Clips");
    await settle();
    await shot(u + "-clips.png");
    await toHeading("#detail", "Body parts");
    await settle();
    await shot(u + "-parts.png");
  }
  await page.evaluate(() => { location.hash = ""; });
  writeFileSync(join(out, "report.json"), JSON.stringify(await page.evaluate(() => window.__audit.report()), null, 1));
}

if (what === "all" || what === "sounds") {
  await page.goto(base, { waitUntil: "networkidle0" });
  await wait(500);
  await shot("hub.png");
  await page.goto(base + "sounds.html", { waitUntil: "networkidle0" });
  await page.evaluate(() => window.__sounds.ready);
  if (await page.evaluate(() => !!window.AuditSound.context())) errors.push("an AudioContext was made before any click");
  await wait(600);
  await shot("sounds.png");
  await toHeading("#page", "Silent");
  await wait(300);
  await shot("sounds-events.png");
  await toHeading("#page", "Sounds, worst first");
  await wait(300);
  await shot("sounds-worst.png");
  // One sound through the game's chain at the opening camera, then the stress test for a few seconds.
  await page.click("#worst tbody tr:not([hidden]) button.play");
  await wait(800);
  await toHeading("#page", "The mix under load");
  await page.click("#stress-go");
  await wait(4000);
  await shot("sounds-stress.png");
  const mix = await page.evaluate(() => ({ ...window.AuditSound.counters, state: window.AuditSound.context().state }));
  if (!mix.played) errors.push("the stress test played nothing: " + JSON.stringify(mix));
  console.log("stress after 4 s:", JSON.stringify(mix));
  await page.click("#stress-go");
  await page.evaluate(() => { location.hash = "#s=snd_gore_body_fall"; });
  await wait(1500);
  await shot("sounds-cards.png");
  await toHeading("#page", "Voices");
  await page.evaluate(() => { document.querySelector("#set-soldier").open = true; });
  await wait(500);
  await shot("sounds-voices.png");
  await page.click("#set-soldier button.kind-placeholder");
  await wait(600);
  writeFileSync(join(out, "sounds-report.json"), JSON.stringify(await page.evaluate(() => window.AuditSound.manifest.sounds.map((s) => {
    const f = window.AuditSound.flags(s);
    return { id: s.id, score: f.score, flags: f.list.map((x) => x.text) };
  }).sort((a, b) => b.score - a.score)), null, 1));
}

await browser.close();
for (const e of errors) console.error(e);
console.log(`${errors.length} errors; shots in ${out}`);
process.exit(errors.length ? 1 : 0);
