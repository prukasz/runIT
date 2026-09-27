import type { DeviceCatalog } from '../domain/descriptors'
import { useEffect, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Crosshair, Minus, Plus } from 'lucide-react'
import { canvasToScreen, GRID, screenToCanvas, snapPoint, zoomAt } from '../domain/canvas'
import type { Point, Viewport } from '../domain/canvas'
import type { Diagnostic } from '../domain/compiler'
import type { ProjectDevice } from '../domain/project'
import { runitVmCatalog } from '../domain/descriptors'
import { BLOCK_DRAG_TYPE } from './BlockPalette'
import { CanvasBlockView } from './CanvasBlockView'
import { blockShape } from './blockView'
import type { CanvasWorkspace } from './useCanvasWorkspace'

/*
 * The open canvas: a grid that pans (drag the background, one finger) and
 * zooms (wheel, pinch, the buttons) around the pointer, and its blocks in
 * the world layer, which carries the viewport transform. Blocks dropped from
 * the palette show where they will land first; with snap on, the grid point
 * the pointer is nearest is marked. A click on the background clears the
 * selection; Delete removes the selected block.
 */

/** Every MAJOR-th grid line is drawn stronger. */
const MAJOR = 5
const WHEEL_ZOOM = 0.0015
const BUTTON_ZOOM = 1.25
/** Where a dropped block's corner sits relative to the pointer: the pointer holds its header. */
const GRAB = { x: 24, y: 14 }
/** A press that moves less than this is a click. */
const CLICK_SLOP = 4

const isEditing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || !!target.closest('input, textarea, select, [contenteditable="true"]'))

export function CanvasSurface({ workspace, showGrid, diagnostics, devices = [], deviceCatalog }: { workspace: CanvasWorkspace; showGrid: boolean; diagnostics: ReadonlyMap<string, readonly Diagnostic[]>; devices?: readonly ProjectDevice[]; deviceCatalog?: DeviceCatalog }) {
  const { active, snap, selectedBlock } = workspace
  const catalog = runitVmCatalog()
  const [ghost, setGhost] = useState<{ at: Point; type?: string }>()
  const pressAt = useRef<Point | undefined>(undefined)
  const surfaceRef = useRef<HTMLDivElement>(null)
  // The viewport lives here while it moves; the workspace keeps it per canvas.
  const [viewport, setViewportState] = useState<Viewport>(workspace.viewport)
  const viewportRef = useRef(viewport)
  const setViewport = (next: Viewport) => {
    viewportRef.current = next
    setViewportState(next)
  }
  const pointers = useRef(new Map<number, Point>())
  const [panning, setPanning] = useState(false)
  const [cursor, setCursor] = useState<Point>()
  const saveTimer = useRef<number | undefined>(undefined)

  // Another canvas opened: show where it was left.
  useEffect(() => {
    setViewport(workspace.viewport)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active?.id])

  // The middle of the view, for blocks placed by a click in the palette.
  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface || typeof ResizeObserver === 'undefined') return
    const observer = new ResizeObserver(() => { workspace.surfaceSize.current = { width: surface.clientWidth, height: surface.clientHeight } })
    observer.observe(surface)
    return () => observer.disconnect()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Delete removes the selected block, Escape clears the selection (not while typing in a field).
  useEffect(() => {
    if (!selectedBlock) return
    const onKey = (event: KeyboardEvent) => {
      if (isEditing(event.target) || event.ctrlKey || event.metaKey || event.altKey) return
      if (event.key === 'Delete' || event.key === 'Backspace') {
        event.preventDefault()
        workspace.deleteBlock(selectedBlock.id)
      } else if (event.key === 'Escape') workspace.selectBlock(undefined)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedBlock?.id])

  const save = (delay = 0) => {
    window.clearTimeout(saveTimer.current)
    saveTimer.current = window.setTimeout(() => workspace.setViewport(viewportRef.current), delay)
  }

  const local = (clientX: number, clientY: number): Point => {
    const rect = surfaceRef.current!.getBoundingClientRect()
    return { x: clientX - rect.left, y: clientY - rect.top }
  }

  // Wheel zoom needs a non-passive listener to keep the page from scrolling.
  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface) return
    const onWheel = (event: WheelEvent) => {
      event.preventDefault()
      setViewport(zoomAt(viewportRef.current, Math.exp(-event.deltaY * WHEEL_ZOOM), local(event.clientX, event.clientY)))
      save(200)
    }
    surface.addEventListener('wheel', onWheel, { passive: false })
    return () => surface.removeEventListener('wheel', onWheel)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active?.id])

  const onPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0 && event.button !== 1) return
    // Blocks, the zoom buttons and messages handle their own presses.
    if ((event.target as HTMLElement).closest('.canvas-block, .canvas-zoom, .canvas-error, .canvas-empty')) return
    event.currentTarget.setPointerCapture(event.pointerId)
    const point = local(event.clientX, event.clientY)
    pointers.current.set(event.pointerId, point)
    pressAt.current = pointers.current.size === 1 ? point : undefined
    setPanning(true)
  }

  const onPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    const point = local(event.clientX, event.clientY)
    if (event.pointerType === 'mouse') setCursor(point)
    const previous = pointers.current.get(event.pointerId)
    if (!previous) return
    const others = [...pointers.current].filter(([id]) => id !== event.pointerId).map(([, at]) => at)
    pointers.current.set(event.pointerId, point)
    const current = viewportRef.current
    if (others.length === 1) {
      // Pinch: zoom by the change in finger distance around their midpoint, pan by the midpoint's move.
      const other = others[0]!
      const before = Math.hypot(previous.x - other.x, previous.y - other.y)
      const after = Math.hypot(point.x - other.x, point.y - other.y)
      const middle = { x: (point.x + other.x) / 2, y: (point.y + other.y) / 2 }
      const zoomed = before > 0 ? zoomAt(current, after / before, middle) : current
      setViewport({ ...zoomed, x: zoomed.x + (point.x - previous.x) / 2, y: zoomed.y + (point.y - previous.y) / 2 })
    } else if (others.length === 0) {
      setViewport({ ...current, x: current.x + point.x - previous.x, y: current.y + point.y - previous.y })
    }
  }

  const onPointerEnd = (event: React.PointerEvent<HTMLDivElement>) => {
    const press = pressAt.current
    if (press && pointers.current.size === 1 && pointers.current.has(event.pointerId)) {
      const end = local(event.clientX, event.clientY)
      if (Math.hypot(end.x - press.x, end.y - press.y) < CLICK_SLOP) workspace.selectBlock(undefined)
    }
    pressAt.current = undefined
    pointers.current.delete(event.pointerId)
    if (!pointers.current.size) {
      setPanning(false)
      save()
    }
  }

  const zoomButton = (factor: number) => {
    const rect = surfaceRef.current!.getBoundingClientRect()
    setViewport(zoomAt(viewportRef.current, factor, { x: rect.width / 2, y: rect.height / 2 }))
    save()
  }

  const recenter = () => {
    const rect = surfaceRef.current!.getBoundingClientRect()
    setViewport({ zoom: 1, x: Math.round(rect.width / 2 / GRID) * GRID, y: Math.round(rect.height / 2 / GRID) * GRID })
    save()
  }

  const cell = GRID * viewport.zoom
  // Dots on the grid points (each layer shifted half a tile so a dot sits on the origin); minor dots drop out when they would crowd.
  const layers = [{ size: cell * MAJOR, color: 'var(--canvas-dot-major)', radius: 1.4 }, ...(cell >= 8 ? [{ size: cell, color: 'var(--canvas-dot)', radius: 1 }] : [])]
  const gridStyle: CSSProperties = showGrid
    ? {
        backgroundImage: layers.map((layer) => `radial-gradient(circle, ${layer.color} ${layer.radius}px, transparent ${layer.radius + 0.4}px)`).join(', '),
        backgroundSize: layers.map((layer) => `${layer.size}px ${layer.size}px`).join(', '),
        backgroundPosition: layers.map((layer) => `${viewport.x - layer.size / 2}px ${viewport.y - layer.size / 2}px`).join(', '),
      }
    : {}

  const pointed = cursor && screenToCanvas(viewport, cursor)
  const snapped = pointed && (snap ? snapPoint(pointed) : pointed)
  const marker = snapped && canvasToScreen(viewport, snapped)
  const empty = active && active.blocks.length === 0

  // A block dragged from the palette: where its corner would land.
  const dropPoint = (event: React.DragEvent): Point => {
    const under = screenToCanvas(viewport, local(event.clientX, event.clientY))
    const corner = { x: under.x - GRAB.x, y: under.y - GRAB.y }
    return snap ? snapPoint(corner) : corner
  }
  const fromPalette = (event: React.DragEvent) => !!active && event.dataTransfer.types.includes(BLOCK_DRAG_TYPE)
  const ghostShape = ghost && blockShape(ghost.type ? catalog.block(ghost.type) : undefined, { id: '', type: ghost.type ?? '' }, workspace.detailed)

  return (
    <div
      ref={surfaceRef}
      className={`canvas-surface ${panning ? 'is-panning' : ''}`}
      style={gridStyle}
      aria-label={active ? `Canvas ${active.name}` : 'No canvas'}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerEnd}
      onPointerCancel={onPointerEnd}
      onPointerLeave={() => setCursor(undefined)}
      onDragOver={(event) => {
        if (!fromPalette(event)) return
        event.preventDefault()
        event.dataTransfer.dropEffect = 'copy'
        setGhost({ at: dropPoint(event), type: workspace.paletteDrag.current })
      }}
      onDragLeave={(event) => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setGhost(undefined) }}
      onDrop={(event) => {
        setGhost(undefined)
        if (!fromPalette(event)) return
        event.preventDefault()
        const key = event.dataTransfer.getData(BLOCK_DRAG_TYPE)
        if (key) workspace.placeBlock(key, dropPoint(event))
      }}
    >
      <div className="canvas-world" style={{ transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})` }}>
        <div className="canvas-origin" aria-hidden="true" />
        {active?.blocks.map((block) => {
          const type = catalog.block(block.type)
          return (
            <CanvasBlockView
              key={block.id}
              block={block}
              type={type}
              shape={blockShape(type, block, workspace.detailed)}
              detailed={workspace.detailed}
              devices={devices} deviceCatalog={deviceCatalog}
              selected={selectedBlock?.id === block.id}
              errors={(diagnostics.get(block.id) ?? []).filter((entry) => entry.severity === 'error').length}
              zoom={viewport.zoom}
              snap={snap}
              onSelect={() => workspace.selectBlock(block.id)}
              onMove={(to) => workspace.moveBlock(block.id, to)}
            />
          )
        })}
        {ghost && ghostShape && <div className="canvas-block-ghost" aria-hidden="true" style={{ left: ghost.at.x, top: ghost.at.y, width: ghostShape.width, height: ghostShape.height }} />}
      </div>
      {marker && snap && !panning && !ghost && <div className="canvas-snap-marker" aria-hidden="true" style={{ left: marker.x, top: marker.y }} />}
      {!active && (
        <div className="canvas-empty">
          <p>No canvas yet.</p>
          <button type="button" className="canvas-empty-create" onClick={workspace.create}><Plus aria-hidden="true" />New canvas</button>
        </div>
      )}
      {empty && !ghost && <div className="canvas-hint" aria-hidden="true">Drag blocks here from the palette · drag the background to move, scroll or pinch to zoom</div>}
      {workspace.error && <button type="button" className="canvas-error" onClick={workspace.clearError} title="Dismiss">{workspace.error}</button>}
      <div className="canvas-status" aria-live="off">
        {snapped ? `x ${Math.round(snapped.x)}  y ${Math.round(snapped.y)}` : ''}
        {snap ? <span className="canvas-status-snap">snap {GRID}</span> : null}
      </div>
      <div className="canvas-zoom" role="toolbar" aria-label="Zoom">
        <button type="button" aria-label="Zoom out" title="Zoom out" onClick={() => zoomButton(1 / BUTTON_ZOOM)}><Minus aria-hidden="true" /></button>
        <button type="button" className="canvas-zoom-value" title="Zoom" onClick={() => zoomButton(1 / viewport.zoom)}>{`${Math.round(viewport.zoom * 100)}%`}</button>
        <button type="button" aria-label="Zoom in" title="Zoom in" onClick={() => zoomButton(BUTTON_ZOOM)}><Plus aria-hidden="true" /></button>
        <button type="button" aria-label="Back to the origin" title="Back to the origin at 100 %" onClick={recenter}><Crosshair aria-hidden="true" /></button>
      </div>
    </div>
  )
}
