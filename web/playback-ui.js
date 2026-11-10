// SPDX-License-Identifier: GPL-3.0-only
import { Playback, PlaybackView } from './playback.js'
export function attachPlayback (mscz, backend) {
  const root = document.getElementById('playback')
  const play = root.querySelector('#play'), stop = root.querySelector('#stop')
  const message = root.querySelector('#playbackStatus'), rows = document.getElementById('mixerRows')
  const seek = root.querySelector('#seek'), rewind = root.querySelector('#rewind')
  let busy = false
  const clock = t => `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, '0')}`
  const player = new Playback(mscz, backend, () => {
    play.disabled = busy || !player.timeline || player.state === 'loading' || !!player.scrub
    stop.disabled = !player.timeline || player.state === 'stopped'
    play.textContent = player.state === 'playing' ? 'Pause' : player.state === 'loading' ? 'Loading…' : 'Play'
    root.querySelector('#playbackTime').textContent = `${clock(player.position)} / ${clock(player.timeline?.duration || 0)}`
    seek.max = player.timeline?.duration || 0; seek.value = player.position
    seek.disabled = rewind.disabled = busy || !player.canSeek
    message.textContent = player.error || ''
  })
  const follow = root.querySelector('#followPlayback')
  const view = new PlaybackView(player, {
    getPages: () => document.querySelectorAll('#pages > svg'),
    onSystem: (system, { seeking = false } = {}) => {
      if (!follow.checked && !seeking) return
      const svg = document.querySelectorAll('#pages > svg')[system.page]
      if (!svg) return
      const rect = svg.getBoundingClientRect()
      const top = rect.top + system.bounds[1] * rect.height
      const bottom = top + system.bounds[3] * rect.height
      const toolbarBottom = document.querySelector('header')?.getBoundingClientRect().bottom || 60
      const visibleBottom = root.hidden ? innerHeight : root.getBoundingClientRect().top
      if (top < toolbarBottom || bottom > visibleBottom) window.scrollBy({ top: top - toolbarBottom - 12, behavior: 'instant' })
    }
  })
  const panelSize = new ResizeObserver(() => {
    document.getElementById('pages').style.setProperty('--playback-padding', root.hidden ? '16px' : `${root.getBoundingClientRect().height + 16}px`)
    view.resetFollow()
  })
  panelSize.observe(root)
  follow.onchange = () => view.resetFollow()
  view.bindSeek(seek, rewind)
  const mixer = document.getElementById('mixerDialog'), mixerButton = root.querySelector('#showMixer')
  mixerButton.onclick = () => {
    if (mixer.open) mixer.close(); else mixer.show()
    mixerButton.setAttribute('aria-expanded', String(mixer.open))
  }
  document.getElementById('closeMixer').onclick = () => { mixer.close(); mixerButton.setAttribute('aria-expanded', 'false') }
  let tap
  document.getElementById('pages').addEventListener('pointerdown', e => {
    if (!e.isPrimary || e.button !== 0) return
    const svg = e.target.closest('svg'), id = view.noteAt(e.clientX, e.clientY, svg)
    if (!id) return
    e.preventDefault(); tap = { id, pointerId: e.pointerId, x: e.clientX, y: e.clientY }
  })
  document.addEventListener('pointerup', e => {
    if (!tap || tap.pointerId !== e.pointerId) return
    const down = tap; tap = null
    if (!down.moved && Math.hypot(e.clientX - down.x, e.clientY - down.y) < 8) player.seekNote(down.id)
  })
  document.addEventListener('pointermove', e => {
    if (tap && tap.pointerId === e.pointerId && Math.hypot(e.clientX-tap.x,e.clientY-tap.y)>8) tap.moved = true
  })
  document.addEventListener('pointercancel', () => { tap = null })
  play.addEventListener('click', () => player.state === 'playing' ? player.pause().catch(e => player.fail(e)) : player.play())
  stop.addEventListener('click', () => player.stop())
  root.querySelector('#masterVolume').addEventListener('input', e => player.setVolume(Number(e.target.value) / 100))
  return {
    player,
    refresh: () => view.resetFollow(),
    setBusy: value => { busy = value; player.changed() },
    stop: () => player.stop(),
    setScore (score, preserve = false) {
      mixer.close(); mixerButton.setAttribute('aria-expanded', 'false')
      player.error = ''
      try { player.setScore(score, preserve) } catch (e) { player.timeline = null; player.fail(e) }
      rows.replaceChildren()
      player.timeline?.parts.forEach((part, i) => {
        const tr = document.createElement('tr')
        const name = document.createElement('th'); name.scope = 'row'; name.textContent = part.name || `Part ${i + 1}`; tr.append(name)
        for (const [key, label] of [['volume', 'Volume'], ['pan', 'Pan'], ['mute', 'Mute'], ['solo', 'Solo']]) {
          const td = document.createElement('td'), input = document.createElement('input')
          const toggle = key === 'mute' || key === 'solo'
          input.type = toggle ? 'checkbox' : 'range'
          input.setAttribute('aria-label', `${name.textContent} ${label}`)
          input.dataset.part = i; input.dataset.control = key
          if (toggle) input.checked = player.mixer[i][key]
          else { input.min = 0; input.max = 127; input.value = player.mixer[i][key]; input.title = input.value }
          input.addEventListener('input', () => {
            input.title = input.value
            player.setMixer(i, { [key]: toggle ? input.checked : Number(input.value) })
          })
          td.append(input); tr.append(td)
        }
        rows.append(tr)
      })
    }
  }
}
