// SPDX-License-Identifier: GPL-3.0-only
// Providers are async (request) => { bytes, family, format? } | null.
// Returning null delegates to the next provider. A rejected request is an error,
// not permission to silently substitute another font. bytes must be readable
// font data, not just a CSS family name. The PDF exporter subsets OTF/CFF and TTF.
export class FontResolver {
  constructor (providers = []) { this.providers = providers }

  async resolve (request, accepts = () => true) {
    for (const provider of this.providers) {
      const font = await provider(request)
      if (font && await accepts(font)) return font
    }
    throw new Error(`No export font covers ${JSON.stringify(request.text)} (${request.family})`)
  }
}

const families = value => value.split(',').map(s => s.trim().replace(/^["']|["']$/g, '').toLowerCase())

// Hosts can supply Uint8Arrays, async functions, or descriptors. A descriptor
// can distinguish bold/italic faces; no network or permission is involved.
export function embeddedFontProvider (fonts) {
  return async request => {
    const wanted = families(request.family)
    for (const [family, resource] of Object.entries(fonts)) {
      if (!wanted.includes(family.toLowerCase())) continue
      const value = typeof resource === 'function' ? await resource(request) : resource
      if (!value) continue
      return value.bytes ? { family, ...value } : { family, bytes: value }
    }
    return null
  }
}

// A host chooses the URLs and faces. Cache only successful fetches; failures
// can be retried by pressing Save PDF again. No credentials, including file://.
export function downloadFontProvider (select, fetcher = globalThis.fetch.bind(globalThis)) {
  const cache = new Map()
  return async request => {
    const font = await select(request)
    if (!font) return null
    if (!cache.has(font.url)) {
      cache.set(font.url, (async () => {
        const response = await fetcher(font.url, { mode: 'cors', credentials: 'omit' })
        if (!response.ok) throw new Error(`Could not download font (${response.status})`)
        return new Uint8Array(await response.arrayBuffer())
      })().catch(error => { cache.delete(font.url); throw error }))
    }
    return { ...font, bytes: await cache.get(font.url) }
  }
}

// Call explicitly from a user gesture in supported secure contexts. OS font
// access may ask permission. It is never invoked by the default viewer.
export async function localFontProvider () {
  if (!globalThis.queryLocalFonts) throw new Error('Local font access is unavailable in this browser')
  const fonts = await globalThis.queryLocalFonts()
  const cache = new Map()
  return async request => {
    const wanted = families(request.family)
    const face = fonts.find(f => wanted.includes(f.family.toLowerCase()) &&
      /bold/i.test(f.style) === (request.weight >= 600) &&
      /italic|oblique/i.test(f.style) === !!request.italic)
    if (!face) return null
    if (!cache.has(face.postscriptName)) cache.set(face.postscriptName, face.blob().then(async blob => new Uint8Array(await blob.arrayBuffer())))
    return { family: face.family, bytes: await cache.get(face.postscriptName) }
  }
}

// Default standalone policy: export fonts are fetched only on PDF demand.
// Pin the source revision, not a floating branch. No new font payload is bundled.
const source = 'https://cdn.jsdelivr.net/gh/1921music/mscz_webviewer@3daaa61ddc16c16fd28e2cbc13061201d99bc19c/vendor/aria/fonts'
export function defaultFontResolver () {
  return new FontResolver([
    downloadFontProvider(request => {
      const special = [
        [/musejazz text/i, 'musejazz/MuseJazzText.otf'],
        [/musejazz/i, 'musejazz/MuseJazz.otf'],
        [/leland text/i, 'leland/LelandText.otf'],
        [/leland/i, 'leland/Leland.otf'],
        [/campania/i, 'campania/Campania.otf']
      ].find(([pattern]) => pattern.test(request.family))
      if (special) return { family: request.family, format: 'otf', url: `${source}/${special[1]}` }
      if (/[^\u0020-\u024f\u2000-\u206f]/u.test(request.text)) return null
      const face = request.weight >= 600 ? (request.italic ? 'BdIta' : 'Bold') : (request.italic ? 'Italic' : 'Roman')
      return { family: 'Edwin', format: 'otf', url: `${source}/edwin/Edwin-${face}.otf` }
    }),
    downloadFontProvider(() => ({
      family: 'Noto Sans JP', format: 'otf',
      url: 'https://cdn.jsdelivr.net/gh/1921music/rostrum@b5a00adda871d441ce6274e760c0ea67f7045fb7/lib/NotoSansJP-Regular.otf'
    }))
  ])
}
