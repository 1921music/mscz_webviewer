// Runs the wasm build under node and holds it to the native build's baseline.
//
// This is Phase 3's verification. A browser would be the truer target, but
// this distro has no Chromium for playwright to drive, and the part that could
// differ is the .wasm, not the loader: emscripten's libc, 32-bit pointers, the
// trimmed FreeType module set and HB_TINY are all exercised either way. The
// browser is covered separately by web/index.html.
//
// Resources are read off disk rather than fetched, which is exactly the
// substitution the push-based resource API exists to allow -- the engine is
// handed bytes and never learns where they came from.
//
//   <emsdk>/node/.../bin/node scripts/check-wasm-node.js \
//       --web ~/build/mscz-wasm/web --score <path>.mscz [--baseline <file>]

import fs from 'node:fs'
import path from 'node:path'

function arg (name, fallback) {
  const i = process.argv.indexOf(`--${name}`)
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback
}

const webDir = arg('web')
const scorePath = arg('score')
const baselinePath = arg('baseline')
const groups = (arg('groups', 'core,optional')).split(',')

if (!webDir || !scorePath) {
  console.error('need --web <dir> --score <path>.mscz')
  process.exit(2)
}

const { create } = await import(path.join(webDir, 'mscz.js'))

const t0 = performance.now()
const mscz = await create()

const index = JSON.parse(fs.readFileSync(path.join(webDir, 'index.json'), 'utf8'))
for (const r of index.resources) {
  if (groups.includes(r.group)) {
    mscz.addResource(r.name, new Uint8Array(fs.readFileSync(path.join(webDir, r.url))))
  }
}
mscz.init()
console.log(`engine ready in ${Math.round(performance.now() - t0)} ms`
  + `, ${(mscz.resourceBytes / 1024).toFixed(0)} KB of resources`)

const t1 = performance.now()
const score = mscz.open(new Uint8Array(fs.readFileSync(scorePath)), path.basename(scorePath))
console.log(`opened and laid out in ${Math.round(performance.now() - t1)} ms`
  + `, ${score.pageCount} page(s)`)

const outDir = path.join(import.meta.dirname, '..', 'build', 'out', 'wasm')
fs.mkdirSync(outDir, { recursive: true })
const stem = path.basename(scorePath, '.mscz')

const t2 = performance.now()
let svgBytes = 0
for (let i = 0; i < score.pageCount; ++i) {
  const svg = score.pageSvg(i)
  svgBytes += svg.length
  if (i === 0) {
    fs.writeFileSync(path.join(outDir, `${stem}-page-001.svg`), svg)
  }
}
console.log(`rendered ${(svgBytes / 1024).toFixed(0)} KB of SVG`
  + ` in ${Math.round(performance.now() - t2)} ms`)

const fingerprint = score.layoutFingerprint()
const fpPath = path.join(outDir, `${stem}.wasm.fingerprint`)
fs.writeFileSync(fpPath, fingerprint)

const got = fingerprint.trimEnd().split('\n')
console.log(`fingerprint: ${got.length} elements -> ${fpPath}`)

if (baselinePath) {
  const want = fs.readFileSync(baselinePath, 'utf8').trimEnd().split('\n')
  let moved = 0
  const examples = []
  for (let i = 0; i < Math.max(want.length, got.length); ++i) {
    if (want[i] !== got[i]) {
      ++moved
      if (examples.length < 4) { examples.push(`  - ${want[i]}\n  + ${got[i]}`) }
    }
  }
  const pct = want.length ? Math.round((moved * 100) / want.length) : 0
  console.log(`against ${path.basename(baselinePath)}: ${want.length} baseline,`
    + ` ${got.length} now, ${moved} differing (${pct}%)`)
  for (const e of examples) { console.log(e) }
  process.exitCode = moved === 0 ? 0 : 1
}
