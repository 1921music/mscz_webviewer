// SPDX-License-Identifier: GPL-3.0-only
// Web Audio schedules short worker-generated buffers. No whole-song PCM allocation.
export class Playback {
  constructor (mscz, backend, changed = () => {}) {
    this.listeners = new Set()
    this.mscz = mscz; this.backend = backend; this.changed = changed
    this.state = 'stopped'; this.mixer = []; this.pending = new Map()
    this.serial = 0; this.epoch = 0; this.sources = new Set(); this.volume = 0.8
  }
  // Subscriptions coexist with the original changed callback. Unsubscribe on disposal.
  subscribe (listener) { this.listeners.add(listener); return () => this.listeners.delete(listener) }
  notify () { this.changed(); for (const listener of this.listeners) listener(this.tracking) }
  get tracking () {
    return this.canSeek
      ? playbackPosition(this.timeline, this.position)
      : { time: 0, noteIds: [], system: null, cursor: null }
  }
  get canSeek () { return !!this.timeline && ['playing', 'paused', 'loading'].includes(this.state) }
  // Scrubbing holds a silent preview until release. Stop/score changes cancel
  // the transaction, so a late pointerup can never restart playback.
  beginScrub () {
    if (!this.canSeek) return false
    if (this.scrub) return true
    const scrub = { resume: this.state !== 'paused', position: this.position }
    this.disposeWorker()
    this.scrub = scrub; this.pausedPosition = scrub.position; this.state = 'paused'
    this.notify()
    return true
  }
  endScrub ({ cancel = false } = {}) {
    const scrub = this.scrub
    if (!scrub) return false
    if (cancel) this.seek(scrub.position)
    this.scrub = null
    if (scrub.resume && this.state === 'paused') void this.play()
    else this.notify()
    return true
  }
  // A direct seek preserves transport intent; old voices are not reconstructed.
  cue (time = 0) {
    if (!this.timeline || !['stopped','paused'].includes(this.state)) return false
    if (this.state === 'stopped') this.state = 'paused'
    return this.seek(time, { follow: false })
  }
  seek (time, { follow = true } = {}) {
    if (!this.canSeek) return false
    if (!Number.isFinite(time)) throw new RangeError('Invalid playback position')
    const position = Math.max(0, Math.min(this.timeline.duration, time))
    const scrub = this.scrub, resume = !scrub && this.state !== 'paused'
    this.disposeWorker()
    this.scrub = scrub
    this.pausedPosition = position; this.state = 'paused'; this.error = ''
    if (follow) this.seekSerial = (this.seekSerial || 0) + 1
    this.notify()
    if (resume) void this.play()
    return true
  }
  seekNote (id) {
    if (!this.canSeek) return false
    const notes = this.timeline?.visual?.notes.filter(n => n.id === id) || []
    const visit = this.tracking.system?.visit
    const sameVisit = notes.filter(n => visit != null && n.visit === visit)
    const candidates = sameVisit.length ? sameVisit : notes
    if (!candidates.length) return false
    const position = this.position
    return this.seek(candidates.reduce((a, b) => Math.abs(a.start - position) <= Math.abs(b.start - position) ? a : b).start)
  }
  setScore (score, preserve = false) {
    this.disposeWorker()
    const previous = this.mixer
    this.timeline = score ? score.playbackTimeline() : null
    this.mixer = this.timeline?.parts.map((p, i) => preserve && previous[i]
      ? previous[i] : { volume: p.volume, pan: p.pan, mute: false, solo: false }) || []
    this.notify()
  }
  request (action, args) {
    return new Promise((resolve, reject) => {
      const id = ++this.serial
      this.pending.set(id, { resolve, reject })
      this.worker.postMessage({ id, action, args })
    })
  }
  async ensureWorker () {
    if (this.worker) return
    const epoch = this.epoch
    const assets = this.mscz.playbackAssets
    const base = new URL(this.mscz.base + '/', location.href)
    const bytes = async name => {
      const response = await fetch(new URL(name, base))
      if (!response.ok) throw new Error(`Could not load ${name}`)
      return new Uint8Array(await response.arrayBuffer())
    }
    const wasm = assets?.wasm || await bytes('mscz-synth.wasm')
    const font = this.backend === 'TSF_SF3' ? assets?.font || await bytes('sonivox.sf3') : null
    if (epoch !== this.epoch) throw new Error('Playback cancelled')
    let url = new URL('playback-worker.js', base)
    if (assets?.workerURL) url = assets.workerURL
    else if (assets) url = URL.createObjectURL(new Blob([assets.worker], { type: 'text/javascript' }))
    this.worker = new Worker(url, { type: assets && !assets.workerURL ? 'classic' : 'module' })
    if (assets && !assets.workerURL) this.workerURL = url
    this.worker.onmessage = ({ data }) => {
      const call = this.pending.get(data.id)
      if (!call) return
      this.pending.delete(data.id)
      if (data.error) call.reject(new Error(data.error))
      else call.resolve(data.result)
    }
    this.worker.onerror = e => this.fail(new Error(e.message || 'Audio worker failed'))
    await this.request('init', { wasm, font, timeline: this.timeline, mixer: this.mixer, position: this.pausedPosition || 0 })
    if (epoch === this.epoch) await this.request('mix', this.mixer)
  }
  async play () {
    if (!this.timeline || this.scrub || this.state === 'playing' || this.state === 'loading') return
    // Create/resume inside the user's gesture, before awaiting resources.
    if (!this.context) {
      this.context = new AudioContext({ sampleRate: 44100 })
      this.gain = this.context.createGain()
      this.gain.gain.value = this.volume
      this.gain.connect(this.context.destination)
    }
    const resumed = this.context.resume()
    const buffered = this.state === 'paused' && !!this.worker && this.startTime != null
    const position = this.position
    const epoch = this.epoch
    this.error = ''
    this.state = 'loading'; this.notify()
    try {
      await resumed
      if (epoch !== this.epoch) return
      await this.ensureWorker()
      if (epoch !== this.epoch) return
      if (!buffered) { this.nextTime = this.context.currentTime + 0.06; this.startTime = this.nextTime - position; this.pausedPosition = 0; this.done = false }
      this.state = 'playing'; this.notify()
      this.timer = setInterval(() => this.pump(), 40)
      this.pump()
    } catch (e) { if (epoch === this.epoch) this.fail(e) }
  }
  async pump () {
    if (this.state !== 'playing' || this.rendering) return
    if (this.done) {
      this.notify()
      if (this.context.currentTime >= this.nextTime) this.stop()
      return
    }
    this.notify()
    if (this.nextTime - this.context.currentTime > 0.22) return
    const epoch = this.epoch
    this.rendering = true
    try {
      const chunk = await this.request('render')
      if (epoch !== this.epoch) return
      const buffer = this.context.createBuffer(2, chunk.left.length, 44100)
      buffer.copyToChannel(chunk.left, 0); buffer.copyToChannel(chunk.right, 1)
      const source = this.context.createBufferSource()
      source.buffer = buffer; source.connect(this.gain)
      source.onended = () => { source.disconnect(); this.sources.delete(source) }
      this.sources.add(source)
      const time = Math.max(this.nextTime, this.context.currentTime + 0.01)
      // Preserve displayed position if an overloaded device underruns.
      this.startTime += time - this.nextTime
      source.start(time)
      this.nextTime = time + buffer.duration
      this.done = chunk.done
    } catch (e) { if (epoch === this.epoch) this.fail(e) }
    finally { if (epoch === this.epoch) this.rendering = false }
  }
  async pause () {
    if (this.state !== 'playing') return
    this.state = 'paused'
    clearInterval(this.timer)
    await this.context.suspend()
    this.notify()
  }
  stop () {
    this.disposeWorker()
    this.notify()
  }
  disposeWorker () {
    this.scrub = null
    ++this.epoch
    clearInterval(this.timer)
    for (const source of this.sources) { source.stop(); source.disconnect() }
    this.sources.clear()
    this.worker?.terminate(); this.worker = null
    if (this.workerURL) URL.revokeObjectURL(this.workerURL)
    this.workerURL = null
    for (const call of this.pending.values()) call.reject(new Error('Playback cancelled'))
    this.pending.clear()
    this.state = 'stopped'; this.rendering = false; this.startTime = null; this.pausedPosition = 0
  }
  setMixer (index, patch) {
    const p = this.mixer[index]
    if (!p) throw new RangeError('Unknown part')
    for (const key of ['volume', 'pan']) if (key in patch && (!Number.isInteger(patch[key]) || patch[key] < 0 || patch[key] > 127)) throw new RangeError(key)
    Object.assign(p, patch)
    if (this.worker && this.state !== 'loading') this.request('mix', this.mixer).catch(e => { if (e.message !== 'Playback cancelled') this.fail(e) })
    this.notify()
  }
  setVolume (value) {
    this.volume = Math.max(0, Math.min(1, value))
    if (this.gain) this.gain.gain.setValueAtTime(this.volume, this.context.currentTime)
  }
  get position () { return this.startTime == null ? (this.pausedPosition || 0) : Math.max(0, Math.min(this.timeline.duration, this.context.currentTime - this.startTime)) }
  fail (error) { this.stop(); this.error = error.message; this.notify() }
}

// Pure lookup API: no DOM, audio device or host layout dependencies.
// IDs are valid for the current score session/layout only.
const trackingIndexes = new WeakMap()
export function playbackPosition (timeline, time) {
  if (!timeline?.visual) return { time, noteIds: [], system: null, cursor: null }
  let index = trackingIndexes.get(timeline)
  if (!index) {
    const notes = [...timeline.visual.notes].sort((a, b) => a.start - b.start)
    let end = -Infinity
    const maxEnd = notes.map(n => (end = Math.max(end, n.end)))
    const systems = [...timeline.visual.systems].sort((a, b) => a.start - b.start)
    index = { notes, maxEnd, systems }; trackingIndexes.set(timeline, index)
  }
  const upper = values => {
    let lo = 0, hi = values.length
    while (lo < hi) { const mid = (lo + hi) >>> 1; if (values[mid].start <= time) lo = mid + 1; else hi = mid }
    return lo - 1
  }
  const ids = new Set()
  for (let i = upper(index.notes); i >= 0 && index.maxEnd[i] > time; --i) {
    if (index.notes[i].end > time) ids.add(index.notes[i].id)
  }
  const system = index.systems[upper(index.systems)]
  const current = system && (time < system.end || (time === timeline.duration && time === system.end)) ? system : null
  let cursor = null
  if (current?.anchors?.length) {
    const points = current.anchors
    let lo = 0, hi = points.length
    while (lo < hi) { const mid = (lo + hi) >>> 1; if (points[mid][0] <= time) lo = mid + 1; else hi = mid }
    const a = points[Math.max(0, lo - 1)], b = points[Math.min(lo, points.length - 1)]
    const fraction = b[0] > a[0] ? Math.max(0, Math.min(1, (time - a[0]) / (b[0] - a[0]))) : 0
    cursor = { page: current.page, x: a[1] + fraction * (b[1] - a[1]), y: current.bounds[1], height: current.bounds[3] }
  }
  return { time, noteIds: [...ids], system: current, cursor }
}

// Optional SVG adapter. Hosts own page mounting and scrolling via onSystem;
// this class never scrolls window or changes score/source colors.
export class PlaybackView {
  constructor (player, { getPages, onSystem = () => {} }) {
    this.player = player; this.getPages = getPages; this.onSystem = onSystem
    this.pages = new WeakMap(); this.systemKey = null; this.seekSerial = player.seekSerial
    this.unsubscribe = player.subscribe(() => this.update())
    this.update()
  }
  update () {
    if (this.destroyed) return
    const tracking = this.player.tracking, active = new Set(tracking.noteIds)
    let pageIndex = 0
    for (const svg of this.getPages()) {
      const page = Number(svg?.dataset.playbackPage ?? pageIndex)
      ++pageIndex
      if (!svg) continue
      let entry = this.pages.get(svg)
      if (!entry) {
        const style = document.createElementNS('http://www.w3.org/2000/svg', 'style')
        style.textContent = '.mscz-playing text,.mscz-playing use,.mscz-playing path{fill:#d00000!important}'
        style.dataset.playbackStyle = ''; svg.append(style)
        const groups = new Map()
        for (const group of svg.querySelectorAll('[data-note-id]')) {
          const id = group.dataset.noteId
          if (!groups.has(id)) groups.set(id, [])
          groups.get(id).push(group)
        }
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'line')
        line.dataset.playbackCursor = ''; line.setAttribute('aria-hidden', 'true')
        line.setAttribute('stroke', '#d00000'); line.setAttribute('stroke-width', '2')
        line.setAttribute('vector-effect', 'non-scaling-stroke'); line.style.pointerEvents = 'none'
        svg.append(line)
        entry = { groups, line, active: new Set() }; this.pages.set(svg, entry)
      }
      for (const id of entry.active) if (!active.has(id)) entry.groups.get(id)?.forEach(g => g.classList.remove('mscz-playing'))
      for (const id of active) if (!entry.active.has(id)) entry.groups.get(id)?.forEach(g => g.classList.add('mscz-playing'))
      entry.active = active
      const cursor = tracking.cursor
      const visible = cursor && cursor.page === page
      entry.line.style.display = visible ? '' : 'none'
      if (visible) {
        const box = svg.viewBox.baseVal
        const x = box.x + cursor.x * box.width, y = box.y + cursor.y * box.height
        entry.line.setAttribute('x1', x); entry.line.setAttribute('x2', x)
        entry.line.setAttribute('y1', y); entry.line.setAttribute('y2', y + cursor.height * box.height)
      }
    }
    const system = tracking.system
    const key = system ? `${system.page}:${system.id}` : null
    const seeking = this.seekSerial !== this.player.seekSerial
    this.seekSerial = this.player.seekSerial
    if ((this.player.state === 'playing' && key !== this.systemKey) || seeking) {
      this.systemKey = key
      if (system) this.onSystem(system, { seeking })
    } else if (!system) this.systemKey = null
    // Follow the audio clock at display refresh rate, independently of worker
    // buffering and the coarser transport/mixer notifications.
    if (this.player.state === 'playing' && this.frame == null) {
      this.frame = requestAnimationFrame(() => { this.frame = null; this.update() })
    } else if (this.player.state !== 'playing' && this.frame != null) {
      cancelAnimationFrame(this.frame); this.frame = null
    }
  }
  bindSeek (range, rewind) {
    const controller = new AbortController(), options = { signal: controller.signal }
    let pointer = null, keyboard = false
    const finish = cancel => {
      if (pointer == null && !keyboard) return
      pointer = null; keyboard = false
      this.player.endScrub({ cancel })
    }
    range.addEventListener('pointerdown', e => {
      if (e.isPrimary && e.button === 0 && this.player.beginScrub()) pointer = e.pointerId
    }, options)
    document.addEventListener('pointerup', e => { if (pointer === e.pointerId) finish(!e.isTrusted) }, { ...options, capture: true })
    document.addEventListener('pointercancel', e => { if (pointer === e.pointerId) finish(true) }, { ...options, capture: true })
    window.addEventListener('blur', () => finish(true), options)
    const keys = new Set(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End','PageUp','PageDown'])
    range.addEventListener('keydown', e => { if (keys.has(e.key) && this.player.beginScrub()) keyboard = true }, options)
    range.addEventListener('keyup', e => { if (keys.has(e.key) && keyboard) finish(false) }, options)
    range.addEventListener('blur', () => { if (keyboard) finish(false) }, options)
    range.addEventListener('input', () => this.player.seek(Number(range.value)), options)
    rewind.addEventListener('click', () => this.player.seek(0), options)
    const unbind = () => { controller.abort(); finish(true); this.bindings?.delete(unbind) }
    if (!this.bindings) this.bindings = new Set()
    this.bindings.add(unbind)
    return unbind
  }
  noteAt (clientX, clientY, svg) {
    if (!this.player.canSeek || !svg) return null
    let hit = null, distance = Infinity
    for (const group of svg.querySelectorAll('[data-note-id]')) {
      const r = group.getBoundingClientRect()
      if (!r.width || !r.height || clientX < r.left - 6 || clientX > r.right + 6 || clientY < r.top - 6 || clientY > r.bottom + 6) continue
      const d = Math.hypot(clientX - (r.left + r.width / 2), clientY - (r.top + r.height / 2))
      if (d < distance) { distance = d; hit = group.dataset.noteId }
    }
    return hit
  }
  resetFollow () { this.systemKey = null; this.update() }
  destroy () {
    this.destroyed = true
    for (const unbind of [...(this.bindings || [])]) unbind()
    if (this.frame != null) cancelAnimationFrame(this.frame)
    this.unsubscribe()
    for (const svg of this.getPages()) {
      svg?.querySelectorAll('.mscz-playing').forEach(g => g.classList.remove('mscz-playing'))
      svg?.querySelectorAll('[data-playback-style],[data-playback-cursor]').forEach(g => g.remove())
    }
  }
}
