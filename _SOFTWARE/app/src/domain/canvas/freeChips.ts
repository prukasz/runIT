import type { CanvasBlock, CanvasVariable, ObjectPath, ProjectCanvas } from './types'
import type { Point } from './canvas'
import { connect, same } from './wiring'
import type { WireTarget } from './wiring'

/*
 * Variables pulled off their pins (`ProjectCanvas.variables`). A free chip stores only its path and place: the pins reading that
 * path are the ones wired to it, so undocking, moving and deleting a chip never changes what the program does.
 */

/** Size of a free chip on the canvas (px at 100 % zoom): the wires meet its edges. */
export const CHIP_WIDTH = 132
export const CHIP_HEIGHT = 24

const variablesOf = (canvas: ProjectCanvas): readonly CanvasVariable[] => canvas.variables ?? []

const withVariables = (canvas: ProjectCanvas, variables: readonly CanvasVariable[]): ProjectCanvas => {
  const { variables: _old, ...rest } = canvas
  return variables.length ? { ...rest, variables } : rest
}

/** The path a pin holds: an input's path, or an output's user object (a whole object, no steps). */
export const pinPath = (block: CanvasBlock, target: WireTarget): ObjectPath | undefined => {
  if (target.kind === 'in') return block.inputs?.[target.index] ?? undefined
  if (target.kind === 'out') {
    const root = block.outputs?.[target.index]
    return root ? { root } : undefined
  }
  return block.enables?.[target.index ?? -1]
}

/** The free chip standing for a path on this canvas (the first one), if any. */
export const freeChipFor = (canvas: ProjectCanvas, path: ObjectPath): CanvasVariable | undefined => variablesOf(canvas).find((entry) => same(entry.path, path))

const newVariableId = (canvas: ProjectCanvas): string => {
  const taken = new Set(variablesOf(canvas).map((entry) => entry.id))
  for (let index = 1; ; index++) if (!taken.has(`var${index}`)) return `var${index}`
}

/** A chip for `path` at `at`. */
export const addFreeChip = (canvas: ProjectCanvas, path: ObjectPath, at: Point): ProjectCanvas =>
  withVariables(canvas, [...variablesOf(canvas), { id: newVariableId(canvas), path, x: at.x, y: at.y }])

export const moveFreeChip = (canvas: ProjectCanvas, id: string, at: Point): ProjectCanvas =>
  withVariables(canvas, variablesOf(canvas).map((entry) => (entry.id === id ? { ...entry, x: at.x, y: at.y } : entry)))

export const removeFreeChip = (canvas: ProjectCanvas, id: string): ProjectCanvas =>
  withVariables(canvas, variablesOf(canvas).filter((entry) => entry.id !== id))

/**
 * A free chip dropped on a pin: the pin takes its path, and the chip goes (the pin shows it docked again; any other pin reading
 * the path is docked too).
 */
export const dockFreeChip = (canvas: ProjectCanvas, id: string, blockId: string, target: WireTarget): ProjectCanvas => {
  const chip = variablesOf(canvas).find((entry) => entry.id === id)
  if (!chip) return canvas
  const docked: ProjectCanvas = { ...canvas, blocks: canvas.blocks.map((block) => (block.id === blockId ? connect(block, target, chip.path) : block)) }
  return removeFreeChip(docked, id)
}
