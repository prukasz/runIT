import type { DeviceCatalog } from '../domain/descriptors'
import { AlertCircle, AlertTriangle, Minus, Plus, Trash2 } from 'lucide-react'
import type { Diagnostic } from '../domain/compiler'
import { outputObjectId } from '../domain/compiler'
import { runitVmCatalog } from '../domain/descriptors'
import type { VmBlockField, VmBlockType } from '../domain/descriptors'
import type { CanvasBlock, ProjectDevice } from '../domain/project'
import { blockShape, pathText } from './blockView'
import { ExpressionEditor } from './ExpressionEditor'
import { BlockHardwareFields } from './BlockHardwareFields'
import { BlockSwitch } from './BlockSwitch'
import type { CanvasWorkspace } from './useCanvasWorkspace'
import './Canvas.css'

/*
 * Right panel of the canvas: the selected block's settings (its state fields
 * from the block descriptor, the expression of EXPR / EXPR_BIT, the loop body
 * of FOR, pin counts where they vary), how it runs (enables, on error, ENO),
 * its pins, and what the compiler says about it. Text fields apply on Enter or
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

/** A compiler message about this block, with its own name dropped and pin keys (`block:<id>:in1`) named. */
const problemText = (message: string, id: string, title: string, shape: ReturnType<typeof blockShape>): string =>
  message
    .replace(new RegExp(`^Block '${id}'( \\(${title}\\))?: `), '')
    .replace(new RegExp(`block:${id}:(in|en)(\\d+)`, 'g'), (_, kind: string, index: string) => (kind === 'in' ? shape.inputs.find((pin) => pin.index === Number(index))?.title ?? `Input ${index}` : `Enable ${Number(index) + 1}`))

export function BlockDetails({ workspace, diagnostics, devices = [], deviceCatalog }: { workspace: CanvasWorkspace; diagnostics: ReadonlyMap<string, readonly Diagnostic[]>; devices?: readonly ProjectDevice[]; deviceCatalog?: DeviceCatalog }) {
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
  const fields = type.fields.filter((field) => field.source === 'user' && !field.flexible && !field.idKind && !field.letUserSelectAvailable && !field.hiddenByDefault && !(type.encoding && EXPRESSION_COUNTS.has(field.name)))
  const hardware = type.fields.some((field) => field.idKind === 'device')
  const inputsVary = type.inputs.max > Math.max(type.inputs.min, type.inputs.pins.length)
  const outputsVary = type.outputs.max > Math.max(type.outputs.min, type.outputs.pins.length)
  const isLoop = type.fields.some((field) => field.source === 'derived' && field.cType === 'vm_span_t')
  const expression = block.expression

  return (
    <div className="program-panel block-details">
      <div className="object-details-header block-details-header">
        <span className={`block-details-swatch cat-${type.category}`} aria-hidden="true" />
        <div>
          <h2>{type.title}</h2>
          <code>{block.id}</code>
        </div>
      </div>
      <section className="block-section">
        <h3>Running</h3>
        <div className="block-fields">
          <BlockSwitch label="Enables combine" value={block.enableMode ?? 'any'} options={[["any", "Any (OR)"], ["all", "All (AND)"]]} onChange={(value) => update((current) => ({ ...current, enableMode: value as 'any' | 'all' }))} />
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
              <span>{problemText(problem.message, block.id, type.title, shape)}</span>
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
            {isLoop && (
              <NumberField label="Body" hint="How many blocks after it are the loop body" value={block.body} integer min={0} onCommit={(value) => update((current) => {
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
              <dt>{pin.title}<small>{pin.value}{pin.required ? ', required' : ''}</small></dt>
              <dd className={block.inputs?.[pin.index] ? '' : 'is-unwired'}>← {block.inputs?.[pin.index] ? pathText(block.inputs[pin.index]!) : 'not wired'}</dd>
            </div>
          ))}
          {shape.outputs.map((pin, index) => (
            <div key={`out${index}`}>
              <dt>{pin.title}<small>{pin.value}</small></dt>
              <dd>→ {block.outputs?.[index] ?? outputObjectId(block.id, index)}</dd>
            </div>
          ))}
        </dl>
        <p className="block-muted">Wiring comes with the block design.</p>
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
