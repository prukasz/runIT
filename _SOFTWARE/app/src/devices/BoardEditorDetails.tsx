import { FlipHorizontal2, RotateCw, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Button, ToggleChip } from '../components/Button'
import { Card } from '../components/Card'
import { FormGrid, FormRow, NumberField, SelectField, TextField } from '../components/FormField'
import { ListableField } from '../components/ListableField'
import { PanelHeader } from '../components/PanelHeader/PanelHeader'
import { connectorType, pinLabel, terminalId } from '../domain/board'
import type { BoardTemplate, PinBinding, SysPinRef } from '../domain/board'
import type { SysPinProvider } from './board/sysPins'
import { FlowBadge } from './BoardWiringDetails'
import type { BoardModel } from './board/boardModel'
import type { BoardEditorApi } from './useBoardEditor'
import './Devices.css'

/*
 * The board creator's right panel: the board (name, ID, size, outline, mount holes), the selected connector (label,
 * position, turn / mirror / remove) and its pins, each bound to what it carries: a power net, a bus signal or an
 * installed sys pin. A bound pin is a terminal of the board: it wires like any other.
 */

const ROLE_OPTIONS = [
  { value: '', label: 'Provider default' },
  { value: 'in', label: 'Consumer (input)' },
  { value: 'out', label: 'Producer (output)' },
  { value: 'inout', label: 'Producer or consumer' },
]

const numberOrUndefined = (text: string): number | undefined => {
  const value = Number(text)
  return text.trim() === '' || !Number.isInteger(value) || value < 0 ? undefined : value
}

const bindingKey = (bind: PinBinding): string => (bind.type === 'net' ? `net:${bind.net}` : bind.type === 'signal' ? `signal:${bind.signal}` : bind.type)

/** The first sys pin nobody uses: not taken by the board and not bound on this draft yet. */
function firstFreeSysPin(providers: readonly SysPinProvider[], draft: BoardTemplate): SysPinRef {
  const used = new Set(draft.connectors.flatMap((connector) => connector.pins).flatMap((pin) => (pin.bind.type === 'sys' ? [`${pin.bind.device}.${pin.bind.pin}`] : [])))
  for (const provider of providers) {
    const free = provider.pins.find((pin) => !pin.takenBy && !used.has(`${provider.device}.${pin.pin}`))
    if (free) return { device: provider.device, pin: free.pin }
  }
  return { device: providers[0]?.device ?? 0, pin: providers[0]?.pins[0]?.pin ?? 0 }
}

/** A provider device and one of its pins. */
function SysPicker({ providers, value, onChange, optional }: { providers: readonly SysPinProvider[]; value: SysPinRef | undefined; onChange: (ref: SysPinRef | undefined) => void; optional?: boolean }) {
  const provider = providers.find((entry) => entry.device === value?.device)
  const pin = provider?.pins.find((entry) => entry.pin === value?.pin)
  return (
    <>
      <FormGrid>
        <FormRow label="Device">
          <ListableField
            aria-label="Provider device"
            value={value ? String(value.device) : ''}
            options={[...(optional ? [{ value: '', label: 'No sys pin' }] : []), ...providers.map((entry) => ({ value: String(entry.device), label: `${entry.name}${entry.installed ? '' : ' (not installed)'}` }))]}
            onChange={(device) => {
              const next = providers.find((entry) => String(entry.device) === device)
              onChange(next ? { device: next.device, pin: next.pins[0]?.pin ?? 0 } : undefined)
            }}
          />
        </FormRow>
        {provider && (
          <FormRow label="Pin">
            <ListableField
              aria-label="Sys pin"
              value={String(value?.pin ?? '')}
              options={provider.pins.map((entry) => ({ value: String(entry.pin), label: `${entry.label}${entry.takenBy ? ' · used' : ''}` }))}
              onChange={(next) => onChange({ device: provider.device, pin: Number(next) })}
            />
          </FormRow>
        )}
      </FormGrid>
      {pin?.takenBy && <p className="devices-muted board-pin-taken">Already used by {pin.takenBy}.</p>}
    </>
  )
}

function PinRow({ editor: e, connectorId, index, model }: { editor: BoardEditorApi; connectorId: string; index: number; model: BoardModel }) {
  const connector = e.draft?.connectors.find((entry) => entry.id === connectorId)
  const type = connector && connectorType(connector.type)
  if (!connector || !type || !e.draft) return null
  const pin = connector.pins[index]
  const bind = pin.bind
  const chosen = e.selection.pin === index
  const nets = Object.entries(model.nets)
  const net = bind.type === 'net' ? model.nets[bind.net] : undefined
  const change = (value: string) => {
    if (value === 'none') return e.setPinBinding(connectorId, index, { type: 'none' })
    if (value === 'sys') return e.setPinBinding(connectorId, index, { type: 'sys', ...firstFreeSysPin(e.providers, e.draft as BoardTemplate) })
    if (value.startsWith('signal:')) return e.setPinBinding(connectorId, index, { type: 'signal', signal: value.slice('signal:'.length) })
    const name = value.slice('net:'.length)
    // A net that can be fed from outside, on a power connector, starts as an input; every other net is provided.
    e.setPinBinding(connectorId, index, { type: 'net', net: name, ...(model.nets[name]?.input && type.group === 'Power' ? { role: 'input' as const } : {}) })
  }

  return (
    <li className={`board-pin-row${chosen ? ' is-chosen' : ''}`} onClick={() => e.setSelection({ connector: connectorId, pin: index })}>
      <div className="board-pin-head">
        <Badge tone="count" size="compact">{index + 1}</Badge>
        <TextField className="form-field-panel" aria-label={`Pin ${index + 1} label`} value={pin.label ?? ''} placeholder={type.pins[index].name} onChange={(event) => e.setPinLabel(connectorId, index, event.target.value)} />
        <FlowBadge spec={model.terminals.get(terminalId(connectorId, index + 1))} />
      </div>
      <SelectField className="form-field-panel" aria-label={`Pin ${index + 1} binding`} value={bindingKey(bind)} onChange={(event) => change(event.target.value)}>
        <option value="none">Not connected</option>
        <optgroup label="Power net">
          {nets.map(([name, spec]) => <option key={name} value={`net:${name}`}>{spec.label}{spec.kind === 'gnd' ? ' (GND)' : ''}</option>)}
        </optgroup>
        <optgroup label="Bus signal">
          {model.signals.map((signal) => <option key={signal.id} value={`signal:${signal.id}`}>{signal.label}</option>)}
        </optgroup>
        <option value="sys">Installed sys pin</option>
      </SelectField>
      {bind.type === 'net' && net?.input && (
        <FormRow label="Power" note="This net can be fed from outside: the pin either provides it (a power output) or takes it in (the board's input).">
          <SelectField className="form-field-panel" aria-label={`Pin ${index + 1} power flow`} value={bind.role === 'input' ? 'input' : 'source'} onChange={(event) => e.setPinBinding(connectorId, index, { type: 'net', net: bind.net, ...(event.target.value === 'input' ? { role: 'input' as const } : {}) })}>
            <option value="source">Provides power</option>
            <option value="input">Takes power in</option>
          </SelectField>
        </FormRow>
      )}
      {bind.type === 'sys' && (
        <>
          <SysPicker providers={e.providers} value={{ device: bind.device, pin: bind.pin }} onChange={(ref) => ref && e.setPinBinding(connectorId, index, { type: 'sys', ...ref, ...(bind.overrides ? { overrides: bind.overrides } : {}) })} />
          <FormGrid>
            <FormRow label="Role" note="Producer drives the signal, consumer receives it, either takes the role the other end needs. Left on the provider default, it comes from the firmware (the pins csv or what the device can do with a pin).">
              <SelectField className="form-field-panel" aria-label={`Pin ${index + 1} role`} value={bind.overrides?.dir ?? ''} onChange={(event) => e.setPinBinding(connectorId, index, { ...bind, overrides: { ...bind.overrides, dir: (event.target.value || undefined) as 'in' | 'out' | 'inout' | undefined } })}>
                {ROLE_OPTIONS.map((option) => <option key={option.value} value={option.value}>{option.label}</option>)}
              </SelectField>
            </FormRow>
            <FormRow label="Max mV" note="What this pin tolerates on this board (an analog input behind a divider takes more than the chip).">
              <TextField className="form-field-panel" inputMode="numeric" aria-label={`Pin ${index + 1} max millivolts`} value={bind.overrides?.mV_max ?? ''} placeholder="chip default" onChange={(event) => e.setPinBinding(connectorId, index, { ...bind, overrides: { ...bind.overrides, mV_max: numberOrUndefined(event.target.value) } })} />
            </FormRow>
          </FormGrid>
        </>
      )}
      {bind.type === 'signal' && (bind.signal.startsWith('i2c') || bind.sys) && (
        <SysPicker providers={e.providers.filter((provider) => provider.device === 0)} optional value={bind.sys} onChange={(ref) => e.setPinBinding(connectorId, index, { type: 'signal', signal: bind.signal, ...(ref ? { sys: ref } : {}) })} />
      )}
    </li>
  )
}

export function BoardEditorDetails({ editor: e }: { editor: BoardEditorApi }) {
  const { draft, selected, model } = e
  if (!draft || !model) {
    return (
      <div className="program-panel devices-details">
        <PanelHeader title="Board creator" />
        <p className="devices-muted">Create a board to start. A board has an outline, mount holes and connectors; each connector pin binds to a power net, a bus signal or an installed sys pin.</p>
      </div>
    )
  }
  const type = selected && connectorType(selected.type)

  return (
    <div className="program-panel devices-details board-editor-details">
      <PanelHeader title="Board">{e.dirty && <Badge tone="warning" size="compact" push>unsaved</Badge>}</PanelHeader>
      <Card title="Board" className="board-editor-card">
        <FormRow label="Name" htmlFor="board-name"><TextField id="board-name" className="form-field-panel" value={draft.name} onChange={(event) => e.setName(event.target.value)} /></FormRow>
        <FormRow label="ID" htmlFor="board-id" note="Lower case letters, digits and _, starting with a letter. The library file is <id>.board.json.">
          <TextField id="board-id" className="form-field-panel" value={draft.id} aria-invalid={!/^[a-z][a-z0-9_]*$/.test(draft.id)} onChange={(event) => e.setId(event.target.value)} />
        </FormRow>
        <FormGrid>
          <FormRow label="Width (mm)" htmlFor="board-width"><NumberField id="board-width" min={10} max={400} className="form-field-panel" value={draft.width} onCommit={(value) => e.setSize(value, draft.height)} /></FormRow>
          <FormRow label="Height (mm)" htmlFor="board-height"><NumberField id="board-height" min={10} max={400} className="form-field-panel" value={draft.height} onCommit={(value) => e.setSize(draft.width, value)} /></FormRow>
          <FormRow label="Corner radius" htmlFor="board-radius"><NumberField id="board-radius" min={0} max={50} className="form-field-panel" value={draft.radius} onCommit={e.setRadius} /></FormRow>
          <FormRow label="Hole diameter" htmlFor="board-hole"><NumberField id="board-hole" min={0} max={20} step={0.1} className="form-field-panel" value={draft.holeDiameter} onCommit={e.setHoleDiameter} /></FormRow>
        </FormGrid>
        <div className="board-editor-holes">
          <ToggleChip selected={draft.holes.length > 0} onClick={() => e.setHoles(draft.holes.length ? 'none' : 'corners')} title="Mount holes at the four corners">Corner mount holes</ToggleChip>
        </div>
      </Card>

      {selected && type ? (
        <Card title={`${type.label} · ${selected.id}`} className="board-editor-card board-connector-card">
          <FormRow label="Label" htmlFor="connector-label" note="A name for the whole connector, shown in the editor.">
            <TextField id="connector-label" className="form-field-panel" value={selected.label ?? ''} placeholder={type.label} onChange={(event) => e.setConnectorLabel(selected.id, event.target.value)} />
          </FormRow>
          <FormGrid>
            <FormRow label="X (mm)" htmlFor="connector-x"><NumberField id="connector-x" step={0.5} className="form-field-panel" value={selected.x} onCommit={(value) => e.setConnectorPosition(selected.id, value, selected.y)} /></FormRow>
            <FormRow label="Y (mm)" htmlFor="connector-y"><NumberField id="connector-y" step={0.5} className="form-field-panel" value={selected.y} onCommit={(value) => e.setConnectorPosition(selected.id, selected.x, value)} /></FormRow>
          </FormGrid>
          <div className="board-connector-actions">
            <Button size="sm" onClick={() => e.rotate(selected.id)}><RotateCw aria-hidden="true" />Rotate {selected.rotation}°</Button>
            <Button size="sm" onClick={() => e.mirror(selected.id)}><FlipHorizontal2 aria-hidden="true" />{selected.mirror ? 'Mirrored' : 'Mirror'}</Button>
            <Button size="sm" variant="danger" onClick={() => e.removeConnector(selected.id)}><Trash2 aria-hidden="true" />Remove</Button>
          </div>
          <h4 className="board-pins-title">Pins</h4>
          <ul className="board-pin-list">
            {selected.pins.map((_, index) => <PinRow key={`${selected.id}-${index}-${pinLabel(selected, index)}`} editor={e} connectorId={selected.id} index={index} model={model} />)}
          </ul>
        </Card>
      ) : (
        <p className="devices-muted board-editor-hint">{draft.connectors.length ? 'Select a connector on the board to edit its pins.' : 'Add a connector from the palette on the left.'}</p>
      )}

      {e.issues.length > 0 && (
        <Card title="Checks" className="board-editor-card">
          <ul className="board-issues">
            {e.issues.map((issue, index) => (
              <li key={index} className={`board-issue is-${issue.severity}`}>
                <Badge tone={issue.severity === 'error' ? 'danger' : 'warning'} size="compact" caps>{issue.severity}</Badge>
                {issue.connector ? <button type="button" className="board-issue-text" onClick={() => e.setSelection({ connector: issue.connector, pin: issue.pin ? issue.pin - 1 : undefined })}>{issue.message}</button> : <span className="board-issue-text">{issue.message}</span>}
              </li>
            ))}
          </ul>
        </Card>
      )}
      {e.status && <p className={`board-editor-status${e.status.error ? ' is-error' : ''}`} role="status">{e.status.text}</p>}
    </div>
  )
}
