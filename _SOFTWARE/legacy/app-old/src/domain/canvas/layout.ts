import type { CanvasBlock, ObjectPath, ProgramBlock } from '../project'
import { GRID, snap } from './canvas'

/*
 * Places blocks that have no places yet (recovered from stored code, or a
 * program built by hand): a column per depth, so a block stands right of every
 * block it reads or is gated by (loop bodies right of their FOR), and one row
 * each, top to bottom in the given order. The order down the page is what the
 * canvas compiles back to, so the program keeps the order it came in.
 */

export interface BlockSize {
  readonly width: number
  readonly height: number
}

const BLOCK_PIN = /^(.+):(q\d+|eno|body)$/
const roots = (path: ObjectPath): string[] => [path.root, ...(path.steps ?? []).flatMap((step) => (step.kind === 'dynamic' ? roots(step.index) : []))]

export const placeBlocks = (blocks: readonly ProgramBlock[], sizeOf: (block: ProgramBlock) => BlockSize, gap = 3 * GRID): CanvasBlock[] => {
  const depth = new Map<string, number>()
  for (const block of blocks) {
    const sources = [...(block.inputs ?? []), ...(block.enables ?? [])]
      .flatMap((path) => (path ? roots(path) : []))
      .map((root) => BLOCK_PIN.exec(root)?.[1])
      .filter((id): id is string => id !== undefined && id !== block.id && depth.has(id))
    depth.set(block.id, sources.length ? 1 + Math.max(...sources.map((id) => depth.get(id)!)) : 0)
  }
  const columns: number[] = []
  for (const block of blocks) columns[depth.get(block.id)!] = Math.max(columns[depth.get(block.id)!] ?? 0, sizeOf(block).width)
  const left: number[] = []
  columns.reduce((x, width, column) => {
    left[column] = x
    return x + (width ?? 0) + gap
  }, 0)

  let y = 0
  return blocks.map((block) => {
    const placed: CanvasBlock = { ...block, x: snap(left[depth.get(block.id)!] ?? 0), y: snap(y) }
    y += sizeOf(block).height + gap
    return placed
  })
}
