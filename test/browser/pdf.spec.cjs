const { test, expect } = require('@playwright/test')
const fs = require('node:fs'), path = require('node:path')
const { pathToFileURL } = require('node:url')
const root = path.resolve(__dirname, '../..')
const web = path.resolve(process.env.MSCZ_WEB_DIR || 'build/web')
const font = path.join(root, 'test/fonts/NotoSansJP-test.otf')
const pdfkit = 'https://cdn.jsdelivr.net/npm/pdfkit@0.17.2/js/pdfkit.standalone.js'
const converter = 'https://cdn.jsdelivr.net/npm/svg-to-pdfkit@0.1.8/source.js'
async function network (context, { failFont = false } = {}) {
  const seen = [], unexpected = []
  await context.route('**/*', async route => {
    const url = route.request().url()
    if (/^(data:|blob:|file:)/.test(url) || url.startsWith('http://127.0.0.1:8766/')) return route.continue()
    seen.push(url)
    let file, type = 'application/javascript'
    if (url === pdfkit) file = path.join(root, 'node_modules/pdfkit/js/pdfkit.standalone.js')
    else if (url === converter) file = path.join(root, 'node_modules/svg-to-pdfkit/source.js')
    else if (url.endsWith('/lib/NotoSansJP-Regular.otf')) {
      if (failFont) return route.fulfill({ status: 503, headers: { 'Access-Control-Allow-Origin': '*' } })
      file = font; type = 'application/octet-stream'
    } else if (url.startsWith('https://cdn.jsdelivr.net/gh/1921music/mscz_webviewer@3daaa61ddc16c16fd28e2cbc13061201d99bc19c/vendor/aria/fonts/')) {
      file = path.join(root, 'vendor/aria/fonts', url.split('/vendor/aria/fonts/')[1]); type = 'application/octet-stream'
    } else { unexpected.push(url); return route.abort() }
    return route.fulfill({ body: fs.readFileSync(file), contentType: type, headers: { 'Access-Control-Allow-Origin': '*' } })
  })
  return { seen, unexpected }
}
async function open (page, standalone, testInfo) {
  let url = 'http://127.0.0.1:8766/'
  if (standalone) {
    const html = testInfo.outputPath('isolated/index.html')
    fs.mkdirSync(path.dirname(html), { recursive: true })
    fs.copyFileSync(path.join(web, 'index-standalone.html'), html)
    url = pathToFileURL(html).href
  }
  await page.goto(url)
  await expect(page.locator('#file')).toBeEnabled({ timeout: 45000 })
  await expect(page.locator('#savePdf')).toBeDisabled()
  await page.locator('#file').setInputFiles(path.join(root, 'test/pdf.musicxml'))
  await expect(page.locator('#savePdf')).toBeEnabled()
}

for (const standalone of [false, true]) {
  test(`PDF: ${standalone ? 'file standalone' : 'HTTP'} searchable Japanese, subsets, dimensions, lazy loading`, async ({ page, context }, testInfo) => {
    const { seen, unexpected } = await network(context)
    await open(page, standalone, testInfo)
    expect(seen).toEqual([])
    await page.locator('#transposeUp').click()
    await expect(page.locator('#savePdf')).toBeEnabled()
    const before = await page.evaluate(() => ({
      svg: window.__mscz.score.pageSvg(0, { dpi: 72 }),
      pages: window.__mscz.score.pageCount,
      transpose: window.__mscz.score.viewTransposeSemitones
    }))
    const downloaded = page.waitForEvent('download')
    await page.locator('#savePdf').click()
    // Surface a conversion error without waiting for the download timeout.
    await expect(page.locator('#status')).toHaveText('PDF saved (searchable text, embedded font subsets)', { timeout: 45000 })
    const download = await downloaded
    expect(download.suggestedFilename()).toBe('pdf.pdf')
    const output = testInfo.outputPath('score.pdf')
    await download.saveAs(output)
    const pdfjs = await import('pdfjs-dist/legacy/build/pdf.mjs')
    const document = await pdfjs.getDocument({ data: new Uint8Array(fs.readFileSync(output)), useSystemFonts: false }).promise
    expect(document.numPages).toBe(before.pages)
    expect(document.numPages).toBeGreaterThan(1)
    let text = '', embedded = 0
    for (let pn = 1; pn <= document.numPages; pn++) {
      const pg = await document.getPage(pn)
      expect(pg.view[2]).toBeCloseTo(595, -1)
      const content = await pg.getTextContent()
      text += content.items.map(item => item.str).join('')
      const operators = await pg.getOperatorList()
      expect(operators.fnArray).toContain(pdfjs.OPS.showText)
      expect(operators.fnArray).not.toContain(pdfjs.OPS.paintImageXObject)
      for (const item of content.items) {
        const f = pg.commonObjs.get(item.fontName)
        expect(f.missingFile).toBe(false)
        embedded++
      }
    }
    expect(text).toContain('日本語の歌 PDF')
    expect(text).toContain('歌詞')
    expect(text).toContain('Dolce')
    expect(embedded).toBeGreaterThan(0)
    // PDFKit's subset names and font streams, plus a small output even though
    // the source font contains hundreds of unused glyphs.
    const raw = fs.readFileSync(output).toString('latin1')
    expect(raw).toMatch(/\/BaseFont \/[A-Z]{6}\+/)
    expect(raw).toContain('/FontFile3')
    expect(raw).toContain('/ToUnicode')
    expect(fs.statSync(output).size).toBeLessThan(fs.statSync(font).size)
    await document.destroy()
    expect(await page.evaluate(() => ({ svg: window.__mscz.score.pageSvg(0, { dpi: 72 }), pages: window.__mscz.score.pageCount, transpose: window.__mscz.score.viewTransposeSemitones }))).toEqual(before)
    const count = seen.length
    const again = page.waitForEvent('download')
    await page.locator('#savePdf').click()
    await again
    expect(seen.length).toBe(count)
    expect(unexpected).toEqual([])
  })
}

test('PDF: failed download can retry and host font provider avoids font fetches', async ({ page, context }, testInfo) => {
  const first = await network(context, { failFont: true })
  await open(page, false, testInfo)
  await page.locator('#savePdf').click()
  await expect(page.locator('#status')).toContainText('Could not download font (503)')
  await expect(page.locator('#savePdf')).toBeEnabled()
  expect(first.unexpected).toEqual([])
  await context.unroute('**/*')
  const second = await network(context)
  const download = page.waitForEvent('download')
  await page.locator('#savePdf').click()
  await download
  expect(second.seen.some(url => url.endsWith('/lib/NotoSansJP-Regular.otf'))).toBe(true)
  const bytes = [...fs.readFileSync(font)]
  const count = second.seen.length
  const blob = await page.evaluate(async bytes => {
    const { FontResolver, embeddedFontProvider } = await import('/font-resolver.js')
    const { scorePdf } = await import('/pdf-export.js')
    const font = new Uint8Array(bytes)
    const fontResolver = new FontResolver([async request => ({ family: 'Host Noto', bytes: font })])
    const result = await scorePdf(window.__mscz.score, { fontResolver })
    // Also exercise Rostrum's map-of-lazy-font-bytes contract.
    const supplied = await embeddedFontProvider({ 'Noto Sans JP': () => font })({ family: '"Noto Sans JP", sans-serif' })
    return { size: result.size, supplied: supplied.bytes.length }
  }, bytes)
  expect(blob.size).toBeGreaterThan(1000)
  expect(blob.supplied).toBe(bytes.length)
  expect(second.seen.length).toBe(count)
  expect(second.unexpected).toEqual([])
})

test('PDF: missing glyphs fail without silent text loss; providers delegate', async ({ page, context }, testInfo) => {
  const { unexpected } = await network(context)
  await open(page, false, testInfo)
  const result = await page.evaluate(async bytes => {
    const { FontResolver } = await import('/font-resolver.js')
    const { scorePdf } = await import('/pdf-export.js')
    const calls = []
    const resolver = new FontResolver([
      async () => { calls.push('unavailable'); return null },
      async () => { calls.push('host'); return { family: 'Host', bytes: new Uint8Array(bytes) } }
    ])
    let message
    try {
      await scorePdf({ pageCount: 1, pageSvg: () => '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 400"><text font-family="Host">&#x10ffff;</text></svg>' }, { fontResolver: resolver })
    } catch (error) { message = error.message }
    return { calls, message }
  }, [...fs.readFileSync(font)])
  expect(result.calls).toEqual(['unavailable', 'host'])
  expect(result.message).toContain('No export font covers')
  expect(unexpected).toEqual([])
})

for (const standalone of [false, true]) {
  test(`PDF: ${standalone ? 'standalone' : 'HTTP'} Japanese title does not replace chord fonts on page one`, async ({ page, context }, testInfo) => {
    const { unexpected } = await network(context)
    await open(page, standalone, testInfo)
    // The real score uses MuseJazz Text for both the Japanese title and split
    // chord runs. Only the title needs Noto; fallback must not affect siblings
    // or change the chord face between pages. Never copy a user's score here.
    const bytes = await page.evaluate(async () => {
      const module = [...document.querySelectorAll('script[type=module]')].map(s => s.textContent).find(s => s.includes('downloadScorePdf'))
      const url = module.match(/import \{ downloadScorePdf \} from '([^']+)'/)[1]
      const { scorePdf } = await import(url)
      const score = { pageCount: 2, pageSvg: pn => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 400"><g font-family="MuseJazz Text" font-size="18">${pn === 0 ? '<text x="20" y="30">日本語</text>' : ''}<text x="20" y="70">F</text><text x="32" y="70">♯</text><text x="44" y="70">m7</text></g></svg>` }
      return [...new Uint8Array(await (await scorePdf(score)).arrayBuffer())]
    })
    const pdfjs = await import('pdfjs-dist/legacy/build/pdf.mjs')
    const document = await pdfjs.getDocument({ data: new Uint8Array(bytes), useSystemFonts: false }).promise
    const chordFaces = []
    for (let pn = 1; pn <= 2; pn++) {
      const pg = await document.getPage(pn), content = await pg.getTextContent({ disableCombineTextItems: true })
      await pg.getOperatorList()
      const chord = content.items.find(item => item.str.includes('F'))
      expect(chord).toBeDefined()
      const font = pg.commonObjs.get(chord.fontName)
      expect(font.missingFile).toBe(false)
      expect(font.name).toContain('MuseJazz')
      chordFaces.push(font.name)
      if (pn === 1) {
        const title = content.items.find(item => item.str.includes('日本語'))
        expect(pg.commonObjs.get(title.fontName).name).toContain('NotoSansJP')
      }
    }
    expect(chordFaces[0]).toBe(chordFaces[1])
    await document.destroy()
    expect(unexpected).toEqual([])
  })
}
