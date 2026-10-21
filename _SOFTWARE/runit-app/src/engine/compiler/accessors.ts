import type { VariableLayout } from '../model/variables/layout'
import { findVariable } from '../model/variables/variables'
import type { Variable } from '../model/variables/variables'
import { resolvePath } from '../model/paths/paths'
import type { ObjectPath, Target } from '../model/paths/paths'
import type { AccessorDef } from '../wire/format/program'
import type { IndexStep } from '../wire/format/types'

/*
 * Paths to accessors. An accessor on the board is a root object and index steps; one with no steps, or one literal step on
 * the root, is resolved at load and read in a few loads (`vm_accessor_bind`, `vm_acc_resolve`), anything else walks
 * (`vm_acc_walk`). So a path is folded into its root wherever the walk is the same every time:
 *
 *   motors[1][2]  ->  root = the object of motors[1], step [2]
 *
 * It is not folded through a folder whose entries a block can re-link at run time (a `cell` pin names one of its entries:
 * clone writes a new object there), nor past a position read at run time. Identical accessors are made once.
 */

export class AccessorTable {
  readonly defs: AccessorDef[] = []
  private readonly cache = new Map<string, number>()

  /** The accessor for a root and steps: an existing identical one, or a new one with the next id. */
  intern(root: number, steps: readonly IndexStep[]): number {
    const key = `${root}|${steps.map((s) => (s.kind === 'literal' ? `#${s.value}` : s.kind === 'ref' ? `@${s.accessor}` : `.${s.name}`)).join('|')}`
    const found = this.cache.get(key)
    if (found !== undefined) return found
    const id = this.defs.length
    this.defs.push({ id, root, steps })
    this.cache.set(key, id)
    return id
  }

  /** The accessor of a whole object. */
  whole(objectId: number): number {
    return this.intern(objectId, [])
  }
}

export interface PathContext {
  readonly tree: readonly Variable[]
  readonly layout: VariableLayout
  /** Folders whose entries a block can re-link at run time: paths through them keep their steps. */
  readonly live: ReadonlySet<string>
  readonly table: AccessorTable
}

export type PathResult =
  | { readonly ok: true; readonly accessor: number; readonly target: Target }
  | { readonly ok: false; readonly code: string; readonly message: string }

const fail = (code: string, message: string): PathResult => ({ ok: false, code, message })

interface Entry {
  readonly variable: Variable
  /** Where the child sits among the folder's elements on the board; null for an empty folder, which has no object. */
  readonly position: number | null
}

/** The children a step picks from, with the position each has on the board (an empty folder is not an element). */
const entriesOf = (ctx: PathContext, container: Variable): readonly Entry[] => {
  if (container.kind === 'folder') {
    const emitted = ctx.layout.entries.get(container.id) ?? []
    return container.children.map((variable) => {
      const position = emitted.indexOf(variable.id)
      return { variable, position: position < 0 ? null : position }
    })
  }
  if (container.kind === 'reference') {
    const target = container.target === null ? undefined : findVariable(ctx.tree, container.target)
    return target ? [{ variable: target, position: 0 }] : []
  }
  return []
}

/**
 * The accessor for `path`. `cell` says the pin wants the pointer cell itself (a `cell` pin: clone's destination), so the last
 * step is kept and the walk is not folded into the child.
 */
export const compilePath = (ctx: PathContext, path: ObjectPath, cell = false): PathResult => {
  const resolved = resolvePath(ctx.tree, path)
  if (!resolved.ok) return fail(resolved.code, resolved.message)

  const start = findVariable(ctx.tree, path.root)!
  let root = ctx.layout.ids.get(start.id)
  if (root === undefined) return fail('not-in-program', `'${start.name}' is not part of the program: an empty folder has no object on the board.`)

  const steps: IndexStep[] = []
  let current: Variable | null = start
  for (let i = 0; i < path.steps.length; i++) {
    const step = path.steps[i]!
    const last = i === path.steps.length - 1
    const container: Variable | null = current
    const entries: readonly Entry[] = container ? entriesOf(ctx, container) : []

    let picked: Entry | undefined
    if (container && (container.kind === 'folder' || container.kind === 'reference')) {
      if (step.kind === 'index') picked = entries[step.index]
      else if (step.kind === 'name') picked = entries.find((e) => e.variable.name === step.name)
      if (picked && picked.position === null) {
        return fail('not-in-program', `'${picked.variable.name}' is an empty folder: it has no object on the board.`)
      }
    }

    const live = container !== null && ctx.live.has(container.id)
    if (picked && !live && steps.length === 0 && !(cell && last)) {
      // fixed entry of a folder nobody re-links: the walk is the same every time, so the child becomes the root
      root = ctx.layout.ids.get(picked.variable.id)!
      current = picked.variable
      continue
    }

    if (step.kind === 'dynamic') {
      const inner = compilePath(ctx, step.path)
      if (!inner.ok) return inner
      steps.push({ kind: 'ref', accessor: inner.accessor })
    } else if (step.kind === 'index') {
      steps.push({ kind: 'literal', value: step.index })
    } else if (picked && !live && picked.position !== null) {
      steps.push({ kind: 'literal', value: picked.position })
    } else {
      // a name below a re-linkable entry or an unknown object: the board finds the child by its tag
      steps.push({ kind: 'name', name: step.name })
    }
    current = picked ? picked.variable : null
  }

  return { ok: true, accessor: ctx.table.intern(root, steps), target: resolved.target }
}

/**
 * The folder a `cell` path names an entry of: the one whose entries the block may re-link, so no path may be folded through
 * it. Undefined when the path does not end in a folder entry (the pin check reports that).
 */
export const cellContainer = (tree: readonly Variable[], path: ObjectPath): string | undefined => {
  let current = findVariable(tree, path.root)
  for (let i = 0; i < path.steps.length - 1 && current; i++) {
    const step = path.steps[i]!
    if (step.kind === 'dynamic') return undefined
    const kids = current.kind === 'folder' ? current.children : current.kind === 'reference' && current.target !== null ? [findVariable(tree, current.target)].flatMap((v) => (v ? [v] : [])) : []
    current = step.kind === 'index' ? kids[step.index] : kids.find((k) => k.name === step.name)
  }
  if (!current || path.steps.length === 0) return undefined
  return current.kind === 'folder' || current.kind === 'reference' ? current.id : undefined
}
