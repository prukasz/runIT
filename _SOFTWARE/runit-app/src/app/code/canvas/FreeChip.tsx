import { useState } from 'react'
import { CHIP_HEIGHT, CHIP_WIDTH, snapPoint } from './layout'
import type { CanvasChip, Point } from './layout'

/** A press that moves less than this is a click. */
const CLICK_SLOP = 4

/**
 * A variable pulled off its pin: a label on the canvas. Drag it to move it, or onto a pin to dock it there; drag from its dot to
 * wire it to more pins; a click selects it (Delete removes it, the pins keep the variable).
 */
export function FreeChip({ chip, label, full, selected, zoom, onSelect, onMove, onDropAt, onWireStart }: {
  chip: CanvasChip
  label: string
  full: string
  selected: boolean
  zoom: number
  onSelect: () => void
  onMove: (to: Point) => void
  /** Let go at this screen point: true when a pin took it, so it does not move. */
  onDropAt: (clientX: number, clientY: number) => boolean
  onWireStart: (event: React.PointerEvent) => void
}) {
  const [drag, setDrag] = useState<{ start: Point; at: Point; moved: boolean }>()
  const at = drag?.at ?? chip
  return (
    <div className={`canvas-chip is-free ${selected ? 'is-selected' : ''}`} style={{ left: at.x, top: at.y, width: CHIP_WIDTH, height: CHIP_HEIGHT }}
      title={`${full}: drag to move, onto a pin to dock it, from the dot to wire it`}
      onPointerDown={(event) => {
        if (event.button !== 0) return
        event.stopPropagation()
        event.currentTarget.setPointerCapture(event.pointerId)
        onSelect()
        setDrag({ start: { x: event.clientX, y: event.clientY }, at: { x: chip.x, y: chip.y }, moved: false })
      }}
      onPointerMove={(event) => {
        if (!drag) return
        const dx = event.clientX - drag.start.x
        const dy = event.clientY - drag.start.y
        if (!drag.moved && Math.hypot(dx, dy) < CLICK_SLOP) return
        setDrag({ ...drag, moved: true, at: snapPoint({ x: chip.x + dx / zoom, y: chip.y + dy / zoom }) })
      }}
      onPointerUp={(event) => {
        const done = drag
        setDrag(undefined)
        if (!done?.moved) return
        if (onDropAt(event.clientX, event.clientY)) return
        if (done.at.x !== chip.x || done.at.y !== chip.y) onMove(done.at)
      }}
      onPointerCancel={() => setDrag(undefined)}>
      <span>{label}</span>
      <i className="canvas-wire-start" aria-hidden="true"
        onPointerDown={(event) => {
          if (event.button !== 0) return
          event.stopPropagation()
          onWireStart(event)
        }} />
    </div>
  )
}
