// Gauge: a dial between two values (speed, pressure, temperature). The built-in "gauge" widget as an importable file.
// Its kind is "gauge-js" because built-in kinds are reserved.

const NS = 'http://www.w3.org/2000/svg'
const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const TRACK = 'color-mix(in srgb, var(--active) 16%, var(--panel))'
const ARC = 'M 10 50 A 40 40 0 0 1 90 50'

const clamp = (value, min, max) => Math.min(max, Math.max(min, value))
const fraction = (min, max, value) => (max === min ? 0 : clamp((value - min) / (max - min), 0, 1))
const svgNode = (name, attrs, style) => {
  const node = document.createElementNS(NS, name)
  for (const [key, value] of Object.entries(attrs)) node.setAttribute(key, String(value))
  if (style) Object.assign(node.style, style)
  return node
}

export default {
  kind: 'gauge-js',
  title: 'Gauge (file)',
  description: 'A dial between two values: speed, pressure, temperature.',
  size: { w: 4, h: 3, minW: 2, minH: 2 },
  advanced: ['warn', 'warnAt', 'warnColor'],
  slots: [{ key: 'value', label: 'Value', writes: false }],
  fields: [
    { key: 'min', label: 'Lowest value', type: 'number', default: 0 },
    { key: 'max', label: 'Highest value', type: 'number', default: 100 },
    { key: 'unit', label: 'Unit', type: 'text', default: '' },
    { key: 'color', label: 'Colour', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
    { key: 'warn', label: 'Change colour above a level', type: 'bool', default: false },
    { key: 'warnAt', label: 'Level (% of the range)', type: 'number', default: 80, min: 0, max: 100 },
    { key: 'warnColor', label: 'Colour above the level', type: 'choice', default: 'red', choices: COLOR_CHOICES },
  ],

  mount(element) {
    element.style.cssText = 'display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;min-width:0;height:100%;box-sizing:border-box;padding:4px 8px;'
    const dial = svgNode('svg', { viewBox: '0 0 100 58', preserveAspectRatio: 'xMidYMid meet' }, { flex: '1 1 0', minHeight: '0', width: '100%', height: '0', overflow: 'visible', strokeWidth: 'initial' })
    const arc = (stroke) => svgNode('path', { d: ARC, pathLength: 100, fill: 'none', 'stroke-linecap': 'round' }, { stroke, strokeWidth: '9' })
    const track = arc(TRACK)
    const fill = arc(COLORS.accent)
    fill.style.transition = 'stroke-dasharray 120ms ease,stroke 120ms ease'
    const needle = svgNode('line', { x1: 50, y1: 50, x2: 50, y2: 18, 'stroke-linecap': 'round' }, { stroke: 'var(--active)', strokeWidth: '2', transformOrigin: '50px 50px', transition: 'transform 120ms ease' })
    const hub = svgNode('circle', { cx: 50, cy: 50, r: 3 }, { fill: 'var(--active)' })
    dial.append(track, fill, needle, hub)
    const value = document.createElement('span')
    value.style.cssText = 'color:var(--active);font-size:13px;font-variant-numeric:tabular-nums;white-space:nowrap;'
    const text = document.createElement('span')
    const unit = document.createElement('span')
    unit.style.cssText = 'margin-left:4px;color:var(--muted);font-size:.6em;font-weight:400;'
    value.append(text, unit)
    element.append(dial, value)

    return {
      update({ values, options, preview }) {
        // The layout editor shows the dial part way, so the colours are visible.
        const part = values.value === undefined ? (preview ? 0.6 : 0) : fraction(options.min, options.max, values.value)
        const warning = options.warn && part * 100 >= options.warnAt
        fill.style.stroke = COLORS[warning ? options.warnColor : options.color] ?? COLORS.accent
        fill.style.strokeDasharray = `${part * 100} 100`
        needle.style.transform = `rotate(${part * 180 - 90}deg)`
        text.textContent = values.value === undefined ? '–' : values.value.toFixed(0)
        unit.textContent = options.unit
        unit.style.display = options.unit ? '' : 'none'
      },
    }
  },
}
