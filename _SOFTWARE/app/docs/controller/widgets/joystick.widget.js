// Joystick: two axes in one stick (drive, pan / tilt). The built-in "joystick" widget as an importable file.
// Import it in the Remote view (Widgets tab > Import). Its kind is "joystick-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const LINE = 'color-mix(in srgb, var(--active) 38%, var(--panel))'

const lerp = (min, max, fraction) => min + (max - min) * fraction
const rest = (snap, min, max) => (snap === 'middle' ? (min + max) / 2 : snap === 'min' ? min : snap === 'max' ? max : undefined)
// One axis, `position` in -1..1: nothing inside the dead zone (0..1 of the radius), the rest stretched over the range.
const axis = (position, min, max, deadzone, invert) => {
  const signed = invert ? -position : position
  const live = deadzone >= 1 ? 0 : Math.max(0, (Math.abs(signed) - deadzone) / (1 - deadzone))
  return lerp(min, max, (Math.sign(signed) * live + 1) / 2)
}

export default {
  kind: 'joystick-js',
  title: 'Joystick (file)',
  description: 'Two axes in one stick: drive, pan / tilt.',
  size: { w: 5, h: 5, minW: 3, minH: 3 },
  advanced: ['deadzone', 'invertX', 'invertY', 'showValues'],
  slots: [
    { key: 'x', label: 'X axis', writes: true },
    { key: 'y', label: 'Y axis', writes: true },
  ],
  fields: [
    { key: 'min', label: 'Lowest value', type: 'number', default: -100 },
    { key: 'max', label: 'Highest value', type: 'number', default: 100 },
    { key: 'snap', label: 'On release', type: 'choice', default: 'middle', choices: [['hold', 'Stay where left'], ['middle', 'Back to the middle']], hint: 'Back to the middle is what a drive stick wants; stay keeps the last position.' },
    { key: 'deadzone', label: 'Dead zone (%)', type: 'number', default: 8, min: 0, max: 90, hint: 'Near the middle the stick sends the middle value, so a drive does not creep.' },
    { key: 'invertX', label: 'Invert X', type: 'bool', default: false },
    { key: 'invertY', label: 'Invert Y', type: 'bool', default: false },
    { key: 'color', label: 'Stick colour', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
    { key: 'showValues', label: 'Show the X and Y values', type: 'bool', default: false },
  ],

  mount(element, api) {
    element.style.cssText = 'display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;height:100%;padding:8px;box-sizing:border-box;min-height:0;'
    const pad = document.createElement('div')
    pad.style.cssText = `position:relative;flex:1 1 0;min-height:0;max-width:100%;aspect-ratio:1;border:2px solid ${LINE};border-radius:50%;touch-action:none;cursor:grab;box-sizing:border-box;`
    const cross = document.createElement('span')
    cross.style.cssText = `position:absolute;inset:0;border-radius:50%;opacity:.7;background:linear-gradient(${LINE},${LINE}) center/1px 100% no-repeat,linear-gradient(${LINE},${LINE}) center/100% 1px no-repeat;`
    const knob = document.createElement('span')
    knob.style.cssText = 'position:absolute;width:36%;aspect-ratio:1;transform:translate(-50%,-50%);border:3px solid #fff;border-radius:50%;box-shadow:0 2px 8px var(--shadow);left:50%;top:50%;box-sizing:border-box;transition:left 120ms ease,top 120ms ease;'
    pad.append(cross, knob)
    const readout = document.createElement('span')
    readout.style.cssText = 'flex:none;color:var(--muted);font:600 11px ui-monospace,SFMono-Regular,Consolas,monospace;font-variant-numeric:tabular-nums;'
    element.append(pad, readout)

    let state = { options: { min: -100, max: 100, snap: 'middle', deadzone: 8, invertX: false, invertY: false, color: 'accent', showValues: false }, preview: true, live: false }
    let held = false
    let at = { x: 0, y: 0 }
    const colorOf = () => COLORS[state.options.color] ?? COLORS.accent
    const values = () => {
      const o = state.options
      return { x: axis(at.x, o.min, o.max, o.deadzone / 100, o.invertX), y: axis(at.y, o.min, o.max, o.deadzone / 100, o.invertY) }
    }
    const paint = () => {
      knob.style.left = `${50 + at.x * 36}%`
      knob.style.top = `${50 - at.y * 36}%`
      knob.style.background = colorOf()
      pad.style.background = `radial-gradient(circle,color-mix(in srgb,${colorOf()} 16%,transparent) 0,transparent 72%)`
      pad.style.borderColor = held ? colorOf() : LINE
      readout.style.display = state.options.showValues ? '' : 'none'
      const v = values()
      readout.textContent = `X ${v.x.toFixed(0)} · Y ${v.y.toFixed(0)}`
    }
    const send = () => {
      const v = values()
      api.write('x', v.x)
      api.write('y', v.y)
    }
    const follow = (event) => {
      const rect = pad.getBoundingClientRect()
      const radius = Math.min(rect.width, rect.height) / 2
      let x = (event.clientX - (rect.left + rect.width / 2)) / radius
      let y = -(event.clientY - (rect.top + rect.height / 2)) / radius
      const length = Math.hypot(x, y)
      if (length > 1) { x /= length; y /= length }
      at = { x, y }
      paint()
      send()
    }
    const release = () => {
      held = false
      pad.style.cursor = 'grab'
      knob.style.transition = 'left 120ms ease, top 120ms ease'
      if (rest(state.options.snap, state.options.min, state.options.max) !== undefined) {
        at = { x: 0, y: 0 }
        send()
      }
      paint()
    }
    pad.addEventListener('pointerdown', (event) => {
      if (state.preview) return
      pad.setPointerCapture(event.pointerId)
      held = true
      pad.style.cursor = 'grabbing'
      knob.style.transition = 'none'
      follow(event)
    })
    pad.addEventListener('pointermove', (event) => { if (held) follow(event) })
    pad.addEventListener('pointerup', release)
    pad.addEventListener('pointercancel', release)
    paint()

    return {
      update(next) { state = next; paint() },
    }
  },
}
