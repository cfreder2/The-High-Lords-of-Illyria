// Reads sprite sheets back out of the game in a headless browser, exactly as
// the renderer holds them (baked on the GPU, or pre-rendered and loaded), for
// comparing old and new figures (tools/blender/compare.py).
//
//   python3 -m http.server 8821 -d build-wasm/site &
//   node tools/blender/grab_sheets.js 8821 out_dir 1 4 20
//
// Writes out_dir/<type>.rgba (raw RGBA, bottom row first), <type>.json (size
// and the 130 cells). Needs Playwright (npm i playwright).
const { chromium } = require('playwright');
const fs = require('fs');
const [port, out, ...types] = process.argv.slice(2);
(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 640, height: 480 } });
  page.on('console', (m) => console.log('page:', m.text()));
  await page.goto(`http://localhost:${port}/?${Date.now()}`);
  await page.waitForFunction(() => window.illyria !== undefined, null, { timeout: 300000 });
  // Give pre-rendered sheets a moment to arrive if the battle asked for them.
  await page.waitForTimeout(+(process.env.WAIT || 3000));
  fs.mkdirSync(out, { recursive: true });
  for (const t of types) {
    const got = await page.evaluate((type) => {
      const p = Module._illyria_test_sheet(type);
      if (!p) return null;
      const size = Module._illyria_test_sheet_size() >> 2;
      const w = Module.HEAP32[size], h = Module.HEAP32[size + 1];
      const c = Module._illyria_test_sheet_cells() >> 2;
      const cells = Array.from(Module.HEAPF32.subarray(c, c + 130 * 8));
      let bin = '';
      const px = Module.HEAPU8.subarray(p, p + w * h * 4);
      for (let i = 0; i < px.length; i += 0x8000) bin += String.fromCharCode.apply(null, px.subarray(i, i + 0x8000));
      return { w, h, cells, data: btoa(bin) };
    }, +t);
    if (!got) { console.log('no sheet for', t); continue; }
    fs.writeFileSync(`${out}/${t}.rgba`, Buffer.from(got.data, 'base64'));
    fs.writeFileSync(`${out}/${t}.json`, JSON.stringify({ size: [got.w, got.h], cells: got.cells }));
    console.log('sheet', t, got.w, 'x', got.h);
  }
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
