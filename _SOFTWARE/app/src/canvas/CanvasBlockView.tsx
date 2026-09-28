import { useState } from 'react'
import { snapPoint } from '../domain/canvas'
import type { Point, WireSource, WireTarget } from '../domain/canvas'
import type { DeviceCatalog, VmBlockType } from '../domain/descriptors'
import type { CanvasBlock, ObjectPath, ProjectDevice } from '../domain/project'
import { blockSummary, pathText } from './blockView'
import type { BlockPinView, BlockShape } from './blockView'
import { ValueKindBadge } from '../components/TypeBadge/TypeBadge'

/*
 * A function block with an optional expanded view. Presentation stays local;
 * moving still commits one undo step, and settings belong to the inspector.
 */

interface Props {
  readonly block: CanvasBlock
  readonly type?: VmBlockType
  readonly shape: BlockShape
  readonly selected: boolean
  readonly errors: number
  readonly zoom: number
  readonly snap: boolean
  readonly detailed: boolean
  readonly devices?: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
  readonly onSelect: () => void
  readonly onMove: (to: Point) => void
  /** A press on an output, ENO or Body starts a wire from it. */
  readonly onWireStart?: (source: WireSource, event: React.PointerEvent) => void
  /** The chip a pin carries for a variable (short and full name), or none (a wire, or a chip pulled onto the canvas). */
  readonly chipOf?: (path: ObjectPath, output: boolean) => { readonly label: string; readonly full: string } | undefined
  /** A press on a docked chip: a click opens the block, a drag pulls the chip off. */
  readonly onChipPress?: (target: WireTarget, path: ObjectPath, event: React.PointerEvent) => void
  /** The docked chip selected on this block (Delete takes it off). */
  readonly selectedChip?: WireTarget
  /** A path as the user reads it (names, not IDs). */
  readonly labelOf?: (path: ObjectPath) => string
  /** The block's name changed (double-click on its title); empty = no name. */
  readonly onRename?: (name: string | undefined) => void
}

export function CanvasBlockView({ block, type, shape, selected, errors, zoom, snap, detailed, devices, deviceCatalog, onSelect, onMove, onWireStart, chipOf, onChipPress, selectedChip, labelOf = pathText, onRename }: Props) {
  const [drag, setDrag] = useState<{ start: Point; origin: Point; at: Point }>()
  const [renaming, setRenaming] = useState(false)
  const typeTitle = type?.title ?? `Unknown ${block.type}`
  const commitName = (text: string) => {
    setRenaming(false)
    const name = text.trim()
    if (name !== (block.name ?? '')) onRename?.(name || undefined)
  }
  const expanded = block.view ? block.view === 'detailed' : detailed
  const summary = blockSummary(type, block, devices, deviceCatalog)
  const enables = block.enables?.length ?? 0
  const at = drag?.at ?? block
  const classes = ['canvas-block', `cat-${type?.category ?? 'unknown'}`, selected && 'selected', errors > 0 && 'has-errors', drag && 'is-dragging', expanded && 'is-expanded'].filter(Boolean).join(' ')

  const wireFrom = (pin: string) => (event: React.PointerEvent) => {
    if (event.button !== 0 || !onWireStart) return
    event.stopPropagation()
    onWireStart({ block: block.id, pin }, event)
  }

  const pinView = (pin: BlockPinView, index: number, output: boolean) => {
    const pinTitle = output ? block.outputAliases?.[index] || pin.title : pin.title
    const source = output ? block.outputs?.[index] : block.inputs?.[index]
    const text = source ? labelOf(typeof source === 'string' ? { root: source } : source) : output ? `${block.id}:q${index}` : 'Unwired'
    const connected = !!source
    const path: ObjectPath | undefined = source ? (typeof source === 'string' ? { root: source } : source) : undefined
    const chip = path && chipOf?.(path, output)
    const target: WireTarget = output ? { block: block.id, kind: 'out', index } : { block: block.id, kind: 'in', index: pin.index }
    return <div key={index} className={`canvas-block-pin ${pin.required ? 'is-required' : ''} ${connected ? 'is-connected' : ''} ${pin.value === 'gate' || pin.value === 'bool' ? 'is-gate' : ''}`} title={`${pinTitle} (${pin.value})${pin.required ? ', required' : ''} · ${text}`}>
      {chip && (
        <span
          className={`canvas-chip is-docked ${output ? 'is-out' : 'is-in'} ${path!.root === '' ? 'is-empty' : ''} ${selectedChip && selectedChip.kind === target.kind && 'index' in selectedChip && 'index' in target && selectedChip.index === target.index ? 'is-selected' : ''}`}
          role="button"
          tabIndex={-1}
          title={path!.root === '' ? 'Empty variable: choose it in the block details' : `${chip.full}: click to select (Delete removes it), drag onto the canvas to pull it off`}
          onPointerDown={(event) => {
            if (event.button !== 0 || !onChipPress) return
            event.stopPropagation()
            onChipPress(target, path!, event)
          }}
        ><span className="canvas-chip-text">{path!.root === '' ? '?' : chip.label}</span></span>
      )}
      {!output && <span className="canvas-block-input-type"><ValueKindBadge type={pin.value} /></span>}
      <span className="canvas-block-pin-label"><span>{pinTitle}</span>{expanded && <small className={!source && !output ? 'is-unwired' : ''}>{text}</small>}</span>
      {output && <span aria-hidden="true" className="canvas-block-output-type canvas-wire-start" title={`Drag to wire ${pin.title}`} onPointerDown={wireFrom(`q${index}`)}><ValueKindBadge type={pin.value} /></span>}
    </div>
  }

  return (
    <div
      className={classes}
      role="group"
      tabIndex={0}
      aria-current={selected ? 'true' : undefined}
      aria-label={`${type?.title ?? block.type} block ${block.id}`}
      style={{ left: at.x, top: at.y, width: shape.width, height: shape.height }}
      onPointerDown={(event) => {
        if (event.button !== 0 || (event.target as HTMLElement).closest('button')) return
        event.stopPropagation()
        event.currentTarget.setPointerCapture(event.pointerId)
        onSelect()
        setDrag({ start: { x: event.clientX, y: event.clientY }, origin: { x: block.x, y: block.y }, at: { x: block.x, y: block.y } })
      }}
      onPointerMove={(event) => {
        if (!drag) return
        const raw = { x: drag.origin.x + (event.clientX - drag.start.x) / zoom, y: drag.origin.y + (event.clientY - drag.start.y) / zoom }
        setDrag({ ...drag, at: snap ? snapPoint(raw) : raw })
      }}
      onPointerUp={() => {
        if (drag && (drag.at.x !== drag.origin.x || drag.at.y !== drag.origin.y)) onMove(drag.at)
        setDrag(undefined)
      }}
      onPointerCancel={() => setDrag(undefined)}
      onDoubleClick={(event) => {
        // The block holds the pointer while pressed, so the double-click lands here: only the header renames.
        const rect = event.currentTarget.getBoundingClientRect()
        if (onRename && event.clientY - rect.top < 40 * zoom) setRenaming(true)
      }}
      onKeyDown={(event) => { if (event.target === event.currentTarget && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); onSelect() } }}
    >
      {(block.enables ?? []).map((path, index) => {
        const chip = chipOf?.(path, false)
        if (!chip) return null
        const target: WireTarget = { block: block.id, kind: 'en', index }
        const picked = selectedChip?.kind === 'en' && selectedChip.index === index
        return (
          <span
            key={`en${index}`}
            className={`canvas-chip is-docked is-in is-en ${path.root === '' ? 'is-empty' : ''} ${picked ? 'is-selected' : ''}`}
            style={{ top: 20 + index * 22 }}
            role="button"
            tabIndex={-1}
            title={`${chip.full} on EN: the block runs while it is true. Click to select (Delete removes it), drag to move it`}
            onPointerDown={(event) => {
              if (event.button !== 0 || !onChipPress) return
              event.stopPropagation()
              onChipPress(target, path, event)
            }}
          ><span className="canvas-chip-text">{path.root === '' ? '?' : chip.label}</span></span>
        )
      })}
      <span className="canvas-block-enable is-en" role="img" aria-label={`EN connector ${block.id}`} data-pin="en" title={enables ? `EN: ${block.enableMode === 'all' ? 'all' : 'any'} of ${enables} sources\n${block.enables!.map(pathText).join('\n')}` : 'EN: no sources, always enabled'} />
      <span className="canvas-block-enable is-eno canvas-wire-start" role="img" aria-label={`ENO connector ${block.id}`} data-pin="eno" title={type?.key === 'FOR' ? 'ENO of a loop: drag onto a block\'s EN to put it (and what depends on it) in the loop' : 'ENO: true while the block acted — drag onto another block\'s EN to run it only then'} onPointerDown={wireFrom('eno')} />
      <div className="canvas-block-header">
        <span className="canvas-block-heading">
          {renaming ? (
            <input
              className="canvas-block-rename"
              autoFocus
              defaultValue={block.name ?? ''}
              placeholder={typeTitle}
              aria-label={`Name of ${block.id}`}
              onPointerDown={(event) => event.stopPropagation()}
              onKeyDown={(event) => {
                if (event.key === 'Enter') commitName(event.currentTarget.value)
                else if (event.key === 'Escape') setRenaming(false)
                event.stopPropagation()
              }}
              onBlur={(event) => commitName(event.currentTarget.value)}
            />
          ) : (
            <span className="canvas-block-title" title={onRename ? 'Double-click the header to name it' : undefined}>{block.name || typeTitle}</span>
          )}
          {(block.name || expanded) && <span className="canvas-block-id" title={block.id}>{[block.name ? typeTitle : '', expanded ? block.id : ''].filter(Boolean).join(' · ')}</span>}
        </span>
        {errors > 0 && <span className="canvas-block-errors" title={`${errors} problem(s): see the block's details`}>{errors}</span>}
      </div>
      <div className="canvas-block-pins">
        <div className="canvas-block-side is-in">
          {shape.inputs.map((pin) => pinView(pin, pin.index, false))}
        </div>
        <div className="canvas-block-side is-out">
          {shape.outputs.map((pin, index) => pinView(pin, index, true))}
        </div>
      </div>
      {expanded && <div className={`canvas-block-summary ${type?.encoding ? 'is-formula' : ''}`} title={summary.join('\n')}>{summary.length ? summary.map((line, index) => <div key={index}>{line}</div>) : <div className="is-unwired">No settings</div>}</div>}
    </div>
  )
}
