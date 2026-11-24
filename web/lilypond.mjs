// SPDX-License-Identifier: GPL-3.0-only
async function loadWasm (filename, binary) {
  if (!binary) {
    const response = await fetch(new URL(filename, import.meta.url))
    if (!response.ok) throw new Error('Could not load ' + filename)
    binary = await response.arrayBuffer()
  }
  const { instance } = await WebAssembly.instantiate(await binary)
  return instance.exports
}

function render (wasm, operation) {
  const { memory, lilypond_input } = wasm
  const decoder = new TextDecoder()
  return (source, ...args) => {
    const input = lilypond_input(source.byteLength)
    new Uint8Array(memory.buffer, input, source.byteLength).set(source)
    const output = operation(...args)
    const header = new DataView(memory.buffer, output, 5)
    const length = header.getUint32(0, true)
    const bytes = new Uint8Array(memory.buffer, output + 5, length - 5)
    if (header.getUint8(4)) throw new Error(decoder.decode(bytes))
    return bytes.slice()
  }
}

export async function loadLilyPond (binary) {
  const wasm = await loadWasm('./lilypond.wasm', binary)
  const convert = render(wasm, wasm.lilypond_convert)
  return (source, { measureNumbers } = {}) => {
    const numbering = measureNumbers === 'system' ? 1 : measureNumbers === 'none' ? 2 : 0
    return convert(source, numbering)
  }
}

export async function loadLilyPondTera (binary) {
  const wasm = await loadWasm('./lilypond-tera.wasm', binary)
  return render(wasm, wasm.lilypond_render_template)
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
