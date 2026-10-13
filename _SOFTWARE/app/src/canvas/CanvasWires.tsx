import { wiresOf } from '../domain/canvas'
import type { CanvasBlock, Point, ProjectCanvas, Wire, WireSource, WireTarget } from '../domain/canvas'
import { pinAnchor } from '../blocks/blockModel'
import type { BlockShape } from '../blocks/blockModel'

/*
 * Wires (world layer): a curve from an output pin to an input pin, and an EN link as a faded ribbon, narrow where it leaves its
 * source and as tall as the block's EN strip where it arrives, so it reads as "powers this block".
 */

const ARROW_LENGTH = 12
const ARROW_HALF = 8
const BAND_HALF = 4

/** A wire reaches forward by half the gap (at least 40); running back it swings out and under both ends. */
const reach = (from: number, to: number): { dx: number; back: boolean } =>
  to >= from ? { dx: Math.max(40, (to - from) / 2), back: false } : { dx: 80, back: true }

export const curve = (a: Point, b: Point): string => {
  const { dx, back } = reach(a.x, b.x)
  const low = back ? Math.max(a.y, b.y) + 70 : undefined
  return `M ${a.x} ${a.y} C ${a.x + dx} ${low ?? a.y}, ${b.x - dx} ${low ?? b.y}, ${b.x} ${b.y}`
}

interface RibbonSpan {
  readonly x: number
  readonly top: number
  readonly bottom: number
}

const ribbon = (from: RibbonSpan, to: RibbonSpan, at: number): string => {
  const { dx, back } = reach(from.x, to.x)
  const end = to.x - ARROW_LENGTH
  const head = `L ${end} ${at - ARROW_HALF} L ${to.x} ${at} L ${end} ${at + ARROW_HALF} L ${end} ${at + BAND_HALF}`
  if (!back) return `M ${from.x} ${from.top} C ${from.x + dx} ${from.top}, ${end - dx} ${at - BAND_HALF}, ${end} ${at - BAND_HALF} ${head} C ${end - dx} ${at + BAND_HALF}, ${from.x + dx} ${from.bottom}, ${from.x} ${from.bottom} Z`
  const low = Math.max(from.bottom, at + ARROW_HALF) + 40
  return `M ${from.x} ${from.top} C ${from.x + dx} ${low}, ${end - dx} ${low}, ${end} ${at - BAND_HALF} ${head} C ${end - dx} ${low + 6}, ${from.x + dx} ${low + 6}, ${from.x} ${from.bottom} Z`
}

export const anchorOfSource = (pin: string): 'eno' | { side: 'out'; index: number } =>
  pin === 'eno' ? pin : { side: 'out', index: Number(pin.slice(1)) }

const anchorOfTarget = (target: WireTarget): 'en' | { side: 'in' | 'out'; index: number } => (target.kind === 'en' ? 'en' : { side: target.kind, index: target.index })

const wireClass = (wire: Wire): string => (wire.from.pin === 'eno' ? 'is-eno' : wire.to.kind === 'en' ? 'is-gate' : 'is-data')

export const wireKey = (wire: Wire): string => `${wire.from.block}:${wire.from.pin}>${wire.to.block}:${wire.to.kind}${'index' in wire.to ? wire.to.index : ''}:${wire.enableIndex ?? ''}`

export function CanvasWires({ canvas, shapeOf, selected, draft, onSelect }: {
  canvas: ProjectCanvas
  shapeOf: (block: CanvasBlock) => BlockShape
  selected?: Wire
  /** A wire being dragged: from its source pin to the pointer. */
  draft?: { from: WireSource; to: Point }
  onSelect: (wire: Wire) => void
}) {
  const blocks = new Map(canvas.blocks.map((block) => [block.id, block]))
  const selectedKey = selected && wireKey(selected)
  const draftBlock = draft && blocks.get(draft.from.block)
  const draftFrom = draft && draftBlock && pinAnchor(draftBlock, shapeOf(draftBlock), anchorOfSource(draft.from.pin))
  return (
    <svg className="canvas-wires" aria-hidden="true">
      <defs>
        <marker id="canvas-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" markerUnits="userSpaceOnUse" orient="auto">
          <path d="M 0 1 L 9 5 L 0 9 z" fill="context-stroke" />
        </marker>
      </defs>
      {wiresOf(canvas).map((wire) => {
        const source = blocks.get(wire.from.block)!
        const target = blocks.get(wire.to.block)!
        const sourceShape = shapeOf(source)
        const targetShape = shapeOf(target)
        const from = pinAnchor(source, sourceShape, anchorOfSource(wire.from.pin))
        const select = (event: React.PointerEvent) => {
          event.stopPropagation()
          onSelect(wire)
        }
        if (wire.to.kind === 'en') {
          const fromSpan: RibbonSpan = wire.from.pin === 'eno'
            ? { x: source.x + sourceShape.width, top: source.y + 1, bottom: source.y + sourceShape.height - 1 }
            : { x: from.x, top: from.y - 2.5, bottom: from.y + 2.5 }
          const toSpan: RibbonSpan = { x: target.x, top: target.y + 1, bottom: target.y + targetShape.height - 1 }
          return (
            <g key={wireKey(wire)} className={`canvas-wire is-ribbon ${wireClass(wire)} ${selectedKey === wireKey(wire) ? 'is-selected' : ''}`}>
              <path className="canvas-ribbon" d={ribbon(fromSpan, toSpan, pinAnchor(target, targetShape, anchorOfTarget(wire.to)).y)} onPointerDown={select} />
            </g>
          )
        }
        const d = curve(from, pinAnchor(target, targetShape, anchorOfTarget(wire.to)))
        return (
          <g key={wireKey(wire)} className={`canvas-wire ${wireClass(wire)} ${selectedKey === wireKey(wire) ? 'is-selected' : ''}`}>
            <path className="canvas-wire-hit" d={d} onPointerDown={select} />
            <path className="canvas-wire-line" d={d} markerEnd="url(#canvas-arrow)" />
          </g>
        )
      })}
      {draft && draftFrom && <path className="canvas-wire-draft" d={curve(draftFrom, draft.to)} />}
    </svg>
  )
}
