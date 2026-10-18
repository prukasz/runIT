import { useLayoutEffect, useRef, useState } from 'react'
import { ArrowLeftRight } from 'lucide-react'
import type { ValueKind } from '@engine'
import { MenuOption, OptionMenu, TypeLead } from '@ui/components'
import type { PinTarget } from '../blocks'
import { pinLead } from '../PathEditor'
import { canvasToScreen } from './layout'
import type { BlockShape, Point, Viewport } from './layout'

/** One thing a block can send on: an output or its When done. */
export interface SourceChoice {
  readonly key: string
  readonly label: string
  /** What it is, on hover. */
  readonly detail: string
  readonly value: ValueKind
}

/** One place a link can end: an input pin, or the block's Run when. */
export interface PickerRow {
  readonly key: string
  readonly label: string
  /** What it holds now, on hover. */
  readonly detail: string
  readonly value: ValueKind
  readonly target: PinTarget
  /** Whether what is sent fits here. */
  readonly ok: boolean
}

/** A window over a block: as wide as the block on the screen, at least this. */
const MIN_WIDTH = 160

/** Where a window stands: over its block, kept inside the canvas (`bounds`: its size on the screen) so none of it is cut off. */
const windowSpot = (at: Point, shape: BlockShape, viewport: Viewport, bounds: { readonly width: number; readonly height: number }, rows: number) => {
  const width = Math.max(MIN_WIDTH, shape.width * viewport.zoom)
  const height = 31 * (rows + 1)
  const screen = canvasToScreen(viewport, at)
  const inside = (value: number, size: number, room: number) => (room > 0 ? Math.max(8, Math.min(value, room - size - 8)) : value)
  return { at: { x: inside(screen.x, width, bounds.width), y: inside(screen.y, height, bounds.height) }, width }
}

interface Box {
  readonly left: number
  readonly top: number
  readonly right: number
  readonly bottom: number
}

/**
 * The arrow from what is sent to where it goes: it leaves the source from the side that faces the target (right, left, below,
 * above) and enters the target from the side that faces back, a soft curve like the wires with a head at its end.
 */
const arrowPath = (from: Box, to: Box, rowY?: number): { d: string; head: string } | undefined => {
  const fx = (from.left + from.right) / 2
  // Leaving sideways it starts at the chosen row; above or below, at the window's edge.
  const fy = rowY ?? (from.top + from.bottom) / 2
  const tx = (to.left + to.right) / 2
  const ty = (to.top + to.bottom) / 2
  // Beside each other when the gap between them is wider than it is tall, else above or below.
  const gapX = Math.max(to.left - from.right, from.left - to.right)
  const gapY = Math.max(to.top - from.bottom, from.top - to.bottom)
  if (gapX < 6 && gapY < 6) return undefined
  let a: Point
  let b: Point
  let ca: Point
  let cb: Point
  if (gapX >= gapY) {
    const right = tx > fx
    a = { x: right ? from.right : from.left, y: fy }
    b = { x: right ? to.left : to.right, y: Math.min(Math.max(fy, to.top + 14), to.bottom - 14) }
    const reach = Math.max(24, Math.abs(b.x - a.x) / 2)
    ca = { x: a.x + (right ? reach : -reach), y: a.y }
    cb = { x: b.x - (right ? reach : -reach), y: b.y }
  } else {
    const down = ty > fy
    a = { x: Math.min(Math.max(tx, from.left + 20), from.right - 20), y: down ? from.bottom : from.top }
    b = { x: Math.min(Math.max(fx, to.left + 20), to.right - 20), y: down ? to.top : to.bottom }
    const reach = Math.max(24, Math.abs(b.y - a.y) / 2)
    ca = { x: a.x, y: a.y + (down ? reach : -reach) }
    cb = { x: b.x, y: b.y - (down ? reach : -reach) }
  }
  // The head points along the curve's last stretch, its tip on the target's edge.
  const len = Math.hypot(b.x - cb.x, b.y - cb.y) || 1
  const ux = (b.x - cb.x) / len
  const uy = (b.y - cb.y) / len
  const base = { x: b.x - ux * 14, y: b.y - uy * 14 }
  const head = `M ${b.x} ${b.y} L ${base.x - uy * 9} ${base.y + ux * 9} L ${base.x + uy * 9} ${base.y - ux * 9} Z`
  return { d: `M ${a.x} ${a.y} C ${ca.x} ${ca.y}, ${cb.x} ${cb.y}, ${base.x} ${base.y}`, head }
}

/**
 * Two windows for linking the selected block to the one tapped next: one over the source (what it sends), one over the target
 * (the pins that take it). Choosing on the source changes the target's list; choosing a target pin makes the link. An arrow from
 * the chosen source to the target shows which way it goes; ⇄ links the other way round, Select just selects the tapped block.
 * The rows hold names only: what each one is, or holds now, is on hover.
 */
export function LinkWindows({ fromName, fromAt, fromShape, toName, toAt, toShape, sources, chosen, rows, viewport, bounds, onChoose, onPick, onSwap, onSelect, onCancel }: {
  fromName: string
  fromAt: Point
  fromShape: BlockShape
  toName: string
  toAt: Point
  toShape: BlockShape
  sources: readonly SourceChoice[]
  chosen: number
  rows: readonly PickerRow[]
  viewport: Viewport
  /** The canvas's size on the screen. */
  bounds: { readonly width: number; readonly height: number }
  onChoose: (index: number) => void
  onPick: (row: PickerRow) => void
  onSwap: () => void
  onSelect: () => void
  onCancel: () => void
}) {
  const source = windowSpot(fromAt, fromShape, viewport, bounds, sources.length)
  const target = windowSpot(toAt, toShape, viewport, bounds, Math.max(1, rows.length))
  const sourceRef = useRef<HTMLDivElement>(null)
  const targetRef = useRef<HTMLDivElement>(null)
  const [arrow, setArrow] = useState<{ d: string; head: string }>()

  // The arrow is drawn between the windows as they are laid out: from the chosen row of the source to the target window.
  useLayoutEffect(() => {
    const from = sourceRef.current
    const to = targetRef.current
    const surface = to?.offsetParent
    if (!from || !to || !surface) return setArrow(undefined)
    const origin = surface.getBoundingClientRect()
    const box = (el: Element): Box => {
      const r = el.getBoundingClientRect()
      return { left: r.left - origin.left, top: r.top - origin.top, right: r.right - origin.left, bottom: r.bottom - origin.top }
    }
    const row = box(from.querySelector('.is-chosen') ?? from)
    setArrow(arrowPath(box(from), box(to), (row.top + row.bottom) / 2))
  }, [chosen, viewport, fromAt, toAt, sources.length, rows.length])

  const swap = <button type="button" className="wire-picker-icon" onClick={onSwap} title="Link the other way round" aria-label="Link the other way round"><ArrowLeftRight aria-hidden="true" /></button>
  return (
    <>
      {arrow && (
        <svg className="link-arrow" aria-hidden="true">
          <path className="link-arrow-line" d={arrow.d} />
          <path className="link-arrow-head" d={arrow.head} />
        </svg>
      )}
      <OptionMenu ref={sourceRef} className="wire-picker is-source" aria-label={`Send from ${fromName}`} style={{ left: source.at.x, top: source.at.y, width: source.width }} onCancel={onCancel}>
        <div className="wire-picker-head"><span>{fromName}</span>{swap}</div>
        {sources.map((entry, index) => (
          <MenuOption key={entry.key} selected={index === chosen} title={entry.detail} className={`wire-picker-row is-source-row ${index === chosen ? 'is-chosen' : ''}`} onChoose={() => onChoose(index)}>
            <TypeLead kind={pinLead(entry.value)} title={entry.value} />
            <span className="wire-picker-label">{entry.label}</span>
          </MenuOption>
        ))}
      </OptionMenu>
      <OptionMenu ref={targetRef} className="wire-picker is-target" aria-label={`Connect to ${toName}`} style={{ left: target.at.x, top: target.at.y, width: target.width }} onCancel={onCancel}>
        <div className="wire-picker-head">
          <span>{toName}</span>
          {/* ui-raw: a small word in the window's header, beside the swap icon */}
          <button type="button" className="wire-picker-select" onClick={onSelect} title="Just select this block, no link">Select</button>
        </div>
        {rows.map((entry) => (
          <MenuOption key={entry.key} disabled={!entry.ok} title={entry.ok ? entry.detail : `${entry.detail} (this does not fit here)`} className={`wire-picker-row ${entry.target.side === 'en' ? 'is-en' : ''}`} onChoose={() => onPick(entry)}>
            <TypeLead kind={pinLead(entry.value)} title={entry.value} />
            <span className="wire-picker-label">{entry.label}</span>
          </MenuOption>
        ))}
        {!rows.length && <div className="wire-picker-empty">Nothing here fits</div>}
      </OptionMenu>
    </>
  )
}
