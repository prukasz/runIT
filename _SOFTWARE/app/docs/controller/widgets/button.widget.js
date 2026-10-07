// Button: sends a value while it is held (horn, fire, jog). The built-in "button" widget as an importable file.
// Its kind is "button-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]

export default {
  kind: 'button-js',
  title: 'Button (file)',
  description: 'Sends a value while it is held: horn, fire, jog.',
  size: { w: 3, h: 2, minW: 2, minH: 2 },
  advanced: ['pressValue', 'releaseValue'],
  slots: [{ key: 'value', label: 'Value', writes: true }],
  fields: [
    { key: 'pressValue', label: 'Value while held', type: 'number', default: 1 },
    { key: 'releaseValue', label: 'Value when released', type: 'number', default: 0 },
    { key: 'label', label: 'Label', type: 'text', default: 'Hold' },
    { key: 'color', label: 'Colour', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
  ],

  mount(element, api) {
    element.style.cssText = 'display:flex;height:100%;'
    const button = document.createElement('button')
    button.type = 'button'
    button.style.cssText = 'width:auto;height:auto;flex:1;margin:8px;border:2px solid;border-radius:8px;font-size:14px;font-weight:600;touch-action:none;user-select:none;'
    element.append(button)

    let options = { pressValue: 1, releaseValue: 0, label: 'Hold', color: 'accent' }
    let down = false
    const paint = () => {
      const color = COLORS[options.color] ?? COLORS.accent
      button.style.borderColor = color
      button.style.background = down ? color : `color-mix(in srgb, ${color} 18%, var(--panel))`
      button.style.color = down ? '#fff' : 'var(--active)'
      button.style.textShadow = down ? '0 1px 1px rgb(0 0 0 / 35%)' : 'none'
    }
    const press = () => { down = true; paint(); api.write('value', options.pressValue) }
    const stop = () => {
      if (!down) return
      down = false
      paint()
      api.write('value', options.releaseValue)
    }
    button.addEventListener('pointerdown', (event) => { button.setPointerCapture(event.pointerId); press() })
    button.addEventListener('pointerup', stop)
    button.addEventListener('pointercancel', stop)
    button.addEventListener('keydown', (event) => { if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) press() })
    button.addEventListener('keyup', (event) => { if (event.key === ' ' || event.key === 'Enter') stop() })
    paint()

    return {
      update(state) {
        options = state.options
        button.textContent = options.label
        button.disabled = state.preview
        button.style.cursor = state.preview ? 'inherit' : 'pointer'
        paint()
      },
      destroy() {
        // A button still held when the widget goes away lets go, so the variable is not left at the press value.
        stop()
      },
    }
  },
}
