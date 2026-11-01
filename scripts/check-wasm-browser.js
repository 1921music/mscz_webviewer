// Runs the wasm build in a real Chromium and holds it to the same baseline the
// native build is held to.
//
// This is the end-to-end check for Phase 3: the engine starts in a browser,
// fetches its resources, opens a .mscz, lays it out and draws it. If the
// layout fingerprint matches the one the native Qt-free build produces, then
// nothing about emscripten -- the trimmed FreeType module set, HB_TINY, 32-bit
// pointers, a different libc -- moved the layout.
//
// NOT YET RUN. The mscz_dev distro has no Chromium for playwright to drive and
// the download cannot be fetched here, so the automated check in CI terms is
// scripts/check-wasm.sh, which runs the same wasm under the node emsdk brings.
// What this adds over that is the browser itself -- SVG parsing, @font-face,
// text selection -- so it is worth running wherever a Chromium is available.
//
// Run from the repo root, with NODE_PATH pointing at a tree that has
// playwright, and a server already serving the payload:
//
//   python3 -m http.server 8123 --directory ~/build/mscz-wasm/web &
//   NODE_PATH=<podium>/node_modules node scripts/check-wasm-browser.js \
//       --url http://localhost:8123 --score <path>.mscz [--baseline <file>]

const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

function arg(name, fallback) {
  const i = process.argv.indexOf(`--${name}`);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

const url = arg('url', 'http://localhost:8123');
const scorePath = arg('score');
const baselinePath = arg('baseline');
const outDir = path.join(__dirname, '..', 'build', 'out', 'wasm');

if (!scorePath || !fs.existsSync(scorePath)) {
  console.error('need --score <path>.mscz');
  process.exit(2);
}

(async () => {
  fs.mkdirSync(outDir, { recursive: true });

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1000, height: 1400 } });

  page.on('console', (m) => {
    if (m.type() === 'error' || m.text().includes('ERROR')) {
      console.log(`  [console] ${m.text()}`);
    }
  });
  page.on('pageerror', (e) => console.log(`  [pageerror] ${e.message}`));

  await page.goto(`${url}/index.html`, { waitUntil: 'load' });

  // The page starts the engine in a top-level await, so by the time the status
  // line stops saying "loading" the module is up or has failed.
  await page.waitForFunction(
    () => !document.getElementById('status').textContent.startsWith('loading'),
    null, { timeout: 60000 });
  const startup = await page.textContent('#status');
  console.log(`startup: ${startup}`);
  if (!startup.includes('ready')) {
    throw new Error(`the engine did not start: ${startup}`);
  }

  // Hand the score over the way a drop would, through the file input.
  await page.setInputFiles('#file', scorePath);
  await page.waitForFunction(
    () => document.getElementById('status').textContent.includes('page'),
    null, { timeout: 120000 });
  console.log(`render:  ${await page.textContent('#status')}`);

  const pageCount = await page.evaluate(() => document.querySelectorAll('#pages svg').length);
  console.log(`drew ${pageCount} page(s) of SVG in the document`);

  const shot = path.join(outDir, 'page-001.png');
  const first = page.locator('#pages svg').first();
  await first.screenshot({ path: shot });
  console.log(`screenshot ${shot} (${fs.statSync(shot).size} B)`);

  // The fingerprint comes from the engine itself, so this compares layout
  // rather than pixels -- the same lines the native probe prints.
  const fingerprint = await page.evaluate(() => window.__mscz.score.layoutFingerprint());
  const fpPath = path.join(outDir, `${path.basename(scorePath, '.mscz')}.wasm.fingerprint`);
  fs.writeFileSync(fpPath, fingerprint);
  console.log(`fingerprint ${fpPath} (${fingerprint.trimEnd().split('\n').length} elements)`);

  await browser.close();

  if (baselinePath) {
    if (!fs.existsSync(baselinePath)) {
      console.error(`no baseline at ${baselinePath}`);
      process.exit(1);
    }
    const want = fs.readFileSync(baselinePath, 'utf8').trimEnd().split('\n');
    const got = fingerprint.trimEnd().split('\n');
    let moved = 0;
    for (let i = 0; i < Math.max(want.length, got.length); ++i) {
      if (want[i] !== got[i]) { ++moved; }
    }
    const pct = want.length ? Math.round((moved * 100) / want.length) : 0;
    console.log(`\nagainst ${path.basename(baselinePath)}:`
      + ` ${want.length} baseline, ${got.length} now, ${moved} differing (${pct}%)`);
    if (moved) {
      for (let i = 0, shown = 0; i < got.length && shown < 5; ++i) {
        if (want[i] !== got[i]) {
          console.log(`  - ${want[i]}\n  + ${got[i]}`);
          ++shown;
        }
      }
    }
    process.exit(moved === 0 ? 0 : 1);
  }
})().catch((e) => {
  console.error(e.message);
  process.exit(1);
});
