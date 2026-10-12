// Bar: a level between two values (battery, tank, load). The built-in "bar" widget as an importable file.
// Its kind is "bar-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const TRACK = 'color-mix(in srgb, var(--active) 16%, var(--panel))'
const LINE = 'color-mix(in srgb, var(--active) 38%, var(--panel))'

const clamp = (value, min, max) => Math.min(max, Math.max(min, value))
const fraction = (min, max, value) => (max === min ? 0 : clamp((value - min) / (max - min), 0, 1))

export default {
  kind: 'bar-js',
  title: 'Bar (file)',
  description: 'A level between two values: battery, tank, load.',
  size: { w: 6, h: 1, minW: 2, minH: 1 },
  advanced: ['warn', 'warnAt', 'warnColor'],
  slots: [{ key: 'value', label: 'Value', writes: false }],
  fields: [
    { key: 'min', label: 'Lowest value', type: 'number', default: 0 },
    { key: 'max', label: 'Highest value', type: 'number', default: 100 },
    { key: 'orientation', label: 'Direction', type: 'choice', default: 'horizontal', choices: [['horizontal', 'Left to right'], ['vertical', 'Bottom to top']] },
    { key: 'unit', label: 'Unit', type: 'text', default: '' },
    { key: 'color', label: 'Colour', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
    { key: 'warn', label: 'Change colour above a level', type: 'bool', default: false },
    { key: 'warnAt', label: 'Level (% of the range)', type: 'number', default: 80, min: 0, max: 100 },
    { key: 'warnColor', label: 'Colour above the level', type: 'choice', default: 'red', choices: COLOR_CHOICES },
  ],

  mount(element) {
    element.style.cssText = 'display:flex;align-items:center;gap:10px;min-width:0;height:100%;box-sizing:border-box;'
    const track = document.createElement('span')
    track.style.cssText = `position:relative;flex:1;box-sizing:border-box;overflow:hidden;border:1px solid ${LINE};border-radius:8px;background:${TRACK};`
    const fill = document.createElement('span')
    fill.style.cssText = 'position:absolute;left:0;bottom:0;transition:width 120ms ease,height 120ms ease,background 120ms ease;'
    track.append(fill)
    const value = document.createElement('span')
    value.style.cssText = 'color:var(--active);font-size:13px;font-variant-numeric:tabular-nums;white-space:nowrap;'
    const text = document.createElement('span')
    const unit = document.createElement('span')
    unit.style.cssText = 'margin-left:4px;color:var(--muted);font-size:.6em;font-weight:400;'
    value.append(text, unit)
    element.append(track, value)

    return {
      update({ values, options, preview }) {
        const vertical = options.orientation === 'vertical'
        // The layout editor shows the bar part way, so the colours are visible.
        const part = values.value === undefined ? (preview ? 0.6 : 0) : fraction(options.min, options.max, values.value)
        const warning = options.warn && part * 100 >= options.warnAt
        element.style.flexDirection = vertical ? 'column-reverse' : 'row'
        element.style.padding = vertical ? '12px 0' : '0 12px'
        track.style.flex = '1'
        track.style.width = vertical ? '16px' : 'auto'
        track.style.height = vertical ? 'auto' : '16px'
        fill.style.background = COLORS[warning ? options.warnColor : options.color] ?? COLORS.accent
        fill.style.width = vertical ? '100%' : `${part * 100}%`
        fill.style.height = vertical ? `${part * 100}%` : '100%'
        text.textContent = values.value === undefined ? '–' : values.value.toFixed(0)
        unit.textContent = options.unit
        unit.style.display = options.unit ? '' : 'none'
      },
    }
  },
}
