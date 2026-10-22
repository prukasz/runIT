import type { BlockInstance, ConcretePin, ObjectPath, ValueKind } from '@engine'
import { deviceLine, formulaParts, formulaText, headline, headlineText, pinsOf, sourceLabel } from '../blocks'
import type { BlockContext } from '../blocks'

/*
 * The canvas, pure: where blocks and free chips sit (kept in the project file, never on the board), the grid, the viewport
 * (screen = canvas × zoom + offset), the size of a block (the least that holds its header, its pins and its formula, on the
 * grid), where a wire meets a pin, and the curves of the wires.
 */

/** Canvas units per grid cell (px at 100 % zoom). */
export const GRID = 20
export const ZOOM_MIN = 0.25
export const ZOOM_MAX = 2

export interface Point {
  readonly x: number
  readonly y: number
}

export interface Viewport {
  /** Screen position of the canvas origin, px. */
  readonly x: number
  readonly y: number
  readonly zoom: number
}

export const DEFAULT_VIEWPORT: Viewport = { x: 0, y: 0, zoom: 1 }

/**
 * A variable pulled off its pin onto the canvas: a label with a place. It holds no wires: every pin that reads (or, for an output,
 * writes) its path is drawn wired to it, and the path stays on the pin, so the program does not change.
 */
export interface CanvasChip {
  readonly id: string
  readonly path: ObjectPath
  readonly x: number
  readonly y: number
}

/** Where things sit on the canvas: each block's top-left corner by its id, and the free chips. */
export interface CanvasLayout {
  readonly blocks: Readonly<Record<string, Point>>
  readonly chips: readonly CanvasChip[]
}

export const EMPTY_LAYOUT: CanvasLayout = { blocks: {}, chips: [] }

/** The nearest grid point. */
export const snap = (value: number, grid = GRID): number => Math.round(value / grid) * grid
export const snapPoint = (point: Point, grid = GRID): Point => ({ x: snap(point.x, grid), y: snap(point.y, grid) })

export const screenToCanvas = (viewport: Viewport, point: Point): Point => ({ x: (point.x - viewport.x) / viewport.zoom, y: (point.y - viewport.y) / viewport.zoom })
export const canvasToScreen = (viewport: Viewport, point: Point): Point => ({ x: point.x * viewport.zoom + viewport.x, y: point.y * viewport.zoom + viewport.y })

/** Zoom by `factor`, keeping the canvas point under `anchor` (screen px) where it is. */
export const zoomAt = (viewport: Viewport, factor: number, anchor: Point): Viewport => {
  const zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, viewport.zoom * factor))
  const under = screenToCanvas(viewport, anchor)
  return { zoom, x: anchor.x - under.x * zoom, y: anchor.y - under.y * zoom }
}

/* ---- a block's size ---- */

const MIN_WIDTH = 6 * GRID
export const HEADER = 2 * GRID
const ROW = 25
/** Height of the formula line under an expression block's pins. */
export const FORMULA_ROW = 30

export interface PinView {
  /** Position among the block's inputs (or outputs). */
  readonly index: number
  readonly title: string
  readonly value: ValueKind
  readonly required: boolean
  /** The name is not drawn: a pin of a group (an expression's inputs, a switch's cases) that is wired, its chip or wire names it. */
  readonly quiet?: boolean
}

export interface BlockShape {
  readonly width: number
  readonly height: number
  readonly inputs: readonly PinView[]
  readonly outputs: readonly PinView[]
  /** Pin row height. */
  readonly row: number
  /** An expression block: the formula line under its pins. */
  readonly formula: boolean
}

/** Width of a text in a font; `perChar` is the estimate where nothing can measure (tests). */
export type Measure = (text: string, font: string, perChar: number) => number

let measuring: CanvasRenderingContext2D | null | undefined
const family = (): string => (typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily)

/** Measures in the browser (a 2D context); an estimate where there is none. */
export const measureText: Measure = (text, font, perChar) => {
  if (measuring === undefined) {
    try {
      measuring = typeof document === 'undefined' ? null : document.createElement('canvas').getContext('2d')
    } catch {
      measuring = null
    }
  }
  if (!measuring) return text.length * perChar
  // A font that names its own family (the formula's monospace) keeps it; the rest is in the app's family.
  measuring.font = font.includes(',') ? font : `${font} ${family()}`
  return measuring.measureText(text).width
}

/** Width the pin rows need: per row the input's slab (type square, name, padding) beside the output's, and a gap between them. */
const pinsWidth = (inputs: readonly PinView[], outputs: readonly PinView[], measure: Measure): number => {
  const slab = (pin: PinView | undefined): number => (pin ? 40 + (pin.quiet ? 0 : 7 + measure(pin.title, '600 11.5px', 6.8)) + 2 : 0)
  let widest = 0
  // Each side may take half the block plus 6 px (the CSS limit), so a long name on one side widens the block for both.
  for (let row = 0; row < Math.max(inputs.length, outputs.length); row++) {
    widest = Math.max(widest, slab(inputs[row]) + slab(outputs[row]), inputs.length && outputs.length ? 2 * Math.max(slab(inputs[row]), slab(outputs[row])) - 12 : 0)
  }
  return widest + (inputs.length && outputs.length ? 12 : 0)
}

/** The block's pins as the canvas draws them. */
export const pinViews = (ctx: BlockContext, block: BlockInstance): { readonly inputs: PinView[]; readonly outputs: PinView[] } => {
  const pins = pinsOf(ctx.catalog, block)
  const view = (wired: (index: number) => boolean) => (p: ConcretePin): PinView => ({
    index: p.index,
    title: p.pin.alias,
    value: p.pin.value,
    required: p.pin.required,
    ...(p.group && (p.group.spec.label === 'hidden' || wired(p.index)) ? { quiet: true } : {}),
  })
  return {
    inputs: (pins?.inputs ?? []).map(view((i) => !!block.inputs[i])),
    // An output is named by the variable docked on it; a wire to another block names nothing, so the pin keeps its own name.
    outputs: (pins?.outputs ?? []).map(view((i) => !!block.outputs[i])),
  }
}

/** The widest a formula makes its block: a longer one wraps onto more lines under it. */
const FORMULA_MAX_WIDTH = 2 * MIN_WIDTH
/** One character of the formula (11px monospace), its chips' padding and border, the gaps between its pieces and lines, a line's height and the line's padding (Canvas.css). */
const MONO_CHAR = 6.4
const MONO_FONT = '500 11px ui-monospace, SFMono-Regular, Consolas, monospace'
const CHIP_EXTRA = 14
const FORMULA_GAP = 5
const FORMULA_LINE = 22
const FORMULA_PAD = 20 + 4

/** The pieces of the formula as the canvas draws them, each its width: the output chip, `=`, then each input as a chip and each other token as text. */
const formulaPieces = (ctx: BlockContext, block: BlockInstance, result: string, measure: Measure): number[] => {
  const mono = (text: string) => measure(text, MONO_FONT, MONO_CHAR)
  const parts = formulaParts(ctx, block)
  if (!parts || parts.state !== 'ok') return [mono('Set a formula')]
  const output = block.outputs[0]
  return [
    mono(output ? sourceLabel(ctx, { kind: 'variable', path: output.path }) : result) + CHIP_EXTRA,
    mono('='),
    ...parts.parts.flatMap((part) => {
      if (part.kind === 'in') {
        const source = block.inputs[part.pin]
        return [mono(source ? sourceLabel(ctx, source) : '?') + CHIP_EXTRA]
      }
      const text = part.text.trim()
      return text ? [mono(text)] : []
    }),
  ]
}

/** How many lines the pieces take in `width` (the line's inside), wrapped between pieces as the flex line wraps them. */
const formulaLines = (pieces: readonly number[], width: number): number => {
  let lines = 1
  let used = 0
  for (const piece of pieces) {
    const next = used === 0 ? piece : used + FORMULA_GAP + piece
    if (next > width && used > 0) {
      lines++
      used = piece
    } else used = next
  }
  return lines
}

/** The block's size, the least that holds what it shows, on the grid. Its header follows the names of what it reads, not ids. */
export const blockShape = (ctx: BlockContext, block: BlockInstance, measure: Measure = measureText): BlockShape => {
  const { inputs, outputs } = pinViews(ctx, block)
  const rows = Math.max(1, inputs.length, outputs.length)
  // An expression block shows its formula under the pins (at most three widths of the smallest block; longer is cut), and its type in the header.
  const formula = formulaText(ctx, block)
  const title = block.name?.trim() || (formula !== undefined ? ctx.catalog.get(block.type)?.alias ?? block.type : headlineText(headline(ctx, block, true)))
  const header = measure(title, '600 12px', 6.8) + 24 + 8
  const pieces = formula === undefined ? [] : formulaPieces(ctx, block, outputs[0]?.title ?? 'Result', measure)
  const formulaWidth = formula === undefined ? 0 : Math.min(FORMULA_MAX_WIDTH, pieces.reduce((sum, w) => sum + w, 0) + (pieces.length - 1) * FORMULA_GAP + FORMULA_PAD)
  // A device block names its device on a line under the pins.
  const device = deviceLine(ctx, block)
  const deviceWidth = device === undefined ? 0 : measure(device, '500 11px', 7) + 24
  const width = Math.max(MIN_WIDTH, Math.ceil(Math.max(header, pinsWidth(inputs, outputs, measure), formulaWidth, deviceWidth) / GRID) * GRID)
  const lines = (formula === undefined ? 0 : 1) + (device === undefined ? 0 : 1)
  // A long formula wraps: each line more under the first makes the block taller.
  const wrapped = formula === undefined ? 0 : formulaLines(pieces, width - FORMULA_PAD) - 1
  return { width, height: HEADER + rows * ROW + lines * FORMULA_ROW + wrapped * FORMULA_LINE, inputs, outputs, row: ROW, formula: formula !== undefined }
}

/** Where a wire meets a block, in canvas units: inputs on the left edge, outputs on the right, Run when / When done at header height. */
export const pinAnchor = (at: Point, shape: BlockShape, end: { readonly side: 'in' | 'out'; readonly index: number } | 'en' | 'eno'): Point => {
  if (end === 'en') return { x: at.x, y: at.y + HEADER / 2 }
  if (end === 'eno') return { x: at.x + shape.width, y: at.y + HEADER / 2 }
  const pins = end.side === 'in' ? shape.inputs : shape.outputs
  const row = Math.max(0, pins.findIndex((pin) => pin.index === end.index))
  return { x: end.side === 'in' ? at.x : at.x + shape.width, y: at.y + HEADER + row * shape.row + shape.row / 2 }
}

/* ---- wires ---- */

/**
 * The height a When done band leaves and enters a block at: the middle of the header and the first pin row, the same for every block,
 * so two blocks whose tops are level (they snap to the grid) are joined by a straight band, and the smallest block (one row) is
 * crossed symmetrically. Half way down a block without pins.
 */
export const flowY = (at: Point, shape: BlockShape): number => at.y + Math.min(shape.height, HEADER + ROW) / 2

const ARROW_LENGTH = 22
const ARROW_HALF = 15
const BAND_HALF = 6
// When done: a broad band, so it reads as "runs this block", never as a wire into one of its pins
const WHOLE_HALF = 26
const WHOLE_HEAD_HALF = 46
const WHOLE_HEAD_LENGTH = 40

/** A wire reaches forward by half the gap (at least 40); running back it swings out and under both ends. */
const reach = (from: number, to: number): { readonly dx: number; readonly back: boolean } =>
  to >= from ? { dx: Math.max(40, (to - from) / 2), back: false } : { dx: 80, back: true }

/** A data wire: a curve from an output to an input. */
export const curve = (a: Point, b: Point): string => {
  const { dx, back } = reach(a.x, b.x)
  const low = back ? Math.max(a.y, b.y) + 70 : undefined
  return `M ${a.x} ${a.y} C ${a.x + dx} ${low ?? a.y}, ${b.x - dx} ${low ?? b.y}, ${b.x} ${b.y}`
}

export interface RibbonSpan {
  readonly x: number
  readonly top: number
  readonly bottom: number
}

/**
 * A Run when link: a band, narrow where it leaves its source pin and as tall as the block where it arrives, with an arrow head at `at`;
 * `whole` (a block's When done) is a thick band of even width with a broad head, from the middle of the source block.
 */
export const ribbon = (from: RibbonSpan, to: RibbonSpan, at: number, whole = false): string => {
  const { dx, back } = reach(from.x, to.x)
  const end = to.x - ARROW_LENGTH
  if (whole) {
    // block to block: a thick band of even width from `from`'s middle (flowY), with a broad arrow head
    const y0 = (from.top + from.bottom) / 2
    const headEnd = to.x - WHOLE_HEAD_LENGTH
    const head = `L ${headEnd} ${at - WHOLE_HEAD_HALF} L ${to.x} ${at} L ${headEnd} ${at + WHOLE_HEAD_HALF} L ${headEnd} ${at + WHOLE_HALF}`
    if (!back) return `M ${from.x} ${y0 - WHOLE_HALF} C ${from.x + dx} ${y0 - WHOLE_HALF}, ${headEnd - dx} ${at - WHOLE_HALF}, ${headEnd} ${at - WHOLE_HALF} ${head} C ${headEnd - dx} ${at + WHOLE_HALF}, ${from.x + dx} ${y0 + WHOLE_HALF}, ${from.x} ${y0 + WHOLE_HALF} Z`
    const low = Math.max(y0, at + WHOLE_HEAD_HALF) + 60
    return `M ${from.x} ${y0 - WHOLE_HALF} C ${from.x + dx} ${low}, ${headEnd - dx} ${low}, ${headEnd} ${at - WHOLE_HALF} ${head} C ${headEnd - dx} ${low + 2 * WHOLE_HALF}, ${from.x + dx} ${low + 2 * WHOLE_HALF}, ${from.x} ${y0 + WHOLE_HALF} Z`
  }
  const head = `L ${end} ${at - ARROW_HALF} L ${to.x} ${at} L ${end} ${at + ARROW_HALF} L ${end} ${at + BAND_HALF}`
  if (!back) return `M ${from.x} ${from.top} C ${from.x + dx} ${from.top}, ${end - dx} ${at - BAND_HALF}, ${end} ${at - BAND_HALF} ${head} C ${end - dx} ${at + BAND_HALF}, ${from.x + dx} ${from.bottom}, ${from.x} ${from.bottom} Z`
  const low = Math.max(from.bottom, at + ARROW_HALF) + 40
  return `M ${from.x} ${from.top} C ${from.x + dx} ${low}, ${end - dx} ${low}, ${end} ${at - BAND_HALF} ${head} C ${end - dx} ${low + 6}, ${from.x + dx} ${low + 6}, ${from.x} ${from.bottom} Z`
}

/* ---- the layout ---- */

/** Size of a free chip (px at 100 % zoom): the wires meet its edges. */
export const CHIP_WIDTH = 132
export const CHIP_HEIGHT = 24

const samePath = (a: ObjectPath, b: ObjectPath): boolean => JSON.stringify(a) === JSON.stringify(b)

/** The free chip standing for a path, if there is one. */
export const chipFor = (layout: CanvasLayout, path: ObjectPath): CanvasChip | undefined => layout.chips.find((c) => samePath(c.path, path))

export const newChipId = (layout: CanvasLayout): string => {
  const taken = new Set(layout.chips.map((c) => c.id))
  for (let n = 1; ; n++) if (!taken.has(`chip${n}`)) return `chip${n}`
}

/** Where a block sits; a block the layout does not place yet stands in a column down the left, in project order. */
export const positionOf = (layout: CanvasLayout, blocks: readonly BlockInstance[], id: string): Point => {
  const at = layout.blocks[id]
  if (at) return at
  const unplaced = blocks.filter((b) => !layout.blocks[b.id])
  return { x: 0, y: Math.max(0, unplaced.findIndex((b) => b.id === id)) * 8 * GRID }
}

export interface Rect {
  readonly x: number
  readonly y: number
  readonly width: number
  readonly height: number
}

const overlaps = (a: Rect, b: Rect): boolean => a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height

/** A spot near `wanted` where a block of this size covers none of `taken`: moved down past what it would cover. */
export const freeSpot = (wanted: Point, size: { readonly width: number; readonly height: number }, taken: readonly Rect[]): Point => {
  let spot = snapPoint(wanted)
  for (let tries = 0; tries < 200; tries++) {
    const here = { ...spot, ...size }
    const hit = taken.find((rect) => overlaps(here, rect))
    if (!hit) break
    spot = { x: spot.x, y: snap(hit.y + hit.height + GRID) }
  }
  return spot
}

/** Whether any of `moved` covers any of `taken`. */
export const collides = (moved: readonly Rect[], taken: readonly Rect[]): boolean => moved.some((a) => taken.some((b) => overlaps(a, b)))

/** The free spot nearest to `wanted` for a block of this size: beside, above or below what it would cover, a grid step clear of it. */
export const nearestFreeSpot = (wanted: Point, size: { readonly width: number; readonly height: number }, taken: readonly Rect[]): Point => {
  const start = snapPoint(wanted)
  const free = (p: Point): boolean => !taken.some((rect) => overlaps({ ...p, ...size }, rect))
  if (free(start)) return start
  const xs = [start.x, ...taken.flatMap((r) => [snap(r.x - size.width - GRID), snap(r.x + r.width + GRID)])]
  const ys = [start.y, ...taken.flatMap((r) => [snap(r.y - size.height - GRID), snap(r.y + r.height + GRID)])]
  let best: Point | undefined
  let bestDistance = Infinity
  for (const x of xs) for (const y of ys) {
    const distance = (x - start.x) ** 2 + (y - start.y) ** 2
    if (distance < bestDistance && free({ x, y })) { best = { x, y }; bestDistance = distance }
  }
  return best ?? start
}

/** The layout without these blocks. */
export const withoutBlocks = (layout: CanvasLayout, ids: readonly string[]): CanvasLayout => ({
  ...layout,
  blocks: Object.fromEntries(Object.entries(layout.blocks).filter(([id]) => !ids.includes(id))),
})

/** Reads a stored layout, keeping only what makes sense (a file from elsewhere may hold anything). */
export const readLayout = (value: unknown): CanvasLayout => {
  if (typeof value !== 'object' || value === null) return EMPTY_LAYOUT
  const data = value as { blocks?: unknown; chips?: unknown }
  const point = (p: unknown): p is Point => typeof p === 'object' && p !== null && Number.isFinite((p as Point).x) && Number.isFinite((p as Point).y)
  const blocks = typeof data.blocks === 'object' && data.blocks !== null ? Object.fromEntries(Object.entries(data.blocks).filter(([, p]) => point(p))) as Record<string, Point> : {}
  const chips = Array.isArray(data.chips)
    ? data.chips.filter((c): c is CanvasChip => point(c) && typeof (c as CanvasChip).id === 'string' && typeof (c as CanvasChip).path?.root === 'string' && Array.isArray((c as CanvasChip).path.steps))
    : []
  return { blocks, chips }
}
