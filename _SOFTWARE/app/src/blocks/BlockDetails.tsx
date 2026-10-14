import { useEffect, useMemo, useRef, useState } from 'react'
import { Pencil, X } from 'lucide-react'
import { blockEnumMembers } from '../backend/vm/blocks'
import type { BlockDescriptor, StateField } from '../backend/vm/blocks'
import { ChoiceField } from '../components/ChoiceField'
import { NumberField, TextField } from '../components/FormField'
import { SegmentedSwitch } from '../components/SegmentedSwitch/SegmentedSwitch'
import { TypeBadge } from '../components/TypeBadge/TypeBadge'
import { connect, disconnect, parsePathText, pathLabel } from '../domain/canvas'
import type { CanvasBlock, ObjectPath } from '../domain/canvas'
import type { ObjectNode, ProjectDocument } from '../domain/project'
import { blockEncoding } from '../domain/expression'
import { blockShape, descriptorOf, opOf } from './blockModel'
import { ExpressionEditor } from './ExpressionEditor'
import { resolveUniversalDrop, resolveUniversalNode } from './pinAccessors'
import { PinHelpersPanel } from './PinHelpers'
import './Accessors.css'
import './BlockDetails.css'

/*
 * The selected block, as the details panel always showed it: its name as the title, what it is, the Settings (with the device and the
 * operation of a device block), the Pins (what each reads, what each writes) and what the block runs on. Clicking a pin opens the pin
 * helper under the list; a variable selected in the tree is put on a pin with a click.
 */

/** The settings of a block that are not its operation or device: plain numbers and enum choices. */
const settingFields = (descriptor: BlockDescriptor): readonly StateField[] =>
  (descriptor.state?.fields ?? []).filter((field) => field.source === 'user' && !field.array_len && !field.id_kind && !['op', 'idle-op', 'device', 'const', 'idle-const'].includes(field.role ?? ''))

const members = (descriptor: BlockDescriptor, enumName: string): readonly { value: number; label: string }[] =>
  blockEnumMembers(descriptor, enumName).map((member) => ({ value: member.value, label: member.alias || member.name }))

export function BlockDetails({ block, blocks, descriptor, project, selectedObjectId, onClearSelectedObject, onCreateVariable, onChange }: {
  block: CanvasBlock
  /** The blocks of the canvas, to name the pins that are wired to them. */
  blocks: readonly CanvasBlock[]
  descriptor: BlockDescriptor | undefined
  project?: ProjectDocument
  /** The object selected in the tree: a click on a pin puts it there. */
  selectedObjectId?: string
  onClearSelectedObject?: () => void
  onCreateVariable?: (node: ObjectNode) => void
  onChange: (change: (block: CanvasBlock) => CanvasBlock) => void
}) {
  // The pin being edited (its helper is open under the list) and the text typed for it.
  const [activePin, setActivePin] = useState<number>()
  const [activeText, setActiveText] = useState('')
  const pins = useRef<HTMLElement>(null)
  const selected = useMemo(() => (selectedObjectId && project ? resolveUniversalNode(selectedObjectId, project) : undefined), [selectedObjectId, project])

  // The helper belongs to the pin being filled: a press elsewhere closes it (the object tree excepted: a variable is picked there to be used here).
  useEffect(() => {
    if (activePin === undefined) return
    const onPress = (event: PointerEvent) => {
      const target = event.target as HTMLElement | null
      if (pins.current?.contains(target) || target?.closest('.object-tree-palette')) return
      setActivePin(undefined)
    }
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') setActivePin(undefined) }
    document.addEventListener('pointerdown', onPress, true)
    document.addEventListener('keydown', onKey)
    return () => {
      document.removeEventListener('pointerdown', onPress, true)
      document.removeEventListener('keydown', onKey)
    }
  }, [activePin])

  const title = descriptor?.alias ?? `Unknown ${block.type}`
  const shape = blockShape(descriptor, block)
  const op = opOf(descriptor, block)
  const opField = descriptor?.op_field ?? 'op'
  const deviceField = descriptor?.device_field ?? 'device_id'
  const outputTitle = (other: CanvasBlock, index: number) => blockShape(descriptorOf(other.type), other).outputs.find((pin) => pin.index === index)?.title
  const labelOf = (path: ObjectPath) => pathLabel(path, project, blocks, outputTitle)

  const encoding = useMemo(() => blockEncoding(descriptor), [descriptor])
  /** At least `count` input pins (an expression that reads IN3 needs four). */
  const withInputs = (current: CanvasBlock, count: number): CanvasBlock =>
    (current.inputs?.length ?? 0) >= count ? current : { ...current, inputs: Array.from({ length: count }, (_, index) => current.inputs?.[index] ?? null) }

  const set = (name: string, value: number | string | undefined) => onChange((current) => {
    const { [name]: _removed, ...rest } = current.settings ?? {}
    return { ...current, settings: value === undefined ? rest : { ...rest, [name]: value } }
  })
  const setInput = (index: number, path: ObjectPath | null) => onChange((current) => (path ? connect(current, { block: block.id, kind: 'in', index }, path) : disconnect(current, { block: block.id, kind: 'in', index })))
  const setOutput = (index: number, root: string | null) => onChange((current) => (root ? connect(current, { block: block.id, kind: 'out', index }, { root }) : disconnect(current, { block: block.id, kind: 'out', index })))

  /** The text typed (or picked) for an input: a path when it reads as one, nothing yet while it is half typed. */
  const apply = (index: number, text: string) => {
    setActiveText(text)
    if (!text.trim()) return setInput(index, null)
    if (!project) return
    try {
      setInput(index, parsePathText(text.trim(), project))
    } catch {
      // Half-typed text: the helper shows what fits.
    }
  }

  const openPin = (index: number) => {
    const path = block.inputs?.[index]
    setActivePin(index)
    setActiveText(path && path.root ? labelOf(path) : '')
  }

  return (
    <div className="block-details">
      <div className="panel-header block-details-header">
        <span className={`block-details-swatch cat-${descriptor?.category ?? 'unknown'}`} aria-hidden="true" />
        {/* The name is the title: empty, it shows the type's title as the placeholder. */}
        <div className="block-name-row">
          <TextField
            className="block-name-field"
            key={`${block.id}:${block.name ?? ''}`}
            defaultValue={block.name ?? ''}
            placeholder={title}
            title={`${title} block: click to rename`}
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
              if (name !== (block.name ?? '')) onChange((current) => ({ ...current, name: name || undefined }))
            }}
          />
          <span className="block-name-edit" aria-hidden="true"><Pencil /></span>
        </div>
      </div>
      {descriptor && <p className="block-description">{descriptor.description}</p>}

      {descriptor && (descriptor.ops || settingFields(descriptor).length > 0) && (
        <section className="block-section">
          <h3>Settings</h3>
          <div className="block-fields">
            {descriptor.ops && (
              <>
                <label className="block-field is-wide">
                  <span>Operation</span>
                  <ChoiceField<string>
                    placeholder="Choose what the block does"
                    value={op ? String(op.value) : undefined}
                    onChange={(next) => {
                      if (next === undefined) return
                      // The pins follow the operation: what was wired to the old ones goes.
                      onChange((current) => {
                        const { inputs: _inputs, outputs: _outputs, ...rest } = current
                        return { ...rest, settings: { ...(current.settings ?? {}), [opField]: Number(next) } }
                      })
                    }}
                    choices={descriptor.ops.map((entry) => ({ value: String(entry.value), label: entry.alias, description: entry.description }))}
                  />
                  {op && <em>{op.description}</em>}
                </label>
                <label className="block-field">
                  <span>Device</span>
                  <NumberField integer min={0} value={Number(block.settings?.[deviceField] ?? 0)} onCommit={(id) => set(deviceField, id)} />
                  <em>The device ID on the board.</em>
                </label>
                {descriptor.ops.some((entry) => entry.idle_allowed) && (
                  <label className="block-field is-wide">
                    <span>When not enabled</span>
                    <ChoiceField<string>
                      value={block.settings?.idle_op === undefined ? undefined : String(block.settings.idle_op)}
                      emptyLabel="Leave the device as it is"
                      onChange={(next) => set('idle_op', next === undefined ? undefined : Number(next))}
                      choices={descriptor.ops.filter((entry) => entry.idle_allowed).map((entry) => ({ value: String(entry.value), label: entry.alias }))}
                    />
                  </label>
                )}
              </>
            )}
            {settingFields(descriptor).map((field) => {
              const label = field.alias ?? field.name
              const choices = field.enum_ref ? members(descriptor, field.enum_ref) : []
              const value = block.settings?.[field.name]
              return choices.length > 0 ? (
                <label className="block-field" key={field.name}>
                  <span>{label}</span>
                  <ChoiceField<string> value={value === undefined ? undefined : String(value)} emptyLabel="Not set" onChange={(next) => set(field.name, next === undefined ? undefined : Number(next))} choices={choices.map((choice) => ({ value: String(choice.value), label: choice.label }))} />
                  {field.description && <em>{field.description}</em>}
                </label>
              ) : (
                <label className="block-field" key={field.name}>
                  <span>{label}</span>
                  <NumberField integer value={Number(value ?? 0)} onCommit={(next) => set(field.name, next)} />
                  {field.description && <em>{field.description}</em>}
                </label>
              )
            })}
          </div>
        </section>
      )}

      {encoding && (
        <section className="block-section">
          <ExpressionEditor
            key={block.id}
            block={block}
            encoding={encoding}
            maxInputs={descriptor?.inputs?.max ?? 16}
            inputCount={shape.inputs.length}
            labelOf={labelOf}
            onApply={(expression, needed) => onChange((current) => ({ ...withInputs(current, needed), expression }))}
            resolveDrop={(data) => {
              const dropped = resolveUniversalDrop(data, project)
              return dropped && { pathText: dropped.pathText }
            }}
            selected={selected && { name: selected.name, pathText: selected.pathText }}
            onLink={(link) => {
              if (!project) return
              let path: ObjectPath
              try {
                path = parsePathText(link.pathText, project)
              } catch {
                return
              }
              onChange((current) => {
                const grown = withInputs(link.expression ? { ...current, expression: link.expression } : current, Math.max(link.inputsNeeded, link.pin + 1))
                return connect(grown, { block: block.id, kind: 'in', index: link.pin }, path)
              })
              if (link.fromSelection) onClearSelectedObject?.()
            }}
          />
        </section>
      )}

      <section className="block-section" ref={pins}>
        <h3>Pins</h3>
        {shape.inputs.length === 0 && shape.outputs.length === 0 && <p className="block-muted">{descriptor?.ops ? 'Choose an operation: it decides the pins.' : 'This block has no pins.'}</p>}
        <dl className="block-pins">
          {shape.inputs.map((pin) => {
            const path = block.inputs?.[pin.index]
            const wired = !!path && path.root !== ''
            return (
              <div key={`in${pin.index}`}>
                <dt><span className="block-pin-title">{pin.title}</span><TypeBadge type={pin.value} /></dt>
                <dd className={wired ? 'is-wired' : 'is-unwired'}>
                  <span
                    className="block-unwired-label"
                    title={selected ? `Click to use "${selected.name}"` : 'Click to choose a variable'}
                    onClick={() => {
                      if (selected) {
                        apply(pin.index, selected.pathText)
                        setActivePin(pin.index)
                        onClearSelectedObject?.()
                      } else openPin(pin.index)
                    }}
                    onDragOver={(event) => { event.preventDefault(); event.dataTransfer.dropEffect = 'copy' }}
                    onDrop={(event) => {
                      event.preventDefault()
                      const dropped = resolveUniversalDrop(event.dataTransfer, project)
                      if (dropped) {
                        apply(pin.index, dropped.pathText)
                        setActivePin(pin.index)
                      }
                    }}
                  >
                    {selected ? `← use "${selected.name}"` : wired ? `← ${labelOf(path!)}` : 'not wired'}
                  </span>
                  {wired && (
                    <button type="button" aria-label={`Unwire ${pin.title}`} title="Unwire" onClick={() => { if (activePin === pin.index) setActivePin(undefined); setInput(pin.index, null) }}>
                      <X aria-hidden="true" />
                    </button>
                  )}
                </dd>
              </div>
            )
          })}
          {shape.outputs.map((pin) => {
            const target = block.outputs?.[pin.index]
            return (
              <div key={`out${pin.index}`} className="is-output">
                <dt><span className="block-pin-title">{pin.title}</span><TypeBadge type={pin.value} /></dt>
                <dd
                  className={`block-output-dd ${target ? 'is-wired' : 'is-unwired'}`}
                  title={selected ? `Click to write the output to "${selected.name}"` : undefined}
                  onDragOver={(event) => { event.preventDefault(); event.dataTransfer.dropEffect = 'copy' }}
                  onDrop={(event) => {
                    event.preventDefault()
                    const dropped = resolveUniversalDrop(event.dataTransfer, project)
                    if (dropped) setOutput(pin.index, dropped.targetId)
                  }}
                  onClick={() => {
                    if (!selected) return
                    setOutput(pin.index, selected.targetId)
                    onClearSelectedObject?.()
                  }}
                >
                  <span className="block-output-flow">
                    <span className="block-output-arrow">→</span>
                    {target
                      ? <span className="block-output-target" title={`Writes to ${labelOf({ root: target })}`}>{labelOf({ root: target })}</span>
                      : <span className="block-output-default" title="The block's own output">{selected ? `← write to "${selected.name}"` : `${block.name || block.id}.${pin.title}`}</span>}
                  </span>
                  {target && (
                    <button type="button" aria-label={`Stop writing ${labelOf({ root: target })}`} title="Unwire the output variable" onClick={(event) => { event.stopPropagation(); setOutput(pin.index, null) }}>
                      <X aria-hidden="true" />
                    </button>
                  )}
                </dd>
              </div>
            )
          })}
        </dl>
        {activePin !== undefined && shape.inputs.some((pin) => pin.index === activePin) && (
          <PinHelpersPanel
            pinTitle={shape.inputs.find((pin) => pin.index === activePin)?.title ?? `Input ${activePin + 1}`}
            pinType={shape.inputs.find((pin) => pin.index === activePin)?.value ?? 'any'}
            currentText={activeText}
            project={project}
            selectedObjectId={selectedObjectId}
            onClearSelectedObject={onClearSelectedObject}
            onCreateVariable={onCreateVariable}
            onApply={(text) => apply(activePin, text)}
            onClose={() => setActivePin(undefined)}
          />
        )}
      </section>

      <section className="block-section">
        {block.enables?.length ? (
          <>
            <div className="block-gates-head">
              <h3>Running when</h3>
              {block.enables.length > 1 && (
                <SegmentedSwitch label="Conditions combine" value={block.enableMode ?? 'any'} options={[['any', 'OR'], ['all', 'AND']] as const} onChange={(mode) => onChange((current) => ({ ...current, enableMode: mode }))} />
              )}
            </div>
            <ul className="block-gates" aria-label="Runs when">
              {block.enables.map((path, index) => (
                <li key={`${path.root}:${index}`}>
                  <span className="block-gate-dot is-own" aria-hidden="true" />
                  <span>{labelOf(path)}</span>
                  <button type="button" aria-label={`Remove condition ${labelOf(path)}`} title="Remove this condition" onClick={() => onChange((current) => disconnect(current, { block: current.id, kind: 'en' }, index))}><X aria-hidden="true" /></button>
                </li>
              ))}
            </ul>
          </>
        ) : (
          <h3>Runs: <span className="block-gates-always">Always</span></h3>
        )}
      </section>

    </div>
  )
}
