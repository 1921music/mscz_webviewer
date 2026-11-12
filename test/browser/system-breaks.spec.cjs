const { test, expect } = require('@playwright/test')
const fs = require('node:fs'), path = require('node:path')

// Many short notes make extra key-signature/accidental space affect wrapping.
const measures = Array.from({ length: 48 }, (_, i) => `<measure number="${i + 1}">${i ? '' : '<attributes><divisions>4</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>G</sign><line>2</line></clef></attributes>'}${Array.from({ length: 16 }, (_, j) => `<note><pitch><step>${['C', 'D', 'E', 'F', 'G', 'A', 'B'][j % 7]}</step><alter>${j % 3 === 0 ? 1 : 0}</alter><octave>4</octave></pitch><duration>1</duration><type>16th</type></note>`).join('')}</measure>`).join('')
const fixture = Buffer.from(`<?xml version="1.0"?><score-partwise version="4.0"><defaults><scaling><millimeters>7</millimeters><tenths>40</tenths></scaling><page-layout><page-height>1683</page-height><page-width>1190</page-width></page-layout></defaults><part-list><score-part id="P1"><part-name>Piano</part-name><score-instrument id="I1"><instrument-name>Piano</instrument-name></score-instrument><midi-instrument id="I1"><midi-channel>1</midi-channel><midi-program>1</midi-program></midi-instrument></score-part></part-list><part id="P1">${measures}</part></score-partwise>`)
const samples = [{ name: 'dense.musicxml', buffer: fixture }, ...JSON.parse(process.env.MSCZ_LAYOUT_TEST_SCORES || '[]').map(file => ({ name: path.basename(file), buffer: fs.readFileSync(file) }))]

for (const sample of samples) {
  test(`original system breaks survive transposition: ${sample.name}`, async ({ page, context }, testInfo) => {
    test.setTimeout(240000)
    const external = [], errors = []
    page.on('pageerror', error => errors.push(error.message))
    await context.route('**/*', route => {
      const url = route.request().url()
      if (/^(data:|blob:)/.test(url) || url.startsWith('http://127.0.0.1:8766/')) return route.continue()
      external.push(url); return route.abort()
    })
    await page.goto('http://127.0.0.1:8766/')
    await expect(page.locator('#file')).toBeEnabled({ timeout: 45000 })
    await page.locator('#file').setInputFiles({ name: sample.name, mimeType: 'application/octet-stream', buffer: sample.buffer })
    await expect(page.locator('#pages svg').first()).toBeVisible({ timeout: 45000 })
    await expect(page.locator('#preserveSystemBreaks')).toBeChecked()
    const result = await page.evaluate(() => {
      const score = window.__mscz.score
      const groups = () => {
        const systems = new Map()
        for (const a of score.rostrumInfo().anchors) {
          const key = `${a.page}:${a.system}`
          if (!systems.has(key)) systems.set(key, [])
          systems.get(key).push(`${a.key}:${a.staff}`)
        }
        return [...systems.values()]
      }
      const original = groups(), originalPages = score.pageCount, originalFingerprint = score.layoutFingerprint()
      const offsets = []
      for (const spelling of ['readable', 'preserve']) {
        for (let offset = -12; offset <= 12; ++offset) {
          if (!offset) continue
          score.setViewTransposeSemitones(offset, spelling, true)
          const fixed = groups(), fixedPages = score.pageCount
          score.setViewTransposeSemitones(offset, spelling, false)
          const automatic = groups(), automaticPages = score.pageCount
          score.setViewTransposeSemitones(offset, spelling, true)
          offsets.push({ offset, spelling, stable: JSON.stringify(fixed) === JSON.stringify(original), fixedPages,
            roundtrip: JSON.stringify(groups()) === JSON.stringify(fixed), automaticPages,
            automaticChanged: JSON.stringify(automatic) !== JSON.stringify(original) })
        }
      }
      score.setViewTransposeSemitones(0)
      return { originalPages, systems: original.length, offsets, exactReset: score.layoutFingerprint() === originalFingerprint }
    })
    await testInfo.attach('layout-results', { body: JSON.stringify(result, null, 2), contentType: 'application/json' })
    fs.writeFileSync(testInfo.outputPath('layout-results.json'), JSON.stringify(result, null, 2))
    expect(result.systems).toBeGreaterThan(1)
    expect(result.offsets.every(o => o.stable && o.roundtrip)).toBe(true)
    expect(result.exactReset).toBe(true)
    if (sample.name === 'dense.musicxml') expect(result.offsets.some(o => o.automaticChanged)).toBe(true)
    // Toggle in place in an already transposed score, including after reset.
    await page.locator('#transposeUp').click()
    await expect(page.locator('#transposeValue')).toHaveText('+1 semitone')
    await page.locator('#preserveSystemBreaks').uncheck()
    await expect.poll(() => page.evaluate(() => window.__mscz.score.viewPreserveSystemBreaks)).toBe(false)
    await page.locator('#transposeReset').click()
    await expect(page.locator('#preserveSystemBreaks')).not.toBeChecked()
    await page.locator('#preserveSystemBreaks').check()
    await expect.poll(() => page.evaluate(() => window.__mscz.score.viewPreserveSystemBreaks)).toBe(true)
    expect(errors).toEqual([]); expect(external).toEqual([])
  })
}
