import { blockPinAt, runitVmCatalog } from '../../domain/descriptors'
import type { ObjectPath, ProjectCanvas } from '../../domain/project'
import { blockHeadline } from './blockView'

/*
 * Where a variable is used: the blocks whose pins, enables or dynamic
 * positions point at it (or at a folder holding it), for the variable's
 * "Connected blocks" list.
 */

export interface ObjectUse {
  readonly canvasId: string
  readonly canvasName: string
  readonly blockId: string
  /** The block as the canvas titles it: its name, else its title with settings. */
  readonly blockLabel: string
  /** What the block does with it: `Reads Signal`, `Writes Result`, `Run when`, with `(in folder)` when only a folder holding it is wired. */
  readonly role: string
}

/** The roots a path reaches: its own and those of its dynamic positions. */
const rootsOf = (path: ObjectPath): string[] => [path.root, ...(path.steps ?? []).flatMap((step) => (step.kind === 'dynamic' ? rootsOf(step.index) : []))]

/** `direct`: the variable itself; `folders`: folders holding it (a path into one may reach it). */
export const objectUses = (canvases: readonly ProjectCanvas[], direct: ReadonlySet<string>, folders: ReadonlySet<string> = new Set()): ObjectUse[] => {
  const catalog = runitVmCatalog()
  const uses: ObjectUse[] = []
  for (const canvas of canvases) {
    for (const block of canvas.blocks) {
      const type = catalog.block(block.type)
      const label = block.name || blockHeadline(type, block) || type?.title || block.type
      const add = (role: string, roots: readonly string[]) => {
        const via = roots.some((root) => folders.has(root))
        if (roots.some((root) => direct.has(root)) || via) uses.push({ canvasId: canvas.id, canvasName: canvas.name, blockId: block.id, blockLabel: label, role: roots.some((root) => direct.has(root)) ? role : `${role} (in folder)` })
      }
      const pinName = (pins: NonNullable<typeof type>['inputs'] | undefined, index: number) => {
        if (!pins) return String(index)
        const pin = blockPinAt(pins, index)
        const repeated = index >= pins.pins.length - 1 && pins.max > pins.pins.length
        return pin ? (repeated ? `${pin.title} ${index}` : pin.title) : String(index)
      }
      block.inputs?.forEach((path, index) => { if (path) add(`Reads ${pinName(type?.inputs, index)}`, rootsOf(path)) })
      block.outputs?.forEach((root, index) => { if (root) add(`Writes ${pinName(type?.outputs, index)}`, [root]) })
      block.enables?.forEach((path) => add('Run when', rootsOf(path)))
    }
  }
  return uses
}
