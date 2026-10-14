import { blocksFromGenerated } from './fromGenerated'
import type { CatalogResult, GeneratedInput } from './fromGenerated'
import type { BlockType, Category } from './types'

/** The palette: every block type the engine knows, looked up by descriptor id or by the number the board uses. */
export class Catalog {
  readonly blocks: readonly BlockType[]
  /** What the adapter had to assume while reading the descriptors (see CatalogResult). Empty when the descriptors were complete. */
  readonly warnings: readonly string[]
  private readonly byId: ReadonlyMap<string, BlockType>
  private readonly byWire: ReadonlyMap<number, BlockType>

  constructor(result: CatalogResult) {
    this.blocks = result.blocks
    this.warnings = result.warnings
    this.byId = new Map(result.blocks.map((b) => [b.id, b]))
    this.byWire = new Map(result.blocks.map((b) => [b.wireId, b]))
  }

  static fromGenerated(input: GeneratedInput): Catalog {
    return new Catalog(blocksFromGenerated(input))
  }

  get(id: string): BlockType | undefined {
    return this.byId.get(id)
  }

  /** The block type, or an error that names it: for code that has already checked the id. */
  require(id: string): BlockType {
    const block = this.byId.get(id)
    if (!block) throw new Error(`The catalog has no block '${id}'.`)
    return block
  }

  byWireId(wireId: number): BlockType | undefined {
    return this.byWire.get(wireId)
  }

  /** The palette grouped by category, in palette order. */
  categories(): ReadonlyMap<Category, readonly BlockType[]> {
    const groups = new Map<Category, BlockType[]>()
    for (const block of this.blocks) {
      const group = groups.get(block.category)
      if (group) group.push(block)
      else groups.set(block.category, [block])
    }
    return groups
  }
}
