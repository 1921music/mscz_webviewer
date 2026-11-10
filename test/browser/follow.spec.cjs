const { test, expect } = require('@playwright/test')
const fs = require('node:fs'), path = require('node:path')
const { pathToFileURL } = require('node:url')
const root = path.resolve(__dirname, '../..')
const web = path.resolve(process.env.MSCZ_WEB_DIR || 'build/web')
for (const standalone of [false, true]) {
  test(`tracking and system scrolling: ${standalone ? 'offline' : 'HTTP'}`, async ({ page, context }, testInfo) => {
    test.skip(process.env.MSCZ_PLAYBACK === 'OFF')
    const errors = [], external = []
    page.on('pageerror', e => errors.push(e.message))
    await context.route('**/*', route => {
      const url = route.request().url()
      if (/^(data:|blob:|file:)/.test(url) || (!standalone && url.startsWith('http://127.0.0.1:8766/'))) return route.continue()
      external.push(url); return route.abort()
    })
    let url = 'http://127.0.0.1:8766/'
    if (standalone) {
      const file = testInfo.outputPath('offline/index.html'); fs.mkdirSync(path.dirname(file), { recursive: true })
      fs.copyFileSync(path.join(web, 'index-standalone.html'), file); url = pathToFileURL(file).href
      await context.setOffline(true)
    }
    await page.setViewportSize({ width: 900, height: 480 })
    await page.goto(url)
    await expect(page.locator('#file')).toBeEnabled({ timeout: 45000 })
    const xml = fs.readFileSync(path.join(root, 'test/playback.musicxml'), 'utf8')
      .replaceAll('<measure number="2">', '<measure number="2"><print new-system="yes"/>')
    await page.locator('#file').setInputFiles({ name: 'systems.musicxml', mimeType: 'application/xml', buffer: Buffer.from(xml) })
    await expect(page.locator('#play')).toBeEnabled()
    await page.locator('#zoomIn').click(); await page.locator('#zoomIn').click()
    // Freeze the transport at precise score times to verify boundaries, tied
    // continuations, repeat returns, rest-only progress and paused redraws.
    const sample = time => page.evaluate(time => {
      const player = window.__mscz.playback
      player.context = { currentTime: time }; player.startTime = 0; player.state = 'playing'; player.notify()
      return { ...player.tracking, scroll: scrollY }
    }, time)
    const first = await sample(0.1)
    expect(first.noteIds).toHaveLength(2)
    const tiedStart = await sample(1.6)
    const next = await sample(2)
    expect(next.system.id).not.toBe(first.system.id)
    expect(next.noteIds.some(id => tiedStart.noteIds.includes(id))).toBe(false)
    expect(next.scroll).toBeGreaterThan(first.scroll + 20)
    expect(await page.locator('.mscz-playing').count()).toBe(next.noteIds.length)
    const red = await page.locator('.mscz-playing text,.mscz-playing use,.mscz-playing path').first().evaluate(n => getComputedStyle(n).fill)
    expect(red).toBe('rgb(208, 0, 0)')
    await page.screenshot({ path: path.join(root, 'build', `playback-system-${standalone ? 'offline' : 'http'}.png`) })
    const rest = await sample(4.1)
    expect(rest.noteIds).toHaveLength(1) // piano rests; flute sustains
    expect(rest.system.id).toBe(next.system.id)
    const repeated = await sample(6)
    expect(repeated.noteIds).toEqual(first.noteIds)
    expect(repeated.scroll).toBeLessThan(next.scroll - 20)
    await page.locator('#followPlayback').uncheck()
    const unfollowed = await sample(8.1)
    expect(unfollowed.scroll).toBe(repeated.scroll)
    await page.evaluate(() => { window.__mscz.playback.state = 'paused' })
    await page.locator('#pathSymbols').uncheck()
    expect(await page.locator('.mscz-playing').count()).toBe(unfollowed.noteIds.length)
    await page.evaluate(() => { const p = window.__mscz.playback; p.stop(); p.context = null })
    await expect(page.locator('.mscz-playing')).toHaveCount(0)
    // Grace-note intervals come from the synth renderer rather than zero
    // notation durations, and still refer to real SVG noteheads.
    const grace = xml.replace('<note><pitch><step>D</step>', '<note><grace slash="yes"/><pitch><step>B</step><octave>3</octave></pitch><type>eighth</type></note><note><pitch><step>D</step>')
    await page.locator('#file').setInputFiles({ name: 'grace.musicxml', mimeType: 'application/xml', buffer: Buffer.from(grace) })
    await expect.poll(() => page.evaluate(() => window.__mscz.playback.timeline.visual.notes.length)).toBeGreaterThan(14)
    const validGrace = await page.evaluate(() => {
      const t = window.__mscz.playback.timeline
      const ids = new Set([...document.querySelectorAll('[data-note-id]')].map(g => g.dataset.noteId))
      return t.visual.notes.filter(n => !ids.has(n.id) || n.end <= n.start).map(n => ({ ...n, present: ids.has(n.id) }))
    })
    expect(validGrace).toEqual([])
    await page.setViewportSize({ width: 390, height: 844 })
    await page.locator('#followPlayback').check()
    await sample(0.6)
    await page.screenshot({ path: path.join(root, 'build', `playback-phone-${standalone ? 'offline' : 'http'}.png`) })
    await page.evaluate(() => { const p = window.__mscz.playback; p.stop(); p.context = null })
    expect(errors).toEqual([]); expect(external).toEqual([])
  })
}
