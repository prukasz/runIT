import { useMemo, useState } from 'react'
import { AlertCircle, Check, ChevronDown, ChevronRight, Download, ListPlus, Send, SlidersHorizontal } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Button } from '../components/Button'
import { ChoiceField } from '../components/ChoiceField'
import { NumberField } from '../components/FormField'
import { PanelHeader } from '../components/PanelHeader'
import { RouterClient } from '../backend/protocol/client'
import type { LinkState, RunitBleLink } from '../ble'
import {
  BOARD_DEVICES,
  commandFrame,
  createFrame,
  deviceCommands,
  deviceType,
  newStep,
  stepFrame,
} from '../domain/devices'
import type { DeviceCommand, DeviceType, ProjectDevice } from '../domain/devices'
import { defaultConfig } from '../domain/settings'
import type { ConfigValues } from '../domain/settings'
import { DeviceTile } from './DeviceTile'
import { boardRef } from './useDevicesWorkspace'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import './Devices.css'

interface ResolvedTarget {
  readonly ref: string
  readonly deviceId: number
  readonly name: string
  readonly type?: DeviceType
  readonly isBoard: boolean
  readonly projectDevice?: ProjectDevice
}

const LIFECYCLE_OPS = ['SYS_DEVICE_OP_RESET', 'SYS_DEVICE_OP_SUSPEND', 'SYS_DEVICE_OP_RESUME']

export function decodeDeviceResponse(data: Uint8Array, cmd?: DeviceCommand): string {
  if (!data || data.byteLength === 0) return 'OK'
  const returns = cmd?.rawReturns as
    | {
        type?: string
        alias?: string
        unit?: string
        choices?: readonly { value: number; alias: string }[]
        fields?: readonly {
          name: string
          offset: number
          type: string
          size: number
          alias: string
          unit?: string
          choices?: readonly { value: number; alias: string }[]
        }[]
      }
    | undefined

  if (returns) {
    if (returns.type === 'struct' && Array.isArray(returns.fields)) {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
      const parts: string[] = []
      for (const f of returns.fields) {
        if (f.offset + f.size > data.byteLength) continue
        let val = 0
        if (f.size === 1) val = f.type.startsWith('i') ? view.getInt8(f.offset) : view.getUint8(f.offset)
        else if (f.size === 2) val = f.type.startsWith('i') ? view.getInt16(f.offset, true) : view.getUint16(f.offset, true)
        else if (f.size === 4) val = f.type.startsWith('i') ? view.getInt32(f.offset, true) : view.getUint32(f.offset, true)
        const choice = f.choices?.find((c) => c.value === val)
        parts.push(`${f.alias}: ${choice ? choice.alias : val}${f.unit ? ` ${f.unit}` : ''}`)
      }
      if (parts.length > 0) return parts.join(' · ')
    } else if (returns.type === 'i32' || returns.type === 'u32') {
      if (data.byteLength >= 4) {
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
        const val = returns.type === 'i32' ? view.getInt32(0, true) : view.getUint32(0, true)
        const choice = returns.choices?.find((c) => c.value === val)
        return `${returns.alias ?? 'Result'}: ${choice ? choice.alias : val}${returns.unit ? ` ${returns.unit}` : ''}`
      }
    } else if (returns.type === 'u16' || returns.type === 'i16') {
      if (data.byteLength >= 2) {
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
        const val = returns.type === 'i16' ? view.getInt16(0, true) : view.getUint16(0, true)
        const choice = returns.choices?.find((c) => c.value === val)
        return `${returns.alias ?? 'Result'}: ${choice ? choice.alias : val}${returns.unit ? ` ${returns.unit}` : ''}`
      }
    } else if (returns.type === 'u8' || returns.type === 'i8') {
      if (data.byteLength >= 1) {
        const val = returns.type === 'i8' ? new DataView(data.buffer, data.byteOffset, data.byteLength).getInt8(0) : data[0]
        const choice = returns.choices?.find((c) => c.value === val)
        return `${returns.alias ?? 'Result'}: ${choice ? choice.alias : val}${returns.unit ? ` ${returns.unit}` : ''}`
      }
    }
  }

  if (data.byteLength === 1) return `OK · ${data[0]}`
  if (data.byteLength === 2) return `OK · ${new DataView(data.buffer, data.byteOffset, data.byteLength).getUint16(0, true)}`
  if (data.byteLength === 4) return `OK · ${new DataView(data.buffer, data.byteOffset, data.byteLength).getInt32(0, true)}`

  return `OK · ${Array.from(data).map((b) => b.toString(16).padStart(2, '0')).join(' ')}`
}

export function DeviceDetails({
  workspace: w,
  link,
  linkState,
}: {
  workspace: DevicesWorkspace
  link: RunitBleLink
  linkState?: LinkState
}) {
  const ref = w.selection.kind === 'device' ? w.selection.ref : undefined
  const board = ref?.startsWith('board:') ? BOARD_DEVICES.find((d) => boardRef(d.id) === ref) : undefined
  const dev = !board && ref ? w.devices.find((d) => d.id === ref) : undefined

  const target = useMemo<ResolvedTarget | undefined>(() => {
    if (board) {
      return {
        ref: boardRef(board.id),
        deviceId: board.id,
        name: w.boardName(board.id),
        type: board.type,
        isBoard: true,
      }
    }
    if (dev) {
      return {
        ref: dev.id,
        deviceId: dev.deviceId,
        name: dev.name,
        type: deviceType(dev.type),
        isBoard: false,
        projectDevice: dev,
      }
    }
    return undefined
  }, [board, dev, w])

  if (!target) {
    return (
      <div className="devices-details">
        <PanelHeader title="Commands" />
        <p className="devices-muted" style={{ padding: '0 12px' }}>Select a device on the left to send live commands.</p>
      </div>
    )
  }

  const isConnected = (linkState ?? link.state) === 'connected'

  return (
    <div className="devices-details">
      <PanelHeader
        icon={<DeviceTile type={target.type} size="small" />}
        title={target.name}
      >
        <Badge tone="system" size="compact">{`#${target.deviceId}`}</Badge>
      </PanelHeader>

      {!isConnected && <p className="devices-notice">Connect a board to send commands.</p>}

      <Commands
        key={target.ref}
        workspace={w}
        target={target}
        link={link}
        linkState={linkState}
      />
    </div>
  )
}

function Commands({
  workspace: w,
  target,
  link,
  linkState,
}: {
  workspace: DevicesWorkspace
  target: ResolvedTarget
  link: RunitBleLink
  linkState?: LinkState
}) {
  const client = useMemo(() => new RouterClient(link), [link])
  const isConnected = (linkState ?? link.state) === 'connected'

  const [open, setOpen] = useState<string>()
  const [values, setValues] = useState<Readonly<Record<string, ConfigValues>>>({})
  const [results, setResults] = useState<Readonly<Record<string, { ok: boolean; text: string }>>>({})
  const [busy, setBusy] = useState(false)

  const allCommands = useMemo(() => deviceCommands(target.type), [target.type])
  const defaults = useMemo(() => w.setup.filter((s) => s.device === target.deviceId), [w.setup, target.deviceId])

  const quickCommands = useMemo(
    () => allCommands.filter((cmd) => LIFECYCLE_OPS.includes(cmd.symbol) || (cmd.group === 'Device' && cmd.op.fields.length === 0)),
    [allCommands],
  )

  const grouped = useMemo(() => {
    const map = new Map<string, DeviceCommand[]>()
    for (const cmd of allCommands) {
      if (quickCommands.includes(cmd)) continue
      const group = cmd.group || 'Other'
      const list = map.get(group) ?? []
      list.push(cmd)
      map.set(group, list)
    }
    return map
  }, [allCommands, quickCommands])

  const valuesOf = (cmd: DeviceCommand): ConfigValues => values[cmd.symbol] ?? defaultConfig(cmd.op)

  const run = async (key: string, label: string, frames: readonly { frame: Uint8Array; cmd?: DeviceCommand; label: string }[]) => {
    if (!isConnected) return
    setBusy(true)
    let result: { ok: boolean; text: string } = { ok: true, text: 'OK' }
    try {
      for (const entry of frames) {
        const data = await client.request([entry.frame[0]!], entry.frame.subarray(1), { timeoutMs: 4000 })
        const decoded = decodeDeviceResponse(data, entry.cmd)
        result = { ok: true, text: decoded }
      }
      if (frames.length > 1 && result.ok) {
        result = { ok: true, text: `${label}: ${frames.length} commands OK` }
      }
    } catch (error) {
      result = { ok: false, text: error instanceof Error ? error.message : String(error) }
    } finally {
      setBusy(false)
      setResults((current) => ({ ...current, [key]: result }))
    }
  }

  const deviceResult = results.device

  return (
    <div className="devices-commands">
      <section className="devices-command-group">
        <h3>Device</h3>
        <div className="devices-command-buttons">
          {!target.isBoard && target.projectDevice && target.type && (
            <Button
              variant="primary"
              size="sm"
              disabled={!isConnected || busy}
              title="Install this device onto the connected board"
              onClick={() =>
                void run('device', 'Install', [
                  { frame: createFrame(target.projectDevice!, target.type!), label: 'Install' },
                ])
              }
            >
              <Download aria-hidden="true" />
              <span>Install</span>
            </Button>
          )}

          {defaults.length > 0 && (
            <Button
              size="sm"
              disabled={!isConnected || busy}
              title="Send this device's default settings now"
              onClick={() =>
                void run(
                  'device',
                  'Default settings',
                  defaults.flatMap((step) => {
                    const frame = stepFrame(step, w.devices)
                    return frame ? [{ frame, label: step.op }] : []
                  }),
                )
              }
            >
              <SlidersHorizontal aria-hidden="true" />
              <span>Apply defaults ({defaults.length})</span>
            </Button>
          )}

          {quickCommands.map((cmd) => (
            <Button
              key={cmd.symbol}
              size="sm"
              disabled={!isConnected || busy}
              title={cmd.description}
              onClick={() =>
                void run('device', cmd.alias, [
                  { frame: commandFrame(target.deviceId, cmd, {}), cmd, label: cmd.alias },
                ])
              }
            >
              <span>{cmd.alias}</span>
            </Button>
          ))}
        </div>
        {deviceResult && (
          <p className={`program-diag ${deviceResult.ok ? '' : 'is-error'}`}>
            {deviceResult.ok ? <Check aria-hidden="true" /> : <AlertCircle aria-hidden="true" />}
            {deviceResult.text}
          </p>
        )}
      </section>

      {[...grouped.entries()].map(([groupName, cmds]) => (
        <section key={groupName} className="devices-command-group">
          <h3>{groupName}</h3>
          {cmds.map((cmd) => {
            const isCardOpen = open === cmd.symbol
            const result = results[cmd.symbol]
            const currentValues = valuesOf(cmd)

            return (
              <div key={cmd.symbol} className={`devices-contract ${isCardOpen ? 'is-open' : ''}`}>
                <button
                  type="button"
                  className="devices-contract-head"
                  onClick={() => setOpen(isCardOpen ? undefined : cmd.symbol)}
                  aria-expanded={isCardOpen}
                >
                  {isCardOpen ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />}
                  <span>{cmd.alias}</span>
                  {result && <i className={`devices-result-dot ${result.ok ? '' : 'is-error'}`} />}
                </button>

                {isCardOpen && (
                  <div className="devices-contract-body">
                    {cmd.description && <p className="devices-muted">{cmd.description}</p>}

                    <CommandFields
                      command={cmd}
                      values={currentValues}
                      onChange={(next) => setValues((curr) => ({ ...curr, [cmd.symbol]: next }))}
                    />

                    {cmd.returns && <p className="devices-hint">Returns response data</p>}

                    <div className="devices-contract-actions">
                      <Button
                        variant="primary"
                        size="sm"
                        disabled={!isConnected || busy}
                        onClick={() =>
                          void run(cmd.symbol, cmd.alias, [
                            {
                              frame: commandFrame(target.deviceId, cmd, currentValues),
                              cmd,
                              label: cmd.alias,
                            },
                          ])
                        }
                      >
                        <Send aria-hidden="true" />
                        <span>Send</span>
                      </Button>

                      {cmd.stored && (
                        <Button
                          size="sm"
                          onClick={() => {
                            const step = {
                              ...newStep(target.deviceId, cmd),
                              values: currentValues,
                            }
                            w.addStepToAction(undefined, step)
                          }}
                          title="Add this command to an action"
                        >
                          <ListPlus aria-hidden="true" />
                          <span>Add to action</span>
                        </Button>
                      )}
                    </div>

                    {result && (
                      <p className={`program-diag ${result.ok ? '' : 'is-error'}`}>
                        {result.ok ? <Check aria-hidden="true" /> : <AlertCircle aria-hidden="true" />}
                        {result.text}
                      </p>
                    )}
                  </div>
                )}
              </div>
            )
          })}
        </section>
      ))}
    </div>
  )
}

export function CommandFields({
  command,
  values,
  onChange,
}: {
  command: DeviceCommand
  values: ConfigValues
  onChange: (values: ConfigValues) => void
}) {
  const fields = command.op.fields.filter((field) => !field.hidden)
  if (!fields.length) return null

  return (
    <div className="contract-fields">
      {fields.map((field) => {
        const value = values[field.name] ?? field.default ?? field.min ?? 0
        const set = (next: number) => onChange({ ...values, [field.name]: next })
        return (
          <div key={field.name} className="contract-field" title={field.description}>
            <span>
              {field.alias}
              {field.unit ? ` (${field.unit})` : ''}
            </span>
            {field.choices ? (
              <ChoiceField<number>
                value={value}
                choices={field.choices.map((choice) => ({
                  value: choice.value,
                  label: choice.alias,
                }))}
                onChange={(next) => next !== undefined && set(next)}
              />
            ) : (
              <NumberField
                value={value}
                min={field.min}
                max={field.max}
                integer={!field.type?.includes('float') && !field.type?.includes('double')}
                onCommit={set}
              />
            )}
          </div>
        )
      })}
    </div>
  )
}
