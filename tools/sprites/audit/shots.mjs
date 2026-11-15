// Screenshots of the sprite audit page and the numbers behind it, from headless Chrome.
//   tools/sprites/audit/serve.sh &
//   NODE_PATH="$(npm root -g)/@mermaid-js/mermaid-cli/node_modules" node tools/sprites/audit/shots.mjs OUT_DIR [unit ...]
// Writes OUT_DIR/grid.png, summary.png, <unit>.png and <unit>-clips.png for each unit named (default: giants,
// dwarf_grenadier), and report.json. Exits 1 on a page error.
import { createRequire } from "node:module";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const require = createRequire(import.meta.url);
const puppeteer = require("puppeteer");
const out = process.argv[2] || "sprite-audit";
const units = process.argv.slice(3).length ? process.argv.slice(3) : ["giants", "dwarf_grenadier"];
const url = process.env.AUDIT_URL || "http://localhost:8765/tools/sprites/audit/";
mkdirSync(out, { recursive: true });

const browser = await puppeteer.launch({ headless: "new", args: ["--enable-unsafe-swiftshader", "--ignore-gpu-blocklist", "--enable-webgl"] });
const page = await browser.newPage();
const errors = [];
page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
page.on("console", (m) => { if (m.type() === "error" || m.type() === "warning") errors.push(m.type() + ": " + m.text()); });
page.on("requestfailed", (r) => { if (!r.url().endsWith("favicon.ico")) errors.push("failed: " + r.url()); });
await page.setViewport({ width: 1680, height: 1050, deviceScaleFactor: 1 });
const settle = async () => {
  await page.waitForFunction(() => window.__audit && document.querySelector("#status").textContent.indexOf("loading") < 0, { timeout: 180000 });
  await new Promise((r) => setTimeout(r, 700));
};
const shot = (name) => page.screenshot({ path: join(out, name) });

await page.goto(url, { waitUntil: "networkidle0" });
await page.evaluate(() => window.__audit.ready);
await page.evaluate(() => window.__audit.checkAll());
await settle();
await shot("summary.png");
await page.evaluate(() => document.querySelector("#grid").scrollIntoView());
await settle();
await shot("grid.png");
await page.evaluate(() => window.scrollBy(0, 900));
await settle();
await shot("grid-2.png");
for (const u of units) {
  await page.evaluate((u) => { location.hash = "#u=" + u; }, u);
  await settle();
  await shot(u + ".png");
  await page.evaluate(() => [...document.querySelectorAll("#detail h2")].find((h) => h.textContent === "Clips").scrollIntoView());
  await page.evaluate(() => window.scrollBy(0, -90));
  await settle();
  await shot(u + "-clips.png");
  await page.evaluate(() => [...document.querySelectorAll("#detail h2")].find((h) => h.textContent === "Body parts").scrollIntoView());
  await page.evaluate(() => window.scrollBy(0, -90));
  await settle();
  await shot(u + "-parts.png");
}
await page.evaluate(() => { location.hash = ""; });
const report = await page.evaluate(() => window.__audit.report());
writeFileSync(join(out, "report.json"), JSON.stringify(report, null, 1));
await browser.close();
for (const e of errors) console.error(e);
console.log(`${report.length} units; ${errors.length} errors; shots in ${out}`);
process.exit(errors.length ? 1 : 0);
