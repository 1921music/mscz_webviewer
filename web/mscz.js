// SPDX-License-Identifier: GPL-3.0-only
//
// Part of mscz_test. Built on MuseScore-derived code; see vendor/aria/VENDOR.md.

// A small wrapper over the wasm module's C entry points.
//
// The engine cannot fetch anything itself: it asks for a resource by name and
// gets bytes, synchronously, in the middle of laying a page out. So everything
// asynchronous happens here -- the resources are fetched and pushed in before
// the first render. See src/mscz_wasm.cpp for the other side.

import createMscz from './mscz-engine.js'
import { textFontBackend } from './text-fonts.js'

// Pushed before the first render. "optional" is everything a score has to ask
// for by name: alternative music fonts, tablature, figured bass.
const DEFAULT_GROUPS = ['core']

export async function load ({ base = '.', groups = DEFAULT_GROUPS, wasmBinary, resources, textFonts = 'browser', playbackAssets } = {}) {
  const mscz = await create({ wasmBinary, textFonts })
  mscz.playbackAssets = playbackAssets
  mscz.base = base
  if (resources) {
    for (const { name, bytes } of resources) { mscz.addResource(name, bytes) }
  } else {
    await mscz.loadResources(base, groups)
  }
  await mscz.prepareTextFonts()
  mscz.init()
  return mscz
}

// Starts the module without loading anything. For a caller that gets its
// resources some other way than fetch -- the node-side check reads them off
// disk -- since the engine does not care where the bytes came from.
export async function create ({ wasmBinary, textFonts = 'browser' } = {}) {
  // Embedded modules have a data: URL, which cannot resolve a relative wasm
  // URL. With bytes supplied, locateFile is only a name; no fetch occurs.
  const options = wasmBinary ? { wasmBinary, locateFile: name => name } : {}
  const backend = textFontBackend(textFonts)
  if (backend) options.textFontBackend = backend
  return new Mscz(await createMscz(options), backend)
}

export class Mscz {
  constructor (module, textFonts = null) {
    this.m = module
    this.textFonts = textFonts
    const c = (name, ret, args) => module.cwrap(name, ret, args)

    this._resourceAdd = c('mscz_resource_add', null, ['string', 'number', 'number'])
    this._init = c('mscz_init', null, [])
    this._open = c('mscz_open', 'number', ['number', 'number', 'string'])
    this._close = c('mscz_close', null, ['number'])
    this._pageCount = c('mscz_page_count', 'number', ['number'])
    this._viewTranspose = c('mscz_view_transpose_semitones', 'number', ['number'])
    this._setViewTranspose = c('mscz_set_view_transpose_semitones', 'number', ['number', 'number'])
    this._viewSpelling = c('mscz_view_transpose_spelling', 'number', ['number'])
    this._viewPreserveSystemBreaks = c('mscz_view_preserve_system_breaks', 'number', ['number'])
    this._setTranspose = c('mscz_set_view_transpose_layout', 'number', ['number', 'number', 'number', 'number'])
    this._renderSvg = c('mscz_render_page_svg', 'number', ['number', 'number', 'number', 'number', 'number'])
    this._fingerprint = c('mscz_layout_fingerprint', 'number', ['number'])
    this._resultPtr = c('mscz_result_ptr', 'number', [])
    this._resultLen = c('mscz_result_len', 'number', [])
    this._lastError = c('mscz_last_error', 'string', [])
    this._alloc = c('mscz_alloc', 'number', ['number'])
    this._free = c('mscz_free', null, ['number'])

    this.resourceBytes = 0
  }

  // Fetches the index and pushes over every resource in the named groups.
  async loadResources (base, groups) {
    const index = await (await fetch(`${base}/index.json`)).json()
    const wanted = index.resources.filter(r => groups.includes(r.group))

    await Promise.all(wanted.map(async r => {
      const bytes = new Uint8Array(await (await fetch(`${base}/${r.url}`)).arrayBuffer())
      this.addResource(r.name, bytes)
    }))
  }

  // Hands bytes over by the name the engine asks for them by. Usable after
  // init() too, which is how a font a score turns out to need can arrive late.
  addResource (name, bytes) {
    this.textFonts?.addResource?.(name, bytes)
    const ptr = this._alloc(bytes.length)
    this.m.HEAPU8.set(bytes, ptr)
    this._resourceAdd(name, ptr, bytes.length)
    this._free(ptr)
    this.resourceBytes += bytes.length
  }

  init () {
    if (this.textFonts?.isReady === false) throw new Error('Await prepareTextFonts() before init()')
    this._init()
  }

  async prepareTextFonts () {
    await this.textFonts?.ready?.()
  }

  // Opens .mscz, .musicxml/.xml or .mxl. The extension of `name` selects
  // native MuseScore or MusicXML loading; bytes never go through a filesystem.
  open (bytes, name = 'score.mscz') {
    const ptr = this._alloc(bytes.length)
    this.m.HEAPU8.set(bytes, ptr)
    const id = this._open(ptr, bytes.length, name)
    this._free(ptr)
    if (!id) {
      throw new Error(this._lastError() || 'could not open the score')
    }
    return new Score(this, id)
  }

  _result () {
    const ptr = this._resultPtr()
    const len = this._resultLen()
    return new TextDecoder().decode(this.m.HEAPU8.subarray(ptr, ptr + len))
  }
}

// Symbols as outlines in <defs> and fonts embedded as subsets: the page is
// self-contained, and selecting the lyrics does not select the notation.
export const SYMBOLS_FONT = 0
export const SYMBOLS_PATH = 1
export const FONTS_REFERENCE = 0
export const FONTS_EMBED = 1

class Score {
  constructor (mscz, id) {
    this.mscz = mscz
    this.id = id
  }

  get pageCount () {
    return this.mscz._pageCount(this.id)
  }

  rostrumInfo () {
    const fn = this.mscz.m.cwrap('mscz_rostrum_info', 'number', ['number'])
    if (!fn(this.id)) throw new Error(this.mscz._lastError() || 'Could not read score anchors')
    return JSON.parse(this.mscz._result())
  }

  saveRostrum (data) {
    const fn = this.mscz.m.cwrap('mscz_rostrum_save', 'number', ['number', 'string'])
    if (!fn(this.id, JSON.stringify(data))) throw new Error(this.mscz._lastError() || 'Could not save MSCZ')
    return this.mscz.m.HEAPU8.slice(this.mscz._resultPtr(), this.mscz._resultPtr() + this.mscz._resultLen())
  }

  // PLAYBACK_BEGIN
  playbackTimeline () {
    if (!this.id) throw new Error('the score is closed')
    const fn = this.mscz.m.cwrap('mscz_playback_timeline', 'number', ['number'])
    if (!fn(this.id)) throw new Error(this.mscz._lastError() || 'Could not generate playback events')
    return JSON.parse(this.mscz._result())
  }
  // PLAYBACK_END

  get viewTransposeSemitones () {
    if (!this.id) { throw new Error('the score is closed') }
    return this.mscz._viewTranspose(this.id)
  }

  // Absolute offset from the original score, not from the current view.
  setViewTransposeSemitones (semitones, spelling = this.viewTransposeSpelling, preserveSystemBreaks = this.viewPreserveSystemBreaks) {
    if (!this.id) { throw new Error('the score is closed') }
    if (!Number.isInteger(semitones) || semitones < -12 || semitones > 12) {
      throw new RangeError('display transposition must be an integer from -12 to +12')
    }
    if (!['preserve', 'readable'].includes(spelling)) { throw new RangeError('unknown spelling mode') }
    if (typeof preserveSystemBreaks !== 'boolean') { throw new TypeError('preserveSystemBreaks must be a boolean') }
    if (!this.mscz._setTranspose(this.id, semitones, spelling === 'readable' ? 1 : 0, preserveSystemBreaks ? 1 : 0)) {
      throw new Error(this.mscz._lastError() || 'could not transpose the score')
    }
  }

  get viewTransposeSpelling () {
    if (!this.id) { throw new Error('the score is closed') }
    return this.mscz._viewSpelling(this.id) === 1 ? 'readable' : 'preserve'
  }

  get viewPreserveSystemBreaks () {
    if (!this.id) { throw new Error('the score is closed') }
    return this.mscz._viewPreserveSystemBreaks(this.id) === 1
  }

  setViewTransposeSpelling (spelling) {
    this.setViewTransposeSemitones(this.viewTransposeSemitones, spelling)
  }

  clearViewTranspose () {
    this.setViewTransposeSemitones(0)
  }

  pageSvg (pageIndex, {
    dpi = 300,
    symbols = SYMBOLS_PATH,
    fonts = FONTS_EMBED
  } = {}) {
    if (!this.mscz._renderSvg(this.id, pageIndex, dpi, symbols, fonts)) {
      throw new Error(this.mscz._lastError() || 'could not render the page')
    }
    return this.mscz._result()
  }

  // The same regression check the native probe prints, so a browser build can
  // be held to the committed baselines.
  layoutFingerprint () {
    if (!this.mscz._fingerprint(this.id)) {
      throw new Error(this.mscz._lastError() || 'could not read the layout')
    }
    return this.mscz._result()
  }

  close () {
    this.mscz._close(this.id)
    this.id = 0
  }
}
