import { canvasToScreen } from '../domain/canvas'
import type { CanvasBlock, Viewport, WireTarget } from '../domain/canvas'
import { MenuOption, OptionMenu } from '../components/OptionMenu'
import { TypeBadge } from '../components/TypeBadge/TypeBadge'
import type { BlockShape } from '../blocks/blockModel'

/** One thing a block can send on: an output or its ENO. */
export interface SourceChoice {
  readonly key: string
  readonly label: string
  readonly detail: string
  readonly type?: string
}

/** One place a link can end: an input pin, or the block's EN strip. */
export interface PickerRow {
  readonly key: string
  readonly label: string
  readonly detail: string
  readonly type?: string
  readonly target: WireTarget
  /** Whether what is sent fits here. */
  readonly ok: boolean
}

const windowSpot = (block: CanvasBlock, shape: BlockShape, viewport: Viewport) => ({
  at: canvasToScreen(viewport, { x: block.x, y: block.y }),
  width: Math.max(220, shape.width * viewport.zoom),
})

/**
 * Two windows for linking a selected block to a touched one: one over the source (what it sends), one over the target (the pins
 * that take it). Choosing on the source changes the target's list; choosing a target pin makes the link.
 */
export function LinkWindows({ from, fromShape, to, toShape, sources, chosen, rows, viewport, onChoose, onPick, onSwap, onSelect, onCancel }: {
  from: CanvasBlock
  fromShape: BlockShape
  to: CanvasBlock
  toShape: BlockShape
  sources: readonly SourceChoice[]
  chosen: number
  rows: readonly PickerRow[]
  viewport: Viewport
  onChoose: (index: number) => void
  onPick: (row: PickerRow) => void
  onSwap: () => void
  /** Just select the touched block instead of linking. */
  onSelect: () => void
  onCancel: () => void
}) {
  const source = windowSpot(from, fromShape, viewport)
  const target = windowSpot(to, toShape, viewport)
  // One thing to send: nothing to choose, so only the target shows (and says where it comes from).
  const choosing = sources.length > 1
  const swap = <button type="button" className="wire-picker-swap" onClick={onSwap} title="Link the other way round">⇄</button>
  const row = (entry: PickerRow) => (
    <MenuOption key={entry.key} disabled={!entry.ok} className={`wire-picker-row ${entry.target.kind === 'en' ? 'is-en' : ''}`} onChoose={() => onPick(entry)}>
      <span className="wire-picker-label">{entry.label}</span>
      <span className="wire-picker-detail">
        {entry.type && <TypeBadge type={entry.type} />}
        <span className="wire-picker-text">{entry.detail}</span>
      </span>
    </MenuOption>
  )
  // A big arrow in the gap between the two windows: from what is sent to where it goes.
  const rightwards = target.at.x >= source.at.x
  const startX = rightwards ? source.at.x + source.width : source.at.x
  const endX = rightwards ? target.at.x : target.at.x + target.width
  const gap = Math.abs(endX - startX)
  // As tall as the taller window: a header and a row or two each, about 48px per row.
  const height = 34 + 48 * Math.max(sources.length, rows.length, 1)
  const top = Math.min(source.at.y, target.at.y)
  const mid = height / 2
  // A soft ribbon like the wires on the canvas: it narrows from the source into a rounded head at the target.
  const head = Math.min(gap * 0.6, 40)
  const half = height * 0.2
  const lean = (x: number) => (rightwards ? x : gap - x)
  const arrow = choosing && gap > 12 && (
    <svg className="link-arrow" aria-hidden="true" style={{ left: Math.min(startX, endX), top, width: gap, height }} viewBox={`0 0 ${gap} ${height}`}>
      <path d={`M${lean(0)} ${mid - half} C${lean(gap - head)} ${mid - half} ${lean(gap - head)} ${mid - half} ${lean(gap - head)} ${mid - half} V${height * 0.04} Q${lean(gap - head)} ${height * 0.02} ${lean(gap - head + 8)} ${height * 0.1} L${lean(gap - 4)} ${mid - 6} Q${lean(gap)} ${mid} ${lean(gap - 4)} ${mid + 6} L${lean(gap - head + 8)} ${height * 0.9} Q${lean(gap - head)} ${height * 0.98} ${lean(gap - head)} ${height * 0.96} V${mid + half} H${lean(0)} Z`} />
    </svg>
  )
  return (
    <>
      {arrow}
      {choosing && (
        <OptionMenu className="wire-picker is-source" aria-label={`Send from ${from.id}`} style={{ left: source.at.x, top: source.at.y, width: source.width }} onCancel={onCancel}>
          <div className="wire-picker-head"><span>From {from.name || from.id}</span>{swap}</div>
          {sources.map((entry, index) => (
            <MenuOption key={entry.key} selected={index === chosen} className={`wire-picker-row is-source-row ${index === chosen ? 'is-chosen' : ''}`} onChoose={() => onChoose(index)}>
              <span className="wire-picker-label">{entry.label}</span>
              <span className="wire-picker-detail">
                {entry.type && <TypeBadge type={entry.type} />}
                <span className="wire-picker-text">{entry.detail}</span>
              </span>
            </MenuOption>
          ))}
        </OptionMenu>
      )}
      <OptionMenu className="wire-picker is-target" aria-label={`Connect to ${to.id}`} style={{ left: target.at.x, top: target.at.y, width: target.width }} onCancel={onCancel}>
        <div className="wire-picker-head">
          <span>{choosing ? `Into ${to.name || to.id}` : `${sources[0]?.label ?? 'Link'} of ${from.name || from.id} → ${to.name || to.id}`}</span>
          {!choosing && swap}
          <button type="button" className="wire-picker-select" onClick={onSelect} title="Just select this block">Select</button>
        </div>
        {rows.map(row)}
        {!rows.length && <div className="wire-picker-empty">Nothing here fits</div>}
      </OptionMenu>
    </>
  )
}
