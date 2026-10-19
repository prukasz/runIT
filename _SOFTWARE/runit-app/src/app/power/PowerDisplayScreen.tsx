import { ArrowDown, ArrowUp, Plus, RotateCcw, Trash2 } from 'lucide-react'
import type { BoardDescriptor, FieldValues, Services } from '@backend'
import { Button, Card, CardStack, ChoiceField, FormRow, NumberField, PanelHeader, Switch, TextField } from '@ui/components'
import type { Choice } from '@ui/components'
import type { DeviceInfo } from '../devices/devices'
import type { useProject } from '../project/useProject'
import { answerFields, commandOf, defaultReadings, readCommands, readKey, showValue, valueSpec } from './powerDisplay'
import type { PowerReading } from './powerDisplay'
import { usePowerLive } from './usePowerLive'
import './PowerLive.css'

/*
 * The Project view's Live power display tab: what the display in the right panel reads, one reading a card: its name, the command
 * that reads it (a service's or a device's, any that only reads and answers), that command's arguments, the field of a struct answer,
 * whether the short view shows it; its value now. Unset, the project shows the board's default (board.json's input and rails).
 */

export function PowerDisplayScreen({ project, services, board, devices, connected }: {
  project: ReturnType<typeof useProject>
  services: Services
  board: BoardDescriptor
  devices: readonly DeviceInfo[]
  connected: boolean
}) {
  const own = project.project.powerDisplay
  const readings = own ?? defaultReadings(board)
  const values = usePowerLive(services, readings, connected, true)
  const commands = readCommands(services.catalog, devices)
  const choices: Choice<string>[] = commands.map((c) => ({ value: readKey(c.read), label: c.label, group: c.group }))
  const set = (next: readonly PowerReading[], key?: string) => project.edit((p) => ({ ...p, powerDisplay: next }), key)
  const change = (i: number, patch: Partial<PowerReading>, key?: string) => set(readings.map((r, j) => (j === i ? { ...r, ...patch } : r)), key)
  const move = (i: number, by: number) => {
    const next = [...readings]
    const [taken] = next.splice(i, 1)
    next.splice(i + by, 0, taken!)
    set(next)
  }

  return (
    <div className="project-screen power-display-screen">
      <PanelHeader title="Live power display" />
      <p className="power-display-intro">What the right panel shows of the board's power, read every second while connected. Readings of one name stand together; those not in the short view show with More.</p>
      <div className="power-display-actions">
        <Button onClick={() => set([...readings, { name: 'Reading', read: { kind: 'service', service: 'POWER_MANAGER', command: 'GET_STATUS' }, field: 'input_mV', main: true }])}><Plus aria-hidden="true" />Add a reading</Button>
        <Button disabled={own === undefined} title="Back to the board's own: the input, the source and every regulator rail" onClick={() => project.edit((p) => {
          const { powerDisplay: _dropped, ...rest } = p
          return rest
        })}><RotateCcw aria-hidden="true" />The board's default</Button>
      </div>
      <CardStack>
        {readings.map((reading, i) => {
          const command = commandOf(services.catalog, devices, reading.read)
          const fields = answerFields(command)
          const spec = valueSpec(command, reading.field)
          const args = command?.args?.fields ?? []
          const setArg = (id: string, value: FieldValues[string]) => change(i, { args: { ...(reading.args ?? {}), [id]: value } }, `power-arg:${i}:${id}`)
          return (
            <Card key={i} title={reading.name || 'Reading'} actions={(
              <span className="power-display-row-actions">
                <span className="power-display-now" title="Its value now">{connected ? showValue(values[i], spec) : 'not connected'}</span>
                <Button size="icon-sm" aria-label="Move up" disabled={i === 0} onClick={() => move(i, -1)}><ArrowUp aria-hidden="true" /></Button>
                <Button size="icon-sm" aria-label="Move down" disabled={i === readings.length - 1} onClick={() => move(i, 1)}><ArrowDown aria-hidden="true" /></Button>
                <Button size="icon-sm" variant="danger" aria-label="Remove the reading" onClick={() => set(readings.filter((_, j) => j !== i))}><Trash2 aria-hidden="true" /></Button>
              </span>
            )}>
              <FormRow label="Name" note="Readings of one name stand together under it.">
                <TextField aria-label="Name" value={reading.name} onChange={(event) => change(i, { name: event.target.value }, `power-name:${i}`)} />
              </FormRow>
              <FormRow label="Read by" note={command?.description}>
                <ChoiceField<string> aria-label="Read by" value={readKey(reading.read)} choices={choices} unavailableLabel={() => 'A command the board does not have'}
                  onChange={(key) => {
                    // The same command again keeps its arguments and its value; another one starts at its first measured value (one with a unit).
                    if (key === readKey(reading.read)) return
                    const found = commands.find((c) => readKey(c.read) === key)
                    const fields = answerFields(commandOf(services.catalog, devices, found?.read ?? reading.read))
                    if (found) change(i, { read: found.read, args: undefined, field: (fields.find((f) => f.unit) ?? fields[0])?.id })
                  }} />
              </FormRow>
              {args.map((field) => (
                <FormRow key={field.id} label={`${field.label}${field.unit ? ` (${field.unit})` : ''}`} note={field.description}>
                  {field.choices
                    ? <ChoiceField<string> aria-label={field.label} value={reading.args?.[field.id] === undefined ? undefined : String(reading.args[field.id])} choices={field.choices.map((c) => ({ value: String(c.value), label: c.label }))}
                      onChange={(v) => { const c = field.choices!.find((x) => String(x.value) === v); if (c) setArg(field.id, typeof c.value === 'string' ? c.label : Number(c.value)) }} />
                    : <NumberField aria-label={field.label} integer value={typeof reading.args?.[field.id] === 'number' ? (reading.args[field.id] as number) : undefined} onCommit={(v) => setArg(field.id, v)} />}
                </FormRow>
              ))}
              {fields.length > 0 && (
                <FormRow label="Value" note="Which part of the answer the display shows.">
                  <ChoiceField<string> aria-label="Value" value={reading.field} placeholder="Choose a value" choices={fields.filter((f) => f.type !== 'bitmask').map((f) => ({ value: f.id, label: `${f.label}${f.unit ? ` (${f.unit})` : ''}`, ...(f.description ? { description: f.description } : {}) }))}
                    onChange={(field) => change(i, { field })} />
                </FormRow>
              )}
              <Switch checked={reading.main ?? false} label="In the short view" onChange={(main) => change(i, { main: main || undefined })} />
            </Card>
          )
        })}
      </CardStack>
    </div>
  )
}
