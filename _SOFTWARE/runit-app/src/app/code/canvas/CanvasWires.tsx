import { doneKey, outputKey } from '@engine'
import type { BlockInstance, ObjectPath, Source } from '@engine'
import { useLive } from '../../run/live'
import { sameTarget, wiresOf } from '../blocks'
import type { BlockPin, PinTarget, Wire } from '../blocks'
import { CHIP_HEIGHT, CHIP_WIDTH, chipFor, curve, pinAnchor, ribbon } from './layout'
import type { BlockShape, CanvasLayout, Point, RibbonSpan } from './layout'

/*
 * The wires (world layer): a curve from an output to an input; a link into Run when as a faded band, narrow where it leaves its
 * source and as tall as the block where it arrives, so it reads as "powers this block"; and the wires of free chips.
 */

const anchorOfSource = (pin: BlockPin): 'eno' | { side: 'out'; index: number } => (pin === 'eno' ? pin : { side: 'out', index: pin })
const wireClass = (wire: Wire): string => (wire.from.pin === 'eno' ? 'is-eno' : wire.to.side === 'en' ? 'is-gate' : 'is-data')
export const wireKey = (wire: Wire): string => `${wire.from.block}:${wire.from.pin}>${wire.to.block}:${wire.to.side}${wire.to.index ?? ''}`

interface ChipWire {
  readonly key: string
  readonly target: PinTarget
  readonly d: string
  readonly gate: boolean
}

/** The wires between free chips and the pins that read or write their path: a chip feeds an input or Run when, an output feeds a chip. */
const chipWires = (layout: CanvasLayout, blocks: readonly BlockInstance[], place: (id: string) => Point, shapeOf: (b: BlockInstance) => BlockShape): ChipWire[] =>
  blocks.flatMap((block) => {
    const shape = shapeOf(block)
    const at = place(block.id)
    const wires: ChipWire[] = []
    const out = (c: Point): Point => ({ x: c.x + CHIP_WIDTH, y: c.y + CHIP_HEIGHT / 2 })
    const into = (c: Point): Point => ({ x: c.x, y: c.y + CHIP_HEIGHT / 2 })
    const variable = (s: Source | null | undefined): ObjectPath | undefined => (s?.kind === 'variable' ? s.path : undefined)
    block.inputs.forEach((s, index) => {
      const path = variable(s)
      const c = path && chipFor(layout, path)
      if (c) wires.push({ key: `${block.id}:in${index}`, target: { block: block.id, side: 'in', index }, d: curve(out(c), pinAnchor(at, shape, { side: 'in', index })), gate: false })
    })
    block.enables.forEach((s, index) => {
      const path = variable(s)
      const c = path && chipFor(layout, path)
      if (c) wires.push({ key: `${block.id}:en${index}`, target: { block: block.id, side: 'en', index }, d: curve(out(c), pinAnchor(at, shape, 'en')), gate: true })
    })
    block.outputs.forEach((t, index) => {
      const c = t && chipFor(layout, t.path)
      if (c) wires.push({ key: `${block.id}:out${index}`, target: { block: block.id, side: 'out', index }, d: curve(pinAnchor(at, shape, { side: 'out', index }), into(c)), gate: false })
    })
    return wires
  })

export function CanvasWires({ blocks, layout, place, shapeOf, selected, draft, chipDraft, selectedPin, onSelect, onSelectPin }: {
  blocks: readonly BlockInstance[]
  layout: CanvasLayout
  place: (id: string) => Point
  shapeOf: (block: BlockInstance) => BlockShape
  selected?: Wire
  /** A wire being dragged: from its source pin to the pointer. */
  draft?: { readonly from: { readonly block: string; readonly pin: BlockPin }; readonly to: Point }
  /** A wire being dragged from a free chip. */
  chipDraft?: { readonly from: Point; readonly to: Point }
  /** The pin whose wire to a free chip is selected. */
  selectedPin?: PinTarget
  onSelect: (wire: Wire) => void
  onSelectPin: (target: PinTarget) => void
}) {
  const live = useLive()
  const byId = new Map(blocks.map((b) => [b.id, b]))
  const selectedKey = selected && wireKey(selected)
  const draftBlock = draft && byId.get(draft.from.block)
  const draftFrom = draft && draftBlock && pinAnchor(place(draftBlock.id), shapeOf(draftBlock), anchorOfSource(draft.from.pin))
  return (
    <svg className="canvas-wires" aria-hidden="true">
      <defs>
        <marker id="canvas-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" markerUnits="userSpaceOnUse" orient="auto">
          <path d="M 0 1 L 9 5 L 0 9 z" fill="context-stroke" />
        </marker>
      </defs>
      {wiresOf(blocks).map((wire) => {
        const source = byId.get(wire.from.block)!
        const target = byId.get(wire.to.block)!
        const sourceShape = shapeOf(source)
        const targetShape = shapeOf(target)
        const sourceAt = place(source.id)
        const targetAt = place(target.id)
        const from = pinAnchor(sourceAt, sourceShape, anchorOfSource(wire.from.pin))
        const select = (event: React.PointerEvent) => {
          event.stopPropagation()
          onSelect(wire)
        }
        const key = wireKey(wire)
        // Lit while the block it comes from acts (When done), or just after the value it carries changed; a Run when from a pin while it is true.
        const lit = wire.from.pin === 'eno' ? live.active(doneKey(wire.from.block)) : wire.to.side === 'en' ? live.active(outputKey(wire.from.block, wire.from.pin)) : live.changed(outputKey(wire.from.block, wire.from.pin))
        // A boolean the board holds: the wire is green while it is true, red while it is false (the same as the lead of the pin).
        const held = live.flag(wire.from.pin === 'eno' ? doneKey(wire.from.block) : outputKey(wire.from.block, wire.from.pin))
        const level = held === undefined ? '' : held ? 'is-true' : 'is-false'
        if (wire.to.side === 'en') {
          const fromSpan: RibbonSpan = wire.from.pin === 'eno'
            ? { x: sourceAt.x + sourceShape.width, top: sourceAt.y + 1, bottom: sourceAt.y + sourceShape.height - 1 }
            : { x: from.x, top: from.y - 2.5, bottom: from.y + 2.5 }
          const toSpan: RibbonSpan = { x: targetAt.x, top: targetAt.y + 1, bottom: targetAt.y + targetShape.height - 1 }
          return (
            <g key={key} className={`canvas-wire is-ribbon ${wireClass(wire)} ${selectedKey === key ? 'is-selected' : ''} ${lit ? 'is-live' : ''} ${level}`}>
              <path className="canvas-ribbon" d={ribbon(fromSpan, toSpan, pinAnchor(targetAt, targetShape, 'en').y, wire.from.pin === 'eno')} onPointerDown={select} />
            </g>
          )
        }
        const d = curve(from, pinAnchor(targetAt, targetShape, { side: 'in', index: wire.to.index }))
        return (
          <g key={key} className={`canvas-wire ${wireClass(wire)} ${selectedKey === key ? 'is-selected' : ''} ${lit ? 'is-live' : ''} ${level}`}>
            <path className="canvas-wire-hit" d={d} onPointerDown={select} />
            <path className="canvas-wire-line" d={d} markerEnd="url(#canvas-arrow)" />
          </g>
        )
      })}
      {chipWires(layout, blocks, place, shapeOf).map((wire) => (
        <g key={wire.key} className={`canvas-wire ${wire.gate ? 'is-gate' : ''} ${sameTarget(selectedPin, wire.target) ? 'is-selected' : ''}`}>
          <path className="canvas-wire-hit" d={wire.d} onPointerDown={(event) => { event.stopPropagation(); onSelectPin(wire.target) }} />
          <path className="canvas-wire-line is-variable" d={wire.d} markerEnd="url(#canvas-arrow)" />
        </g>
      ))}
      {chipDraft && <path className="canvas-wire-draft" d={curve(chipDraft.from, chipDraft.to)} />}
      {draft && draftFrom && <path className="canvas-wire-draft" d={curve(draftFrom, draft.to)} />}
    </svg>
  )
}
