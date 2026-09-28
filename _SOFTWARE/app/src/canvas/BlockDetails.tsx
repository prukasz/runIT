import type { DeviceCatalog } from '../domain/descriptors'
import { AlertCircle, AlertTriangle, Minus, Pencil, Plus, Trash2, X } from 'lucide-react'
import { arrangeProgram, disconnect, nameIds, parsePathText, pathLabel, PathTextError, sourceOf } from '../domain/canvas'
import type { Diagnostic } from '../domain/compiler'
import { runitVmCatalog } from '../domain/descriptors'
import type { VmBlockField, VmBlockType } from '../domain/descriptors'
import type { CanvasBlock, ObjectPath, ProjectDevice, ProjectDocument } from '../domain/project'
import { useState } from 'react'
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
 * ones it takes from the blocks feeding it, always run, on error), its pins
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

/** A pin's variable as typed text (`motor.gains[2]`, `table[sel]`): applied on Enter or blur, a mistake is said under it. */
function PathField({ path, project, onCommit }: { path: ObjectPath; project?: ProjectDocument; onCommit: (path: ObjectPath) => void }) {
  const [problem, setProblem] = useState('')
  const shown = path.root === '' ? '' : pathLabel(path, project)
  return (
    <span className="block-path-field">
      <input
        key={shown}
        defaultValue={shown}
        placeholder="variable, e.g. motor.gains[2]"
        aria-label="Variable path"
        aria-invalid={problem ? 'true' : undefined}
        autoFocus={path.root === ''}
        onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }}
        onBlur={(event) => {
          const text = event.currentTarget.value.trim()
          if (!text || text === shown || !project) return setProblem('')
          try {
            onCommit(parsePathText(text, project))
            setProblem('')
          } catch (error) {
            setProblem(error instanceof PathTextError ? error.message : String(error))
          }
        }}
      />
      {problem && <em className="is-error">{problem}</em>}
    </span>
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

export function BlockDetails({ workspace, diagnostics, devices = [], deviceCatalog, project }: { workspace: CanvasWorkspace; diagnostics: ReadonlyMap<string, readonly Diagnostic[]>; devices?: readonly ProjectDevice[]; deviceCatalog?: DeviceCatalog; project?: ProjectDocument }) {
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
  const update = (change: (current: CanvasBlock) => CanvasBlock) => workspace.updateBlock(block.id, change)
  const labelPath = (path: ObjectPath) => pathLabel(path, project, workspace.active?.blocks)
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
  // What the arrangement makes of it: its gates after inheritance and the loops it runs in.
  const arranged = arrangeProgram(workspace.canvases, { includeDisabled: true })
  const gates = arranged.gates.get(block.id)
  const own = new Set((block.enables ?? []).map((path) => JSON.stringify(path)))
  const inherited = (gates?.enables ?? []).filter((path) => !own.has(JSON.stringify(path)))
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
          <code>{block.name || block.id}</code>
          <input
            className="block-name-field"
            key={`${block.id}:${block.name ?? ''}`}
            defaultValue={block.name ?? ''}
            placeholder="Name this block"
            aria-label="Block name"
            onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }}
            onBlur={(event) => {
              const name = event.currentTarget.value.trim()
              if (name !== (block.name ?? '')) update((current) => ({ ...current, name: name || undefined }))
            }}
          />
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
          {inherited.map((path) => (
            <li key={`from${pathText(path)}`} className="is-inherited" title="Taken from the blocks feeding it: it is in their branch">
              <span className="block-gate-dot" aria-hidden="true" />
              <span><BlockAlias path={path} blocks={workspace.active?.blocks ?? []} label={labelPath(path)} onRename={(blockId, name, outputIndex) => workspace.updateBlock(blockId, (current) => outputIndex === undefined
                ? { ...current, name }
                : { ...current, outputAliases: Array.from({ length: Math.max(current.outputAliases?.length ?? 0, outputIndex + 1) }, (_, at) => at === outputIndex ? name ?? null : current.outputAliases?.[at] ?? null) })} /> <small>from the branch</small></span>
            </li>
          ))}
          {(gates?.loops ?? []).filter((loop) => !(block.enables ?? []).some((path) => path.root === `${loop}:body` || path.root === `${loop}:eno`)).map((loop) => (
            <li key={`loop${loop}`} className="is-inherited"><span className="block-gate-dot is-loop" aria-hidden="true" /><span>In the loop of {loop} <small>from the chain</small></span></li>
          ))}
          {!gates?.enables.length && !gates?.loops.length && <li className="is-empty">Runs every cycle (nothing on EN)</li>}
        </ul>
        <div className="block-fields">
          <BlockSwitch label="Branch" value={block.inheritGates === false ? 'always' : 'follow'} options={[["follow", "Follow feeders"], ["always", "Always run"]]} onChange={(value) => update((current) => {
            const { inheritGates: _, ...rest } = current
            return value === 'always' ? { ...rest, inheritGates: false } : rest
          })} />
          {(block.enables?.length ?? 0) > 1 && <BlockSwitch label="Enables combine" value={block.enableMode ?? 'any'} options={[["any", "Any (OR)"], ["all", "All (AND)"]]} onChange={(value) => update((current) => ({ ...current, enableMode: value as 'any' | 'all' }))} />}
          <BlockSwitch label="On error" value={block.onError ?? 'stop'} options={[["stop", "Stop"], ["continue", "Continue"]]} onChange={(value) => update((current) => ({ ...current, onError: value as 'stop' | 'continue' }))} />
        </div>
        <p className="block-muted">A block runs in the branch of the blocks feeding it. "Always run" ignores that; a gate dropped on its EN narrows it.</p>
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
          {shape.inputs.map((pin) => (
            <div key={`in${pin.index}`}>
              <dt><span className="block-pin-title">{pin.title}{pin.required && <small>required</small>}</span><TypeBadge type={pin.value} /></dt>
              <dd className={block.inputs?.[pin.index] ? '' : 'is-unwired'}>
                {(() => {
                  const path = block.inputs?.[pin.index]
                  const wired = path && sourceOf(path, new Map(workspace.active?.blocks.map((entry) => [entry.id, entry]) ?? []))
                  if (path && !wired) return <PathField path={path} project={project} onCommit={(next) => update((current) => ({ ...current, inputs: (current.inputs ?? []).map((entry, index) => (index === pin.index ? next : entry)) }))} />
                  return <>← {path ? labelPath(path) : 'not wired'}</>
                })()}
                {block.inputs?.[pin.index] && <button type="button" aria-label={`Unwire ${pin.title}`} title="Unwire" onClick={() => update((current) => disconnect(current, { block: current.id, kind: 'in', index: pin.index }))}><X aria-hidden="true" /></button>}
              </dd>
            </div>
          ))}
          {shape.outputs.map((pin, index) => (
            <div key={`out${index}`}>
              <dt><span className="block-pin-title">{pin.title}</span><TypeBadge type={pin.value} /></dt>
              <dd>
                → {block.outputs?.[index] ? pathLabel({ root: block.outputs[index]! }, project, workspace.active?.blocks) : `${block.name || block.id}.${block.outputAliases?.[index] || pin.title}`}
                <input className="block-output-alias" aria-label={`Alias for ${pin.title}`} placeholder={pin.title} defaultValue={block.outputAliases?.[index] ?? ''} key={`${block.id}:${index}:${block.outputAliases?.[index] ?? ''}`} onBlur={(event) => {
                  const alias = event.currentTarget.value.trim() || null
                  if (alias !== (block.outputAliases?.[index] ?? null)) update((current) => ({ ...current, outputAliases: Array.from({ length: Math.max(current.outputAliases?.length ?? 0, index + 1) }, (_, at) => at === index ? alias : current.outputAliases?.[at] ?? null) }))
                }} onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }} />
                {block.outputs?.[index] && <button type="button" aria-label={`Stop writing ${labelPath({ root: block.outputs[index]! })}`} title="Back to its own output" onClick={() => update((current) => disconnect(current, { block: current.id, kind: 'out', index }))}><X aria-hidden="true" /></button>}
              </dd>
            </div>
          ))}
        </dl>
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
