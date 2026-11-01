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

// Pushed before the first render. "optional" is everything a score has to ask
// for by name: alternative music fonts, tablature, figured bass.
const DEFAULT_GROUPS = ['core']

export async function load ({ base = '.', groups = DEFAULT_GROUPS } = {}) {
  const mscz = await create()
  await mscz.loadResources(base, groups)
  mscz.init()
  return mscz
}

// Starts the module without loading anything. For a caller that gets its
// resources some other way than fetch -- the node-side check reads them off
// disk -- since the engine does not care where the bytes came from.
export async function create () {
  return new Mscz(await createMscz())
}

export class Mscz {
  constructor (module) {
    this.m = module
    const c = (name, ret, args) => module.cwrap(name, ret, args)

    this._resourceAdd = c('mscz_resource_add', null, ['string', 'number', 'number'])
    this._init = c('mscz_init', null, [])
    this._open = c('mscz_open', 'number', ['number', 'number', 'string'])
    this._close = c('mscz_close', null, ['number'])
    this._pageCount = c('mscz_page_count', 'number', ['number'])
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
    const ptr = this._alloc(bytes.length)
    this.m.HEAPU8.set(bytes, ptr)
    this._resourceAdd(name, ptr, bytes.length)
    this._free(ptr)
    this.resourceBytes += bytes.length
  }

  init () {
    this._init()
  }

  // Opens a .mscz. `name` is only the document's display name.
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
