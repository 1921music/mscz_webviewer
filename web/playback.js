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
    return this.state === 'playing' || this.state === 'paused'
      ? playbackPosition(this.timeline, this.position)
      : { time: 0, noteIds: [], system: null }
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
    await this.request('init', { wasm, font, timeline: this.timeline, mixer: this.mixer })
    if (epoch === this.epoch) await this.request('mix', this.mixer)
  }
  async play () {
    if (!this.timeline || this.state === 'playing' || this.state === 'loading') return
    // Create/resume inside the user's gesture, before awaiting resources.
    if (!this.context) {
      this.context = new AudioContext({ sampleRate: 44100 })
      this.gain = this.context.createGain()
      this.gain.gain.value = this.volume
      this.gain.connect(this.context.destination)
    }
    const resumed = this.context.resume()
    const paused = this.state === 'paused'
    const epoch = this.epoch
    this.error = ''
    this.state = 'loading'; this.notify()
    try {
      await resumed
      if (epoch !== this.epoch) return
      await this.ensureWorker()
      if (epoch !== this.epoch) return
      if (!paused) { this.nextTime = this.context.currentTime + 0.06; this.startTime = this.nextTime; this.done = false }
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
    ++this.epoch
    clearInterval(this.timer)
    for (const source of this.sources) { source.stop(); source.disconnect() }
    this.sources.clear()
    this.worker?.terminate(); this.worker = null
    if (this.workerURL) URL.revokeObjectURL(this.workerURL)
    this.workerURL = null
    for (const call of this.pending.values()) call.reject(new Error('Playback cancelled'))
    this.pending.clear()
    this.state = 'stopped'; this.rendering = false; this.startTime = null
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
  get position () { return this.startTime == null ? 0 : Math.max(0, Math.min(this.timeline.duration, this.context.currentTime - this.startTime)) }
  fail (error) { this.stop(); this.error = error.message; this.notify() }
}

// Pure lookup API: no DOM, audio device or host layout dependencies.
// IDs are valid for the current score session/layout only.
const trackingIndexes = new WeakMap()
export function playbackPosition (timeline, time) {
  if (!timeline?.visual) return { time, noteIds: [], system: null }
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
  return { time, noteIds: [...ids], system: system && time < system.end ? system : null }
}

// Optional SVG adapter. Hosts own page mounting and scrolling via onSystem;
// this class never scrolls window or changes score/source colors.
export class PlaybackView {
  constructor (player, { getPages, onSystem = () => {} }) {
    this.player = player; this.getPages = getPages; this.onSystem = onSystem
    this.pages = new WeakMap(); this.systemKey = null
    this.unsubscribe = player.subscribe(() => this.update())
    this.update()
  }
  update () {
    const tracking = this.player.tracking, active = new Set(tracking.noteIds)
    for (const svg of this.getPages()) {
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
        entry = { groups, active: new Set() }; this.pages.set(svg, entry)
      }
      for (const id of entry.active) if (!active.has(id)) entry.groups.get(id)?.forEach(g => g.classList.remove('mscz-playing'))
      for (const id of active) if (!entry.active.has(id)) entry.groups.get(id)?.forEach(g => g.classList.add('mscz-playing'))
      entry.active = active
    }
    const system = tracking.system
    const key = system ? `${system.page}:${system.id}` : null
    if (this.player.state === 'playing' && key !== this.systemKey) {
      this.systemKey = key
      if (system) this.onSystem(system)
    } else if (!system) this.systemKey = null
  }
  resetFollow () { this.systemKey = null; this.update() }
  destroy () {
    this.unsubscribe()
    for (const svg of this.getPages()) {
      svg?.querySelectorAll('.mscz-playing').forEach(g => g.classList.remove('mscz-playing'))
      svg?.querySelectorAll('[data-playback-style]').forEach(g => g.remove())
    }
  }
}
