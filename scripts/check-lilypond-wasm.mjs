// SPDX-License-Identifier: GPL-3.0-only
// Usage: node scripts/check-lilypond-wasm.mjs WASM CLI OPENBOOK.LY [OpenBook src/openbook]
import { createHash } from 'node:crypto'
import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import { execFile as execFileCallback } from 'node:child_process'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { promisify } from 'node:util'
import { loadLilyPond, loadLilyPondTera, openBookEntries } from '../web/lilypond.mjs'

const execFile = promisify(execFileCallback)
const encode = new TextEncoder()
const hash = bytes => createHash('sha256').update(bytes).digest('hex')
const [wasm, cli, book, templateDir] = process.argv.slice(2)
if (!book) throw new Error('Expected WASM, native CLI and rendered openbook.ly')
const convert = await loadLilyPond(await readFile(wasm))
const source = await readFile(book, 'utf8')
const entries = openBookEntries(source)
if (!entries.length) throw new Error('Could not find any OpenBook entries')

const work = await mkdtemp(join(tmpdir(), 'check-lilypond-wasm-'))
const input = join(work, 'score.ly')
const output = join(work, 'score.musicxml')
const expected = new Map()
let failed = 0
try {
  for (const entry of entries) {
    const section = encode.encode(source.slice(entry.start, entry.end))
    try {
      await writeFile(input, section)
      await execFile(resolve(cli), [input, output, '--measure-numbers', 'system'], { timeout: 30000 })
      const xml = await readFile(output)
      const key = hash(xml)
      expected.set(key, (expected.get(key) || 0) + 1)
      const actual = convert(section, { measureNumbers: 'system' })
      if (!xml.equals(actual)) throw new Error('MusicXML byte mismatch')
    } catch (error) {
      failed++
      console.error(entry.title, error.message)
    }
  }
} finally {
  await rm(work, { recursive: true })
}
console.log('WASM .ly byte-identical:', entries.length - failed, '/', entries.length)

if (templateDir) {
  const renderTemplate = await loadLilyPondTera(await readFile(join(dirname(wasm), 'lilypond-tera.wasm')))
  const templates = (await readdir(templateDir, { recursive: true })).filter(path => path.endsWith('.ly.tera'))
  if (templates.length !== entries.length) {
    console.error('Template count:', templates.length, 'Expected:', entries.length)
    process.exitCode = 1
  }
  let matched = 0
  for (const template of templates) {
    try {
      const raw = await readFile(join(templateDir, template))
      const xml = convert(renderTemplate(raw), { measureNumbers: 'system' })
      const key = hash(xml)
      const count = expected.get(key)
      if (!count) throw new Error('No remaining byte-identical native MusicXML')
      expected.set(key, count - 1)
      matched++
    } catch (error) {
      console.error(template, error.message)
    }
  }
  if (matched !== entries.length || matched !== templates.length) process.exitCode = 1
  console.log('WASM .ly.tera byte-identical:', matched, '/', templates.length)
}
if (failed) process.exitCode = 1
