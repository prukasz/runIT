import { useEffect, useRef, useState } from 'react'
import { AlertTriangle, Maximize, Minimize, Power, X } from 'lucide-react'
import { OBJECT_DRAG_TYPE } from '../domain/canvas'
import { CELL, COLS, fit, isMissingKind, PAD_LABELS, padSources, ROWS, widgetTitle, widgetType } from '../domain/controller'
import type { ControllerWidget } from '../domain/controller'
import type { ProjectDocument } from '../domain/project'
import { useDebug } from '../debug/DebugContext'
import { WIDGET_DRAG_TYPE } from './drag'
import { widgetIcon } from './icons'
import type { ControllerWorkspace } from './useControllerWorkspace'
import { useControllerLink } from './useControllerLink'
import { useGamepad } from './useGamepad'
import { controllerVariables } from './variables'
import { WidgetView } from './widgets'

/*
 * The middle of the Remote view: the widgets on their grid. Not enabled, it is
 * the editor (move, resize, drop a widget from the palette or a variable onto
 * one); enabled, the layout is locked and the widgets work. Full screen shows
 * only the widgets and the Enable button.
 */

type Box = { x: number; y: number; w: number; h: number }
interface Gesture {
  readonly id: string
  readonly mode: 'move' | 'resize'
  readonly startX: number
  readonly startY: number
  readonly origin: Box
  readonly box: Box
  /** Screen pixels per grid pixel (the stage is scaled in full screen). */
  readonly scale: number
}

const typing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName))

/** The Enable button: uploads the program and starts the interface, or stops it. */
export function EnableButton({ className = '' }: { className?: string }) {
  const debug = useDebug()
  const live = debug.active && !debug.stale
  const label = debug.busy ? 'Starting…' : live ? 'Disable' : debug.active ? 'Update' : 'Enable'
  const title = debug.error
    ?? (debug.busy ? 'Uploading the program and starting it'
      : live ? 'The interface is working: the widgets read and set variables on the board. Click to stop.'
        : debug.active ? 'The program changed since it was uploaded: click to upload it again'
          : 'Upload the program, run it and make the widgets work')
  return (
    <button
      type="button"
      className={`controller-enable ${live ? 'is-live' : ''} ${debug.error ? 'has-error' : ''} ${className}`.trim()}
      aria-pressed={live}
      disabled={debug.busy}
      title={title}
      onClick={debug.toggle}
    ><Power aria-hidden="true" /><span>{label}</span></button>
  )
}

/** Toolbar part of the Remote view: Enable and full screen. */
export function ControllerToolbar({ workspace }: { workspace: ControllerWorkspace }) {
  return (
    <div className="controller-toolbar">
      <EnableButton />
      <button type="button" aria-label="Full screen" title="Full screen: only the widgets (Esc leaves)" onClick={() => workspace.setFullscreen(true)}><Maximize aria-hidden="true" /></button>
    </div>
  )
}

export function ControllerStage({ workspace, project }: { workspace: ControllerWorkspace; project: ProjectDocument }) {
  const { controller, selectedId, fullscreen } = workspace
  const { io, live, problem, clearProblem } = useControllerLink(controller)
  useGamepad(controller, io, live)
  const pad = padSources(controller.widgets)
  // The editor (grid, selection, handles) is for the screen with its panels; full screen is only the widgets.
  const editing = !live && !fullscreen
  const names = new Map(controllerVariables(project).map((variable) => [variable.id, variable]))
  const boardRef = useRef<HTMLDivElement>(null)
  const frameRef = useRef<HTMLDivElement>(null)
  const [gesture, setGesture] = useState<Gesture>()
  const [dropTarget, setDropTarget] = useState<string>()
  const [fitScale, setFitScale] = useState(1)

  // Full screen: the browser's full screen when it allows one, and the layout scaled to fill the screen.
  useEffect(() => {
    if (!fullscreen) return undefined
    const root = document.documentElement
    if (!document.fullscreenElement) void root.requestFullscreen?.().catch(() => undefined)
    const onChange = () => { if (!document.fullscreenElement) workspace.setFullscreen(false) }
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') workspace.setFullscreen(false) }
    document.addEventListener('fullscreenchange', onChange)
    window.addEventListener('keydown', onKey)
    return () => {
      document.removeEventListener('fullscreenchange', onChange)
      window.removeEventListener('keydown', onKey)
      if (document.fullscreenElement) void document.exitFullscreen().catch(() => undefined)
    }
    // `workspace` changes on every render; only entering or leaving full screen matters.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fullscreen])
  useEffect(() => {
    const frame = frameRef.current
    if (!fullscreen || !frame) { setFitScale(1); return undefined }
    const measure = () => setFitScale(Math.min(frame.clientWidth / (COLS * CELL), frame.clientHeight / (ROWS * CELL)))
    measure()
    const observer = new ResizeObserver(measure)
    observer.observe(frame)
    return () => observer.disconnect()
  }, [fullscreen])

  const scaleNow = (): number => {
    const rect = boardRef.current?.getBoundingClientRect()
    return rect && rect.width > 0 ? rect.width / (COLS * CELL) : 1
  }
  const cellAt = (event: { clientX: number; clientY: number }): { x: number; y: number } => {
    const rect = boardRef.current?.getBoundingClientRect()
    const scale = scaleNow()
    return rect ? { x: Math.floor((event.clientX - rect.left) / (CELL * scale)), y: Math.floor((event.clientY - rect.top) / (CELL * scale)) } : { x: 0, y: 0 }
  }

  const begin = (event: React.PointerEvent, widget: ControllerWidget, mode: Gesture['mode']) => {
    if (!editing || event.button !== 0) return
    event.stopPropagation()
    event.currentTarget.setPointerCapture(event.pointerId)
    workspace.select(widget.id)
    const origin = { x: widget.x, y: widget.y, w: widget.w, h: widget.h }
    setGesture({ id: widget.id, mode, startX: event.clientX, startY: event.clientY, origin, box: origin, scale: scaleNow() })
  }
  const drag = (event: React.PointerEvent, widget: ControllerWidget) => {
    if (!gesture || gesture.id !== widget.id) return
    const dx = Math.round((event.clientX - gesture.startX) / (CELL * gesture.scale))
    const dy = Math.round((event.clientY - gesture.startY) / (CELL * gesture.scale))
    const wanted = gesture.mode === 'move'
      ? { ...gesture.origin, x: gesture.origin.x + dx, y: gesture.origin.y + dy }
      : { ...gesture.origin, w: gesture.origin.w + dx, h: gesture.origin.h + dy }
    const { x, y, w, h } = fit({ ...widget, ...wanted })
    if (x !== gesture.box.x || y !== gesture.box.y || w !== gesture.box.w || h !== gesture.box.h) setGesture({ ...gesture, box: { x, y, w, h } })
  }
  const end = (widget: ControllerWidget) => {
    if (!gesture || gesture.id !== widget.id) return
    const { box, origin } = gesture
    setGesture(undefined)
    if (box.x !== origin.x || box.y !== origin.y || box.w !== origin.w || box.h !== origin.h) workspace.place(widget.id, box)
  }

  /** A variable dropped on a widget fills its first free slot (else the first). */
  const dropVariable = (widget: ControllerWidget, variableId: string) => {
    if (!names.has(variableId)) return
    const slots = widgetType(widget.kind).slots
    const slot = slots.find((entry) => !widget.links[entry.key]) ?? slots[0]
    if (slot) workspace.link(widget.id, slot.key, variableId)
    workspace.select(widget.id)
  }

  const board = (
    <div
      className={`controller-board ${editing ? 'is-editing' : 'is-live'}`}
      ref={boardRef}
      style={{ width: COLS * CELL, height: ROWS * CELL, ...(fullscreen ? { transform: `scale(${fitScale})`, transformOrigin: '0 0' } : {}), ['--cell' as string]: `${CELL}px` }}
      onPointerDown={() => workspace.select(undefined)}
      onDragOver={(event) => { if (editing && event.dataTransfer.types.includes(WIDGET_DRAG_TYPE)) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy' } }}
      onDrop={(event) => {
        const kind = event.dataTransfer.getData(WIDGET_DRAG_TYPE)
        if (!editing || !kind || isMissingKind(kind)) return
        event.preventDefault()
        const type = widgetType(kind)
        const at = cellAt(event)
        workspace.add(kind, { x: at.x - Math.floor(type.w / 2), y: at.y - Math.floor(type.h / 2) })
      }}
    >
      {controller.widgets.map((widget) => {
        const moving = gesture?.id === widget.id ? gesture.box : undefined
        const box = moving ?? widget
        const type = widgetType(widget.kind)
        const Icon = widgetIcon(widget.kind)
        // A slot behind "Advanced" is optional (the chart's second and third line): only a link to a missing variable is a problem there.
        const optional = new Set(type.advanced ?? [])
        const unlinked = type.slots.filter((slot) => (widget.links[slot.key] ? !names.has(widget.links[slot.key]!) : !optional.has(slot.key)))
        const selected = editing && selectedId === widget.id
        return (
          <div
            key={widget.id}
            className={`controller-widget kind-${widget.kind} ${selected ? 'is-selected' : ''} ${moving ? 'is-moving' : ''} ${dropTarget === widget.id ? 'is-drop' : ''} ${editing && unlinked.length > 0 ? 'has-errors' : ''}`}
            style={{ left: box.x * CELL, top: box.y * CELL, width: box.w * CELL, height: box.h * CELL }}
            onPointerDown={(event) => begin(event, widget, 'move')}
            onPointerMove={(event) => drag(event, widget)}
            onPointerUp={() => end(widget)}
            onPointerCancel={() => end(widget)}
            onDragOver={(event) => { if (editing && event.dataTransfer.types.includes(OBJECT_DRAG_TYPE)) { event.preventDefault(); event.stopPropagation(); setDropTarget(widget.id) } }}
            onDragLeave={() => setDropTarget((current) => (current === widget.id ? undefined : current))}
            onDrop={(event) => {
              const id = event.dataTransfer.getData(OBJECT_DRAG_TYPE)
              setDropTarget(undefined)
              if (!editing || !id) return
              event.preventDefault()
              event.stopPropagation()
              dropVariable(widget, id)
            }}
          >
            <div className="controller-widget-body"><WidgetView widget={{ ...widget, ...box }} io={io} preview={!live} /></div>
            {editing && (
              <>
                <span className="controller-widget-tag"><Icon aria-hidden="true" /><span>{widgetTitle(widget)}</span>{pad.has(widget.id) && <span className="controller-widget-pad" title="Driven by this control of an Xbox gamepad while the interface runs">{PAD_LABELS[pad.get(widget.id)!]}</span>}</span>
                {unlinked.length > 0 && <span className="controller-widget-warn" title="Not linked to a variable yet" aria-label="Not linked to a variable yet">{unlinked.length}</span>}
                {selected && <span className="controller-resize" aria-hidden="true" title="Drag to resize" onPointerDown={(event) => begin(event, widget, 'resize')} onPointerMove={(event) => drag(event, widget)} onPointerUp={() => end(widget)} onPointerCancel={() => end(widget)} />}
              </>
            )}
          </div>
        )
      })}
      {controller.widgets.length === 0 && (
        <div className="controller-empty">
          <strong>An empty controller</strong>
          <span>Drag a widget from the palette (or click it), then drop a variable on it to link them. Enable the interface to use it.</span>
        </div>
      )}
    </div>
  )

  return (
    <div
      className={`controller-stage ${fullscreen ? 'is-fullscreen' : ''} ${live ? 'is-live' : ''}`}
      tabIndex={0}
      aria-label="Controller"
      onKeyDown={(event) => {
        if (editing && selectedId && (event.key === 'Delete' || event.key === 'Backspace') && !typing(event.target)) {
          event.preventDefault()
          workspace.remove(selectedId)
        }
      }}
    >
      {problem && (
        <div className="controller-problem" role="alert">
          <AlertTriangle aria-hidden="true" /><span>{problem}</span>
          <button type="button" aria-label="Dismiss" onClick={clearProblem}><X aria-hidden="true" /></button>
        </div>
      )}
      {fullscreen && (
        <div className="controller-fullscreen-bar">
          <EnableButton />
          <button type="button" aria-label="Leave full screen" title="Leave full screen (Esc)" onClick={() => workspace.setFullscreen(false)}><Minimize aria-hidden="true" /></button>
        </div>
      )}
      <div className="controller-frame" ref={frameRef}>
        {fullscreen ? <div className="controller-sizer" style={{ width: COLS * CELL * fitScale, height: ROWS * CELL * fitScale }}>{board}</div> : board}
      </div>
    </div>
  )
}
