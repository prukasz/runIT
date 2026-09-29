import type { CanvasBlock, ProgramBlock, ProjectCanvas } from '../project'
import { arrangeProgram } from './arrange'

/*
 * Canvases: the list (execution order), the grid and the viewport. A project
 * is one program run in one scan cycle; its blocks are every enabled canvas's
 * blocks, the canvases in list order (features.md CAN-4).
 *
 * Canvas units are px at 100 % zoom. The viewport maps them to the screen:
 * screen = canvas × zoom + offset.
 */

/** Canvas units per grid cell. */
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

/** The nearest grid line. */
export const snap = (value: number, grid = GRID): number => Math.round(value / grid) * grid

export const snapPoint = (point: Point, grid = GRID): Point => ({ x: snap(point.x, grid), y: snap(point.y, grid) })

export const screenToCanvas = (viewport: Viewport, point: Point): Point => ({ x: (point.x - viewport.x) / viewport.zoom, y: (point.y - viewport.y) / viewport.zoom })

export const canvasToScreen = (viewport: Viewport, point: Point): Point => ({ x: point.x * viewport.zoom + viewport.x, y: point.y * viewport.zoom + viewport.y })

const clampZoom = (zoom: number): number => Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, zoom))

/** Zoom by `factor`, keeping the canvas point under `anchor` (screen px) where it is. */
export const zoomAt = (viewport: Viewport, factor: number, anchor: Point): Viewport => {
  const zoom = clampZoom(viewport.zoom * factor)
  const under = screenToCanvas(viewport, anchor)
  return { zoom, x: anchor.x - under.x * zoom, y: anchor.y - under.y * zoom }
}

// ---------------------------------------------------------------------------
// The list
// ---------------------------------------------------------------------------

/** `Canvas <n>`, the lowest n not taken. */
export const newCanvasName = (canvases: readonly ProjectCanvas[]): string => {
  const names = new Set(canvases.map((canvas) => canvas.name))
  for (let index = 1; ; index++) if (!names.has(`Canvas ${index}`)) return `Canvas ${index}`
}

const find = (canvases: readonly ProjectCanvas[], id: string): number => {
  const index = canvases.findIndex((canvas) => canvas.id === id)
  if (index < 0) throw new Error(`No canvas '${id}'.`)
  return index
}

export const addCanvas = (canvases: readonly ProjectCanvas[], id: string, name = newCanvasName(canvases)): ProjectCanvas[] => {
  if (canvases.some((canvas) => canvas.id === id)) throw new Error(`Canvas ID '${id}' is taken.`)
  return [...canvases, { id, name, blocks: [] }]
}

export const renameCanvas = (canvases: readonly ProjectCanvas[], id: string, name: string): ProjectCanvas[] => {
  const trimmed = name.trim()
  if (!trimmed) throw new Error('A canvas needs a name.')
  const index = find(canvases, id)
  return canvases.map((canvas, at) => (at === index ? { ...canvas, name: trimmed } : canvas))
}

export const removeCanvas = (canvases: readonly ProjectCanvas[], id: string): ProjectCanvas[] => {
  const index = find(canvases, id)
  return canvases.filter((_, at) => at !== index)
}

/** Move a canvas to position `to` (0 = runs first). */
export const moveCanvas = (canvases: readonly ProjectCanvas[], id: string, to: number): ProjectCanvas[] => {
  const from = find(canvases, id)
  const target = Math.min(canvases.length - 1, Math.max(0, to))
  if (from === target) return [...canvases]
  const next = [...canvases]
  const [moved] = next.splice(from, 1)
  next.splice(target, 0, moved!)
  return next
}

/** A disabled canvas stays in the project but is left out of the program. */
export const setCanvasDisabled = (canvases: readonly ProjectCanvas[], id: string, disabled: boolean): ProjectCanvas[] => {
  const index = find(canvases, id)
  return canvases.map((canvas, at) => {
    if (at !== index) return canvas
    const { disabled: _, ...rest } = canvas
    return disabled ? { ...rest, disabled: true } : rest
  })
}

/** The program: every enabled canvas's blocks in execution order, explicit gates kept, and loop bodies placed (arrange.ts). */
export const programBlocks = (canvases: readonly ProjectCanvas[]): readonly ProgramBlock[] => arrangeProgram(canvases).blocks

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

/** The canvas holding a block, and the block. */
export const findBlock = (canvases: readonly ProjectCanvas[], blockId: string): { canvas: ProjectCanvas; block: CanvasBlock } | undefined => {
  for (const canvas of canvases) {
    const block = canvas.blocks.find((entry) => entry.id === blockId)
    if (block) return { canvas, block }
  }
  return undefined
}

/** `<type><n>` in lower case (`periodic1`, `expr3`), the lowest n free on every canvas: wires name blocks by it (`periodic1:q0`). */
export const newBlockId = (canvases: readonly ProjectCanvas[], typeKey: string): string => {
  const taken = new Set(canvases.flatMap((canvas) => canvas.blocks.map((block) => block.id)))
  const base = typeKey.toLowerCase()
  for (let index = 1; ; index++) if (!taken.has(`${base}${index}`)) return `${base}${index}`
}

const replaceBlocks = (canvases: readonly ProjectCanvas[], canvasId: string, change: (blocks: readonly CanvasBlock[]) => readonly CanvasBlock[]): ProjectCanvas[] => {
  const index = find(canvases, canvasId)
  return canvases.map((canvas, at) => (at === index ? { ...canvas, blocks: change(canvas.blocks) } : canvas))
}

export const addBlock = (canvases: readonly ProjectCanvas[], canvasId: string, block: CanvasBlock): ProjectCanvas[] => {
  if (findBlock(canvases, block.id)) throw new Error(`Block ID '${block.id}' is taken.`)
  return replaceBlocks(canvases, canvasId, (blocks) => [...blocks, block])
}

/**
 * A copy of a block on a canvas at `at`, under a new ID of its type. It keeps
 * its settings, inputs and gates (it reads what the original reads); the user
 * variables the original writes are dropped, as a variable has one writer.
 */
export const pasteBlock = (canvases: readonly ProjectCanvas[], canvasId: string, copy: CanvasBlock, at: Point): { canvases: ProjectCanvas[]; id: string } => {
  const id = newBlockId(canvases, copy.type)
  const outputs = copy.outputs?.map(() => null)
  const { outputs: _outputs, ...rest } = copy
  const block: CanvasBlock = { ...rest, id, x: at.x, y: at.y, ...(outputs?.length ? { outputs } : {}) }
  return { canvases: addBlock(canvases, canvasId, block), id }
}

/** Change a block (settings, position …); its ID stays. */
export const updateBlock = (canvases: readonly ProjectCanvas[], blockId: string, change: (block: CanvasBlock) => CanvasBlock): ProjectCanvas[] => {
  const found = findBlock(canvases, blockId)
  if (!found) throw new Error(`No block '${blockId}'.`)
  return replaceBlocks(canvases, found.canvas.id, (blocks) => blocks.map((block) => (block.id === blockId ? { ...change(block), id: blockId } : block)))
}

export const moveBlock = (canvases: readonly ProjectCanvas[], blockId: string, to: Point): ProjectCanvas[] => updateBlock(canvases, blockId, (block) => ({ ...block, x: to.x, y: to.y }))

export const removeBlock = (canvases: readonly ProjectCanvas[], blockId: string): ProjectCanvas[] => {
  const found = findBlock(canvases, blockId)
  if (!found) throw new Error(`No block '${blockId}'.`)
  return replaceBlocks(canvases, found.canvas.id, (blocks) => blocks.filter((block) => block.id !== blockId))
}
