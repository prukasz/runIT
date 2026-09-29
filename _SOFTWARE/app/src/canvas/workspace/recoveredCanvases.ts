import { placeBlocks } from '../../domain/canvas'
import { runitVmCatalog } from '../../domain/descriptors'
import type { ProgramBlock, ProjectCanvas } from '../../domain/project'
import { blockShape } from '../blocks/blockView'

/** Blocks recovered from stored code, laid out at their real sizes on one canvas (none, no canvas). */
export const recoveredCanvases = (blocks: readonly ProgramBlock[]): ProjectCanvas[] => {
  if (!blocks.length) return []
  const catalog = runitVmCatalog()
  const placed = placeBlocks(blocks, (block) => blockShape(catalog.block(block.type), block))
  return [{ id: `canvas-${Date.now().toString(36)}`, name: 'Recovered', blocks: placed }]
}
