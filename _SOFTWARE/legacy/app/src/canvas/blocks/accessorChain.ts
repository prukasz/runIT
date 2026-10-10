import { findObject, walkObjects } from '../../domain/project'
import type { ObjectNode, ProjectDocument } from '../../domain/project'

/*
 * A pin's variable as an editable chain: the root object and the steps after it.
 * `motor.gains["kp"][2].value`  =  root `motor.gains`, key "kp", index 2, member `value`.
 * A `[text]` step is a number or a variable read live for its position.
 */

export type ChainStep =
  | { readonly kind: 'member'; readonly name: string }
  | { readonly kind: 'key'; readonly name: string }
  /** A position: a number (`3`), or a variable read live (`sel`, `motor.speed`, `b[1]`); empty until filled. */
  | { readonly kind: 'index'; readonly text: string }

export interface Chain {
  readonly root: string
  readonly steps: readonly ChainStep[]
}

const objectsByPath = (project: ProjectDocument): { byPath: Map<string, ObjectNode>; byName: Map<string, ObjectNode[]> } => {
  const byPath = new Map<string, ObjectNode>()
  const byName = new Map<string, ObjectNode[]>()
  walkObjects(project.objects, (node, ancestors) => {
    // Names are read trimmed: a stray space in a name must not hide the object.
    byPath.set([...ancestors.map((folder) => folder.name.trim()), node.name.trim()].join('.'), node)
    byPath.set(node.id, node)
    byName.set(node.name.trim(), [...(byName.get(node.name.trim()) ?? []), node])
  })
  return { byPath, byName }
}

/** The object a name path (`motor.gains`), an ID, or a unique name stands for. */
export const findByText = (project: ProjectDocument, text: string): ObjectNode | undefined => {
  const { byPath, byName } = objectsByPath(project)
  const named = byName.get(text)
  return byPath.get(text) ?? (named?.length === 1 ? named[0] : undefined)
}

const stepOf = (inner: string): ChainStep => {
  const text = inner.trim()
  const key = /^"([^"]*)"$|^'([^']*)'$/.exec(text)
  if (key) return { kind: 'key', name: key[1] ?? key[2] ?? '' }
  return { kind: 'index', text }
}

/** Splits accessor text into its root and steps; tolerant of text still being typed (`gains[`, `motor.`). */
export const parseChain = (text: string, project?: ProjectDocument): Chain => {
  const source = text.trim()
  const bracket = source.indexOf('[')
  const head = (bracket === -1 ? source : source.slice(0, bracket)).trim()
  let root = head
  const steps: ChainStep[] = []
  if (project && head.includes('.') && !head.includes(':')) {
    const { byPath, byName } = objectsByPath(project)
    const parts = head.split('.')
    for (let cut = parts.length; cut > 0; cut--) {
      const candidate = parts.slice(0, cut).join('.')
      if (byPath.has(candidate) || byName.get(candidate)?.length === 1) {
        root = candidate
        for (const name of parts.slice(cut)) if (name.trim()) steps.push({ kind: 'member', name: name.trim() })
        break
      }
    }
  }
  let at = bracket === -1 ? source.length : bracket
  while (at < source.length) {
    if (source[at] === '[') {
      let depth = 1
      let quote = ''
      let close = at + 1
      for (; close < source.length; close++) {
        const c = source[close]!
        if (quote) { if (c === quote) quote = '' }
        else if (c === '"' || c === "'") quote = c
        else if (c === '[') depth++
        else if (c === ']' && --depth === 0) break
      }
      steps.push(stepOf(source.slice(at + 1, close)))
      at = close + 1
    } else if (source[at] === '.') {
      const start = ++at
      while (at < source.length && source[at] !== '.' && source[at] !== '[') at++
      const name = source.slice(start, at).trim()
      if (name) steps.push({ kind: 'member', name })
    } else at++
  }
  return { root, steps }
}

export const formatStep = (step: ChainStep): string =>
  step.kind === 'member' ? `.${step.name}` : step.kind === 'key' ? `["${step.name}"]` : `[${step.text || ' '}]`

export const formatChain = (chain: Chain): string => chain.root + chain.steps.map(formatStep).join('')

const deref = (project: ProjectDocument, node: ObjectNode): ObjectNode =>
  node.kind === 'reference' ? findObject(project, node.targetId)?.node ?? node : node

/** Sizes of an array's dimensions: a value array `[n]`, a folder of equal value rows `[rows, cols]`, a folder of such matrices `[planes, rows, cols]`. */
export const arrayDims = (node: ObjectNode | undefined, project?: ProjectDocument): number[] | undefined => {
  if (!node) return undefined
  const target = project ? deref(project, node) : node
  if (target.kind === 'value') return target.length > 1 && target.type !== 'STR' ? [target.length] : undefined
  if (target.kind !== 'folder' || target.children.length === 0) return undefined
  const inner = target.children.map((child) => arrayDims(child, project))
  const first = inner[0]
  if (!first || first.length > 2 || !inner.every((dims) => dims && dims.length === first.length && dims.every((size, i) => size === first[i]))) return undefined
  return [target.children.length, ...first]
}

export interface ChainResolution {
  readonly ok: boolean
  /** Why the chain does not lead to an object. */
  readonly reason?: string
  /** What was missing: a `.member` of a static folder is a mistake, a `["key"]` may exist only at run time. */
  readonly missing?: 'member' | 'key' | 'position'
  /** The object the chain reaches (a folder, or the value whose element is addressed). */
  readonly node?: ObjectNode
  /** An element of a value array is addressed already. */
  readonly indexed: boolean
}

/** Follows the steps through the tree (a slot stands for any child: the first one). */
export const resolveChain = (project: ProjectDocument, chain: Chain): ChainResolution => {
  const found = findByText(project, chain.root)
  if (!found) return { ok: false, reason: `No variable is called '${chain.root}'.`, indexed: false }
  let node = deref(project, found)
  let indexed = false
  for (const step of chain.steps) {
    if (node.kind === 'folder') {
      const literal = step.kind === 'index' && /^\d+$/.test(step.text) ? Number(step.text) : undefined
      const child = step.kind !== 'index' ? node.children.find((entry) => entry.name === step.name) : literal !== undefined ? node.children[literal] : node.children[0]
      if (!child) {
        if (step.kind === 'member') return { ok: false, node, missing: 'member', reason: `'${step.name}' is not in '${node.name}'. If it only exists at run time, write ["${step.name}"].`, indexed }
        if (step.kind === 'key') return { ok: false, node, missing: 'key', reason: `"${step.name}" is not in '${node.name}' yet: fine for objects filled at run time.`, indexed }
        return { ok: false, node, missing: 'position', reason: `'${node.name}' has no position ${step.text}.`, indexed }
      }
      node = deref(project, child)
    } else if (node.kind === 'value' && !indexed && step.kind === 'index') {
      if (/^\d+$/.test(step.text) && Number(step.text) >= node.length) return { ok: false, node, reason: `'${node.name}' has ${node.length} elements.`, indexed }
      indexed = true
    } else return { ok: false, node, reason: `'${node.name}' has nothing more to select.`, indexed }
  }
  return { ok: true, node, indexed }
}

/** How a candidate root reads in the list: `2D 3×4`, `3D 2×3×4`, `folder · 5`, `12 items`. */
export const shapeText = (node: ObjectNode, project?: ProjectDocument): string => {
  const dims = arrayDims(node, project)
  if (dims && dims.length > 1) return `${dims.length}D ${dims.join('×')}`
  if (dims) return `${dims[0]} items`
  return node.kind === 'folder' ? `folder · ${node.children.length}` : ''
}

export interface RootCandidate {
  readonly id: string
  readonly name: string
  readonly fullPath: string
  readonly node: ObjectNode
  /** What to append after the name: one empty slot per dimension. */
  readonly suffix: string
}

/** Everything a pin can start from: values, arrays of any depth, folders. */
export const rootCandidates = (project?: ProjectDocument): RootCandidate[] => {
  if (!project) return []
  const list: RootCandidate[] = []
  walkObjects(project.objects, (node, ancestors) => {
    const target = deref(project, node)
    const dims = arrayDims(target, project)
    list.push({ id: node.id, name: node.name, fullPath: [...ancestors.map((folder) => folder.name), node.name].join('.'), node: target, suffix: dims ? '[ ]'.repeat(dims.length) : '' })
  })
  return list
}
