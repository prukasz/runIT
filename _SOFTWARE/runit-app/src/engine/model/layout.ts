import { encodeValue } from '../wire/codec'
import type { DataDef, ObjectDef } from '../wire/program'
import type { Scalar } from '../wire/types'
import type { FolderVariable, ValueVariable, Variable } from './variables'

/*
 * The user's variables as objects of the program: each gets an object id (depth-first, a folder before its children, in tree
 * order: ids 0..n-1), its type and flags, and its initial value. Block-owned objects (outputs, ENO) are added after these by
 * the compiler, so the user's objects keep the lowest ids.
 *
 *   value      -> an object of its type and count
 *   folder     -> a pointer object whose elements are the ids of its children
 *   reference  -> a pointer object of one element: the id of its target (none while unset)
 *
 * A folder with nothing in it cannot exist on the board (an object needs a payload), so it is left out, with the references
 * to it, and reported in `skipped`.
 */

export interface VariableLayout {
  readonly objects: readonly ObjectDef[]
  readonly data: readonly DataDef[]
  /** Variable id -> object id, for every variable that became an object. */
  readonly ids: ReadonlyMap<string, number>
  /** Folder id -> the ids of the children that became its elements, in order (an empty folder among them is not one). */
  readonly entries: ReadonlyMap<string, readonly string[]>
  /** Variables that did not become objects, and why. */
  readonly skipped: readonly { readonly id: string; readonly reason: string }[]
}

/** A folder is part of the program when it holds at least one variable that is. */
const included = (v: Variable): boolean => v.kind !== 'folder' || v.children.some(included)

const asList = (initial: NonNullable<ValueVariable['initial']>): readonly Scalar[] => (Array.isArray(initial) ? (initial as readonly Scalar[]) : [initial as Scalar])

/** The initial bytes of a value variable, or none when it keeps the zero the board starts with. */
const initialBytes = (v: ValueVariable): Uint8Array | undefined => {
  if (v.initial === undefined) return undefined
  if (v.type === 'str') {
    if (typeof v.initial !== 'string') return undefined
    return new TextEncoder().encode(v.initial)
  }
  if (typeof v.initial === 'string') return undefined
  return encodeValue(v.type, asList(v.initial))
}

export const layoutVariables = (tree: readonly Variable[]): VariableLayout => {
  const ids = new Map<string, number>()
  const skipped: { id: string; reason: string }[] = []
  const order: Variable[] = []

  const assign = (list: readonly Variable[]) => {
    for (const variable of list) {
      if (!included(variable)) {
        skipped.push({ id: variable.id, reason: 'an empty folder has no object on the board' })
        continue
      }
      ids.set(variable.id, order.length)
      order.push(variable)
      if (variable.kind === 'folder') assign(variable.children)
    }
  }
  assign(tree)

  const objects: ObjectDef[] = []
  const data: DataDef[] = []
  const entries = new Map<string, readonly string[]>()
  for (const variable of order) {
    const id = ids.get(variable.id)!
    if (variable.kind === 'value') {
      objects.push({
        id,
        type: variable.type,
        count: variable.count,
        flags: { mutable: variable.writable !== false, updResetable: true, retentive: variable.retained === true, usrProtected: variable.protected === true },
        name: variable.name,
      })
      const bytes = initialBytes(variable)
      if (bytes && bytes.length > 0) data.push({ id, start: 0, data: bytes })
    } else if (variable.kind === 'folder') {
      const kept = (variable as FolderVariable).children.filter(included)
      const children = kept.map((c) => ids.get(c.id)!)
      entries.set(variable.id, kept.map((c) => c.id))
      objects.push({ id, type: 'ptr', count: children.length, flags: { mutable: true, updResetable: true }, name: variable.name })
      data.push({ id, start: 0, data: encodeValue('ptr', children) })
    } else {
      objects.push({ id, type: 'ptr', count: 1, flags: { mutable: true, updResetable: true }, name: variable.name })
      const target = variable.target === null ? undefined : ids.get(variable.target)
      if (target !== undefined) data.push({ id, start: 0, data: encodeValue('ptr', [target]) })
      else if (variable.target !== null) skipped.push({ id: variable.id, reason: 'its target is not part of the program' })
    }
  }
  return { objects, data, ids, entries, skipped }
}
