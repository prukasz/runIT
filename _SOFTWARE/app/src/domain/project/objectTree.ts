import type { FolderNode, ObjectNode, ProjectDocument, ReferenceNode, ValueNode } from './document'

/*
 * Edits of the object tree. Every function returns a new document and leaves
 * the old one untouched (undo keeps the old ones). A parent of null is the top
 * level.
 */

export class ObjectTreeError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'ObjectTreeError'
  }
}

export interface FoundObject {
  readonly node: ObjectNode
  /** null at the top level. */
  readonly parent: FolderNode | null
  readonly index: number
  /** Folders from the top down to the node's parent. */
  readonly ancestors: readonly FolderNode[]
}

/** A new stable object ID. */
export const newObjectId = (): string => `obj-${crypto.randomUUID()}`

/** Visit every node depth-first, a folder before its children. */
export const walkObjects = (nodes: readonly ObjectNode[], visit: (node: ObjectNode, ancestors: readonly FolderNode[]) => void, ancestors: readonly FolderNode[] = []): void => {
  for (const node of nodes) {
    visit(node, ancestors)
    if (node.kind === 'folder') walkObjects(node.children, visit, [...ancestors, node])
  }
}

export const findObject = (project: ProjectDocument, id: string): FoundObject | undefined => {
  const search = (nodes: readonly ObjectNode[], parent: FolderNode | null, ancestors: readonly FolderNode[]): FoundObject | undefined => {
    for (const [index, node] of nodes.entries()) {
      if (node.id === id) return { node, parent, index, ancestors }
      if (node.kind === 'folder') {
        const found = search(node.children, node, [...ancestors, node])
        if (found) return found
      }
    }
    return undefined
  }
  return search(project.objects, null, [])
}

/** Replace the child list of `parentId` (null = top level). */
const withChildren = (project: ProjectDocument, parentId: string | null, change: (children: readonly ObjectNode[]) => readonly ObjectNode[]): ProjectDocument => {
  if (parentId === null) return { ...project, objects: change(project.objects) }
  let hit = false
  const rewrite = (nodes: readonly ObjectNode[]): readonly ObjectNode[] =>
    nodes.map((node) => {
      if (node.kind !== 'folder') return node
      if (node.id === parentId) {
        hit = true
        return { ...node, children: change(node.children) }
      }
      const children = rewrite(node.children)
      return children === node.children ? node : { ...node, children }
    })
  const objects = rewrite(project.objects)
  if (!hit) throw new ObjectTreeError(`No folder '${parentId}'.`)
  return { ...project, objects }
}

const insertAt = <T>(list: readonly T[], item: T, index = list.length): readonly T[] => {
  if (index < 0 || index > list.length) throw new ObjectTreeError(`Position ${index} is outside 0..${list.length}.`)
  return [...list.slice(0, index), item, ...list.slice(index)]
}

const collectIds = (node: ObjectNode, into: Set<string>): Set<string> => {
  walkObjects([node], (entry) => into.add(entry.id))
  return into
}

/** Add a node (with its children) under `parentId`, at `index` or last. */
export const addObject = (project: ProjectDocument, parentId: string | null, node: ObjectNode, index?: number): ProjectDocument => {
  if (node.kind === 'reference' && parentId === null) throw new ObjectTreeError('References cannot exist at the top level.')
  const existing = new Set<string>()
  walkObjects(project.objects, (entry) => existing.add(entry.id))
  for (const id of collectIds(node, new Set())) if (existing.has(id)) throw new ObjectTreeError(`ID '${id}' is already in the project.`)
  return withChildren(project, parentId, (children) => insertAt(children, node, index))
}

export type ValuePatch = Partial<Omit<ValueNode, 'kind' | 'id'>>
export type FolderPatch = Partial<Pick<FolderNode, 'name' | 'description' | 'subscribed'>>
export type ReferencePatch = Partial<Pick<ReferenceNode, 'name' | 'targetId'>>

/** Change fields of one node; the kind and ID stay. */
export const updateObject = (project: ProjectDocument, id: string, patch: ValuePatch | FolderPatch | ReferencePatch): ProjectDocument => {
  const found = findObject(project, id)
  if (!found) throw new ObjectTreeError(`No object '${id}'.`)
  if (found.node.kind === 'folder' && Object.keys(patch).some((key) => key !== 'name' && key !== 'description' && key !== 'subscribed')) throw new ObjectTreeError(`'${found.node.name}' is a folder: only its name, description and subscription change.`)
  if (found.node.kind === 'reference' && Object.keys(patch).some((key) => key !== 'name' && key !== 'targetId')) throw new ObjectTreeError(`'${found.node.name}' is a reference: only its name and target change.`)
  const updated = { ...found.node, ...patch } as ObjectNode
  return withChildren(project, found.parent?.id ?? null, (children) => children.map((child) => (child.id === id ? updated : child)))
}

/** Remove a node and everything under it. */
export const removeObject = (project: ProjectDocument, id: string): ProjectDocument => {
  const found = findObject(project, id)
  if (!found) throw new ObjectTreeError(`No object '${id}'.`)
  const removedIds = collectIds(found.node, new Set())
  const detached = withChildren(project, found.parent?.id ?? null, (children) => children.filter((child) => child.id !== id))
  const prune = (nodes: readonly ObjectNode[]): readonly ObjectNode[] => nodes.flatMap((node) => {
    if (node.kind === 'reference' && removedIds.has(node.targetId)) return []
    return [node.kind === 'folder' ? { ...node, children: prune(node.children) } : node]
  })
  return { ...detached, objects: prune(detached.objects) }
}

/** Move a node under `parentId` at `index` (counted after the node left its old place), or last. */
export const moveObject = (project: ProjectDocument, id: string, parentId: string | null, index?: number): ProjectDocument => {
  const found = findObject(project, id)
  if (!found) throw new ObjectTreeError(`No object '${id}'.`)
  if (found.node.kind === 'reference' && parentId === null) throw new ObjectTreeError('References cannot exist at the top level.')
  if (parentId !== null && collectIds(found.node, new Set()).has(parentId)) throw new ObjectTreeError(`'${found.node.name}' can't move into itself.`)
  const removed = withChildren(project, found.parent?.id ?? null, (children) => children.filter((child) => child.id !== id))
  return withChildren(removed, parentId, (children) => insertAt(children, found.node, index))
}

/** Replace all children of a folder with a new list of nodes. IDs new to the folder must be new to the project. */
export const setFolderChildren = (project: ProjectDocument, folderId: string, children: readonly ObjectNode[]): ProjectDocument => {
  const folder = findObject(project, folderId)
  if (!folder || folder.node.kind !== 'folder') throw new ObjectTreeError(`No folder '${folderId}'.`)
  const own = collectIds(folder.node, new Set())
  const elsewhere = new Set<string>()
  walkObjects(project.objects, (entry) => { if (!own.has(entry.id)) elsewhere.add(entry.id) })
  const incoming = new Set<string>()
  for (const child of children) {
    walkObjects([child], (entry) => {
      if (elsewhere.has(entry.id) || incoming.has(entry.id)) throw new ObjectTreeError(`ID '${entry.id}' is already in the project.`)
      incoming.add(entry.id)
    })
  }
  return withChildren(project, folderId, () => children)
}
