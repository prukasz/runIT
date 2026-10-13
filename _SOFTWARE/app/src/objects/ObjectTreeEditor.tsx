import { useEffect, useState } from 'react'
import type { ReactNode } from 'react'
import { ChevronDown, ChevronRight, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { ChoiceField } from '../components/ChoiceField'
import { NumberField, TextField } from '../components/FormField'
import { Switch } from '../components/Switch/Switch'
import { NAME_MAX, maxElementsOf, typeOf, valueTypes } from '../domain/objects/catalog'
import { findObject, updateObject } from '../domain/project'
import type { FolderNode, ObjectNode, ValueNode } from '../domain/project'
import { ObjectDesignator } from './ObjectTreePalette'
import { DEFAULT_STR_LENGTH, inferValue, parseValue, valueText } from './useObjectTree'
import type { ObjectWorkspace } from './useObjectTree'
import './Objects.css'

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

  /** A type picked for an object: `auto` follows what it holds, anything else sets the type and clears the value. */
  const retype = (node: ValueNode, type: string) => {
    setDrafts((current) => { const next = { ...current }; delete next[node.id]; return next })
    const resolvedType = type === 'auto' ? (typeof node.value === 'string' ? 'STR' : node.type === 'B' ? 'B' : 'F') : type
    const length = resolvedType === 'STR' ? Math.max(node.length, DEFAULT_STR_LENGTH) : node.length
    patch(node.id, type === 'auto' ? { typeMode: 'auto', type: resolvedType, length } : { typeMode: undefined, type, value: undefined, length })
  }
  const patch = (id: string, fields: Parameters<typeof updateObject>[2], options?: { key?: string; discrete?: boolean }) =>
    w.edit((current) => updateObject(current, id, fields), options)

  const changeValue = (node: ValueNode, text: string) => {
    try {
      const inferred = node.typeMode === 'auto' ? inferValue(text) : undefined
      const value = inferred ? inferred.value : parseValue(typeOf(node.type), text)
      const type = inferred?.type ?? node.type
      const count = type === 'STR' ? Math.max(DEFAULT_STR_LENGTH, new TextEncoder().encode(text).length) : inferred?.count ?? (Array.isArray(value) ? value.length : 1)
      const length = type === node.type ? Math.max(node.length, count, 1) : Math.max(count, 1)
      setDrafts((current) => ({ ...current, [node.id]: { text } }))
      patch(node.id, { type, value, length }, { key: `${node.id}:val` })
    } catch (cause) { setDrafts((current) => ({ ...current, [node.id]: { text, error: cause instanceof Error ? cause.message : String(cause) } })) }
  }

  /** The initial value: a true / false switch for a single boolean, otherwise a text field. */
  const initialField = (node: ValueNode, label: string): ReactNode => {
    if (node.type === 'B' && node.length === 1) {
      const on = Array.isArray(node.value) && node.value[0] === true
      return (
        <div className="object-main-value" onClick={(event) => event.stopPropagation()}>
          <Switch tone="truefalse" checked={on} label={on ? 'True' : 'False'} onChange={(checked) => patch(node.id, { value: [checked] }, { key: `${node.id}:val` })} />
        </div>
      )
    }
    return (
      <label className="object-main-value">
        Default
        <TextField
          aria-label={`Default of ${label}`}
          value={drafts[node.id]?.text ?? valueText(node.value)}
          onChange={(event) => changeValue(node, event.target.value)}
          placeholder={node.type === 'STR' ? 'Text' : '0 or 1, 2, 3'}
        />
      </label>
    )
  }

  const render = (nodes: readonly ObjectNode[], depth: number, parent?: ObjectNode): ReactNode => nodes.map((node, index) => {
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
                    <ChoiceField<string> aria-label={`Type of ${label}`} value={node.typeMode === 'auto' ? 'auto' : node.type} onChange={(type) => type !== undefined && retype(node, type)}
 choices={[{ value: 'auto', label: `Auto · ${node.type === 'B' ? 'T/F' : node.type}` }, ...valueTypes.map((type) => ({ value: type.key, label: type.key === 'B' ? 'T/F' : type.key }))]} />
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
                  {initialField(node, label)}
                  <label className="object-main-length">
                    <span>{node.type === 'STR' ? 'Characters' : 'Items'}</span>
                    <NumberField
                      aria-label={`Items of ${label}`}
                      integer
                      min={1}
                      value={node.length}
                      onCommit={(length) => patch(node.id, { length })}
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
                {/* The name stays the main field on top when collapsed, as when expanded; the data only shows once expanded. */}
                <TextField
                  aria-label={`Name of ${label}`}
                  placeholder={`[${index}]`}
                  value={node.name}
                  onChange={(event) => patch(node.id, { name: event.target.value })}
                  onClick={(e) => e.stopPropagation()}
                />
                <div className="object-main-type-picker" title="Click to override type" onClick={(e) => e.stopPropagation()}>
                  {node.type !== 'STR' && node.length > 1 && <span className="object-main-shape">Table</span>}
                  <ObjectDesignator node={node} />
                  <ChoiceField<string> aria-label={`Type of ${label}`} value={node.typeMode === 'auto' ? 'auto' : node.type} onChange={(type) => type !== undefined && retype(node, type)}
 choices={[{ value: 'auto', label: `Auto · ${node.type === 'B' ? 'T/F' : node.type}` }, ...valueTypes.map((type) => ({ value: type.key, label: type.key === 'B' ? 'T/F' : type.key }))]} />
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
                  maxLength={NAME_MAX}
                  onChange={(event) => patch(node.id, { name: event.target.value }, { key: `${node.id}:name` })}
                />
              )}
              {node.kind === 'value' ? (
                <div className="object-main-type-picker" title="Click to override type">
                  {node.type !== 'STR' && node.length > 1 && <span className="object-main-shape">Table</span>}
                  <ObjectDesignator node={node} />
                  <ChoiceField<string> aria-label={`Type of ${node.name}`} value={node.typeMode === 'auto' ? 'auto' : node.type} onChange={(type) => type !== undefined && retype(node, type)}
 choices={[{ value: 'auto', label: `Auto · ${node.type === 'B' ? 'T/F' : node.type}` }, ...valueTypes.map((type) => ({ value: type.key, label: type.key === 'B' ? 'T/F' : type.key }))]} />
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
                  {initialField(node, label)}
                  {!isArrayItem && (
                    <label className="object-main-length">
                      <span>{node.type === 'STR' ? 'Characters' : 'Items'}</span>
                      <NumberField
                        aria-label={`Items of ${node.name || 'array'}`}
                        integer
                        min={1}
                        max={maxElementsOf(node.type)}
                        value={node.length}
                        onCommit={(length) => patch(node.id, { length }, { key: `${node.id}:len` })}
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

