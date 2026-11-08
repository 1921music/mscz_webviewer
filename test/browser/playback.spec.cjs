const { test, expect } = require('@playwright/test')
const path = require('node:path'), fs = require('node:fs')
const { pathToFileURL } = require('node:url')
const root = path.resolve(__dirname, '../..')
const web = path.resolve(process.env.MSCZ_WEB_DIR || 'build/web')
const backend = process.env.MSCZ_PLAYBACK || 'EAS'
for (const standalone of [false, true]) {
  test(`${backend}: ${standalone ? 'offline single HTML' : 'HTTP'} playback, mixer, lifecycle`, async ({ page, context }, testInfo) => {
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
    if (backend === 'OFF') {
      await expect(page.locator('#playback')).toHaveCount(0)
      expect(fs.existsSync(path.join(web, 'mscz-synth.wasm'))).toBe(false)
      expect(await page.evaluate(() => typeof window.__mscz.mscz.m._mscz_playback_timeline)).toBe('undefined')
    } else {
      await expect(page.locator('#play')).toBeEnabled()
      await expect(page.locator('#mixerRows tr')).toHaveCount(3)
      const timeline = await page.evaluate(() => window.__mscz.score.playbackTimeline())
      expect(timeline.parts[0].name).toBe('Piano "A"')
      expect(timeline.parts[2].name).toBe('Piano "A"：コード')
      expect(timeline.parts[2].kind).toBe('harmony')
      expect(timeline.duration).toBeCloseTo(12, 1)
      const notes = timeline.events.filter(e => e[1] === 0 && (e[2] & 0xf0) === 0x90 && e[4] > 0)
      expect(notes.length).toBe(8) // tied continuation is not re-struck; repeat is expanded
      expect(notes[0][3]).toBe(60)
      expect(notes[4][0]).toBeCloseTo(6, 1)
      // Stop must cancel a pending AudioContext resume before it creates a worker.
      expect(await page.evaluate(async () => {
        const p = window.__mscz.playback
        const started = p.play(); p.stop(); await started
        return { state: p.state, worker: p.worker }
      })).toEqual({ state: 'stopped', worker: null })
      await page.locator('#play').click()
      await expect.poll(() => page.evaluate(() => ({ state: window.__mscz.playback.state, error: window.__mscz.playback.error })), { timeout: 30000 }).toEqual({ state: 'playing', error: '' })
      await page.locator('#play').click()
      await expect(page.locator('#play')).toHaveText('再生')
      const position = await page.evaluate(() => window.__mscz.playback.position)
      await page.waitForTimeout(150)
      expect(await page.evaluate(() => window.__mscz.playback.position)).toBeCloseTo(position, 3)
      await page.locator('#play').click()
      await expect(page.locator('#play')).toHaveText('一時停止')
      await page.locator('#stop').click()
      // Check actual PCM, not just checkbox appearance.
      const pcm = await page.evaluate(async () => {
        const p = window.__mscz.playback
        await p.ensureWorker()
        const energy = x => x.left.reduce((n, v) => n + v * v, 0) + x.right.reduce((n, v) => n + v * v, 0)
        const render = async mix => {
          await p.request('mix', mix); await p.request('reset')
          return p.request('render')
        }
        const mix = p.mixer.map(x => ({ ...x }))
        const normal = energy(await render(mix))
        const chords = energy(await render(mix.map((x, i) => ({ ...x, solo: i === 2 }))))
        const chordsMuted = energy(await render(mix.map((x, i) => ({ ...x, solo: i === 2, mute: i === 2 }))))
        const instruments = energy(await render(mix.map((x, i) => ({ ...x, mute: i === 2 }))))
        const muted = energy(await render(mix.map(x => ({ ...x, mute: true }))))
        const soloMuted = energy(await render(mix.map((x, i) => ({ ...x, solo: i === 0, mute: i === 0 }))))
        const zeroVolume = energy(await render(mix.map(x => ({ ...x, volume: 0 }))))
        const left = await render(mix.map((x, i) => ({ ...x, pan: 0, solo: i === 0 })))
        const leftEnergy = left.left.reduce((n, v) => n + v * v, 0), rightEnergy = left.right.reduce((n, v) => n + v * v, 0)
        p.stop()
        return { normal, chords, chordsMuted, instruments, muted, soloMuted, zeroVolume, leftEnergy, rightEnergy }
      })
      expect(pcm.normal).toBeGreaterThan(0)
      expect(pcm.chords).toBeGreaterThan(0); expect(pcm.chordsMuted).toBe(0)
      expect(pcm.instruments).toBeGreaterThan(0)
      expect(pcm.muted).toBe(0); expect(pcm.soloMuted).toBe(0); expect(pcm.zeroVolume).toBe(0)
      expect(pcm.leftEnergy).toBeGreaterThan(pcm.rightEnergy * 10)
      await page.locator('details summary').first().click()
      await page.locator('[data-part="0"][data-control="solo"]').check()
      await page.locator('[data-part="1"][data-control="solo"]').check()
      await page.locator('[data-part="2"][data-control="solo"]').check()
      fs.mkdirSync(path.join(root, 'build/out'), { recursive: true })
      await page.screenshot({ path: path.join(root, `build/out/playback-${backend}-${standalone ? 'offline' : 'http'}.png`) })
      expect(await page.evaluate(() => window.__mscz.playback.mixer.every(p => p.solo))).toBe(true)
      await page.locator('#transposeUp').click()
      await expect(page.locator('#transposeValue')).toHaveText('+1 半音')
      expect(await page.evaluate(() => window.__mscz.score.playbackTimeline().events.find(e => e[1] === 0 && (e[2] & 0xf0) === 0x90 && e[4] > 0)[3])).toBe(61)
      expect(await page.evaluate(() => window.__mscz.playback.mixer.every(p => p.solo))).toBe(true)
      await page.locator('#play').click()
      await expect(page.locator('#play')).toHaveText('一時停止')
      await page.locator('#file').setInputFiles(path.join(root, 'test/playback.musicxml'))
      await expect(page.locator('#play')).toHaveText('再生')
      await expect.poll(() => page.evaluate(() => window.__mscz.playback.mixer.some(p => p.solo))).toBe(false)
      if (process.env.MSCZ_TEST_SCORE) {
        await page.locator('#file').setInputFiles(process.env.MSCZ_TEST_SCORE)
        await expect(page.locator('#play')).toBeEnabled()
        await expect.poll(() => page.evaluate(() => window.__mscz.score.playbackTimeline().events.length)).toBeGreaterThan(1000)
        await page.locator('#play').click()
        await expect(page.locator('#play')).toHaveText('一時停止')
        await page.waitForTimeout(300)
        await page.locator('#stop').click()
      }
      await expect(page.locator('#playbackStatus')).toHaveText('')
    }
    expect(errors).toEqual([]); expect(external).toEqual([])
  })
}
