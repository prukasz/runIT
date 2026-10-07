// Switch: on / off that stays (a relay, a light). The built-in "switch" widget as an importable file.
// Its kind is "switch-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const TRACK = 'color-mix(in srgb, var(--active) 16%, var(--panel))'
const LINE = 'color-mix(in srgb, var(--active) 38%, var(--panel))'

export default {
  kind: 'switch-js',
  title: 'Switch (file)',
  description: 'On / off that stays: a relay, a light.',
  size: { w: 3, h: 2, minW: 2, minH: 2 },
  advanced: ['onValue', 'offValue', 'showState'],
  slots: [{ key: 'value', label: 'Value', writes: true }],
  fields: [
    { key: 'onValue', label: 'Value when on', type: 'number', default: 1 },
    { key: 'offValue', label: 'Value when off', type: 'number', default: 0 },
    { key: 'label', label: 'Label', type: 'text', default: '' },
    { key: 'color', label: 'Colour when on', type: 'choice', default: 'green', choices: COLOR_CHOICES },
    { key: 'showState', label: 'Show ON / OFF', type: 'bool', default: true },
  ],

  mount(element, api) {
    element.style.cssText = 'display:flex;height:100%;'
    const button = document.createElement('button')
    button.type = 'button'
    button.setAttribute('role', 'switch')
    button.style.cssText = 'display:flex;width:auto;height:auto;flex:1;flex-direction:column;align-items:center;justify-content:center;gap:8px;padding:6px;border:0;background:transparent;color:var(--active);font-size:13px;'
    const track = document.createElement('span')
    track.style.cssText = 'position:relative;display:flex;width:68px;height:34px;flex:none;box-sizing:border-box;align-items:center;border:2px solid;border-radius:17px;transition:background 140ms ease,border-color 140ms ease;'
    const state = document.createElement('span')
    state.style.cssText = 'position:absolute;font-size:10px;font-weight:700;letter-spacing:.06em;'
    const thumb = document.createElement('span')
    thumb.style.cssText = 'position:absolute;top:50%;left:3px;width:24px;height:24px;transform:translateY(-50%);border-radius:50%;background:#fff;box-shadow:0 1px 4px var(--shadow),0 0 0 1px color-mix(in srgb,var(--active) 22%,transparent);transition:left 140ms ease;'
    track.append(state, thumb)
    const text = document.createElement('span')
    text.style.cssText = 'max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:600;'
    button.append(track, text)
    element.append(button)

    let options = { onValue: 1, offValue: 0, label: '', color: 'green', showState: true }
    let preview = true
    let live
    let local = false
    let isOn = false
    const paint = () => {
      const color = COLORS[options.color] ?? COLORS.green
      button.setAttribute('aria-checked', String(isOn))
      track.style.background = isOn ? color : TRACK
      track.style.borderColor = isOn ? color : LINE
      thumb.style.left = isOn ? 'calc(100% - 27px)' : '3px'
      state.textContent = isOn ? 'ON' : 'OFF'
      state.style.display = options.showState ? '' : 'none'
      state.style.left = isOn ? '10px' : 'auto'
      state.style.right = isOn ? 'auto' : '10px'
      state.style.color = isOn ? '#fff' : 'var(--muted)'
      state.style.textShadow = isOn ? '0 1px 1px rgb(0 0 0 / 35%)' : 'none'
      text.textContent = options.label
      text.style.display = options.label ? '' : 'none'
    }
    const compute = () => {
      // The layout editor shows the switch on, so the colour chosen for it is visible.
      isOn = preview ? true : live === undefined ? local : live === options.onValue
      paint()
    }
    button.addEventListener('click', () => {
      local = !isOn
      api.write('value', isOn ? options.offValue : options.onValue)
      isOn = local
      paint()
    })

    return {
      update(next) {
        options = next.options
        preview = next.preview
        live = next.values.value
        button.disabled = preview
        button.style.cursor = preview ? 'inherit' : 'pointer'
        compute()
      },
    }
  },
}
