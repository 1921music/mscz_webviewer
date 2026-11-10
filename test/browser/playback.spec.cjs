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
    await expect(page.locator('html')).toHaveAttribute('lang', 'en')
    await expect(page.locator('#file')).toBeEnabled({ timeout: 45000 })
    await page.locator('#file').setInputFiles(path.join(root, 'test/playback.musicxml'))
    await expect(page.locator('#pages svg').first()).toBeVisible({ timeout: 30000 })
    if (backend === 'OFF') {
      await expect(page.locator('#showPlayback')).toHaveCount(0)
      await expect(page.locator('#playback')).toHaveCount(0)
      expect(fs.existsSync(path.join(web, 'mscz-synth.wasm'))).toBe(false)
      expect(await page.evaluate(() => typeof window.__mscz.mscz.m._mscz_playback_timeline)).toBe('undefined')
    } else {
      await expect(page.getByRole('checkbox', { name: 'Playback panel', exact: true })).toBeChecked()
      await expect(page.locator('#playback')).toBeVisible()
      await expect(page.locator('#play')).toBeEnabled()
      await expect(page.locator('#mixerRows tr')).toHaveCount(3)
      const timeline = await page.evaluate(() => window.__mscz.score.playbackTimeline())
      expect(timeline.parts[0].name).toBe('Piano "A"')
      expect(timeline.parts[2].name).toBe('Piano "A": Chords')
      expect(timeline.parts[2].kind).toBe('harmony')
      expect(timeline.duration).toBeCloseTo(12, 1)
      expect(timeline.visual.version).toBe(1)
      expect(timeline.visual.notes.length).toBe(14) // includes each tied continuation and repeat
      const svgIds = await page.locator('[data-note-id]').evaluateAll(nodes => nodes.map(n => n.dataset.noteId))
      expect(timeline.visual.notes.every(n => svgIds.includes(n.id))).toBe(true)
      expect(timeline.visual.systems).toHaveLength(4)
      expect(timeline.visual.systems.every(s => s.bounds.every(Number.isFinite) && s.bounds[3] > 0)).toBe(true)
      const notes = timeline.events.filter(e => e[1] === 0 && (e[2] & 0xf0) === 0x90 && e[4] > 0)
      expect(notes.length).toBe(8) // tied continuation is not re-struck; repeat is expanded
      expect(notes[0][3]).toBe(60)
      expect(notes[4][0]).toBeCloseTo(6, 1)
      const continuity = await page.evaluate(() => {
        const p = window.__mscz.playback
        p.context = { currentTime: 1.9999 }; p.startTime = 0; p.state = 'paused'
        const left = p.tracking.cursor.x
        p.context.currentTime = 2.0001
        const right = p.tracking.cursor.x
        p.stop(); p.context = null
        return Math.abs(right - left)
      })
      expect(continuity).toBeLessThan(0.001) // no jump over a barline in the same system
      // Stop must cancel a pending AudioContext resume before it creates a worker.
      expect(await page.evaluate(async () => {
        const p = window.__mscz.playback
        const started = p.play(); p.stop(); await started
        return { state: p.state, worker: p.worker }
      })).toEqual({ state: 'stopped', worker: null })
      await page.locator('#play').click()
      await expect.poll(() => page.evaluate(() => ({ state: window.__mscz.playback.state, error: window.__mscz.playback.error })), { timeout: 30000 }).toEqual({ state: 'playing', error: '' })
      await expect.poll(() => page.locator('.mscz-playing').count()).toBeGreaterThan(0)
      const motion = await page.evaluate(async () => {
        const samples = []
        for (let i = 0; i < 8; ++i) {
          await new Promise(requestAnimationFrame)
          const line = document.querySelector('[data-playback-cursor]')
          samples.push(Number(line.getAttribute('x1')))
        }
        return samples
      })
      expect(new Set(motion).size).toBeGreaterThan(4) // advances every frame, not every note
      expect(motion.at(-1)).toBeGreaterThan(motion[0])
      await page.screenshot({ path: path.join(root, 'build', `playback-${standalone ? 'offline' : 'http'}.png`) })
      await page.locator('#showPlayback').uncheck()
      await expect(page.locator('#playback')).toBeHidden()
      expect(await page.evaluate(() => getComputedStyle(document.getElementById('pages')).paddingBottom)).toBe('16px')
      expect(await page.evaluate(() => window.__mscz.playback.state)).toBe('playing')
      await page.locator('#showPlayback').check()
      await expect(page.locator('#playback')).toBeVisible()
      await page.locator('#play').click()
      await expect(page.locator('#play')).toHaveText('Play')
      const pausedIds = await page.locator('.mscz-playing').evaluateAll(nodes => nodes.map(n => n.dataset.noteId))
      expect(pausedIds.length).toBeGreaterThan(0)
      const position = await page.evaluate(() => window.__mscz.playback.position)
      await page.waitForTimeout(150)
      expect(await page.locator('.mscz-playing').evaluateAll(nodes => nodes.map(n => n.dataset.noteId))).toEqual(pausedIds)
      expect(await page.evaluate(() => window.__mscz.playback.position)).toBeCloseTo(position, 3)
      await page.locator('#play').click()
      await expect(page.locator('#play')).toHaveText('Pause')
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
      await expect(page.locator('#transposeValue')).toHaveText('+1 semitone')
      expect(await page.evaluate(() => window.__mscz.score.playbackTimeline().events.find(e => e[1] === 0 && (e[2] & 0xf0) === 0x90 && e[4] > 0)[3])).toBe(61)
      expect(await page.evaluate(() => window.__mscz.playback.mixer.every(p => p.solo))).toBe(true)
      await page.locator('#play').click()
      await expect(page.locator('#play')).toHaveText('Pause')
      await page.locator('#file').setInputFiles(path.join(root, 'test/playback.musicxml'))
      await expect(page.locator('#play')).toHaveText('Play')
      await expect.poll(() => page.evaluate(() => window.__mscz.playback.mixer.some(p => p.solo))).toBe(false)
      if (process.env.MSCZ_TEST_SCORE) {
        await page.locator('#file').setInputFiles(process.env.MSCZ_TEST_SCORE)
        await expect(page.locator('#play')).toBeEnabled()
        await expect.poll(() => page.evaluate(() => window.__mscz.score.playbackTimeline().events.length)).toBeGreaterThan(1000)
        await page.locator('#play').click()
        await expect(page.locator('#play')).toHaveText('Pause')
        await page.waitForTimeout(300)
        await page.locator('#stop').click()
      }
      await expect(page.locator('#playbackStatus')).toHaveText('')
    }
    expect(errors).toEqual([]); expect(external).toEqual([])
  })
}
