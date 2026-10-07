// Keyboard: keys drive two axes (laptop only), for example W A S D or the arrows. The built-in "keyboard" widget as an
// importable file. Its kind is "keyboard-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const TRACK = 'color-mix(in srgb, var(--active) 16%, var(--panel))'
const LINE = 'color-mix(in srgb, var(--active) 38%, var(--panel))'

const normalize = (key) => (key.length === 1 ? key.toLowerCase() : key)
const label = (key) => (key === ' ' ? 'Space' : key.startsWith('Arrow') ? key.slice(5) : key.length === 1 ? key.toUpperCase() : key)
const typing = (target) => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))

export default {
  kind: 'keyboard-js',
  title: 'Keyboard (file)',
  description: 'Keys drive two axes (laptop only): W A S D, arrows.',
  size: { w: 5, h: 4, minW: 4, minH: 3 },
  advanced: ['amount', 'rest'],
  keyboard: true,
  slots: [
    { key: 'x', label: 'Left / right', writes: true },
    { key: 'y', label: 'Up / down', writes: true },
  ],
  fields: [
    { key: 'left', label: 'Left key', type: 'key', default: 'a' },
    { key: 'right', label: 'Right key', type: 'key', default: 'd' },
    { key: 'up', label: 'Up key', type: 'key', default: 'w' },
    { key: 'down', label: 'Down key', type: 'key', default: 's' },
    { key: 'amount', label: 'Value while a key is held', type: 'number', default: 100 },
    { key: 'rest', label: 'Value with no key', type: 'number', default: 0 },
    { key: 'color', label: 'Colour of a pressed key', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
  ],

  mount(element, api) {
    element.style.cssText = 'display:grid;grid-template-columns:repeat(3,1fr);grid-template-rows:repeat(2,1fr);gap:5px;padding:10px;height:100%;box-sizing:border-box;'
    const caps = {}
    for (const [name, column, row] of [['up', 2, 1], ['left', 1, 2], ['down', 2, 2], ['right', 3, 2]]) {
      const cap = document.createElement('span')
      cap.style.cssText = `grid-column:${column};grid-row:${row};display:grid;min-width:0;place-items:center;overflow:hidden;border:2px solid ${LINE};border-radius:6px;font-size:13px;font-weight:600;`
      caps[name] = cap
      element.append(cap)
    }

    let state = { options: { left: 'a', right: 'd', up: 'w', down: 's', amount: 100, rest: 0, color: 'accent' }, live: false, preview: true }
    const pressed = new Set()
    const keys = () => ({ left: normalize(state.options.left), right: normalize(state.options.right), up: normalize(state.options.up), down: normalize(state.options.down) })
    const paint = () => {
      const map = keys()
      const color = COLORS[state.options.color] ?? COLORS.accent
      for (const [name, cap] of Object.entries(caps)) {
        const on = pressed.has(map[name])
        cap.textContent = label(map[name])
        cap.style.background = on ? color : TRACK
        cap.style.color = on ? '#fff' : 'var(--active)'
        cap.style.borderColor = on ? color : LINE
        cap.style.textShadow = on ? '0 1px 1px rgb(0 0 0 / 35%)' : 'none'
      }
    }
    const apply = () => {
      const { amount, rest } = state.options
      const map = keys()
      const axisOf = (plus, minus) => rest + ((pressed.has(plus) ? 1 : 0) - (pressed.has(minus) ? 1 : 0)) * amount
      api.write('x', axisOf(map.right, map.left))
      api.write('y', axisOf(map.up, map.down))
      paint()
    }
    const onDown = (event) => {
      if (state.preview || !state.live) return
      const key = normalize(event.key)
      if (typing(event.target) || event.ctrlKey || event.metaKey || event.altKey || !Object.values(keys()).includes(key)) return
      event.preventDefault()
      if (pressed.has(key)) return
      pressed.add(key)
      apply()
    }
    const onUp = (event) => {
      if (!pressed.delete(normalize(event.key))) return
      apply()
    }
    const onBlur = () => { if (pressed.size) { pressed.clear(); apply() } }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', onBlur)

    return {
      update(next) {
        const wasWorking = state.live && !state.preview
        state = next
        // Leaving live mode (or the layout editor opening) lets go of every key.
        if (wasWorking && !(next.live && !next.preview) && pressed.size) { pressed.clear(); apply() }
        element.title = next.live || next.preview ? '' : 'Enable the interface to use the keys'
        paint()
      },
      destroy() {
        window.removeEventListener('keydown', onDown)
        window.removeEventListener('keyup', onUp)
        window.removeEventListener('blur', onBlur)
        if (pressed.size) { pressed.clear(); api.write('x', state.options.rest); api.write('y', state.options.rest) }
      },
    }
  },
}
