import { useEffect, useLayoutEffect, useRef } from 'react'
import type { CSSProperties, PointerEvent as ReactPointerEvent } from 'react'
import { Crosshair, Minus, Plus } from 'lucide-react'
import { GRID } from '../domain/canvas'
import { ANCHOR_CELLS } from './useComponentEditor'
import type { ComponentEditorApi } from './useComponentEditor'
import '../canvas/Canvas.css'
import './Devices.css'

/** Zoom per button press; a wheel step and a pinch scale by their own amount. */
const BUTTON_ZOOM = 1.25
const WHEEL_ZOOM = 0.0025
/** A touch that moves less than this (px) is a tap, not a pan. */
const PAN_SLOP = 6

/*
 * The stage of the anchor editor (board-canvas-concept.md §4a): the picture at its size on the grid with the pin
 * anchors on it. Select a pin in the right panel and click the picture to place it, drag a placed anchor, nudge it with
 * the arrow keys (Shift for bigger steps), Delete takes it off. The picture zooms with the mouse wheel, a two-finger
 * pinch and the buttons, around the pointer; dragging the background (mouse or one finger) pans. The state lives in
 * `useComponentEditor`; the controls are the top toolbar (`ComponentToolbar`) and the right panel (`ComponentPins`).
 */
export function ComponentStage({ editor: e }: { editor: ComponentEditorApi }) {
  const stageRef = useRef<HTMLDivElement>(null)
  const { setStageWidth, zoomBy, zoomAnchor, frameRef } = e
  const touches = useRef(new Map<number, { x: number; y: number; startX: number; startY: number }>())
  const pinchDistance = useRef<number | undefined>(undefined)
  const panned = useRef(false)

  useEffect(() => {
    const stage = stageRef.current
    if (!stage) return
    const observer = new ResizeObserver(() => setStageWidth(stage.clientWidth))
    observer.observe(stage)
    setStageWidth(stage.clientWidth)
    return () => observer.disconnect()
  }, [setStageWidth])

  // After a zoom by gesture, scroll so the point that was under the pointer stays under it; any other change of the
  // picture's size (a new picture, the size controls, the zoom menu, Fit, the centre button) centres it. The margin around the picture
  // (`component-editor-world`) gives the scroll room in both cases.
  const { sizeCells } = e
  const pictureUrl = e.picture?.url
  useLayoutEffect(() => {
    const anchor = zoomAnchor.current
    const stage = stageRef.current
    const frame = frameRef.current
    zoomAnchor.current = undefined
    if (!stage || !frame) return
    if (anchor) {
      const rect = stage.getBoundingClientRect()
      stage.scrollLeft = frame.offsetLeft + anchor.fx * frame.offsetWidth - (anchor.clientX - rect.left - stage.clientLeft)
      stage.scrollTop = frame.offsetTop + anchor.fy * frame.offsetHeight - (anchor.clientY - rect.top - stage.clientTop)
    } else {
      stage.scrollLeft = frame.offsetLeft + frame.offsetWidth / 2 - stage.clientWidth / 2
      stage.scrollTop = frame.offsetTop + frame.offsetHeight / 2 - stage.clientHeight / 2
    }
  }, [e.scale, e.viewResets, pictureUrl, sizeCells, zoomAnchor, frameRef])

  // The wheel zooms about the pointer (the stage pans by dragging).
  useEffect(() => {
    const stage = stageRef.current
    if (!stage) return
    const onWheel = (event: WheelEvent) => {
      event.preventDefault()
      zoomBy(Math.exp(-event.deltaY * WHEEL_ZOOM), event)
    }
    stage.addEventListener('wheel', onWheel, { passive: false })
    return () => stage.removeEventListener('wheel', onWheel)
  }, [zoomBy, e.picture])

  const centre = (): { clientX: number; clientY: number } => {
    const rect = stageRef.current?.getBoundingClientRect()
    return rect ? { clientX: rect.left + rect.width / 2, clientY: rect.top + rect.height / 2 } : { clientX: 0, clientY: 0 }
  }

  // Two fingers pinch; one finger or the mouse pans by dragging the background past a small slop (an anchor under the
  // pointer is dragged instead: its own handler stops the press here, and a plain click still places a pin).
  const onPointerDown = (event: ReactPointerEvent) => {
    if (event.pointerType === 'mouse' && event.button !== 0 && event.button !== 1) return
    const at = { x: event.clientX, y: event.clientY }
    if (touches.current.size === 0) panned.current = false
    touches.current.set(event.pointerId, { ...at, startX: at.x, startY: at.y })
    pinchDistance.current = undefined
  }
  const onPointerMove = (event: ReactPointerEvent) => {
    const known = touches.current.get(event.pointerId)
    if (!known) return
    if (event.pointerType === 'mouse' && event.buttons === 0) {
      touches.current.delete(event.pointerId)
      return
    }
    const now = { ...known, x: event.clientX, y: event.clientY }
    touches.current.set(event.pointerId, now)
    const points = [...touches.current.values()]
    if (points.length >= 2) {
      const [a, b] = points
      const distance = Math.hypot(a.x - b.x, a.y - b.y)
      if (pinchDistance.current && distance > 0) zoomBy(distance / pinchDistance.current, { clientX: (a.x + b.x) / 2, clientY: (a.y + b.y) / 2 })
      pinchDistance.current = distance
      panned.current = true
      return
    }
    if (!panned.current && Math.hypot(now.x - now.startX, now.y - now.startY) >= PAN_SLOP) {
      panned.current = true
      // A mouse drag keeps following the pointer outside the stage; captured only now, so a plain click still reaches the picture.
      if (event.pointerType === 'mouse') event.currentTarget.setPointerCapture(event.pointerId)
    }
    if (panned.current && stageRef.current) {
      stageRef.current.scrollLeft -= now.x - known.x
      stageRef.current.scrollTop -= now.y - known.y
    }
  }
  const onPointerUp = (event: ReactPointerEvent) => {
    touches.current.delete(event.pointerId)
    pinchDistance.current = undefined
  }

  if (!e.types.length) return <div className="devices-muted component-editor-empty">No device has a pins csv yet.</div>

  const [wide, high] = e.sizeCells
  return (
    <div className="component-editor">
      {e.status && <div className={`component-editor-status ${e.status.error ? 'is-error' : ''}`} role="status">{e.status.text}</div>}
      <div className="component-editor-stagewrap">
      <div ref={stageRef} className="component-editor-stage" onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onPointerCancel={onPointerUp}>
        {e.picture ? (
          <>
            <div className="component-editor-size" aria-label="Picture size">{wide} × {high} mm · {e.placed} / {e.ports.length} pins placed</div>
            <div className="component-editor-world">
            <div
              ref={e.frameRef}
              className={`component-editor-frame ${e.selected && !e.anchors[e.selected] ? 'is-placing' : ''} ${e.labels ? '' : 'hide-labels'}`}
              style={{
                width: wide * GRID * e.scale,
                height: high * GRID * e.scale,
                backgroundImage: `url("${e.picture.url}")`,
                '--anchor-size': `${Math.max(10, ANCHOR_CELLS * GRID * e.scale)}px`,
                '--anchor-font': `${Math.min(14, Math.max(9, 0.6 * GRID * e.scale))}px`,
                '--grid-step': `${e.gridStepMm * GRID * e.scale}px`,
              } as CSSProperties}
              onClick={(event) => { if (!panned.current) e.onFrameClick(event) }}
            >
              <div className={`component-editor-grid${e.snap === 'off' || e.snap === 'pins' ? '' : ' is-snap'}`} aria-hidden="true" />
              {e.guides?.x !== undefined && <div className="component-guide is-vertical" style={{ left: `${e.guides.x * 100}%` }} aria-hidden="true" />}
              {e.guides?.y !== undefined && <div className="component-guide is-horizontal" style={{ top: `${e.guides.y * 100}%` }} aria-hidden="true" />}
              {Object.entries(e.anchors).map(([name, anchor]) => {
                const port = e.ports.find((entry) => entry.name === name)
                if (!port) return null
                return (
                  <button
                    key={name}
                    type="button"
                    className={`component-anchor kind-${port.kind} ${e.selected === name ? 'is-selected' : ''}`}
                    style={{ left: `${anchor.x * 100}%`, top: `${anchor.y * 100}%`, '--label-angle': `${e.angleOf(name)}deg` } as CSSProperties}
                    title={`${name}${port.note ? ` — ${port.note}` : ''}`}
                    onPointerDown={(event) => e.onAnchorDown(name, event)}
                    onPointerMove={(event) => e.onAnchorMove(name, event)}
                    onPointerUp={e.onAnchorUp}
                    onClick={(event) => event.stopPropagation()}
                  >
                    <span>{name}</span>
                  </button>
                )
              })}
            </div>
            </div>
          </>
        ) : (
          <div className="devices-muted component-editor-empty">Choose a picture of the IC or board (SVG or PNG) in the toolbar, then place its pins on it from the right panel.</div>
        )}
      </div>
      {e.picture && (
        <div className="canvas-zoom component-editor-zoom">
          <button type="button" aria-label="Zoom out" title="Zoom out (mouse wheel)" onClick={() => zoomBy(1 / BUTTON_ZOOM, centre())}><Minus aria-hidden="true" /></button>
          <button type="button" className="canvas-zoom-value" title="Back to 100%, centred" onClick={e.resetView}>{`${Math.round(e.scale * 100)}%`}</button>
          <button type="button" aria-label="Zoom in" title="Zoom in (mouse wheel)" onClick={() => zoomBy(BUTTON_ZOOM, centre())}><Plus aria-hidden="true" /></button>
          <button type="button" aria-label="Centre at 100%" title="Centre the picture at 100%" onClick={e.resetView}><Crosshair aria-hidden="true" /></button>
        </div>
      )}
      </div>
    </div>
  )
}
