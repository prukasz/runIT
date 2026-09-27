import { useState } from 'react'
import { snapPoint } from '../domain/canvas'
import type { Point } from '../domain/canvas'
import type { DeviceCatalog, VmBlockType } from '../domain/descriptors'
import type { CanvasBlock, ProjectDevice } from '../domain/project'
import { blockSummary, pathText } from './blockView'
import type { BlockPinView, BlockShape } from './blockView'

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
}

export function CanvasBlockView({ block, type, shape, selected, errors, zoom, snap, detailed, devices, deviceCatalog, onSelect, onMove }: Props) {
  const [drag, setDrag] = useState<{ start: Point; origin: Point; at: Point }>()
  const expanded = block.view ? block.view === 'detailed' : detailed
  const summary = blockSummary(type, block, devices, deviceCatalog)
  const enables = block.enables?.length ?? 0
  const at = drag?.at ?? block
  const classes = ['canvas-block', `cat-${type?.category ?? 'unknown'}`, selected && 'selected', errors > 0 && 'has-errors', drag && 'is-dragging', expanded && 'is-expanded'].filter(Boolean).join(' ')

  const pinView = (pin: BlockPinView, index: number, output: boolean) => {
    const source = output ? block.outputs?.[index] : block.inputs?.[index]
    const text = source ? (typeof source === 'string' ? source : pathText(source)) : output ? `${block.id}:q${index}` : 'Unwired'
    const connected = !!source
    return <div key={index} className={`canvas-block-pin ${pin.required ? 'is-required' : ''} ${connected ? 'is-connected' : ''} ${pin.value === 'gate' || pin.value === 'bool' ? 'is-gate' : ''}`} title={`${pin.title} (${pin.value})${pin.required ? ', required' : ''} · ${text}`}>
      {!output && <i aria-hidden="true" />}
      <span className="canvas-block-pin-label"><span>{pin.title}</span>{expanded && <small className={!source && !output ? 'is-unwired' : ''}>{text}</small>}</span>
      {output && <i aria-hidden="true" />}
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
      onKeyDown={(event) => { if (event.target === event.currentTarget && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); onSelect() } }}
    >
      <span className="canvas-block-enable is-en" role="img" aria-label={`EN connector ${block.id}`} data-pin="en" title={enables ? `EN: ${block.enableMode === 'all' ? 'all' : 'any'} of ${enables} sources\n${block.enables!.map(pathText).join('\n')}` : 'EN: no sources, always enabled'} />
      <span className="canvas-block-enable is-eno" role="img" aria-label={`ENO connector ${block.id}`} data-pin="eno" title={`ENO: ${block.id}:eno — true while the block acted`} />
      <div className="canvas-block-header">
        <span className="canvas-block-heading"><span className="canvas-block-title">{type?.title ?? `Unknown ${block.type}`}</span>{expanded && <span className="canvas-block-id" title={block.id}>{block.id}</span>}</span>
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
