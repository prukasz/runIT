import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Crosshair, Minus, Plus } from 'lucide-react'
import { GRID, snap, zoomAt } from '../domain/canvas'
import type { Point, Viewport } from '../domain/canvas'
import type { BoardPlacement } from '../domain/project'
import { ART_SCALE, fitViewport, terminalAt } from './board/boardArt'
import type { BoardTerminal } from './board/boardArt'
import { mirrored, placements, placementTransform, rotated, sizeMm, SYMBOL_SCOPE, SYMBOL_UNITS_PER_MM, symbolOf } from './board/symbols'
import { BoardWires, distanceToPolyline, wirePoints } from './board/BoardWires'
import type { PendingWire } from './board/BoardWires'
import { useBoardModel } from './board/boardModel'
import { endpointAt, measureEndpoints } from './board/endpoints'
import type { EndpointPositions } from './board/endpoints'
import { installLink, installWith } from './board/installLinks'
import { useWiringReport } from './board/wiringReport'
import { BOARD_REF, configLinks, connectionProblem, describeEnd, endpointKey, FLOW_SHORT, flowOf, sameEndpoint, specOf } from '../domain/hardware'
import type { Endpoint } from '../domain/hardware'
import type { BoardWiring } from './useBoardWiring'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import { BOARD_SCOPE } from './board/runitBoard'
import '../canvas/Canvas.css'
import './Devices.css'

/*
 * The Board view's canvas (concept: board-canvas-concept.md): the runIT board, fixed with its top-left corner on the
 * world origin, on the always-on grid (the code canvas's GRID, one cell = 1 mm) that pans and zooms around the pointer.
 * The project's user devices are drawn from their generated symbols (a plain box without one): drag to move on the
 * grid, rotate / mirror the selected one (R / M or the toolbar). Hovering a terminal or pin names it. Press on a board
 * terminal or a device pin and drag to another to wire them (the toolbar's options choose what is highlighted); click a
 * wire to select it, Delete removes it. Connectors and the board editor are later phases.
 */

const BUTTON_ZOOM = 1.25
const WHEEL_ZOOM = 0.0015
const MAJOR = 5
const SYMBOL_SCALE = GRID / SYMBOL_UNITS_PER_MM
/** A press that moves less than this (px) is a click, not a drag. */
const CLICK_SLOP = 4
/** Pin names of picture variants show from this zoom on (always for the selected device). */
const LABELS_FROM_ZOOM = 1.3

type Drag =
  | { kind: 'pan'; at: Point; origin: Viewport }
  | { kind: 'device'; ref: string; at: Point; from: BoardPlacement; moved: boolean }
  | { kind: 'wire'; from: Endpoint }

/** A press this close (screen pixels) to a wire selects it. */
const WIRE_PICK = 6

export function BoardCanvas({ workspace: w, wiring }: { workspace: DevicesWorkspace; wiring: BoardWiring }) {
  const model = useBoardModel(w.board, w.catalog)
  const board = model.art
  const worldRef = useRef<HTMLDivElement>(null)
  const surfaceRef = useRef<HTMLDivElement>(null)
  const boardRef = useRef<HTMLDivElement>(null)
  const [viewport, setViewport] = useState<Viewport>({ x: 0, y: 0, zoom: 1 })
  const viewportRef = useRef(viewport)
  viewportRef.current = viewport
  const drag = useRef<Drag | undefined>(undefined)
  const [panning, setPanning] = useState(false)
  const [hover, setHover] = useState<string>()
  /** The placement of the device being dragged, until it is dropped. */
  const [moving, setMoving] = useState<{ ref: string; placement: BoardPlacement }>()
  const selectedRef = w.selection?.kind === 'device' ? w.selection.ref : undefined
  const report = useWiringReport(w, model)
  /** Where the wired pins are on the canvas, measured from the DOM. */
  const [positions, setPositions] = useState<EndpointPositions>(new Map())
  const [pending, setPending] = useState<PendingWire>()
  /** While a wire is dragged: the pins that can take it. */
  const [fits, setFits] = useState<EndpointPositions>(new Map())
  const [notice, setNotice] = useState<string>()

  const symbols = useMemo(() => new Map(w.devices.map((device) => [device.id, symbolOf(w.catalog.type(device.type), w.boardLayout?.[device.id]?.variant)])), [w.devices, w.catalog, w.boardLayout])
  const placed = useMemo(() => placements(w.devices, w.boardLayout, model.widthMm, (device) => sizeMm(symbols.get(device.id)!)), [w.devices, w.boardLayout, symbols, model.widthMm])

  const pendingFrom = pending?.from
  useLayoutEffect(() => {
    const world = worldRef.current
    if (!world) return
    const ends = [...w.wires.flatMap((wire) => [wire.a, wire.b]), ...(pendingFrom ? [pendingFrom] : [])]
    setPositions(measureEndpoints(world, ends, viewport.zoom))
  }, [w.wires, placed, moving, symbols, viewport.zoom, pendingFrom])

  /** A point of the screen in world pixels. */
  const worldPoint = (event: { clientX: number; clientY: number }): Point => {
    const rect = surfaceRef.current?.getBoundingClientRect()
    return { x: (event.clientX - (rect?.left ?? 0) - viewport.x) / viewport.zoom, y: (event.clientY - (rect?.top ?? 0) - viewport.y) / viewport.zoom }
  }

  /** The wire under a point (within a few screen pixels), if any. */
  const wireAt = (point: Point): string | undefined => {
    let best: { id: string; distance: number } | undefined
    for (const wire of w.wires) {
      const a = positions.get(endpointKey(wire.a))
      const b = positions.get(endpointKey(wire.b))
      if (!a || !b) continue
      const distance = distanceToPolyline(point, wirePoints(a, b))
      if (distance * viewport.zoom <= WIRE_PICK && (!best || distance < best.distance)) best = { id: wire.id, distance }
    }
    return best?.id
  }

  /** Every other pin that could take a wire from `from`: a fitting kind, role and signal. */
  const candidatesFor = (from: Endpoint): Endpoint[] => {
    const spec = specOf(report.context, from)
    if (!spec) return []
    const all: Endpoint[] = [...model.terminals.keys()].map((pin) => ({ device: BOARD_REF, pin }))
    for (const device of report.context.devices) for (const pin of device.pins.keys()) all.push({ device: device.ref, pin })
    return all.filter((other) => !sameEndpoint(other, from) && !!specOf(report.context, other) && connectionProblem(spec, specOf(report.context, other)!) === undefined)
  }

  /** A new wire fills a device's pin ref in its install frame while that field is unset (the details panel handles the rest). */
  const fillInstall = (from: Endpoint, to: Endpoint) => {
    for (const link of configLinks([{ id: 'new', a: from, b: to }], report.context)) {
      const entry = installLink(link, w.devices, w.catalog)
      if (entry?.state === 'unset') w.updateDevice(entry.device.id, { install: installWith(entry) })
    }
  }

  const fit = () => {
    const rect = surfaceRef.current?.getBoundingClientRect()
    if (rect) setViewport(fitViewport({ width: board.width * ART_SCALE, height: board.height * ART_SCALE }, rect))
  }
  useLayoutEffect(fit, [])

  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface) return
    const onWheel = (event: WheelEvent) => {
      event.preventDefault()
      const rect = surface.getBoundingClientRect()
      setViewport(zoomAt(viewportRef.current, Math.exp(-event.deltaY * WHEEL_ZOOM), { x: event.clientX - rect.left, y: event.clientY - rect.top }))
    }
    surface.addEventListener('wheel', onWheel, { passive: false })
    return () => surface.removeEventListener('wheel', onWheel)
  }, [])

  const zoomBy = (factor: number) => {
    const rect = surfaceRef.current?.getBoundingClientRect()
    if (rect) setViewport(zoomAt(viewport, factor, { x: rect.width / 2, y: rect.height / 2 }))
  }

  const place = (ref: string, change: (placement: BoardPlacement) => BoardPlacement) => {
    const current = placed.get(ref)
    if (current) w.setPlacement(ref, change(current))
  }

  const cell = GRID * viewport.zoom
  // The code canvas's dot grid: dots on the grid points, minor dots drop out when they would crowd.
  const layers = [{ size: cell * MAJOR, color: 'var(--canvas-dot-major)', radius: 1.4 }, ...(cell >= 8 ? [{ size: cell, color: 'var(--canvas-dot)', radius: 1 }] : [])]
  const grid: CSSProperties = {
    backgroundImage: layers.map((layer) => `radial-gradient(circle, ${layer.color} ${layer.radius}px, transparent ${layer.radius + 0.4}px)`).join(', '),
    backgroundSize: layers.map((layer) => `${layer.size}px ${layer.size}px`).join(', '),
    backgroundPosition: layers.map((layer) => `${viewport.x - layer.size / 2}px ${viewport.y - layer.size / 2}px`).join(', '),
  }

  const onPointerMove = (event: React.PointerEvent) => {
    const d = drag.current
    if (d?.kind === 'wire') {
      const under = document.elementFromPoint(event.clientX, event.clientY)
      const found = endpointAt(under, boardRef.current, report.pinsOf, model.terminals)
      const target = found && !sameEndpoint(found, d.from) ? found : undefined
      const from = specOf(report.context, d.from)
      const to = target && specOf(report.context, target)
      const problem = !target || !from || !to ? undefined : connectionProblem(from, to)
      setPending({ from: d.from, ...worldPoint(event), ...(target ? { target } : {}), ...(problem ? { problem } : {}) })
      setHover(problem ?? (target ? `Release to wire to ${describeEnd(report.context, target)}` : `Wiring from ${describeEnd(report.context, d.from)}`))
      return
    }
    if (d?.kind === 'pan') setViewport({ ...d.origin, x: d.origin.x + event.clientX - d.at.x, y: d.origin.y + event.clientY - d.at.y })
    else if (d?.kind === 'device') {
      const dx = event.clientX - d.at.x
      const dy = event.clientY - d.at.y
      if (!d.moved && Math.hypot(dx, dy) < CLICK_SLOP) return
      d.moved = true
      setMoving({ ref: d.ref, placement: { ...d.from, x: snap(d.from.x * GRID + dx / viewport.zoom) / GRID, y: snap(d.from.y * GRID + dy / viewport.zoom) / GRID } })
    } else if (event.target instanceof Element) {
      const owner = event.target.closest<HTMLElement>('[data-device]')
      const found: BoardTerminal | undefined = owner ? terminalAt(event.target, owner) : terminalAt(event.target, boardRef.current)
      const device = owner && w.devices.find((entry) => entry.id === owner.dataset.device)
      const pinEnd = endpointAt(event.target, boardRef.current, report.pinsOf, model.terminals)
      const pinSpec = pinEnd && specOf(report.context, pinEnd)
      setHover(found ? `${device ? `${device.name} · ` : ''}${found.id.replace(/^pin-/, '')}${found.title ? ` · ${found.title}` : ''}${pinSpec ? ` · ${FLOW_SHORT[flowOf(pinSpec)]}` : ''}` : device?.name)
    }
  }

  const endDrag = () => {
    const d = drag.current
    drag.current = undefined
    setPanning(false)
    if (d?.kind === 'device' && d.moved && moving) w.setPlacement(d.ref, moving.placement)
    if (d?.kind === 'wire') {
      if (pending?.target && !pending.problem) {
        const id = w.addWire(d.from, pending.target)
        if (id) {
          wiring.selectWire(id)
          fillInstall(d.from, pending.target)
          setNotice(`Wired ${describeEnd(report.context, d.from)} to ${describeEnd(report.context, pending.target)}.`)
        } else setNotice('Those two pins are already wired.')
      } else if (pending?.problem) setNotice(`Not wired: ${pending.problem}`)
      setPending(undefined)
      setFits(new Map())
    }
    setMoving(undefined)
  }

  return (
    <div className="canvas-editor board-canvas">
      <div
        ref={surfaceRef}
        className={`canvas-surface board-surface${panning ? ' is-panning' : ''}${viewport.zoom >= LABELS_FROM_ZOOM ? ' show-pin-labels' : ''}${pending ? ' is-wiring' : ''}${pending && wiring.availableOnly ? ' is-wiring-available' : ''}`}
        style={grid}
        tabIndex={0}
        onKeyDown={(event) => {
          if ((event.key === 'Delete' || event.key === 'Backspace') && wiring.selectedWire) {
            w.removeWire(wiring.selectedWire)
            wiring.selectWire(undefined)
            return
          }
          if (!selectedRef || event.ctrlKey || event.metaKey) return
          if (event.key === 'r' || event.key === 'R') place(selectedRef, rotated)
          else if (event.key === 'm' || event.key === 'M') place(selectedRef, mirrored)
        }}
        onPointerDown={(event) => {
          if (event.button !== 0) return
          event.currentTarget.setPointerCapture(event.pointerId)
          setNotice(undefined)
          // A press on a terminal or a device pin starts a wire; near a wire it selects it.
          const pin = endpointAt(event.target, boardRef.current, report.pinsOf, model.terminals)
          if (pin) {
            drag.current = { kind: 'wire', from: pin }
            setPending({ from: pin, ...worldPoint(event) })
            wiring.selectWire(undefined)
            const world = worldRef.current
            setFits(wiring.availableOnly && world ? measureEndpoints(world, candidatesFor(pin), viewport.zoom) : new Map())
            return
          }
          const wire = wireAt(worldPoint(event))
          if (wire) {
            wiring.selectWire(wire)
            return
          }
          wiring.selectWire(undefined)
          const at = { x: event.clientX, y: event.clientY }
          const owner = event.target instanceof Element ? event.target.closest<HTMLElement>('[data-device]') : null
          const from = owner?.dataset.device ? placed.get(owner.dataset.device) : undefined
          if (owner?.dataset.device && from) {
            drag.current = { kind: 'device', ref: owner.dataset.device, at, from, moved: false }
            w.select({ kind: 'device', ref: owner.dataset.device })
          } else {
            drag.current = { kind: 'pan', at, origin: viewport }
            setPanning(true)
            w.select(undefined)
          }
        }}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onPointerLeave={() => setHover(undefined)}
      >
        <div ref={worldRef} className="canvas-world" style={{ transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})` }}>
          <div ref={boardRef} className={`${BOARD_SCOPE} board-art-layer`} style={{ width: board.width, height: board.height, transform: `scale(${ART_SCALE})`, transformOrigin: '0 0' }} dangerouslySetInnerHTML={{ __html: board.markup }} />
          {w.devices.map((device) => {
            const art = symbols.get(device.id)!
            const placement = moving?.ref === device.id ? moving.placement : placed.get(device.id)!
            return (
              <div
                key={device.id}
                data-device={device.id}
                className={`board-device${selectedRef === device.id ? ' is-selected' : ''}${moving?.ref === device.id ? ' is-moving' : ''}`}
                style={{ width: art.width * SYMBOL_SCALE, height: art.height * SYMBOL_SCALE, transform: placementTransform(placement, GRID) }}
              >
                <div className={`${SYMBOL_SCOPE} board-device-art`} style={{ width: art.width, height: art.height, transform: `scale(${SYMBOL_SCALE})` }} dangerouslySetInnerHTML={{ __html: art.markup }} />
              </div>
            )
          })}
          <BoardWires wires={w.wires} positions={positions} report={report} wiring={wiring} selectedDevice={selectedRef} pending={pending} fits={fits} zoom={viewport.zoom} />
        </div>
        <div className="canvas-status"><span>{hover ?? notice ?? 'runIT board'}</span></div>
        <div className="canvas-zoom">
          <button type="button" aria-label="Zoom out" title="Zoom out" onClick={() => zoomBy(1 / BUTTON_ZOOM)}><Minus aria-hidden="true" /></button>
          <button type="button" className="canvas-zoom-value" title="Fit the board" onClick={fit}>{`${Math.round(viewport.zoom * 100)}%`}</button>
          <button type="button" aria-label="Zoom in" title="Zoom in" onClick={() => zoomBy(BUTTON_ZOOM)}><Plus aria-hidden="true" /></button>
          <button type="button" aria-label="Fit the board" title="Fit the board" onClick={fit}><Crosshair aria-hidden="true" /></button>
        </div>
      </div>
    </div>
  )
}
