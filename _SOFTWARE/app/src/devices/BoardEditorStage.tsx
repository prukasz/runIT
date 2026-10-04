import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Crosshair, Minus, Plus } from 'lucide-react'
import { connectorBounds, connectorCorners, connectorType, pinAt, pinLabel, pinPosition, pointInConnector } from '../domain/board'
import type { Point as MmPoint } from '../domain/board'
import { GRID, zoomAt } from '../domain/canvas'
import type { Point, Viewport } from '../domain/canvas'
import { Button } from '../components/Button'
import { describeBinding } from './board/boardModel'
import { fitViewport } from './board/boardArt'
import { BOARD_SCOPE } from './board/runitBoard'
import type { BoardEditorApi } from './useBoardEditor'
import '../canvas/Canvas.css'
import './Devices.css'

/*
 * The board creator's stage: the draft board at true millimetres (one grid cell, 20 px at 100%, is one millimetre) with
 * its connectors. Press a connector to select it (and the pin under the pointer) and drag it, snapped to the chosen
 * step; drag the background to pan, wheel to zoom about the pointer. R / M turn and mirror, Delete removes, arrows nudge.
 */

const BUTTON_ZOOM = 1.25
const WHEEL_ZOOM = 0.0015
const MAJOR = 5
const CLICK_SLOP = 4
/** How near (millimetres) the pointer must be to a pin to take it. */
const PIN_REACH = 1.3

type Drag =
  | { kind: 'pan'; at: Point; origin: Viewport }
  | { kind: 'connector'; id: string; start: MmPoint; from: MmPoint; moved: boolean; at: Point }

export function BoardEditorStage({ editor: e, catalog }: { editor: BoardEditorApi; catalog: Parameters<typeof describeBinding>[1] }) {
  const surfaceRef = useRef<HTMLDivElement>(null)
  const [viewport, setViewport] = useState<Viewport>({ x: 0, y: 0, zoom: 1 })
  const viewportRef = useRef(viewport)
  viewportRef.current = viewport
  const drag = useRef<Drag | undefined>(undefined)
  const [panning, setPanning] = useState(false)
  const [hover, setHover] = useState<string>()
  const { draft, model } = e

  const fit = () => {
    const rect = surfaceRef.current?.getBoundingClientRect()
    if (rect && draft) setViewport(fitViewport({ width: draft.width * GRID, height: draft.height * GRID }, rect))
  }
  useLayoutEffect(fit, [draft?.id, draft ? draft.width : 0, draft ? draft.height : 0]) // eslint-disable-line react-hooks/exhaustive-deps

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
  }, [draft === undefined])

  if (!draft || !model) {
    return (
      <div className="board-editor-empty">
        <h3>Board creator</h3>
        <p className="devices-muted">The project uses the built-in runIT board. Make a board of your own from premade connectors: screw terminals, pin headers, XT60, USB-C. Each connector pin binds to a power net, a bus signal or an installed sys pin, and then wires like any terminal.</p>
        <div className="board-editor-empty-actions">
          <Button variant="primary" onClick={() => e.createBoard()}>Create a board</Button>
        </div>
        {e.library.length > 0 && (
          <div className="board-editor-library">
            <h4>From the library</h4>
            {e.library.map((entry) => <Button key={entry.file} size="sm" onClick={() => e.openFromLibrary(entry.file)}>{entry.template.name}</Button>)}
          </div>
        )}
      </div>
    )
  }

  const cell = GRID * viewport.zoom
  const layers = [{ size: cell * MAJOR, color: 'var(--canvas-dot-major)', radius: 1.4 }, ...(cell >= 8 ? [{ size: cell, color: 'var(--canvas-dot)', radius: 1 }] : [])]
  const grid: CSSProperties = {
    backgroundImage: layers.map((layer) => `radial-gradient(circle, ${layer.color} ${layer.radius}px, transparent ${layer.radius + 0.4}px)`).join(', '),
    backgroundSize: layers.map((layer) => `${layer.size}px ${layer.size}px`).join(', '),
    backgroundPosition: layers.map((layer) => `${viewport.x - layer.size / 2}px ${viewport.y - layer.size / 2}px`).join(', '),
  }

  const mm = (event: { clientX: number; clientY: number }): MmPoint => {
    const rect = surfaceRef.current?.getBoundingClientRect()
    return { x: (event.clientX - (rect?.left ?? 0) - viewport.x) / viewport.zoom / GRID, y: (event.clientY - (rect?.top ?? 0) - viewport.y) / viewport.zoom / GRID }
  }
  const topConnector = (point: MmPoint) => [...draft.connectors].reverse().find((connector) => pointInConnector(connector, point, 0.4))

  const onPointerMove = (event: React.PointerEvent) => {
    const d = drag.current
    if (d?.kind === 'pan') {
      setViewport({ ...d.origin, x: d.origin.x + event.clientX - d.at.x, y: d.origin.y + event.clientY - d.at.y })
      return
    }
    const here = mm(event)
    if (d?.kind === 'connector') {
      if (!d.moved && Math.hypot(event.clientX - d.at.x, event.clientY - d.at.y) < CLICK_SLOP) return
      d.moved = true
      e.moveConnector(d.id, d.from.x + (here.x - d.start.x), d.from.y + (here.y - d.start.y))
      return
    }
    const over = topConnector(here)
    const pin = over ? pinAt(over, here, PIN_REACH) : undefined
    const type = over && connectorType(over.type)
    setHover(over && type ? `${over.label || `${type.label} ${over.id}`}${pin !== undefined ? ` · pin ${pin + 1}: ${pinLabel(over, pin)} · ${describeBinding(over.pins[pin].bind, catalog, model.nets)}` : ''}` : undefined)
  }

  const endDrag = () => {
    drag.current = undefined
    setPanning(false)
  }

  const selected = e.selected
  const selectedType = selected && connectorType(selected.type)

  return (
    <div className="canvas-editor board-canvas board-editor-stage">
      <div
        ref={surfaceRef}
        className={`canvas-surface board-surface${panning ? ' is-panning' : ''}`}
        style={grid}
        tabIndex={0}
        onPointerDown={(event) => {
          if (event.button !== 0) return
          event.currentTarget.setPointerCapture(event.pointerId)
          const at = { x: event.clientX, y: event.clientY }
          const point = mm(event)
          const hit = topConnector(point)
          if (hit) {
            e.setSelection({ connector: hit.id, pin: pinAt(hit, point, PIN_REACH) })
            drag.current = { kind: 'connector', id: hit.id, start: point, from: { x: hit.x, y: hit.y }, moved: false, at }
          } else {
            e.setSelection({})
            drag.current = { kind: 'pan', at, origin: viewport }
            setPanning(true)
          }
        }}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        onPointerLeave={() => setHover(undefined)}
      >
        <div className="canvas-world" style={{ transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})` }}>
          <div className={`${BOARD_SCOPE} board-art-layer`} style={{ width: model.art.width, height: model.art.height, transformOrigin: '0 0' }} dangerouslySetInnerHTML={{ __html: model.art.markup }} />
          <svg className="board-editor-overlay" width="1" height="1" aria-hidden="true">
            {draft.connectors.map((connector) => {
              const type = connectorType(connector.type)
              if (!type) return null
              const isSelected = connector.id === selected?.id
              return (
                <g key={connector.id} className={isSelected ? 'is-selected' : ''}>
                  {isSelected && <polygon className="board-editor-outline" points={connectorCorners(connector).map((corner) => `${corner.x * GRID},${corner.y * GRID}`).join(' ')} />}
                  {connector.pins.map((pin, index) => {
                    const at = pinPosition(connector, index)
                    if (!at) return null
                    const bound = pin.bind.type !== 'none'
                    const chosen = isSelected && e.selection.pin === index
                    return <circle key={index} className={`board-editor-pin${bound ? ' is-bound' : ''}${chosen ? ' is-chosen' : ''}`} cx={at.x * GRID} cy={at.y * GRID} r={(chosen ? 1.25 : 1) * GRID * 0.5} />
                  })}
                </g>
              )
            })}
            {selected && selectedType && (() => {
              const bounds = connectorBounds(selected)
              return <text className="board-editor-tag" x={bounds.left * GRID} y={(bounds.top - 0.7) * GRID} fontSize={GRID * 0.9}>{selected.label || `${selectedType.label} ${selected.id}`}</text>
            })()}
          </svg>
        </div>
        <div className="canvas-status"><span>{hover ?? `${draft.name} · ${draft.width} × ${draft.height} mm · ${draft.connectors.length} connectors${e.dirty ? ' · unsaved' : ''}`}</span></div>
        <div className="canvas-zoom">
          <button type="button" aria-label="Zoom out" title="Zoom out" onClick={() => { const rect = surfaceRef.current?.getBoundingClientRect(); if (rect) setViewport(zoomAt(viewport, 1 / BUTTON_ZOOM, { x: rect.width / 2, y: rect.height / 2 })) }}><Minus aria-hidden="true" /></button>
          <button type="button" className="canvas-zoom-value" title="Fit the board" onClick={fit}>{`${Math.round(viewport.zoom * 100)}%`}</button>
          <button type="button" aria-label="Zoom in" title="Zoom in" onClick={() => { const rect = surfaceRef.current?.getBoundingClientRect(); if (rect) setViewport(zoomAt(viewport, BUTTON_ZOOM, { x: rect.width / 2, y: rect.height / 2 })) }}><Plus aria-hidden="true" /></button>
          <button type="button" aria-label="Fit the board" title="Fit the board" onClick={fit}><Crosshair aria-hidden="true" /></button>
        </div>
      </div>
    </div>
  )
}
