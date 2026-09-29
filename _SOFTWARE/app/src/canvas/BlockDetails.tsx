import type { DeviceCatalog } from '../domain/descriptors'
import { AlertCircle, AlertTriangle, Minus, Pencil, Plus, Trash2, X } from 'lucide-react'
import { arrangeProgram, disconnect, nameIds, OBJECT_DRAG_TYPE, parsePathText, pathLabel, PathTextError, sourceOf } from '../domain/canvas'
import type { Diagnostic } from '../domain/compiler'
import { runitVmCatalog } from '../domain/descriptors'
import type { VmBlockField, VmBlockType } from '../domain/descriptors'
import type { CanvasBlock, ObjectNode, ObjectPath, ProjectDevice, ProjectDocument, ValueNode } from '../domain/project'
import { findObject, newObjectId, walkObjects } from '../domain/project'
import { useEffect, useMemo, useState } from 'react'
import { blockShape, pathText } from './blockView'
import { ExpressionEditor } from './ExpressionEditor'
import { BlockHardwareFields } from './BlockHardwareFields'
import { BlockSwitch } from './BlockSwitch'
import { TypeBadge } from '../components/TypeBadge/TypeBadge'
import type { CanvasWorkspace } from './useCanvasWorkspace'
import './Canvas.css'

/*
 * Right panel of the canvas: the selected block's settings (its state fields
 * from the block descriptor, the expression of EXPR / EXPR_BIT, the loop body
 * of FOR, pin counts where they vary), how it runs (the gates it has and the
 * its explicit enables, on error), its pins
 * with what feeds them, and what the compiler says about it. Text fields apply on Enter or
 * when they lose focus (one undo step each).
 */

/** The expression blocks' counts come from the expression, not a setting. */
const EXPRESSION_COUNTS = new Set(['const_cnt', 'code_len'])

const humanize = (name: string): string => {
  const text = name.replace(/^k_/, '').replace(/_/g, ' ')
  return text.charAt(0).toUpperCase() + text.slice(1)
}

/** Members of an enum without their shared prefix (`VM_TIMER_UNIT_MS` → `MS`). */
const enumChoices = (type: VmBlockType, field: VmBlockField): { label: string; value: number }[] => {
  const members = type.enums.get(field.enumRef!) ?? []
  const names = members.map((member) => member.name)
  let prefix = names[0] ?? ''
  for (const name of names) while (!name.startsWith(prefix)) prefix = prefix.slice(0, -1)
  prefix = prefix.slice(0, prefix.lastIndexOf('_') + 1)
  return members.map((member) => ({ label: member.name.slice(prefix.length) || member.name, value: member.value }))
}


const isInteger = (field: VmBlockField): boolean => field.cType !== 'float' && !field.cType.endsWith('_u')

/** A number field that applies its value on Enter or blur; empty = back to 0 (the setting is dropped). */
function NumberField({ label, hint, value, integer, min, onCommit }: { label: string; hint?: string; value: number | undefined; integer: boolean; min?: number; onCommit: (value: number | undefined) => void }) {
  return (
    <label className="block-field">
      <span>{label}</span>
      <input
        key={String(value)}
        type="number"
        inputMode={integer ? 'numeric' : 'decimal'}
        step={integer ? 1 : 'any'}
        min={min}
        defaultValue={value ?? ''}
        placeholder="0"
        onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }}
        onBlur={(event) => {
          const text = event.currentTarget.value.trim()
          const next = text === '' ? undefined : Number(text)
          if (next !== undefined && !Number.isFinite(next)) return
          if (next !== value) onCommit(next)
        }}
      />
      {hint && <em>{hint}</em>}
    </label>
  )
}

function Counter({ label, value, min, max, onChange }: { label: string; value: number; min: number; max: number; onChange: (value: number) => void }) {
  return (
    <div className="block-counter">
      <span>{label}</span>
      <button type="button" aria-label={`Fewer ${label.toLowerCase()}`} disabled={value <= min} onClick={() => onChange(value - 1)}><Minus aria-hidden="true" /></button>
      <strong>{value}</strong>
      <button type="button" aria-label={`More ${label.toLowerCase()}`} disabled={value >= max} onClick={() => onChange(value + 1)}><Plus aria-hidden="true" /></button>
    </div>
  )
}

const isLoopType = (type: VmBlockType): boolean => type.fields.some((field) => field.source === 'derived' && field.cType === 'vm_span_t')

/** A compiler message about this block, with its own name dropped and pin keys (`block:<id>:in1`) named. */
const problemText = (message: string, id: string, title: string, shape: ReturnType<typeof blockShape>, project?: ProjectDocument): string =>
  nameIds(message, project)
    .replace(new RegExp(`^Block '${id}'( \\(${title}\\))?: `), '')
    .replace(new RegExp(`block:${id}:(in|en)(\\d+)`, 'g'), (_, kind: string, index: string) => (kind === 'in' ? shape.inputs.find((pin) => pin.index === Number(index))?.title ?? `Input ${index}` : `Enable ${Number(index) + 1}`))

const is2DArrayFolder = (node: ObjectNode): boolean => {
  if (node.kind !== 'folder' || node.children.length === 0) return false
  const firstChild = node.children[0]
  if (firstChild.kind !== 'value') return false
  const cols = firstChild.length ?? 1
  // A 2D matrix requires each child to be a value row and each row to have multiple columns (cols > 1).
  // A 3x1 or 1x1 structure is 1D (array of objects or folder of scalars), so second accessor isn't available.
  if (cols <= 1) return false
  return node.children.every((child) => child.kind === 'value' && (child.length ?? 1) === cols)
}


interface VariableCandidate {
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

const getProjectCandidates = (project?: ProjectDocument): VariableCandidate[] => {
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
    const is2D = is2DArrayFolder(node)
    return {
      id: found.node.id,
      name: found.node.name,
      fullPath,
      targetId,
      is2D,
      is1D: false,
      isFolder: true,
      pathText: is2D ? `${found.node.name}[ ][ ]` : found.node.name,
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
const parseIndexedPath = (text: string) => {
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
function DynamicIndexSlot({
  label,
  value,
  candidates,
  selectedCandidate,
  project,
  onAssign,
  onClear,
}: {
  slotIndex: number
  label: string
  value: string
  candidates: VariableCandidate[]
  selectedCandidate?: VariableCandidate
  project?: ProjectDocument
  onAssign: (varName: string) => void
  onClear: () => void
}) {
  const [isDragOver, setIsDragOver] = useState(false)
  const [searchQuery, setSearchQuery] = useState('')
  const [isFocused, setIsFocused] = useState(false)

  const matchedVar = useMemo(() => {
    if (!value || !value.trim()) return undefined
    return candidates.find(
      (c) => c.name.toLowerCase() === value.toLowerCase() || c.fullPath.toLowerCase() === value.toLowerCase()
    )
  }, [candidates, value])

  const searchResults = useMemo(() => {
    if (!searchQuery.trim()) return []
    const q = searchQuery.toLowerCase()
    return candidates
      .filter((c) => !c.is2D && (c.name.toLowerCase().includes(q) || c.fullPath.toLowerCase().includes(q)))
      .slice(0, 6)
  }, [candidates, searchQuery])

  return (
    <div className="pin-helpers-slot-row">
      <div className="pin-helpers-slot-label">
        <span>{label}</span>
        {selectedCandidate && !value && (
          <span className="pin-helpers-slot-hint">
            ← Click slot to insert &quot;{selectedCandidate.name}&quot;
          </span>
        )}
      </div>

      {value ? (
        <div className="pin-helpers-slot-box">
          <span className="pin-helpers-slot-bracket">[</span>
          {(() => {
            const isKey = /^["'].*["']$/.test(value.trim())
            const isLiteralIndex = /^\d+$/.test(value.trim())
            const keyText = isKey ? value.trim().slice(1, -1) : value
            return (
              <span className={`pin-helpers-slot-chip ${isKey ? 'is-key' : isLiteralIndex ? 'is-index' : ''}`}>
                {isKey && <span className="pin-helpers-slot-key-tag">KEY</span>}
                {isLiteralIndex && <span className="pin-helpers-slot-key-tag">IDX</span>}
                {matchedVar?.type && !isKey && !isLiteralIndex && <TypeBadge type={matchedVar.type} />}
                <strong>{isKey ? `"${keyText}"` : value}</strong>
                <button
                  type="button"
                  className="pin-helpers-slot-clear"
                  onClick={onClear}
                  title="Clear slot"
                >
                  <X aria-hidden="true" />
                </button>
              </span>
            )
          })()}
          <span className="pin-helpers-slot-bracket">]</span>
        </div>
      ) : (
        <div
          className={`pin-helpers-slot-box is-empty ${isDragOver ? 'is-drag-over' : ''} ${selectedCandidate ? 'can-click-to-assign' : ''}`}
          onDragOver={(e) => {
            e.preventDefault()
            e.dataTransfer.dropEffect = 'copy'
            setIsDragOver(true)
          }}
          onDragLeave={() => setIsDragOver(false)}
          onDrop={(e) => {
            e.preventDefault()
            setIsDragOver(false)
            const dropped = resolveUniversalDrop(e.dataTransfer, project)
            if (dropped) {
              onAssign(dropped.name)
            }
          }}
          onClick={() => {
            if (selectedCandidate) {
              onAssign(selectedCandidate.name)
            }
          }}
        >
          <span className="pin-helpers-slot-bracket">[</span>
          <input
            className="pin-helpers-slot-input"
            placeholder={
              selectedCandidate
                ? `Click to insert "${selectedCandidate.name}", or drop/type variable or "key"...`
                : 'Drop variable, click from tree, or type variable / "key"...'
            }
            value={searchQuery}
            onFocus={() => setIsFocused(true)}
            onBlur={() => setTimeout(() => setIsFocused(false), 200)}
            onChange={(e) => setSearchQuery(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') {
                if (searchResults.length > 0) {
                  onAssign(searchResults[0]!.name)
                  setSearchQuery('')
                } else if (searchQuery.trim()) {
                  onAssign(searchQuery.trim())
                  setSearchQuery('')
                }
              }
            }}
          />
          <span className="pin-helpers-slot-bracket">]</span>

          {isFocused && searchResults.length > 0 && (
            <div className="pin-helpers-slot-dropdown">
              {searchResults.map((cand) => (
                <button
                  key={cand.id}
                  type="button"
                  className="pin-helpers-slot-option"
                  onMouseDown={(e) => {
                    e.preventDefault()
                    onAssign(cand.name)
                    setSearchQuery('')
                  }}
                >
                  <span>{cand.fullPath}</span>
                  {cand.type && <TypeBadge type={cand.type} />}
                </button>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  )
}

/** A pin's variable as typed text (`table[3]`, `coefficient[row][col]`, `table[sel]`). */
function PathField({
  path,
  project,
  isEditing,
  externalText,
  selectedCandidate,
  onClearSelectedObject,
  onFocus,
  onChangeText,
  onCommit,
}: {
  path: ObjectPath
  project?: ProjectDocument
  isEditing: boolean
  externalText?: string
  selectedCandidate?: VariableCandidate
  onClearSelectedObject?: () => void
  onFocus: () => void
  onChangeText: (text: string) => void
  onCommit: (path: ObjectPath) => void
}) {
  const [problem, setProblem] = useState('')
  const shown = path.root === '' ? '' : pathLabel(path, project)
  const [text, setText] = useState(shown)
  const inputRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    setText(shown)
    setProblem('')
  }, [shown])

  useEffect(() => {
    if (isEditing && externalText !== undefined && externalText !== text) {
      setText(externalText)
      if (/\[\s*\]/.test(externalText)) {
        setProblem('')
      } else {
        const err = validate(externalText)
        setProblem(err)
      }
    }
  }, [isEditing, externalText, text])

  const validate = (val: string): string => {
    const trimmed = val.trim()
    if (!trimmed || !project) return ''
    if (/\[\s*\]/.test(trimmed)) return ''
    try {
      const parsed = parsePathText(trimmed, project)
      const target = findObject(project, parsed.root)?.node
      if (target?.kind === 'folder') {
        const stepsCount = (parsed.steps ?? []).filter((s) => s.kind === 'index' || s.kind === 'dynamic' || s.kind === 'name').length
        if (is2DArrayFolder(target)) {
          if (stepsCount < 2) {
            return `2D array '${target.name}' must be indexed with [row][col]`
          }
        } else if (stepsCount === 0) {
          if (isEditing) return ''
          const firstChild = target.children[0]
          return `Folder '${target.name}': pick member (.${firstChild?.name ?? 'member'}) or index [0]`
        }
      }
      return ''
    } catch (err) {
      if (isEditing && (trimmed.endsWith('[') || trimmed.includes('[ ]'))) return ''
      return err instanceof PathTextError ? err.message : String(err)
    }
  }

  const commitText = (val: string) => {
    const trimmed = val.trim()
    if (!trimmed || trimmed === shown || !project) return
    const err = validate(trimmed)
    if (err) {
      setProblem(err)
      return
    }
    try {
      onCommit(parsePathText(trimmed, project))
      setProblem('')
    } catch (e) {
      setProblem(e instanceof PathTextError ? e.message : String(e))
    }
  }

  return (
    <div className={`block-path-field ${isEditing ? 'is-editing' : ''}`}>
      <input
        ref={inputRef}
        value={text}
        placeholder={selectedCandidate ? `Click to assign "${selectedCandidate.name}"` : 'variable, e.g. table[3]'}
        title={selectedCandidate ? `Click to assign "${selectedCandidate.name}" (${selectedCandidate.fullPath})` : undefined}
        aria-label="Variable path"
        aria-invalid={problem ? 'true' : undefined}
        autoFocus={path.root === ''}
        onFocus={() => {
          onFocus()
          onChangeText(text)
        }}
        onClick={() => {
          if (selectedCandidate) {
            const nextVal = selectedCandidate.is2D
              ? `${selectedCandidate.fullPath}[ ][ ]`
              : selectedCandidate.is1D
              ? `${selectedCandidate.fullPath}[ ]`
              : selectedCandidate.fullPath
            setText(nextVal)
            onChangeText(nextVal)
            commitText(nextVal)
            onClearSelectedObject?.()
          }
        }}
        onDragOver={(e) => {
          e.preventDefault()
          e.dataTransfer.dropEffect = 'copy'
        }}
        onDrop={(e) => {
          e.preventDefault()
          const dropped = resolveUniversalDrop(e.dataTransfer, project)
          if (dropped) {
            setText(dropped.pathText)
            onChangeText(dropped.pathText)
            commitText(dropped.pathText)
          }
        }}
        onChange={(e) => {
          const val = e.target.value
          setText(val)
          onChangeText(val)
          const err = validate(val)
          setProblem(err)
        }}
        onBlur={() => {
          commitText(text)
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            commitText(text)
            e.currentTarget.blur()
          } else if (e.key === 'Escape') {
            setText(shown)
            onChangeText(shown)
            setProblem('')
            e.currentTarget.blur()
          }
        }}
      />
      {problem && <em className="is-error">{problem}</em>}
    </div>
  )
}

/** Full-width helpers card rendered directly below the pins list when editing a pin. */
function PinHelpersPanel({
  pinTitle,
  pinType,
  currentText,
  project,
  selectedObjectId,
  onClearSelectedObject,
  onCreateVariable,
  onApply,
  onClose,
}: {
  pinTitle: string
  pinType: string
  currentText: string
  project?: ProjectDocument
  selectedObjectId?: string
  onClearSelectedObject?: () => void
  onCreateVariable?: (node: ObjectNode) => void
  onApply: (text: string) => void
  onClose: () => void
}) {
  const candidates = useMemo(() => getProjectCandidates(project), [project])

  const parsed = useMemo(() => parseIndexedPath(currentText), [currentText])
  const base = parsed.base
  const indices = parsed.indices

  const matchedCandidate = useMemo(() => {
    if (!base) return undefined
    return candidates.find(
      (c) => c.name.toLowerCase() === base.toLowerCase() || c.fullPath.toLowerCase() === base.toLowerCase()
    )
  }, [candidates, base])

  const baseFolder = useMemo(() => {
    if (!base || !project) return undefined
    const found = findObject(project, base)?.node
    return found?.kind === 'folder' ? found : undefined
  }, [base, project])

  const selectedCandidate = useMemo(() => {
    if (!selectedObjectId || !project) return undefined
    return resolveUniversalNode(selectedObjectId, project)
  }, [selectedObjectId, project])

  const filteredCandidates = useMemo(() => {
    const query = base ? base.toLowerCase() : currentText.trim().toLowerCase()
    if (!query) return candidates
    return candidates.filter(
      (c) => c.name.toLowerCase().includes(query) || c.fullPath.toLowerCase().includes(query)
    )
  }, [candidates, base, currentText])

  return (
    <div
      className="pin-helpers-card"
      onMouseDown={(e) => {
        if ((e.target as HTMLElement).tagName !== 'BUTTON' && (e.target as HTMLElement).tagName !== 'INPUT') {
          e.preventDefault()
        }
      }}
    >
      <div className="pin-helpers-header">
        <div className="pin-helpers-heading">
          <span className="pin-helpers-badge">Pin Helper</span>
          <strong>{pinTitle}</strong>
          <TypeBadge type={pinType} />
          {matchedCandidate?.is2D && (
            <span className="pin-helpers-filter-tag">
              2D Array: {matchedCandidate.rows}×{matchedCandidate.cols}
            </span>
          )}
          {matchedCandidate?.is1D && (
            <span className="pin-helpers-filter-tag">
              1D Array: {matchedCandidate.length} items
            </span>
          )}
          {baseFolder && (
            <span className="pin-helpers-filter-tag">
              Folder: {baseFolder.name} ({baseFolder.children.length} members)
            </span>
          )}
        </div>
        <button type="button" className="pin-helpers-close" onClick={onClose} title="Close helper">
          <X aria-hidden="true" />
        </button>
      </div>

      {selectedCandidate && (
        <div className="pin-helpers-warning" style={{ background: 'color-mix(in srgb, var(--accent) 16%, transparent)', borderColor: 'var(--accent)' }}>
          <span>Selected in tree: <strong>{selectedCandidate.name}</strong> ({selectedCandidate.fullPath}) — click input field to assign</span>
        </div>
      )}

      {/* Warning if a 2D array or array folder has no index */}
      {matchedCandidate?.is2D && indices.length === 0 && (
        <div className="pin-helpers-warning">
          <span>⚠️ <strong>{base}</strong> is a 2D array and cannot be used as a bare number. Click <strong>[ ][ ]</strong> to add index fields, or pick an element below:</span>
        </div>
      )}

      {/* Folder members if base matches a folder */}
      {baseFolder && (
        <div className="pin-helpers-section">
          <div className="pin-helpers-label">
            <span>Folder <strong>{baseFolder.name}</strong> members:</span>
          </div>
          <div className="pin-helpers-chips">
            {baseFolder.children.map((child) => {
              const fullKeyPath = `${baseFolder.name}["${child.name}"]`
              const isSelected = currentText.trim() === fullKeyPath
              return (
                <button
                  key={`key-${child.id}`}
                  type="button"
                  className={`pin-helpers-chip is-key-chip ${isSelected ? 'is-selected' : ''}`}
                  onClick={() => onApply(fullKeyPath)}
                  title={`Select member by string key ["${child.name}"]`}
                >
                  [&quot;{child.name}&quot;]
                </button>
              )
            })}
            {baseFolder.children.map((child) => {
              const fullMemberPath = `${baseFolder.name}.${child.name}`
              const isSelected = currentText.trim() === fullMemberPath
              return (
                <button
                  key={child.id}
                  type="button"
                  className={`pin-helpers-chip ${isSelected ? 'is-selected' : ''}`}
                  onClick={() => onApply(fullMemberPath)}
                  title={`Select member .${child.name}`}
                >
                  .{child.name}
                </button>
              )
            })}
            {baseFolder.children.map((child, idx) => {
              const fullIdxPath = `${baseFolder.name}[${idx}]`
              const isSelected = currentText.trim() === fullIdxPath
              return (
                <button
                  key={`idx-${child.id}`}
                  type="button"
                  className={`pin-helpers-chip ${isSelected ? 'is-selected' : ''}`}
                  onClick={() => onApply(fullIdxPath)}
                  title={`Select item at index [${idx}]`}
                >
                  [{idx}]
                </button>
              )
            })}
          </div>
        </div>
      )}

      {/* Quick Syntax Chips */}
      <div className="pin-helpers-section">
        <div className="pin-helpers-label">
          <span>Quick Syntax &amp; Slots:</span>
        </div>
        <div className="pin-helpers-chips">
          {/* Dynamic slot builders */}
          <button
            type="button"
            className="pin-helpers-chip"
            title="Add dynamic slot [ ]"
            onClick={() => {
              const root = base || matchedCandidate?.name || candidates[0]?.name || 'table'
              onApply(`${root}[ ]`)
            }}
          >
            [ ]
          </button>
          {matchedCandidate?.is2D && !baseFolder && (
            <button
              type="button"
              className="pin-helpers-chip"
              title="Add double dynamic slots [ ][ ] (2D array)"
              onClick={() => {
                const root = base || matchedCandidate?.name || candidates[0]?.name || 'matrix'
                onApply(`${root}[ ][ ]`)
              }}
            >
              [ ][ ]
            </button>
          )}

          {/* Literal index chips */}
          {(['[0]', '[1]', '[2]', '[3]', '[sel]'] as const).map((chip) => (
            <button
              key={chip}
              type="button"
              className="pin-helpers-chip"
              title={`Insert ${chip}`}
              onClick={() => {
                const root = base || matchedCandidate?.name || candidates[0]?.name || 'table'
                if (indices.length > 0) {
                  const next = [...indices]
                  next[next.length - 1] = chip.replace(/[[\]]/g, '')
                  onApply(`${root}${next.map((v) => `[${v}]`).join('')}`)
                } else {
                  onApply(`${root}${chip}`)
                }
              }}
            >
              {chip}
            </button>
          ))}

          {/* String key chip */}
          <button
            type="button"
            className="pin-helpers-chip is-key-chip"
            title='Access by string key ["name"]'
            onClick={() => {
              const root = base || matchedCandidate?.name || candidates[0]?.name || 'obj'
              const firstChild = baseFolder?.children[0]?.name || 'name'
              if (indices.length > 0) {
                const next = [...indices]
                next[next.length - 1] = `"${firstChild}"`
                onApply(`${root}${next.map((v) => `[${v}]`).join('')}`)
              } else {
                onApply(`${root}["${firstChild}"]`)
              }
            }}
          >
            [&quot;name&quot;]
          </button>
        </div>
      </div>

      {/* Dynamic Index Slots (if brackets are active or user clicked [ ] / [ ][ ]) */}
      {indices.length > 0 && (
        <div className="pin-helpers-slots-container">
          <div className="pin-helpers-slots-header">
            <span>
              Dynamic Index Slots for <strong>{base}</strong>
              {indices.length > 1 ? ' (Row & Column)' : ''}:
            </span>
            {indices.length === 1 && matchedCandidate?.is2D && (
              <button
                type="button"
                className="pin-helpers-chip"
                title="Add second index for 2D array"
                onClick={() => onApply(`${base}[${indices[0]}][ ]`)}
              >
                + Add [ ] Col Slot
              </button>
            )}
          </div>

          <div className="pin-helpers-slots-list">
            {indices.map((idxVal, sIdx) => (
              <DynamicIndexSlot
                key={sIdx}
                slotIndex={sIdx}
                label={indices.length > 1 ? (sIdx === 0 ? 'Row Index [row]:' : 'Column Index [col]:') : 'Index [index]:'}
                value={idxVal}
                candidates={candidates}
                selectedCandidate={selectedCandidate ? {
                  id: selectedCandidate.id,
                  name: selectedCandidate.name,
                  fullPath: selectedCandidate.fullPath,
                  kind: selectedCandidate.is2D ? 'array2d' : 'value',
                  is2D: selectedCandidate.is2D,
                  is1D: selectedCandidate.is1D,
                } : undefined}
                project={project}
                onAssign={(varName) => {
                  const next = [...indices]
                  next[sIdx] = varName
                  onApply(`${base}${next.map((v) => `[${v}]`).join('')}`)
                  if (selectedCandidate) onClearSelectedObject?.()
                }}
                onClear={() => {
                  const next = [...indices]
                  next[sIdx] = ''
                  onApply(`${base}${next.map((v) => `[${v}]`).join('')}`)
                }}
              />
            ))}
          </div>
        </div>
      )}

      {/* 2D Array Literal Element Grid */}
      {matchedCandidate?.is2D && (
        <div className="pin-helpers-array-section">
          <div className="pin-helpers-label">
            <span>
              <strong>{matchedCandidate.name}</strong> matrix cells ({matchedCandidate.rows}×{matchedCandidate.cols}):
            </span>
          </div>
          <div className="pin-helpers-matrix-grid">
            {Array.from({ length: Math.min(matchedCandidate.rows ?? 3, 4) }, (_, r) =>
              Array.from({ length: Math.min(matchedCandidate.cols ?? 3, 4) }, (_, c) => {
                const elemPath = `${matchedCandidate.fullPath}[${r}][${c}]`
                const isSelected = currentText.trim() === elemPath
                return (
                  <button
                    key={`${r}-${c}`}
                    type="button"
                    className={`pin-helpers-elem-btn ${isSelected ? 'is-selected' : ''}`}
                    onClick={() => onApply(elemPath)}
                  >
                    [{r}][{c}]
                  </button>
                )
              })
            )}
          </div>
        </div>
      )}

      {/* 1D Array Literal Element Grid */}
      {matchedCandidate && !matchedCandidate.is2D && (matchedCandidate.length ?? 1) > 1 && (
        <div className="pin-helpers-array-section">
          <div className="pin-helpers-label">
            <span>
              Array <strong>{matchedCandidate.name}</strong> ({matchedCandidate.length} elements):
            </span>
          </div>
          <div className="pin-helpers-elements-grid">
            {Array.from({ length: Math.min(matchedCandidate.length!, 48) }, (_, idx) => {
              const elemPath = `${matchedCandidate.fullPath}[${idx}]`
              const isSelected = currentText.trim() === elemPath
              return (
                <button
                  key={idx}
                  type="button"
                  className={`pin-helpers-elem-btn ${isSelected ? 'is-selected' : ''}`}
                  title={elemPath}
                  onClick={() => onApply(elemPath)}
                >
                  [{idx}]
                </button>
              )
            })}
            {matchedCandidate.length! > 48 && (
              <span className="pin-helpers-more">+{matchedCandidate.length! - 48} more...</span>
            )}
          </div>
        </div>
      )}

      {/* Available Project Variables: only shown when user is filtering/searching or no baseFolder */}
      {currentText.trim().length > 0 && !baseFolder && (
        <div className="pin-helpers-section">
          <div className="pin-helpers-label">
            <span>Filtered Variables ({filteredCandidates.length}):</span>
            {base && <span className="pin-helpers-filter-tag">filter: &quot;{base}&quot;</span>}
          </div>
          <div className="pin-helpers-var-list">
            {filteredCandidates.slice(0, 8).map((cand) => (
              <button
                key={cand.id}
                type="button"
                className="pin-helpers-var-row"
                onClick={() => {
                  if (cand.is2D) {
                    onApply(`${cand.fullPath}[ ][ ]`)
                  } else if (cand.is1D) {
                    onApply(`${cand.fullPath}[ ]`)
                  } else {
                    onApply(cand.fullPath)
                  }
                }}
              >
                <span className="pin-helpers-var-name">{cand.fullPath}</span>
                {cand.type && <TypeBadge type={cand.type} />}
                {cand.is2D && (
                  <span className="pin-helpers-var-count">{cand.rows}×{cand.cols} 2D</span>
                )}
                {cand.is1D && (
                  <span className="pin-helpers-var-count">{cand.length} items</span>
                )}
              </button>
            ))}
            {filteredCandidates.length === 0 && (
              <div className="pin-helpers-empty">No matching variables found</div>
            )}
          </div>
        </div>
      )}

      {/* Variable Creator: when typed variable does not exist yet */}
      {(() => {
        const raw = (base || currentText.trim()).replace(/[[\]'"]/g, '').trim()
        const hasMatch = candidates.some((c) => c.name.toLowerCase() === raw.toLowerCase() || c.fullPath.toLowerCase() === raw.toLowerCase())
        const canCreateTyped = raw.length > 0 && !hasMatch && isValidVariableName(raw) && !!onCreateVariable
        const vmType = mapPinTypeToVmType(pinType)

        return (
          <>
            {canCreateTyped && (
              <div className="pin-helpers-creator-bar">
                <span className="pin-helpers-creator-msg">
                  Variable <strong>&quot;{raw}&quot;</strong> does not exist yet.
                </span>
                <button
                  type="button"
                  className="pin-helpers-create-btn"
                  onClick={() => {
                    const newNode: ValueNode = {
                      kind: 'value',
                      id: newObjectId(),
                      name: raw,
                      type: vmType,
                      length: 1,
                      mutable: true,
                      retentive: false,
                      typeMode: 'auto',
                    }
                    onCreateVariable(newNode)
                    onApply(raw)
                  }}
                >
                  + Create &quot;{raw}&quot; [{vmType}]
                </button>
              </div>
            )}
            {onCreateVariable && !canCreateTyped && (
              <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: '4px' }}>
                <button
                  type="button"
                  className="pin-helpers-new-var-btn"
                  title={`Create a new ${vmType} variable in project`}
                  onClick={() => {
                    const siblings = project?.objects ?? []
                    const prefix = (pinTitle.toLowerCase().replace(/[^a-z0-9_]/g, '') || 'var')
                    const name = nextVarName(siblings, prefix)
                    const newNode: ValueNode = {
                      kind: 'value',
                      id: newObjectId(),
                      name,
                      type: vmType,
                      length: 1,
                      mutable: true,
                      retentive: false,
                      typeMode: 'auto',
                    }
                    onCreateVariable(newNode)
                    onApply(name)
                  }}
                >
                  + New {vmType} Variable
                </button>
              </div>
            )}
          </>
        )
      })()}

      <div className="pin-helpers-tips">
        <div><strong>Syntax Tips:</strong></div>
        <div>• <code>coefficient[row][col]</code> : <strong>2D dynamic indexing</strong> (drop row &amp; col variables into slots).</div>
        <div>• <code>obj[&quot;key&quot;]</code> : <strong>String key accessor</strong> for VM objects / folders.</div>
        <div>• <code>table[sel]</code> : <strong>1D dynamic indexing</strong> (drop index variable into slot).</div>
        <div>• <code>table[3]</code> / <code>coefficient[0][1]</code> : <strong>Literal indexing</strong> (click matrix/cell chips).</div>
      </div>
    </div>
  )
}

function BlockAlias({ path, blocks, label, onRename }: { path: ObjectPath; blocks: readonly CanvasBlock[]; label: string; onRename: (blockId: string, name: string | undefined, outputIndex?: number) => void }) {
  const [editing, setEditing] = useState(false)
  const match = /^(.+):(q(\d+)|eno)$/.exec(path.root)
  if (!match || !blocks.some((entry) => entry.id === match[1])) return <span>{label}</span>
  const source = blocks.find((entry) => entry.id === match[1])!
  const outputIndex = match[3] === undefined ? undefined : Number(match[3])
  const current = outputIndex === undefined ? source.name ?? '' : source.outputAliases?.[outputIndex] ?? ''
  const commit = (value: string) => {
    setEditing(false)
    const name = value.trim() || undefined
    if (name !== current) onRename(source.id, name, outputIndex)
  }
  return editing ? <input className="block-alias-inline" autoFocus aria-label={outputIndex === undefined ? `Alias for ${source.id}` : `Alias for ${source.id} output ${outputIndex}`} defaultValue={current} placeholder={outputIndex === undefined ? source.id : `q${outputIndex}`} onPointerDown={(event) => event.stopPropagation()} onKeyDown={(event) => {
    if (event.key === 'Enter') event.currentTarget.blur()
    if (event.key === 'Escape') { event.currentTarget.dataset.cancel = 'true'; event.currentTarget.blur() }
    event.stopPropagation()
  }} onBlur={(event) => { if (event.currentTarget.dataset.cancel !== 'true') commit(event.currentTarget.value); else setEditing(false) }} />
    : <span className="block-alias-edit"><span title="Block or output alias">{label}</span><button type="button" aria-label="Edit alias" title="Edit block or output alias" onClick={() => setEditing(true)}><Pencil aria-hidden="true" /></button></span>
}

export function BlockDetails({
  workspace,
  diagnostics,
  devices = [],
  deviceCatalog,
  project,
  selectedObjectId,
  onClearSelectedObject,
  onCreateVariable,
  onExpandDetails,
}: {
  workspace: CanvasWorkspace
  diagnostics: ReadonlyMap<string, readonly Diagnostic[]>
  devices?: readonly ProjectDevice[]
  deviceCatalog?: DeviceCatalog
  project?: ProjectDocument
  selectedObjectId?: string
  onClearSelectedObject?: () => void
  onCreateVariable?: (node: ObjectNode) => void
  onExpandDetails?: (expanded: boolean) => void
}) {
  const block = workspace.selectedBlock
  if (!block) {
    return (
      <div className="program-panel block-details">
        <div className="object-details-header"><h2>Block</h2></div>
        <p className="block-muted">Select a block on the canvas to set it up.</p>
      </div>
    )
  }
  const catalog = runitVmCatalog()
  const type = catalog.block(block.type)
  const problems = diagnostics.get(block.id) ?? []
  const [activePinIndex, setActivePinIndex] = useState<number | undefined>(undefined)
  const [activePinText, setActivePinText] = useState<string>('')

  const selectedCandidate = useMemo(() => {
    if (!selectedObjectId || !project) return undefined
    return resolveUniversalNode(selectedObjectId, project)
  }, [selectedObjectId, project])

  useEffect(() => {
    setActivePinIndex(undefined)
    setActivePinText('')
  }, [block.id])

  const update = (change: (current: CanvasBlock) => CanvasBlock) => workspace.updateBlock(block.id, change)
  const labelPath = (path: ObjectPath) => pathLabel(path, project, workspace.active?.blocks)
  const handleApplyPath = (pinIndex: number, newText: string) => {
    setActivePinText(newText)
    if (!project) return
    try {
      const parsed = parsePathText(newText.trim(), project)
      update((current) => ({
        ...current,
        inputs: Array.from(
          { length: Math.max(current.inputs?.length ?? 0, pinIndex + 1) },
          (_, i) => (i === pinIndex ? parsed : current.inputs?.[i] ?? null),
        ),
      }))
    } catch {
      // Keep partial text in state so user can continue typing
    }
  }
  const setSetting = (name: string, value: number | string | undefined) => update((current) => {
    const { [name]: _, ...rest } = current.settings ?? {}
    const settings = value === undefined ? rest : { ...rest, [name]: value }
    const { settings: _old, ...without } = current
    return Object.keys(settings).length ? { ...without, settings } : without
  })

  if (!type) {
    return (
      <div className="program-panel block-details">
        <div className="object-details-header"><h2>{block.id}</h2></div>
        <p className="block-muted">'{block.type}' is not a block type of this firmware.</p>
        <button type="button" className="block-delete" onClick={() => workspace.deleteBlock(block.id)}><Trash2 aria-hidden="true" />Delete block</button>
      </div>
    )
  }

  const shape = blockShape(type, block)
  // What the arrangement makes of it: its explicit gates and the loops it runs in.
  const arranged = arrangeProgram(workspace.canvases, { includeDisabled: true })
  const gates = arranged.gates.get(block.id)
  const loopBody = isLoopType(type) ? arranged.blocks.find((entry) => entry.id === block.id)?.body : undefined
  const members = [...arranged.gates].filter(([, entry]) => entry.loops.includes(block.id)).length
  const fields = type.fields.filter((field) => field.source === 'user' && !field.flexible && !field.idKind && !field.letUserSelectAvailable && !field.hiddenByDefault && !(type.encoding && EXPRESSION_COUNTS.has(field.name)))
  const hardware = type.fields.some((field) => field.idKind === 'device')
  const inputsVary = type.inputs.max > Math.max(type.inputs.min, type.inputs.pins.length)
  const outputsVary = type.outputs.max > Math.max(type.outputs.min, type.outputs.pins.length)
  const isLoop = isLoopType(type)
  const expression = block.expression

  return (
    <div className="program-panel block-details">
      <div className="object-details-header block-details-header">
        <span className={`block-details-swatch cat-${type.category}`} aria-hidden="true" />
        <div>
          <h2>{type.title}</h2>
          <div className="block-name-row">
            <input
              className="block-name-field"
              key={`${block.id}:${block.name ?? ''}`}
              defaultValue={block.name ?? ''}
              placeholder="Name this block"
              aria-label="Block name"
              onKeyDown={(event) => {
                if (event.key === 'Enter') event.currentTarget.blur()
                if (event.key === 'Escape') { event.currentTarget.dataset.cancel = 'true'; event.currentTarget.blur() }
              }}
              onBlur={(event) => {
                if (event.currentTarget.dataset.cancel === 'true') {
                  event.currentTarget.value = block.name ?? ''
                  delete event.currentTarget.dataset.cancel
                  return
                }
                const name = event.currentTarget.value.trim()
                if (name !== (block.name ?? '')) update((current) => ({ ...current, name: name || undefined }))
              }}
            />
            <span className="block-name-edit" title="Editable block name" aria-hidden="true"><Pencil /></span>
          </div>
        </div>
      </div>
      <section className="block-section">
        <h3>Running</h3>
        <ul className="block-gates" aria-label="Enabled by">
          {(block.enables ?? []).map((path, index) => (
            <li key={`own${index}`}>
              <span className="block-gate-dot is-own" aria-hidden="true" />
              <BlockAlias path={path} blocks={workspace.active?.blocks ?? []} label={(() => {
                const owner = path.root.slice(0, path.root.lastIndexOf(':'))
                const loop = path.root.endsWith(':body') || (path.root.endsWith(':eno') && workspace.active?.blocks.some((entry) => entry.id === owner && entry.type === 'FOR'))
                return loop ? `In the loop of ${workspace.active?.blocks.find((entry) => entry.id === owner)?.name || owner}` : labelPath(path)
              })()} onRename={(blockId, name, outputIndex) => workspace.updateBlock(blockId, (current) => outputIndex === undefined
                ? { ...current, name }
                : { ...current, outputAliases: Array.from({ length: Math.max(current.outputAliases?.length ?? 0, outputIndex + 1) }, (_, at) => at === outputIndex ? name ?? null : current.outputAliases?.[at] ?? null) })} />
              <button type="button" aria-label={`Remove enable ${labelPath(path)}`} title="Remove this enable" onClick={() => update((current) => disconnect(current, { block: current.id, kind: 'en' }, index))}><X aria-hidden="true" /></button>
            </li>
          ))}
          {(gates?.loops ?? []).filter((loop) => !(block.enables ?? []).some((path) => path.root === `${loop}:body` || path.root === `${loop}:eno`)).map((loop) => (
            <li key={`loop${loop}`} className="is-inherited"><span className="block-gate-dot is-loop" aria-hidden="true" /><span>In the loop of {loop} <small>from the chain</small></span></li>
          ))}
          {!gates?.enables.length && !gates?.loops.length && <li className="is-empty">Always enabled (nothing on EN)</li>}
        </ul>
        <div className="block-fields">
          {(block.enables?.length ?? 0) > 1 && <BlockSwitch label="Enables combine" value={block.enableMode ?? 'any'} options={[["any", "Any (OR)"], ["all", "All (AND)"]]} onChange={(value) => update((current) => ({ ...current, enableMode: value as 'any' | 'all' }))} />}
          <BlockSwitch label="On error" value={block.onError ?? 'stop'} options={[["stop", "Stop"], ["continue", "Continue"]]} onChange={(value) => update((current) => ({ ...current, onError: value as 'stop' | 'continue' }))} />
        </div>
      </section>

      <p className="block-description">{type.description}</p>
      <p className="block-muted">Runs: {type.activation === 'triggered' ? 'when an input it reads is fresh' : type.activation === 'enable-rising' ? 'once each time it is enabled' : 'every cycle while enabled'}</p>

      {problems.length > 0 && (
        <ul className="block-problems">
          {problems.map((problem, index) => (
            <li key={index} className={problem.severity === 'error' ? 'is-error' : 'is-warning'}>
              {problem.severity === 'error' ? <AlertCircle aria-hidden="true" /> : <AlertTriangle aria-hidden="true" />}
              <span>{problemText(problem.message, block.id, type.title, shape, project)}</span>
            </li>
          ))}
        </ul>
      )}

      {(fields.length > 0 || hardware || type.encoding || isLoop) && (
        <section className="block-section">
          <h3>Settings</h3>
          {hardware && <BlockHardwareFields block={block} type={type} devices={devices} deviceCatalog={deviceCatalog} onUpdate={update} />}
          <div className="block-fields">
            {fields.map((field) => {
              const raw = block.settings?.[field.name]
              if (field.enumRef) {
                const choices = enumChoices(type, field)
                const current = typeof raw === 'number' ? choices.find((choice) => choice.value === raw) : choices.find((choice) => choice.label === raw) ?? (raw === undefined ? choices.find((choice) => choice.value === 0) : undefined)
                return (
                  <label key={field.name} className="block-field">
                    <span>{humanize(field.name)}</span>
                    <select value={current?.label ?? ''} onChange={(event) => setSetting(field.name, event.target.value)}>
                      {!current && <option value="">{String(raw)}?</option>}
                      {choices.map((choice) => <option key={choice.value} value={choice.label}>{choice.label}</option>)}
                    </select>
                    {field.description && <em>{field.description}</em>}
                  </label>
                )
              }
              return (
                <NumberField
                  key={field.name}
                  label={humanize(field.name)}
                  hint={field.description}
                  value={typeof raw === 'number' ? raw : undefined}
                  integer={isInteger(field)}
                  min={field.cType.startsWith('uint') ? 0 : undefined}
                  onCommit={(value) => setSetting(field.name, value)}
                />
              )
            })}
            {isLoop && members > 0 && <p className="block-muted">Body: {loopBody} blocks, the ones its ENO is on (and what depends on them).</p>}
            {isLoop && members === 0 && (
              <NumberField label="Body" hint="Drag its green ENO onto the EN of the first block to repeat, or count the blocks after it here" value={block.body} integer min={0} onCommit={(value) => update((current) => {
                const { body: _, ...rest } = current
                return value === undefined ? rest : { ...rest, body: value }
              })} />
            )}
          </div>
          {type.encoding && (
            <ExpressionEditor
              key={`${block.id}:${JSON.stringify(expression ?? null)}`}
              block={block}
              type={type}
              inputCount={shape.inputs.length}
              labelOf={labelPath}
              onApply={(next, inputsNeeded) => update((current) => ({
                ...current,
                expression: { ...(next.constants.length ? { constants: next.constants } : {}), code: next.code },
                ...(inputsNeeded > shape.inputs.length ? { inputs: Array.from({ length: inputsNeeded }, (_, index) => current.inputs?.[index] ?? null) } : {}),
              }))}
            />
          )}
        </section>
      )}

      <section className="block-section">
        <h3>Pins</h3>
        {inputsVary && (
          <Counter label="Inputs" value={shape.inputs.length} min={Math.max(type.inputs.min, type.inputs.pins.length)} max={type.inputs.max} onChange={(count) => update((current) => ({ ...current, inputs: Array.from({ length: count }, (_, index) => current.inputs?.[index] ?? null) }))} />
        )}
        {outputsVary && (
          <Counter label="Outputs" value={shape.outputs.length} min={Math.max(type.outputs.min, type.outputs.pins.length)} max={type.outputs.max} onChange={(count) => update((current) => ({ ...current, outputs: Array.from({ length: count }, (_, index) => current.outputs?.[index] ?? null) }))} />
        )}
        <dl className="block-pins">
          {shape.inputs.map((pin) => {
            const isEditingThis = activePinIndex === pin.index
            return (
              <div key={`in${pin.index}`} className={isEditingThis ? 'is-editing-pin' : ''}>
                <dt><span className="block-pin-title">{pin.title}{pin.required && <small>required</small>}</span><TypeBadge type={pin.value} /></dt>
                <dd
                  className={block.inputs?.[pin.index] ? '' : 'is-unwired'}
                  onDragOver={(e) => {
                    e.preventDefault()
                    e.dataTransfer.dropEffect = 'copy'
                  }}
                  onDrop={(e) => {
                    e.preventDefault()
                    const dropped = resolveUniversalDrop(e.dataTransfer, project)
                    if (dropped) {
                      handleApplyPath(pin.index, dropped.pathText)
                      setActivePinIndex(pin.index)
                      onExpandDetails?.(true)
                    }
                  }}
                >
                  {(() => {
                    const path = block.inputs?.[pin.index]
                    const wired = path && sourceOf(path, new Map(workspace.active?.blocks.map((entry) => [entry.id, entry]) ?? []))
                    if (path && !wired) {
                      return (
                        <PathField
                          path={path}
                          project={project}
                          isEditing={isEditingThis}
                          externalText={isEditingThis ? activePinText : undefined}
                          selectedCandidate={selectedCandidate ? {
                            id: selectedCandidate.id,
                            name: selectedCandidate.name,
                            fullPath: selectedCandidate.fullPath,
                            kind: selectedCandidate.is2D ? 'array2d' : 'value',
                            is2D: selectedCandidate.is2D,
                            is1D: selectedCandidate.is1D,
                          } : undefined}
                          onClearSelectedObject={onClearSelectedObject}
                          onFocus={() => {
                            setActivePinIndex(pin.index)
                            const current = pathLabel(path, project)
                            setActivePinText(current)
                            onExpandDetails?.(true)
                          }}
                          onChangeText={(val) => {
                            setActivePinText(val)
                          }}
                          onCommit={(next) =>
                            update((current) => ({
                              ...current,
                              inputs: (current.inputs ?? []).map((entry, index) => (index === pin.index ? next : entry)),
                            }))
                          }
                        />
                      )
                    }
                    return (
                      <span
                        className="block-unwired-label"
                        title={selectedCandidate ? `Click to assign "${selectedCandidate.name}"` : 'Click to assign variable'}
                        onClick={() => {
                          if (selectedCandidate) {
                            handleApplyPath(pin.index, selectedCandidate.pathText)
                            setActivePinIndex(pin.index)
                            onExpandDetails?.(true)
                            onClearSelectedObject?.()
                          } else {
                            update((current) => ({
                              ...current,
                              inputs: Array.from(
                                { length: Math.max(current.inputs?.length ?? 0, pin.index + 1) },
                                (_, i) => (i === pin.index ? { root: '' } : current.inputs?.[i] ?? null),
                              ),
                            }))
                            setActivePinIndex(pin.index)
                            setActivePinText('')
                            onExpandDetails?.(true)
                          }
                        }}
                      >
                        {selectedCandidate ? `← assign "${selectedCandidate.name}"` : (path ? `← ${labelPath(path)}` : '← not wired')}
                      </span>
                    )
                  })()}
                  {block.inputs?.[pin.index] && (
                    <button
                      type="button"
                      aria-label={`Unwire ${pin.title}`}
                      title="Unwire"
                      onClick={() => {
                        if (activePinIndex === pin.index) {
                          setActivePinIndex(undefined)
                          onExpandDetails?.(false)
                        }
                        update((current) => disconnect(current, { block: current.id, kind: 'in', index: pin.index }))
                      }}
                    >
                      <X aria-hidden="true" />
                    </button>
                  )}
                </dd>
              </div>
            )
          })}
          {shape.outputs.map((pin, index) => (
            <div key={`out${index}`} className="is-output">
              <dt><span className="block-pin-title">{pin.title}</span><TypeBadge type={pin.value} /></dt>
              <dd
                className={`block-output-dd ${block.outputs?.[index] ? 'is-wired' : 'is-unwired'}`}
                title={selectedCandidate ? `Click to write output to "${selectedCandidate.name}"` : undefined}
                onDragOver={(e) => {
                  e.preventDefault()
                  e.dataTransfer.dropEffect = 'copy'
                }}
                onDrop={(e) => {
                  e.preventDefault()
                  const dropped = resolveUniversalDrop(e.dataTransfer, project)
                  if (dropped) {
                    update((current) => ({
                      ...current,
                      outputs: Array.from(
                        { length: Math.max(current.outputs?.length ?? 0, index + 1) },
                        (_, at) => (at === index ? dropped.targetId : current.outputs?.[at] ?? null),
                      ),
                    }))
                  }
                }}
                onClick={() => {
                  if (selectedCandidate) {
                    update((current) => ({
                      ...current,
                      outputs: Array.from(
                        { length: Math.max(current.outputs?.length ?? 0, index + 1) },
                        (_, at) => (at === index ? selectedCandidate.targetId : current.outputs?.[at] ?? null),
                      ),
                    }))
                    onClearSelectedObject?.()
                  }
                }}
              >
                <span className="block-output-flow">
                  <span className="block-output-arrow">→</span>
                  {block.outputs?.[index] ? (
                    <span className="block-output-target" title={`Writes to ${pathLabel({ root: block.outputs[index]! }, project, workspace.active?.blocks)}`}>
                      {pathLabel({ root: block.outputs[index]! }, project, workspace.active?.blocks)}
                    </span>
                  ) : (
                    <span className="block-output-default" title="Default block output">
                      {selectedCandidate ? `← write to "${selectedCandidate.name}"` : `${block.name || block.id}.${pin.title}`}
                    </span>
                  )}
                </span>
                {block.outputs?.[index] && (
                  <button
                    type="button"
                    aria-label={`Stop writing ${labelPath({ root: block.outputs[index]! })}`}
                    title="Unwire output variable"
                    onClick={(e) => {
                      e.stopPropagation()
                      update((current) => disconnect(current, { block: current.id, kind: 'out', index }))
                    }}
                  >
                    <X aria-hidden="true" />
                  </button>
                )}
              </dd>
            </div>
          ))}
        </dl>
        {activePinIndex !== undefined && (
          <PinHelpersPanel
            pinTitle={shape.inputs.find((p) => p.index === activePinIndex)?.title ?? `Input ${activePinIndex + 1}`}
            pinType={shape.inputs.find((p) => p.index === activePinIndex)?.value ?? 'any'}
            currentText={activePinText}
            project={project}
            selectedObjectId={selectedObjectId}
            onClearSelectedObject={onClearSelectedObject}
            onCreateVariable={onCreateVariable}
            onApply={(newText) => handleApplyPath(activePinIndex, newText)}
            onClose={() => {
              setActivePinIndex(undefined)
              onExpandDetails?.(false)
            }}
          />
        )}
        <p className="block-muted">Drag from an output (right side), ENO or Body onto another block to wire it; drag a variable from the Variables tab onto a block to use it.</p>
      </section>

      <section className="block-section">
        <h3>Appearance</h3>
        <label className="block-field">
          <span>Block view</span>
          <select value={block.view ?? 'default'} onChange={(event) => update((current) => {
            const { view: _, ...rest } = current
            return event.target.value === 'default' ? rest : { ...rest, view: event.target.value as 'simple' | 'detailed' }
          })}>
            <option value="default">Follow toolbar</option>
            <option value="simple">Always simple</option>
            <option value="detailed">Always detailed</option>
          </select>
          <em>Fixed views are saved with the project.</em>
        </label>
      </section>

      <button type="button" className="block-delete" onClick={() => workspace.deleteBlock(block.id)}><Trash2 aria-hidden="true" />Delete block</button>
    </div>
  )
}
