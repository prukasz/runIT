import { useEffect, useMemo, useRef, useState } from 'react'
import type { DragEvent, KeyboardEvent, PointerEvent, WheelEvent } from 'react'
import { Maximize, Minus, Plus } from 'lucide-react'
import { ConnectorShape } from './ConnectorShape'
import { boxAround, centreOn, fitBox, leadOut, partTransform, placedSize, rulerCm, screenToCanvas, zoomAt } from './geometry'
import type { Viewport } from './geometry'
import type { Point } from './library'
import { svgUrl } from './library'
import { BOARD_ITEM, endKey } from './layout'
import { pinsNear } from './netlist'
import type { PinNode } from './netlist'
import { PartView, WireLines } from './PartView'
import { PinLoupe } from './PinLoupe'
import type { LoupeAt } from './PinLoupe'
import type { HardwareState } from './useHardware'
import { PART_DRAG_TYPE } from './useHardware'
import './Hardware.css'

/*
 * The Devices canvas: the board's drawing and the parts round it, at their true size (the unit is the millimetre, the ruler in
 * the corner says how long a centimetre is), with the wires between their pins. The board is the middle of it: the canvas opens
 * with the board in the middle of the screen (the first time fitted with the parts round it), Fit shows everything round the
 * board, the board itself is not moved (a drag on it pans), and a canvas that changes size (a panel opened, the window) keeps
 * its middle where it was. Pan (drag the background), zoom (wheel, the buttons), a part from the palette is placed with a click
 * or dropped. A part is dragged to move it, R or Space turns it (the board
 * too, and the part being placed) a quarter right about its centre, Shift+R or Shift+Space left, as the action bar's two buttons do; Delete removes
 * what is selected; a double-click on a part or a wire opens its details. The bar at the foot is the view's only (zoom, fit):
 * the tools are in the action bar. Wiring needs no exact aim: a tap within REACH_PX of a pin opens the magnifier there
 * (PinLoupe.tsx), the spot drawn large with every pin named, and a click in it picks a pin. A picked pin draws a wire to the
 * pointer, every pin it may be wired to lights green, the others dim (their tooltip says why); a tap near one of those, or
 * anywhere on a part (the magnifier at its nearest pin), opens the magnifier again to choose the end; so does a drag from a pin
 * dropped near another.
 */

/** The zoom (px per mm) at which a part is its true size on a 96 dpi screen: the zoom shown as 100 %. */
const TRUE_SIZE = 96 / 25.4

/** How far from the pointer (screen px) a tap reaches a pin. */
const REACH_PX = 16

type Drag =
  | { readonly kind: 'pan'; readonly start: Point; readonly view: Viewport; moved: boolean; readonly item?: string }
  | { readonly kind: 'item'; readonly id: string; readonly start: Point; readonly origin: Point; moved: boolean }
  | { readonly kind: 'wire'; readonly near: readonly PinNode[]; readonly start: Point; moved: boolean }
  | { readonly kind: 'line'; readonly id: string }

export function HardwareCanvas({ state, onOpenDetails }: { state: HardwareState; onOpenDetails?: () => void }) {
  const box = useRef<HTMLDivElement>(null)
  const drag = useRef<Drag | undefined>(undefined)
  const [pointer, setPointer] = useState<Point | undefined>()
  const [wireEnd, setWireEnd] = useState<Point | undefined>()
  const [loupe, setLoupe] = useState<LoupeAt | undefined>()
  // What the magnifier shows (mm): framed on the canvas.
  const [lens, setLens] = useState<{ readonly centre: Point; readonly radius: number } | undefined>()
  const { net, layout } = state
  const view = state.viewport ?? { x: 40, y: 40, zoom: 4 }

  // The middle of the canvas: the board's centre (it turns about it), else the middle of the parts.
  const boxes = useMemo(() => net.parts.map((p) => ({ x: p.x, y: p.y, ...placedSize(p.part, p.rotation) })), [net.parts])
  const board = boxes[net.parts.findIndex((p) => p.id === BOARD_ITEM)]
  const middle = board ? { x: board.x + board.w / 2, y: board.y + board.h / 2 }
    : boxes.length ? { x: (Math.min(...boxes.map((b) => b.x)) + Math.max(...boxes.map((b) => b.x + b.w))) / 2, y: (Math.min(...boxes.map((b) => b.y)) + Math.max(...boxes.map((b) => b.y + b.h))) / 2 }
    : { x: 0, y: 0 }
  const screen = () => {
    const r = box.current?.getBoundingClientRect()
    return r && r.width > 0 ? { w: r.width, h: r.height } : undefined
  }
  /** Everything on the canvas, the board in the middle. */
  const fit = () => {
    const size = screen()
    if (size) state.setViewport(fitBox(boxAround(middle, boxes.length ? boxes : [{ x: -50, y: -50, w: 100, h: 100 }]), size, 40))
  }
  // Opened: the board in the middle (the zoom kept from the last time, or everything fitted the first time).
  useEffect(() => {
    const size = screen()
    if (size && state.viewport) state.setViewport(centreOn(state.viewport, middle, size))
    else fit()
  }, [])
  // A canvas that changes size keeps its middle where it was.
  const viewRef = useRef(view)
  viewRef.current = view
  useEffect(() => {
    const el = box.current
    if (!el) return
    let last = { w: el.clientWidth, h: el.clientHeight }
    const observer = new ResizeObserver(() => {
      const now = { w: el.clientWidth, h: el.clientHeight }
      if (now.w === last.w && now.h === last.h) return
      // Steps of one resize can come before the next render: each starts from the view the last one set.
      const v = viewRef.current
      viewRef.current = { ...v, x: v.x + (now.w - last.w) / 2, y: v.y + (now.h - last.h) / 2 }
      state.setViewport(viewRef.current)
      last = now
    })
    observer.observe(el)
    return () => observer.disconnect()
  }, [])
  // A part picked in the palette is placed here: the keys (R, Esc) come to the canvas.
  useEffect(() => { if (state.armed) box.current?.focus() }, [state.armed])

  const local = (e: { clientX: number; clientY: number }): Point => {
    const r = box.current!.getBoundingClientRect()
    return { x: e.clientX - r.left, y: e.clientY - r.top }
  }
  const toMm = (e: { clientX: number; clientY: number }) => screenToCanvas(view, local(e))
  const hot = useMemo(() => new Set(layout.wires.flatMap((w) => [endKey(w.a), endKey(w.b)])), [layout.wires])
  const reach = (at: Point) => pinsNear(net, at, { radius: REACH_PX / view.zoom })

  const closeLoupe = () => {
    setLoupe(undefined)
    box.current?.focus()
  }
  const choose = (node: PinNode) => {
    closeLoupe()
    state.tapPin(node)
  }

  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return
    const target = e.target as Element
    setLoupe(undefined)
    box.current?.focus()
    const near = state.armed ? [] : reach(toMm(e))
    const item = target.closest('[data-item]')?.getAttribute('data-item')
    const line = target.closest('[data-wire]')?.getAttribute('data-wire')
    box.current?.setPointerCapture(e.pointerId)
    if (near.length > 0) {
      drag.current = { kind: 'wire', near, start: local(e), moved: false }
    } else if (line) {
      drag.current = { kind: 'line', id: line }
    } else if (item && item !== BOARD_ITEM && !state.armed) {
      const placed = net.parts.find((p) => p.id === item)!
      drag.current = { kind: 'item', id: item, start: toMm(e), origin: { x: placed.x, y: placed.y }, moved: false }
    } else {
      drag.current = { kind: 'pan', start: local(e), view, moved: false, ...(item && !state.armed ? { item } : {}) }
    }
  }
  const onPointerMove = (e: PointerEvent<HTMLDivElement>) => {
    const d = drag.current
    if (state.armed || state.picked) setPointer(toMm(e))
    if (!d) return
    if (d.kind === 'pan') {
      const p = local(e)
      if (Math.hypot(p.x - d.start.x, p.y - d.start.y) > 3) d.moved = true
      if (d.moved) state.setViewport({ ...d.view, x: d.view.x + p.x - d.start.x, y: d.view.y + p.y - d.start.y })
    } else if (d.kind === 'item') {
      const p = toMm(e)
      if (!d.moved && Math.hypot(p.x - d.start.x, p.y - d.start.y) * view.zoom < 4) return
      d.moved = true
      // Half a millimetre is the finest step: fine enough to line up, coarse enough to land where it is meant.
      state.move(d.id, { x: Math.round((d.origin.x + p.x - d.start.x) * 2) / 2, y: Math.round((d.origin.y + p.y - d.start.y) * 2) / 2 })
    } else if (d.kind === 'wire') {
      const p = local(e)
      if (!d.moved && Math.hypot(p.x - d.start.x, p.y - d.start.y) > 5) {
        d.moved = true
        state.pick(d.near[0]!.key)
      }
      if (d.moved) setWireEnd(toMm(e))
    }
  }
  /** A tap on a part (or the board): selects it; with a wire on its way, the magnifier at its pin nearest the tap, to end it on. */
  const tapItem = (id: string, e: PointerEvent<HTMLDivElement>) => {
    const nearest = state.picked ? pinsNear(net, toMm(e), { item: id })[0] : undefined
    if (nearest) return setLoupe({ centre: nearest.at, anchor: local(e) })
    state.select({ kind: 'item', id })
  }
  const onPointerUp = (e: PointerEvent<HTMLDivElement>) => {
    const d = drag.current
    drag.current = undefined
    setWireEnd(undefined)
    if (!d) return
    if (d.kind === 'pan' && !d.moved && d.item) {
      tapItem(d.item, e)
    } else if (d.kind === 'pan' && !d.moved) {
      if (state.armed) {
        const part = state.library.get(state.armed)
        if (part) state.place(part, toMm(e))
      } else {
        state.select(undefined)
        state.pick(undefined)
        state.setNote(undefined)
      }
    } else if (d.kind === 'item' && !d.moved) {
      tapItem(d.id, e)
    } else if (d.kind === 'line') {
      state.select({ kind: 'wire', id: d.id })
      state.pick(undefined)
    } else if (d.kind === 'wire') {
      if (!d.moved) return setLoupe({ centre: toMm(e), anchor: local(e) })
      // Dropped: the magnifier there when pins are in reach, else at the nearest pin of the part under it.
      const at = toMm(e)
      const item = document.elementFromPoint(e.clientX, e.clientY)?.closest('[data-item]')?.getAttribute('data-item')
      const centre = reach(at).length > 0 ? at : item ? pinsNear(net, at, { item })[0]?.at : undefined
      if (centre) setLoupe({ centre, anchor: local(e) })
      else state.pick(undefined)
    }
  }
  const onWheel = (e: WheelEvent<HTMLDivElement>) => state.setViewport(zoomAt(view, e.deltaY < 0 ? 1.12 : 1 / 1.12, local(e)))
  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    if (e.key === 'Escape') { setLoupe(undefined); state.pick(undefined); state.arm(undefined); state.select(undefined) }
    if ((e.key === 'Delete' || e.key === 'Backspace') && state.selection) { e.preventDefault(); state.remove() }
    if ((e.key === 'r' || e.key === 'R' || e.key === ' ') && !e.ctrlKey && !e.metaKey && !e.altKey) { e.preventDefault(); state.turnSelected(e.shiftKey ? -90 : 90) }
  }
  const onDrop = (e: DragEvent<HTMLDivElement>) => {
    const id = e.dataTransfer.getData(PART_DRAG_TYPE)
    const part = id ? state.library.get(id) : undefined
    if (!part) return
    e.preventDefault()
    state.place(part, toMm(e))
  }

  const zoomBy = (factor: number) => {
    const r = box.current?.getBoundingClientRect()
    state.setViewport(zoomAt(view, factor, { x: (r?.width ?? 0) / 2, y: (r?.height ?? 0) / 2 }))
  }
  const armedPart = state.armed ? state.library.get(state.armed) : undefined
  const cm = rulerCm(view.zoom)
  const room = box.current?.getBoundingClientRect()

  return (
    <div ref={box} className={`hw-canvas ${state.armed ? 'is-placing' : ''} ${state.picked ? 'is-picking' : ''}`.trim()} tabIndex={0}
      onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onWheel={onWheel} onKeyDown={onKeyDown}
      onDoubleClick={() => { if (state.selection) onOpenDetails?.() }}
      onDragOver={(e) => { if (e.dataTransfer.types.includes(PART_DRAG_TYPE)) e.preventDefault() }} onDrop={onDrop}>
      <svg className="hw-svg" width="100%" height="100%">
        <defs>
          <pattern id="hw-grid" width={10 * view.zoom} height={10 * view.zoom} patternUnits="userSpaceOnUse" x={view.x} y={view.y}>
            <path d={`M ${10 * view.zoom} 0 L 0 0 0 ${10 * view.zoom}`} className="hw-grid-line" />
          </pattern>
        </defs>
        <rect width="100%" height="100%" fill="url(#hw-grid)" />
        <g transform={`translate(${view.x} ${view.y}) scale(${view.zoom})`}>
          {net.parts.map((p) => <PartView key={p.id} placed={p} state={state} zoom={view.zoom} hot={hot} minHit={7 / view.zoom} />)}
          <WireLines state={state} zoom={view.zoom} />
          {state.picked && (wireEnd ?? pointer) && !loupe && (() => {
            const end = wireEnd ?? pointer!
            return <line className="hw-wire-draft" x1={state.picked.at.x} y1={state.picked.at.y} x2={end.x} y2={end.y} />
          })()}
          {loupe && lens && <rect className="hw-loupe-spot" x={lens.centre.x - lens.radius} y={lens.centre.y - lens.radius} width={2 * lens.radius} height={2 * lens.radius} rx={0.6} strokeWidth={2.5 / view.zoom} />}
          {armedPart && pointer && (() => {
            // The part itself, turned as it will be placed, under the pointer.
            const size = placedSize(armedPart, state.armedTurn)
            const at = { x: pointer.x - size.w / 2, y: pointer.y - size.h / 2, rotation: state.armedTurn }
            return (
              <g className="hw-ghost" transform={partTransform(armedPart, at)}>
                <image href={svgUrl(armedPart.svg)} x={0} y={0} width={armedPart.size_mm.w} height={armedPart.size_mm.h} preserveAspectRatio="none" />
                {armedPart.connectors.map((c) => <ConnectorShape key={c.id} c={c} out={leadOut(armedPart, c)} />)}
                <rect x={0} y={0} width={armedPart.size_mm.w} height={armedPart.size_mm.h} rx={1} />
              </g>
            )
          })()}
        </g>
      </svg>
      {net.parts.length === 0 && <div className="hw-hint">This board has no drawing in the library yet: make one with the creator (the pen).</div>}
      {state.armed && <div className="hw-hint is-top">Click where {armedPart?.name ?? 'the part'} goes · R or Space turns it · Esc puts it back</div>}
      {state.picked && !state.armed && !loupe && <div className="hw-hint is-top">Wire from {state.picked.pin.label}: click near a green pin or on a part · Esc lets it go</div>}
      {loupe && <PinLoupe state={state} hot={hot} at={loupe} room={{ w: room?.width ?? 0, h: room?.height ?? 0 }} onChoose={choose} onClose={closeLoupe} onView={(centre, radius) => setLens({ centre, radius })} />}
      <div className="hw-ruler" aria-label={`Ruler: ${cm} cm`}>
        <div className="hw-ruler-bar" style={{ width: cm * 10 * view.zoom }} />
        <span>{cm < 1 ? `${cm * 10} mm` : `${cm} cm`}</span>
      </div>
      {state.note && <div className="hw-note" role="status" onClick={() => state.setNote(undefined)}>{state.note}</div>}
      <div className="hw-tools" onPointerDown={(e) => e.stopPropagation()}>
        <button aria-label="Zoom out" onClick={() => zoomBy(1 / 1.25)}><Minus aria-hidden="true" /></button>
        <button className="hw-zoom-value" title="Back to 100 %: the true size on the screen" onClick={() => zoomBy(TRUE_SIZE / view.zoom)}>{Math.round((view.zoom / TRUE_SIZE) * 100)} %</button>
        <button aria-label="Zoom in" onClick={() => zoomBy(1.25)}><Plus aria-hidden="true" /></button>
        <button aria-label="Fit" title="Show everything, the board in the middle" onClick={fit}><Maximize aria-hidden="true" /></button>
      </div>
    </div>
  )
}
