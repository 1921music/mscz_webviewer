const { test, expect } = require('@playwright/test')
const path = require('node:path'), fs = require('node:fs')
const { pathToFileURL } = require('node:url')
const root = path.resolve(__dirname, '../..')
const web = path.resolve(process.env.MSCZ_WEB_DIR || 'build/web')
for (const standalone of [false, true]) {
  test(`embedding API: ${standalone ? 'offline single HTML' : 'HTTP'} preserves viewer state`, async ({ page, context }, testInfo) => {
    const errors = [], external = []
    page.on('pageerror', e => errors.push(e.message))
    await context.route('**/*', route => {
      const u = route.request().url()
      if (/^(data:|blob:|file:)/.test(u) || (!standalone && u.startsWith('http://127.0.0.1:8766/'))) return route.continue()
      external.push(u); return route.abort()
    })
    let url = 'http://127.0.0.1:8766/'
    if (standalone) {
      const html = testInfo.outputPath('isolated/index.html')
      fs.mkdirSync(path.dirname(html), { recursive: true })
      fs.copyFileSync(path.join(web, 'index-standalone.html'), html)
      url = pathToFileURL(html).href
      await context.setOffline(true)
    }
    await page.goto(url)
    await expect(page.locator('#file')).toBeEnabled({ timeout: 45000 })
    await page.locator('#file').setInputFiles(path.join(root, 'test/playback.musicxml'))
    await expect(page.locator('#pages svg').first()).toBeVisible({ timeout: 30000 })
    const result = await page.evaluate(() => {
      const { score, mscz } = window.__mscz
      const fingerprint = score.layoutFingerprint()
      const info = score.rostrumInfo()
      const anchor = info.anchors[0]
      const settings = JSON.stringify({ version: 1, transpose: 2 })
      const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"><metadata>https://1921music.github.io/rostrum/ns</metadata><path d="M0 0L10 10"/></svg>'
      score.setViewTransposeSemitones(2)
      const transposed = score.layoutFingerprint()
      const bytes = score.saveRostrum({ settings, annotations: [{ key: anchor.key, staff: anchor.staff, svg, dx: 1, dy: 2, width: 3, height: 4 }] })
      const unchangedView = score.layoutFingerprint() === transposed && score.viewTransposeSemitones === 2
      const saved = mscz.open(bytes, 'saved.mscz')
      // The viewer opens the saved score in its original key without invoking
      // the overlay API. Only the annotation image should disappear when the
      // host explicitly requests editable overlays; notation must stay put.
      const originalKey = saved.viewTransposeSemitones
      const before = saved.layoutFingerprint()
      const restored = saved.rostrumInfo()
      const after = saved.layoutFingerprint()
      const notation = fp => fp.split('\n').filter(line => !/^\d+ Image /.test(line)).join('\n')
      const stableLayout = notation(before) === notation(after)
      const optInHiding = /^\d+ Image /m.test(before) && !/^\d+ Image /m.test(after)
      saved.close()
      score.clearViewTranspose()
      const unchangedSource = score.layoutFingerprint() === fingerprint
      return { pages: info.pages.length, anchors: info.anchors.length,
        initialImages: info.images.length, settings: restored.settings,
        images: restored.images.map(i => ({ key: i.key, staff: i.staff, svg: i.svg })),
        expected: { key: anchor.key, staff: anchor.staff, svg }, originalKey,
        unchangedView, stableLayout, optInHiding, unchangedSource }
    })
    expect(result.pages).toBeGreaterThan(0)
    expect(result.anchors).toBeGreaterThan(0)
    expect(result.initialImages).toBe(0)
    expect(JSON.parse(result.settings)).toEqual({ version: 1, transpose: 2 })
    expect(result.images).toEqual([result.expected])
    expect(result.originalKey).toBe(0)
    expect(result.unchangedView).toBe(true)
    expect(result.stableLayout).toBe(true)
    expect(result.optInHiding).toBe(true)
    expect(result.unchangedSource).toBe(true)
    expect(errors).toEqual([]); expect(external).toEqual([])
  })
}
