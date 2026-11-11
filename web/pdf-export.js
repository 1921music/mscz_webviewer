// SPDX-License-Identifier: GPL-3.0-only
import { defaultFontResolver } from './font-resolver.js'

// Browser bundles, fetched only after export is requested. SRI also applies
// from a file:// standalone page (crossOrigin=anonymous, public CORS assets).
const scripts = [
  ['https://cdn.jsdelivr.net/npm/pdfkit@0.17.2/js/pdfkit.standalone.js', 'sha384-ypSSdG04RynoHlwFQaS7yTuqoZbDvcJYjZrL3Eq1Ay6TgW9aWwRc8DtUnPkT9wU9'],
  ['https://cdn.jsdelivr.net/npm/svg-to-pdfkit@0.1.8/source.js', 'sha384-GIADrkVi8pR/GSCa7TmHbKOWoEZF7YeJumm3qv5RjfeqsS7cFJhF0gaIPWBnej9l']
]
const loaded = new Map()
function loadScript ([url, integrity]) {
  if (!loaded.has(url)) loaded.set(url, new Promise((resolve, reject) => {
    const script = document.createElement('script')
    script.src = url; script.integrity = integrity; script.crossOrigin = 'anonymous'
    script.onload = () => { script.remove(); resolve() }
    script.onerror = () => { script.remove(); loaded.delete(url); reject(new Error('Could not load PDF tools. Check your connection and try again.')) }
    document.head.append(script)
  }))
  return loaded.get(url)
}

async function libraries () {
  for (const script of scripts) await loadScript(script)
  if (!globalThis.PDFDocument || !globalThis.SVGtoPDF) throw new Error('PDF tools did not initialize')
  return { PDFDocument: globalThis.PDFDocument, SVGtoPDF: globalThis.SVGtoPDF }
}

function inherited (element, name, fallback) {
  for (let node = element; node?.getAttribute; node = node.parentElement) {
    if (node.hasAttribute(name)) return node.getAttribute(name)
  }
  return fallback
}
const fontKey = (family, bold, italic) => JSON.stringify([family.replace(/["']/g, '').replace(/\s+/g, ' ').trim().toLowerCase(), !!bold, !!italic])

// No access to the screen DOM or Rostrum anchors: fresh SVGs exclude playback
// highlights/cursors and preserve annotations. 72 dpi makes viewBox units PDF pt.
export async function scorePdf (score, {
  fontResolver = defaultFontResolver(), title = 'Score', onProgress = () => {}
} = {}) {
  if (!score || score.pageCount < 1) throw new Error('Open a score before exporting PDF')
  onProgress('Loading PDF tools…')
  const { PDFDocument, SVGtoPDF } = await libraries()
  const doc = new PDFDocument({ autoFirstPage: false, compress: true, info: { Title: title } })
  const chunks = []
  const complete = new Promise((resolve, reject) => {
    doc.on('data', chunk => chunks.push(chunk))
    doc.on('end', () => resolve(new Blob(chunks, { type: 'application/pdf' })))
    doc.on('error', reject)
  })
  // Attach a rejection handler now, including while waiting for font downloads.
  complete.catch(() => {})
  const registered = new Map()
  let fontNumber = 0
  try {
    for (let page = 0; page < score.pageCount; page++) {
      onProgress(`Preparing PDF page ${page + 1}/${score.pageCount}…`)
      const xml = score.pageSvg(page, { dpi: 72, symbols: 1, fonts: 0 })
      const svg = new DOMParser().parseFromString(xml, 'image/svg+xml').documentElement
      if (svg.localName !== 'svg' || svg.querySelector('parsererror')) throw new Error('Invalid score SVG')
      const [, , width, height] = svg.getAttribute('viewBox').trim().split(/[\s,]+/).map(Number)
      if (!(width > 0 && height > 0)) throw new Error('Invalid PDF page size')
      // Group all strings using a face so one resolved font must cover them all.
      const requests = new Map(), faces = new Map()
      for (const text of svg.querySelectorAll('text')) {
        const family = inherited(text, 'font-family', 'serif')
        const weightValue = inherited(text, 'font-weight', 'normal')
        const weight = weightValue === 'bold' ? 700 : Number(weightValue) || 400
        const italic = /italic|oblique/.test(inherited(text, 'font-style', 'normal'))
        const key = fontKey(family, weight >= 600, italic)
        if (!requests.has(key)) requests.set(key, { family, weight, italic, text: '', purpose: 'pdf' })
        requests.get(key).text += text.textContent
      }
      for (const [key, request] of requests) {
        onProgress(`Loading PDF fonts (page ${page + 1}/${score.pageCount})…`)
        let selected
        await fontResolver.resolve(request, font => {
          let name = registered.get(font.bytes)
          if (!name) {
            name = `export-font-${fontNumber++}`
            doc.registerFont(name, font.bytes)
            registered.set(font.bytes, name)
          }
          doc.font(name)
          const covers = [...request.text].every(ch => /[\r\n\t]/u.test(ch) || doc._font.font.hasGlyphForCodePoint(ch.codePointAt(0)))
          if (covers) selected = { name, font }
          return covers
        })
        faces.set(key, selected)
      }
      doc.addPage({ size: [width, height], margin: 0 })
      SVGtoPDF(doc, svg, 0, 0, {
        width, height, assumePt: true,
        fontCallback: (family, bold, italic, options) => {
          const face = faces.get(fontKey(family, bold, italic))
          if (!face) throw new Error(`Unresolved PDF font: ${family}`)
          // Noto's fallback face is regular. Preserve requested emphasis using
          // PDF text rendering, without turning searchable text into outlines.
          options.fauxBold = bold && face.font.family === 'Noto Sans JP'
          options.fauxItalic = italic && face.font.family === 'Noto Sans JP'
          return face.name
        },
        // Arbitrary external images/documents must never trigger export fetches.
        imageCallback: () => { throw new Error('PDF export of score images is not supported yet') },
        documentCallback: () => { throw new Error('External SVG documents are not supported') },
        warningCallback: warning => { throw new Error(`PDF conversion: ${warning}`) }
      })
      await new Promise(resolve => setTimeout(resolve, 0))
    }
    doc.end() // PDFKit embeds only used glyphs, with Unicode mapping for search.
    return await complete
  } catch (error) {
    doc.destroy()
    throw error
  }
}

export async function downloadScorePdf (score, options = {}) {
  const blob = await scorePdf(score, options)
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = (options.filename || 'score.pdf').replace(/[\\/:*?"<>|\u0000-\u001f]/g, '_')
  document.body.append(link); link.click(); link.remove()
  setTimeout(() => URL.revokeObjectURL(url), 60000)
}
