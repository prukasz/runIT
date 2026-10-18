import { useEffect, useRef, useState } from 'react'
import { Minus, Plus, X } from 'lucide-react'
import { draftForVariable, toPath, variantOf } from '@engine'
import type { BlockInstance, BlockType, Field, FieldValue, ObjectPath, Source } from '@engine'
import { onboardActions } from '@backend'
import { Button, Card, CardStack, ChoiceField, FieldError, FormRow, NumberField, PanelHeader, SegmentedSwitch, Switch, TextField, TypeLead } from '@ui/components'
import type { Choice } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import { blockDiagnostics, connect, disconnect, groupCount, isBlockTarget, pinsOf, sourceLabel, withGroupCount } from './blocks'
import type { PinTarget } from './blocks'
import { ExpressionEditor } from './ExpressionEditor'
import { PathEditor, pinLead } from './PathEditor'
import { deviceChoicesFor } from './useBlocks'
import type { BlocksState } from './useBlocks'
import type { CodeState } from './useCode'
import { VARIABLE_DRAG_TYPE } from './VariablesExplorer'
import './Blocks.css'

const ANY = 'any'

/** A field's stored value as a choice key (`any`, an option's value as text). */
const keyOf = (value: FieldValue | undefined): string | undefined => (value === undefined ? undefined : String(value))

/** The variable selected in the tree, as a path a pin can read. */
const selectedPath = (code: CodeState): ObjectPath | undefined => {
  const id = code.selected?.variable.id
  const draft = id === undefined ? undefined : draftForVariable(code.tree, id)
  return draft && toPath(draft)
}

/** One setting of a block, by its type: a choice, a number, a switch, a device, a pin, an event, an action. */
function FieldInput({ field, block, blocks, onChange }: { field: Field; block: BlockInstance; blocks: BlocksState; onChange: (value: FieldValue | undefined) => void }) {
  const value = block.values?.[field.id]
  const any = field.any !== undefined ? ANY : undefined
  const choose = (choices: Choice<string>[], empty?: string) => (
    <ChoiceField<string> aria-label={field.alias} value={keyOf(value) ?? (field.default !== undefined ? keyOf(field.default === field.any ? ANY : field.default) : undefined)} choices={choices}
      emptyLabel={empty} placeholder={`Choose ${field.alias.toLowerCase()}`} unavailableLabel={(v) => `${v} (not on the board)`}
      onChange={(next) => onChange(next === undefined ? undefined : next === ANY ? ANY : Number.isNaN(Number(next)) ? next : Number(next))} />
  )
  const anyChoice: Choice<string>[] = any ? [{ value: ANY, label: 'Any' }] : []
  const deviceOf = (): number | undefined => {
    const v = field.deviceField ? block.values?.[field.deviceField] : block.device
    return typeof v === 'number' ? v : undefined
  }
  const { devices } = blocks
  switch (field.type) {
    case 'enum':
      return choose([...anyChoice, ...(field.options ?? []).map((o) => ({ value: String(o.value), label: o.alias, description: o.description }))])
    case 'boolean':
      return <Switch checked={value === true || (value === undefined && field.default === true)} label={field.alias} onChange={(on) => onChange(on)} />
    case 'device':
      return choose([...anyChoice, ...devices.devices.map((d) => ({ value: String(d.id), label: d.name, description: d.type?.label }))])
    case 'pin': {
      const device = deviceOf()
      const info = device === undefined ? undefined : devices.devices.find((d) => d.id === device)
      const count = info?.type?.pins?.count
      if (count === undefined) break
      return choose([...anyChoice, ...Array.from({ length: count }, (_, pin) => ({ value: String(pin), label: devices.pinName(info!.id, pin) }))])
    }
    case 'event': {
      const device = deviceOf()
      const contracts = device === undefined ? [...devices.catalog.contracts.keys()] : devices.devices.find((d) => d.id === device)?.type?.contracts ?? []
      const events = contracts.flatMap((c) => (devices.catalog.contracts.get(c)?.events ?? []).filter((e) => e.code !== undefined).map((e) => ({ value: String(e.code), label: e.label, description: e.description, group: devices.catalog.contracts.get(c)?.label })))
      return choose([...anyChoice, ...events])
    }
    case 'action':
      return choose([
        ...blocks.devices.project.actions.map((a) => ({ value: String(a.id), label: a.name, group: 'The project\'s' })),
        ...onboardActions().map((a) => ({ value: String(a.id), label: a.alias.replace(/^Onboard action:\s*/i, ''), group: 'The board\'s' })),
      ])
    case 'string':
      return <TextField aria-label={field.alias} value={typeof value === 'string' ? value : ''} onChange={(event) => onChange(event.target.value || undefined)} />
    default:
      break
  }
  const integer = field.rawType !== undefined && field.rawType !== 'float' && (field.scale === undefined || field.scale === 1)
  return (
    <span className="block-number">
      <NumberField aria-label={field.alias} integer={integer} min={field.min} max={field.max} value={typeof value === 'number' ? value : typeof field.default === 'number' ? field.default : undefined} placeholder={field.min !== undefined ? `${field.min} or more` : 'A number'} onCommit={(n) => onChange(n)} />
      {field.unit && <span className="block-unit">{field.unit}</span>}
    </span>
  )
}

/** The fields the user sets: the block's and its variant's, not those the variant fixes or hides. */
const settingFields = (type: BlockType, block: BlockInstance): Field[] => {
  const variant = variantOf(type, block.variant)
  return [...type.fields, ...(variant?.fields ?? [])].filter((f) => f.fixed === undefined && !f.hidden && f.type !== 'accessor')
}

/**
 * The right panel of the Code view for the selected block: its name, what it is, its settings (the operation and the device of a
 * device block), its formula, its pins (a click opens what a pin reads; the variable selected in the tree goes on with one click),
 * what it runs on, what the compiler says of it and the board's errors about it.
 */
export function BlockDetails({ blocks, code }: { blocks: BlocksState; code: CodeState }) {
  const block = blocks.selected
  const [active, setActive] = useState<PinTarget>()
  const pinsRef = useRef<HTMLDivElement>(null)
  useEffect(() => setActive(undefined), [block?.id])
  // The pin editor belongs to the pin being filled: a press elsewhere closes it (the variables' tree excepted: a variable is picked there).
  useEffect(() => {
    if (!active) return
    const onPress = (event: PointerEvent) => {
      const target = event.target as HTMLElement | null
      if (pinsRef.current?.contains(target) || target?.closest('.code-explorer')) return
      setActive(undefined)
    }
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') setActive(undefined) }
    document.addEventListener('pointerdown', onPress, true)
    document.addEventListener('keydown', onKey)
    return () => {
      document.removeEventListener('pointerdown', onPress, true)
      document.removeEventListener('keydown', onKey)
    }
  }, [active])
  if (!block) return null
  const type = blocks.catalog.get(block.type)
  if (!type) return <div className="block-details"><PanelHeader title={`Unknown block ${block.type}`} /></div>
  const variant = variantOf(type, block.variant)
  const pins = pinsOf(blocks.catalog, block)!
  const language = blocks.ctx.language(block.type)
  const ctx = blocks.ctx
  const tree = code.tree
  const chosen = selectedPath(code)
  const chosenName = code.selected ? code.selected.variable.name : undefined
  const change = (fn: (b: BlockInstance) => BlockInstance, key?: string) => blocks.update(block.id, fn, key)
  const setValue = (id: string, value: FieldValue | undefined) => change((b) => {
    const { [id]: _old, ...rest } = b.values ?? {}
    return { ...b, values: value === undefined ? rest : { ...rest, [id]: value } }
  }, `block-value:${block.id}:${id}`)
  const diagnostics = blockDiagnostics(blocks.compiled.diagnostics, block.id)
  const fieldProblem = (id: string) => diagnostics.filter((d) => d.where.field === id).map((d) => d.message).join(' ')
  // A required input left unwired is marked on its slot (`pin-unwired`); what else the compiler says of a pin goes under it.
  const pinProblem = (side: 'in' | 'out' | 'en', index: number) => diagnostics.filter((d) => d.where.pin?.side === side && d.where.pin.index === index && d.code !== 'pin-unwired').map((d) => d.message).join(' ')
  const missing = (index: number) => diagnostics.find((d) => d.code === 'pin-unwired' && d.where.pin?.side === 'in' && d.where.pin.index === index)?.message
  const general = diagnostics.filter((d) => !d.where.field && !d.where.pin && !(type.integration !== 'core' && d.code.startsWith('device')))
  const fields = settingFields(type, block)
  const inputGroup = groupCount(type, block, 'inputs')
  const outputGroup = groupCount(type, block, 'outputs')
  const devices = type.integration === 'core' ? [] : deviceChoicesFor(type, blocks.devices.devices)
  const wiredField = (id: string) => {
    const pin = pins.inputs.find((p) => p.pin.field === id)
    const source = pin ? block.inputs[pin.index] : undefined
    return source ? sourceLabel(ctx, source) : undefined
  }

  const setPin = (target: PinTarget, source: Source | null) => change((b) => (source ? connect(b, target, source) : disconnect(b, target)))
  const pathEditor = (target: PinTarget, title: string, value: Parameters<typeof pinLead>[0], path: ObjectPath | undefined) => (
    <PathEditor key={`${target.side}:${target.index}`} tree={tree} title={title} value={value} path={path} selected={chosen}
      onChange={(p) => {
        if (target.side === 'en') {
          if (p) change((b) => connect(b, { block: b.id, side: 'en' }, { kind: 'variable', path: p }))
          return
        }
        setPin(target, p ? { kind: 'variable', path: p } : null)
      }}
      onCreate={(name, t) => code.create(name, t)} onClose={() => setActive(undefined)} />
  )

  return (
    <div className="block-details">
      <PanelHeader icon={<span className={`block-details-swatch cat-${type.category}`} aria-hidden="true" />} title={(
        <TextField className="block-name-field" value={block.name ?? ''} placeholder={type.alias} aria-label="Name of the block" title="The block's name: empty, it is called by its type"
          onChange={(event) => change((b) => ({ ...b, name: event.target.value || undefined }), `block-name:${block.id}`)} />
      )} />
      <p className="block-description">{variant?.description ?? type.description}</p>
      <CardStack>
        <ErrorsCard match={(target) => isBlockTarget(target, block.id)} />
        {general.length > 0 && <Card title="To fix">{general.map((d, i) => <FieldError key={i}>{d.message}</FieldError>)}</Card>}

        {(type.variants.length > 0 || type.integration !== 'core' || fields.length > 0) && (
          <Card title="Settings">
            <div className="block-fields">
              {type.variants.length > 0 && (
                <FormRow label={type.integration === 'core' ? 'Kind' : 'Operation'} hint={variant?.activation?.text}>
                  <ChoiceField<string> aria-label="Operation" value={variant?.id} choices={type.variants.map((v) => ({ value: v.id, label: v.alias, description: v.description }))}
                    onChange={(next) => next && next !== variant?.id && change((b) => ({ ...b, variant: next, inputs: [], outputs: [], values: {} }))} />
                </FormRow>
              )}
              {type.integration !== 'core' && (
                <FormRow label="Device" hint={devices.length === 0 ? `No device of the project can do this: add one in Devices.` : undefined}>
                  <ChoiceField<number> aria-label="Device" value={block.device ?? undefined} placeholder="Choose the device" choices={devices.map((d) => ({ value: d.id, label: d.name, description: d.type?.label }))}
                    unavailableLabel={() => 'A device not in the project'} onChange={(id) => change((b) => ({ ...b, device: id ?? null }))} />
                  <FieldError>{diagnostics.filter((d) => d.code.startsWith('device')).map((d) => d.message).join(' ')}</FieldError>
                </FormRow>
              )}
              {fields.map((field) => {
                const wired = wiredField(field.id)
                return (
                  <FormRow key={field.id} label={field.alias} note={field.description} hint={wired ? `Read from ${wired} while wired.` : field.hints.find((h) => h.text)?.text}>
                    <FieldInput field={field} block={block} blocks={blocks} onChange={(v) => setValue(field.id, v)} />
                    <FieldError>{fieldProblem(field.id)}</FieldError>
                  </FormRow>
                )
              })}
            </div>
          </Card>
        )}

        {language && (
          <Card title="Formula">
            <ExpressionEditor key={block.id} language={language} text={String((block.editor as { text?: string } | undefined)?.text ?? '')}
              inputCount={pins.inputs.length} maxInputs={inputGroup?.max ?? pins.inputs.length}
              inputLabel={(pin) => {
                const s = block.inputs[pin]
                return s ? sourceLabel(ctx, s) : undefined
              }}
              onApply={(text, needed) => change((b) => ({ ...withGroupCount(type, b, 'inputs', Math.max(needed, groupCount(type, b, 'inputs')?.count ?? 0)), editor: { text } }))}
              selectedName={chosen ? chosenName : undefined}
              accepts={(data) => data.types.includes(VARIABLE_DRAG_TYPE)}
              onLink={(pin, text, needed, dropped) => {
                const id = dropped?.getData(VARIABLE_DRAG_TYPE)
                const draft = id ? draftForVariable(tree, id) : undefined
                const path = (draft && toPath(draft)) ?? chosen
                if (!path) return
                change((b) => {
                  const grown = withGroupCount(type, b, 'inputs', Math.max(needed, groupCount(type, b, 'inputs')?.count ?? 0))
                  return connect({ ...grown, ...(text !== undefined ? { editor: { text } } : {}) }, { block: b.id, side: 'in', index: pin }, { kind: 'variable', path })
                })
              }} />
          </Card>
        )}

        <div ref={pinsRef}>
          <Card title="Pins" actions={(
            <span className="block-group-actions">
              {inputGroup && (
                <span className="block-group" title={`${inputGroup.alias}: ${inputGroup.min} to ${inputGroup.max}`}>
                  <Button size="icon-sm" aria-label={`One ${inputGroup.alias.toLowerCase()} less`} disabled={inputGroup.count <= inputGroup.min} onClick={() => change((b) => withGroupCount(type, b, 'inputs', inputGroup.count - 1))}><Minus aria-hidden="true" /></Button>
                  <span>{inputGroup.count} {inputGroup.alias.toLowerCase()}s</span>
                  <Button size="icon-sm" aria-label={`One ${inputGroup.alias.toLowerCase()} more`} disabled={inputGroup.count >= inputGroup.max} onClick={() => change((b) => withGroupCount(type, b, 'inputs', inputGroup.count + 1))}><Plus aria-hidden="true" /></Button>
                </span>
              )}
              {outputGroup && (
                <span className="block-group" title={`${outputGroup.alias}: ${outputGroup.min} to ${outputGroup.max}`}>
                  <Button size="icon-sm" aria-label={`One ${outputGroup.alias.toLowerCase()} less`} disabled={outputGroup.count <= outputGroup.min} onClick={() => change((b) => withGroupCount(type, b, 'outputs', outputGroup.count - 1))}><Minus aria-hidden="true" /></Button>
                  <span>{outputGroup.count} {outputGroup.alias.toLowerCase()}s</span>
                  <Button size="icon-sm" aria-label={`One ${outputGroup.alias.toLowerCase()} more`} disabled={outputGroup.count >= outputGroup.max} onClick={() => change((b) => withGroupCount(type, b, 'outputs', outputGroup.count + 1))}><Plus aria-hidden="true" /></Button>
                </span>
              )}
            </span>
          )}>
            {pins.inputs.length === 0 && pins.outputs.length === 0 && <p className="block-muted">{type.integration !== 'core' && !variant ? 'Choose an operation: it decides the pins.' : 'This block has no pins.'}</p>}
            <div className="block-pins">
              {pins.inputs.map((p) => {
                const source = block.inputs[p.index]
                const target: PinTarget = { block: block.id, side: 'in', index: p.index }
                const open = active?.side === 'in' && active.index === p.index
                return (
                  <div key={`in${p.index}`} className={`block-pin ${open ? 'is-editing' : ''}`}>
                    <span className="block-pin-name" title={p.pin.description}><TypeLead kind={pinLead(p.pin.value)} title={p.pin.value} />{p.pin.alias}</span>
                    <span className={`block-pin-source ${source ? 'is-wired' : ''} ${missing(p.index) ? 'is-missing' : ''}`} role="button" tabIndex={0}
                      title={`${missing(p.index) ? `${missing(p.index)} ` : ''}${chosen && !source ? `Click to read ${chosenName}` : source ? 'Click to change what it reads' : 'Click to choose a variable'}`}
                      onClick={() => {
                        if (chosen && !source) setPin(target, { kind: 'variable', path: chosen })
                        setActive(target)
                      }}
                      onKeyDown={(event) => { if (event.key === 'Enter') setActive(target) }}
                      onDragOver={(event) => { if (event.dataTransfer.types.includes(VARIABLE_DRAG_TYPE)) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy' } }}
                      onDrop={(event) => {
                        const draft = draftForVariable(tree, event.dataTransfer.getData(VARIABLE_DRAG_TYPE))
                        const path = draft && toPath(draft)
                        if (!path) return
                        event.preventDefault()
                        setPin(target, { kind: 'variable', path })
                      }}>
                      {source ? `← ${sourceLabel(ctx, source)}` : chosen ? `← use ${chosenName}` : p.pin.field ? 'its setting' : missing(p.index) ? 'required' : 'not wired'}
                    </span>
                    {source && <button type="button" className="block-pin-clear" aria-label={`Unwire ${p.pin.alias}`} title="Unwire" onClick={() => { setPin(target, null); if (open) setActive(undefined) }}><X aria-hidden="true" /></button>}
                    <FieldError>{pinProblem('in', p.index)}</FieldError>
                    {open && pathEditor(target, p.pin.alias, p.pin.value, source?.kind === 'variable' ? source.path : undefined)}
                  </div>
                )
              })}
              {pins.outputs.map((p) => {
                const written = block.outputs[p.index]
                const target: PinTarget = { block: block.id, side: 'out', index: p.index }
                const open = active?.side === 'out' && active.index === p.index
                return (
                  <div key={`out${p.index}`} className={`block-pin is-output ${open ? 'is-editing' : ''}`}>
                    <span className="block-pin-name" title={p.pin.description}><TypeLead kind={pinLead(p.pin.value)} title={p.pin.value} />{p.pin.alias}</span>
                    <span className={`block-pin-target ${written ? 'is-wired' : ''}`} role="button" tabIndex={0}
                      title={chosen && !written ? `Click to write it to ${chosenName}` : 'Click to choose the variable it writes'}
                      onClick={() => {
                        if (chosen && !written) setPin(target, { kind: 'variable', path: chosen })
                        setActive(target)
                      }}
                      onKeyDown={(event) => { if (event.key === 'Enter') setActive(target) }}>
                      <span className="block-pin-arrow">→</span>
                      {written ? <strong>{sourceLabel(ctx, { kind: 'variable', path: written.path })}</strong> : <em>{chosen ? `write to ${chosenName}` : `${blocks.nameOf(block.id)}.${p.pin.alias}`}</em>}
                    </span>
                    {written && <button type="button" className="block-pin-clear" aria-label={`Stop writing ${p.pin.alias} to a variable`} title="Stop writing the variable" onClick={() => { setPin(target, null); if (open) setActive(undefined) }}><X aria-hidden="true" /></button>}
                    <FieldError>{pinProblem('out', p.index)}</FieldError>
                    {open && pathEditor(target, p.pin.alias, p.pin.value, written?.path)}
                  </div>
                )
              })}
            </div>
          </Card>

          <Card title={`Run when${type.enable?.alias ? ` (${type.enable.alias})` : ''}${block.enables.length === 0 ? ': Always' : ''}`} help={type.enable?.description ?? type.activation.text}
            actions={block.enables.length > 1 ? <SegmentedSwitch label="Conditions combine" value={block.enableMode ?? 'any'} options={[['any', 'Any'], ['all', 'All']] as const} onChange={(mode) => change((b) => ({ ...b, enableMode: mode }))} /> : undefined}>
            <ul className="block-gates">
              {block.enables.map((s, i) => (
                <li key={i}>
                  <span className="block-gate-dot" aria-hidden="true" />
                  <span>{sourceLabel(ctx, s)}</span>
                  <button type="button" aria-label={`Remove the condition ${sourceLabel(ctx, s)}`} title="Remove this condition" onClick={() => change((b) => disconnect(b, { block: b.id, side: 'en', index: i }))}><X aria-hidden="true" /></button>
                </li>
              ))}
            </ul>
            <FieldError>{diagnostics.filter((d) => d.where.pin?.side === 'en').map((d) => d.message).join(' ')}</FieldError>
            {active?.side === 'en'
              ? pathEditor({ block: block.id, side: 'en' }, 'Run when', 'bool', undefined)
              : <Button size="sm" variant="dashed" onClick={() => (chosen ? change((b) => connect(b, { block: b.id, side: 'en' }, { kind: 'variable', path: chosen })) : setActive({ block: block.id, side: 'en' }))}><Plus aria-hidden="true" />{chosen ? `Run while ${chosenName}` : 'A variable'}</Button>}
          </Card>
        </div>

        <Card>
          <FormRow label="On a fault" note="Both report the error. Skip what follows: after a failed call its When done is off, so the blocks that run on it skip that pass.">
            <SegmentedSwitch label="On a fault" value={block.onError ?? 'continue'} options={[['continue', 'Go on'], ['stop', 'Skip what follows']] as const} onChange={(onError) => change((b) => ({ ...b, onError }))} />
          </FormRow>
          <Button variant="danger" size="sm" onClick={() => blocks.remove([block.id])}>Remove the block</Button>
        </Card>
      </CardStack>
    </div>
  )
}
