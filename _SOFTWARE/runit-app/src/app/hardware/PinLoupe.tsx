import { useEffect, useRef, useState } from 'react'
import type { PointerEvent, WheelEvent } from 'react'
import { X } from 'lucide-react'
import { pinRadius, pitchOf } from './geometry'
import type { Point } from './library'
import { pinsNear } from './netlist'
import type { PinNode } from './netlist'
import { PartView, WireLines } from './PartView'
import type { HardwareState } from './useHardware'

/*
 * The magnifier of the Devices canvas: a tap near pins opens it beside the tap, the scene round that spot drawn large, every pin
 * in it named and coloured by whether it may take the picked pin's wire (green) or not (dimmed; its tooltip says why). A click
 * on a pin, or near one, chooses it; the wheel zooms the magnifier, a drag moves what it shows; Esc or the cross closes it.
 */

/** The magnifier's side (screen px). */
const LOUPE_PX = 300
/** How much of the canvas it shows round the tap, at first (mm from the centre to the edge): at least this, and about five
 * pins of the connector nearest the tap (a screw terminal's are twice a goldpin's apart). */
const LOUPE_MM = 6
const LOUPE_PINS = 2.5
const GAP_PX = 24
const HEAD_PX = 30

export interface LoupeAt {
  /** The spot it magnifies (mm on the canvas). */
  readonly centre: Point
  /** The tap (px in the canvas): the magnifier opens beside it. */
  readonly anchor: Point
}

export function PinLoupe({ state, hot, at, room, onChoose, onClose, onView }: {
  state: HardwareState
  hot: ReadonlySet<string>
  at: LoupeAt
  /** The canvas's size (px): the magnifier stays inside it. */
  room: { w: number; h: number }
  onChoose: (node: PinNode) => void
  onClose: () => void
  /** What it shows now (mm on the canvas): the canvas frames it. */
  onView?: (centre: Point, radius: number) => void
}) {
  const root = useRef<HTMLDivElement>(null)
  const firstRadius = (centre: Point) => {
    const nearest = pinsNear(state.net, centre)[0]
    return Math.max(LOUPE_MM, nearest ? pitchOf(nearest.connector) * LOUPE_PINS : 0)
  }
  const [radius, setRadius] = useState(() => firstRadius(at.centre))
  const [centre, setCentre] = useState(at.centre)
  const pan = useRef<{ start: Point; from: Point; moved: boolean } | undefined>(undefined)
  useEffect(() => { setCentre(at.centre); setRadius(firstRadius(at.centre)) }, [at.centre.x, at.centre.y])
  useEffect(() => { root.current?.focus() }, [])
  useEffect(() => { onView?.(centre, radius) }, [centre.x, centre.y, radius])

  const scale = LOUPE_PX / (2 * radius)
  const inView = pinsNear(state.net, centre, { radius: radius * Math.SQRT2 })
    .filter((n) => Math.abs(n.at.x - centre.x) <= radius && Math.abs(n.at.y - centre.y) <= radius)

  // Beside the tap: right of it when there is room, else left; level with it, kept inside the canvas.
  const height = LOUPE_PX + HEAD_PX + 2
  const right = at.anchor.x + GAP_PX + LOUPE_PX + 8 <= room.w
  const left = Math.max(8, Math.min(right ? at.anchor.x + GAP_PX : at.anchor.x - GAP_PX - LOUPE_PX, room.w - LOUPE_PX - 8))
  const top = Math.max(8, Math.min(at.anchor.y - height / 2, room.h - height - 8))

  /** The canvas point (mm) under a pointer in the magnifier. */
  const toMm = (e: { clientX: number; clientY: number }, svg: Element): Point => {
    const r = svg.getBoundingClientRect()
    return { x: centre.x - radius + (e.clientX - r.left) / scale, y: centre.y - radius + (e.clientY - r.top) / scale }
  }
  const onPointerDown = (e: PointerEvent<SVGSVGElement>) => {
    e.currentTarget.setPointerCapture(e.pointerId)
    pan.current = { start: { x: e.clientX, y: e.clientY }, from: centre, moved: false }
  }
  const onPointerMove = (e: PointerEvent<SVGSVGElement>) => {
    const p = pan.current
    if (!p) return
    if (!p.moved && Math.hypot(e.clientX - p.start.x, e.clientY - p.start.y) < 4) return
    p.moved = true
    setCentre({ x: p.from.x - (e.clientX - p.start.x) / scale, y: p.from.y - (e.clientY - p.start.y) / scale })
  }
  const onPointerUp = (e: PointerEvent<SVGSVGElement>) => {
    const p = pan.current
    pan.current = undefined
    if (!p || p.moved) return
    // The pin clicked, or the nearest within a pin's reach of the click.
    const at = toMm(e, e.currentTarget)
    const node = pinsNear(state.net, at, { radius: Math.max(2.3, 12 / scale) }).find((n) => Math.hypot(n.at.x - at.x, n.at.y - at.y) <= Math.max(pinRadius(n.connector), 12 / scale))
    if (!node) return
    const verdict = state.picked && node.key !== state.picked.key ? state.verdicts?.get(node.key) : undefined
    if (verdict && !verdict.ok) return state.setNote(verdict.why)
    onChoose(node)
  }
  const onWheel = (e: WheelEvent<HTMLDivElement>) => {
    e.stopPropagation()
    setRadius((r) => Math.min(40, Math.max(2, r * (e.deltaY < 0 ? 1 / 1.15 : 1.15))))
  }

  const title = state.picked ? `Wire ${state.picked.pin.label} to…` : 'Wire from…'
  return (
    <div ref={root} className={`hw-loupe ${state.picked ? 'is-picking' : ''}`.trim()} role="dialog" aria-label={title} tabIndex={-1} style={{ left, top, width: LOUPE_PX + 2 }}
      onPointerDown={(e) => e.stopPropagation()} onDoubleClick={(e) => e.stopPropagation()} onWheel={onWheel}
      onKeyDown={(e) => { e.stopPropagation(); if (e.key === 'Escape') onClose() }}>
      <div className="hw-loupe-head">
        <span>{title} <em>click a pin · wheel zooms · drag moves</em></span>
        <button type="button" aria-label="Close" title="Close (Esc)" onClick={onClose}><X aria-hidden="true" /></button>
      </div>
      <svg style={{ width: LOUPE_PX, height: LOUPE_PX }} viewBox={`${centre.x - radius} ${centre.y - radius} ${2 * radius} ${2 * radius}`}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp}>
        {state.net.parts.map((p) => <PartView key={p.id} placed={p} state={state} zoom={scale} hot={hot} />)}
        <WireLines state={state} zoom={scale} />
        {inView.map((n) => {
          const verdict = state.picked ? state.verdicts?.get(n.key) : undefined
          const tone = state.picked?.key === n.key ? 'is-picked' : verdict ? (verdict.ok ? 'is-ok' : 'is-no') : ''
          return <text key={n.key} className={`hw-loupe-label ${tone}`.trim()} x={n.at.x} y={n.at.y - pinRadius(n.connector) - 3 / scale} fontSize={12 / scale} strokeWidth={3 / scale}>{n.pin.label}</text>
        })}
      </svg>
    </div>
  )
}
