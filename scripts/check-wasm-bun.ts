// SPDX-License-Identifier: GPL-3.0-only
// Browser E2E using Bun.WebView and an installed Edge; no npm dependencies.
// C:\Tools\bun.exe scripts/check-wasm-bun.ts [--web DIR] [--samples DIR]
import assert from 'node:assert/strict'
import { existsSync } from 'node:fs'
import { readdir, mkdir, mkdtemp, readFile, copyFile } from 'node:fs/promises'
import path from 'node:path'
import { pathToFileURL } from 'node:url'

const repo = path.resolve(import.meta.dir, '..')
function option(name: string, fallback: string) {
  const i = process.argv.indexOf(`--${name}`)
  return i < 0 ? fallback : process.argv[i + 1]
}
const web = path.resolve(option('web', path.join(repo, 'build/web')))
const standalone = option('standalone', '')
const textFonts = option('text-fonts', 'browser')
const samples = path.resolve(option('samples', path.join(repo, '../sample')))
const out = path.join(repo, 'build/out', (standalone ? 'bun-standalone-e2e' : 'bun-e2e') + (textFonts === 'embedded' ? '-embedded' : ''))
const edge = option('browser', process.env.BUN_CHROME_PATH || [
  path.join(process.env['ProgramFiles(x86)'] || '', 'Microsoft/Edge/Application/msedge.exe'),
  path.join(process.env.ProgramFiles || '', 'Microsoft/Edge/Application/msedge.exe'),
].find(existsSync) || '')
assert(existsSync(edge), 'Edge not found; pass --browser PATH')
assert(existsSync(standalone || path.join(web, 'mscz-engine.wasm')), `Build wasm first; missing ${standalone || web}`)
const scores = (await readdir(samples)).filter(f => /\.(mscz|musicxml|xml|mxl)$/i.test(f)).sort()
assert(scores.length > 0, `No supported scores in ${samples}`)
await mkdir(out, { recursive: true })

// Prove that the one HTML works in an otherwise empty folder, offline. Do
// not start an HTTP server for this mode and do not relax browser security.
let standaloneUrl = ''
if (standalone) {
  const isolated = await mkdtemp(path.join(out, 'isolated-'))
  const html = path.join(isolated, 'index.html')
  await copyFile(standalone, html)
  standaloneUrl = pathToFileURL(html).href + `?textFonts=${encodeURIComponent(textFonts)}`
}
const server = standalone ? null : Bun.serve({
  hostname: '127.0.0.1',
  port: 0,
  async fetch(req) {
    let name: string
    try { name = decodeURIComponent(new URL(req.url).pathname) } catch { return new Response(null, { status: 400 }) }
    const isSample = name.startsWith('/samples/')
    const root = isSample ? samples : web
    const file = path.resolve(root, '.' + (isSample ? name.slice('/samples'.length) : name === '/' ? '/index.html' : name))
    if (!file.startsWith(root + path.sep)) return new Response(null, { status: 403 })
    const data = Bun.file(file)
    if (!await data.exists()) return new Response(null, { status: 404 })
    return new Response(data)
  },
})
const errors: string[] = []
// The Windows pipe backend can exit immediately even when Edge starts fine.
// Use a dedicated headless process and its ephemeral loopback CDP port.
const profile = await mkdtemp(path.join(out, 'edge-profile-'))
const browser = Bun.spawn([edge, '--headless=new', '--disable-gpu', '--no-first-run',
  '--no-default-browser-check', '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'],
  { stdout: 'ignore', stderr: 'ignore' })
let endpoint = ''
for (let i = 0; i < 100; ++i) {
  try {
    const [port, route] = (await readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).trim().split(/\r?\n/)
    endpoint = `ws://127.0.0.1:${port}${route}`
    break
  } catch { await Bun.sleep(100) }
}
if (!endpoint) {
  browser.kill()
  server?.stop(true)
  throw new Error('Edge did not open its DevTools port within 10 seconds')
}
const view = new Bun.WebView({
  backend: { type: 'chrome', url: endpoint },
  width: 1100,
  height: 1400,
  console: (type, ...args) => {
    if (type !== 'error') return
    const message = args.map(String).join(' ')
    errors.push(message)
  },
})
// Also bound startup failures, which can otherwise leave an unresolved CDP call.
const watchdog = setTimeout(() => {
  console.error('E2E exceeded 300 seconds')
  Bun.WebView.closeAll()
  browser.kill()
  server?.stop(true)
  process.exit(1)
}, 300_000)
async function waitFor(expression: string) {
  const until = Date.now() + 45_000
  while (Date.now() < until) {
    if (await view.evaluate(expression)) return
    await Bun.sleep(100)
  }
  throw new Error(`Timed out: ${expression}; status: ${await view.evaluate("document.querySelector('#status')?.textContent")}`)
}
const inspect = `(() => {
  const svgs = [...document.querySelectorAll('#pages > svg')];
  const texts = [...document.querySelectorAll('#pages text')];
  const ids = [...document.querySelectorAll('#pages [id]')].map(e => e.id);
  const families = svgs.map(svg => [...svg.querySelectorAll('style')].flatMap(style =>
    [...style.textContent.matchAll(/font-family:"([^"]+)"/g)].map(m => m[1])));
  return {
    status: document.querySelector('#status').textContent,
    pages: svgs.length,
    expected: window.__mscz?.score.pageCount,
    paths: document.querySelectorAll('#pages path').length,
    notationText: texts.filter(t => /[\\uE000-\\uF8FF\\u{1D100}-\\u{1D1FF}]/u.test(t.textContent)).length,
    textRuns: texts.length,
    fonts: [...document.querySelectorAll('#pages style')].filter(t => t.textContent.includes('@font-face')).length,
    viewBox: svgs[0]?.getAttribute('viewBox'),
    invalid: document.querySelectorAll('#pages parsererror').length,
    uniqueIds: new Set(ids).size === ids.length,
    localReferences: svgs.every(svg => [...svg.querySelectorAll('*')].every(e =>
      [...e.attributes].every(a => {
        const refs = a.localName === 'href' && a.value.startsWith('#') ? [a.value.slice(1)]
          : [...a.value.matchAll(/url\\(#([^)]*)\\)/g)].map(m => m[1]);
        return refs.every(id => document.getElementById(id)?.ownerSVGElement === svg);
      }))),
    separateFonts: families.every((names, i) => names.every(name =>
      families.every((other, j) => i === j || !other.includes(name)))),
    visible: svgs[0]?.getBoundingClientRect().height > 0
  };
})()`
const results: unknown[] = []
const unexpectedRequests: string[] = []
async function chooseFile(name: string) {
  await view.evaluate("document.querySelector('#file').value = ''")
  const { root } = await view.cdp('DOM.getDocument') as any
  const { nodeId } = await view.cdp('DOM.querySelector', { nodeId: root.nodeId, selector: '#file' }) as any
  await view.cdp('DOM.setFileInputFiles', { nodeId, files: [path.join(samples, name)] })
}
try {
  if (standalone) {
    // Bun attaches its CDP session during the first navigation.
    await view.navigate('about:blank')
    await view.cdp('Network.enable')
    view.addEventListener('Network.requestWillBeSent', (event: any) => {
      const url = event.data.request.url as string
      if (url !== standaloneUrl && !url.startsWith('data:')) unexpectedRequests.push(url)
    })
    await view.cdp('Network.emulateNetworkConditions', {
      offline: true, latency: 0, downloadThroughput: 0, uploadThroughput: 0,
    })
  }
  await view.navigate(standaloneUrl || `${server!.url}index.html?textFonts=${encodeURIComponent(textFonts)}`)
  await view.cdp('Runtime.enable')
  view.addEventListener('Runtime.exceptionThrown', (event: any) => {
    errors.push(JSON.stringify(event.data.exceptionDetails))
  })
  await waitFor("document.querySelector('#status').textContent.startsWith('engine ready')")
  assert(await view.evaluate("['transposeUp', 'transposeDown', 'transposeReset'].every(id => document.getElementById(id).disabled)"), 'transpose controls enabled without a score')

  for (const name of scores) {
    if (standalone) {
      await view.navigate(standaloneUrl)
      await waitFor("document.querySelector('#status').textContent.startsWith('engine ready')")
      assert.equal(await view.evaluate('location.protocol'), 'file:', 'standalone did not use file://')
      await chooseFile(name)
    } else {
      const sampleUrl = '/samples/' + name + (/\.(mxl|musicxml|xml)$/i.test(name) ? '?download=1' : '')
      await view.navigate(`${server!.url}index.html?textFonts=${encodeURIComponent(textFonts)}&score=${encodeURIComponent(sampleUrl)}`)
    }
    await waitFor("document.querySelector('#status').textContent.includes('page(s)')")
    const state: any = await view.evaluate(inspect)
    assert(await view.evaluate("window.__mscz.score.viewTransposeSpelling === 'readable' && document.querySelector('#transposeSpelling').value === 'readable'"), `${name}: new score must default to readable mode`)
    assert(state.pages > 0 && state.pages === state.expected, `${name}: page count mismatch`)
    if (name === 'Shade Of The Cedar Tree.mscz') {
      assert.equal(state.pages, 1, 'pre-4.6 title-frame padding pushed Shade Of The Cedar Tree onto a second page')
    }
    if (name === 'Morning_Gm.mscz') {
      assert.equal(state.pages, 1, 'legacy rehearsal alignment pushed Morning_Gm onto a second page')
    }
    if (name === 'Bella_Ciao.mxl') assert.equal(state.pages, 3, 'Bella Ciao differs from reference PDF page count')
    if (name === 'Bring It On Home To Me.mxl') assert.equal(state.pages, 18, 'Bring It On Home To Me differs from reference PDF page count')
    assert(state.paths > 0 && state.visible && state.invalid === 0, `${name}: invalid or empty rendering`)
    assert(state.uniqueIds && state.localReferences && state.separateFonts, `${name}: SVG definitions leak across pages`)
    assert.equal(state.notationText, 0, `${name}: music symbols remain selectable`)
    assert(state.textRuns > 0, `${name}: ordinary words must remain selectable`)
    if (textFonts === 'browser') {
      assert(await view.evaluate('window.__mscz.mscz.textFonts.stats.calls > 0'), `${name}: browser metrics were not used`)
      const checked: any = await view.evaluate(`(() => {
        const backend = window.__mscz.mscz.textFonts;
        const families = ['Times New Roman', 'Arial', 'Courier New'].map(n => backend.family(n));
        const monoI = backend.measure('Courier New', 100, false, false, 'iiii')[0];
        const monoW = backend.measure('Courier New', 100, false, false, 'WWWW')[0];
        const nodes = [...document.querySelectorAll('#pages text')].filter(t => /[\\u3000-\\u30ff\\u3400-\\u9fff\\uac00-\\ud7af]/u.test(t.textContent));
        const ctx = document.createElement('canvas').getContext('2d'); ctx.fontKerning = 'normal';
        const widths = nodes.map(t => {
          const s = getComputedStyle(t);
          ctx.font = s.fontStyle + ' ' + s.fontWeight + ' ' + s.fontSize + ' ' + s.fontFamily;
          return { svg: t.getComputedTextLength(), canvas: ctx.measureText(t.textContent).width, family: s.fontFamily, text: t.textContent };
        });
        return {families, monoI, monoW, widths, stats: backend.stats};
      })()`)
      assert(checked.families[0].endsWith('serif') && checked.families[1].endsWith('sans-serif') && checked.families[2].endsWith('monospace'))
      assert(Math.abs(checked.monoI - checked.monoW) < 0.01, 'monospace fallback has unequal advances')
      for (const width of checked.widths) {
        assert(width.svg > 0 && Math.abs(width.svg - width.canvas) < Math.max(1, width.canvas * 0.02), `${name}: CJK SVG and browser metrics disagree: ${JSON.stringify(width)}`)
        assert(width.family.includes('serif') || width.family.includes('monospace'), `${name}: CJK font stack lost`)
      }
      if (checked.widths.length) assert(checked.stats.cjk > 0, `${name}: CJK text bypassed browser metrics`)
    }
    const baselines = option('baselines', '')
    if (baselines) {
      const expected = await Bun.file(path.join(baselines, path.basename(name, '.mscz') + '.wasm.fingerprint')).text()
      assert.equal(await view.evaluate('window.__mscz.score.layoutFingerprint()'), expected, `${name}: layout changed`)
    }
    await view.evaluate(`(() => {
      const score = window.__mscz.score;
      window.__transposeBaseline = {
        fingerprint: score.layoutFingerprint(),
        svg: Array.from({ length: score.pageCount }, (_, page) => score.pageSvg(page)).join(''),
        pages: score.pageCount
      };
      return true;
    })()`)
    if (name === 'Those Days.mscz') {
      await view.click('#transposeDown')
      await waitFor("window.__mscz.score.viewTransposeSemitones === -1 && !document.querySelector('#transposeReset').disabled")
      assert.equal(await view.evaluate('window.__mscz.score.viewTransposeSpelling'), 'readable')
      await Bun.write(path.join(out, name + '.default-minus1.png'), await view.screenshot())
      await view.click('#transposeReset')
      await waitFor("window.__mscz.score.viewTransposeSemitones === 0 && document.querySelector('#transposeReset').disabled")
    }
    // The remaining roundtrip checks explicitly start in preserve mode.
    await view.evaluate(`(() => {
      const select = document.querySelector('#transposeSpelling');
      select.value = 'preserve'; select.dispatchEvent(new Event('change', { bubbles: true }));
    })()`)
    await waitFor("window.__mscz.score.viewTransposeSpelling === 'preserve' && !document.querySelector('#transposeSpelling').disabled")
    await view.click('#transposeUp')
    await waitFor("window.__mscz.score.viewTransposeSemitones === 1 && document.querySelector('#transposeValue').textContent === '+1 半音' && !document.querySelector('#transposeReset').disabled")
    assert(await view.evaluate(`(() => {
      const score = window.__mscz.score;
      return Array.from({ length: score.pageCount }, (_, page) => score.pageSvg(page)).join('') !== window.__transposeBaseline.svg;
    })()`), `${name}: semitone up did not change the score`)
    assert.equal((await view.evaluate(inspect) as any).notationText, 0, `${name}: transposition left selectable symbols`)
    await Bun.write(path.join(out, name + '.transpose-plus1.png'), await view.screenshot())
    await view.evaluate(`(() => {
      const score = window.__mscz.score;
      window.__preserveSvg = Array.from({ length: score.pageCount }, (_, page) => score.pageSvg(page)).join('');
      const select = document.querySelector('#transposeSpelling');
      select.value = 'readable'; select.dispatchEvent(new Event('change', { bubbles: true }));
    })()`)
    await waitFor("window.__mscz.score.viewTransposeSpelling === 'readable' && !document.querySelector('#transposeSpelling').disabled")
    assert.equal((await view.evaluate(inspect) as any).notationText, 0, `${name}: readable mode left selectable symbols`)
    await Bun.write(path.join(out, name + '.readable-plus1.png'), await view.screenshot())
    await view.evaluate(`(() => {
      const select = document.querySelector('#transposeSpelling');
      select.value = 'preserve'; select.dispatchEvent(new Event('change', { bubbles: true }));
    })()`)
    await waitFor("window.__mscz.score.viewTransposeSpelling === 'preserve' && !document.querySelector('#transposeSpelling').disabled")
    assert(await view.evaluate(`(() => {
      const score = window.__mscz.score;
      return Array.from({ length: score.pageCount }, (_, page) => score.pageSvg(page)).join('') === window.__preserveSvg;
    })()`), `${name}: mode roundtrip changed original-based transposition`)
    await view.click('#transposeDown')
    await waitFor("window.__mscz.score.viewTransposeSemitones === 0 && document.querySelector('#transposeReset').disabled")
    const isOriginal = `(() => {
      const score = window.__mscz.score, before = window.__transposeBaseline;
      return score.pageCount === before.pages && score.layoutFingerprint() === before.fingerprint
        && Array.from({ length: score.pageCount }, (_, page) => score.pageSvg(page)).join('') === before.svg;
    })()`
    assert(await view.evaluate(isOriginal), `${name}: up/down did not restore the original exactly`)
    await view.click('#transposeDown')
    await waitFor("window.__mscz.score.viewTransposeSemitones === -1 && document.querySelector('#transposeValue').textContent === '-1 半音'")
    if (name === 'Those Days.mscz') {
      await view.evaluate(`(() => {
        const select = document.querySelector('#transposeSpelling');
        select.value = 'readable'; select.dispatchEvent(new Event('change', { bubbles: true }));
      })()`)
      await waitFor("window.__mscz.score.viewTransposeSpelling === 'readable' && !document.querySelector('#transposeSpelling').disabled")
      const readable: any = await view.evaluate(inspect)
      assert(readable.uniqueIds && readable.localReferences && readable.separateFonts && readable.notationText === 0)
      await Bun.write(path.join(out, name + '.readable-minus1.png'), await view.screenshot())
    }
    await view.click('#transposeReset')
    await waitFor("window.__mscz.score.viewTransposeSemitones === 0 && document.querySelector('#transposeReset').disabled")
    assert(await view.evaluate(isOriginal), `${name}: original-pitch button did not restore the score exactly`)
    assert(await view.evaluate(`(() => {
      const score = window.__mscz.score;
      for (const value of [-13, 13, 0.5, NaN, Infinity, '1']) {
        try { score.setViewTransposeSemitones(value); return false; }
        catch (error) { if (!(error instanceof RangeError)) return false; }
      }
      try { score.setViewTransposeSpelling('invalid'); return false; }
      catch (error) { if (!(error instanceof RangeError)) return false; }
      return score.mscz._setViewTranspose(score.id, 13) === 0 && score.viewTransposeSemitones === 0;
    })()`), `${name}: invalid transposition accepted`)
    assert(await view.evaluate(isOriginal), `${name}: invalid request changed the view`)
    await Bun.write(path.join(out, name + '.png'), await view.screenshot())
    if (state.pages > 1) {
      await view.evaluate("document.querySelectorAll('#pages > svg')[1].scrollIntoView()")
      await Bun.write(path.join(out, name + '.page2.png'), await view.screenshot())
      await view.evaluate('scrollTo(0, 0)')
    }
    results.push({ name, ...state, transpose: 'both spelling modes, mode roundtrip, up/down, original pitch, exact layout and all-page SVG reset, invalid input' })
    console.log(`PASS ${name}: ${state.pages} pages, transpose and exact reset, screenshots saved`)
  }

  // Exercise the actual file-input change handler, including a Windows path.
  const file = scores.find(n => n === 'Morning_Gm.mscz') || scores[0]
  await view.click('#transposeUp')
  await waitFor("window.__mscz.score.viewTransposeSemitones === 1 && !document.querySelector('#file').disabled")
  await chooseFile(file)
  await waitFor("window.__mscz.score.viewTransposeSemitones === 0 && document.querySelector('#status').textContent.includes('page(s)')")
  const initial: any = await view.evaluate(inspect)
  assert.equal(await view.evaluate('window.__mscz.score.viewTransposeSemitones'), 0, 'opening another score did not reset transposition')
  assert.equal(await view.evaluate('window.__mscz.score.viewTransposeSpelling'), 'readable', 'opening another score did not default to readable mode')
  assert.equal(await view.evaluate("document.querySelector('#transposeValue').textContent"), '原調')
  assert(initial.pages > 0)
  assert.equal(initial.notationText, 0)
  const fileFingerprint = await view.evaluate('window.__mscz.score.layoutFingerprint()')
  const pageWidth = await view.evaluate(`(() => {
    window.__zoomPage = document.querySelector('#pages > svg');
    return window.__zoomPage.getBoundingClientRect().width;
  })()`)
  await view.click('#zoomIn')
  assert.equal(await view.evaluate("document.querySelector('#zoomValue').textContent"), '125%')
  const zoomedWidth = await view.evaluate('window.__zoomPage.getBoundingClientRect().width')
  assert(Math.abs(zoomedWidth - pageWidth * 1.25) < 1, `125% width: ${zoomedWidth}, initial: ${pageWidth}`)
  assert(await view.evaluate("window.__zoomPage === document.querySelector('#pages > svg')"), 'zoom re-rendered the score')
  await view.click('#zoomOut')
  await view.click('#zoomOut')
  assert.equal(await view.evaluate("document.querySelector('#zoomValue').textContent"), '75%')
  assert(Math.abs(await view.evaluate('window.__zoomPage.getBoundingClientRect().width') - pageWidth * 0.75) < 1)
  await view.evaluate("(() => { for (let i = 0; i < 20; i++) document.querySelector('#zoomIn').click(); return true })()")
  assert(await view.evaluate("document.querySelector('#zoomValue').textContent === '400%' && document.querySelector('#zoomIn').disabled && document.documentElement.scrollWidth > document.documentElement.clientWidth"))
  await view.evaluate("(() => { for (let i = 0; i < 20; i++) document.querySelector('#zoomOut').click(); return true })()")
  assert(await view.evaluate("document.querySelector('#zoomValue').textContent === '25%' && document.querySelector('#zoomOut').disabled"))
  await view.click('#zoomReset')
  assert.equal(await view.evaluate("document.querySelector('#zoomValue').textContent"), '100%')
  assert(Math.abs(await view.evaluate('window.__zoomPage.getBoundingClientRect().width') - pageWidth) < 1)
  assert.equal(await view.evaluate('window.__mscz.score.layoutFingerprint()'), fileFingerprint, 'zoom changed score layout')
  for (const direction of [1, -1]) {
    for (let step = 1; step <= 12; ++step) {
      await view.click(direction > 0 ? '#transposeUp' : '#transposeDown')
      await waitFor(`window.__mscz.score.viewTransposeSemitones === ${direction * step} && document.querySelector('#status').textContent.includes('page(s)')`)
    }
    assert(await view.evaluate(`document.querySelector('${direction > 0 ? '#transposeUp' : '#transposeDown'}').disabled`), 'octave limit button remains enabled')
    await view.click('#transposeReset')
    await waitFor("window.__mscz.score.viewTransposeSemitones === 0 && document.querySelector('#transposeReset').disabled")
    assert.equal(await view.evaluate('window.__mscz.score.layoutFingerprint()'), fileFingerprint, 'octave reset changed layout')
  }
  await view.click('#pathSymbols')
  assert((await view.evaluate(inspect) as any).notationText > initial.notationText, 'font symbol toggle had no effect')
  await view.click('#embedFonts')
  assert.equal((await view.evaluate(inspect) as any).fonts, 0)
  await view.click('#embedFonts')
  assert((await view.evaluate(inspect) as any).fonts > 0)
  await view.click('#pathSymbols')
  assert.equal((await view.evaluate(inspect) as any).notationText, initial.notationText)
  await view.click('#dpi')
  await view.press('a', { modifiers: ['Control'] })
  await view.type('150')
  await view.press('Tab')
  const resized: any = await view.evaluate(inspect)
  assert.notEqual(resized.viewBox, initial.viewBox, 'DPI input did not change SVG dimensions')
  assert.equal(resized.pages, initial.pages)
  assert.deepEqual(errors, [], 'browser errors')
  assert.deepEqual(unexpectedRequests, [], 'standalone tried to load an external dependency')
  results.push({ interactions: 'file input resets transposition, zoom and fit width, zoom bounds, octave bounds, symbols, embedded fonts, DPI', passed: true })
  await Bun.write(path.join(out, 'results.json'), JSON.stringify({ bun: Bun.version, browser: edge,
    standalone: standaloneUrl || undefined, offline: !!standalone, unexpectedRequests, errors, results }, null, 2))
  console.log(`PASS file input, zoom, symbol/font toggles, DPI; no browser errors\nResults: ${out}`)
} finally {
  clearTimeout(watchdog)
  try { await view.cdp('Browser.close') } catch {}
  view.close()
  Bun.WebView.closeAll()
  if (browser.exitCode === null) browser.kill()
  server?.stop(true)
}
