import { OBJECT_DRAG_TYPE } from '../../domain/canvas'
import { findObject, walkObjects } from '../../domain/project'
import { arrayDims } from './accessorChain'
import type { ObjectNode, ObjectPath, ProjectDocument, ValueNode } from '../../domain/project'

export const is2DArrayFolder = (node: ObjectNode): boolean => {
  if (node.kind !== 'folder' || node.children.length === 0) return false
  const firstChild = node.children[0]
  if (firstChild.kind !== 'value') return false
  const cols = firstChild.length ?? 1
  // A 2D matrix requires each child to be a value row and each row to have multiple columns (cols > 1).
  // A 3x1 or 1x1 structure is 1D (array of objects or folder of scalars), so second accessor isn't available.
  if (cols <= 1) return false
  return node.children.every((child) => child.kind === 'value' && (child.length ?? 1) === cols)
}


export interface VariableCandidate {
  readonly id: string
  readonly name: string
  readonly fullPath: string
  readonly kind: 'value' | 'reference' | 'array2d'
  readonly type?: string
  readonly length?: number
  readonly rows?: number
  readonly cols?: number
  readonly is2D?: boolean
  readonly is1D?: boolean
}

export const getProjectCandidates = (project?: ProjectDocument): VariableCandidate[] => {
  if (!project) return []
  const list: VariableCandidate[] = []
  walkObjects(project.objects, (node, ancestors) => {
    // If it's a 2D array folder (like coefficient or arr2d)
    if (node.kind === 'folder' && is2DArrayFolder(node)) {
      const fullPath = [...ancestors.map((a) => a.name), node.name].join('.')
      const firstRow = node.children[0] as ValueNode
      list.push({
        id: node.id,
        name: node.name,
        fullPath,
        kind: 'array2d',
        type: firstRow.type,
        rows: node.children.length,
        cols: firstRow.length ?? 1,
        is2D: true,
      })
      return
    }

    // Do NOT list children of 2D array folders as separate items if they are unnamed rows
    if (ancestors.some((a) => is2DArrayFolder(a))) {
      return
    }

    // Do NOT include plain grouping folders in candidate variable values
    if (node.kind === 'folder') {
      return
    }

    const fullPath = [...ancestors.map((a) => a.name), node.name].join('.')
    if (node.kind === 'value') {
      const len = node.length ?? 1
      list.push({
        id: node.id,
        name: node.name,
        fullPath,
        kind: 'value',
        type: node.type,
        length: len,
        is1D: len > 1,
      })
    } else if (node.kind === 'reference') {
      const target = findObject(project, node.targetId)?.node
      if (target?.kind === 'value') {
        const len = target.length ?? 1
        list.push({
          id: node.id,
          name: node.name,
          fullPath,
          kind: 'reference',
          type: target.type,
          length: len,
          is1D: len > 1,
        })
      }
    }
  })
  return list
}

export const mapPinTypeToVmType = (pinType: string): string => {
  const norm = pinType.toLowerCase().trim()
  if (norm === 'i32' || norm === 'i16' || norm === 'i8' || norm === 'int') return 'I32'
  if (norm === 'u32' || norm === 'u16') return 'U32'
  if (norm === 'u8' || norm === 'byte') return 'U8'
  if (norm === 'bool' || norm === 'b' || norm === 'gate') return 'B'
  if (norm === 'str' || norm === 'string') return 'STR'
  if (norm === 'f' || norm === 'f32' || norm === 'float' || norm === 'number') return 'F'
  return 'F'
}

export const isValidVariableName = (name: string): boolean => {
  return /^[a-zA-Z_][a-zA-Z0-9_]*$/.test(name)
}

export const nextVarName = (siblings: readonly ObjectNode[], prefix: string): string => {
  const names = new Set(siblings.map((node) => node.name))
  for (let index = 1; ; index++) if (!names.has(`${prefix}${index}`)) return `${prefix}${index}`
}

export interface UniversalDropInfo {
  readonly id: string
  readonly name: string
  readonly fullPath: string
  readonly targetId: string
  readonly is2D: boolean
  readonly is1D: boolean
  readonly isFolder: boolean
  readonly pathText: string
  /** The path itself, when the object needs no position: what a wire stores, found by ID so a name never has to be read back. */
  readonly path?: ObjectPath
}

export const resolveUniversalNode = (
  objId: string,
  project: ProjectDocument
): UniversalDropInfo | undefined => {
  const found = findObject(project, objId)
  if (!found) {
    const candidate = getProjectCandidates(project).find(
      (c) => c.name.toLowerCase() === objId.toLowerCase() || c.fullPath.toLowerCase() === objId.toLowerCase()
    )
    if (candidate) {
      return {
        id: candidate.id,
        name: candidate.name,
        fullPath: candidate.fullPath,
        targetId: candidate.id,
        is2D: !!candidate.is2D,
        is1D: !!candidate.is1D,
        isFolder: false,
        pathText: candidate.is2D ? `${candidate.fullPath}[ ][ ]` : candidate.is1D ? `${candidate.fullPath}[ ]` : candidate.fullPath,
      }
    }
    return undefined
  }

  let node = found.node
  let targetId = node.id
  if (node.kind === 'reference') {
    const target = findObject(project, node.targetId)
    if (target) {
      node = target.node
      targetId = node.id
    }
  }

  const fullPath = [...found.ancestors.map((a) => a.name), found.node.name].join('.')

  if (node.kind === 'folder') {
    const dims = arrayDims(node, project)
    const is2D = !!dims && dims.length > 1
    return {
      id: found.node.id,
      name: found.node.name,
      fullPath,
      targetId,
      is2D,
      is1D: false,
      isFolder: true,
      pathText: dims ? `${fullPath}${'[ ]'.repeat(dims.length)}` : fullPath,
    }
  }

  if (node.kind === 'value') {
    const is1D = (node.length ?? 1) > 1
    return {
      id: found.node.id,
      name: found.node.name,
      fullPath,
      targetId,
      is2D: false,
      is1D,
      isFolder: false,
      pathText: is1D ? `${fullPath}[ ]` : fullPath,
      ...(is1D ? {} : { path: { root: found.node.id } }),
    }
  }

  return {
    id: found.node.id,
    name: found.node.name,
    fullPath,
    targetId,
    is2D: false,
    is1D: false,
    isFolder: false,
    pathText: fullPath,
  }
}

export const resolveUniversalDrop = (
  dataTransfer: DataTransfer,
  project?: ProjectDocument
): UniversalDropInfo | undefined => {
  if (!project) return undefined
  const objId = dataTransfer.getData(OBJECT_DRAG_TYPE) || dataTransfer.getData('text/plain')
  if (!objId) return undefined
  return resolveUniversalNode(objId, project)
}

/** Parses `base[idx1][idx2]` into `{ base, indices }`. */
export const parseIndexedPath = (text: string) => {
  const trimmed = text.trim()
  const match = /^([^[\s]+)(.*)$/.exec(trimmed)
  if (!match) return { base: trimmed, indices: [] as string[], hasBrackets: false }
  const base = match[1]
  const rest = match[2]
  const bracketMatches = [...rest.matchAll(/\[([^\]]*)\]/g)]
  const indices = bracketMatches.map((m) => m[1].trim())
  return { base, indices, hasBrackets: bracketMatches.length > 0 }
}

/** A single dynamic index slot (droppable, clickable from tree selection, searchable by name). */
