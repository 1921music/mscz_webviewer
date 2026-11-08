// SPDX-License-Identifier: GPL-3.0-only
import fs from 'node:fs'
import path from 'node:path'
import assert from 'node:assert/strict'
import { pathToFileURL } from 'node:url'
const web = path.resolve(process.argv[2])
const { load } = await import(pathToFileURL(path.join(web, 'mscz.js')))
const index = JSON.parse(fs.readFileSync(path.join(web, 'index.json')))
const resources = index.resources.map(r => ({ name: r.name, bytes: new Uint8Array(fs.readFileSync(path.join(web, r.url))) }))
const mscz = await load({ wasmBinary: new Uint8Array(fs.readFileSync(path.join(web, 'mscz-engine.wasm'))), resources, textFonts: 'embedded' })
for (const file of [path.resolve('test/playback.musicxml'), ...process.argv.slice(3)]) {
  const score = mscz.open(new Uint8Array(fs.readFileSync(file)), path.basename(file))
  const before = score.layoutFingerprint()
  const data = score.playbackTimeline()
  assert.equal(score.layoutFingerprint(), before, 'MIDI rendering must not change layout')
  assert.deepEqual(score.playbackTimeline(), data, 'MIDI rendering must be repeatable')
  const notes = data.events.filter(e => (e[2] & 0xf0) === 0x90 && e[4] > 0)
  assert(notes.length > 0)
  assert(data.events.every((e, i) => Number.isFinite(e[0]) && (!i || e[0] >= data.events[i - 1][0])))
  if (file.endsWith('playback.musicxml')) {
    assert(Math.abs(data.duration - 12) < 0.01, JSON.stringify(data))
    assert.equal(notes.filter(e => e[1] === 0).length, 8)
    assert.equal(data.parts.length, 3)
    assert.equal(data.parts[2].kind, 'harmony')
    assert.equal(data.parts[2].name, 'Piano "A": Chords')
    const chords = notes.filter(e => e[1] === 2)
    assert(chords.length >= 12, 'chord symbols must produce independent MIDI notes')
    assert(chords.some(e => Math.abs(e[0] - 6) < 0.01), 'chords must follow repeats')
    const pitch = notes.find(e => e[1] === 0)[3]
    score.setViewTransposeSemitones(2)
    const transposed = score.playbackTimeline().events.filter(e => (e[2] & 0xf0) === 0x90 && e[4] > 0)
    assert.equal(transposed.find(e => e[1] === 0)[3], pitch + 2)
    // MuseScore may revoice a chord into another octave after transposition.
    const pitchClasses = events => events.map(e => [e[0], e[3] % 12]).sort((a, b) => a[0] - b[0] || a[1] - b[1])
    assert.deepEqual(pitchClasses(transposed.filter(e => e[1] === 2)), pitchClasses(chords.map(e => [e[0], e[1], e[2], e[3] + 2])))
    score.clearViewTranspose()
    assert.deepEqual(score.playbackTimeline(), data)
  }
  console.log(JSON.stringify({ file: path.basename(file), parts: data.parts, duration: data.duration, events: data.events.length, notes: notes.length }))
  score.close()
}
