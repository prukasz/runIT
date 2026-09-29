import type { Diagnostic } from '../compiler/objects'
import type { CanvasBlock, ObjectPath, ProgramBlock, ProjectCanvas } from '../project'

/*
 * Canvases → the program's block list (features.md §8.1). Three things the
 * user never writes down are worked out here:
 *
 * - Execution order. Per canvas, the trees (blocks joined by wires) run top to
 *   bottom by their topmost block; inside a tree a block runs after every block
 *   it reads or is gated by, ties broken top to bottom, then left to right. The
 *   canvases follow in list order. A loop in the wires is cut at the block
 *   placed first; the block reading across the cut sees the previous pass.
 *
 * - Enables. Only paths explicitly connected to a block's EN gate it. No EN
 *   paths means always enabled. Loop membership still follows data wires so
 *   dependent blocks execute inside their FOR body.
 *
 * - Loop bodies. A FOR's ENO on a block's EN puts the block in the loop: it,
 *   and every block depending on it (through data wires or its gates), is that
 *   FOR's body, placed right after it. That link never reaches the VM as an
 *   enable. (`<for id>:body` is the older spelling of the same link.) Wires only count within one canvas; a path to a block on another
 *   canvas is a plain read.
 */

export interface Arrangement {
  /** The program, in execution order (block ID = position). */
  readonly blocks: readonly ProgramBlock[]
  /** Per block: its enables after inheritance and the loops (FOR IDs) it runs in, innermost last. */
  readonly gates: ReadonlyMap<string, { readonly enables: readonly ObjectPath[]; readonly mode?: 'any' | 'all'; readonly loops: readonly string[] }>
  readonly diagnostics: readonly Diagnostic[]
}

const BLOCK_PIN = /^(.+):(q\d+|eno|body)$/
/** The link that puts a block in a FOR's body: the FOR's ENO on its EN. */
export const loopBodyGate = (forId: string): ObjectPath => ({ root: `${forId}:eno` })

/** Every root a path reads: the path itself and the dynamic indices inside it. */
const roots = (path: ObjectPath): string[] => [path.root, ...(path.steps ?? []).flatMap((step) => (step.kind === 'dynamic' ? roots(step.index) : []))]

const strip = ({ x: _x, y: _y, view: _view, name: _name, outputAliases: _outputAliases, inheritGates: _inherit, ...block }: CanvasBlock): ProgramBlock => block

export const arrangeProgram = (canvases: readonly ProjectCanvas[], options: { includeDisabled?: boolean } = {}): Arrangement => {
  const diagnostics: Diagnostic[] = []
  const gates = new Map<string, { enables: ObjectPath[]; mode?: 'any' | 'all'; loops: string[] }>()
  const out: ProgramBlock[] = []

  for (const canvas of canvases) {
    if (canvas.disabled && !options.includeDisabled) continue
    const byId = new Map(canvas.blocks.map((block) => [block.id, block]))
    const blockOf = (root: string): string | undefined => {
      const id = BLOCK_PIN.exec(root)?.[1]
      return id !== undefined && byId.has(id) ? id : undefined
    }
    const sourcesOf = (paths: readonly (ObjectPath | null)[], self: string): string[] =>
      [...new Set(paths.flatMap((path) => (path ? roots(path) : [])).map(blockOf).filter((id): id is string => id !== undefined && id !== self))]
    const data = new Map(canvas.blocks.map((block) => [block.id, sourcesOf(block.inputs ?? [], block.id)]))
    const gating = new Map(canvas.blocks.map((block) => [block.id, sourcesOf(block.enables ?? [], block.id)]))
    const before = (a: CanvasBlock, b: CanvasBlock) => a.y - b.y || a.x - b.x || a.id.localeCompare(b.id)

    // Trees: blocks joined by any wire, topmost tree first.
    const parent = new Map(canvas.blocks.map((block) => [block.id, block.id]))
    const find = (id: string): string => (parent.get(id) === id ? id : find(parent.get(id)!))
    for (const block of canvas.blocks) for (const source of [...data.get(block.id)!, ...gating.get(block.id)!]) parent.set(find(source), find(block.id))
    const trees = new Map<string, CanvasBlock[]>()
    for (const block of canvas.blocks) trees.set(find(block.id), [...(trees.get(find(block.id)) ?? []), block])
    const ordered = [...trees.values()].map((tree) => [...tree].sort(before)).sort((a, b) => before(a[0]!, b[0]!))

    // Inside a tree: after everything it reads or is gated by (Kahn), ties top-left first.
    const order: CanvasBlock[] = []
    for (const tree of ordered) {
      const placed = new Set<string>()
      const pending = [...tree]
      while (pending.length) {
        const ready = pending.find((block) => [...data.get(block.id)!, ...gating.get(block.id)!].every((source) => placed.has(source)))
        const next = ready ?? pending[0]!
        if (!ready) {
          for (const source of [...data.get(next.id)!, ...gating.get(next.id)!].filter((id) => !placed.has(id))) {
            diagnostics.push({ severity: 'warning', blockId: next.id, message: `Block '${next.id}': the wires loop, it reads '${source}' from the previous pass.` })
          }
        }
        placed.add(next.id)
        order.push(next)
        pending.splice(pending.indexOf(next), 1)
      }
    }

    // Enables and loop bodies, sources first.
    const loopsOf = new Map<string, Set<string>>()
    for (const block of order) {
      const error = (message: string) => diagnostics.push({ severity: 'error', blockId: block.id, message: `Block '${block.id}': ${message}` })
      // A FOR's ENO (or the older `:body`) on EN is loop membership, not an enable.
      const loopOf = (path: ObjectPath): string | undefined => {
        const match = path.steps?.length ? null : /^(.+):(eno|body)$/.exec(path.root)
        if (!match) return undefined
        if (byId.get(match[1]!)?.type === 'FOR') return match[1]
        if (match[2] === 'body') error(`'${match[1]}' is not a loop on this canvas; only a FOR has a body.`)
        return undefined
      }
      const explicit: ObjectPath[] = []
      const loops = new Set<string>()
      for (const path of block.enables ?? []) {
        const loop = loopOf(path)
        if (loop) loops.add(loop)
        else if (!path.root.endsWith(':body') || path.steps?.length) explicit.push(path)
      }
      for (const source of gating.get(block.id)!) for (const loop of loopsOf.get(source) ?? []) loops.add(loop)
      for (const source of data.get(block.id)!) for (const loop of loopsOf.get(source) ?? []) loops.add(loop)
      loopsOf.set(block.id, loops)

      gates.set(block.id, { enables: explicit, ...(block.enableMode ? { mode: block.enableMode } : {}), loops: [] })
    }

    // A block in two loops that aren't nested can't be placed.
    const loopList = (id: string): string[] => {
      const loops = [...(loopsOf.get(id) ?? [])]
      return loops.sort((a, b) => (loopsOf.get(a)?.has(b) ? 1 : loopsOf.get(b)?.has(a) ? -1 : 0))
    }
    for (const block of order) {
      const loops = loopList(block.id)
      for (let index = 1; index < loops.length; index++) {
        if (!loopsOf.get(loops[index]!)?.has(loops[index - 1]!)) diagnostics.push({ severity: 'error', blockId: block.id, message: `Block '${block.id}': it is in loops '${loops[index - 1]}' and '${loops[index]}', and neither runs inside the other.` })
      }
      gates.get(block.id)!.loops = loops
    }

    // Place each FOR's body right after it (nested loops recursively).
    const place = (list: readonly CanvasBlock[]): ProgramBlock[] => {
      const result: ProgramBlock[] = []
      const done = new Set<string>()
      for (const block of list) {
        if (done.has(block.id)) continue
        done.add(block.id)
        const { enables, mode } = gates.get(block.id)!
        const { enables: _own, enableMode: _ownMode, ...rest } = strip(block)
        const program: ProgramBlock = { ...rest, ...(enables.length ? { enables } : {}), ...(enables.length && mode ? { enableMode: mode } : {}) }
        if (block.type === 'FOR') {
          const members = list.filter((entry) => !done.has(entry.id) && loopsOf.get(entry.id)?.has(block.id))
          members.forEach((entry) => done.add(entry.id))
          const body = place(members)
          result.push({ ...program, body: members.length ? body.length : (block.body ?? 0) }, ...body)
        } else result.push(program)
      }
      return result
    }
    out.push(...place(order))
  }
  return { blocks: out, gates, diagnostics }
}
