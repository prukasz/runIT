import { useEffect, useMemo, useState } from 'react'
import { readStored, usePersistEffect } from './hooks/useStorage'
import { PanelHeader } from './components/PanelHeader'
import { Badge } from './components/Badge'
import { SelectField, TextField } from './components/FormField'
import { PaletteSearch } from './components/PaletteSearch'
import { ArrowDown, ArrowUp, Boxes, ChevronDown, ChevronRight, Eye, Folder, FolderPlus, Grid2X2, Link2, Plus, Trash2 } from 'lucide-react'
import { TreeSlab } from './components/TreeSlab'
import { TypeBadge, ValueKindBadge } from './components/TypeBadge/TypeBadge'
import { useUndoHistory } from './hooks/useUndoHistory'
import { OBJECT_DRAG_TYPE, objectKindDragType, variableKind } from './domain/canvas'
import { compileObjects } from './domain/compiler/objects'
import { runitVmCatalog } from './domain/descriptors'
import type { VmObjectType } from './domain/descriptors'
import { addObject, createProject, findObject, moveObject, newObjectId, parseProject, removeObject, serializeProject, setFolderChildren, updateObject, walkObjects } from './domain/project'
import type { FolderNode, ObjectNode, ObjectSection, ObjectValue, ProjectDocument, ValueNode } from './domain/project'

const STORAGE_KEY = 'runit.project'
const catalog = runitVmCatalog()
const valueTypes = catalog.types.filter((type) => type.key !== 'PTR')
/** Most elements an object of this type holds (payload_size range / memory width, from the VM descriptors). */
const maxElementsOf = (key: string): number => {
  const type = catalog.type(key)
  return type ? catalog.maxElements(type) : catalog.payloadMax
}

const loadProject = (): ProjectDocument => readStored<ProjectDocument | undefined>(STORAGE_KEY, parseProject, undefined) ?? createProject('Untitled')

const nextName = (siblings: readonly ObjectNode[], prefix: string): string => {
  const names = new Set(siblings.map((node) => node.name))
  for (let index = 1; ; index++) if (!names.has(`${prefix}${index}`)) return `${prefix}${index}`
}

const valueText = (value: ObjectValue | undefined): string =>
  value === undefined ? '' : typeof value === 'string' ? value : value.join(' ')

const parseValue = (type: VmObjectType | undefined, text: string): ObjectValue | undefined => {
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

const DEFAULT_STR_LENGTH = 32

const inferValue = (text: string): { type: string; value: ObjectValue | undefined; count: number } => {
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

export function useObjectTreeWorkspace(onSelect?: () => void) {
  const history = useUndoHistory(loadProject, { coalesceMs: 800 })
  const [historyVersion, setHistoryVersion] = useState(0)
  // Block-owned objects: generated from blocks later; for now only what a recovery brings (not saved).
  const [sections, setSections] = useState<ObjectSection[]>([])

  const project = history.present
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [error, setError] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const [linkingParentId, setLinkingParentId] = useState<string | null | undefined>()
  const selected = selectedId ? findObject(project, selectedId) : undefined
  const diagnostics = useMemo(() => compileObjects(project, catalog, sections).diagnostics, [project, sections])

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
      const problem = compileObjects(next, catalog, sections).diagnostics.find((entry) => entry.objectId === id && entry.severity === 'error')
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

  /**
   * Replace the document (a project opened or recovered from a board); undo
   * goes back to the one before. Settings, devices and actions live in their own workspaces: the
   * document here holds objects, autostart and extra frames. `sections`
   * replaces the block-owned object sections when given.
   */
  const load = (loaded: ProjectDocument, nextSections?: readonly ObjectSection[]) => {
    const { settings: _settings, devices: _devices, actions: _actions, setup: _setup, ...document } = loaded
    history.replace(document)
    setHistoryVersion((v) => v + 1)
    if (nextSections) setSections([...nextSections])
    setSelectedId(null)
    setCollapsed(new Set())
    setLinkingParentId(undefined)
    setError('')
  }

  const setAutostart = (autostart: boolean) => edit((current) => ({ ...current, autostart }), { discrete: true })

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
    setAutostart,
    sections,
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

export type ObjectWorkspace = ReturnType<typeof useObjectTreeWorkspace>

function ObjectDesignator({ node }: { node: ObjectNode }) {
  if (node.kind === 'value') return <ValueKindBadge type={node.type} />
  const category = node.kind === 'reference' ? 'reference' : 'folder'
  const title = node.kind === 'reference' ? 'Reference' : 'Folder (owns its children)'
  return <span className={`object-type-icon ${category}`} title={title} aria-hidden="true">
    {category === 'reference' ? <Link2 /> : <Folder />}
  </span>
}

export function ObjectTreePalette({ workspace: w }: { workspace: ObjectWorkspace }) {
  const [searchQuery, setSearchQuery] = useState('')
  const [draggedId, setDraggedId] = useState<string | null>(null)
  const [dropTarget, setDropTarget] = useState<{ id: string; mode: 'into' | 'before' | 'after' } | null>(null)
  const [isRootDrop, setIsRootDrop] = useState(false)

  const isNodeMatch = (node: ObjectNode): boolean => {
    if (!matchesSearch(node.name, searchQuery)) {
      if (node.kind === 'reference') {
        const target = findObject(w.project, node.targetId)
        return target ? matchesSearch(target.node.name, searchQuery) : false
      }
      return false
    }
    return true
  }

  const hasMatchingDescendant = (node: ObjectNode): boolean => {
    if (!searchQuery.trim() || node.kind !== 'folder') return false
    return node.children.some((child) => isNodeMatch(child) || hasMatchingDescendant(child))
  }

  useEffect(() => {
    if (!w.linkingParentId) return
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        w.setLinkingParentId(undefined)
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [w.linkingParentId, w.setLinkingParentId])

  const render = (nodes: readonly ObjectNode[], depth: number) => nodes.map((node) => {
    const isLinkTarget = typeof w.linkingParentId === 'string' && isViableLinkTarget(w.project, w.linkingParentId, node)
    const isLinkDisabled = typeof w.linkingParentId === 'string' && !isLinkTarget
    const isDragging = draggedId === node.id
    const isDropInto = dropTarget?.id === node.id && dropTarget.mode === 'into'
    const isDropBefore = dropTarget?.id === node.id && dropTarget.mode === 'before'
    const isDropAfter = dropTarget?.id === node.id && dropTarget.mode === 'after'
    const isMatch = isNodeMatch(node)

    const isFolderCollapsed = w.collapsed.has(node.id) && !hasMatchingDescendant(node)

    return (
      <div key={node.id}>
        <TreeSlab
          isFolder={node.kind === 'folder'}
          isReference={node.kind === 'reference'}
          selected={w.selectedId === node.id}
          isMatch={isMatch}
          isLinkTarget={isLinkTarget}
          isLinkDisabled={isLinkDisabled}
          isDragging={isDragging}
          isDropInto={isDropInto}
          isDropBefore={isDropBefore}
          isDropAfter={isDropAfter}
          draggable={w.linkingParentId === undefined}
          onDragStart={(event) => {
            event.stopPropagation()
            event.dataTransfer.setData('text/plain', node.id)
            // Also a variable for the canvas: dropped on a block pin it becomes that pin's source.
            event.dataTransfer.setData(OBJECT_DRAG_TYPE, node.id)
            event.dataTransfer.setData(objectKindDragType(variableKind(node.kind === 'reference' ? findObject(w.project, node.targetId)?.node : node)), '')
            event.dataTransfer.effectAllowed = 'all'
            setDraggedId(node.id)
          }}
          onDragEnd={() => {
            setDraggedId(null)
            setDropTarget(null)
            setIsRootDrop(false)
          }}
          onDragOver={(event) => {
            if (!draggedId || draggedId === node.id) return
            if (findObject(w.project, node.id)?.ancestors.some((a) => a.id === draggedId)) return
            const dragged = findObject(w.project, draggedId)
            const isReference = dragged?.node.kind === 'reference'
            event.preventDefault()
            event.stopPropagation()
            event.dataTransfer.dropEffect = 'move'
            const rect = event.currentTarget.getBoundingClientRect()
            const y = event.clientY - rect.top
            let mode: 'into' | 'before' | 'after'
            if (node.kind === 'folder') {
              if (isReference && depth === 0) {
                mode = 'into'
              } else if (y < rect.height * 0.25) {
                mode = 'before'
              } else if (y > rect.height * 0.75) {
                mode = 'after'
              } else {
                mode = 'into'
              }
            } else {
              if (isReference && depth === 0) return
              mode = y < rect.height * 0.5 ? 'before' : 'after'
            }
            setDropTarget((prev) => (prev?.id === node.id && prev?.mode === mode ? prev : { id: node.id, mode }))
            setIsRootDrop(false)
          }}
          onDragLeave={(event) => {
            if (event.currentTarget.contains(event.relatedTarget as Node)) return
            if (dropTarget?.id === node.id) setDropTarget(null)
          }}
          onDrop={(event) => {
            event.preventDefault()
            event.stopPropagation()
            if (!draggedId || draggedId === node.id) {
              setDraggedId(null)
              setDropTarget(null)
              return
            }
            const target = findObject(w.project, node.id)
            if (!target) return
            const dragged = findObject(w.project, draggedId)
            const currentMode = dropTarget?.id === node.id ? dropTarget.mode : 'into'
            if (currentMode === 'into' && node.kind === 'folder') {
              w.moveTo(draggedId, node.id)
              w.setCollapsed((current) => new Set([...current].filter((entry) => entry !== node.id)))
            } else if (currentMode === 'before') {
              if (dragged?.node.kind !== 'reference' || target.parent?.id) {
                w.moveTo(draggedId, target.parent?.id ?? null, target.index)
              }
            } else if (currentMode === 'after') {
              if (dragged?.node.kind !== 'reference' || target.parent?.id) {
                w.moveTo(draggedId, target.parent?.id ?? null, target.index + 1)
              }
            }
            setDraggedId(null)
            setDropTarget(null)
            setIsRootDrop(false)
          }}
          onClick={() => {
            if (w.linkingParentId !== undefined) {
              if (isLinkTarget) w.linkTo(node.id)
            } else {
              w.select(w.selectedId === node.id ? null : node.id)
            }
          }}
          onDoubleClick={() => {
            if (node.kind === 'folder') {
              w.setCollapsed((current) => {
                const next = new Set(current)
                if (next.has(node.id)) next.delete(node.id)
                else next.add(node.id)
                return next
              })
            }
          }}
          icon={<ObjectDesignator node={node} />}
          label={node.kind === 'reference' ? findObject(w.project, node.targetId)?.node.name ?? node.name : node.name}
          badges={
            node.kind === 'value' ? (
              <TypeBadge type={node.type} count={node.length > 1 ? node.length : undefined} />
            ) : node.kind === 'folder' && node.children.length > 0 ? (
              <Badge tone="count" title={`${node.children.length} items`}>
                {node.children.length}
              </Badge>
            ) : null
          }
          disclosure={
            node.kind === 'folder' ? (
              <button
                type="button"
                className="tree-slab-disclosure"
                title={isFolderCollapsed ? 'Expand folder' : 'Collapse folder'}
                aria-label={`${isFolderCollapsed ? 'Expand' : 'Collapse'} ${node.name}`}
                onClick={(e) => {
                  e.stopPropagation()
                  w.setCollapsed((current) => {
                    const next = new Set(current)
                    if (next.has(node.id)) next.delete(node.id)
                    else next.add(node.id)
                    return next
                  })
                }}
              >
                {isFolderCollapsed ? <ChevronRight /> : <ChevronDown />}
              </button>
            ) : null
          }
          actions={
            <button
              type="button"
              className="tree-slab-action"
              title={`Delete ${node.name}`}
              aria-label={`Delete ${node.name}`}
              onClick={(e) => {
                e.stopPropagation()
                w.remove(node.id)
              }}
            >
              <Trash2 />
            </button>
          }
        />
        {node.kind === 'folder' && (!w.collapsed.has(node.id) || hasMatchingDescendant(node)) && (
          <div className={`object-tree-children depth-${Math.min(depth + 1, 4)}`}>{render(node.children, depth + 1)}</div>
        )}
      </div>
    )
  })

  return (
    <div className="object-tree-palette">
      <PaletteSearch value={searchQuery} onChange={setSearchQuery} placeholder="Search variables..." label="Search variables" />
      <div className="object-tree-heading">
        <span />
        <button title="Add value" aria-label="Add value" onClick={() => w.add('value', w.selected?.node.kind === 'folder' ? w.selected.node.id : w.selected?.parent?.id ?? null)}><Plus /></button>
        <button title="Add folder" aria-label="Add folder" onClick={() => w.add('folder', w.selected?.node.kind === 'folder' ? w.selected.node.id : w.selected?.parent?.id ?? null)}><FolderPlus /></button>
        {(w.selected?.node.kind === 'folder' || w.linkingParentId !== undefined) && (
          <>
            <button title="Add 2D array" aria-label="Add 2D array" onClick={() => w.selected?.node.kind === 'folder' && w.addArray2D(w.selected.node.id)}><Grid2X2 /></button>
            <button title="Add 3D array" aria-label="Add 3D array" onClick={() => w.selected?.node.kind === 'folder' && w.addArray3D(w.selected.node.id)}><Boxes /></button>
            <button
              title={w.linkingParentId !== undefined ? 'Cancel linking' : 'Link existing variable'}
              aria-label={w.linkingParentId !== undefined ? 'Cancel linking' : 'Link existing variable'}
              className={w.linkingParentId !== undefined ? 'selected' : ''}
              onClick={() => w.beginLink()}
            >
              <Link2 />
            </button>
          </>
        )}
      </div>
      <div
        className="object-tree-scroll"
        role="navigation"
        aria-label="Project variables"
        onDragOver={(event) => {
          if (!draggedId) return
          if (findObject(w.project, draggedId)?.node.kind === 'reference') return
          event.preventDefault()
          event.dataTransfer.dropEffect = 'move'
          setIsRootDrop(true)
        }}
        onDragLeave={(event) => {
          if (event.currentTarget.contains(event.relatedTarget as Node)) return
          setIsRootDrop(false)
        }}
        onDrop={(event) => {
          event.preventDefault()
          if (draggedId && findObject(w.project, draggedId)?.node.kind !== 'reference') {
            w.moveTo(draggedId, null)
          }
          setDraggedId(null)
          setDropTarget(null)
          setIsRootDrop(false)
        }}
      >
        {render(w.project.objects, 0)}
        {!w.project.objects.length && <p className="object-tree-empty">No variables yet. Add a value or folder above.</p>}
        {draggedId && findObject(w.project, draggedId)?.node.kind !== 'reference' && (
          <div
            className={`object-tree-root-dropzone ${isRootDrop ? 'active' : ''}`}
            onDragOver={(event) => {
              event.preventDefault()
              event.stopPropagation()
              setIsRootDrop(true)
              setDropTarget(null)
            }}
            onDrop={(event) => {
              event.preventDefault()
              event.stopPropagation()
              w.moveTo(draggedId, null)
              setDraggedId(null)
              setDropTarget(null)
              setIsRootDrop(false)
            }}
          >
            Move to top level
          </div>
        )}
      </div>
    </div>
  )
}

export function ObjectTreeEditor({ workspace: w }: { workspace: ObjectWorkspace }) {
  const [drafts, setDrafts] = useState<Record<string, { text: string; error?: string }>>({})
  const [expandedItemIds, setExpandedItemIds] = useState<Set<string>>(new Set())

  useEffect(() => {
    setDrafts({})
  }, [w.historyVersion])

  const toggleItem = (id: string) => {
    setExpandedItemIds((current) => {
      const next = new Set(current)
      if (next.has(id)) next.delete(id)
      else next.add(id)
      return next
    })
  }

  const patch = (id: string, fields: Parameters<typeof updateObject>[2], options?: { key?: string; discrete?: boolean }) =>
    w.edit((current) => updateObject(current, id, fields), options)

  const changeValue = (node: ValueNode, text: string) => {
    try {
      const inferred = node.typeMode === 'auto' ? inferValue(text) : undefined
      const value = inferred ? inferred.value : parseValue(catalog.type(node.type), text)
      const type = inferred?.type ?? node.type
      const count = type === 'STR' ? Math.max(DEFAULT_STR_LENGTH, new TextEncoder().encode(text).length) : inferred?.count ?? (Array.isArray(value) ? value.length : 1)
      const length = type === node.type ? Math.max(node.length, count, 1) : Math.max(count, 1)
      setDrafts((current) => ({ ...current, [node.id]: { text } }))
      patch(node.id, { type, value, length }, { key: `${node.id}:val` })
    } catch (cause) { setDrafts((current) => ({ ...current, [node.id]: { text, error: cause instanceof Error ? cause.message : String(cause) } })) }
  }

  const render = (nodes: readonly ObjectNode[], depth: number, parent?: ObjectNode): React.ReactNode => nodes.map((node, index) => {
    const selected = w.selectedId === node.id
    const problems = w.diagnostics.filter((entry) => entry.objectId === node.id)
    const label = node.kind === 'reference' ? findObject(w.project, node.targetId)?.node.name ?? node.name : (node.name || `[${index}]`)

    const isParentArray = parent?.kind === 'folder' && (
      (parent.children.length > 0 && parent.children.every((c) => c.kind === 'value')) ||
      parent.name.startsWith('arr2d_') ||
      parent.name.startsWith('arr3d_')
    )

    const isArrayItem = node.kind === 'value' && (isParentArray || (parent !== undefined && node.name === ''))
    const isItemExpanded = expandedItemIds.has(node.id)

    const is2DArray = node.kind === 'folder' && node.children.length > 0 && node.children.every((c) => c.kind === 'value')
    const is3DArray = node.kind === 'folder' && node.children.length > 0 && node.children.every((c) => c.kind === 'folder' && c.children.length > 0 && c.children.every((cc) => cc.kind === 'value'))
    const isFolderCollapsed = w.collapsed.has(node.id)

    return (
      <div key={node.id} className={`object-main-entry depth-${Math.min(depth, 4)} ${isArrayItem ? 'is-array-item' : ''}`}>
        <div className={`object-main-card ${node.kind} ${selected ? 'selected' : ''} ${isArrayItem && !isItemExpanded ? 'is-compact' : ''}`} onClick={() => w.select(node.id)}>
          {isArrayItem ? (
            isItemExpanded ? (
              <>
                <div className="object-main-row object-main-top object-main-array-row">
                  <button
                    type="button"
                    className="object-main-disclosure"
                    title="Collapse row"
                    aria-label={`Collapse ${label}`}
                    onClick={(e) => {
                      e.stopPropagation()
                      toggleItem(node.id)
                    }}
                  >
                    <ChevronDown />
                  </button>
                  <span className="object-main-array-index">[{index}]</span>
                  <TextField
                    aria-label={`Name of ${label}`}
                    placeholder={`[${index}]`}
                    value={node.name}
                    onChange={(event) => patch(node.id, { name: event.target.value })}
                  />
                  <div className="object-main-type-picker" title="Click to override type" onClick={(e) => e.stopPropagation()}>
                    {node.type !== 'STR' && node.length > 1 && <span className="object-main-shape">Table</span>}
                    <ObjectDesignator node={node} />
                    <SelectField
                      aria-label={`Type of ${label}`}
                      value={node.typeMode === 'auto' ? 'auto' : node.type}
                      onChange={(event) => {
                        const type = event.target.value
                        setDrafts((current) => { const next = { ...current }; delete next[node.id]; return next })
                        const resolvedType = type === 'auto' ? (typeof node.value === 'string' ? 'STR' : node.type === 'B' ? 'B' : 'F') : type
                        const length = resolvedType === 'STR' ? Math.max(node.length, DEFAULT_STR_LENGTH) : node.length
                        patch(node.id, type === 'auto' ? { typeMode: 'auto', type: resolvedType, length } : { typeMode: undefined, type, value: undefined, length })
                      }}
                    >
                      <option value="auto">Auto · {node.type === 'B' ? 'T/F' : node.type}</option>
                      {valueTypes.map((type) => <option key={type.key} value={type.key}>{type.key === 'B' ? 'T/F' : type.key}</option>)}
                    </SelectField>
                  </div>
                  <button
                    type="button"
                    className="object-main-delete"
                    title="Delete (Del)"
                    aria-label={`Delete ${label}`}
                    onClick={(event) => {
                      event.stopPropagation()
                      w.remove(node.id)
                    }}
                  >
                    <Trash2 />
                  </button>
                </div>
                <div className="object-main-row object-main-bottom">
                  <label className="object-main-value">
                    Initial value
                    <TextField
                      aria-label={`Initial value of ${label}`}
                      value={drafts[node.id]?.text ?? valueText(node.value)}
                      onChange={(event) => changeValue(node, event.target.value)}
                      placeholder={node.type === 'STR' ? 'Text' : '0 or 1, 2, 3'}
                    />
                  </label>
                  <label className="object-main-length">
                    <span>{node.type === 'STR' ? 'Characters' : 'Items'}</span>
                    <TextField
                      aria-label={`Items of ${label}`}
                      type="number"
                      min="1"
                      value={node.length}
                      onChange={(event) => patch(node.id, { length: Math.max(1, Math.floor(Number(event.target.value) || 1)) })}
                    />
                  </label>
                </div>
              </>
            ) : (
              <div className="object-main-row object-main-top object-main-array-row is-collapsed">
                <button
                  type="button"
                  className="object-main-disclosure"
                  title="Expand row"
                  aria-label={`Expand ${label}`}
                  onClick={(e) => {
                    e.stopPropagation()
                    toggleItem(node.id)
                  }}
                >
                  <ChevronRight />
                </button>
                <span className="object-main-array-index">[{index}]</span>
                <TextField
                  className="object-main-array-inline-value"
                  aria-label={`Value of ${label}`}
                  value={drafts[node.id]?.text ?? valueText(node.value)}
                  onChange={(event) => changeValue(node, event.target.value)}
                  placeholder={node.type === 'STR' ? 'Text value' : '0 or 1, 2, 3...'}
                  onClick={(e) => e.stopPropagation()}
                />
                <div className="object-main-type-picker" title="Click to override type" onClick={(e) => e.stopPropagation()}>
                  {node.type !== 'STR' && node.length > 1 && <span className="object-main-shape">Table</span>}
                  <ObjectDesignator node={node} />
                  <SelectField
                    aria-label={`Type of ${label}`}
                    value={node.typeMode === 'auto' ? 'auto' : node.type}
                    onChange={(event) => {
                      const type = event.target.value
                      setDrafts((current) => { const next = { ...current }; delete next[node.id]; return next })
                      const resolvedType = type === 'auto' ? (typeof node.value === 'string' ? 'STR' : node.type === 'B' ? 'B' : 'F') : type
                      const length = resolvedType === 'STR' ? Math.max(node.length, DEFAULT_STR_LENGTH) : node.length
                      patch(node.id, type === 'auto' ? { typeMode: 'auto', type: resolvedType, length } : { typeMode: undefined, type, value: undefined, length })
                    }}
                  >
                    <option value="auto">Auto · {node.type === 'B' ? 'T/F' : node.type}</option>
                    {valueTypes.map((type) => <option key={type.key} value={type.key}>{type.key === 'B' ? 'T/F' : type.key}</option>)}
                  </SelectField>
                </div>
                <button
                  type="button"
                  className="object-main-delete"
                  title="Delete (Del)"
                  aria-label={`Delete ${label}`}
                  onClick={(event) => {
                    event.stopPropagation()
                    w.remove(node.id)
                  }}
                >
                  <Trash2 />
                </button>
              </div>
            )
          ) : (
            <div className="object-main-row object-main-top">
              {node.kind === 'folder' && (
                <button
                  type="button"
                  className="object-main-disclosure"
                  title={isFolderCollapsed ? 'Expand folder' : 'Collapse folder'}
                  aria-label={isFolderCollapsed ? `Expand ${node.name}` : `Collapse ${node.name}`}
                  onClick={(e) => {
                    e.stopPropagation()
                    w.setCollapsed((current) => {
                      const next = new Set(current)
                      if (next.has(node.id)) next.delete(node.id)
                      else next.add(node.id)
                      return next
                    })
                  }}
                >
                  {isFolderCollapsed ? <ChevronRight /> : <ChevronDown />}
                </button>
              )}
              {node.kind !== 'value' && <ObjectDesignator node={node} />}
              {node.kind === 'reference' ? (
                <span className="object-main-name">{label}</span>
              ) : (
                <TextField
                  aria-label={`Name of ${node.name}`}
                  placeholder={node.name ? "Name" : `[${index}]`}
                  value={node.name}
                  maxLength={15}
                  onChange={(event) => patch(node.id, { name: event.target.value }, { key: `${node.id}:name` })}
                />
              )}
              {node.kind === 'value' ? (
                <div className="object-main-type-picker" title="Click to override type">
                  {node.type !== 'STR' && node.length > 1 && <span className="object-main-shape">Table</span>}
                  <ObjectDesignator node={node} />
                  <SelectField
                    aria-label={`Type of ${node.name}`}
                    value={node.typeMode === 'auto' ? 'auto' : node.type}
                    onChange={(event) => {
                      const type = event.target.value
                      setDrafts((current) => { const next = { ...current }; delete next[node.id]; return next })
                      const resolvedType = type === 'auto' ? (typeof node.value === 'string' ? 'STR' : node.type === 'B' ? 'B' : 'F') : type
                      const length = resolvedType === 'STR' ? Math.max(node.length, DEFAULT_STR_LENGTH) : node.length
                      patch(node.id, type === 'auto' ? { typeMode: 'auto', type: resolvedType, length } : { typeMode: undefined, type, value: undefined, length })
                    }}
                  >
                    <option value="auto">Auto · {node.type === 'B' ? 'T/F' : node.type}</option>
                    {valueTypes.map((type) => <option key={type.key} value={type.key}>{type.key === 'B' ? 'T/F' : type.key}</option>)}
                  </SelectField>
                </div>
              ) : (
                <span className="object-main-kind">{is2DArray ? '2D Array' : is3DArray ? '3D Array' : node.kind === 'folder' ? 'Folder' : 'Reference'}</span>
              )}
              <button
                type="button"
                className="object-main-delete"
                title="Delete (Del)"
                aria-label={`Delete ${label}`}
                onClick={(event) => {
                  event.stopPropagation()
                  w.remove(node.id)
                }}
              >
                <Trash2 />
              </button>
            </div>
          )}

          {!isArrayItem && (
            <div className="object-main-row object-main-bottom">
              {node.kind === 'value' ? (
                <>
                  <label className="object-main-value">
                    Initial value
                    <TextField
                      aria-label={`Initial value of ${label}`}
                      value={drafts[node.id]?.text ?? valueText(node.value)}
                      onChange={(event) => changeValue(node, event.target.value)}
                      placeholder={node.type === 'STR' ? 'Text' : '0 or 1, 2, 3'}
                    />
                  </label>
                  {!isArrayItem && (
                    <label className="object-main-length">
                      <span>{node.type === 'STR' ? 'Characters' : 'Items'}</span>
                      <TextField
                        aria-label={`Items of ${node.name || 'array'}`}
                        type="number"
                        min="1"
                        max={maxElementsOf(node.type)}
                        value={node.length}
                        onChange={(event) => patch(node.id, { length: Math.max(1, Math.min(maxElementsOf(node.type), Math.floor(Number(event.target.value) || 1))) }, { key: `${node.id}:len` })}
                      />
                    </label>
                  )}
                </>
              ) : is2DArray ? (() => {
                const valueChildren = node.children.filter((c): c is ValueNode => c.kind === 'value')
                const lengths = valueChildren.map((c) => c.length)
                const isUniformLength = lengths.length > 0 && lengths.every((l) => l === lengths[0])
                const types = valueChildren.map((c) => c.type)
                const isUniformType = types.length > 0 && types.every((t) => t === types[0])
                const typeLabel = isUniformType ? (types[0] === 'B' ? 'T/F' : types[0]) : 'mixed'
                return (
                  <>
                    <span>{node.children.length} rows {isUniformLength ? `× ${lengths[0]} items` : '· mixed items'}</span>
                    <Badge size="detail">Type: {typeLabel}</Badge>
                  </>
                )
              })() : is3DArray ? (() => {
                const planes = node.children.filter((c): c is FolderNode => c.kind === 'folder')
                const planeRows = planes.map((p) => p.children.length)
                const isUniformRows = planeRows.length > 0 && planeRows.every((r) => r === planeRows[0])
                const allLeafs = planes.flatMap((p) => p.children.filter((c): c is ValueNode => c.kind === 'value'))
                const leafLengths = allLeafs.map((c) => c.length)
                const isUniformCols = leafLengths.length > 0 && leafLengths.every((l) => l === leafLengths[0])
                const leafTypes = allLeafs.map((c) => c.type)
                const isUniformType = leafTypes.length > 0 && leafTypes.every((t) => t === leafTypes[0])
                const typeLabel = isUniformType ? (leafTypes[0] === 'B' ? 'T/F' : leafTypes[0]) : 'mixed'
                return (
                  <>
                    <span>{node.children.length} planes {isUniformRows && isUniformCols ? `× ${planeRows[0]} × ${leafLengths[0]} items` : '· mixed'}</span>
                    <Badge size="detail">Type: {typeLabel}</Badge>
                  </>
                )
              })() : node.kind === 'folder' ? (
                <span>{node.children.length} {node.children.length === 1 ? 'entry' : 'entries'}</span>
              ) : (
                <span>Links to {findObject(w.project, node.targetId)?.node.name ?? 'missing object'}</span>
              )}
            </div>
          )}

          {drafts[node.id]?.error && <p className="object-editor-error">{drafts[node.id].error}</p>}
          {!!problems.length && (
            <div className="object-main-problems">
              {problems.map((problem, idx) => (
                <p key={idx} className={problem.severity}>{problem.message}</p>
              ))}
            </div>
          )}
        </div>

        {node.kind === 'folder' && node.children.length > 0 && !isFolderCollapsed && (
          <div className="object-main-children">{render(node.children, depth + 1, node)}</div>
        )}
      </div>
    )
  })

  return <div className="object-editor">
    {w.error && <p className="object-editor-error" role="alert">{w.error}</p>}
    <div className="object-main-list">{render(w.project.objects, 0)}</div>
    {!w.project.objects.length && <div className="object-editor-empty"><p>No variables yet.</p><div><button onClick={() => w.add('value')}>Add value</button><button onClick={() => w.add('folder')}>Add folder</button></div></div>}
  </div>
}

export function ObjectDetails({ workspace: w, onJump }: { workspace: ObjectWorkspace; onJump: (id: string) => void }) {
  const selected = w.selected?.node
  if (!selected) return <div className="object-details"><p>Select an item to see its properties and relations.</p></div>
  const target = selected.kind === 'reference' ? findObject(w.project, selected.targetId)?.node : selected
  const related = new Map<string, string>()
  const availableFolders: { id: string; name: string }[] = []
  if (w.selected?.parent) related.set(w.selected.parent.id, w.selected.parent.name)
  const targetParent = target ? findObject(w.project, target.id)?.parent : undefined
  if (targetParent) related.set(targetParent.id, targetParent.name)
  walkObjects(w.project.objects, (node, ancestors) => {
    if (node.kind === 'folder' && node.id !== selected.id && !ancestors.some((folder) => folder.id === selected.id)) availableFolders.push({ id: node.id, name: `${'  '.repeat(ancestors.length)}${node.name}` })
    if (node.kind === 'reference' && node.targetId === target?.id) {
      const parent = ancestors.at(-1)
      if (parent) related.set(parent.id, parent.name)
    }
  })

  // Check if selected is a 2D array folder
  const is2DArray = selected.kind === 'folder' && selected.children.length > 0 && selected.children.every((c) => c.kind === 'value')
  const rows2D = is2DArray ? selected.children.length : 0
  const rowChildren2D = is2DArray ? (selected.children as ValueNode[]) : []
  const lengths2D = rowChildren2D.map((c) => c.length)
  const isUniformCols2D = lengths2D.length > 0 && lengths2D.every((l) => l === lengths2D[0])
  const cols2D = isUniformCols2D ? lengths2D[0] : 0
  const types2D = rowChildren2D.map((c) => c.type)
  const isUniformType2D = types2D.length > 0 && types2D.every((t) => t === types2D[0])
  const type2D = isUniformType2D ? types2D[0] : 'mixed'

  // Check if selected is a 3D array folder
  const is3DArray = selected.kind === 'folder' && selected.children.length > 0 && selected.children.every((c) => c.kind === 'folder' && c.children.length > 0 && c.children.every((cc) => cc.kind === 'value'))
  const depth3D = is3DArray ? selected.children.length : 0
  const rows3D = is3DArray ? (selected.children[0] as FolderNode).children.length : 0
  const cols3D = is3DArray ? ((selected.children[0] as FolderNode).children[0] as ValueNode).length : 0
  const type3D = is3DArray ? ((selected.children[0] as FolderNode).children[0] as ValueNode).type : 'F'

  const displayName = target?.name || selected.name || (w.selected?.parent ? `${w.selected.parent.name || 'folder'}[${w.selected.index}]` : `[${w.selected?.index ?? 0}]`)

  return <div className="object-details">
    <PanelHeader icon={<ObjectDesignator node={selected} />} title={displayName} />

    {is2DArray && (
      <div className="object-details-section">
        <div className="object-details-section-title">
          <h3>2D Array Configuration</h3>
          {isUniformCols2D ? (
            <Badge tone="accent" size="detail">{rows2D} × {cols2D} = {rows2D * cols2D} items</Badge>
          ) : (
            <Badge tone="warning" size="detail">{rows2D} rows · mixed items</Badge>
          )}
        </div>
        <div className="object-details-grid">
          <label>
            Rows
            <TextField
              type="number"
              min="1"
              max="256"
              value={rows2D}
              onChange={(e) => {
                const r = Math.max(1, Math.min(256, Math.floor(Number(e.target.value) || 1)))
                w.resizeArray2D(selected.id, r, cols2D, type2D)
              }}
            />
          </label>
          <label>
            Items (Cols)
            <TextField
              type="number"
              min="1"
              max="1024"
              value={cols2D || ''}
              placeholder={isUniformCols2D ? String(cols2D) : 'Mixed'}
              onChange={(e) => {
                const c = Math.max(1, Math.min(1024, Math.floor(Number(e.target.value) || 1)))
                w.resizeArray2D(selected.id, rows2D, c, type2D === 'mixed' ? undefined : type2D)
              }}
            />
          </label>
        </div>
        <label>
          Element Type
          <SelectField
            value={type2D}
            onChange={(e) => w.resizeArray2D(selected.id, rows2D, cols2D, e.target.value)}
          >
            {valueTypes.map((type) => (
              <option key={type.key} value={type.key}>
                {type.key === 'B' ? 'T/F (Boolean)' : `${type.key} · ${type.alias}`}
              </option>
            ))}
          </SelectField>
        </label>
      </div>
    )}

    {is3DArray && (
      <div className="object-details-section">
        <div className="object-details-section-title">
          <h3>3D Array Configuration</h3>
          <Badge tone="accent" size="detail">{depth3D} × {rows3D} × {cols3D} = {depth3D * rows3D * cols3D} items</Badge>
        </div>
        <div className="object-details-grid-3">
          <label>
            Depth
            <TextField
              type="number"
              min="1"
              max="64"
              value={depth3D}
              onChange={(e) => {
                const d = Math.max(1, Math.min(64, Math.floor(Number(e.target.value) || 1)))
                w.resizeArray3D(selected.id, d, rows3D, cols3D, type3D)
              }}
            />
          </label>
          <label>
            Rows
            <TextField
              type="number"
              min="1"
              max="128"
              value={rows3D}
              onChange={(e) => {
                const r = Math.max(1, Math.min(128, Math.floor(Number(e.target.value) || 1)))
                w.resizeArray3D(selected.id, depth3D, r, cols3D, type3D)
              }}
            />
          </label>
          <label>
            Items (Cols)
            <TextField
              type="number"
              min="1"
              max="512"
              value={cols3D}
              onChange={(e) => {
                const c = Math.max(1, Math.min(512, Math.floor(Number(e.target.value) || 1)))
                w.resizeArray3D(selected.id, depth3D, rows3D, c, type3D)
              }}
            />
          </label>
        </div>
        <label>
          Element Type
          <SelectField
            value={type3D}
            onChange={(e) => w.resizeArray3D(selected.id, depth3D, rows3D, cols3D, e.target.value)}
          >
            {valueTypes.map((type) => (
              <option key={type.key} value={type.key}>
                {type.key === 'B' ? 'T/F (Boolean)' : `${type.key} · ${type.alias}`}
              </option>
            ))}
          </SelectField>
        </label>
      </div>
    )}

    {selected.kind === 'folder' && !is2DArray && !is3DArray && (
      <div className="object-details-section">
        <h3>Folder Actions</h3>
        <div className="object-details-grid">
          <button onClick={() => w.beginLink()}><Link2 />Link Variable</button>
          <button onClick={() => w.addArray2D(selected.id)}><Grid2X2 />2D Array</button>
          <button onClick={() => w.addArray3D(selected.id)}><Boxes />3D Array</button>
        </div>
      </div>
    )}

    {selected.kind === 'value' && (
      <div className="object-details-section">
        <div className="object-details-section-title">
          <h3>Variable / 1D Array</h3>
          {selected.length > 1 && <Badge tone="accent" size="detail">{selected.length} items</Badge>}
        </div>
        <div className="object-details-grid">
          <label>
            Type
            <SelectField
              value={selected.typeMode === 'auto' ? 'auto' : selected.type}
              onChange={(e) => {
                const type = e.target.value
                const resolvedType = type === 'auto' ? (typeof selected.value === 'string' ? 'STR' : selected.type === 'B' ? 'B' : 'F') : type
                const length = resolvedType === 'STR' ? Math.max(selected.length, DEFAULT_STR_LENGTH) : selected.length
                w.edit((curr) => updateObject(curr, selected.id, type === 'auto' ? { typeMode: 'auto', type: resolvedType, length } : { typeMode: undefined, type, value: undefined, length }))
              }}
            >
              <option value="auto">Auto · {selected.type === 'B' ? 'T/F' : selected.type}</option>
              {valueTypes.map((t) => (
                <option key={t.key} value={t.key}>
                  {t.key === 'B' ? 'T/F' : t.key}
                </option>
              ))}
            </SelectField>
          </label>
          <label>
            {selected.type === 'STR' ? 'Characters' : 'Items'}
            <TextField
              type="number"
              min="1"
              max={maxElementsOf(selected.type)}
              value={selected.length}
              onChange={(e) => w.edit((curr) => updateObject(curr, selected.id, { length: Math.max(1, Math.min(maxElementsOf(selected.type), Math.floor(Number(e.target.value) || 1))) }))}
            />
          </label>
        </div>
      </div>
    )}

    <div className="object-details-section">
      <h3>Properties</h3>
      <label>Location<SelectField value={w.selected?.parent?.id ?? ''} onChange={(event) => w.edit((current) => moveObject(current, selected.id, event.target.value || null))}>{selected.kind !== 'reference' && <option value="">Top level</option>}{availableFolders.map((folder) => <option key={folder.id} value={folder.id}>{folder.name}</option>)}</SelectField></label>
      <div className="object-details-move">
        <button disabled={w.selected?.index === 0} onClick={() => w.move(selected.id, -1)}>
          <ArrowUp />
          <span>Move up</span>
        </button>
        <button disabled={w.selected?.index === (w.selected?.parent?.children ?? w.project.objects).length - 1} onClick={() => w.move(selected.id, 1)}>
          <ArrowDown />
          <span>Move down</span>
        </button>
      </div>
      {selected.kind !== 'reference' && <label>Description<textarea value={selected.description ?? ''} onChange={(event) => w.edit((current) => updateObject(current, selected.id, { description: event.target.value || undefined }), { key: `${selected.id}:desc` })} /></label>}
      {selected.kind === 'value' && (
        <div className="object-details-toggles">
          <label className="object-details-check">
            <TextField
              type="checkbox"
              checked={!selected.mutable}
              onChange={(event) => w.edit((current) => updateObject(current, selected.id, { mutable: !event.target.checked }))}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Read only</span>
          </label>
          <label className="object-details-check">
            <TextField
              type="checkbox"
              checked={selected.retentive}
              onChange={(event) => w.edit((current) => updateObject(current, selected.id, { retentive: event.target.checked }))}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Keep after restart</span>
          </label>
          <label className="object-details-check subscribe-check">
            <TextField
              type="checkbox"
              checked={selected.subscribed ?? false}
              onChange={(event) => w.edit((current) => updateObject(current, selected.id, { subscribed: event.target.checked }))}
            />
            <span className="object-check-box" aria-hidden="true" />
            <Eye className="object-check-eye" />
            <span>Track live (subscribe)</span>
          </label>
        </div>
      )}
      {selected.kind === 'folder' && (
        <div className="object-details-toggles">
          <label className="object-details-check subscribe-check">
            <TextField
              type="checkbox"
              checked={selected.subscribed ?? false}
              onChange={(event) => w.edit((current) => updateObject(current, selected.id, { subscribed: event.target.checked }))}
            />
            <span className="object-check-box" aria-hidden="true" />
            <Eye className="object-check-eye" />
            <span>Track live (subscribe)</span>
          </label>
        </div>
      )}
    </div>
    <div className="object-details-section"><h3>Connected blocks</h3><p>No connected blocks yet.</p></div>
    <div className="object-details-section"><h3>Related folders</h3>{related.size ? [...related].map(([id, name]) => <button key={id} onClick={() => onJump(id)}><Folder />{name}<ChevronRight /></button>) : <p>No related folders.</p>}</div>
    {selected.kind === 'reference' && <div className="object-details-section"><h3>Reference target</h3><button onClick={() => onJump(selected.targetId)}><Link2 />{target?.name ?? 'Missing target'}<ChevronRight /></button></div>}
  </div>
}

