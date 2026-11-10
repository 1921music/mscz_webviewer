// SPDX-License-Identifier: GPL-3.0-only
// Web Audio schedules short worker-generated buffers. No whole-song PCM allocation.
export class Playback {
  constructor (mscz, backend, changed = () => {}) {
    this.mscz = mscz; this.backend = backend; this.changed = changed
    this.state = 'stopped'; this.mixer = []; this.pending = new Map()
    this.serial = 0; this.epoch = 0; this.sources = new Set(); this.volume = 0.8
  }
  setScore (score, preserve = false) {
    this.disposeWorker()
    const previous = this.mixer
    this.timeline = score ? score.playbackTimeline() : null
    this.mixer = this.timeline?.parts.map((p, i) => preserve && previous[i]
      ? previous[i] : { volume: p.volume, pan: p.pan, mute: false, solo: false }) || []
    this.changed()
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
    this.state = 'loading'; this.changed()
    try {
      await resumed
      if (epoch !== this.epoch) return
      await this.ensureWorker()
      if (epoch !== this.epoch) return
      if (!paused) { this.nextTime = this.context.currentTime + 0.06; this.startTime = this.nextTime; this.done = false }
      this.state = 'playing'; this.changed()
      this.timer = setInterval(() => this.pump(), 40)
      this.pump()
    } catch (e) { if (epoch === this.epoch) this.fail(e) }
  }
  async pump () {
    if (this.state !== 'playing' || this.rendering) return
    if (this.done) {
      if (this.context.currentTime >= this.nextTime) this.stop()
      return
    }
    this.changed()
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
    this.changed()
  }
  stop () {
    this.disposeWorker()
    this.changed()
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
    this.changed()
  }
  setVolume (value) {
    this.volume = Math.max(0, Math.min(1, value))
    if (this.gain) this.gain.gain.setValueAtTime(this.volume, this.context.currentTime)
  }
  get position () { return this.startTime == null ? 0 : Math.max(0, Math.min(this.timeline.duration, this.context.currentTime - this.startTime)) }
  fail (error) { this.stop(); this.error = error.message; this.changed() }
}
