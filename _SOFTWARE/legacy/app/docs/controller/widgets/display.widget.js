// Value: a number shown big (a sensor, a counter). The built-in "display" widget as an importable file.
// Its kind is "display-js" because built-in kinds are reserved.

const COLORS = { text: 'var(--active)', accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const TEXT_COLOR_CHOICES = [['text', 'Text'], ['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const SIZES = { small: '18px', normal: '28px', large: '46px' }

const format = (value, decimals) => (Number.isFinite(value) ? value.toFixed(Math.min(6, Math.max(0, Math.round(decimals)))) : '–')

export default {
  kind: 'display-js',
  title: 'Value (file)',
  description: 'A number shown big: a sensor, a counter.',
  size: { w: 4, h: 2, minW: 2, minH: 1 },
  advanced: ['decimals', 'size'],
  slots: [{ key: 'value', label: 'Value', writes: false }],
  fields: [
    { key: 'decimals', label: 'Decimals', type: 'number', default: 1, min: 0, max: 6 },
    { key: 'unit', label: 'Unit', type: 'text', default: '' },
    { key: 'label', label: 'Label', type: 'text', default: '' },
    { key: 'color', label: 'Colour', type: 'choice', default: 'text', choices: TEXT_COLOR_CHOICES },
    { key: 'size', label: 'Size', type: 'choice', default: 'normal', choices: [['small', 'Small'], ['normal', 'Normal'], ['large', 'Large']] },
  ],

  mount(element) {
    element.style.cssText = 'display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;padding:4px 10px;min-width:0;height:100%;box-sizing:border-box;'
    const caption = document.createElement('span')
    caption.style.cssText = 'color:var(--muted);font-size:11px;'
    const number = document.createElement('span')
    number.style.cssText = 'font-weight:600;font-variant-numeric:tabular-nums;line-height:1.1;'
    const text = document.createElement('span')
    const unit = document.createElement('span')
    unit.style.cssText = 'margin-left:4px;color:var(--muted);font-size:.6em;font-weight:400;'
    number.append(text, unit)
    element.append(caption, number)

    return {
      update({ values, options }) {
        caption.textContent = options.label
        caption.style.display = options.label ? '' : 'none'
        number.style.color = COLORS[options.color] ?? COLORS.text
        number.style.fontSize = SIZES[options.size] ?? SIZES.normal
        text.textContent = values.value === undefined ? '–' : format(values.value, options.decimals)
        unit.textContent = options.unit
        unit.style.display = options.unit ? '' : 'none'
      },
    }
  },
}
