import { NAME_MAX } from '../../wire/format/types'
import type { Scalar } from '../../wire/format/types'

/*
 * The user's variables: the tree the program reads and writes. A variable is an object on the board: a value (one element
 * or an array of one type), a folder (a pointer object whose elements are its children, which are objects in their own
 * right) or a reference (a pointer to another variable). Everything here is plain, serializable, immutable data; ids are
 * the project's and never change, names are what the user reads and what accessors can address by name.
 */

/** The element types of a value variable (everything but the pointer type). */
export const VALUE_TYPES = ['bool', 'u8', 'u32', 'i32', 'f32', 'str'] as const
export type ValueType = (typeof VALUE_TYPES)[number]

interface VariableBase {
  /** Stable id inside the project; unique across the whole tree. */
  readonly id: string
  /** What the user calls it and what a path can name it by: 1..NAME_MAX bytes of UTF-8, unique among its siblings. */
  readonly name: string
  readonly description?: string
  /** The app watches it live (a SUBSCRIBE of its object while the program runs). Not part of the program: the compiler ignores it. */
  readonly subscribed?: boolean
}

export interface ValueVariable extends VariableBase {
  readonly kind: 'value'
  readonly type: ValueType
  /** Elements: 1 is a scalar; for `str`, the length in bytes. */
  readonly count: number
  /** The value at load: one element, a list of up to `count` elements, or for `str` the text. Unset elements are zero. */
  readonly initial?: Scalar | readonly Scalar[] | string
  /** The board keeps the value across restarts (named objects only; not a folder). */
  readonly retained?: boolean
  /** A user's run-time writes are refused (blocks still write it). */
  readonly protected?: boolean
  /** False: not writable at run time at all (a constant). Default true. */
  readonly writable?: boolean
}

export interface FolderVariable extends VariableBase {
  readonly kind: 'folder'
  readonly children: readonly Variable[]
  /** What it holds is known only at run time (a block re-links its entries, a parsed message): a path below it is checked on the board. */
  readonly dynamic?: boolean
}

export interface ReferenceVariable extends VariableBase {
  readonly kind: 'reference'
  /** The id of the variable it points to, or null while unset. */
  readonly target: string | null
  /** What it points to is built at run time (a clone of a message): a path below it is checked on the board, the target may stay unset. */
  readonly dynamic?: boolean
}

export type Variable = ValueVariable | FolderVariable | ReferenceVariable

/** Where a variable sits: its parent folder (undefined at the top) and its position among its siblings. */
export interface Location {
  readonly variable: Variable
  readonly parent?: FolderVariable
  readonly index: number
  /** The folders from the top down to the parent. */
  readonly ancestors: readonly FolderVariable[]
}

const utf8Length = (text: string): number => new TextEncoder().encode(text).length

/** Every variable, parents before their children, in tree order. */
export const flattenVariables = (tree: readonly Variable[]): readonly Variable[] => {
  const out: Variable[] = []
  const walk = (list: readonly Variable[]) => {
    for (const variable of list) {
      out.push(variable)
      if (variable.kind === 'folder') walk(variable.children)
    }
  }
  walk(tree)
  return out
}

export const locate = (tree: readonly Variable[], id: string): Location | undefined => {
  const walk = (list: readonly Variable[], ancestors: readonly FolderVariable[]): Location | undefined => {
    for (let index = 0; index < list.length; index++) {
      const variable = list[index]!
      if (variable.id === id) {
        const parent = ancestors[ancestors.length - 1]
        return { variable, ...(parent ? { parent } : {}), index, ancestors }
      }
      if (variable.kind === 'folder') {
        const found = walk(variable.children, [...ancestors, variable])
        if (found) return found
      }
    }
    return undefined
  }
  return walk(tree, [])
}

export const findVariable = (tree: readonly Variable[], id: string): Variable | undefined => locate(tree, id)?.variable

/* ---- editing: every function returns a new tree ---- */

const mapTree = (list: readonly Variable[], fn: (variable: Variable) => Variable | null): Variable[] =>
  list.flatMap((variable) => {
    const mapped = fn(variable)
    if (mapped === null) return []
    return [mapped.kind === 'folder' ? { ...mapped, children: mapTree(mapped.children, fn) } : mapped]
  })

/** Adds a variable at the top (`parentId` null) or into a folder, at `index` (end by default). */
export const addVariable = (tree: readonly Variable[], parentId: string | null, variable: Variable, index?: number): Variable[] => {
  const insert = (list: readonly Variable[]): Variable[] => {
    const at = index === undefined ? list.length : Math.max(0, Math.min(index, list.length))
    return [...list.slice(0, at), variable, ...list.slice(at)]
  }
  if (parentId === null) return insert(tree)
  const parent = findVariable(tree, parentId)
  if (!parent || parent.kind !== 'folder') throw new Error(`Variable '${parentId}' is not a folder.`)
  return mapTree(tree, (v) => (v.id === parentId && v.kind === 'folder' ? { ...v, children: insert(v.children) } : v))
}

/** Removes a variable and what is inside it; references that pointed at anything removed are set to null. */
export const removeVariable = (tree: readonly Variable[], id: string): Variable[] => {
  const location = locate(tree, id)
  if (!location) return [...tree]
  const gone = new Set(flattenVariables([location.variable]).map((v) => v.id))
  return mapTree(tree, (v) => {
    if (gone.has(v.id)) return null
    if (v.kind === 'reference' && v.target !== null && gone.has(v.target)) return { ...v, target: null }
    return v
  })
}

/** Changes fields of one variable (not its kind or id). */
export const updateVariable = (
  tree: readonly Variable[],
  id: string,
  patch: Partial<Omit<ValueVariable, 'kind' | 'id'>> | Partial<Omit<ReferenceVariable, 'kind' | 'id'>> | Partial<Pick<FolderVariable, 'name' | 'description' | 'subscribed' | 'dynamic'>>,
): Variable[] => mapTree(tree, (v) => (v.id === id ? ({ ...v, ...patch } as Variable) : v))

export const renameVariable = (tree: readonly Variable[], id: string, name: string): Variable[] => updateVariable(tree, id, { name })

/** Moves a variable into another folder (or the top) at `index`. A folder cannot move into itself or into what it holds. */
export const moveVariable = (tree: readonly Variable[], id: string, parentId: string | null, index?: number): Variable[] => {
  const location = locate(tree, id)
  if (!location) throw new Error(`No variable '${id}'.`)
  if (parentId !== null && flattenVariables([location.variable]).some((v) => v.id === parentId)) {
    throw new Error(`The folder '${location.variable.name}' cannot move into itself.`)
  }
  const without = mapTree(tree, (v) => (v.id === id ? null : v))
  return addVariable(without, parentId, location.variable, index)
}

/* ---- checks ---- */

export interface VariableProblem {
  readonly code: string
  readonly id: string
  readonly message: string
}

/** A name a path can address: not empty, at most NAME_MAX bytes of UTF-8, no control characters. */
export const nameProblem = (name: string): string | undefined => {
  if (name.length === 0) return 'The name is empty.'
  const bytes = utf8Length(name)
  if (bytes > NAME_MAX) return `The name is ${bytes} bytes; the limit is ${NAME_MAX}.`
  for (const ch of name) {
    const code = ch.codePointAt(0)!
    if (code < 0x20 || code === 0x7f) return 'The name has a control character.'
  }
  return undefined
}

/**
 * What would stop the tree becoming a program: bad or repeated names (siblings must differ, a path names a child by it),
 * repeated ids, initial values that do not fit, references that point nowhere or into themselves.
 */
export const checkVariables = (tree: readonly Variable[]): VariableProblem[] => {
  const problems: VariableProblem[] = []
  const add = (code: string, id: string, message: string) => problems.push({ code, id, message })
  const seen = new Set<string>()
  const all = flattenVariables(tree)
  const byId = new Map(all.map((v) => [v.id, v]))

  const siblings = (list: readonly Variable[]) => {
    const names = new Set<string>()
    for (const variable of list) {
      if (names.has(variable.name)) add('duplicate-name', variable.id, `Another variable here is also called '${variable.name}'.`)
      names.add(variable.name)
      if (variable.kind === 'folder') siblings(variable.children)
    }
  }
  siblings(tree)

  for (const variable of all) {
    if (seen.has(variable.id)) add('duplicate-id', variable.id, `The id '${variable.id}' is used twice.`)
    seen.add(variable.id)
    const bad = nameProblem(variable.name)
    if (bad) add('bad-name', variable.id, bad)

    if (variable.kind === 'value') {
      if (!Number.isInteger(variable.count) || variable.count < 1) add('bad-count', variable.id, 'A value needs at least one element.')
      else if (variable.type === 'str' && typeof variable.initial === 'string' && utf8Length(variable.initial) > variable.count) {
        add('initial-too-long', variable.id, `The text is longer than the ${variable.count} bytes the variable holds.`)
      } else if (Array.isArray(variable.initial) && variable.initial.length > variable.count) {
        add('initial-too-long', variable.id, `${variable.initial.length} initial values for ${variable.count} elements.`)
      }
      if (variable.retained && variable.writable === false) add('retained-constant', variable.id, 'A retained value must be writable.')
    } else if (variable.kind === 'reference' && variable.target !== null) {
      const target = byId.get(variable.target)
      if (!target) add('dangling-reference', variable.id, `The reference points to '${variable.target}', which does not exist.`)
      else if (target.id === variable.id) add('reference-cycle', variable.id, 'A reference cannot point to itself.')
      else if (target.kind === 'folder' && flattenVariables([target]).some((v) => v.id === variable.id)) {
        add('reference-cycle', variable.id, `The reference points to the folder '${target.name}' that holds it.`)
      }
    }
  }
  return problems
}
