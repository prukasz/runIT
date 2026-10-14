import type { ObjType } from '../wire/types'
import { findVariable } from './variables'
import type { Variable } from './variables'

/*
 * Accessors, as the user sees them: a path that starts at a variable and walks into it. On the board an accessor is a root
 * object plus index steps (`vm_accessor_t`); this is the same walk over the project's variables, so the editor can say
 * what a path names, in which type, before anything is uploaded. How firmware walks: obj/vm_obj_access_internal.c
 * (`vm_internal_acc_resolve_deep`): a step picks an element of the current object; a pointer element is followed to its child
 * if more steps remain, and a path that ends on a pointer element names the child object.
 *
 *   path      = root step*
 *   step      = [n]  |  .name  |  ["name"]  |  [path]
 *               a fixed position, a child by name, a child by name that needs quotes, a position read from another path
 *
 * `root` is a variable id (a rename must not break a path); text uses names.
 */

export type PathStep =
  | { readonly kind: 'index'; readonly index: number }
  | { readonly kind: 'name'; readonly name: string }
  /** The position is the current value of another path (`VM_IDX_REF`). */
  | { readonly kind: 'dynamic'; readonly path: ObjectPath }

export interface ObjectPath {
  /** Id of the variable the path starts at. */
  readonly root: string
  readonly steps: readonly PathStep[]
}

/** What a path names. */
export interface Target {
  /** The variable whose payload it is: for one element of an array, the array's variable. */
  readonly variable: Variable
  /** The element of a value array it names, or null for the whole object. */
  readonly element: number | null
  /** The type of what it names; null when it depends on a run-time position and the candidates differ. */
  readonly type: ObjType | null
  /** Elements the target holds: 1 for an element, the array length, the child count of a folder. */
  readonly count: number
  /** A position on the way is read at run time, so the exact object is not known before. */
  readonly dynamic: boolean
}

export type Resolution =
  | { readonly ok: true; readonly target: Target }
  | { readonly ok: false; readonly code: string; readonly message: string; /** The step that failed (0-based), or -1 for the root. */ readonly at: number }

const fail = (code: string, message: string, at: number): Resolution => ({ ok: false, code, message, at })

const typeOf = (v: Variable): ObjType => (v.kind === 'value' ? v.type : 'ptr')

const countOf = (v: Variable): number => (v.kind === 'value' ? v.count : v.kind === 'folder' ? v.children.length : 1)

/** The children a step can pick from: a folder's own, or a reference's one (the variable it points to, as the board's pointer cell holds it). */
const childrenOf = (tree: readonly Variable[], v: Variable): readonly Variable[] => {
  if (v.kind === 'folder') return v.children
  if (v.kind === 'reference') {
    const target = v.target === null ? undefined : findVariable(tree, v.target)
    return target ? [target] : []
  }
  return []
}

/** The type every candidate shares, or null when they differ (or there is none). */
const commonType = (candidates: readonly Variable[]): ObjType | null => {
  const first = candidates[0]
  if (!first) return null
  return candidates.every((c) => typeOf(c) === typeOf(first) && countOf(c) === countOf(first)) ? typeOf(first) : null
}

/**
 * What `path` names in `tree`: walks the steps like the board does. A folder (or a reference, which holds one child) is
 * entered by position or name; a value array ends the path with one of its elements. A position read at run time keeps the
 * walk going over every entry it could pick, and the target's type is what they have in common.
 */
export const resolvePath = (tree: readonly Variable[], path: ObjectPath): Resolution => {
  const root = findVariable(tree, path.root)
  if (!root) return fail('ERR_VM_ACCESSOR_UNKNOWN_ID', `The path starts at '${path.root}', which is not a variable.`, -1)

  /** Where the walk may be: one variable, or every entry a run-time position could pick. */
  let at: readonly Variable[] = [root]
  let dynamic = false

  for (let i = 0; i < path.steps.length; i++) {
    const step = path.steps[i]!
    const last = i === path.steps.length - 1
    const label = at.length === 1 ? `'${at[0]!.name}'` : 'the entries'

    if (step.kind === 'dynamic') {
      const inner = resolvePath(tree, step.path)
      if (!inner.ok) return fail(inner.code, `Step ${i + 1}: ${inner.message}`, i)
      if (inner.target.type === 'ptr') return fail('ERR_VM_ACCESSOR_TYPE_MISMATCH', `Step ${i + 1} reads its position from '${inner.target.variable.name}', which is not a number.`, i)
      dynamic = true
    }

    // Value arrays: the path ends here with one of their elements.
    if (at.every((v) => v.kind === 'value')) {
      const arrays = at as readonly Extract<Variable, { kind: 'value' }>[]
      if (!last) return fail('ERR_VM_OBJ_NOT_PTR', `${label} is not a folder: nothing can be read inside it.`, i)
      if (step.kind === 'name') return fail('ERR_VM_OBJ_NOT_PTR', `${label} has no named children.`, i)
      if (step.kind === 'index') {
        const shortest = Math.min(...arrays.map((a) => a.count))
        if (step.index < 0 || step.index >= shortest) return fail('ERR_VM_ACCESSOR_OOB', `Position ${step.index} is outside ${label} (${shortest} elements).`, i)
      }
      const type = commonType(arrays)
      return { ok: true, target: { variable: arrays[0]!, element: step.kind === 'index' ? step.index : null, type: type === null ? null : arrays[0]!.type, count: 1, dynamic } }
    }

    const next: Variable[] = []
    for (const candidate of at) {
      const entries = childrenOf(tree, candidate)
      if (step.kind === 'index') {
        const entry = entries[step.index]
        if (entry) next.push(entry)
        else if (at.length === 1) return fail('ERR_VM_ACCESSOR_OOB', `Position ${step.index} is outside ${label} (${entries.length} entries).`, i)
      } else if (step.kind === 'name') {
        const entry = entries.find((e) => e.name === step.name)
        if (entry) next.push(entry)
        else if (at.length === 1) return fail('ERR_VM_ACCESSOR_NAME_NOT_FOUND', `${label} has nothing called '${step.name}'.`, i)
      } else {
        next.push(...entries)
      }
    }
    if (next.length === 0) return fail(step.kind === 'name' ? 'ERR_VM_ACCESSOR_NAME_NOT_FOUND' : 'ERR_VM_ACCESSOR_OOB', `Nothing to pick in ${label}.`, i)
    at = [...new Set(next)]
  }

  const first = at[0]!
  return { ok: true, target: { variable: first, element: null, type: at.length === 1 ? typeOf(first) : commonType(at), count: countOf(first), dynamic: dynamic || at.length > 1 } }
}

/* ---- text ---- */

/** A name that can follow a dot: letters, digits, `_` and `-`, not starting with a digit or `-`. */
const plain = (name: string): boolean => /^[\p{L}_][\p{L}\p{N}_-]*$/u.test(name)

const quote = (name: string): string => `["${name.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"]`

/** The path as text, with the variables' names: `motors.left[2]`, `motors["front left"]`, `table[position]`. */
export const formatPath = (tree: readonly Variable[], path: ObjectPath): string => {
  const rootName = findVariable(tree, path.root)?.name ?? path.root
  let text = plain(rootName) ? rootName : quote(rootName)
  for (const step of path.steps) {
    if (step.kind === 'index') text += `[${step.index}]`
    else if (step.kind === 'name') text += plain(step.name) ? `.${step.name}` : quote(step.name)
    else text += `[${formatPath(tree, step.path)}]`
  }
  return text
}

export type ParsedPath = { readonly ok: true; readonly path: ObjectPath } | { readonly ok: false; readonly message: string; /** Character offset of the problem. */ readonly at: number }

/** Parses the text form (see the grammar above) against the top-level variables of `tree`. */
export const parsePath = (tree: readonly Variable[], text: string): ParsedPath => {
  let pos = 0
  const error = (message: string, at = pos): ParsedPath => ({ ok: false, message, at })

  const readName = (): string | undefined => {
    const start = pos
    if (text[pos] === '[' && text[pos + 1] === '"') {
      pos += 2
      let out = ''
      while (pos < text.length && text[pos] !== '"') {
        if (text[pos] === '\\' && pos + 1 < text.length) pos++
        out += text[pos]
        pos++
      }
      if (text[pos] !== '"' || text[pos + 1] !== ']') {
        pos = start
        return undefined
      }
      pos += 2
      return out
    }
    const match = /^[\p{L}_][\p{L}\p{N}_-]*/u.exec(text.slice(pos))
    if (!match) return undefined
    pos += match[0].length
    return match[0]
  }

  const readPath = (): ParsedPath => {
    const rootStart = pos
    const rootName = readName()
    if (rootName === undefined) return error('A path starts with the name of a variable.')
    const root = tree.find((v) => v.name === rootName)
    if (!root) return error(`There is no variable called '${rootName}'.`, rootStart)
    const steps: PathStep[] = []
    while (pos < text.length) {
      if (text[pos] === '.') {
        pos++
        const name = readName()
        if (name === undefined) return error('A name is expected after the dot.')
        steps.push({ kind: 'name', name })
      } else if (text[pos] === '[' && text[pos + 1] === '"') {
        const name = readName()
        if (name === undefined) return error('The quoted name is not closed.')
        steps.push({ kind: 'name', name })
      } else if (text[pos] === '[') {
        pos++
        const digits = /^\d+/.exec(text.slice(pos))
        if (digits) {
          pos += digits[0].length
          steps.push({ kind: 'index', index: Number(digits[0]) })
        } else {
          const inner = readPath()
          if (!inner.ok) return inner
          steps.push({ kind: 'dynamic', path: inner.path })
        }
        if (text[pos] !== ']') return error("A ']' is expected.")
        pos++
      } else {
        break
      }
    }
    return { ok: true, path: { root: root.id, steps } }
  }

  const result = readPath()
  if (!result.ok) return result
  return pos < text.length ? error(`Unexpected '${text[pos]}'.`) : result
}
