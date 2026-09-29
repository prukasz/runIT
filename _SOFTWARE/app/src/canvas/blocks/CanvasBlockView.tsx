import { useMemo, useState } from 'react'
import { InlineRename } from '../../components/InlineRename'
import { snapPoint } from '../../domain/canvas'
import type { Point, WireSource, WireTarget } from '../../domain/canvas'
import type { DeviceCatalog, VmBlockType } from '../../domain/descriptors'
import { decompileExpression, expressionLanguage, tokenize } from '../../domain/expression'
import type { CanvasBlock, ObjectPath, ProjectDevice } from '../../domain/project'
import { blockHeadline, blockSummary, pathText } from './blockView'
import type { BlockPinView, BlockShape } from './blockView'
import { ValueKindBadge } from '../../components/TypeBadge/TypeBadge'

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
  readonly chipOf?: (path: ObjectPath, output: boolean, target?: WireTarget) => { readonly label: string; readonly full: string } | undefined
  /** A press on a docked chip: a click opens the block, a drag pulls the chip off. */
  readonly onChipPress?: (target: WireTarget, path: ObjectPath, event: React.PointerEvent) => void
  /** The docked chip selected on this block (Delete takes it off). */
  readonly selectedChip?: WireTarget
  /** A path as the user reads it (names, not IDs). */
  readonly labelOf?: (path: ObjectPath) => string
  /** The block's name changed (double-click on its title); empty = no name. */
  readonly onRename?: (name: string | undefined) => void
  /** Runtime execution state: whether ENO is true this cycle (right strip green). */
  readonly eno?: boolean
  /** Runtime execution state: whether the block failed (whole block red tint). */
  readonly failed?: boolean
  /** Whether a variable is selected for linking (clicking block opens pin picker without dragging). */
  readonly isLinking?: boolean
}

export function CanvasBlockView({ block, type, shape, selected, errors, zoom, snap, detailed, devices, deviceCatalog, onSelect, onMove, onWireStart, chipOf, onChipPress, selectedChip, labelOf = pathText, onRename, eno, failed, isLinking }: Props) {
  const [drag, setDrag] = useState<{ start: Point; origin: Point; at: Point }>()
  const [renaming, setRenaming] = useState(false)
  const typeTitle = type?.title ?? `Unknown ${block.type}`
  const headline = blockHeadline(type, block) ?? typeTitle
  const commitName = (text: string) => {
    setRenaming(false)
    const name = text.trim()
    if (name !== (block.name ?? '')) onRename?.(name || undefined)
  }
  const expanded = block.view ? block.view === 'detailed' : detailed
  const summary = blockSummary(type, block, devices, deviceCatalog)
  const enables = block.enables?.length ?? 0
  const at = drag?.at ?? block

  const formulaData = useMemo(() => {
    if (!type?.encoding) return undefined
    const language = expressionLanguage(type.encoding)
    if (!block.expression?.code.length) {
      return { text: 'Set a formula', elements: <span className="is-unwired">Set a formula</span> }
    }
    const raw = decompileExpression(block.expression.code, block.expression.constants ?? [], language)
    if (!raw) {
      return { text: 'Invalid formula', elements: <span className="is-unwired">Invalid formula</span> }
    }

    const outSource = block.outputs?.[0]
    const outAlias = block.outputAliases?.[0]
    const outPath: ObjectPath | undefined = outSource ? (typeof outSource === 'string' ? { root: outSource } : outSource) : undefined
    const outChip = outPath && chipOf?.(outPath, true)
    const outLabel = outChip?.label ?? (outPath ? labelOf(outPath) : outAlias || shape.outputs[0]?.title || 'OUT')

    const { tokens } = tokenize(raw, language.flavor)
    const elements: React.ReactNode[] = []
    let fullText = `${outLabel} = `
    let atIdx = 0

    elements.push(
      <span key="out" className="expr-target is-output" title={outPath ? labelOf(outPath) : undefined}>
        {outLabel}
      </span>,
      <span key="eq" className="canvas-formula-op">=</span>,
    )

    for (const token of tokens) {
      if (token.start > atIdx) {
        const gap = raw.slice(atIdx, token.start)
        elements.push(gap)
        fullText += gap
      }
      if (token.kind === 'input') {
        const path = block.inputs?.[token.value!]
        const inPath: ObjectPath | undefined = path ? (typeof path === 'string' ? { root: path } : path) : undefined
        const inChip = inPath && chipOf?.(inPath, false, { block: block.id, kind: 'in', index: token.value! })
        const inLabel = inChip?.label ?? (inPath ? labelOf(inPath) : `IN${token.value}`)
        const hasTarget = Boolean(inPath && (typeof inPath === 'string' ? inPath !== '' : inPath.root !== ''))
        elements.push(
          <span
            key={`tok-${token.start}`}
            className={`expr-target ${hasTarget ? '' : 'is-unwired'}`}
            title={`IN${token.value}${hasTarget ? `: ${inLabel}` : ': not wired'}`}
          >
            {inLabel}
          </span>,
        )
        fullText += inLabel
      } else {
        elements.push(
          <span key={`tok-${token.start}`} className="canvas-formula-tok">
            {token.text}
          </span>,
        )
        fullText += token.text
      }
      atIdx = token.end
    }
    if (atIdx < raw.length) {
      const remainder = raw.slice(atIdx)
      elements.push(remainder)
      fullText += remainder
    }

    return {
      text: fullText,
      elements: <div className="canvas-formula-line">{elements}</div>,
    }
  }, [type, block, shape.outputs, labelOf, chipOf])
  const isFailed = failed || errors > 0
  const classes = [
    'canvas-block',
    `cat-${type?.category ?? 'unknown'}`,
    selected && 'selected',
    isFailed && 'has-errors is-failed',
    eno === true && 'has-eno',
    eno === false && 'eno-false',
    drag && 'is-dragging',
    expanded && 'is-expanded',
    isLinking && 'is-linking',
  ].filter(Boolean).join(' ')

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
    const target: WireTarget = output ? { block: block.id, kind: 'out', index } : { block: block.id, kind: 'in', index: pin.index }
    const chip = path && chipOf?.(path, output, target)
    return <div key={index} className={`canvas-block-pin ${chip ? 'has-docked-chip' : ''} ${pin.required ? 'is-required' : ''} ${connected ? 'is-connected' : ''} ${pin.value === 'gate' || pin.value === 'bool' ? 'is-gate' : ''}`} title={`${pinTitle} (${pin.value})${pin.required ? ', required' : ''} · ${text}`}>
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
      {!output ? (
        <div className="canvas-pin-slab is-in">
          <span className="canvas-block-input-type"><ValueKindBadge type={pin.value} /></span>
          <span className="canvas-block-pin-label">
            <span className="canvas-block-pin-name">{pinTitle}</span>
          </span>
        </div>
      ) : (
        <div className="canvas-pin-slab is-out">
          <span className="canvas-block-pin-label">
            <span className="canvas-block-pin-name">{pinTitle}</span>
          </span>
          <span aria-hidden="true" className="canvas-block-output-type canvas-wire-start" title={`Drag to wire ${pin.title}`} onPointerDown={wireFrom(`q${index}`)}>
            <ValueKindBadge type={pin.value} />
          </span>
        </div>
      )}
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
        if (isLinking) {
          onSelect()
          return
        }
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
        const chip = chipOf?.(path, false, { block: block.id, kind: 'en', index })
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
            title={`${chip.full}: the block runs only while this is true. Click to select (Delete removes it), drag to move it`}
            onPointerDown={(event) => {
              if (event.button !== 0 || !onChipPress) return
              event.stopPropagation()
              onChipPress(target, path, event)
            }}
          ><span className="canvas-chip-text">{path.root === '' ? '?' : chip.label}</span></span>
        )
      })}
      <span className="canvas-block-enable is-en" role="img" aria-label={`Run when connector ${block.id}`} data-pin="en" title={enables ? `Run when: ${block.enableMode === 'all' ? 'all' : 'any'} of ${enables} sources\n${block.enables!.map(pathText).join('\n')}` : 'Run when: nothing set, always runs'} />
      <span className={`canvas-block-enable is-eno canvas-wire-start ${eno === true ? 'is-active' : eno === false ? 'is-false' : ''}`} role="img" aria-label={`When done connector ${block.id}`} data-pin="eno" title={type?.key === 'FOR' ? 'Loop body: drag onto a block\'s Run when to put it (and what depends on it) in the loop' : 'When done: true while the block acted. Drag onto another block\'s Run when to run it only then'} onPointerDown={wireFrom('eno')} />
      <div className="canvas-block-header">
        <span className="canvas-block-heading">
          {renaming ? (
            <InlineRename
              className="canvas-block-rename"
              value={block.name ?? ''}
              placeholder={typeTitle}
              label={`Name of ${block.id}`}
              onCommit={commitName}
              onCancel={() => setRenaming(false)}
            />
          ) : (
            <span className="canvas-block-title" title={onRename ? 'Double-click the header to name it' : undefined}>{block.name || headline}</span>
          )}
          {(block.name || expanded) && <span className="canvas-block-id" title={block.id}>{[block.name ? headline : '', expanded ? block.id : ''].filter(Boolean).join(' · ')}</span>}
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
      {expanded && (
        <div className={`canvas-block-summary ${type?.encoding ? 'is-formula' : ''}`} title={formulaData?.text ?? summary.join('\n')}>
          {formulaData ? (
            formulaData.elements
          ) : summary.length ? (
            summary.map((line, index) => <div key={index}>{line}</div>)
          ) : (
            <div className="is-unwired">No settings</div>
          )}
        </div>
      )}
    </div>
  )
}
