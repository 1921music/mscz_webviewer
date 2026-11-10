// SPDX-License-Identifier: GPL-3.0-only
import createSynth from './mscz-synth.js'

let synth, timeline, handles = [], mixer = [], frame = 0, eventIndex = 0
const rate = 44100, block = 256
function sendEvent (part, status, a, b) {
  if (!synth._synth_event(handles[part], status, a, b)) throw new Error('Could not deliver a MIDI event')
}
function reset () {
  for (const h of handles) synth._synth_destroy(h)
  handles = []
  frame = eventIndex = 0
  for (const part of timeline.parts) {
    const h = synth._synth_create()
    if (!h) throw new Error('Could not create a part synthesizer')
    handles.push(h)
    const p = handles.length - 1
    for (const [ch, program, volume, pan] of part.channels) {
      sendEvent(p, 0xc0 | ch, program, 0)
      sendEvent(p, 0xb0 | ch, 7, volume)
      sendEvent(p, 0xb0 | ch, 10, pan)
      // MuseScore's compatibility MIDI pitch bends use a 12-semitone range.
      for (const [cc, value] of [[101, 0], [100, 0], [6, 12], [101, 127], [100, 127]]) sendEvent(p, 0xb0 | ch, cc, value)
    }
  }
  applyMixer()
}
function seek (position) {
  reset()
  position = Math.max(0, Math.min(timeline.duration, Number(position) || 0))
  // Restore programs, controllers and pitch bend, but never old note voices.
  // Events exactly at the destination remain queued and will sound on resume.
  while (eventIndex < timeline.events.length && timeline.events[eventIndex][0] < position) {
    const [, p, status, a, b] = timeline.events[eventIndex++]
    if ((status & 0xf0) !== 0x80 && (status & 0xf0) !== 0x90) sendEvent(p, status, a, b)
  }
  applyMixer()
  frame = Math.round(position * rate)
}
function applyMixer () {
  timeline.parts.forEach((part, p) => {
    for (const [ch] of part.channels) {
      sendEvent(p, 0xb0 | ch, 7, mixer[p].volume)
      sendEvent(p, 0xb0 | ch, 10, mixer[p].pan)
    }
  })
}
function render (blocks) {
  const end = Math.ceil((timeline.duration + 2) * rate / block) * block
  const count = Math.max(0, Math.min(blocks * block, end - frame))
  const left = new Float32Array(count), right = new Float32Array(count)
  const solo = mixer.some(p => p.solo)
  const start = frame
  for (let offset = 0; offset < count; offset += block, frame += block) {
    while (eventIndex < timeline.events.length && timeline.events[eventIndex][0] * rate <= frame) {
      let [, p, status, a, b] = timeline.events[eventIndex++]
      if ((status & 0xf0) === 0xb0) {
        if (a === 7) b = mixer[p].volume
        if (a === 10) b = mixer[p].pan
      }
      sendEvent(p, status, a, b)
    }
    for (let p = 0; p < handles.length; p++) {
      const ptr = synth._synth_render(handles[p])
      if (!ptr) throw new Error('Audio synthesis failed')
      if (mixer[p].mute || (solo && !mixer[p].solo)) continue
      const pcm = synth.HEAP16.subarray(ptr / 2, ptr / 2 + block * 2)
      for (let i = 0; i < block; i++) {
        left[offset + i] += pcm[i * 2] / 32768
        right[offset + i] += pcm[i * 2 + 1] / 32768
      }
    }
  }
  return { left, right, start: start / rate, done: frame >= end }
}
self.onmessage = async ({ data: { id, action, args } }) => {
  try {
    let result
    if (action === 'init') {
      synth = await createSynth({ wasmBinary: args.wasm, locateFile: n => n })
      if (args.font) {
        const ptr = synth._malloc(args.font.length)
        if (!ptr) throw new Error('Not enough memory for the sound bank')
        try {
          synth.HEAPU8.set(args.font, ptr)
          if (!synth._synth_font(ptr, args.font.length)) throw new Error('Could not read the GM sound bank')
        } finally { synth._free(ptr) }
      }
      timeline = args.timeline
      mixer = args.mixer
      seek(args.position || 0)
    } else if (action === 'reset') reset()
    else if (action === 'seek') seek(args)
    else if (action === 'mix') { mixer = args; applyMixer() }
    else if (action === 'render') result = render(32)
    else throw new Error('Unknown playback request')
    self.postMessage({ id, result }, result ? [result.left.buffer, result.right.buffer] : [])
  } catch (e) { self.postMessage({ id, error: e.message }) }
}
