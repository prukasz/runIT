import { pathWires } from '../compiler'
import type { CompiledProgram } from '../compiler'
import type { VmCatalog } from '../descriptors'
import type { ObjectPath, ProgramBlock } from '../project'

/*
 * Debug view: what to watch on a running program. Every block reports its ENO
 * (the debug build gives each one an ENO object), and the values on its pins:
 * the objects its enables and inputs read, and the objects it writes. The
 * subscribe packet holds a limited list, so the states that colour a block
 * (ENO, the enable sources, outputs) come first and input values fill the rest.
 */

export interface BlockWatch {
  readonly id: string
  /** ENO object wire ID. */
  readonly eno?: number
  /** The paths that gate the block after the arrangement (none = always enabled). */
  readonly enables: readonly ObjectPath[]
  readonly enableMode: 'any' | 'all'
  readonly inputs: readonly (ObjectPath | null)[]
  /** What each output pin drives: the object's path, in pin order. */
  readonly outputs: readonly ObjectPath[]
}

export interface DebugWatch {
  readonly blocks: ReadonlyMap<string, BlockWatch>
  /** Wire IDs to subscribe to, ascending. */
  readonly wires: readonly number[]
  /** Values that would not fit in the subscribe packet, left out. */
  readonly dropped: number
}

/** How many objects one subscribe packet holds: the board's limit, or what fits in a frame. */
export const subscribeRoom = (catalog: VmCatalog, maxFrameBytes: number): number => {
  const { batch, recordSize } = catalog.wire.subscribe
  const overhead = 2 + (batch?.countBytes ?? 1)
  return Math.max(0, Math.min(batch?.max ?? 0, Math.floor((maxFrameBytes - overhead) / recordSize)))
}

/**
 * The subscription while debugging: what the debug build watches, plus the
 * objects the user chose to watch (project IDs; one the program lacks is
 * skipped). `dropped` lists the chosen ones that no longer fit the packet.
 */
export const withWatched = (program: CompiledProgram, base: readonly number[], watched: Iterable<string>, room: number): { readonly wires: number[]; readonly dropped: string[] } => {
  const wires = new Set(base)
  const dropped: string[] = []
  for (const id of watched) {
    const wire = program.objects.wireIdOf.get(id)
    if (wire === undefined || wires.has(wire)) continue
    if (wires.size < room) wires.add(wire)
    else dropped.push(id)
  }
  return { wires: [...wires].sort((a, b) => a - b), dropped }
}

/** Which block runs next in block mode, counting from the first (the board does not report it): entering block mode starts a pass, each Next runs one block and the pass wraps after the last. */
export const nextBlockIndex = (current: number | undefined, count: number, action: 'enter' | 'next'): number | undefined =>
  count === 0 ? undefined : action === 'enter' ? 0 : ((current ?? 0) + 1) % count

export const debugWatch = (blocks: readonly ProgramBlock[], program: CompiledProgram, catalog: VmCatalog, maxFrameBytes: number, userSubscribed: readonly number[]): DebugWatch => {
  const layout = program.objects
  const watched = new Map<string, BlockWatch>()
  const states = new Set<number>(userSubscribed)
  const values = new Set<number>()
  const placedBlocks = new Map(program.blocks.blocks.map((block) => [block.id, block]))

  for (const block of blocks) {
    const placed = placedBlocks.get(block.id)
    if (!placed) continue
    const enables = block.enables ?? []
    const outputs = placed.outputs.map((wire): ObjectPath => ({ root: layout.objects[wire]!.node.id }))
    watched.set(block.id, { id: block.id, ...(placed.eno !== undefined ? { eno: placed.eno } : {}), enables, enableMode: block.enableMode ?? 'any', inputs: block.inputs ?? [], outputs })
    if (placed.eno !== undefined) states.add(placed.eno)
    for (const path of enables) for (const wire of pathWires(layout, path)) states.add(wire)
    for (const wire of placed.outputs) if (!layout.objects[wire]?.children) states.add(wire)
    for (const path of block.inputs ?? []) if (path) for (const wire of pathWires(layout, path)) values.add(wire)
  }

  const room = subscribeRoom(catalog, maxFrameBytes)
  const chosen = [...states]
  let dropped = 0
  for (const wire of values) {
    if (states.has(wire)) continue
    if (chosen.length < room) chosen.push(wire)
    else dropped++
  }
  if (chosen.length > room) {
    dropped += chosen.length - room
    chosen.length = room
  }
  return { blocks: watched, wires: chosen.sort((a, b) => a - b), dropped }
}
