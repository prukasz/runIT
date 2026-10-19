import type { Catalog } from '../catalog/catalog'
import type { Diagnostic } from '../model/program/diagnostics'
import type { ObjectPath } from '../model/paths/paths'
import { concretePins } from '../model/program/pins'
import type { BlockInstance, Project, Source } from '../model/program/program'
import { DEFAULT_LIMITS } from '../wire/load/limits'
import type { VmLimits } from '../wire/load/limits'

/*
 * Execution order. The board runs the blocks in the order they are loaded, once per pass, and a block sees what an earlier
 * block wrote in the same pass: so a block must come after every block whose output (or enable-out) it reads, and after
 * every block that writes a variable it reads. Among blocks the wiring does not order, the user's order stands.
 *
 * A loop block (`span` in its descriptor: for) owns the run of blocks right below it. Its body is every block gated by its
 * enable-out, and everything that depends on those, except a block whose Run when comes from outside the loop (and what
 * depends on the loop only through it): that one runs after the loop, when its own gate fires. The board loops the body
 * inside the pass (`vm_exec_run_range`), so the body must be contiguous and directly follow the loop block, with everything
 * it reads placed before the loop block.
 */

export interface Arrangement {
  /** Block ids in execution order. */
  readonly order: readonly string[]
  /** For each loop block: the run it owns, as positions in `order` ([start, end), `vm_span_t`). */
  readonly spans: ReadonlyMap<string, { readonly start: number; readonly end: number }>
  readonly diagnostics: readonly Diagnostic[]
}

const block = (id: string, severity: Diagnostic['severity'], code: string, message: string): Diagnostic => ({ severity, code, message, where: { kind: 'block', id } })

const present = (sources: readonly (Source | null)[]): Source[] => sources.filter((s): s is Source => s !== null)

export const arrange = (catalog: Catalog, project: Project, limits: VmLimits = DEFAULT_LIMITS): Arrangement => {
  const diagnostics: Diagnostic[] = []
  const blocks = project.blocks
  const index = new Map(blocks.map((b, i) => [b.id, i]))

  const note = (map: Map<string, Set<string>>, key: string, value: string) => {
    const set = map.get(key)
    if (set) set.add(value)
    else map.set(key, new Set([value]))
  }

  /** Blocks that read / write each variable (by root variable id: any path into it counts). */
  const readers = new Map<string, Set<string>>()
  const writers = new Map<string, Set<string>>()
  /** `a -> b`: a runs before b. */
  const successors = new Map<string, Set<string>>()
  const predecessors = new Map<string, Set<string>>()
  const link = (a: string, b: string) => {
    if (a === b || !index.has(a) || !index.has(b)) return
    note(successors, a, b)
    note(predecessors, b, a)
  }

  const loopTypes = new Set(catalog.blocks.filter((t) => t.span).map((t) => t.id))

  for (const instance of blocks) {
    const type = catalog.get(instance.type)
    const pins = type ? concretePins(type, instance) : undefined
    /** A path reads its root, and every variable that picks one of its positions. */
    const readVariable = (path: ObjectPath) => {
      note(readers, path.root, instance.id)
      for (const step of path.steps) if (step.kind === 'dynamic') readVariable(step.path)
    }
    const read = (source: Source) => {
      if (source.kind === 'variable') readVariable(source.path)
      else link(source.block, instance.id)
    }
    instance.inputs.forEach((source, i) => {
      if (!source) return
      if (pins?.inputs[i]?.pin.writes && source.kind === 'variable') note(writers, source.path.root, instance.id)
      else read(source)
    })
    for (const source of present(instance.enables)) read(source)
    for (const target of instance.outputs) if (target) note(writers, target.path.root, instance.id)
  }
  for (const [variable, who] of writers) {
    for (const writer of who) for (const reader of readers.get(variable) ?? []) link(writer, reader)
  }

  /* Loop bodies: what the loop's enable-out gates, and everything downstream of that, but for a block with a Run when of
     its own from outside the loop (a branch, a tick, a variable): it runs when that fires, after the loop, and so does
     what is downstream only through it. Repeated until no such block is left in the body. */
  const byId = new Map(blocks.map((b) => [b.id, b]))
  const bodies = new Map<string, Set<string>>()
  for (const instance of blocks) {
    if (!loopTypes.has(instance.type)) continue
    const seeds = blocks.filter((b) => b.enables.some((s) => s.kind === 'eno' && s.block === instance.id)).map((b) => b.id)
    const outside = new Set<string>()
    let body = new Set<string>()
    for (;;) {
      body = new Set<string>()
      const queue = seeds.filter((id) => !outside.has(id))
      while (queue.length) {
        const id = queue.pop()!
        if (body.has(id) || outside.has(id)) continue
        body.add(id)
        for (const next of successors.get(id) ?? []) queue.push(next)
      }
      const gatedElsewhere = [...body].filter((id) =>
        present(byId.get(id)!.enables).some((s) => s.kind === 'variable' || (s.block !== instance.id && !body.has(s.block))))
      if (gatedElsewhere.length === 0) break
      for (const id of gatedElsewhere) outside.add(id)
    }
    if (body.has(instance.id)) {
      diagnostics.push(block(instance.id, 'error', 'loop-feeds-itself', 'The loop is fed by its own body: something inside the loop is read by the loop block.'))
      body.delete(instance.id)
    }
    bodies.set(instance.id, body)
  }

  /* What a body reads from outside it must run before the loop block. */
  for (const [loop, body] of bodies) {
    for (const member of body) {
      for (const before of predecessors.get(member) ?? []) if (!body.has(before) && before !== loop) link(before, loop)
    }
  }

  /* Kahn, among ready blocks the user's order first. A cycle (block A reads B, B reads A: a feedback) is cut at the block
     the user placed first; the one that closes it then reads the previous pass's value. */
  const indegree = new Map(blocks.map((b) => [b.id, predecessors.get(b.id)?.size ?? 0]))
  const remaining = new Set(blocks.map((b) => b.id))
  const sorted: string[] = []
  const cut = new Set<string>()
  while (remaining.size > 0) {
    let pick: string | undefined
    for (const id of remaining) if ((indegree.get(id) ?? 0) === 0 && (pick === undefined || index.get(id)! < index.get(pick)!)) pick = id
    if (pick === undefined) {
      for (const id of remaining) if (pick === undefined || index.get(id)! < index.get(pick)!) pick = id
      cut.add(pick!)
    }
    remaining.delete(pick!)
    sorted.push(pick!)
    for (const next of successors.get(pick!) ?? []) indegree.set(next, (indegree.get(next) ?? 1) - 1)
  }
  for (const id of cut) {
    diagnostics.push(block(id, 'warning', 'feedback', 'Blocks read each other in a circle: this block runs first and reads the previous pass of the one that follows.'))
  }

  /* Bodies contiguous: a loop block is followed by its body, in order, loops inside the body the same way. */
  const position = new Map(sorted.map((id, i) => [id, i]))
  const byOrder = (ids: Iterable<string>) => [...ids].sort((a, b) => position.get(a)! - position.get(b)!)
  const order: string[] = []
  const placed = new Set<string>()
  const place = (ids: readonly string[]) => {
    for (const id of ids) {
      if (placed.has(id)) continue
      placed.add(id)
      order.push(id)
      const body = bodies.get(id)
      if (body && body.size > 0) place(byOrder([...body].filter((m) => !placed.has(m))))
    }
  }
  place(sorted)

  const spans = new Map<string, { start: number; end: number }>()
  const at = new Map(order.map((id, i) => [id, i]))
  for (const [loop, body] of bodies) {
    const start = at.get(loop)! + 1
    const end = start + body.size
    if (body.size === 0) diagnostics.push(block(loop, 'error', 'loop-empty', 'The loop has no body: put a block on its "loop body" enable.'))
    else if (order.slice(start, end).some((id) => !body.has(id))) {
      diagnostics.push(block(loop, 'error', 'loop-bodies-overlap', 'The loop body shares blocks with another loop that it does not contain.'))
    }
    spans.set(loop, { start, end })
  }

  /* Nesting: the top-level run is the first level of `spanDepthMax` (`vm_exec_run_range` counts it), so loops nest one less. */
  for (const [loop, span] of spans) {
    let depth = 1
    for (const [other, outer] of spans) if (other !== loop && outer.start < span.start && span.end <= outer.end) depth++
    if (depth > limits.spanDepthMax - 1) {
      diagnostics.push(block(loop, 'error', 'ERR_VM_EXEC_SPAN_DEPTH', `Loops are nested ${depth} deep; the board runs ${limits.spanDepthMax - 1}.`))
    }
  }

  return { order, spans, diagnostics }
}

/** The blocks of `project` in execution order. */
export const inExecutionOrder = (project: Project, arrangement: Arrangement): BlockInstance[] => {
  const byId = new Map(project.blocks.map((b) => [b.id, b]))
  return arrangement.order.map((id) => byId.get(id)!)
}
