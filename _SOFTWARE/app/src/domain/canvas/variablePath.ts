import type { CanvasBlock, FolderNode, ObjectNode, ObjectPath, PathStep, ProjectDocument } from '../project'
import { walkObjects } from '../project'
import { blockPinAt, runitVmCatalog } from '../descriptors'

/*
 * Variable paths as the user reads and types them: names from the tree,
 * `.child`, `[2]`, `[selector]` (read live). The project stores IDs.
 */

const BLOCK_PIN = /^[^.[\]]+:(q\d+|eno|body)$/

/** Name path of an object: its folders' names and its own (`motor.gains`). */
export const nameOf = (project: ProjectDocument, id: string): string | undefined => {
  let found: string | undefined
  walkObjects(project.objects, (node: ObjectNode, ancestors: readonly FolderNode[]) => {
    if (found === undefined && node.id === id) found = [...ancestors.map((folder) => folder.name), node.name].join('.')
  })
  return found
}

/** A path as names: `motor.gains[2]`, `table[sel]`, `if1:q0`; an unknown ID stays as it is. */
export const pathLabel = (path: ObjectPath, project?: ProjectDocument, blocks?: readonly CanvasBlock[]): string => {
  const pin = /^(.+):(q\d+|eno|body)$/.exec(path.root)
  const block = pin && blocks?.find((entry) => entry.id === pin[1])
  const alias = block?.name || pin?.[1]
  const outputIndex = pin?.[2].startsWith('q') ? Number(pin[2].slice(1)) : -1
  const outputAlias = outputIndex >= 0 ? block?.outputAliases?.[outputIndex] : undefined
  const outputTitle = outputIndex >= 0 && block ? blockPinAt(runitVmCatalog().block(block.type)?.outputs ?? { min: 0, max: 0, pins: [] }, outputIndex)?.title : undefined
  const root = block && pin
    ? outputIndex >= 0 ? `${alias}.${outputAlias || outputTitle || pin[2]}` : `${alias}:${pin[2] === 'eno' ? 'ENO' : 'body'}`
    : (project && nameOf(project, path.root)) || path.root
  return root + (path.steps ?? []).map((step) => (step.kind === 'index' ? `[${step.index}]` : step.kind === 'name' ? `.${step.name}` : `[${pathLabel(step.index, project, blocks)}]`)).join('')
}

/** A message with every quoted object ID ('obj-…') replaced by the object's name path. */
export const nameIds = (message: string, project?: ProjectDocument): string =>
  project ? message.replace(/'([^'\s]+)'/g, (whole, id: string) => {
    const name = nameOf(project, id)
    return name && name !== id ? `'${name}'` : whole
  }) : message

/** A label short enough for a chip: whole when it fits, else the end that names it (`…temperature`, `…gains[2]`). */
export const chipLabel = (label: string, max = 16): string => {
  if (label.length <= max) return label
  const last = label.slice(label.lastIndexOf('.') + 1)
  return last.length + 1 <= max ? `…${last}` : `…${label.slice(-(max - 1))}`
}

export class PathTextError extends Error {}

/** Finds an object by its name path from the top (`motor.gains`), or by a unique name anywhere, or by its ID. */
const resolveName = (project: ProjectDocument, text: string): string | undefined => {
  const byPath: string[] = []
  const byName: string[] = []
  walkObjects(project.objects, (node: ObjectNode, ancestors: readonly FolderNode[]) => {
    if ([...ancestors.map((folder) => folder.name), node.name].join('.') === text) byPath.push(node.id)
    if (node.name === text) byName.push(node.id)
    if (node.id === text) byPath.push(node.id)
  })
  return byPath[0] ?? (byName.length === 1 ? byName[0] : undefined)
}

/**
 * Parses `motor.gains[2]`, `table[sel]`, `a.b.c`, `if1:q0` into a path. The
 * longest dotted prefix that names an object is the root; the rest become
 * name steps. Throws PathTextError with a readable reason.
 */
export const parsePathText = (text: string, project: ProjectDocument): ObjectPath => {
  let at = 0
  const source = text.trim()
  const parse = (): ObjectPath => {
    const start = at
    while (at < source.length && source[at] !== '[' && source[at] !== ']') at++
    const head = source.slice(start, at).trim()
    if (!head) throw new PathTextError(`A name is missing at position ${start + 1}.`)
    let root: string | undefined
    let rest: string[] = []
    if (BLOCK_PIN.test(head)) root = head
    else {
      const parts = head.split('.')
      for (let cut = parts.length; cut > 0 && !root; cut--) {
        root = resolveName(project, parts.slice(0, cut).join('.'))
        rest = parts.slice(cut)
      }
    }
    if (!root) throw new PathTextError(`No variable is called '${head.split('.')[0]}'.`)
    const steps: PathStep[] = rest.map((name) => {
      if (!name) throw new PathTextError(`An empty name after '.' in '${head}'.`)
      return { kind: 'name', name }
    })
    while (source[at] === '[') {
      at++
      const close = source.indexOf(']', at)
      if (close === -1) throw new PathTextError(`']' is missing.`)
      const inner = source.slice(at, close).trim()
      const strMatch = /^"([^"]*)"$|^'([^']*)'$/.exec(inner)
      if (/^\d+$/.test(inner)) {
        steps.push({ kind: 'index', index: Number(inner) })
        at = close + 1
      } else if (strMatch) {
        const keyName = strMatch[1] ?? strMatch[2]
        if (!keyName) throw new PathTextError('An empty key inside brackets [""].')
        steps.push({ kind: 'name', name: keyName })
        at = close + 1
      } else {
        steps.push({ kind: 'dynamic', index: parse() })
        if (source[at] !== ']') throw new PathTextError(`']' is missing after '${inner}'.`)
        at++
      }
      while (source[at] === '.') {
        at++
        const nameStart = at
        while (at < source.length && !'.[]'.includes(source[at]!)) at++
        const name = source.slice(nameStart, at).trim()
        if (!name) throw new PathTextError('An empty name after \'.\'.')
        steps.push({ kind: 'name', name })
      }
    }
    return steps.length ? { root, steps } : { root }
  }
  const path = parse()
  if (at < source.length) throw new PathTextError(`Unexpected '${source.slice(at)}'.`)
  return path
}
