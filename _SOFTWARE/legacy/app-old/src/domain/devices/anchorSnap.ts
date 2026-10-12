/*
 * Snapping of an anchor while it is placed or dragged on a picture (positions are fractions 0..1 of the picture).
 *   - `pins`: line up with the other placed pins. A pin joins the row (same y) or column (same x) of a pin within the
 *     threshold, and along a row or column that already holds two or more pins it takes the same pitch, so a header
 *     of pads is placed with even spacing.
 *   - a number: a grid of that many millimetres from the picture's top-left corner (one cell is one millimetre).
 *   - `off`: free.
 */

export type SnapMode = 'off' | 'pins' | number

export interface Point { readonly x: number; readonly y: number }

export interface SnapOptions {
  readonly mode: SnapMode
  /** Reach of a snap, as fractions of the picture's width and height. */
  readonly thresholdX: number
  readonly thresholdY: number
  /** Picture size in millimetres (grid cells). */
  readonly sizeCells: readonly [number, number]
}

export interface Snapped extends Point {
  /** The x / y of the row or column the pin was lined up with, for a guide line. */
  readonly guideX?: number
  readonly guideY?: number
}

/** Placed pins closer than this fraction are the same spot, not neighbours (a pitch is never this small). */
const SAME_LINE = 0.003

const nearest = (value: number, candidates: readonly number[], threshold: number): number | undefined => {
  let best: number | undefined
  for (const candidate of candidates) {
    if (Math.abs(candidate - value) <= threshold && (best === undefined || Math.abs(candidate - value) < Math.abs(best - value))) best = candidate
  }
  return best
}

/** The smallest gap between neighbouring values (the pitch of a row), or undefined with fewer than two. */
const pitchOf = (values: readonly number[]): number | undefined => {
  const sorted = [...values].sort((a, b) => a - b)
  let gap: number | undefined
  for (let i = 1; i < sorted.length; i++) {
    const step = sorted[i] - sorted[i - 1]
    if (step > SAME_LINE && (gap === undefined || step < gap)) gap = step
  }
  return gap
}

/** Where a value falls one pitch beyond (or between) the values of a row: the places the next pin would sit. */
const pitchSlots = (values: readonly number[], pitch: number): number[] => {
  const slots: number[] = []
  for (const value of values) slots.push(value - pitch, value + pitch)
  return slots.filter((slot) => !values.some((value) => Math.abs(value - slot) < SAME_LINE))
}

export function snapAnchor(point: Point, others: readonly Point[], options: SnapOptions): Snapped {
  const { mode, thresholdX, thresholdY, sizeCells } = options
  if (mode === 'off') return point
  if (typeof mode === 'number') {
    const snap = (value: number, cells: number): number => Math.min(1, Math.max(0, (Math.round((value * cells) / mode) * mode) / cells))
    return { x: snap(point.x, sizeCells[0]), y: snap(point.y, sizeCells[1]) }
  }

  let x = point.x
  let y = point.y
  let guideX: number | undefined
  let guideY: number | undefined

  // A row: the same y as a pin within reach, then the pitch of the pins already on that row.
  const row = nearest(y, others.map((other) => other.y), thresholdY)
  if (row !== undefined) {
    y = row
    guideY = row
    const onRow = others.filter((other) => Math.abs(other.y - row) <= thresholdY).map((other) => other.x)
    const pitch = pitchOf(onRow)
    const slot = pitch === undefined ? undefined : nearest(x, pitchSlots(onRow, pitch), thresholdX)
    if (slot !== undefined) x = slot
  }
  // A column: the same x as a pin within reach, and the pitch down it.
  const column = nearest(point.x, others.map((other) => other.x), thresholdX)
  if (column !== undefined && (row === undefined || Math.abs(column - point.x) <= Math.abs(x - point.x) + SAME_LINE)) {
    x = column
    guideX = column
    const onColumn = others.filter((other) => Math.abs(other.x - column) <= thresholdX).map((other) => other.y)
    const pitch = pitchOf(onColumn)
    const slot = pitch === undefined || row !== undefined ? undefined : nearest(point.y, pitchSlots(onColumn, pitch), thresholdY)
    if (slot !== undefined) y = slot
  }
  return { x, y, ...(guideX !== undefined ? { guideX } : {}), ...(guideY !== undefined ? { guideY } : {}) }
}
