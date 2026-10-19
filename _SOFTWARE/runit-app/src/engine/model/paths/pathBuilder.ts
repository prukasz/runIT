import { childrenOf, formatPath, resolvePath, scanName } from './paths'
import type { ObjectPath, PathStep, Resolution } from './paths'
import { flattenVariables, locate } from '../variables/variables'
import type { Variable } from '../variables/variables'

/*
 * The accessor builder, without the interface: what an editor of a path needs to know while the user builds it. A path is
 * edited as a draft (the root and a chain of steps, a position read at run time may still be an empty slot), and the
 * functions here say what the draft names so far, what can follow, and what to offer for the root and for a slot. Text in,
 * text out: `parseDraft` reads what is typed (even half of it: `gains[`, `motor.`), `formatDraft` writes it back, and
 * `toPath` gives the `ObjectPath` the compiler takes once the draft is complete. The walk is `resolvePath`'s, the board's.
 */

export type DraftStep =
  | { readonly kind: 'index'; readonly index: number }
  | { readonly kind: 'name'; readonly name: string }
  /** A position read at run time from another path; null while the slot is empty. */
  | { readonly kind: 'dynamic'; readonly path: ObjectPath | null }

export interface PathDraft {
  /** Id of the top-level variable the path starts at; null while none is chosen. */
  readonly root: string | null
  readonly steps: readonly DraftStep[]
}

export const EMPTY_DRAFT: PathDraft = { root: null, steps: [] }

export interface DraftProblem {
  readonly message: string
  /** Character offset in the text. */
  readonly at: number
}

/* ---- text <-> draft ---- */

/** Reads path text as far as it makes sense. The draft is what was understood; `problem` is the first thing that was not. */
export const parseDraft = (tree: readonly Variable[], text: string): { readonly draft: PathDraft; readonly problem?: DraftProblem } => {
  let pos = 0
  let problem: DraftProblem | undefined
  const note = (message: string, at = pos) => {
    problem ??= { message, at }
  }
  const spaces = () => {
    while (text[pos] === ' ') pos++
  }

  /** A path (the root, then steps) or, when none could be read, null. */
  const readPath = (): PathDraft | null => {
    spaces()
    const rootAt = pos
    const scanned = scanName(text, pos)
    if (!scanned) {
      if (text.slice(pos).trim() !== '') note('A path starts with the name of a variable.')
      return null
    }
    pos = scanned.end
    const root = tree.find((v) => v.name === scanned.name)
    if (!root) {
      note(`There is no variable called '${scanned.name}'.`, rootAt)
      return null
    }
    const steps: DraftStep[] = []
    while (pos < text.length) {
      if (text[pos] === '.') {
        pos++
        const name = scanName(text, pos)
        if (!name) {
          note('A name is expected after the dot.')
          break
        }
        pos = name.end
        steps.push({ kind: 'name', name: name.name })
      } else if (text[pos] === '[' && text[pos + 1] === '"') {
        const name = scanName(text, pos)
        if (!name) {
          note('The quoted name is not closed.')
          break
        }
        pos = name.end
        steps.push({ kind: 'name', name: name.name })
      } else if (text[pos] === '[') {
        pos++
        spaces()
        const digits = /^\d+/.exec(text.slice(pos))
        if (digits) {
          pos += digits[0].length
          steps.push({ kind: 'index', index: Number(digits[0]) })
        } else if (pos >= text.length || text[pos] === ']') {
          steps.push({ kind: 'dynamic', path: null }) // an empty slot: typed, or still being typed
        } else {
          const inner = readPath()
          steps.push({ kind: 'dynamic', path: (inner && toPath(inner)) || null }) // a half-typed inner path leaves the slot empty
          if (!inner) break
        }
        spaces()
        if (text[pos] === ']') pos++
        else {
          if (pos < text.length) note("A ']' is expected.")
          break
        }
      } else {
        if (text[pos] !== ']') note(`Unexpected '${text[pos]}'.`) // a ']' closes the slot this path sits in
        break
      }
    }
    return { root: root.id, steps }
  }

  const draft = readPath()
  if (draft && pos < text.length && !problem) note(`Unexpected '${text[pos]}'.`)
  return problem ? { draft: draft ?? EMPTY_DRAFT, problem } : { draft: draft ?? EMPTY_DRAFT }
}

/** The draft as text with the variables' names; an empty slot is `[]`. */
export const formatDraft = (tree: readonly Variable[], draft: PathDraft): string => {
  if (draft.root === null) return ''
  const complete = formatPath(tree, { root: draft.root, steps: [] })
  let text = complete
  for (const step of draft.steps) {
    if (step.kind === 'dynamic' && step.path === null) text += '[]'
    else text += formatPath(tree, { root: draft.root, steps: [step as PathStep] }).slice(complete.length)
  }
  return text
}

/** The path the compiler takes; undefined while the draft has no root or an empty slot. */
export const toPath = (draft: PathDraft): ObjectPath | undefined => {
  if (draft.root === null) return undefined
  const steps: PathStep[] = []
  for (const step of draft.steps) {
    if (step.kind === 'dynamic') {
      if (step.path === null) return undefined
      steps.push({ kind: 'dynamic', path: step.path })
    } else steps.push(step)
  }
  return { root: draft.root, steps }
}

export const draftOf = (path: ObjectPath): PathDraft => ({ root: path.root, steps: path.steps })

/** The draft that names a variable anywhere in the tree: its top-level ancestor, then its names down (`motors.left`). */
export const draftForVariable = (tree: readonly Variable[], id: string): PathDraft | undefined => {
  const where = locate(tree, id)
  if (!where) return undefined
  const chain = [...where.ancestors, where.variable]
  return { root: chain[0]!.id, steps: chain.slice(1).map((v) => ({ kind: 'name', name: v.name })) }
}

/* ---- editing: each returns a new draft ---- */

export const appendStep = (draft: PathDraft, ...steps: DraftStep[]): PathDraft => ({ ...draft, steps: [...draft.steps, ...steps] })
export const replaceStep = (draft: PathDraft, at: number, step: DraftStep): PathDraft => ({ ...draft, steps: draft.steps.map((s, i) => (i === at ? step : s)) })
/** Removes the step and everything after it: what follows a step depends on it. */
export const cutSteps = (draft: PathDraft, from: number): PathDraft => ({ ...draft, steps: draft.steps.slice(0, from) })
/** A new root: the steps are kept only when the new root is the same one. */
export const setRoot = (draft: PathDraft, root: string): PathDraft => (draft.root === root ? draft : { root, steps: [] })

/* ---- what a draft names ---- */

/** The dimensions of an array: a value array `[n]`, a folder of equal value rows `[rows, cols]`, of such matrices `[planes, rows, cols]`. */
export const dimensionsOf = (tree: readonly Variable[], variable: Variable): readonly number[] | undefined => {
  const target = variable.kind === 'reference' ? childrenOf(tree, variable)[0] : variable
  if (!target) return undefined
  if (target.kind === 'value') return target.count > 1 && target.type !== 'str' ? [target.count] : undefined
  if (target.kind !== 'folder' || target.children.length === 0) return undefined
  const inner = target.children.map((child) => dimensionsOf(tree, child))
  const first = inner[0]
  if (!first || first.length > 2 || !inner.every((dims) => dims && dims.length === first.length && dims.every((size, i) => size === first[i]))) return undefined
  return [target.children.length, ...first]
}

/** How a variable reads in a list: `2D 3×4`, `12 items`, `folder · 5`, or nothing for a scalar. */
export const shapeText = (tree: readonly Variable[], variable: Variable): string => {
  const dims = dimensionsOf(tree, variable)
  if (dims && dims.length > 1) return `${dims.length}D ${dims.join('×')}`
  if (dims) return `${dims[0]} items`
  if (variable.kind === 'folder') return `folder · ${variable.children.length}`
  return ''
}

export interface DraftView {
  /** What the draft names, or why it does not lead anywhere. Undefined while there is no root. An empty slot stands for the first entry. */
  readonly resolution?: Resolution
  /** The steps that are empty slots. */
  readonly empty: readonly number[]
  readonly complete: boolean
}

/** What the draft names so far. */
export const inspectDraft = (tree: readonly Variable[], draft: PathDraft): DraftView => {
  const empty = draft.steps.flatMap((s, i) => (s.kind === 'dynamic' && s.path === null ? [i] : []))
  if (draft.root === null) return { empty, complete: false }
  const steps: PathStep[] = draft.steps.map((s) => (s.kind === 'dynamic' ? (s.path ? { kind: 'dynamic', path: s.path } : { kind: 'index', index: 0 }) : s))
  return { resolution: resolvePath(tree, { root: draft.root, steps }), empty, complete: empty.length === 0 }
}

/** A child of the folder (or the one a reference points to) the draft has reached. */
export interface ChildOption {
  readonly variable: Variable
  /** The step that picks it by name, and by position. */
  readonly byName: DraftStep
  readonly byIndex: DraftStep
}

export type NextSteps =
  /** Nothing can follow: the draft does not lead anywhere yet, or ends on an element. */
  | { readonly kind: 'none' }
  | { readonly kind: 'children'; readonly of: Variable; readonly children: readonly ChildOption[]; readonly dimensions?: readonly number[] }
  | { readonly kind: 'elements'; readonly of: Variable; readonly count: number }

/** What can follow the draft: the children of the folder it reached, or the positions of the array. Either can also be a slot. */
export const nextSteps = (tree: readonly Variable[], draft: PathDraft): NextSteps => {
  const view = inspectDraft(tree, draft)
  if (!view.resolution?.ok) return { kind: 'none' }
  const { variable, element, unchecked } = view.resolution.target
  if (element !== null || unchecked) return { kind: 'none' }
  if (variable.kind === 'value') return variable.count > 1 && variable.type !== 'str' ? { kind: 'elements', of: variable, count: variable.count } : { kind: 'none' }
  const dimensions = dimensionsOf(tree, variable)
  return {
    kind: 'children',
    of: variable,
    children: childrenOf(tree, variable).map((child, index) => ({ variable: child, byName: { kind: 'name', name: child.name }, byIndex: { kind: 'index', index } })),
    ...(dimensions ? { dimensions } : {}),
  }
}

/* ---- what to offer ---- */

export interface RootOption {
  readonly variable: Variable
  /** What to append to the name: one empty slot per dimension of an array of folders (`[][]`). */
  readonly suffix: string
  readonly shape: string
}

/** The variables a path can start at. */
export const rootOptions = (tree: readonly Variable[]): readonly RootOption[] =>
  tree.map((variable) => ({ variable, suffix: '[]'.repeat(dimensionsOf(tree, variable)?.length ?? 0), shape: shapeText(tree, variable) }))

export interface PositionOption {
  readonly variable: Variable
  /** The path to read the position from. */
  readonly path: ObjectPath
  readonly text: string
}

/**
 * The variables whose value can be a position: one number (any value variable but text, of one element), found anywhere in
 * the tree. `query` keeps those whose path contains it (case does not matter) but is not it.
 */
export const positionOptions = (tree: readonly Variable[], query = '', limit = 6): readonly PositionOption[] => {
  const q = query.trim().toLowerCase()
  const found: PositionOption[] = []
  for (const variable of flattenVariables(tree)) {
    if (variable.kind !== 'value' || variable.count !== 1 || variable.type === 'str') continue
    const draft = draftForVariable(tree, variable.id)
    const path = draft && toPath(draft)
    if (!path) continue
    const text = formatPath(tree, path)
    if (q !== '' && (!text.toLowerCase().includes(q) || text.toLowerCase() === q)) continue
    found.push({ variable, path, text })
    if (found.length >= limit) break
  }
  return found
}
