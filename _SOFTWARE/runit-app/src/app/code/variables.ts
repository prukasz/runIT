import { OBJ_WIDTH, flattenVariables, locate, resolvePath } from '@engine'
import type { ObjectPath, Project, Scalar, ValueType, ValueVariable, Variable } from '@engine'

/*
 * The Code view's variables, pure: new ids and names, the initial value as text and back, a change of type, a variable's path
 * and what a folder or a reference may point at. The tree itself and its edits are the engine's (`model/variables`).
 */

/** What the user reads for an element type (the engine's name, `f32`, is the hover text). The firmware publishes no alias for them. */
export const TYPE_LABEL: Readonly<Record<ValueType, string>> = { f32: 'float', i32: 'int', u32: 'uint', u8: 'byte', bool: 'bool', str: 'text' }

/** The length a new text variable holds, in bytes. */
export const STR_LENGTH = 32

/** Elements of one value: its payload size is 16 bits (`ERR_VM_OBJ_BAD_SIZE`, `wire/load/check.ts`). */
export const maxCount = (type: ValueType): number => Math.floor(0xffff / OBJ_WIDTH[type].mem)

/** An id no variable of the tree has (`var1`, `var2`, ...). Ids are the project's and never shown. */
export const newVariableId = (tree: readonly Variable[]): string => {
  const ids = new Set(flattenVariables(tree).map((v) => v.id))
  for (let n = 1; ; n++) if (!ids.has(`var${n}`)) return `var${n}`
}

/** `prefix1`, `prefix2`, ...: the first one no sibling is called. */
export const freeName = (siblings: readonly Variable[], prefix: string): string => {
  const names = new Set(siblings.map((v) => v.name))
  for (let n = 1; ; n++) if (!names.has(`${prefix}${n}`)) return `${prefix}${n}`
}

export type NewKind = Variable['kind']

/** A new variable of `kind` among `siblings`: a single f32, an empty folder, a reference not set yet. */
export const newVariable = (kind: NewKind, tree: readonly Variable[], siblings: readonly Variable[]): Variable => {
  const id = newVariableId(tree)
  if (kind === 'folder') return { kind, id, name: freeName(siblings, 'folder'), children: [] }
  if (kind === 'reference') return { kind, id, name: freeName(siblings, 'ref'), target: null }
  return { kind, id, name: freeName(siblings, 'var'), type: 'f32', count: 1 }
}

/** The initial value as the user types it: the text itself, or the elements apart by commas. */
export const initialText = (variable: ValueVariable): string => {
  const initial = variable.initial
  if (initial === undefined) return ''
  if (typeof initial === 'string') return initial
  return (Array.isArray(initial) ? initial : [initial]).map(String).join(', ')
}

/** The range of an integer type (`vm_types.h`: u8, u32, i32). */
const INTEGER_RANGE: Partial<Record<ValueType, readonly [number, number]>> = {
  u8: [0, 0xff],
  u32: [0, 0xffffffff],
  i32: [-0x80000000, 0x7fffffff],
}

export type ParsedInitial = { readonly ok: true; readonly initial: ValueVariable['initial'] } | { readonly ok: false; readonly error: string }

/** Text back into an initial value of the variable's type; empty is no initial value (zero). */
export const parseInitial = (variable: ValueVariable, text: string): ParsedInitial => {
  if (variable.type === 'str') return { ok: true, initial: text === '' ? undefined : text }
  const tokens = text.split(/[\s,;]+/).filter(Boolean)
  if (tokens.length === 0) return { ok: true, initial: undefined }
  if (tokens.length > variable.count) return { ok: false, error: `${tokens.length} values for ${variable.count} elements.` }
  const values: Scalar[] = []
  for (const token of tokens) {
    if (variable.type === 'bool') {
      if (/^(true|1|on)$/i.test(token)) values.push(true)
      else if (/^(false|0|off)$/i.test(token)) values.push(false)
      else return { ok: false, error: `'${token}' is not true or false.` }
      continue
    }
    const number = Number(token)
    if (!Number.isFinite(number)) return { ok: false, error: `'${token}' is not a number.` }
    const range = INTEGER_RANGE[variable.type]
    if (range && (!Number.isInteger(number) || number < range[0] || number > range[1])) return { ok: false, error: `'${token}' is not a whole number from ${range[0]} to ${range[1]}.` }
    values.push(number)
  }
  return { ok: true, initial: values.length === 1 ? values[0] : values }
}

/** A new element type: the initial value is dropped (it was of the old type); text gets room for its bytes, a number one element. */
export const retype = (variable: ValueVariable, type: ValueType): Partial<ValueVariable> => ({
  type,
  initial: undefined,
  count: type === 'str' ? Math.max(variable.count, STR_LENGTH) : variable.type === 'str' ? 1 : variable.count,
})

/** The path a user reads (`angles.arm1`), the same as an accessor names it. */
export const pathOf = (tree: readonly Variable[], id: string): string | undefined => {
  const location = locate(tree, id)
  return location && [...location.ancestors, location.variable].map((v) => v.name).join('.')
}

/** The folders `id` can move into: every folder but itself and those inside it. */
export const folderChoices = (tree: readonly Variable[], id: string) => {
  const location = locate(tree, id)
  const inside = new Set(location ? flattenVariables([location.variable]).map((v) => v.id) : [])
  return flattenVariables(tree).filter((v) => v.kind === 'folder' && !inside.has(v.id)).map((v) => ({ value: v.id, label: pathOf(tree, v.id) ?? v.name }))
}

/** What the reference `id` can point at: any variable but itself and the folders that hold it (`checkVariables`: a cycle). */
export const targetChoices = (tree: readonly Variable[], id: string) => {
  const holders = new Set(locate(tree, id)?.ancestors.map((f) => f.id))
  return flattenVariables(tree).filter((v) => v.id !== id && !holders.has(v.id)).map((v) => ({ value: v.id, label: pathOf(tree, v.id) ?? v.name }))
}

/** The variables a path touches: its root, what it names, and the paths a run-time position reads. */
const pathVariables = (tree: readonly Variable[], path: ObjectPath): string[] => {
  const resolved = resolvePath(tree, path)
  return [
    path.root,
    ...(resolved.ok ? [resolved.target.variable.id] : []),
    ...path.steps.flatMap((step) => (step.kind === 'dynamic' ? pathVariables(tree, step.path) : [])),
  ]
}

/** How a block uses a variable: reads it on an input, writes it from an output, or runs when it holds (its enable, "Run when"). */
export interface BlockUse {
  readonly block: string
  readonly role: 'input' | 'output' | 'enable'
  readonly index: number
}

/** Every pin of every block that reads or writes `id` (or an element of it). */
export const blockUses = (project: Project, id: string): BlockUse[] => project.blocks.flatMap((block) => {
  const uses = (path: ObjectPath | undefined) => path !== undefined && pathVariables(project.variables, path).includes(id)
  return [
    ...block.inputs.flatMap((s, index) => (s?.kind === 'variable' && uses(s.path) ? [{ block: block.id, role: 'input' as const, index }] : [])),
    ...block.outputs.flatMap((t, index) => (uses(t?.path) ? [{ block: block.id, role: 'output' as const, index }] : [])),
    ...block.enables.flatMap((s, index) => (s.kind === 'variable' && uses(s.path) ? [{ block: block.id, role: 'enable' as const, index }] : [])),
  ]
})

/** The variable `id` is what an error's reference is about (`programRefs`: `{ kind: 'variable', variable }`). */
export const isVariableTarget = (target: unknown, id: string): boolean =>
  typeof target === 'object' && target !== null && (target as { kind?: unknown }).kind === 'variable' && (target as { variable?: unknown }).variable === id

/** Where a dragged variable lands: into a folder, or before / after a row. Null when it cannot (a folder into itself). */
export const dropPlace = (tree: readonly Variable[], dragged: string, target: string, mode: 'into' | 'before' | 'after'): { parent: string | null; index?: number } | null => {
  const from = locate(tree, dragged)
  const to = locate(tree, target)
  if (!from || !to || dragged === target) return null
  const parent = mode === 'into' ? target : to.parent?.id ?? null
  if (parent !== null && flattenVariables([from.variable]).some((v) => v.id === parent)) return null
  if (mode === 'into') return { parent }
  // The index is counted without the dragged one: moving down in the same folder shifts the rows after it up by one.
  const index = to.index + (mode === 'after' ? 1 : 0)
  const sameParent = (from.parent?.id ?? null) === parent
  return { parent, index: sameParent && from.index < index ? index - 1 : index }
}
