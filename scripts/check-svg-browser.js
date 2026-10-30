// Screenshots the generated SVGs in a real Chromium, which is the only thing
// that honours @font-face -- rsvg-convert ignores it, so the rasterised
// comparisons cannot tell whether embedding worked.
//
// Run from the repo root, with NODE_PATH pointing at a tree that has playwright:
//
//   NODE_PATH=<podium>/node_modules node scripts/check-svg-browser.js
//
// Reads build/out/<mode>/page-001.svg for each mode and writes
// build/out/<mode>-browser.png beside them. Blocks every network and file
// request except the document itself, so anything that renders is carried by
// the document.

const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const OUT = path.join(__dirname, '..', 'build', 'out');
const MODES = ['font-reference', 'font-embed', 'path-reference', 'path-embed'];

(async () => {
  const browser = await chromium.launch();
  const results = [];

  for (const mode of MODES) {
    const svgPath = path.join(OUT, mode, 'page-001.svg');
    if (!fs.existsSync(svgPath)) {
      console.log(`skip ${mode}: no ${svgPath}`);
      continue;
    }
    const svg = fs.readFileSync(svgPath, 'utf8').replace(/<\?xml[^>]*\?>/, '');

    const page = await browser.newPage({ viewport: { width: 909, height: 1286 } });

    // Nothing may come from outside the document.
    await page.route('**/*', (route) =>
      route.request().url().startsWith('data:') ? route.continue() : route.abort());

    await page.setContent(
      `<body style="margin:0;background:#fff">${svg}</body>`,
      { waitUntil: 'load' });
    await page.evaluate(() => document.fonts.ready);
    await page.waitForTimeout(400);

    const shot = path.join(OUT, `${mode}-browser.png`);
    await page.screenshot({ path: shot });
    await page.close();

    results.push([mode, fs.statSync(shot).size]);
  }

  await browser.close();

  console.log('\nscreenshot sizes (bigger means more ink on the page):');
  for (const [mode, size] of results) {
    console.log(`  ${mode.padEnd(16)} ${String(size).padStart(8)} B`);
  }
})();
