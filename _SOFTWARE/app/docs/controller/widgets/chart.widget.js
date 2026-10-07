// Chart: one to three values over the last seconds, with a grid, a limit line and min / max / average.
// The built-in "chart" widget as an importable file. Its kind is "chart-js" because built-in kinds are reserved.

const COLORS = { accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const COLOR_CHOICES = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const LINE = 'color-mix(in srgb, var(--active) 38%, var(--panel))'

const SAMPLE_MS = 200
// Space kept above and below the lines, in percent of the plot's height, so a value at the limit is not clipped.
const PAD = 6
const SERIES = [
  { slot: 'value', color: 'color', name: 'name' },
  { slot: 'value2', color: 'color2', name: 'name2' },
  { slot: 'value3', color: 'color3', name: 'name3' },
]
const LINE_WIDTH = { thin: 1.2, normal: 2, thick: 3.5 }
const NS = 'http://www.w3.org/2000/svg'
const TICKS = [0, 25, 50, 75, 100]

const clamp = (value, min, max) => Math.min(max, Math.max(min, value))
const fraction = (min, max, value) => (max === min ? 0 : clamp((value - min) / (max - min), 0, 1))
const format = (value, decimals) => (Number.isFinite(value) ? value.toFixed(decimals) : '–')
// What the layout editor draws in an empty chart: a gentle wave per line, as fractions of the range.
const demoWave = (index) => Array.from({ length: 48 }, (_, at) => 0.5 + 0.34 * Math.sin((at / 47) * Math.PI * 2 * (1.2 + index * 0.45) + index * 1.4))
// A line's samples as runs of consecutive known values (a gap breaks the line).
const runsOf = (data) => {
  const runs = []
  data.forEach((value, index) => {
    if (value === undefined) { runs.push([]); return }
    if (!runs.length) runs.push([])
    runs[runs.length - 1].push({ index, value })
  })
  return runs.filter((run) => run.length > 0)
}
const svgNode = (name, attrs, style) => {
  const node = document.createElementNS(NS, name)
  for (const [key, value] of Object.entries(attrs)) node.setAttribute(key, String(value))
  if (style) Object.assign(node.style, style)
  return node
}
const box = (css) => {
  const node = document.createElement('div')
  node.style.cssText = css
  return node
}

export default {
  kind: 'chart-js',
  title: 'Chart (file)',
  description: 'One to three values over the last seconds, with a grid, limit line and min / max / average.',
  size: { w: 8, h: 4, minW: 4, minH: 3 },
  advanced: ['value2', 'name2', 'color2', 'value3', 'name3', 'color3', 'line', 'fill', 'grid', 'points', 'stats', 'showLimit', 'limit', 'limitColor'],
  slots: [
    { key: 'value', label: 'Series 1', writes: false },
    { key: 'value2', label: 'Series 2', writes: false, hint: 'Optional: a second line in the same chart.' },
    { key: 'value3', label: 'Series 3', writes: false, hint: 'Optional: a third line.' },
  ],
  fields: [
    { key: 'seconds', label: 'Window (seconds)', type: 'number', default: 20, min: 2, max: 600 },
    { key: 'auto', label: 'Fit the range to the data', type: 'bool', default: true },
    { key: 'min', label: 'Lowest value', type: 'number', default: 0 },
    { key: 'max', label: 'Highest value', type: 'number', default: 100 },
    { key: 'name', label: 'Name of series 1', type: 'text', default: '' },
    { key: 'color', label: 'Colour of series 1', type: 'choice', default: 'accent', choices: COLOR_CHOICES },
    { key: 'name2', label: 'Name of series 2', type: 'text', default: '' },
    { key: 'color2', label: 'Colour of series 2', type: 'choice', default: 'green', choices: COLOR_CHOICES },
    { key: 'name3', label: 'Name of series 3', type: 'text', default: '' },
    { key: 'color3', label: 'Colour of series 3', type: 'choice', default: 'amber', choices: COLOR_CHOICES },
    { key: 'line', label: 'Line', type: 'choice', default: 'normal', choices: [['thin', 'Thin'], ['normal', 'Normal'], ['thick', 'Thick']] },
    { key: 'fill', label: 'Fill under the line', type: 'bool', default: true },
    { key: 'grid', label: 'Grid and time axis', type: 'bool', default: true },
    { key: 'points', label: 'Mark the newest point', type: 'bool', default: true },
    { key: 'stats', label: 'Show min / max / average', type: 'bool', default: true },
    { key: 'showLimit', label: 'Show a limit line', type: 'bool', default: false },
    { key: 'limit', label: 'Limit', type: 'number', default: 80 },
    { key: 'limitColor', label: 'Limit colour', type: 'choice', default: 'red', choices: COLOR_CHOICES },
  ],

  mount(element) {
    element.style.cssText = 'display:flex;flex-direction:column;gap:4px;min-width:0;min-height:0;height:100%;padding:8px 10px 6px;box-sizing:border-box;'
    const head = box('display:flex;flex-wrap:wrap;align-items:center;gap:2px 12px;min-height:20px;font-size:11px;')
    const body = box('display:flex;flex:1;min-height:0;gap:6px;')
    const yaxis = box('display:flex;min-width:28px;flex:none;flex-direction:column;justify-content:space-between;color:var(--muted);font-size:10px;font-variant-numeric:tabular-nums;text-align:right;')
    const plot = box(`position:relative;flex:1;min-width:0;border:1px solid ${LINE};border-radius:4px;background:color-mix(in srgb, var(--active) 3%, var(--panel));`)
    const svg = svgNode('svg', { viewBox: '0 0 100 100', preserveAspectRatio: 'none' }, { position: 'absolute', inset: '0', display: 'block', width: '100%', height: '100%' })
    const dots = box('position:absolute;inset:0;pointer-events:none;')
    const paused = box(`position:absolute;right:6px;top:4px;padding:0 6px;border-radius:3px;background:${LINE};color:var(--panel);font-size:10px;font-weight:600;display:none;`)
    paused.textContent = 'Paused'
    plot.append(svg, dots, paused)
    body.append(yaxis, plot)
    const xaxis = box('display:flex;justify-content:space-between;padding-left:34px;color:var(--muted);font-size:10px;')
    element.append(head, body, xaxis)

    let state
    let history = [[], [], []]
    let isPaused = false
    let working = false
    // The host calls update() only when something changes, so the widget keeps its own sampling clock.
    const timer = setInterval(() => {
      if (!state || !working || isPaused) return
      const capacity = Math.ceil((Math.max(2, state.options.seconds) * 1000) / SAMPLE_MS)
      history = history.map((line, at) => [...line, state.values[SERIES[at].slot]].slice(-capacity))
      draw()
    }, SAMPLE_MS)

    const draw = () => {
      if (!state) return
      const o = state.options
      const seconds = Math.max(2, o.seconds)
      const capacity = Math.ceil((seconds * 1000) / SAMPLE_MS)
      const lines = SERIES.map((entry, at) => ({ ...entry, at, linked: state.linked[entry.slot] })).filter((entry) => entry.at === 0 || entry.linked)
      const known = lines.flatMap((entry) => history[entry.at].filter((value) => value !== undefined))
      const demo = known.length === 0 && (state.preview || !state.live)
      const fitted = o.auto && known.length > 0
      const candidates = o.showLimit ? [...known, o.limit] : known
      const low = fitted ? Math.min(...candidates) : o.min
      const high = fitted ? Math.max(...candidates) : o.max
      const span = high === low ? 1 : high - low
      const yOf = (value) => 100 - PAD - fraction(low, low + span, value) * (100 - 2 * PAD)
      const xOf = (index, length) => 100 - ((length - 1 - index) / Math.max(1, (demo ? length : capacity) - 1)) * 100
      const data = (at) => (demo ? demoWave(at).map((part) => low + part * span) : history[at])
      const decimals = span >= 10 ? 0 : 1

      // Legend, statistics, pause button.
      head.replaceChildren()
      for (const entry of lines) {
        const last = history[entry.at][history[entry.at].length - 1]
        const key = document.createElement('span')
        key.style.cssText = 'display:inline-flex;align-items:center;gap:5px;color:var(--muted);white-space:nowrap;'
        const swatch = document.createElement('i')
        swatch.style.cssText = `width:12px;height:4px;border-radius:2px;background:${COLORS[o[entry.color]] ?? COLORS.accent};`
        const name = document.createElement('span')
        name.textContent = o[entry.name] || `Series ${entry.at + 1}`
        const number = document.createElement('b')
        number.style.cssText = 'color:var(--active);font-variant-numeric:tabular-nums;'
        number.textContent = last === undefined ? '–' : format(last, decimals)
        key.append(swatch, name, number)
        head.append(key)
      }
      const first = history[0].filter((value) => value !== undefined)
      const showStats = o.stats && first.length > 0 && state.width >= 300
      if (showStats) {
        const stats = document.createElement('span')
        stats.style.cssText = 'margin-left:auto;color:var(--muted);font-variant-numeric:tabular-nums;white-space:nowrap;'
        stats.textContent = `min ${format(Math.min(...first), decimals)} · max ${format(Math.max(...first), decimals)} · avg ${format(first.reduce((sum, value) => sum + value, 0) / first.length, decimals)}`
        head.append(stats)
      }
      if (!state.preview && state.live) {
        const pause = document.createElement('button')
        pause.type = 'button'
        pause.textContent = isPaused ? '▶' : '❚❚'
        pause.title = isPaused ? 'Resume' : 'Pause'
        pause.setAttribute('aria-label', isPaused ? 'Resume the chart' : 'Pause the chart')
        pause.style.cssText = `display:grid;width:20px;height:20px;flex:none;place-items:center;margin-left:${showStats ? '0' : 'auto'};padding:0;border:1px solid ${LINE};border-radius:4px;background:transparent;color:var(--active);font-size:9px;cursor:pointer;`
        pause.addEventListener('click', () => { isPaused = !isPaused; draw() })
        head.append(pause)
      }

      // Axes.
      yaxis.replaceChildren(...[high, (high + low) / 2, low].map((value) => { const label = document.createElement('span'); label.textContent = format(value, decimals); return label }))
      xaxis.style.display = o.grid ? 'flex' : 'none'
      xaxis.replaceChildren(...[`−${seconds.toFixed(0)} s`, 'now'].map((text) => { const label = document.createElement('span'); label.textContent = text; return label }))
      paused.style.display = isPaused ? '' : 'none'

      // The plot.
      const children = []
      if (o.grid) {
        for (const tick of TICKS) children.push(svgNode('line', { x1: 0, y1: tick, x2: 100, y2: tick, 'stroke-width': 1, 'vector-effect': 'non-scaling-stroke' }, { stroke: LINE, opacity: '0.55' }))
        for (const tick of TICKS) children.push(svgNode('line', { x1: tick, y1: 0, x2: tick, y2: 100, 'stroke-width': 1, 'vector-effect': 'non-scaling-stroke' }, { stroke: LINE, opacity: '0.3' }))
      }
      if (o.showLimit) children.push(svgNode('line', { x1: 0, y1: yOf(o.limit), x2: 100, y2: yOf(o.limit), 'stroke-width': 1.5, 'stroke-dasharray': '5 4', 'vector-effect': 'non-scaling-stroke' }, { stroke: COLORS[o.limitColor] ?? COLORS.red }))
      for (const entry of lines) {
        const color = COLORS[o[entry.color]] ?? COLORS.accent
        const samples = data(entry.at)
        const group = svgNode('g', { opacity: demo ? 0.6 : 1 })
        for (const run of runsOf(samples)) {
          if (run.length < 2) continue
          const points = run.map((point) => `${xOf(point.index, samples.length)},${yOf(point.value)}`)
          if (o.fill) group.append(svgNode('polygon', { points: `${points.join(' ')} ${xOf(run[run.length - 1].index, samples.length)},100 ${xOf(run[0].index, samples.length)},100` }, { fill: color, fillOpacity: '0.16', stroke: 'none' }))
          group.append(svgNode('polyline', { points: points.join(' '), 'vector-effect': 'non-scaling-stroke' }, { fill: 'none', stroke: color, strokeWidth: String(LINE_WIDTH[o.line] ?? 2), strokeLinejoin: 'round', strokeLinecap: 'round' }))
        }
        children.push(group)
      }
      svg.replaceChildren(...children)

      // The newest point of each line.
      const marks = []
      if (o.points && !demo) {
        for (const entry of lines) {
          const last = history[entry.at][history[entry.at].length - 1]
          if (last === undefined) continue
          const dot = document.createElement('span')
          dot.style.cssText = `position:absolute;right:-4px;top:${yOf(last)}%;width:8px;height:8px;margin-top:-4px;border-radius:50%;background:${COLORS[o[entry.color]] ?? COLORS.accent};box-shadow:0 0 0 2px var(--panel);`
          marks.push(dot)
        }
      }
      dots.replaceChildren(...marks)
    }

    return {
      update(next) {
        state = next
        const now = next.live && !next.preview
        if (!now && working) { history = [[], [], []]; isPaused = false }
        working = now
        draw()
      },
      destroy() { clearInterval(timer) },
    }
  },
}
