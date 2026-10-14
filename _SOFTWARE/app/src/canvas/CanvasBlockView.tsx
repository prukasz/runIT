import { useState } from 'react'
import { InlineRename } from '../components/InlineRename'
import { ValueKindBadge } from '../components/TypeBadge/TypeBadge'
import { chipLabel, snapPoint } from '../domain/canvas'
import type { CanvasBlock, ObjectPath, Point, WireSource, WireTarget } from '../domain/canvas'
import type { BlockDescriptor } from '../backend/vm/blocks'
import { formulaOf, formulaText, headlineOf, pathText } from '../blocks/blockModel'
import type { BlockShape, PinView } from '../blocks/blockModel'

/*
 * A function block: its headline, an EN strip on the left (what runs it), an ENO strip on the right (that it acted), input pins
 * on the left and output pins on the right. Moving commits one undo step; settings belong to the details panel.
 */

interface Props {
  readonly block: CanvasBlock
  readonly descriptor?: BlockDescriptor
  readonly shape: BlockShape
  readonly selected: boolean
  readonly zoom: number
  /** `additive`: Shift / Ctrl held, so the block joins the selection instead of replacing it. */
  readonly onSelect: (additive: boolean) => void
  /** A press released without moving the block: a tap. */
  readonly onTap?: () => void
  /** Called while the block is dragged with how far it has moved (undefined when the drag ends), so a selected group follows. */
  readonly onDrag?: (by: Point | undefined) => void
  /** Where the block is drawn, off its place: a block of a selected group while another one is dragged. */
  readonly offset?: Point
  readonly onMove: (to: Point) => void
  /** A press on an output or the ENO strip starts a wire from it. */
  readonly onWireStart?: (source: WireSource, event: React.PointerEvent) => void
  /** A path as the user reads it (names, not IDs). */
  readonly labelOf?: (path: ObjectPath) => string
  /** Whether a path is a variable of the tree (not another block's pin): those are drawn as a chip on the pin. */
  readonly isVariable?: (path: ObjectPath) => boolean
  /** Whether a path is shown as a free chip on the canvas: its pins are wired to that chip instead of showing a docked one. */
  readonly isFree?: (path: ObjectPath) => boolean
  /** A press on a docked variable chip: a click selects it (Delete takes it off the pin), a drag pulls it off to move it. */
  readonly onChipPress?: (target: WireTarget, path: ObjectPath, event: React.PointerEvent) => void
  /** The docked chip selected on this block. */
  readonly selectedChip?: WireTarget
  /** The block's name changed (double-click on its title); empty = no name. */
  readonly onRename?: (name: string | undefined) => void
}

export function CanvasBlockView({ block, descriptor, shape, selected, zoom, onSelect, onMove, onTap, onDrag, offset, onWireStart, labelOf = pathText, isVariable = () => false, isFree = () => false, onChipPress, selectedChip, onRename }: Props) {
  const [drag, setDrag] = useState<{ start: Point; origin: Point; at: Point }>()
  const [renaming, setRenaming] = useState(false)
  const title = descriptor?.alias ?? `Unknown ${block.type}`
  const head = headlineOf(descriptor, block, labelOf)
  // An expression block's formula, in its body under the pins (as in the legacy app): `Result = (speed + 2) * IN1`.
  const formula = formulaOf(descriptor, block)
  const outputLabel = block.outputs?.[0] ? labelOf({ root: block.outputs[0] }) : block.outputAliases?.[0] || shape.outputs[0]?.title || 'Result'
  const enables = block.enables?.length ?? 0
  const at = drag?.at ?? (offset ? { x: block.x + offset.x, y: block.y + offset.y } : block)

  const commitName = (text: string) => {
    setRenaming(false)
    const name = text.trim()
    if (name !== (block.name ?? '')) onRename?.(name || undefined)
  }

  const wireFrom = (pin: string) => (event: React.PointerEvent) => {
    if (event.button !== 0 || !onWireStart) return
    event.stopPropagation()
    onWireStart({ block: block.id, pin }, event)
  }

  const pinView = (pin: PinView, index: number, output: boolean) => {
    const source = output ? block.outputs?.[index] : block.inputs?.[index]
    const connected = !!source
    const text = source ? labelOf(typeof source === 'string' ? { root: source } : source) : output ? `${block.id}:q${index}` : 'Unwired'
    const pinTitle = output ? block.outputAliases?.[index] || pin.title : pin.title
    const path: ObjectPath | undefined = source ? (typeof source === 'string' ? { root: source } : source) : undefined
    const chip = path && path.root !== '' && isVariable(path) && !isFree(path) ? { label: chipLabel(labelOf(path)), full: labelOf(path) } : undefined
    return (
      <div key={index} data-block={block.id} data-pin-kind={output ? 'out' : 'in'} data-pin-index={pin.index} className={`canvas-block-pin ${chip ? 'has-docked-chip' : ''} ${pin.required ? 'is-required' : ''} ${connected ? 'is-connected' : ''} ${pin.value === 'gate' || pin.value === 'bool' ? 'is-gate' : ''}`} title={`${pinTitle} (${pin.value})${pin.required ? ', required' : ''} · ${text}`}>
        {chip && (
          <span
            className={`canvas-chip is-docked ${output ? 'is-out' : 'is-in'} ${selectedChip && selectedChip.kind === (output ? 'out' : 'in') && 'index' in selectedChip && selectedChip.index === index ? 'is-selected' : ''}`}
            title={`${chip.full}: click to select (Delete takes it off), drag it onto a pin to move it`}
            onPointerDown={(event) => {
              if (event.button !== 0 || !onChipPress) return
              event.stopPropagation()
              onChipPress(output ? { block: block.id, kind: 'out', index } : { block: block.id, kind: 'in', index }, path!, event)
            }}
          >
            <span className="canvas-chip-text">{chip.label}</span>
          </span>
        )}
        {!output ? (
          <div className="canvas-pin-slab is-in">
            <span className="canvas-block-input-type"><ValueKindBadge type={pin.value} /></span>
            {!pin.quiet && <span className="canvas-block-pin-label"><span className="canvas-block-pin-name">{pinTitle}</span></span>}
          </div>
        ) : (
          <div className="canvas-pin-slab is-out">
            <span className="canvas-block-pin-label"><span className="canvas-block-pin-name">{pinTitle}</span></span>
            <span aria-hidden="true" className="canvas-block-output-type canvas-wire-start" title={`Drag to wire ${pin.title}`} onPointerDown={wireFrom(`q${index}`)}>
              <ValueKindBadge type={pin.value} />
            </span>
          </div>
        )}
      </div>
    )
  }

  return (
    <div
      className={['canvas-block', `cat-${descriptor?.category ?? 'unknown'}`, selected && 'selected', drag && 'is-dragging'].filter(Boolean).join(' ')}
      role="group"
      tabIndex={0}
      aria-current={selected ? 'true' : undefined}
      aria-label={`${title} block ${block.id}`}
      style={{ left: at.x, top: at.y, width: shape.width, height: shape.height }}
      onPointerDown={(event) => {
        if (event.button !== 0 || (event.target as HTMLElement).closest('button')) return
        event.stopPropagation()
        event.currentTarget.setPointerCapture(event.pointerId)
        onSelect(event.shiftKey || event.ctrlKey || event.metaKey)
        setDrag({ start: { x: event.clientX, y: event.clientY }, origin: { x: block.x, y: block.y }, at: { x: block.x, y: block.y } })
      }}
      onPointerMove={(event) => {
        if (!drag) return
        const raw = { x: drag.origin.x + (event.clientX - drag.start.x) / zoom, y: drag.origin.y + (event.clientY - drag.start.y) / zoom }
        const to = snapPoint(raw)
        setDrag({ ...drag, at: to })
        onDrag?.({ x: to.x - drag.origin.x, y: to.y - drag.origin.y })
      }}
      onPointerUp={() => {
        if (drag && (drag.at.x !== drag.origin.x || drag.at.y !== drag.origin.y)) onMove(drag.at)
        else if (drag) onTap?.()
        setDrag(undefined)
        onDrag?.(undefined)
      }}
      onPointerCancel={() => { setDrag(undefined); onDrag?.(undefined) }}
      onDoubleClick={(event) => {
        // The block holds the pointer while pressed, so the double-click lands here: only the header renames.
        const rect = event.currentTarget.getBoundingClientRect()
        if (onRename && event.clientY - rect.top < 40 * zoom) setRenaming(true)
      }}
      onKeyDown={(event) => { if (event.target === event.currentTarget && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); onSelect(event.shiftKey || event.ctrlKey || event.metaKey) } }}
    >
      <span className="canvas-block-enable is-en" role="img" aria-label={`Run when connector ${block.id}`} data-pin="en" data-block={block.id} data-pin-kind="en" title={`Run when: ${enables ? `${block.enableMode === 'all' ? 'all' : 'any'} of ${enables} sources\n${block.enables!.map(labelOf).join('\n')}` : 'nothing set, always runs'}`} />
      <span className="canvas-block-enable is-eno canvas-wire-start" role="img" aria-label={`When done connector ${block.id}`} data-pin="eno" title="When done: true while the block acted; drag it onto the Run when of another block to run that block only then" onPointerDown={wireFrom('eno')} />
      <div className="canvas-block-header">
        <span className="canvas-block-heading">
          {renaming ? (
            <InlineRename className="canvas-block-rename" value={block.name ?? ''} placeholder={title} label={`Name of ${block.id}`} onCommit={commitName} onCancel={() => setRenaming(false)} />
          ) : (
            <span className="canvas-block-title" title={onRename ? 'Double-click the header to name it' : undefined}>
              {block.name ?? <>{head.lead}{head.value && <> <span className="canvas-block-value">{head.value}</span></>}</>}
            </span>
          )}
        </span>
      </div>
      <div className="canvas-block-pins">
        <div className="canvas-block-side is-in">{shape.inputs.map((pin) => pinView(pin, pin.index, false))}</div>
        <div className="canvas-block-side is-out">{shape.outputs.map((pin, index) => pinView(pin, index, true))}</div>
      </div>
      {formula && (
        <div className="canvas-block-summary is-formula" title={formulaText(descriptor, block, labelOf, outputLabel)}>
          {formula.state !== 'ok' ? (
            <span className="is-unwired">{formula.state === 'empty' ? 'Set a formula' : 'Invalid formula'}</span>
          ) : (
            <div className="canvas-formula-line">
              {formula.parts.map((part, index) => {
                if (part.kind === 'out') return <span key={index} className="expr-target is-output">{outputLabel}</span>
                if (part.kind === 'in') {
                  const path = block.inputs?.[part.pin]
                  const wired = !!path && path.root !== ''
                  return <span key={index} className={`expr-target ${wired ? '' : 'is-unwired'}`} title={`IN${part.pin}${wired ? `: ${labelOf(path!)}` : ': not wired'}`}>{wired ? labelOf(path!) : part.text}</span>
                }
                return part.text.trim() ? <span key={index} className="canvas-formula-tok">{part.text.trim()}</span> : null
              }).flatMap((node, index) => (index === 1 ? [<span key="eq" className="canvas-formula-op">=</span>, node] : [node]))}
            </div>
          )}
        </div>
      )}
    </div>
  )
}
