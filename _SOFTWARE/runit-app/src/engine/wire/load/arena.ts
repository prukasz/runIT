import type { ProgramIR } from '../format/program'
import {
  ACCESSOR_HEADER_SIZE,
  BLOCK_HEADER_SIZE,
  INDEX_STEP_SIZE,
  OBJ_HEAD_SIZE,
  OBJ_WIDTH,
  POINTER_SIZE,
} from '../format/types'

const utf8Length = (text: string): number => new TextEncoder().encode(text).length

export interface ArenaUsage {
  /** The three registry tables (a pointer per slot), carved first by `vm_store_open`. */
  readonly registries: number
  readonly objects: number
  readonly accessors: number
  readonly blocks: number
  /** The arena the program needs: what OPEN's `total_size` has to be at least. */
  readonly total: number
}

/**
 * The bytes the board's arena uses for a program, by replaying its allocator: every allocation is zeroed and 4-aligned
 * (`arena_carve`, store/vm_store.c), in the order the loader makes them: the registry tables (obj, acc, blk; one pointer per
 * slot, none for an empty registry), then each object (`vm_obj_create`: head + payload + tag), then each accessor
 * (`vm_accessor_create`: header + 8 per step, and a NUL-terminated copy of each name step, in step order), then each block
 * (`vm_block_calc_size`: header + 4 per pin + state). Heap objects the program creates at run time live outside the arena.
 */
export const arenaUsage = (ir: ProgramIR): ArenaUsage => {
  let offset = 0
  const carve = (size: number): number => {
    const start = (offset + 3) & ~3
    offset = start + size
    return size
  }

  let registries = 0
  for (const count of [ir.objects.length, ir.accessors.length, ir.blocks.length]) {
    if (count > 0) registries += carve(count * POINTER_SIZE)
  }

  let objects = 0
  for (const object of ir.objects) {
    objects += carve(OBJ_HEAD_SIZE + object.count * OBJ_WIDTH[object.type].mem + utf8Length(object.name))
  }

  let accessors = 0
  for (const accessor of ir.accessors) {
    accessors += carve(ACCESSOR_HEADER_SIZE + accessor.steps.length * INDEX_STEP_SIZE)
    for (const step of accessor.steps) if (step.kind === 'name') accessors += carve(utf8Length(step.name) + 1)
  }

  let blocks = 0
  for (const block of ir.blocks) {
    blocks += carve(BLOCK_HEADER_SIZE + (block.inputs.length + block.outputs.length + block.enables.length) * POINTER_SIZE + block.state.length)
  }

  return { registries, objects, accessors, blocks, total: offset }
}
