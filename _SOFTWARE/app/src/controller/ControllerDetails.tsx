import { useEffect, useState } from 'react'
import { Copy, Trash2, X } from 'lucide-react'
import { BlockSwitch } from '../canvas/blocks/BlockSwitch'
import { Button } from '../components/Button'
import { ChoiceField } from '../components/ChoiceField'
import { NumberField, TextField } from '../components/FormField'
import { PaletteSectionHeader } from '../components/PaletteSectionHeader'
import { PanelHeader } from '../components/PanelHeader'
import { TypeBadge } from '../components/TypeBadge/TypeBadge'
import { OBJECT_DRAG_TYPE } from '../domain/canvas'
import { formatNumber, keyLabel, normalizeKey, optionOf, PAD_CHOICES, PAD_LABELS, padSources, widgetTitle, widgetType } from '../domain/controller'
import type { ControllerWidget, FieldSpec, SlotSpec } from '../domain/controller'
import type { ProjectDocument } from '../domain/project'
import { useDebug } from '../debug/DebugContext'
import { widgetIcon } from './icons'
import type { ControllerWorkspace } from './useControllerWorkspace'
import { controllerVariables, numberOfLive } from './variables'
import type { ControllerVariable } from './variables'
import '../canvas/Canvas.css'

/*
 * Right panel of the Remote view, built from the same parts as a block's
 * details: the variables a widget uses are rows like a block's pins (drop a
 * variable on one, or select it in the Variables tab and click), then its
 * settings as fields, switches and drop-downs, then where it sits on the grid.
 * The type lists them (`widgetType(kind).slots` / `.fields`), so no widget
 * needs panel code of its own.
 */

/** A key field: click it, press the key to use. */
function KeyField({ value, onChange, label }: { value: string; onChange: (key: string) => void; label: string }) {
  const [listening, setListening] = useState(false)
  useEffect(() => {
    if (!listening) return undefined
    const onKey = (event: KeyboardEvent) => {
      event.preventDefault()
      event.stopPropagation()
      if (event.key !== 'Escape') onChange(normalizeKey(event.key))
      setListening(false)
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [listening, onChange])
  return <Button size="sm" aria-label={label} variant={listening ? 'primary' : 'default'} onClick={() => setListening(!listening)}>{listening ? 'Press a key…' : keyLabel(value)}</Button>
}

/** A setting, as the block details draw theirs: a labelled field, a two-way switch, a drop-down. */
function Field({ widget, field, onChange }: { widget: ControllerWidget; field: FieldSpec; onChange: (value: number | string | boolean) => void }) {
  const value = optionOf(widget, field.key)
  const hint = field.hint && <em>{field.hint}</em>
  if (field.type === 'bool') {
    return <div className="block-field is-wide"><BlockSwitch label={field.label} value={value === true ? 'on' : 'off'} options={[['on', 'On'], ['off', 'Off']]} onChange={(next) => onChange(next === 'on')} />{hint}</div>
  }
  if (field.type === 'choice' && field.choices.length === 2) {
    return <div className="block-field is-wide"><BlockSwitch label={field.label} value={String(value)} options={[field.choices[0]!, field.choices[1]!]} onChange={onChange} />{hint}</div>
  }
  if (field.type === 'choice') {
    return (
      <label className="block-field is-wide">
        <span>{field.label}</span>
        <ChoiceField<string> aria-label={field.label} value={String(value)} choices={field.choices.map(([choice, label]) => ({ value: choice, label }))} onChange={(next) => next !== undefined && onChange(next)} />
        {hint}
      </label>
    )
  }
  if (field.type === 'key') return <div className="block-field"><span>{field.label}</span><KeyField label={field.label} value={String(value)} onChange={onChange} /></div>
  if (field.type === 'text') return <label className="block-field"><span>{field.label}</span><TextField value={String(value)} onChange={(event) => onChange(event.target.value)} />{hint}</label>
  return <label className="block-field"><span>{field.label}</span><NumberField value={Number(value)} min={field.min} max={field.max} onCommit={onChange} />{hint}</label>
}

/** A variable of a widget, drawn like a block's output pin: the arrow and the variable's name, a drop target. */
function SlotRow({ widget, slot, variables, selected, onLink, onUseSelected }: {
  widget: ControllerWidget
  slot: SlotSpec
  variables: readonly ControllerVariable[]
  /** The variable selected in the Variables tab: a click on an empty row takes it. */
  selected: ControllerVariable | undefined
  onLink: (id: string | undefined) => void
  onUseSelected: () => void
}) {
  const debug = useDebug()
  const linked = widget.links[slot.key]
  const variable = variables.find((entry) => entry.id === linked)
  const live = linked && debug.active && !debug.stale ? numberOfLive(debug.read(linked)) : undefined
  const problem = linked && !variable
    ? 'This variable is gone or is not a single value.'
    : slot.writes && variable && !variable.mutable ? "A constant: the board will not take the widget's values." : undefined
  return (
    <>
      <div className="is-output">
        <dt><span className="block-pin-title">{slot.label}<small>{slot.writes ? 'sets' : 'shows'}</small></span><TypeBadge type={variable?.type ?? 'number'} /></dt>
        <dd
          className={`block-output-dd ${variable ? 'is-wired' : 'is-unwired'}`}
          title={selected ? `Click to link "${selected.name}"` : 'Drop a variable here'}
          onDragOver={(event) => {
            if (!event.dataTransfer.types.includes(OBJECT_DRAG_TYPE)) return
            event.preventDefault()
            event.dataTransfer.dropEffect = 'copy'
          }}
          onDrop={(event) => {
            const id = event.dataTransfer.getData(OBJECT_DRAG_TYPE)
            if (!id || !variables.some((entry) => entry.id === id)) return
            event.preventDefault()
            onLink(id)
          }}
          onClick={() => { if (!variable && selected) onUseSelected() }}
        >
          <span className="block-output-flow">
            <span className="block-output-arrow">{slot.writes ? '→' : '←'}</span>
            {variable ? (
              <span className="block-output-target" title={variable.name}>{variable.name}</span>
            ) : (
              <span className="block-output-default">{linked ? 'Missing variable' : selected ? `${slot.writes ? '→' : '←'} "${selected.name}"` : 'Drop a variable'}</span>
            )}
          </span>
          {linked && (
            <button type="button" aria-label={`Unlink ${slot.label}`} title="Unlink the variable" onClick={(event) => { event.stopPropagation(); onLink(undefined) }}><X aria-hidden="true" /></button>
          )}
        </dd>
      </div>
      {(problem ?? slot.hint ?? live !== undefined) && (
        <p className="block-muted controller-slot-note">{problem ?? slot.hint}{live !== undefined && <>{problem || slot.hint ? ' ' : ''}Now {formatNumber(live, 2)}.</>}</p>
      )}
    </>
  )
}

/** Which control of an Xbox gamepad drives the widget: the automatic one (by place on the layout), none, or one chosen. */
function GamepadSection({ widget, widgets, onChange }: { widget: ControllerWidget; widgets: readonly ControllerWidget[]; onChange: (pad: string | undefined) => void }) {
  const choices = PAD_CHOICES[widget.kind]
  if (!choices) return null
  const automatic = padSources(widgets.map((entry) => (entry.id === widget.id ? { ...entry, pad: undefined } : entry))).get(widget.id)
  const options = [
    { value: 'auto', label: automatic ? `Automatic (${PAD_LABELS[automatic]})` : 'Automatic (none left)' },
    { value: 'off', label: 'None' },
    ...choices.map((source) => ({ value: source, label: PAD_LABELS[source] })),
  ]
  return (
    <section className="block-section">
      <h3>Gamepad</h3>
      <label className="block-field is-wide">
        <span>Driven by</span>
        <ChoiceField<string> aria-label="Gamepad control" value={widget.pad ?? 'auto'} choices={options} onChange={(next) => next !== undefined && onChange(next === 'auto' ? undefined : next)} />
        <em>An Xbox gamepad works while the interface runs: press a button on it once so the browser sees it.</em>
      </label>
    </section>
  )
}

/** The settings most widgets do not need: a heading like the palette's sections, closed until opened. */
function AdvancedSection({ count, children }: { count: number; children: React.ReactNode }) {
  const [open, setOpen] = useState(false)
  return (
    <section className="block-section controller-advanced">
      <PaletteSectionHeader label="Advanced" count={count} open={open} onToggle={() => setOpen(!open)} />
      {open && children}
    </section>
  )
}

export function ControllerDetails({ workspace, project, selectedVariableId }: {
  workspace: ControllerWorkspace
  project: ProjectDocument
  /** The variable selected in the left tree, if any. */
  selectedVariableId: string | null | undefined
}) {
  const widget = workspace.selected
  const variables = controllerVariables(project)
  if (!widget) {
    return (
      <div className="program-panel block-details controller-details">
        <PanelHeader title="Widget settings" />
        <p className="block-description">Select a widget to link it to a variable and set its range and behaviour.</p>
        <ul className="block-muted controller-help-list">
          <li>Drag a widget from the palette onto the grid, or click it.</li>
          <li>Drop a variable from the Variables tab on a widget, or on its row here, to link it.</li>
          <li>Drag a widget to move it, its corner to resize it, Delete removes it.</li>
          <li>Enable uploads the program and makes the widgets work; the layout is locked meanwhile.</li>
        </ul>
        <p className="block-muted">{workspace.controller.widgets.length} widget{workspace.controller.widgets.length === 1 ? '' : 's'}.</p>
      </div>
    )
  }
  const type = widgetType(widget.kind)
  const Icon = widgetIcon(widget.kind)
  const selected = variables.find((entry) => entry.id === selectedVariableId)
  // The settings most widgets need stay open; the rest sit behind "Advanced". Position and size are set on the grid.
  const hidden = new Set(type.advanced ?? [])
  const basic = type.fields.filter((field) => !hidden.has(field.key))
  const advanced = type.fields.filter((field) => hidden.has(field.key))
  const basicSlots = type.slots.filter((slot) => !hidden.has(slot.key))
  const advancedSlots = type.slots.filter((slot) => hidden.has(slot.key))
  const slotRow = (slot: SlotSpec) => (
    <SlotRow key={slot.key} widget={widget} slot={slot} variables={variables} selected={selected} onLink={(id) => workspace.link(widget.id, slot.key, id)} onUseSelected={() => selected && workspace.link(widget.id, slot.key, selected.id)} />
  )
  const renderField = (field: FieldSpec) => <Field key={field.key} widget={widget} field={field} onChange={(value) => workspace.option(widget.id, field.key, value)} />
  return (
    <div className="program-panel block-details controller-details">
      <PanelHeader icon={<Icon aria-hidden="true" />} title={widgetTitle(widget)}>
        <Button size="icon-sm" aria-label="Duplicate widget" title="Duplicate" onClick={() => workspace.duplicate(widget.id)}><Copy aria-hidden="true" /></Button>
        <Button size="icon-sm" variant="danger" aria-label="Delete widget" title="Delete (Delete key)" onClick={() => workspace.remove(widget.id)}><Trash2 aria-hidden="true" /></Button>
      </PanelHeader>
      <p className="block-description">{type.description}{type.keyboard ? ' It needs a keyboard, so it does nothing on a phone.' : ''}</p>

      <section className="block-section">
        <label className="block-field is-wide"><span>Name</span><TextField value={widget.name ?? ''} placeholder={type.title} onChange={(event) => workspace.rename(widget.id, event.target.value)} /></label>
      </section>

      {basicSlots.length > 0 && (
        <section className="block-section">
          <h3>Variables</h3>
          <dl className="block-pins">{basicSlots.map(slotRow)}</dl>
          {variables.length === 0 && <p className="block-muted">No single-value variable yet: add one in the Variables tab.</p>}
        </section>
      )}

      {basic.length > 0 && (
        <section className="block-section">
          <h3>Behaviour</h3>
          <div className="block-fields">{basic.map(renderField)}</div>
        </section>
      )}

      <GamepadSection widget={widget} widgets={workspace.controller.widgets} onChange={(pad) => workspace.pad(widget.id, pad)} />

      {advanced.length + advancedSlots.length > 0 && (
        <AdvancedSection count={advanced.length + advancedSlots.length}>
          {advancedSlots.length > 0 && <dl className="block-pins">{advancedSlots.map(slotRow)}</dl>}
          {advanced.length > 0 && <div className="block-fields">{advanced.map(renderField)}</div>}
        </AdvancedSection>
      )}
    </div>
  )
}
