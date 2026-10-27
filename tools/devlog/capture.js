// Films a build of the game for the devlog: a set of screenshots at every zoom
// level plus a short video flying from god view down into the fighting.
//
//   node tools/devlog/capture.js <site-url> <output-dir>
//
// Uses the page's test hooks when the build has them (older builds lack some;
// those shots are skipped). Runs in headless Chromium with software WebGL, so
// frame rates in the captures say nothing about real devices.

const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [url, outDir] = process.argv.slice(2);
if (!url || !outDir) {
  console.error('usage: node capture.js <site-url> <output-dir>');
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });

const launchArgs = ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'];

async function open(browser, contextOptions) {
  const context = await browser.newContext(contextOptions);
  const page = await context.newPage();
  const log = [];
  page.on('console', (m) => log.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', (e) => log.push(`pageerror: ${e.message}`));
  await page.goto(url);
  await page.waitForFunction(() => window.illyria !== undefined || window.Module?.calledRun, null, { timeout: 60000 });
  await page.waitForTimeout(1500);
  // The opening hint covers the battlefield; captures are about the game.
  await page.evaluate(() => { const h = document.getElementById('hint'); if (h) h.style.display = 'none'; });
  return { context, page, log };
}

const has = (page, fn) => page.evaluate((fn) => typeof Module[`_${fn}`] === 'function', fn);
const call = (page, fn, ...args) => page.evaluate(([fn, args]) => Module[`_${fn}`](...args), [fn, args]);

(async () => {
  const browser = await chromium.launch({ args: launchArgs });
  const notes = [];

  // --- Screenshots (desktop) ---
  {
    const { context, page, log } = await open(browser, { viewport: { width: 1280, height: 720 } });
    const shot = async (name) => { await page.waitForTimeout(700); await page.screenshot({ path: path.join(outDir, `${name}.png`) }); };
    await shot('01-opening');
    const canCamera = await has(page, 'illyria_set_camera');
    const canForward = await has(page, 'illyria_fast_forward');
    const canHot = await has(page, 'illyria_hot_spot');
    if (canCamera) {
      await call(page, 'illyria_set_camera', 512, 512, 1700, 0.3); await shot('02-god-view');
      await call(page, 'illyria_set_camera', 500, 512, 420, 0.3); await shot('03-strategic');
    }
    if (canForward) {
      await call(page, 'illyria_fast_forward', 45);
      if (canHot) await call(page, 'illyria_hot_spot');
      else if (canCamera) await call(page, 'illyria_set_camera', 520, 512, 70, 0.3);
      await shot('04-battle');
      await call(page, 'illyria_fast_forward', 20);
      if (canHot) await call(page, 'illyria_hot_spot');
      await shot('05-battle-later');
      if (canCamera) {
        const hud = await page.evaluate(() => (window.illyria && window.illyria.hud ? JSON.parse(window.illyria.hud()) : null));
        if (hud) notes.push(`hud after ~65 s: tick ${hud.tick}, sim ${hud.stepMs} ms/tick`);
      }
      await call(page, 'illyria_fast_forward', 60);
      if (canCamera) { await call(page, 'illyria_set_camera', 512, 512, 380, 0.5); await shot('06-aftermath'); }
    }
    notes.push(...log.map((l) => `desktop ${l}`));
    await context.close();
  }

  // --- Screenshot (phone, landscape) ---
  {
    const { context, page, log } = await open(browser, { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
    await page.screenshot({ path: path.join(outDir, '07-phone.png') });
    notes.push(...log.map((l) => `phone ${l}`));
    await context.close();
  }

  // --- Video: god view, swoop down into the fighting ---
  {
    const videoDir = path.join(outDir, '.video');
    const { context, page, log } = await open(browser, {
      viewport: { width: 960, height: 540 },
      recordVideo: { dir: videoDir, size: { width: 960, height: 540 } },
    });
    if (await has(page, 'illyria_set_camera')) {
      if (await has(page, 'illyria_fast_forward')) await call(page, 'illyria_fast_forward', 30);
      const zoomable = await has(page, 'illyria_wheel');
      if (await has(page, 'illyria_hot_spot')) await call(page, 'illyria_hot_spot');
      else await call(page, 'illyria_set_camera', 520, 512, 70, 0.3);
      if (zoomable) {
        // Pull straight up to the gods, hold, then dive back down into the fighting
        // (zooming about the screen centre keeps the battle in frame).
        const cx = 480, cy = 270;
        await call(page, 'illyria_wheel', 6000, cx, cy);
        await page.waitForTimeout(1200);
        for (let i = 0; i < 70; i++) {
          await call(page, 'illyria_wheel', -60, cx, cy);
          await page.waitForTimeout(140);
        }
      }
      if (await has(page, 'illyria_hot_spot')) await call(page, 'illyria_hot_spot');
      await page.waitForTimeout(5000);
    } else {
      await page.waitForTimeout(8000);
    }
    notes.push(...log.map((l) => `video ${l}`));
    const video = page.video();
    await context.close();
    if (video) {
      fs.renameSync(await video.path(), path.join(outDir, 'flythrough.webm'));
      fs.rmSync(videoDir, { recursive: true, force: true });
    }
  }

  await browser.close();
  fs.writeFileSync(path.join(outDir, 'capture-log.txt'), notes.join('\n') + '\n');
  console.log(`captured into ${outDir}`);
})().catch((e) => { console.error(e); process.exit(1); });
