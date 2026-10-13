import { useEffect, useState } from 'react'
import { Boxes, ChevronDown, ChevronRight, Folder, FolderPlus, Grid2X2, Link2, Plus, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { InlineRename } from '../components/InlineRename'
import { PaletteSearch } from '../components/PaletteSearch'
import { TreeSlab } from '../components/TreeSlab'
import { TypeBadge, ValueKindBadge } from '../components/TypeBadge/TypeBadge'
import { NAME_MAX } from '../domain/objects/catalog'
import { OBJECT_DRAG_TYPE, objectKindDragType, variableKind } from '../domain/objects/drag'
import { findObject, updateObject } from '../domain/project'
import type { ObjectNode } from '../domain/project'
import { isViableLinkTarget, matchesSearch } from './useObjectTree'
import type { ObjectWorkspace } from './useObjectTree'
import './Objects.css'

export function ObjectDesignator({ node }: { node: ObjectNode }) {
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
  // Double-click renames a row in place; a folder opens and closes with its chevron.
  const [renamingId, setRenamingId] = useState<string | null>(null)

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
          draggable={w.linkingParentId === undefined && renamingId !== node.id}
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
            // A reference shows its target's name: that one is renamed on the target.
            if (node.kind !== 'reference' && w.linkingParentId === undefined) setRenamingId(node.id)
          }}
          icon={<ObjectDesignator node={node} />}
          label={node.kind === 'reference' ? findObject(w.project, node.targetId)?.node.name ?? node.name : node.name}
          labelEditor={renamingId === node.id ? (
            <InlineRename
              className="tree-slab-rename"
              value={node.name}
              label={`Name of ${node.name || 'object'}`}
              placeholder="Name"
              maxLength={NAME_MAX}
              selectOnFocus
              onCommit={(text) => {
                setRenamingId(null)
                // An empty name would leave a top-level object unnamed: keep the old one.
                const name = text.trim()
                if (name && name !== node.name) w.edit((current) => updateObject(current, node.id, { name }), { key: `${node.id}:name` })
              }}
              onCancel={() => setRenamingId(null)}
            />
          ) : undefined}
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

