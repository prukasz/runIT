// Slider: one value along a track (speed, brightness, angle). The built-in "slider" widget as an importable file.
// Its kind is "slider-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const TRACK = 'color-mix(in srgb, var(--active) 16%, var(--panel))'

const clamp = (value, min, max) => Math.min(max, Math.max(min, value))
const lerp = (min, max, fraction) => min + (max - min) * fraction
const fraction = (min, max, value) => (max === min ? 0 : clamp((value - min) / (max - min), 0, 1))
const rest = (snap, min, max) => (snap === 'middle' ? (min + max) / 2 : snap === 'min' ? min : snap === 'max' ? max : undefined)
// A value rounded to `step` counted from the lowest end, kept inside the range. A zero step does not round.
const stepped = (value, min, max, step) => {
  const low = Math.min(min, max)
  const high = Math.max(min, max)
  const rounded = step > 0 ? low + Math.round((value - low) / step) * step : value
  return clamp(Number(rounded.toFixed(6)), low, high)
}

export default {
  kind: 'slider-js',
  title: 'Slider (file)',
  description: 'One value along a track: speed, brightness, angle.',
  size: { w: 6, h: 2, minW: 2, minH: 2 },
  advanced: ['step', 'snap', 'showValue', 'unit'],
  slots: [{ key: 'value', label: 'Value', writes: true }],
  fields: [
    { key: 'min', label: 'Lowest value', type: 'number', default: 0 },
    { key: 'max', label: 'Highest value', type: 'number', default: 100 },
    { key: 'step', label: 'Step', type: 'number', default: 1, min: 0 },
    { key: 'orientation', label: 'Direction', type: 'choice', default: 'horizontal', choices: [['horizontal', 'Left to right'], ['vertical', 'Bottom to top']] },
    { key: 'snap', label: 'On release', type: 'choice', default: 'hold', choices: [['hold', 'Stay where left'], ['middle', 'Back to the middle'], ['min', 'Back to the lowest'], ['max', 'Back to the highest']] },
    { key: 'showValue', label: 'Show the value', type: 'bool', default: true },
    { key: 'unit', label: 'Unit', type: 'text', default: '' },
    { key: 'color', label: 'Colour', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
  ],

  mount(element, api) {
    element.style.cssText = 'display:flex;align-items:center;gap:12px;height:100%;min-width:0;box-sizing:border-box;'
    const track = document.createElement('div')
    track.setAttribute('role', 'slider')
    track.style.cssText = `position:relative;flex:1;border-radius:5px;background:${TRACK};cursor:pointer;touch-action:none;outline:none;`
    const fill = document.createElement('span')
    fill.style.cssText = 'position:absolute;border-radius:5px;'
    const thumb = document.createElement('span')
    thumb.style.cssText = 'position:absolute;width:22px;height:22px;box-sizing:border-box;border-radius:50%;background:#fff;box-shadow:0 1px 5px var(--shadow);border:3px solid;'
    track.append(fill, thumb)
    const label = document.createElement('span')
    label.style.cssText = 'color:var(--active);font-size:13px;font-variant-numeric:tabular-nums;white-space:nowrap;'
    element.append(track, label)

    let options = { min: 0, max: 100, step: 1, orientation: 'horizontal', snap: 'hold', showValue: true, unit: '', color: 'accent' }
    let preview = true
    let live
    let held = false
    let local = 0
    let shown = 0
    const vertical = () => options.orientation === 'vertical'
    const paint = () => {
      const color = COLORS[options.color] ?? COLORS.accent
      const percent = fraction(options.min, options.max, shown) * 100
      const scale = held ? ' scale(1.14)' : ''
      fill.style.background = color
      thumb.style.borderColor = color
      if (vertical()) {
        Object.assign(fill.style, { left: '0', right: '0', bottom: '0', top: 'auto', width: 'auto', height: `${percent}%` })
        thumb.style.left = '50%'
        thumb.style.top = 'auto'
        thumb.style.bottom = `${percent}%`
        thumb.style.transform = `translate(-50%,50%)${scale}`
      } else {
        Object.assign(fill.style, { left: '0', top: '0', bottom: '0', right: 'auto', height: 'auto', width: `${percent}%` })
        thumb.style.top = '50%'
        thumb.style.bottom = 'auto'
        thumb.style.left = `${percent}%`
        thumb.style.transform = `translate(-50%,-50%)${scale}`
      }
      track.setAttribute('aria-valuenow', String(shown))
      label.textContent = `${Number.isFinite(shown) ? shown.toFixed(options.step % 1 === 0 ? 0 : 2) : '–'}${options.unit ? ` ${options.unit}` : ''}`
    }
    const compute = () => {
      // The slider follows the board's value; while it is held it follows the finger. The layout editor shows it part way.
      shown = preview ? lerp(options.min, options.max, 0.4) : held || live === undefined ? local : live
      paint()
    }
    const set = (value) => { local = value; compute(); api.write('value', value) }
    const fromPointer = (event) => {
      const rect = track.getBoundingClientRect()
      const along = vertical() ? 1 - (event.clientY - rect.top) / rect.height : (event.clientX - rect.left) / rect.width
      set(stepped(lerp(options.min, options.max, clamp(along, 0, 1)), options.min, options.max, options.step))
    }
    const release = () => {
      held = false
      const target = rest(options.snap, options.min, options.max)
      if (target !== undefined) set(target)
      else compute()
    }
    const nudge = (direction) => set(stepped(shown + direction * (options.step > 0 ? options.step : Math.abs(options.max - options.min) / 100), options.min, options.max, options.step))
    track.addEventListener('pointerdown', (event) => {
      if (preview) return
      track.setPointerCapture(event.pointerId)
      held = true
      fromPointer(event)
    })
    track.addEventListener('pointermove', (event) => { if (held) fromPointer(event) })
    track.addEventListener('pointerup', release)
    track.addEventListener('pointercancel', release)
    track.addEventListener('keydown', (event) => {
      if (preview) return
      const keys = { ArrowRight: () => nudge(1), ArrowUp: () => nudge(1), ArrowLeft: () => nudge(-1), ArrowDown: () => nudge(-1), Home: () => set(options.min), End: () => set(options.max) }
      if (keys[event.key]) { event.preventDefault(); keys[event.key]() }
    })

    return {
      update(state) {
        options = state.options
        preview = state.preview
        live = state.values.value
        track.setAttribute('aria-label', state.name || 'Slider')
        track.setAttribute('aria-valuemin', String(Math.min(options.min, options.max)))
        track.setAttribute('aria-valuemax', String(Math.max(options.min, options.max)))
        track.setAttribute('aria-disabled', String(preview))
        track.tabIndex = preview ? -1 : 0
        track.style.cursor = preview ? 'inherit' : 'pointer'
        element.style.flexDirection = vertical() ? 'column' : 'row'
        element.style.padding = vertical() ? '18px 0' : '0 18px'
        track.style.width = vertical() ? '10px' : ''
        track.style.height = vertical() ? '' : '10px'
        label.style.display = options.showValue ? '' : 'none'
        compute()
      },
    }
  },
}
