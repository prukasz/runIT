import type { BlockInstance, ConcretePin, ObjectPath, ValueKind } from '@engine'
import { formulaText, headline, headlineText, pinsOf } from '../blocks'
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
  measuring.font = `${font} ${family()}`
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
    outputs: (pins?.outputs ?? []).map(view((i) => !!block.outputs[i] || ctx.project.blocks.some((b) => b.inputs.some((s) => s?.kind === 'output' && s.block === block.id && s.pin === i) || b.enables.some((s) => s.kind === 'output' && s.block === block.id && s.pin === i)))),
  }
}

/** The block's size, the least that holds what it shows, on the grid. Its header follows the names of what it reads, not ids. */
export const blockShape = (ctx: BlockContext, block: BlockInstance, measure: Measure = measureText): BlockShape => {
  const { inputs, outputs } = pinViews(ctx, block)
  const rows = Math.max(1, inputs.length, outputs.length)
  const title = block.name?.trim() || headlineText(headline(ctx, block))
  const header = measure(title, '600 12px', 6.8) + 24 + 8
  // An expression block shows its formula under the pins (at most three widths of the smallest block; longer is cut).
  const formula = formulaText(ctx, block)
  const output = block.outputs[0]
  const line = formula === undefined ? undefined : `${output ? 'x' : outputs[0]?.title ?? 'Result'} = ${formula || 'Set a formula'}`
  const formulaWidth = line === undefined ? 0 : Math.min(3 * MIN_WIDTH, measure(line, '500 11px', 7) + 24 + line.split(' ').length * 4)
  const width = Math.max(MIN_WIDTH, Math.ceil(Math.max(header, pinsWidth(inputs, outputs, measure), formulaWidth) / GRID) * GRID)
  return { width, height: HEADER + rows * ROW + (formula === undefined ? 0 : FORMULA_ROW), inputs, outputs, row: ROW, formula: formula !== undefined }
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

const ARROW_LENGTH = 22
const ARROW_HALF = 15
const BAND_HALF = 6

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

/** A Run when link: a band, narrow where it leaves its source and as tall as the block where it arrives, with an arrow head at `at`. */
export const ribbon = (from: RibbonSpan, to: RibbonSpan, at: number): string => {
  const { dx, back } = reach(from.x, to.x)
  const end = to.x - ARROW_LENGTH
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
