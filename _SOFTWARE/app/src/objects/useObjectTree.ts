import { useMemo, useState } from 'react'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { useUndoHistory } from '../hooks/useUndoHistory'
import type { VmObjectType } from '../domain/objects/catalog'
import { checkObjects } from '../domain/objects/diagnostics'
import { addObject, createProject, findObject, moveObject, newObjectId, parseProject, removeObject, serializeProject, setFolderChildren } from '../domain/project'
import type { FolderNode, ObjectNode, ObjectValue, ProjectDocument, ValueNode } from '../domain/project'

const STORAGE_KEY = 'runit.project'
const loadProject = (): ProjectDocument => readStored<ProjectDocument | undefined>(STORAGE_KEY, parseProject, undefined) ?? createProject('Untitled')

const nextName = (siblings: readonly ObjectNode[], prefix: string): string => {
  const names = new Set(siblings.map((node) => node.name))
  for (let index = 1; ; index++) if (!names.has(`${prefix}${index}`)) return `${prefix}${index}`
}

export const valueText = (value: ObjectValue | undefined): string =>
  value === undefined ? '' : typeof value === 'string' ? value : value.join(' ')

export const parseValue = (type: VmObjectType | undefined, text: string): ObjectValue | undefined => {
  if (type?.key === 'STR') return text || undefined
  const tokens = text.split(/[\s,;]+/).filter(Boolean)
  if (!tokens.length) return undefined
  if (type?.key === 'B') return tokens.map((token) => {
    if (/^(true|1|on)$/i.test(token)) return true
    if (/^(false|0|off)$/i.test(token)) return false
    throw new Error(`'${token}' must be true or false.`)
  })
  return tokens.map((token) => {
    const number = Number(token)
    if (!Number.isFinite(number)) throw new Error(`'${token}' must be a number.`)
    return number
  })
}

export const DEFAULT_STR_LENGTH = 32

export const inferValue = (text: string): { type: string; value: ObjectValue | undefined; count: number } => {
  if (!text.trim()) return { type: 'F', value: undefined, count: 0 }
  const tokens = text.split(/[\s,;]+/).filter(Boolean)
  if (tokens.every((token) => /^(true|false)$/i.test(token))) return { type: 'B', value: tokens.map((token) => token.toLowerCase() === 'true'), count: tokens.length }
  if (tokens.every((token) => token !== '' && Number.isFinite(Number(token)))) return { type: 'F', value: tokens.map(Number), count: tokens.length }
  const byteCount = new TextEncoder().encode(text).length
  return { type: 'STR', value: text, count: Math.max(DEFAULT_STR_LENGTH, byteCount) }
}

export const isViableLinkTarget = (project: ProjectDocument, folderId: string, node: ObjectNode): boolean => {
  if (node.kind === 'reference') return false
  if (node.id === folderId) return false
  const dest = findObject(project, folderId)
  if (!dest || dest.node.kind !== 'folder') return false

  // Only top-level objects can be linked
  const candidate = findObject(project, node.id)
  if (!candidate || candidate.parent !== null) return false

  // Cannot link if already an owned child in the folder
  if (dest.node.children.some((c) => c.id === node.id)) return false

  // Cannot link if destination folder already has a reference targeting this node
  if (dest.node.children.some((c) => c.kind === 'reference' && c.targetId === node.id)) return false

  // Destination folder cannot already have an object with the same target name
  const existingNames = new Set(
    dest.node.children.map((c) => {
      if (c.kind === 'reference') {
        return findObject(project, c.targetId)?.node.name ?? c.name
      }
      return c.name
    }).filter(Boolean)
  )
  if (existingNames.has(node.name)) return false

  // If candidate is a folder, check for cycles
  if (node.kind === 'folder') {
    if (dest.ancestors.some((a) => a.id === node.id)) return false
    const hasCycle = (f: FolderNode): boolean => {
      if (f.id === folderId) return true
      for (const child of f.children) {
        if (child.kind === 'folder' && hasCycle(child)) return true
        if (child.kind === 'reference') {
          const target = findObject(project, child.targetId)?.node
          if (target?.kind === 'folder' && hasCycle(target)) return true
        }
      }
      return false
    }
    if (hasCycle(node)) return false
  }

  return true
}

export function useObjectTree(onSelect?: () => void) {
  const history = useUndoHistory(loadProject, { coalesceMs: 800 })
  const [historyVersion, setHistoryVersion] = useState(0)
  const project = history.present
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [error, setError] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const [linkingParentId, setLinkingParentId] = useState<string | null | undefined>()
  const selected = selectedId ? findObject(project, selectedId) : undefined
  const diagnostics = useMemo(() => checkObjects(project), [project])

  usePersistEffect(STORAGE_KEY, project, serializeProject)

  /** Apply a document change; false (and the reason in `error`) when it was refused. */
  const edit = (change: (current: ProjectDocument) => ProjectDocument, options?: { key?: string; discrete?: boolean }): boolean => {
    let next: ProjectDocument
    try {
      next = change(history.current())
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
      return false
    }
    setError('')
    history.record(next, options)
    return true
  }

  const undo = () => {
    if (!history.undo()) return
    setHistoryVersion((v) => v + 1)
    setError('')
  }

  const redo = () => {
    if (!history.redo()) return
    setHistoryVersion((v) => v + 1)
    setError('')
  }


  const select = (id?: string | null) => {
    if (!id) {
      setSelectedId(null)
      return
    }
    const found = findObject(project, id)
    if (!found) return
    setSelectedId(id)
    onSelect?.()
    setCollapsed((current) => new Set([...current].filter((entry) => !found.ancestors.some((folder) => folder.id === entry))))
  }

  const add = (kind: 'folder' | 'value', parentId: string | null = null) => {
    const parent = parentId ? findObject(project, parentId)?.node : undefined
    if (parentId && parent?.kind !== 'folder') return
    const siblings = parent?.kind === 'folder' ? parent.children : project.objects
    const id = newObjectId()
    const node: ObjectNode = kind === 'folder'
      ? { kind, id, name: nextName(siblings, 'folder'), children: [] }
      : { kind, id, name: nextName(siblings, 'var'), type: 'F', typeMode: 'auto', length: 1, mutable: true, retentive: false }
    edit((current) => addObject(current, parentId, node))
    setSelectedId(id)
    onSelect?.()
    if (parentId) setCollapsed((current) => new Set([...current].filter((entry) => entry !== parentId)))
  }

  const addArray2D = (parentId: string, rows = 3, cols = 3) => {
    const parent = findObject(project, parentId)?.node
    if (parent?.kind !== 'folder') return
    const siblings = parent.children
    const folderId = newObjectId()
    const folderName = nextName(siblings, 'arr2d_')
    const children: ObjectNode[] = []
    for (let r = 0; r < rows; r++) {
      children.push({
        kind: 'value',
        id: newObjectId(),
        name: '',
        type: 'F',
        typeMode: 'auto',
        length: cols,
        mutable: true,
        retentive: false,
      })
    }
    const arrayFolder: ObjectNode = {
      kind: 'folder',
      id: folderId,
      name: folderName,
      children,
    }
    edit((current) => addObject(current, parentId, arrayFolder))
    setSelectedId(folderId)
    onSelect?.()
    setCollapsed((current) => new Set([...current].filter((entry) => entry !== parentId)))
  }

  const addArray3D = (parentId: string, depth = 2, rows = 2, cols = 2) => {
    const parent = findObject(project, parentId)?.node
    if (parent?.kind !== 'folder') return
    const siblings = parent.children
    const folderId = newObjectId()
    const folderName = nextName(siblings, 'arr3d_')
    const planes: ObjectNode[] = []
    for (let d = 0; d < depth; d++) {
      const planeChildren: ObjectNode[] = []
      for (let r = 0; r < rows; r++) {
        planeChildren.push({
          kind: 'value',
          id: newObjectId(),
          name: '',
          type: 'F',
          typeMode: 'auto',
          length: cols,
          mutable: true,
          retentive: false,
        })
      }
      planes.push({
        kind: 'folder',
        id: newObjectId(),
        name: '',
        children: planeChildren,
      })
    }
    const arrayFolder: ObjectNode = {
      kind: 'folder',
      id: folderId,
      name: folderName,
      children: planes,
    }
    edit((current) => addObject(current, parentId, arrayFolder))
    setSelectedId(folderId)
    onSelect?.()
    setCollapsed((current) => new Set([...current].filter((entry) => entry !== parentId)))
  }

  const resizeArray2D = (folderId: string, rows: number, cols: number, newType?: string) => {
    edit((current) => {
      const found = findObject(current, folderId)
      if (!found || found.node.kind !== 'folder') return current
      const folder = found.node
      const currentRows = folder.children.filter((c): c is ValueNode => c.kind === 'value')
      const targetType = newType ?? currentRows[0]?.type ?? 'F'
      const targetTypeMode = currentRows[0]?.typeMode ?? 'auto'
      const newChildren: ObjectNode[] = []

      for (let r = 0; r < rows; r++) {
        if (r < currentRows.length) {
          newChildren.push({
            ...currentRows[r],
            length: Math.max(1, cols),
            type: targetType,
            typeMode: targetTypeMode,
          })
        } else {
          newChildren.push({
            kind: 'value',
            id: newObjectId(),
            name: '',
            type: targetType,
            typeMode: targetTypeMode,
            length: Math.max(1, cols),
            mutable: true,
            retentive: false,
          })
        }
      }
      return setFolderChildren(current, folderId, newChildren)
    })
  }

  const resizeArray3D = (folderId: string, depth: number, rows: number, cols: number, newType?: string) => {
    edit((current) => {
      const found = findObject(current, folderId)
      if (!found || found.node.kind !== 'folder') return current
      const folder = found.node
      const currentPlanes = folder.children.filter((c): c is FolderNode => c.kind === 'folder')
      const sampleRow = currentPlanes[0]?.children.find((c): c is ValueNode => c.kind === 'value')
      const targetType = newType ?? sampleRow?.type ?? 'F'
      const targetTypeMode = sampleRow?.typeMode ?? 'auto'
      const newPlanes: ObjectNode[] = []

      for (let d = 0; d < depth; d++) {
        const existingPlane = currentPlanes[d]
        const existingRows = existingPlane?.children.filter((c): c is ValueNode => c.kind === 'value') ?? []
        const planeChildren: ObjectNode[] = []

        for (let r = 0; r < rows; r++) {
          if (r < existingRows.length) {
            planeChildren.push({
              ...existingRows[r],
              length: Math.max(1, cols),
              type: targetType,
              typeMode: targetTypeMode,
            })
          } else {
            planeChildren.push({
              kind: 'value',
              id: newObjectId(),
              name: '',
              type: targetType,
              typeMode: targetTypeMode,
              length: Math.max(1, cols),
              mutable: true,
              retentive: false,
            })
          }
        }

        newPlanes.push({
          kind: 'folder',
          id: existingPlane?.id ?? newObjectId(),
          name: '',
          children: planeChildren,
        })
      }
      return setFolderChildren(current, folderId, newPlanes)
    })
  }

  const remove = (id: string) => {
    edit((current) => removeObject(current, id))
    setSelectedId(null)
  }

  const beginLink = () => {
    if (linkingParentId !== undefined) {
      setLinkingParentId(undefined)
      return
    }
    if (selected?.node.kind !== 'folder') return
    setLinkingParentId(selected.node.id)
    setError('')
  }

  const linkTo = (targetId: string) => {
    if (!linkingParentId) return
    const found = findObject(project, targetId)
    if (!found || !isViableLinkTarget(project, linkingParentId, found.node)) return

    const id = newObjectId()
    const reference: ObjectNode = { kind: 'reference', id, name: found.node.name, targetId }
    const linked = edit((current) => {
      const next = addObject(current, linkingParentId, reference)
      const problem = checkObjects(next).find((entry) => entry.objectId === id && entry.severity === 'error')
      if (problem) throw new Error(problem.message)
      return next
    })
    if (!linked) return
    setSelectedId(id)
    onSelect?.()
    setLinkingParentId(undefined)
  }

  const moveTo = (id: string, newParentId: string | null, newIndex?: number) => {
    const found = findObject(project, id)
    if (!found) return
    if (found.node.kind === 'reference' && newParentId === null) return
    if (newParentId !== null && (id === newParentId || findObject(project, newParentId)?.ancestors.some((a) => a.id === id))) {
      return
    }
    let adjustedIndex = newIndex
    if (newIndex !== undefined && found.parent?.id === newParentId) {
      if (found.index < newIndex) adjustedIndex = newIndex - 1
    }
    edit((current) => moveObject(current, id, newParentId, adjustedIndex))
  }

  const move = (id: string, step: -1 | 1) => {
    const found = findObject(project, id)
    if (found) edit((current) => moveObject(current, id, found.parent?.id ?? null, found.index + step))
  }

  /** Replace the document (a project opened); undo goes back to the one before. */
  const load = (loaded: ProjectDocument) => {
    history.replace(loaded)
    setHistoryVersion((v) => v + 1)
    setSelectedId(null)
    setCollapsed(new Set())
    setLinkingParentId(undefined)
    setError('')
  }

  return {
    project,
    canUndo: history.canUndo,
    canRedo: history.canRedo,
    undo,
    redo,
    historyVersion,
    selected,
    selectedId,
    setSelectedId,
    select,
    error,
    diagnostics,
    collapsed,
    setCollapsed,
    linkingParentId,
    setLinkingParentId,
    beginLink,
    linkTo,
    edit,
    add,
    addArray2D,
    addArray3D,
    resizeArray2D,
    resizeArray3D,
    remove,
    move,
    moveTo,
    load,
  }
}

export const matchesSearch = (name: string, query: string): boolean => {
  const q = query.trim()
  if (!q) return false
  const qNorm = q.replace(/\s+/g, '').toLowerCase()
  if (!qNorm) return false
  const nNorm = name.replace(/\s+/g, '').toLowerCase()
  if (nNorm.includes(qNorm)) return true
  const qLoose = q.replace(/[\s_]+/g, '').toLowerCase()
  const nLoose = name.replace(/[\s_]+/g, '').toLowerCase()
  if (qLoose && nLoose.includes(qLoose)) return true
  return false
}

export type ObjectWorkspace = ReturnType<typeof useObjectTree>

