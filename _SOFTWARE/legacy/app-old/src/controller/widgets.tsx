import { useEffect, useRef, useState } from 'react'
import { Pause, Play } from 'lucide-react'
import { axisValue, CELL, fractionOf, flagOption, formatNumber, isBuiltinKind, isMissingKind, keyLabel, lampLit, lerp, normalizeKey, numberOption, optionOf, restValue, stepped, textOption, widgetType } from '../domain/controller'
import type { ControllerWidget, WidgetInstance, WidgetOption, WidgetState } from '../domain/controller'

/*
 * What each widget looks like and does. A widget reads and writes through `io`
 * (the debug session's live link, see ControllerStage): `read` of a slot's
 * variable, `write` to it. While the layout is being edited (`preview`) the
 * widgets only show themselves and do nothing.
 */

export interface WidgetIo {
  /** The interface is enabled: the board runs the program and the widgets work. */
  readonly live: boolean
  readonly read: (variableId: string | undefined) => number | undefined
  readonly write: (variableId: string | undefined, value: number) => void
}

interface WidgetProps {
  readonly widget: ControllerWidget
  readonly io: WidgetIo
  readonly preview: boolean
}

const linkOf = (widget: ControllerWidget, slot: string): string | undefined => widget.links[slot]

/* The colour a widget is drawn in: `cw-c-<name>` sets --cw-c (Controller.css). */
const colorClass = (widget: ControllerWidget, key = 'color'): string => `cw-c-${textOption(widget, key)}`
const unit01 = (value: number): number => Math.min(1, Math.max(0, value))

/* ---- Joystick ---- */

function Joystick({ widget, io, preview }: WidgetProps) {
  const min = numberOption(widget, 'min')
  const max = numberOption(widget, 'max')
  const dead = numberOption(widget, 'deadzone') / 100
  const invertX = flagOption(widget, 'invertX')
  const invertY = flagOption(widget, 'invertY')
  const snap = textOption(widget, 'snap')
  const [stick, setStick] = useState({ x: 0, y: 0 })
  const [held, setHeld] = useState(false)
  const pad = useRef<HTMLDivElement>(null)
  const send = (at: { x: number; y: number }) => {
    io.write(linkOf(widget, 'x'), axisValue(at.x, min, max, dead, invertX))
    io.write(linkOf(widget, 'y'), axisValue(at.y, min, max, dead, invertY))
  }
  const follow = (event: React.PointerEvent) => {
    const rect = pad.current?.getBoundingClientRect()
    if (!rect) return
    const radius = Math.min(rect.width, rect.height) / 2
    let x = (event.clientX - (rect.left + rect.width / 2)) / radius
    let y = -(event.clientY - (rect.top + rect.height / 2)) / radius
    const length = Math.hypot(x, y)
    if (length > 1) { x /= length; y /= length }
    const at = { x, y }
    setStick(at)
    send(at)
  }
  const release = () => {
    setHeld(false)
    if (restValue(snap, min, max) === undefined) return
    const at = { x: 0, y: 0 }
    setStick(at)
    send(at)
  }
  return (
    <div className={`cw-joystick ${colorClass(widget)}`}>
      <div
        ref={pad}
        className={`cw-stick-pad ${held ? 'is-held' : ''}`}
        onPointerDown={(event) => { if (preview) return; event.currentTarget.setPointerCapture(event.pointerId); setHeld(true); follow(event) }}
        onPointerMove={(event) => { if (held) follow(event) }}
        onPointerUp={release}
        onPointerCancel={release}
      >
        <span className="cw-stick-cross" aria-hidden="true" />
        <span className="cw-stick-knob" style={{ left: `${50 + stick.x * 36}%`, top: `${50 - stick.y * 36}%` }} />
      </div>
      {flagOption(widget, 'showValues') && (
        <span className="cw-stick-readout">X {formatNumber(axisValue(stick.x, min, max, dead, invertX), 0)} · Y {formatNumber(axisValue(stick.y, min, max, dead, invertY), 0)}</span>
      )}
    </div>
  )
}

/* ---- Slider ---- */

function Slider({ widget, io, preview }: WidgetProps) {
  const min = numberOption(widget, 'min')
  const max = numberOption(widget, 'max')
  const step = numberOption(widget, 'step')
  const vertical = textOption(widget, 'orientation') === 'vertical'
  const snap = textOption(widget, 'snap')
  const unit = textOption(widget, 'unit')
  const id = linkOf(widget, 'value')
  const [local, setLocal] = useState(() => lerp(min, max, 0))
  const [held, setHeld] = useState(false)
  const track = useRef<HTMLDivElement>(null)
  const live = io.read(id)
  // The slider follows the board's value; while it is held it follows the finger. The layout editor shows it part way.
  const shown = preview ? lerp(min, max, 0.4) : held || live === undefined ? local : live
  const rest = restValue(snap, min, max)
  const set = (value: number) => { setLocal(value); io.write(id, value) }
  const fromPointer = (event: React.PointerEvent) => {
    const rect = track.current?.getBoundingClientRect()
    if (!rect) return
    const along = vertical ? 1 - (event.clientY - rect.top) / rect.height : (event.clientX - rect.left) / rect.width
    set(stepped(lerp(min, max, unit01(along)), min, max, step))
  }
  const release = () => {
    setHeld(false)
    if (rest !== undefined) set(rest)
  }
  const nudge = (direction: number) => set(stepped(shown + direction * (step > 0 ? step : Math.abs(max - min) / 100), min, max, step))
  const percent = fractionOf(min, max, shown) * 100
  return (
    <div className={`cw-slider ${vertical ? 'is-vertical' : ''} ${colorClass(widget)} ${held ? 'is-held' : ''}`}>
      <div
        ref={track}
        className="cw-slider-track"
        role="slider"
        tabIndex={preview ? -1 : 0}
        aria-label={widget.name || 'Slider'}
        aria-valuemin={Math.min(min, max)}
        aria-valuemax={Math.max(min, max)}
        aria-valuenow={shown}
        aria-disabled={preview}
        onPointerDown={(event) => { if (preview) return; event.currentTarget.setPointerCapture(event.pointerId); setHeld(true); fromPointer(event) }}
        onPointerMove={(event) => { if (held) fromPointer(event) }}
        onPointerUp={release}
        onPointerCancel={release}
        onKeyDown={(event) => {
          if (preview) return
          const keys: Record<string, () => void> = { ArrowRight: () => nudge(1), ArrowUp: () => nudge(1), ArrowLeft: () => nudge(-1), ArrowDown: () => nudge(-1), Home: () => set(min), End: () => set(max) }
          const action = keys[event.key]
          if (action) { event.preventDefault(); action() }
        }}
      >
        <span className="cw-slider-fill" style={vertical ? { height: `${percent}%` } : { width: `${percent}%` }} />
        <span className="cw-slider-thumb" style={vertical ? { bottom: `${percent}%` } : { left: `${percent}%` }} />
      </div>
      {flagOption(widget, 'showValue') && <span className="cw-value">{formatNumber(shown, step % 1 === 0 ? 0 : 2)}{unit && <span className="cw-unit">{unit}</span>}</span>}
    </div>
  )
}

/* ---- Switch and button ---- */

function Switch({ widget, io, preview }: WidgetProps) {
  const on = numberOption(widget, 'onValue')
  const off = numberOption(widget, 'offValue')
  const id = linkOf(widget, 'value')
  const [local, setLocal] = useState(false)
  const live = io.read(id)
  // The layout editor shows the switch on, so the colour chosen for it is visible.
  const isOn = preview ? true : live === undefined ? local : live === on
  const label = textOption(widget, 'label')
  return (
    <button
      type="button"
      role="switch"
      aria-checked={isOn}
      className={`cw-switch ${colorClass(widget)} ${isOn ? 'is-on' : ''}`}
      disabled={preview}
      onClick={() => { setLocal(!isOn); io.write(id, isOn ? off : on) }}
    >
      <span className="cw-switch-track">
        {flagOption(widget, 'showState') && <span className="cw-switch-state">{isOn ? 'ON' : 'OFF'}</span>}
        <span className="cw-switch-thumb" />
      </span>
      {label && <span className="cw-switch-label">{label}</span>}
    </button>
  )
}

function Hold({ widget, io, preview }: WidgetProps) {
  const id = linkOf(widget, 'value')
  const [down, setDown] = useState(false)
  const stop = () => {
    if (!down) return
    setDown(false)
    io.write(id, numberOption(widget, 'releaseValue'))
  }
  return (
    <button
      type="button"
      className={`cw-hold ${colorClass(widget)} ${down ? 'is-down' : ''}`}
      disabled={preview}
      onPointerDown={(event) => { event.currentTarget.setPointerCapture(event.pointerId); setDown(true); io.write(id, numberOption(widget, 'pressValue')) }}
      onPointerUp={stop}
      onPointerCancel={stop}
      onKeyDown={(event) => { if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) { setDown(true); io.write(id, numberOption(widget, 'pressValue')) } }}
      onKeyUp={(event) => { if (event.key === ' ' || event.key === 'Enter') stop() }}
    >{textOption(widget, 'label')}</button>
  )
}

/* ---- Input field ---- */

function NumberInput({ widget, io, preview }: WidgetProps) {
  const min = numberOption(widget, 'min')
  const max = numberOption(widget, 'max')
  const id = linkOf(widget, 'value')
  const live = io.read(id)
  const [draft, setDraft] = useState<string>()
  const every = textOption(widget, 'send') === 'change'
  const parsed = draft === undefined || draft.trim() === '' ? undefined : Number(draft)
  const valid = parsed !== undefined && Number.isFinite(parsed) && parsed >= Math.min(min, max) && parsed <= Math.max(min, max)
  const send = () => {
    if (valid) { io.write(id, parsed); setDraft(undefined) }
  }
  return (
    <form className="cw-input" onSubmit={(event) => { event.preventDefault(); send() }}>
      <input
        type="number"
        aria-label={widget.name || 'Value to send'}
        className={draft !== undefined && !valid ? 'is-invalid' : ''}
        disabled={preview}
        min={Math.min(min, max)}
        max={Math.max(min, max)}
        step={numberOption(widget, 'step') || 'any'}
        value={draft ?? (live === undefined ? '' : String(live))}
        placeholder="–"
        onChange={(event) => {
          setDraft(event.target.value)
          const next = Number(event.target.value)
          if (every && event.target.value.trim() !== '' && Number.isFinite(next) && next >= Math.min(min, max) && next <= Math.max(min, max)) io.write(id, next)
        }}
        onBlur={() => { if (every) setDraft(undefined) }}
      />
      {textOption(widget, 'unit') && <span className="cw-unit">{textOption(widget, 'unit')}</span>}
      {!every && <button type="submit" disabled={preview || !valid}>Send</button>}
    </form>
  )
}

/* ---- Keyboard ---- */

const typing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))

function KeyboardPad({ widget, io, preview }: WidgetProps) {
  const keys = { left: normalizeKey(textOption(widget, 'left')), right: normalizeKey(textOption(widget, 'right')), up: normalizeKey(textOption(widget, 'up')), down: normalizeKey(textOption(widget, 'down')) }
  const amount = numberOption(widget, 'amount')
  const rest = numberOption(widget, 'rest')
  const [down, setDown] = useState<ReadonlySet<string>>(new Set())
  const latest = useRef({ widget, io, keys, amount, rest })
  latest.current = { widget, io, keys, amount, rest }
  useEffect(() => {
    if (preview || !io.live) return undefined
    const pressed = new Set<string>()
    const apply = () => {
      const { widget: current, io: link, keys: map, amount: size, rest: idle } = latest.current
      const axis = (plus: string, minus: string) => idle + ((pressed.has(plus) ? 1 : 0) - (pressed.has(minus) ? 1 : 0)) * size
      link.write(current.links.x, axis(map.right, map.left))
      link.write(current.links.y, axis(map.up, map.down))
      setDown(new Set(pressed))
    }
    const mapped = (key: string) => Object.values(latest.current.keys).includes(key)
    const onDown = (event: KeyboardEvent) => {
      const key = normalizeKey(event.key)
      if (typing(event.target) || event.ctrlKey || event.metaKey || event.altKey || !mapped(key)) return
      event.preventDefault()
      if (pressed.has(key)) return
      pressed.add(key)
      apply()
    }
    const onUp = (event: KeyboardEvent) => {
      const key = normalizeKey(event.key)
      if (!pressed.delete(key)) return
      apply()
    }
    const onBlur = () => { if (pressed.size) { pressed.clear(); apply() } }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', onBlur)
    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', onBlur)
      if (pressed.size) {
        pressed.clear()
        const { widget: current, io: link, rest: idle } = latest.current
        link.write(current.links.x, idle)
        link.write(current.links.y, idle)
      }
    }
  }, [preview, io.live])
  const cap = (key: string, place: string) => <span className={`cw-key at-${place} ${down.has(key) ? 'is-pressed' : ''}`}>{keyLabel(key)}</span>
  return (
    <div className={`cw-keyboard ${colorClass(widget)}`} title={io.live || preview ? undefined : 'Enable the interface to use the keys'}>
      {cap(keys.up, 'up')}{cap(keys.left, 'left')}{cap(keys.down, 'down')}{cap(keys.right, 'right')}
    </div>
  )
}

/* ---- Displays ---- */

function Display({ widget, io }: WidgetProps) {
  const value = io.read(linkOf(widget, 'value'))
  const unit = textOption(widget, 'unit')
  return (
    <div className={`cw-display is-${textOption(widget, 'size')} ${colorClass(widget)}`}>
      {textOption(widget, 'label') && <span className="cw-caption">{textOption(widget, 'label')}</span>}
      <span className="cw-number">{value === undefined ? '–' : formatNumber(value, numberOption(widget, 'decimals'))}{unit && <span className="cw-unit">{unit}</span>}</span>
    </div>
  )
}

const SAMPLE_MS = 200
/** Space kept above and below the chart lines, in percent of the plot's height, so a value at the limit isn't clipped. */
const CHART_PAD = 6
/** The chart's three lines: the slot they read, and the settings that name and colour them. */
const SERIES = [
  { slot: 'value', color: 'color', name: 'name' },
  { slot: 'value2', color: 'color2', name: 'name2' },
  { slot: 'value3', color: 'color3', name: 'name3' },
] as const
const LINE_WIDTH: Readonly<Record<string, number>> = { thin: 1.2, normal: 2, thick: 3.5 }
type Samples = readonly (number | undefined)[]

/** What the layout editor draws in an empty chart: a gentle wave per line, as fractions of the range. */
const demoWave = (index: number): number[] => Array.from({ length: 48 }, (_, at) => 0.5 + 0.34 * Math.sin((at / 47) * Math.PI * 2 * (1.2 + index * 0.45) + index * 1.4))

/** A line's samples as runs of consecutive known values (a gap in the data breaks the line). */
const runsOf = (data: Samples): { index: number; value: number }[][] => {
  const runs: { index: number; value: number }[][] = []
  data.forEach((value, index) => {
    if (value === undefined) { runs.push([]); return }
    if (!runs.length) runs.push([])
    runs.at(-1)!.push({ index, value })
  })
  return runs.filter((run) => run.length > 0)
}

function Chart({ widget, io, preview }: WidgetProps) {
  const seconds = Math.max(2, numberOption(widget, 'seconds'))
  const capacity = Math.ceil((seconds * 1000) / SAMPLE_MS)
  const ids = SERIES.map((entry) => linkOf(widget, entry.slot))
  const [history, setHistory] = useState<readonly Samples[]>([[], [], []])
  const [paused, setPaused] = useState(false)
  const current = useRef<(number | undefined)[]>([])
  const pausedNow = useRef(false)
  current.current = ids.map((id) => io.read(id))
  pausedNow.current = paused
  useEffect(() => {
    if (preview || !io.live) { setHistory([[], [], []]); setPaused(false); return undefined }
    const timer = window.setInterval(() => {
      if (pausedNow.current) return
      setHistory((all) => all.map((line, at) => [...line, current.current[at]].slice(-capacity)))
    }, SAMPLE_MS)
    return () => window.clearInterval(timer)
  }, [preview, io.live, capacity])

  const lines = SERIES.map((entry, at) => ({ ...entry, at, linked: ids[at] !== undefined })).filter((entry) => entry.at === 0 || entry.linked)
  const auto = flagOption(widget, 'auto')
  const showLimit = flagOption(widget, 'showLimit')
  const limit = numberOption(widget, 'limit')
  const grid = flagOption(widget, 'grid')
  const known = lines.flatMap((entry) => history[entry.at]!.filter((value): value is number => value !== undefined))
  const demo = known.length === 0 && (preview || !io.live)
  const fitted = auto && known.length > 0
  const candidates = showLimit ? [...known, limit] : known
  const low = fitted ? Math.min(...candidates) : numberOption(widget, 'min')
  const high = fitted ? Math.max(...candidates) : numberOption(widget, 'max')
  const span = high === low ? 1 : high - low
  const yOf = (value: number): number => 100 - CHART_PAD - fractionOf(low, low + span, value) * (100 - 2 * CHART_PAD)
  const data = (at: number): Samples => (demo ? demoWave(at).map((part) => low + part * span) : history[at]!)
  const xOf = (index: number, length: number): number => 100 - ((length - 1 - index) / Math.max(1, (demo ? length : capacity) - 1)) * 100
  const first = history[0]!.filter((value): value is number => value !== undefined)
  const decimals = span >= 10 ? 0 : 1
  const stats = flagOption(widget, 'stats') && first.length > 0
  const thickness = LINE_WIDTH[textOption(widget, 'line')] ?? 2
  const ticks = [0, 25, 50, 75, 100]
  return (
    <div className="cw-chart">
      <div className="cw-chart-head">
        {lines.map((entry) => {
          const last = history[entry.at]!.at(-1)
          return (
            <span key={entry.slot} className={`cw-chart-key ${colorClass(widget, entry.color)}`}>
              <i aria-hidden="true" />{textOption(widget, entry.name) || `Series ${entry.at + 1}`}
              <b>{last === undefined ? '–' : formatNumber(last, decimals)}</b>
            </span>
          )
        })}
        {stats && <span className="cw-chart-stats">min {formatNumber(Math.min(...first), decimals)} · max {formatNumber(Math.max(...first), decimals)} · avg {formatNumber(first.reduce((sum, value) => sum + value, 0) / first.length, decimals)}</span>}
        {!preview && io.live && (
          <button type="button" className="cw-chart-pause" aria-label={paused ? 'Resume the chart' : 'Pause the chart'} title={paused ? 'Resume' : 'Pause'} onClick={() => setPaused(!paused)}>
            {paused ? <Play aria-hidden="true" /> : <Pause aria-hidden="true" />}
          </button>
        )}
      </div>
      <div className="cw-chart-body">
        <div className="cw-chart-yaxis" aria-hidden="true">
          <span>{formatNumber(high, decimals)}</span><span>{formatNumber((high + low) / 2, decimals)}</span><span>{formatNumber(low, decimals)}</span>
        </div>
        <div className="cw-chart-plot">
          <svg viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
            {grid && ticks.map((tick) => <line key={`h${tick}`} x1="0" y1={tick} x2="100" y2={tick} className="cw-chart-grid" vectorEffect="non-scaling-stroke" />)}
            {grid && ticks.map((tick) => <line key={`v${tick}`} x1={tick} y1="0" x2={tick} y2="100" className="cw-chart-grid is-vertical" vectorEffect="non-scaling-stroke" />)}
            {showLimit && <line x1="0" y1={yOf(limit)} x2="100" y2={yOf(limit)} className={`cw-chart-limit ${colorClass(widget, 'limitColor')}`} vectorEffect="non-scaling-stroke" />}
            {lines.map((entry) => {
              const samples = data(entry.at)
              return (
                <g key={entry.slot} className={colorClass(widget, entry.color)} opacity={demo ? 0.6 : 1}>
                  {runsOf(samples).map((run, at) => {
                    const points = run.map((point) => `${xOf(point.index, samples.length)},${yOf(point.value)}`)
                    return (
                      <g key={at}>
                        {flagOption(widget, 'fill') && run.length > 1 && <polygon points={`${points.join(' ')} ${xOf(run.at(-1)!.index, samples.length)},100 ${xOf(run[0]!.index, samples.length)},100`} className="cw-chart-area" />}
                        {run.length > 1 && <polyline points={points.join(' ')} className="cw-chart-line" style={{ strokeWidth: thickness }} vectorEffect="non-scaling-stroke" />}
                      </g>
                    )
                  })}
                </g>
              )
            })}
          </svg>
          {flagOption(widget, 'points') && !demo && lines.map((entry) => {
            const last = history[entry.at]!.at(-1)
            return last === undefined ? null : <span key={entry.slot} className={`cw-chart-dot ${colorClass(widget, entry.color)}`} style={{ top: `${yOf(last)}%` }} />
          })}
          {paused && <span className="cw-chart-paused">Paused</span>}
        </div>
      </div>
      {grid && <div className="cw-chart-xaxis" aria-hidden="true"><span>−{formatNumber(seconds, 0)} s</span><span>now</span></div>}
    </div>
  )
}

function Lamp({ widget, io }: WidgetProps) {
  const lit = lampLit(io.read(linkOf(widget, 'value')), textOption(widget, 'mode'), numberOption(widget, 'limit'))
  return (
    <div className={`cw-lamp ${colorClass(widget)}`}>
      <span className={`cw-bulb ${lit ? 'is-lit' : ''} ${lit && flagOption(widget, 'blink') ? 'is-blinking' : ''}`} role="img" aria-label={lit ? 'on' : 'off'} />
      {textOption(widget, 'label') && <span className="cw-caption">{textOption(widget, 'label')}</span>}
    </div>
  )
}

function Bar({ widget, io, preview }: WidgetProps) {
  const min = numberOption(widget, 'min')
  const max = numberOption(widget, 'max')
  const value = io.read(linkOf(widget, 'value'))
  const vertical = textOption(widget, 'orientation') === 'vertical'
  // The layout editor shows the bar part way, so the colours are visible.
  const fraction = value === undefined ? (preview ? 0.6 : 0) : fractionOf(min, max, value)
  const unit = textOption(widget, 'unit')
  const warning = flagOption(widget, 'warn') && fraction * 100 >= numberOption(widget, 'warnAt')
  return (
    <div className={`cw-bar ${vertical ? 'is-vertical' : ''}`}>
      <span className="cw-bar-track"><span className={`cw-bar-fill ${colorClass(widget, warning ? 'warnColor' : 'color')}`} style={vertical ? { height: `${fraction * 100}%` } : { width: `${fraction * 100}%` }} /></span>
      <span className="cw-value">{value === undefined ? '–' : formatNumber(value, 0)}{unit && <span className="cw-unit">{unit}</span>}</span>
    </div>
  )
}

/* The dial: a half circle from (10,50) to (90,50) with the needle turning about (50,50). */
function Gauge({ widget, io, preview }: WidgetProps) {
  const value = io.read(linkOf(widget, 'value'))
  const fraction = value === undefined ? (preview ? 0.6 : 0) : fractionOf(numberOption(widget, 'min'), numberOption(widget, 'max'), value)
  const unit = textOption(widget, 'unit')
  const warning = flagOption(widget, 'warn') && fraction * 100 >= numberOption(widget, 'warnAt')
  return (
    <div className={`cw-gauge ${colorClass(widget, warning ? 'warnColor' : 'color')}`}>
      <svg viewBox="0 0 100 58" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
        <path className="cw-gauge-track" d="M 10 50 A 40 40 0 0 1 90 50" pathLength={100} />
        <path className="cw-gauge-fill" d="M 10 50 A 40 40 0 0 1 90 50" pathLength={100} strokeDasharray={`${fraction * 100} 100`} />
        <line className="cw-gauge-needle" x1="50" y1="50" x2="50" y2="18" style={{ transform: `rotate(${fraction * 180 - 90}deg)` }} />
        <circle className="cw-gauge-hub" cx="50" cy="50" r="3" />
      </svg>
      <span className="cw-value">{value === undefined ? '–' : formatNumber(value, 0)}{unit && <span className="cw-unit">{unit}</span>}</span>
    </div>
  )
}

function Text({ widget }: WidgetProps) {
  return <div className={`cw-label is-${textOption(widget, 'size')} is-${textOption(widget, 'align')} ${colorClass(widget)}`}>{textOption(widget, 'text')}</div>
}

/* ---- Imported widgets ---- */

/**
 * Hosts an imported widget: `mount` draws it once into a div, `update` tells it what changed (values, settings,
 * live or editing, size). The widget's own code is the user's, so a throw in it is shown in the widget, not let out.
 */
function ImportedView({ widget, io, preview }: WidgetProps) {
  const type = widgetType(widget.kind)
  const host = useRef<HTMLDivElement>(null)
  const instance = useRef<WidgetInstance | void>(undefined)
  const [failure, setFailure] = useState<string>()
  const latest = useRef({ widget, io })
  latest.current = { widget, io }

  useEffect(() => {
    const element = host.current
    if (!element || !type.mount) return undefined
    setFailure(undefined)
    try {
      instance.current = type.mount(element, {
        write: (slot, value) => {
          const { widget: current, io: link } = latest.current
          if (typeof value === 'number' && widgetType(current.kind).slots.some((entry) => entry.key === slot && entry.writes)) link.write(current.links[slot], value)
        },
      })
    } catch (cause) {
      setFailure(cause instanceof Error ? cause.message : String(cause))
    }
    return () => {
      try { instance.current?.destroy?.() } catch { /* the widget's own cleanup failing is not the app's problem */ }
      instance.current = undefined
      element.replaceChildren()
    }
  }, [type])

  const values: Record<string, number | undefined> = {}
  for (const slot of type.slots) values[slot.key] = io.read(widget.links[slot.key])
  const options: Record<string, WidgetOption> = {}
  for (const field of type.fields) options[field.key] = optionOf(widget, field.key)
  const linked: Record<string, boolean> = {}
  for (const slot of type.slots) linked[slot.key] = widget.links[slot.key] !== undefined
  const state: WidgetState = { values, options, name: widget.name ?? '', linked, live: io.live, preview, width: widget.w * CELL - 8, height: widget.h * CELL - 8 }
  const key = JSON.stringify(state)
  useEffect(() => {
    try {
      instance.current?.update?.(state)
    } catch (cause) {
      setFailure(cause instanceof Error ? cause.message : String(cause))
    }
    // The state is rebuilt on every render; only a change in what it says is worth telling the widget.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, type])

  return (
    <div className="cw-imported">
      <div ref={host} className="cw-imported-host" />
      {failure && <div className="cw-imported-error" role="alert">{failure}</div>}
    </div>
  )
}

function MissingView({ widget }: WidgetProps) {
  return <div className="cw-missing" title="Import the widget's .js file again to use it"><strong>{widget.kind}</strong><span>widget file not imported</span></div>
}

const VIEWS: Readonly<Record<string, (props: WidgetProps) => React.ReactElement>> = {
  joystick: Joystick, slider: Slider, switch: Switch, button: Hold, input: NumberInput, keyboard: KeyboardPad,
  display: Display, chart: Chart, lamp: Lamp, bar: Bar, gauge: Gauge, label: Text,
}

export function WidgetView(props: WidgetProps) {
  const { kind } = props.widget
  const View = isBuiltinKind(kind) ? VIEWS[kind]! : isMissingKind(kind) ? MissingView : ImportedView
  return <View {...props} />
}
