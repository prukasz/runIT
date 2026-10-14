import { ArrowDown, ArrowUp, Plus, Trash2 } from 'lucide-react'
import { Button } from '../components/Button'
import { ChoiceField } from '../components/ChoiceField'
import { FormGrid, FormRow, NumberField } from '../components/FormField'
import { BOARD_DEVICES, commandsOf, deviceType, newStep, NO_DEVICE, stepCommand } from '../domain/devices'
import type { ActionStep, DeviceCommand, ProjectDevice } from '../domain/devices'
import type { DevicesWorkspace } from './useDevicesWorkspace'

/*
 * A list of device commands, the one editor behind a device's default settings and an action: each step is a device, one of the
 * commands that device type has, and the values of the command's arguments (the form is built from the descriptors, as the install form).
 */

const storedCommands = (deviceId: number, devices: readonly ProjectDevice[]): readonly DeviceCommand[] => commandsOf(deviceId, devices).filter((command) => command.stored)

/** The devices a step can go to: the board's and the project's that have a command to store, and every device at once. */
const targets = (w: DevicesWorkspace) => [
  ...BOARD_DEVICES.filter((device) => storedCommands(device.id, w.devices).length).map((device) => ({ value: device.id, label: `${w.boardName(device.id)} (#${device.id})` })),
  ...w.devices.filter((device) => storedCommands(device.deviceId, w.devices).length).map((device) => ({ value: device.deviceId, label: `${device.name} (#${device.deviceId})` })),
  { value: NO_DEVICE, label: 'All devices' },
]

function StepFields({ command, step, onChange }: { command: DeviceCommand; step: ActionStep; onChange: (values: Record<string, number>) => void }) {
  const fields = command.op.fields.filter((field) => !field.hidden)
  if (!fields.length) return <p className="devices-muted">{command.description ?? 'No settings.'}</p>
  return (
    <FormGrid>
      {fields.map((field) => {
        const value = step.values[field.name] ?? field.default ?? field.min ?? 0
        const set = (next: number) => onChange({ ...step.values, [field.name]: next })
        return (
          <FormRow key={field.name} label={<>{field.alias}{field.unit ? ` (${field.unit})` : ''}</>} note={field.description}>
            {field.choices
              ? <ChoiceField<number> selectClassName="form-field-panel" value={value} unavailableLabel={(entry) => `Invalid: ${entry}`} choices={field.choices.map((choice) => ({ value: choice.value, label: choice.alias }))} onChange={(next) => next !== undefined && set(next)} />
              : <NumberField className="form-field-panel" integer min={field.min} max={field.max} value={value} onCommit={set} />}
          </FormRow>
        )
      })}
    </FormGrid>
  )
}

/**
 * `fixedDevice`: the steps are one device's default settings, so every step is on that device and a step is added by picking its command.
 * Without it (an action) each step names its device.
 */
export function ActionSteps({ workspace: w, steps, fixedDevice, onChange }: { workspace: DevicesWorkspace; steps: readonly ActionStep[]; fixedDevice?: number; onChange: (steps: ActionStep[]) => void }) {
  const replace = (id: string, step: ActionStep) => onChange(steps.map((entry) => (entry.id === id ? step : entry)))
  const move = (index: number, by: number) => {
    const next = [...steps]
    const [taken] = next.splice(index, 1)
    next.splice(index + by, 0, taken!)
    onChange(next)
  }
  const deviceChoices = targets(w)
  const append = (device: number, symbol?: string) => {
    const command = storedCommands(device, w.devices).find((entry) => entry.symbol === symbol) ?? storedCommands(device, w.devices)[0]
    if (command) onChange([...steps, newStep(device, command)])
  }
  const fixedCommands = fixedDevice === undefined ? [] : storedCommands(fixedDevice, w.devices)

  return (
    <div className="step-list">
      {steps.map((step, index) => {
        const command = stepCommand(step, w.devices)
        const commands = storedCommands(step.device, w.devices)
        return (
          <div key={step.id} className="step-card">
            <div className="step-head">
              <span className="step-number">{index + 1}</span>
              {fixedDevice === undefined && (
                <ChoiceField<number> selectClassName="form-field-panel" value={step.device} unavailableLabel={(id) => `Device #${id}`} choices={deviceChoices}
                  onChange={(next) => {
                    if (next === undefined) return
                    const first = storedCommands(next, w.devices)[0]
                    if (first) replace(step.id, newStep(next, first))
                  }} />
              )}
              <ChoiceField<string> selectClassName="form-field-panel" value={step.op} unavailableLabel={(op) => op} choices={commands.map((entry) => ({ value: entry.symbol, label: entry.alias, group: entry.group, description: entry.description }))}
                onChange={(next) => {
                  const picked = commands.find((entry) => entry.symbol === next)
                  if (picked) replace(step.id, newStep(step.device, picked))
                }} />
              <span className="step-actions">
                <button type="button" title="Move up" aria-label="Move up" disabled={index === 0} onClick={() => move(index, -1)}><ArrowUp aria-hidden="true" /></button>
                <button type="button" title="Move down" aria-label="Move down" disabled={index === steps.length - 1} onClick={() => move(index, 1)}><ArrowDown aria-hidden="true" /></button>
                <button type="button" title="Remove this step" aria-label="Remove step" onClick={() => onChange(steps.filter((entry) => entry.id !== step.id))}><Trash2 aria-hidden="true" /></button>
              </span>
            </div>
            {command
              ? <StepFields command={command} step={step} onChange={(values) => replace(step.id, { ...step, values })} />
              : <p className="program-diag is-error">{step.op} is not a command of {deviceType(w.devices.find((entry) => entry.deviceId === step.device)?.type)?.alias ?? `device #${step.device}`}.</p>}
          </div>
        )
      })}
      {fixedDevice !== undefined ? (
        fixedCommands.length > 0 && (
          <div className="step-add">
            <ChoiceField<string> selectClassName="form-field-panel" value={undefined} emptyLabel="Add a default setting…" aria-label="Command to add"
              choices={fixedCommands.map((entry) => ({ value: entry.symbol, label: entry.alias, group: entry.group, description: entry.description }))}
              onChange={(next) => next && append(fixedDevice, next)} />
          </div>
        )
      ) : (
        <Button variant="dashed" block disabled={!deviceChoices.length} onClick={() => append(steps[steps.length - 1]?.device ?? deviceChoices[0]!.value)}><Plus aria-hidden="true" />Add a step</Button>
      )}
    </div>
  )
}
