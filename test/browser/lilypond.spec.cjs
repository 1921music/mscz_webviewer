const { test, expect } = require('@playwright/test')
const path = require('node:path')
const { pathToFileURL } = require('node:url')

const web = path.resolve(process.env.MSCZ_WEB_DIR || 'build/web')
const sample = path.resolve(__dirname, '../playback.musicxml')
const simple = '\\score { \\new Staff { c1 | } }'
const tera = String.raw`{#
default_version = "Fake"
[attributes]
title = "A Tune"
composer = "A Composer"
typesetter = "A Typesetter"
[versions.Fake]
doVoice = true
doChords = true
doLyrics = true
#}
{% if part == "VoiceFake" %}{ c4 d e f | }{% endif %}
{% if part == "ChordsFake" %}\chordmode { c1 | }{% endif %}
{% if part == "LyricsFake" %}\lyricmode { one two three four }{% endif %}`
const book = String.raw`
\bookpart {
\tocItem \markup "First Song"
\markup \fill-line { \larger "First Song" }
\score { \new Staff { c1 | } }
}
\bookpart {
\tocItem \markup "Second Song"
\markup \fill-line { \larger "Second Song" }
\score { \new Staff { d1 | } }
}`

function file (name, text) {
  return { name, mimeType: 'text/plain', buffer: Buffer.from(text) }
}

async function notation (page) {
  return page.evaluate(() => {
    const score = window.__mscz.score
    const pages = Array.from({ length: score.pageCount }, (_, i) => score.pageSvg(i)
      .replace(/(data-(?:object|note-id)="note_)\d+/g, '$1'))
    return { fingerprint: score.layoutFingerprint(), pages }
  })
}

for (const offline of [false, true]) {
  test('LilyPond import and native MSCZ rendering over ' + (offline ? 'file://' : 'HTTP'), async ({ page, context }) => {
    const errors = []
    let converterRequests = 0
    page.on('pageerror', e => errors.push(e.message))
    await context.route('**/*', route => {
      const url = route.request().url()
      if (url.endsWith('/lilypond.wasm')) converterRequests++
      if (/^(data:|blob:|file:)/.test(url) || (!offline && url.startsWith('http://127.0.0.1:8766/'))) return route.continue()
      errors.push(url)
      return route.abort()
    })
    if (offline) await context.setOffline(true)
    await page.goto(offline ? pathToFileURL(path.join(web, 'index-standalone.html')).href : 'http://127.0.0.1:8766/')
    const picker = page.locator('#file')
    const status = page.locator('#status')
    await expect(picker).toBeEnabled({ timeout: 45000 })
    await picker.setInputFiles(sample)
    await expect(page.locator('#pages svg').first()).toBeVisible({ timeout: 30000 })
    await expect(picker).toBeEnabled()
    const mscz = await page.evaluate(() => Array.from(window.__mscz.score.saveRostrum({})))
    const nativeFile = file('native.mscz', mscz)
    await picker.setInputFiles(nativeFile)
    await expect(status).toContainText('drawn in')
    const before = await notation(page)
    expect(converterRequests).toBe(0)

    await picker.setInputFiles(file('score.ly', simple))
    await expect(status).toContainText('drawn in')
    expect((await notation(page)).fingerprint).not.toEqual(before.fingerprint)

    await picker.setInputFiles(file('bad.ly', '\\score {}'))
    await expect(status).toContainText('No music staff or chords')
    await expect(picker).toBeEnabled()

    await page.evaluate(text => {
      const drop = new DataTransfer()
      drop.items.add(new File([text], 'song.ly.tera'))
      document.dispatchEvent(new DragEvent('drop', { dataTransfer: drop, bubbles: true, cancelable: true }))
    }, tera)
    await expect(page.locator('#pages')).toContainText('A Tune')

    await picker.setInputFiles(file('book.ly', book))
    const dialog = page.locator('#lilypondBookDialog')
    await expect(dialog).toBeVisible()
    await dialog.locator('select').selectOption({ label: 'Second Song' })
    await dialog.getByRole('button', { name: 'Open' }).click()
    await expect(page.locator('#pages')).toContainText('Second Song')
    await expect(picker).toBeEnabled()

    await picker.setInputFiles(nativeFile)
    await expect(status).toContainText('drawn in')
    expect(await notation(page)).toEqual(before)
    expect(converterRequests).toBe(offline ? 0 : 1)
    expect(errors).toEqual([])
  })
}
