// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict'
import { BrowserTextFonts } from '../web/text-fonts.js'
const resolver = Object.create(BrowserTextFonts.prototype)
resolver.faces = new Map([
 ['leland text', {family:'Leland Text', local:'test-Leland'}],
 ['musejazz text', {family:'MuseJazz Text', local:'test-Jazz'}]
])
for (const family of ['Bravura Text','MScore Text','Gootville Text','Finale Maestro Text','ScoreFont']) assert(resolver.family(family).includes('test-Leland'))
for (const family of ['Petaluma','Petaluma Text','Inkpen2','Reprise Script','Finale Broadway Text']) assert(resolver.family(family).includes('test-Jazz'))
resolver.faces.delete('musejazz text')
assert(resolver.family('Petaluma Text').includes('test-Leland'))
resolver.faces.clear()
assert(resolver.family('Leland Text').endsWith('serif'))
assert.equal(resolver.family(''), 'serif')
assert(resolver.family('Arial').endsWith('sans-serif'))
assert(resolver.family('Courier New').endsWith('monospace'))
console.log('PASS MuseScore aliases, jazz aliases, chained fallbacks, cycles, font categories')
