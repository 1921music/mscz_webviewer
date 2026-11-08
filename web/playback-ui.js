// SPDX-License-Identifier: GPL-3.0-only
import { Playback } from './playback.js'
export function attachPlayback (mscz, backend) {
  const root = document.getElementById('playback')
  const play = root.querySelector('#play'), stop = root.querySelector('#stop')
  const message = root.querySelector('#playbackStatus'), rows = root.querySelector('#mixerRows')
  let busy = false
  const clock = t => `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, '0')}`
  const player = new Playback(mscz, backend, () => {
    play.disabled = busy || !player.timeline || player.state === 'loading'
    stop.disabled = !player.timeline || player.state === 'stopped'
    play.textContent = player.state === 'playing' ? 'Pause' : player.state === 'loading' ? 'Loading…' : 'Play'
    root.querySelector('#playbackTime').textContent = `${clock(player.position)} / ${clock(player.timeline?.duration || 0)}`
    message.textContent = player.error || ''
  })
  play.addEventListener('click', () => player.state === 'playing' ? player.pause().catch(e => player.fail(e)) : player.play())
  stop.addEventListener('click', () => player.stop())
  root.querySelector('#masterVolume').addEventListener('input', e => player.setVolume(Number(e.target.value) / 100))
  return {
    player,
    setBusy: value => { busy = value; player.changed() },
    stop: () => player.stop(),
    setScore (score, preserve = false) {
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
