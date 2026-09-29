import type { DeviceCatalog } from '../../domain/descriptors'
import { PanelHeader } from '../../components/PanelHeader'
import { Button } from '../../components/Button'
import { SelectField, TextField } from '../../components/FormField'
import { InlineRename } from '../../components/InlineRename'
import { AlertCircle, AlertTriangle, Minus, Pencil, Plus, Trash2, X } from 'lucide-react'
import { arrangeProgram, disconnect, nameIds, parsePathText, pathLabel, PathTextError, sourceOf } from '../../domain/canvas'
import type { Diagnostic } from '../../domain/compiler'
import { enumMemberLabels, runitVmCatalog } from '../../domain/descriptors'
import type { VmBlockField, VmBlockType } from '../../domain/descriptors'
import type { CanvasBlock, ObjectNode, ObjectPath, ProjectDevice, ProjectDocument } from '../../domain/project'
import { findObject } from '../../domain/project'
import { useEffect, useMemo, useRef, useState } from 'react'
import { blockShape } from './blockView'
import { ExpressionEditor } from './ExpressionEditor'
import { BlockHardwareFields } from './BlockHardwareFields'
import { BlockSwitch } from './BlockSwitch'
import { TypeBadge } from '../../components/TypeBadge/TypeBadge'
import { PinHelpersPanel } from './PinHelpers'
import { arrayDims, parseChain, resolveChain } from './accessorChain'
import { resolveUniversalDrop, resolveUniversalNode } from './pinAccessors'
import type { VariableCandidate } from './pinAccessors'
export { mapPinTypeToVmType, isValidVariableName, nextVarName, resolveUniversalDrop, resolveUniversalNode } from './pinAccessors'
import type { CanvasWorkspace } from '../workspace/useCanvasWorkspace'
import '../Canvas.css'

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
const enumChoices = (type: VmBlockType, field: VmBlockField): { label: string; value: number }[] => enumMemberLabels(type.enums.get(field.enumRef!) ?? [])


const isInteger = (field: VmBlockField): boolean => field.cType !== 'float' && !field.cType.endsWith('_u')

/** A number field that applies its value on Enter or blur; empty = back to 0 (the setting is dropped). */
function NumberField({ label, hint, value, integer, min, onCommit }: { label: string; hint?: string; value: number | undefined; integer: boolean; min?: number; onCommit: (value: number | undefined) => void }) {
  return (
    <label className="block-field">
      <span>{label}</span>
      <TextField
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
    .replace(new RegExp(`block:${id}:(in|en)(\\d+)`, 'g'), (_, kind: string, index: string) => (kind === 'in' ? shape.inputs.find((pin) => pin.index === Number(index))?.title ?? `Input ${index}` : `Run when ${Number(index) + 1}`))

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
      const chain = parseChain(trimmed, project)
      const reached = resolveChain(project, chain)
      if (reached.missing === 'member') return reached.reason ?? ''
      const target = findObject(project, parsed.root)?.node
      if (target?.kind === 'folder') {
        const stepsCount = (parsed.steps ?? []).filter((s) => s.kind === 'index' || s.kind === 'dynamic' || s.kind === 'name').length
        const dims = arrayDims(target, project)
        if (dims && dims.length > 1) {
          if (stepsCount < dims.length) {
            return `${dims.length}D array '${target.name}' must be indexed with ${'[n]'.repeat(dims.length)}`
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
      <TextField
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
  return editing ? <InlineRename className="block-alias-inline" label={outputIndex === undefined ? `Alias for ${source.id}` : `Alias for ${source.id} output ${outputIndex}`} value={current} placeholder={outputIndex === undefined ? source.id : `q${outputIndex}`} onCommit={commit} onCancel={() => setEditing(false)} />
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
  const [activePinIndex, setActivePinIndex] = useState<number | undefined>(undefined)
  const [activePinText, setActivePinText] = useState<string>('')

  const selectedCandidate = useMemo(() => {
    if (!selectedObjectId || !project) return undefined
    return resolveUniversalNode(selectedObjectId, project)
  }, [selectedObjectId, project])

  useEffect(() => {
    setActivePinIndex(undefined)
    setActivePinText('')
  }, [block?.id])

  if (!block) {
    return (
      <div className="program-panel block-details">
        <PanelHeader title="Block" />
        <p className="block-muted">Select a block on the canvas to set it up.</p>
      </div>
    )
  }
  const catalog = runitVmCatalog()
  const type = catalog.block(block.type)
  const problems = diagnostics.get(block.id) ?? []

  const update = (change: (current: CanvasBlock) => CanvasBlock) => workspace.updateBlock(block.id, change)
  const labelPath = (path: ObjectPath) => pathLabel(path, project, workspace.active?.blocks)
  const handleApplyPath = (pinIndex: number, newText: string, known?: ObjectPath) => {
    setActivePinText(newText)
    if (!project) return
    try {
      const parsed = known ?? parsePathText(newText.trim(), project)
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
  /** A variable put in an expression's formula: wire input `pin` to it, and store the formula when it is valid (one undo step). */
  const linkInput = (link: { pin: number; pathText: string; path?: ObjectPath; inputsNeeded: number; expression?: { constants: readonly number[]; code: readonly (string | number)[] }; fromSelection?: boolean }) => {
    let parsed: ObjectPath | undefined = link.path
    if (!parsed) try { parsed = project ? parsePathText(link.pathText.trim(), project) : undefined } catch { parsed = undefined }
    update((current) => ({
      ...current,
      ...(link.expression ? { expression: { ...(link.expression.constants.length ? { constants: link.expression.constants } : {}), code: link.expression.code } } : {}),
      inputs: Array.from(
        { length: Math.max(current.inputs?.length ?? 0, link.inputsNeeded, link.pin + 1) },
        (_, index) => (index === link.pin ? parsed ?? current.inputs?.[index] ?? null : current.inputs?.[index] ?? null),
      ),
    }))
    // An array or matrix still needs its position: the pin helper opens on it.
    if (!parsed) {
      setActivePinIndex(link.pin)
      setActivePinText(link.pathText)
      onExpandDetails?.(true)
    }
    if (link.fromSelection) onClearSelectedObject?.()
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
        <PanelHeader title={block.id} />
        <p className="block-muted">'{block.type}' is not a block type of this firmware.</p>
        <Button variant="danger" block className="block-delete" onClick={() => workspace.deleteBlock(block.id)}><Trash2 aria-hidden="true" />Delete block</Button>
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
      <div className="panel-header block-details-header">
        <span className={`block-details-swatch cat-${type.category}`} aria-hidden="true" />
        <div>
          <h2>{type.title}</h2>
          <div className="block-name-row">
            <TextField
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
        <ul className="block-gates" aria-label="Runs when">
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
              <button type="button" aria-label={`Remove condition ${labelPath(path)}`} title="Remove this condition" onClick={() => update((current) => disconnect(current, { block: current.id, kind: 'en' }, index))}><X aria-hidden="true" /></button>
            </li>
          ))}
          {(gates?.loops ?? []).filter((loop) => !(block.enables ?? []).some((path) => path.root === `${loop}:body` || path.root === `${loop}:eno`)).map((loop) => (
            <li key={`loop${loop}`} className="is-inherited"><span className="block-gate-dot is-loop" aria-hidden="true" /><span>In the loop of {loop} <small>from the chain</small></span></li>
          ))}
          {!gates?.enables.length && !gates?.loops.length && <li className="is-empty">Always runs (no Run when set)</li>}
        </ul>
        <div className="block-fields">
          {(block.enables?.length ?? 0) > 1 && <BlockSwitch label="Conditions combine" value={block.enableMode ?? 'any'} options={[["any", "Any (OR)"], ["all", "All (AND)"]]} onChange={(value) => update((current) => ({ ...current, enableMode: value as 'any' | 'all' }))} />}
          <BlockSwitch label="On error" value={block.onError ?? 'stop'} options={[["stop", "Stop"], ["continue", "Continue"]]} onChange={(value) => update((current) => ({ ...current, onError: value as 'stop' | 'continue' }))} />
        </div>
      </section>

      <p className="block-description">{type.description}</p>
      <p className="block-muted">Runs: {type.activation === 'triggered' ? 'when an input it reads is fresh' : type.activation === 'enable-rising' ? 'once each time its Run when turns on' : 'every cycle while its Run when is true'}</p>

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
                    <SelectField value={current?.label ?? ''} onChange={(event) => setSetting(field.name, event.target.value)}>
                      {!current && <option value="">{String(raw)}?</option>}
                      {choices.map((choice) => <option key={choice.value} value={choice.label}>{choice.label}</option>)}
                    </SelectField>
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
            {isLoop && members > 0 && <p className="block-muted">Body: {loopBody} blocks, the ones on its Loop body (and what depends on them).</p>}
            {isLoop && members === 0 && (
              <NumberField label="Body" hint="Drag its green Loop body pin onto the Run when of the first block to repeat, or count the blocks after it here" value={block.body} integer min={0} onCommit={(value) => update((current) => {
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
              resolveDrop={(data) => resolveUniversalDrop(data, project)}
              selected={selectedCandidate ? { name: selectedCandidate.name, pathText: selectedCandidate.pathText, path: selectedCandidate.path } : undefined}
              onLink={linkInput}
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
                      handleApplyPath(pin.index, dropped.pathText, dropped.path)
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
                        {selectedCandidate ? `← assign "${selectedCandidate.name}"` : (path ? `← ${labelPath(path)}` : 'not wired')}
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
        <p className="block-muted">Drag from an output (right side), When done or Loop body onto another block to wire it; drag a variable from the Variables tab onto a block to use it.</p>
      </section>

      <section className="block-section">
        <h3>Appearance</h3>
        <label className="block-field">
          <span>Block view</span>
          <SelectField value={block.view ?? 'default'} onChange={(event) => update((current) => {
            const { view: _, ...rest } = current
            return event.target.value === 'default' ? rest : { ...rest, view: event.target.value as 'simple' | 'detailed' }
          })}>
            <option value="default">Follow toolbar</option>
            <option value="simple">Always simple</option>
            <option value="detailed">Always detailed</option>
          </SelectField>
          <em>Fixed views are saved with the project.</em>
        </label>
      </section>

      <Button variant="danger" block className="block-delete" onClick={() => workspace.deleteBlock(block.id)}><Trash2 aria-hidden="true" />Delete block</Button>
    </div>
  )
}
