import { useState } from 'react'
import { TriangleAlert } from 'lucide-react'
import { doneKey, outputKey, variableKey, variantOf } from '@engine'
import type { BlockInstance, ObjectPath, Source } from '@engine'
import { Badge, TypeLead } from '@ui/components'
import { chipLabel, deviceLine, fieldText, formulaParts, headline, pinsOf, sourceLabel } from '../blocks'
import type { BlockContext, BlockPin, PinTarget } from '../blocks'
import { pinLead } from '../PathEditor'
import { useLive } from '../../run/live'
import { snapPoint } from './layout'
import type { BlockShape, PinView, Point } from './layout'

/*
 * A block on the canvas: its header (the descriptor's words with the values filled in, or the user's name), the Run when strip on
 * the left (what runs it), the When done strip on the right (that it acted), its inputs on the left and outputs on the right, and an
 * expression block's formula under them. A variable a pin reads (or an output writes) sits on the pin as a chip. Moving commits
 * one undo step; the settings (and the name) belong to the details panel, which a double-click opens.
 */

interface Props {
  readonly block: BlockInstance
  readonly ctx: BlockContext
  readonly category: string
  readonly at: Point
  readonly shape: BlockShape
  readonly selected: boolean
  /** The board reported an error about it that is not read yet: the whole block is red. */
  readonly failed: boolean
  /** What the compiler says is wrong with it (a mark in its header), if anything. */
  readonly problem?: string
  readonly zoom: number
  /** `additive`: Shift / Ctrl held, so the block joins the selection instead of replacing it. */
  readonly onSelect: (additive: boolean) => void
  /** A press released without moving the block: a tap. */
  readonly onTap?: () => void
  /** While the block is dragged, how far it has moved (undefined when the drag ends), so a selected group follows. */
  readonly onDrag?: (by: Point | undefined) => void
  /** Where the block is drawn, off its place: a block of a selected group while another one is dragged. */
  readonly offset?: Point
  readonly onMove: (to: Point) => void
  /** A press on an output or the When done strip starts a wire from it. */
  readonly onWireStart?: (pin: BlockPin, event: React.PointerEvent) => void
  /** Whether a path is shown as a free chip on the canvas: its pins are wired to that chip instead of holding one. */
  readonly isFree: (path: ObjectPath) => boolean
  /** A press on a chip on a pin: a click selects it (Delete takes it off), a drag pulls it off to move it. */
  readonly onChipPress?: (target: PinTarget, path: ObjectPath, event: React.PointerEvent) => void
  readonly selectedChip?: PinTarget
  /** A double-click: show the block's details, or hide them. */
  readonly onOpen?: () => void
}

const isVariable = (s: Source | null | undefined): s is Extract<Source, { kind: 'variable' }> => s?.kind === 'variable'

export function CanvasBlockView({ block, ctx, category, at: place, shape, selected, failed, problem, zoom, onSelect, onMove, onTap, onDrag, offset, onWireStart, isFree, onChipPress, selectedChip, onOpen }: Props) {
  const [drag, setDrag] = useState<{ start: Point; origin: Point; at: Point }>()
  // While the program is watched on the board: this block lit while it acts, and what its outputs hold.
  const live = useLive()
  const pulsing = live.active(doneKey(block.id))
  const type = ctx.catalog.get(block.type)
  const title = type?.alias ?? `Unknown ${block.type}`
  const head = headline(ctx, block, true)
  const device = deviceLine(ctx, block)
  const suspended = typeof block.device === 'number' && ctx.deviceSuspended?.(block.device) === true
  const formula = formulaParts(ctx, block)
  const outputLabel = block.outputs[0] ? sourceLabel(ctx, { kind: 'variable', path: block.outputs[0].path }) : shape.outputs[0]?.title ?? 'Result'
  const at = drag?.at ?? (offset ? { x: place.x + offset.x, y: place.y + offset.y } : place)
  const enables = block.enables
  const enChips = enables.flatMap((s, index) => (isVariable(s) && !isFree(s.path) ? [{ index, path: s.path }] : []))
  // An input that stands for a setting shows the setting's value on it while nothing is wired (`#1`, `Low`).
  const concrete = pinsOf(ctx.catalog, block)
  const settings = type ? [...type.fields, ...(variantOf(type, block.variant)?.fields ?? [])] : []
  const constantOf = (index: number): string | undefined => {
    const pin = concrete?.inputs[index]?.pin
    const id = pin?.field ?? pin?.overrides
    const field = id === undefined ? undefined : settings.find((f) => f.id === id)
    return field && (field.fixed ?? block.values?.[field.id] ?? field.default) !== undefined ? fieldText(ctx, block, field) : undefined
  }

  const wireFrom = (pin: BlockPin) => (event: React.PointerEvent) => {
    if (event.button !== 0 || !onWireStart) return
    event.stopPropagation()
    onWireStart(pin, event)
  }

  const chip = (target: PinTarget, path: ObjectPath, side: 'in' | 'out' | 'en') => {
    const full = sourceLabel(ctx, { kind: 'variable', path })
    const isSelected = selectedChip?.side === target.side && (selectedChip.index ?? -1) === (target.index ?? -1) && selectedChip.block === block.id
    return (
      <span key={`${side}${target.index ?? ''}`} className={`canvas-chip is-docked is-${side} ${isSelected ? 'is-selected' : ''}`} title={`${full}: click to select (Delete takes it off), drag it onto a pin to move it`}
        onPointerDown={(event) => {
          if (event.button !== 0 || !onChipPress) return
          event.stopPropagation()
          onChipPress(target, path, event)
        }}>
        <span className="canvas-chip-text">{chipLabel(full)}</span>
      </span>
    )
  }

  const pinView = (pin: PinView, output: boolean) => {
    const source = output ? undefined : block.inputs[pin.index]
    const written = output ? block.outputs[pin.index] : undefined
    const path = output ? written?.path : isVariable(source) ? source.path : undefined
    const connected = output ? !!written : !!source
    const text = source ? sourceLabel(ctx, source) : written ? sourceLabel(ctx, { kind: 'variable', path: written.path }) : output ? 'The block\'s own output' : 'Unwired'
    const constant = !output && !source ? constantOf(pin.index) : undefined
    const docked = path && !isFree(path)
      ? chip({ block: block.id, side: output ? 'out' : 'in', index: pin.index } as PinTarget, path, output ? 'out' : 'in')
      : constant !== undefined
      ? <span className="canvas-chip is-docked is-in is-constant" title={`${pin.title}: ${constant} (its setting; wire a variable to read it instead)`}><span className="canvas-chip-text">{constant}</span></span>
      : undefined
    // What the pin holds on the board now (a watched program): a boolean's lead goes green when true, red when false.
    const liveKey = output ? outputKey(block.id, pin.index)
      : source?.kind === 'variable' && source.path.steps.length === 0 ? variableKey(source.path.root)
      : source?.kind === 'output' ? outputKey(source.block, source.pin)
      : source?.kind === 'eno' ? doneKey(source.block) : undefined
    const level = liveKey === undefined ? undefined : live.flag(liveKey)
    const held = level !== undefined ? String(level) : liveKey === undefined ? undefined : live.text(liveKey)
    const flag = pin.value === 'bool' || pin.value === 'flow' ? level : undefined
    const now = output ? held : undefined
    return (
      <div key={pin.index} data-block={block.id} data-pin-kind={output ? 'out' : 'in'} data-pin-index={pin.index}
        className={`canvas-block-pin ${docked ? 'has-docked-chip' : ''} ${pin.required && !connected ? 'is-required' : ''} ${connected ? 'is-connected' : ''}`}
        title={`${pin.title} (${pin.value})${pin.required ? ', required' : ''} · ${text}`}>
        {docked}
        {now !== undefined && <span className={`canvas-pin-live ${live.changed(outputKey(block.id, pin.index)) ? 'is-changed' : ''}`} title={`${pin.title} now: ${now}`}>{now}</span>}
        {!output ? (
          <div className="canvas-pin-slab is-in">
            <span className="canvas-block-type"><TypeLead kind={pinLead(pin.value)} {...(flag === undefined ? {} : { value: flag })} /></span>
            {!pin.quiet && <span className="canvas-block-pin-name">{pin.title}</span>}
          </div>
        ) : (
          <div className="canvas-pin-slab is-out">
            {!pin.quiet && <span className="canvas-block-pin-name">{pin.title}</span>}
            <span aria-hidden="true" className="canvas-block-type canvas-wire-start" title={`Drag to wire ${pin.title}`} onPointerDown={wireFrom(pin.index)}>
              <TypeLead kind={pinLead(pin.value)} {...(flag === undefined ? {} : { value: flag })} />
            </span>
          </div>
        )}
      </div>
    )
  }

  return (
    <div
      className={['canvas-block', `cat-${category}`, selected && 'selected', drag && 'is-dragging', failed && 'has-errors', live.mode && 'is-watched', pulsing && 'is-pulse', live.next === block.id && 'is-next'].filter(Boolean).join(' ')}
      role="group"
      tabIndex={0}
      data-key={`block:${block.id}`}
      aria-current={selected ? 'true' : undefined}
      aria-label={`${title} block`}
      style={{ left: at.x, top: at.y, width: shape.width, height: shape.height }}
      onPointerDown={(event) => {
        if (event.button !== 0 || (event.target as HTMLElement).closest('button, input')) return
        event.stopPropagation()
        event.currentTarget.setPointerCapture(event.pointerId)
        onSelect(event.shiftKey || event.ctrlKey || event.metaKey)
        setDrag({ start: { x: event.clientX, y: event.clientY }, origin: place, at: place })
      }}
      onPointerMove={(event) => {
        if (!drag) return
        const to = snapPoint({ x: drag.origin.x + (event.clientX - drag.start.x) / zoom, y: drag.origin.y + (event.clientY - drag.start.y) / zoom })
        if (to.x === drag.at.x && to.y === drag.at.y) return
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
        // The block holds the pointer while pressed, so the double-click lands here, wherever on the block it was.
        if ((event.target as HTMLElement).closest('.canvas-chip, .canvas-wire-start')) return
        onOpen?.()
      }}
      onKeyDown={(event) => { if (event.target === event.currentTarget && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); onSelect(event.shiftKey || event.ctrlKey || event.metaKey) } }}
    >
      <span className="canvas-block-enable is-en" role="img" aria-label="Run when" data-block={block.id} data-pin-kind="en"
        title={`Run when: ${enables.length ? `${block.enableMode === 'all' ? 'all' : 'any'} of ${enables.length}\n${enables.map((s) => sourceLabel(ctx, s)).join('\n')}` : 'nothing set, it runs on every pass'}`} />
      <span className="canvas-block-enable is-eno canvas-wire-start" role="img" aria-label={type?.eno?.alias ?? 'When done'}
        title={`${type?.eno?.alias ?? 'When done'}: ${type?.eno?.description ?? 'true while the block acted'}. Drag it onto another block to run that block only then.`} onPointerDown={wireFrom('eno')} />
      {enChips.length > 0 && <span className="canvas-block-en-chips">{enChips.map((c) => chip({ block: block.id, side: 'en', index: c.index }, c.path, 'en'))}</span>}
      <div className="canvas-block-header">
        {problem && <span className="canvas-block-problem" title={problem}><TriangleAlert aria-label="To fix" /></span>}
        <span className="canvas-block-heading">
          <span className="canvas-block-title" title={`${title}: double-click to show or hide its details`}>
            {block.name?.trim() || (formula ? title : head.map((part, i) => (part.value ? <span key={i} className="canvas-block-value">{part.text}</span> : <span key={i}>{part.text}</span>)))}
          </span>
        </span>
      </div>
      <div className="canvas-block-pins">
        <div className="canvas-block-side is-in">{shape.inputs.map((pin) => pinView(pin, false))}</div>
        <div className="canvas-block-side is-out">{shape.outputs.map((pin) => pinView(pin, true))}</div>
      </div>
      {device !== undefined && (
        <div className={`canvas-block-summary is-device ${block.device === null || block.device === undefined ? 'is-missing' : ''}`} title={`Device: ${device}${suspended ? ' (suspended)' : ''}`}>
          <span className="canvas-block-device-name">{device}</span>
          {suspended && <Badge tone="warning" size="compact" title="Suspended on the board: its outputs are safe and it refuses writes until resumed">suspended</Badge>}
        </div>
      )}
      {formula && (
        <div className="canvas-block-summary is-formula">
          {formula.state !== 'ok' ? (
            <span className="is-unwired">Set a formula</span>
          ) : (
            <div className="canvas-formula-line">
              <span className="expr-target is-output">{outputLabel}</span>
              <span className="canvas-formula-op">=</span>
              {formula.parts.map((part, i) => {
                if (part.kind === 'in') {
                  const source = block.inputs[part.pin]
                  return <span key={i} className={`expr-target ${source ? '' : 'is-unwired'}`} title={source ? sourceLabel(ctx, source) : 'Not wired'}>{source ? sourceLabel(ctx, source) : '?'}</span>
                }
                return part.text.trim() ? <span key={i} className="canvas-formula-tok">{part.text.trim()}</span> : null
              })}
            </div>
          )}
        </div>
      )}
    </div>
  )
}
