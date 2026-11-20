// SPDX-License-Identifier: GPL-3.0-only
export async function loadLilyPond (binary) {
  if (!binary) {
    const response = await fetch(new URL('./lilypond.wasm', import.meta.url))
    if (!response.ok) throw new Error('Could not load the LilyPond converter')
    binary = await response.arrayBuffer()
  }
  const { instance } = await WebAssembly.instantiate(await binary)
  const { memory, lilypond_input, lilypond_convert } = instance.exports
  const decoder = new TextDecoder()
  return (source, { tera = false, measureNumbers } = {}) => {
    const numbering = measureNumbers === 'system' ? 1 : measureNumbers === 'none' ? 2 : 0
    const input = lilypond_input(source.byteLength)
    new Uint8Array(memory.buffer, input, source.byteLength).set(source)
    const output = lilypond_convert(Number(tera), numbering)
    const header = new DataView(memory.buffer, output, 5)
    const length = header.getUint32(0, true)
    const bytes = new Uint8Array(memory.buffer, output + 5, length - 5)
    if (header.getUint8(4)) throw new Error(decoder.decode(bytes))
    return bytes.slice()
  }
}

// Book and entry boundaries match the native CLI and the OpenBook source
// exporter. Single scores go straight to the converter.
export function openBookEntries (source) {
  const parts = [...source.matchAll(/^[ \t]*\\bookpart[ \t]*\{/gm)].map(m => m.index)
  if (parts.length <= 1) return []
  parts.push(source.length)
  const entries = []
  let part = 0
  for (const toc of source.matchAll(/^\\tocItem\s+\\markup\s+"((?:[^"\\]|\\.)*)"/gm)) {
    while (parts[part + 1] <= toc.index) ++part
    if (parts[part] <= toc.index) {
      const start = parts[part]
      const end = parts[part + 1]
      entries.push({ title: toc[1], start, end })
    }
  }
  return entries
}
