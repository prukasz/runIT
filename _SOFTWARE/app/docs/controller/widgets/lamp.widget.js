// Lamp: a light for a state (running, fault, limit reached). The built-in "lamp" widget as an importable file.
// Its kind is "lamp-js" because built-in kinds are reserved.

// Whether the lamp is lit for `value`: nothing known is not lit.
const lit = (value, mode, limit) => {
  if (value === undefined || Number.isNaN(value)) return false
  if (mode === 'above') return value > limit
  if (mode === 'below') return value < limit
  if (mode === 'equals') return value === limit
  return value !== 0
}
const COLORS = { green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }

export default {
  kind: 'lamp-js',
  title: 'Lamp (file)',
  description: 'A light for a state: running, fault, limit reached.',
  size: { w: 2, h: 2, minW: 1, minH: 1 },
  advanced: ['blink'],
  slots: [{ key: 'value', label: 'Value', writes: false }],
  fields: [
    { key: 'mode', label: 'Lit when the value is', type: 'choice', default: 'nonzero', choices: [['nonzero', 'Not zero (true)'], ['above', 'Above the limit'], ['below', 'Below the limit'], ['equals', 'Equal to the limit']] },
    { key: 'limit', label: 'Limit', type: 'number', default: 0 },
    { key: 'color', label: 'Colour when lit', type: 'choice', default: 'green', choices: [['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']] },
    { key: 'blink', label: 'Blink while lit', type: 'bool', default: false },
    { key: 'label', label: 'Label', type: 'text', default: '' },
  ],

  mount(element) {
    element.style.cssText = 'display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;padding:6px;min-width:0;height:100%;box-sizing:border-box;'
    const bulb = document.createElement('span')
    bulb.setAttribute('role', 'img')
    bulb.style.cssText = 'width:min(56%,44px);aspect-ratio:1;box-sizing:border-box;border:3px solid;border-radius:50%;transition:background 120ms ease,box-shadow 120ms ease,border-color 120ms ease;'
    const caption = document.createElement('span')
    caption.style.cssText = 'color:var(--muted);font-size:11px;'
    element.append(bulb, caption)
    let blinking
    const blink = (on) => {
      if (on && !blinking && bulb.animate) blinking = bulb.animate([{ opacity: 1 }, { opacity: 0.35 }, { opacity: 1 }], { duration: 900, iterations: Infinity })
      if (!on && blinking) { blinking.cancel(); blinking = undefined }
    }

    return {
      update({ values, options }) {
        const on = lit(values.value, options.mode, options.limit)
        const color = COLORS[options.color] ?? COLORS.green
        bulb.setAttribute('aria-label', on ? 'on' : 'off')
        bulb.style.background = on ? color : `color-mix(in srgb, ${color} 14%, transparent)`
        bulb.style.boxShadow = on ? `0 0 16px ${color}` : 'none'
        bulb.style.borderColor = on ? color : `color-mix(in srgb, ${color} 60%, var(--panel))`
        blink(on && options.blink)
        caption.textContent = options.label
        caption.style.display = options.label ? '' : 'none'
      },
      destroy() { blink(false) },
    }
  },
}
