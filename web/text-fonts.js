// SPDX-License-Identifier: GPL-3.0-only
// Host-side text metrics contract. All distances are pixels at the supplied
// size. A different host can implement measure()/family() without changing
// MuseScore or the C++ adapter. Music symbol fonts remain in the engine.
// measure(family, px, bold, italic, text) returns ten finite numbers:
// [advance, ascent, descent, lineSpacing, xHeight, capHeight, inkX, inkY, inkW, inkH].
// family(name) returns the CSS font stack used by both Canvas and SVG.
const musicAliases = new Map([
  ['leland text', 'Bravura Text'], ['musejazz text', 'Leland Text'],
  ['bravura text', 'Leland Text'], ['mscore text', 'Leland Text'],
  ['gootville text', 'Leland Text'], ['finale maestro text', 'Leland Text'],
  ['scorefont', 'Leland Text'], ['petaluma text', 'MuseJazz Text'],
  ['finale broadway text', 'MuseJazz Text']
])
const bundled = new Map([
  ['edwin-roman.otf', ['Edwin', 'normal', 'normal']],
  ['edwin-bold.otf', ['Edwin', 'bold', 'normal']],
  ['edwin-italic.otf', ['Edwin', 'normal', 'italic']],
  ['edwin-bdita.otf', ['Edwin', 'bold', 'italic']],
  ['leland.otf', ['Leland', 'normal', 'normal']],
  ['lelandtext.otf', ['Leland Text', 'normal', 'normal']],
  ['musejazz.otf', ['MuseJazz', 'normal', 'normal']],
  ['musejazztext.otf', ['MuseJazz Text', 'normal', 'normal']],
  ['campania.otf', ['Campania', 'normal', 'normal']],
  ['mscore-bc.ttf', ['MscoreBC', 'normal', 'normal']],
  ['mscoretab.ttf', ['MScoreTabulature', 'normal', 'normal']]
])
let nextBackend = 0
const quote = name => JSON.stringify(name)
function category (name) {
  if (/mono|courier|consolas|menlo|monaco|typewriter|等幅/i.test(name)) return 'monospace'
  if (/sans|arial|helvetica|calibri|carlito|verdana|tahoma|aptos|segoe|trebuchet|meiryo|gothic|ゴシック|メイリオ/i.test(name)) return 'sans-serif'
  return 'serif'
}

export class BrowserTextFonts {
  constructor ({ language = document.documentElement.lang || navigator.language } = {}) {
    this.context = document.createElement('canvas').getContext('2d')
    if (!this.context) throw new Error('Canvas text metrics are unavailable')
    this.context.lang = language
    this.context.textAlign = 'left'
    this.context.textBaseline = 'alphabetic'
    this.context.direction = 'ltr'
    this.context.fontKerning = 'normal'
    this.prefix = `mscz-text-${nextBackend++}-`
    this.faces = new Map()
    this.fontFiles = []
    this.pending = []
    this.cache = new Map()
    this.stats = { calls: 0, measurements: 0, hits: 0, cjk: 0 }
    this.isReady = true
  }

  addResource (name, bytes) {
    const spec = bundled.get(name.split('/').pop().toLowerCase())
    if (!spec) return
    const [family, weight, style] = spec
    const local = this.prefix + family
    this.fontFiles.push({ local, weight, style, bytes })
    this.faces.set(family.toLowerCase(), { family, local })
    const face = new FontFace(local, bytes, { weight, style })
    this.isReady = false
    this.pending.push(face.load().then(loaded => document.fonts.add(loaded)))
  }

  async ready () {
    await Promise.all(this.pending)
    await document.fonts.ready
    this.cache.clear()
    this.isReady = true
  }

  // Only raster exports need a self-contained SVG. On-screen pages share the
  // FontFace registrations above and carry no font data of their own.
  svgFontCss (svg) {
    return this.fontFiles.filter(f => svg.includes(f.local)).map(f => {
      if (!f.css) {
        let binary = ''
        for (let i = 0; i < f.bytes.length; i += 32768)
          binary += String.fromCharCode(...f.bytes.subarray(i, i + 32768))
        f.css = `@font-face{font-family:${quote(f.local)};font-weight:${f.weight};font-style:${f.style};src:url(data:font/otf;base64,${btoa(binary)})}`
      }
      return f.css
    }).join('\n')
  }

  family (requested) {
    let name = requested.trim() || 'serif'
    const visited = new Set()
    while (!this.faces.has(name.toLowerCase()) && !visited.has(name.toLowerCase())) {
      visited.add(name.toLowerCase())
      const alias = musicAliases.get(name.toLowerCase()) ||
        (/^(petaluma|inkpen|reprise|finale broadway|finale jazz)(\b|[0-9])/i.test(name) ? 'MuseJazz Text' : null)
      if (!alias) break
      name = alias
    }
    const face = this.faces.get(name.toLowerCase())
    const generic = category(name)
    if (face) return `${quote(face.local)}, ${quote(face.family)}, ${generic}`
    if (['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'system-ui'].includes(name.toLowerCase())) return name.toLowerCase()
    return `${quote(name)}, ${generic}`
  }

  measure (family, pixels, bold, italic, text) {
    if (!this.isReady) throw new Error('Await prepareTextFonts() before opening a score')
    if (!(pixels > 0) || !Number.isFinite(pixels)) throw new RangeError('Invalid font size')
    ++this.stats.calls
    const stack = this.family(family)
    const key = JSON.stringify([stack, pixels, bold, italic, text])
    if (this.cache.has(key)) { ++this.stats.hits; return this.cache.get(key) }
    const ctx = this.context
    ctx.font = `${italic ? 'italic ' : ''}${bold ? 'bold ' : ''}${pixels}px ${stack}`
    const probe = ctx.measureText(text || 'Hg')
    const h = ctx.measureText('H'), x = ctx.measureText('x')
    const ascent = probe.fontBoundingBoxAscent ?? Math.max(probe.actualBoundingBoxAscent, pixels * 0.8)
    const descent = probe.fontBoundingBoxDescent ?? Math.max(probe.actualBoundingBoxDescent, pixels * 0.2)
    const left = text ? -probe.actualBoundingBoxLeft : 0
    const top = text ? -probe.actualBoundingBoxAscent : 0
    const values = [text ? probe.width : 0, ascent, descent, ascent + descent,
      x.actualBoundingBoxAscent, h.actualBoundingBoxAscent, left, top,
      text ? probe.actualBoundingBoxRight - left : 0, text ? probe.actualBoundingBoxDescent - top : 0]
    if (!values.every(Number.isFinite)) throw new Error('Browser returned invalid text metrics')
    if (this.cache.size >= 20000) this.cache.clear()
    this.cache.set(key, values)
    ++this.stats.measurements
    if (/[\u3000-\u30ff\u3400-\u9fff\uac00-\ud7af]/u.test(text)) ++this.stats.cjk
    return values
  }
}

export function textFontBackend (option = 'browser') {
  if (option === 'embedded' || option === null) return null
  if (option === 'browser') return typeof document === 'undefined' ? null : new BrowserTextFonts()
  if (option && typeof option.measure === 'function' && typeof option.family === 'function') return option
  throw new TypeError('textFonts must be browser, embedded, or a text metrics backend')
}
