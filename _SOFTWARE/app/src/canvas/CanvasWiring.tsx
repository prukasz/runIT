import { accepts, wiresOf } from '../domain/canvas'
import type { Point, Viewport, Wire, WireKind, WireSource, WireTarget } from '../domain/canvas'
import { canvasToScreen } from '../domain/canvas'
import { blockPinAt } from '../domain/descriptors'
import type { VmBlockType } from '../domain/descriptors'
import type { CanvasBlock, CanvasVariable, CanvasVariableTarget, ObjectPath, ProjectCanvas } from '../domain/project'
import { pathText, pinAnchor } from './blockView'
import type { BlockShape } from './blockView'
import { TypeBadge } from '../components/TypeBadge/TypeBadge'

/*
 * Wires between blocks (an SVG in the world layer, so it pans and zooms with
 * the blocks) and the pin picker: while a wire or a variable is dragged over a
 * block, an enlarged list of its EN, inputs (and, for a variable, outputs)
 * opens over it; only what fits can be chosen (features.md §8.1).
 */

/** What is being connected: a block pin (a wire) or a variable (from the tree, a chip; no path = an empty chip). */
export type Pending =
  | { readonly kind: 'wire'; readonly from: WireSource; readonly carries: WireKind; /** A FOR's ENO: puts blocks in the loop, EN only. */ readonly loop?: boolean }
  | { readonly kind: 'variable'; readonly path?: ObjectPath; readonly carries: WireKind; readonly moveFrom?: WireTarget; /** A free chip dropped back on a pin: it goes. */ readonly fromFree?: string }

/** A chip pulled onto the canvas: its size in canvas units (the wire leaves from its right edge). */
export const CHIP_WIDTH = 120
export const CHIP_HEIGHT = 24

const samePath = (a: ObjectPath | null | undefined, b: ObjectPath): boolean => !!a && JSON.stringify(a) === JSON.stringify(b)

const sameTarget = (a: CanvasVariableTarget, b: CanvasVariableTarget): boolean =>
  a.block === b.block && a.kind === b.kind && (a.kind === 'in' ? b.kind === 'in' && a.index === b.index : b.kind === 'en' && a.index === b.index)

const asCanvasTarget = (target: WireTarget): CanvasVariableTarget | undefined => {
  if (target.kind === 'in') return { block: target.block, kind: 'in', index: target.index }
  if (target.kind === 'en') return { block: target.block, kind: 'en', ...('index' in target && target.index !== undefined ? { index: target.index } : {}) }
  return undefined
}

/** Whether a free label replaces the docked chip at this input or enable. */
export const chipAtTarget = (canvas: ProjectCanvas, path: ObjectPath, target: WireTarget): boolean => {
  const pin = asCanvasTarget(target)
  return !!pin && (canvas.variables ?? []).some((variable) =>
    samePath(variable.path, path) && (variable.targets === undefined || variable.targets.some((entry) => sameTarget(entry, pin))),
  )
}

/** The pins and EN strips of a canvas reading a free chip's path. */
export const chipTargets = (canvas: ProjectCanvas, variable: CanvasVariable): WireTarget[] =>
  canvas.blocks.flatMap((block) => [
    ...(block.inputs ?? []).flatMap((path, index) => (samePath(path, variable.path) ? [{ block: block.id, kind: 'in' as const, index }] : [])),
    ...(block.enables ?? []).flatMap((path, index) => (samePath(path, variable.path) ? [{ block: block.id, kind: 'en' as const, index }] : [])),
  ])
    .filter((target) => variable.targets === undefined || variable.targets.some((entry) => sameTarget(entry, target)))

export interface PickerRow {
  readonly key: string
  readonly label: string
  readonly detail: string
  readonly type?: string
  readonly target: WireTarget
  readonly ok: boolean
}

const anchorOfSource = (pin: string): 'eno' | { side: 'out'; index: number } =>
  pin === 'eno' ? pin : { side: 'out', index: Number(pin.slice(1)) }

const anchorOfTarget = (target: WireTarget): 'en' | { side: 'in' | 'out'; index: number } => (target.kind === 'en' ? 'en' : { side: target.kind, index: target.index })

const curve = (a: Point, b: Point): string => {
  const dx = Math.max(40, Math.abs(b.x - a.x) / 2)
  return `M ${a.x} ${a.y} C ${a.x + dx} ${a.y}, ${b.x - dx} ${b.y}, ${b.x} ${b.y}`
}

const wireClass = (wire: Wire, blocks: ReadonlyMap<string, CanvasBlock>): string =>
  wire.from.pin === 'eno' || wire.from.pin === 'body' ? (blocks.get(wire.from.block)?.type === 'FOR' ? 'is-loop' : 'is-eno') : wire.to.kind === 'en' ? 'is-gate' : 'is-data'

/**
 * An EN link as a faded ribbon: narrow where it leaves its source, as tall as
 * the whole red EN strip where it arrives, so it reads as "powers this block".
 */
interface RibbonSpan {
  readonly x: number
  readonly top: number
  readonly bottom: number
}

const ribbon = (from: RibbonSpan, to: RibbonSpan): string => {
  const dx = Math.max(40, Math.abs(to.x - from.x) / 2)
  return `M ${from.x} ${from.top} C ${from.x + dx} ${from.top}, ${to.x - dx} ${to.top}, ${to.x} ${to.top} L ${to.x} ${to.bottom} C ${to.x - dx} ${to.bottom}, ${from.x + dx} ${from.bottom}, ${from.x} ${from.bottom} Z`
}

/** The rows a block offers for what is being dragged: EN first, then inputs, then (a variable) outputs. */
export const pickerRows = (block: CanvasBlock, type: VmBlockType | undefined, shape: BlockShape, pending: Pending, labelOf: (path: ObjectPath) => string = pathText): PickerRow[] => {
  const fromBlock = pending.kind === 'wire'
  const body = pending.kind === 'wire' && !!pending.loop
  const rows: PickerRow[] = [{
    key: 'en',
    label: 'EN',
    detail: body ? 'put it in the loop' : block.enables?.length ? `enable, ${block.enables.length} already` : 'enable: runs only while true',
    type: 'gate',
    target: { block: block.id, kind: 'en' },
    ok: accepts('en', pending.carries, { fromBlock, body }),
  }]
  for (const pin of shape.inputs) {
    const current = block.inputs?.[pin.index]
    const taken = Boolean(current && (typeof current === 'string' ? current !== '' : current.root !== ''))
    if (taken) continue
    rows.push({ key: `in${pin.index}`, label: pin.title, detail: '', type: pin.value, target: { block: block.id, kind: 'in', index: pin.index }, ok: accepts(pin.value, pending.carries, { fromBlock, body }) })
  }
  // A block whose inputs repeat (EXPR, EXPR_BIT) takes one more.
  const next = Math.max(-1, ...shape.inputs.map((pin) => pin.index)) + 1
  const nextPin = type && next < type.inputs.max ? blockPinAt(type.inputs, next) : undefined
  if (nextPin && !body) rows.push({ key: `in${next}`, label: 'New input', detail: 'adds input', type: nextPin.value, target: { block: block.id, kind: 'in', index: next }, ok: accepts(nextPin.value, pending.carries, { fromBlock }) })
  if (pending.kind === 'variable' && type) {
    shape.outputs.forEach((pin, index) => {
      const current = block.outputs?.[index]
      rows.push({ key: `out${index}`, label: `${pin.title} →`, detail: current ? `writes ${labelOf({ root: current })}` : 'the block writes the variable', type: pin.value, target: { block: block.id, kind: 'out', index }, ok: (pending.carries === 'number' || pending.carries === 'bool' || pending.carries === 'any') && pin.value !== 'gate' })
    })
  }
  return rows
}

export function CanvasWires({ canvas, shapeOf, selected, draft, onSelect }: {
  canvas: ProjectCanvas
  shapeOf: (block: CanvasBlock) => BlockShape
  selected?: Wire
  /** A wire being dragged: from its source (a block pin, or a canvas point for a chip) to the pointer. */
  draft?: { from: WireSource | Point; to: Point }
  onSelect: (wire: Wire) => void
}) {
  const blocks = new Map(canvas.blocks.map((block) => [block.id, block]))
  const key = (wire: Wire) => `${wire.from.block}:${wire.from.pin}>${wire.to.block}:${wire.to.kind}${'index' in wire.to ? wire.to.index : ''}:${wire.enableIndex ?? ''}`
  const selectedKey = selected && key(selected)
  const draftFrom = draft && ('block' in draft.from ? (() => {
    const source = blocks.get((draft.from as WireSource).block)
    return source && pinAnchor(source, shapeOf(source), anchorOfSource((draft.from as WireSource).pin))
  })() : draft.from)
  return (
    <svg className="canvas-wires" aria-hidden="true">
      {wiresOf(canvas).map((wire) => {
        const source = blocks.get(wire.from.block)!
        const target = blocks.get(wire.to.block)!
        const from = pinAnchor(source, shapeOf(source), anchorOfSource(wire.from.pin))
        const select = (event: React.PointerEvent) => { event.stopPropagation(); onSelect(wire) }
        if (wire.to.kind === 'en') {
          const sourceShape = shapeOf(source)
          const targetShape = shapeOf(target)
          const fromSpan: RibbonSpan = wire.from.pin === 'eno'
            ? { x: source.x + sourceShape.width, top: source.y + 1, bottom: source.y + sourceShape.height - 1 }
            : { x: from.x, top: from.y - 2.5, bottom: from.y + 2.5 }
          const toSpan: RibbonSpan = { x: target.x, top: target.y + 1, bottom: target.y + targetShape.height - 1 }
          const d = ribbon(fromSpan, toSpan)
          return (
            <g key={key(wire)} className={`canvas-wire is-ribbon ${wireClass(wire, blocks)} ${selectedKey === key(wire) ? 'is-selected' : ''}`}>
              <path className="canvas-ribbon" d={d} onPointerDown={select} />
            </g>
          )
        }
        const d = curve(from, pinAnchor(target, shapeOf(target), anchorOfTarget(wire.to)))
        return (
          <g key={key(wire)} className={`canvas-wire ${wireClass(wire, blocks)} ${selectedKey === key(wire) ? 'is-selected' : ''}`}>
            <path className="canvas-wire-hit" d={d} onPointerDown={select} />
            <path className="canvas-wire-line" d={d} />
          </g>
        )
      })}
      {(canvas.variables ?? []).flatMap((variable) => chipTargets(canvas, variable).map((target) => {
        const block = blocks.get(target.block)!
        const from = { x: variable.x + CHIP_WIDTH, y: variable.y + CHIP_HEIGHT / 2 }
        const key = `${variable.id}>${target.block}:${target.kind}${'index' in target ? target.index : ''}`
        const blockShape = shapeOf(block)
        return target.kind === 'en'
          ? <path key={key} className="canvas-ribbon is-variable" d={ribbon({ x: from.x, top: from.y - 2.5, bottom: from.y + 2.5 }, { x: block.x, top: block.y + 1, bottom: block.y + blockShape.height - 1 })} />
          : <path key={key} className="canvas-wire-line is-variable" d={curve(from, pinAnchor(block, blockShape, anchorOfTarget(target)))} />
      }))}
      {draft && draftFrom && <path className="canvas-wire-draft" d={curve(draftFrom, draft.to)} />}
    </svg>
  )
}

export function WirePicker({ block, shape, rows, viewport, hovered, picking, onPick, onCancel }: {
  block: CanvasBlock
  shape: BlockShape
  rows: readonly PickerRow[]
  viewport: Viewport
  hovered?: string
  /** Dropped on the block itself: the picker stays until a row is clicked. */
  picking: boolean
  onPick: (row: PickerRow) => void
  onCancel: () => void
}) {
  const at = canvasToScreen(viewport, { x: block.x, y: block.y })
  const width = Math.max(220, shape.width * viewport.zoom)
  const enRow = rows.find((r) => r.target.kind === 'en')
  const pinRows = rows.filter((r) => r.target.kind !== 'en')

  return (
    <div className="wire-picker" role="listbox" aria-label={`Connect to ${block.id}`} style={{ left: at.x, top: at.y, width }}>
      {enRow && (
        <button
          type="button"
          role="option"
          aria-selected={hovered === enRow.key}
          aria-disabled={!enRow.ok}
          disabled={!enRow.ok}
          data-wire-target={enRow.key}
          className={`wire-picker-tab-en ${hovered === enRow.key && enRow.ok ? 'is-hovered' : ''}`}
          onClick={() => enRow.ok && onPick(enRow)}
        >
          <span className="wire-picker-en-pill">EN</span>
          <span className="wire-picker-detail">
            {enRow.ok && enRow.type && <TypeBadge type={enRow.type} />}
            <span className="wire-picker-text">{enRow.ok ? enRow.detail : "doesn't fit"}</span>
          </span>
        </button>
      )}
      <div className="wire-picker-head">
        <span>{picking ? `Choose a pin of ${block.id}` : block.id}</span>
        {picking && <button type="button" onClick={onCancel} aria-label="Cancel">×</button>}
      </div>
      {pinRows.length > 0 ? (
        pinRows.map((row) => (
          <button
            key={row.key}
            type="button"
            role="option"
            aria-selected={hovered === row.key}
            aria-disabled={!row.ok}
            disabled={!row.ok}
            data-wire-target={row.key}
            className={`wire-picker-row ${hovered === row.key && row.ok ? 'is-hovered' : ''}`}
            onClick={() => row.ok && onPick(row)}
          >
            <span className="wire-picker-label">{row.label}</span>
            <span className="wire-picker-detail">
              {row.ok && row.type && <TypeBadge type={row.type} />}
              <span className="wire-picker-text">{row.ok ? row.detail : "doesn't fit"}</span>
            </span>
          </button>
        ))
      ) : (
        <div className="wire-picker-empty">All inputs connected</div>
      )}
    </div>
  )
}

/** A chip pulled onto the canvas: drag it to move, drag from its handle to wire it to more pins. */
export function FreeChip({ variable, label, full, selected, onPress, onWireStart }: {
  variable: CanvasVariable
  label: string
  full: string
  selected: boolean
  onPress: (event: React.PointerEvent) => void
  onWireStart: (event: React.PointerEvent) => void
}) {
  return (
    <div
      className={`canvas-chip is-free ${selected ? 'is-selected' : ''}`}
      style={{ left: variable.x, top: variable.y, width: CHIP_WIDTH, height: CHIP_HEIGHT }}
      title={`${full}: drag to move, drag from the dot to wire it`}
      onPointerDown={(event) => {
        if (event.button !== 0) return
        event.stopPropagation()
        onPress(event)
      }}
    >
      <span>{label}</span>
      <i className="canvas-wire-start" aria-hidden="true" onPointerDown={(event) => {
        if (event.button !== 0) return
        event.stopPropagation()
        onWireStart(event)
      }} />
    </div>
  )
}
