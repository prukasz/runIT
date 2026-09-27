import { useState } from 'react'
import { AlertCircle, AlertTriangle, ChevronDown, ChevronRight, Download, ListPlus, Send, SlidersHorizontal, Trash2 } from 'lucide-react'
import { decodeResponseData } from '../domain/descriptors'
import type { DeviceContract } from '../domain/descriptors'
import { contractFrame, contractsOf, describeStep, findContract, frequencyWarnings, installFrame, resolveDevice, SET_PWM_FREQUENCY } from '../domain/devices'
import type { ResolvedDevice } from '../domain/devices'
import type { StepValues } from '../domain/project'
import { toHex } from '../domain/upload'
import { describeCommandError } from '../sendSteps'
import type { RunitBleSession } from '../backend/runitBleSession'
import { ContractFields, initialValues } from './ContractFields'
import { DeviceTile } from './DeviceTile'
import type { DevicesWorkspace } from './useDevicesWorkspace'

type Result = { readonly ok: boolean; readonly text: string }

const send = async (session: RunitBleSession, label: string, frame: Uint8Array, contract?: DeviceContract): Promise<Result> => {
  try {
    const response = await session.commands.call({ body: frame, label })
    if (!contract?.command.response || !response.data.byteLength) return { ok: true, text: response.data.byteLength ? `OK · ${toHex(response.data)}` : 'OK' }
    const decoded = decodeResponseData(contract.command, response.data)!
    return { ok: true, text: contract.command.response.fields.map((field) => `${field.label}: ${String(decoded.values[field.name])}${field.unit ? ` ${field.unit}` : ''}`).join(' · ') }
  } catch (error) {
    return { ok: false, text: describeCommandError(error) }
  }
}

/** What a contract does, by its command name: the heading it is listed under. */
const GROUPS: readonly [RegExp, string][] = [
  [/^sys_io_/, 'Pins'],
  [/^sys_power_vreg_/, 'Regulator'],
  [/^sys_power_monitor_/, 'Power monitor'],
  [/^sys_power_usb_pd_/, 'USB-C power delivery'],
  [/^sys_power_/, 'Power'],
  [/^sys_hbridge_/, 'Motor driver'],
]
const groupOf = (contract: DeviceContract): string => GROUPS.find(([pattern]) => pattern.test(contract.command.name))?.[1] ?? 'Other'

/** Device commands that take nothing but the device ID: one button each. */
const QUICK = ['reset', 'suspend', 'resume', 'freeze', 'sync']
const SET_MODE = 'packet_sys_io_set_mode_t'

/** Right panel of the Board view: every command the selected device takes, grouped, sent live. */
export function DeviceDetails({ workspace: w, session }: { workspace: DevicesWorkspace; session?: RunitBleSession }) {
  const ref = w.selection?.kind === 'device' ? w.selection.ref : undefined
  const device = ref ? resolveDevice(w.catalog, w.devices, ref) : undefined

  if (!device) {
    return (
      <div className="program-panel devices-details">
        <div className="object-details-header"><h2>Commands</h2></div>
        <p className="devices-muted">Select a device on the left to send it commands.</p>
      </div>
    )
  }

  return (
    <div className="program-panel devices-details">
      <div className="object-details-header">
        <DeviceTile appearance={w.devices.find((entry) => entry.id === ref)?.appearance} type={device.type} size="small" />
        <h2>{device.name}</h2>
        <span className="ble-uuid-chip">#{device.deviceId}</span>
      </div>
      {!session && <p className="devices-notice">Connect a board to send commands.</p>}
      {w.composing && <p className="devices-notice is-accent">Adding to <strong>{w.composing.name}</strong>: use "Add to action" on a command.</p>}
      <Commands key={device.ref} workspace={w} device={device} session={session} />
    </div>
  )
}

function Commands({ workspace: w, device, session }: { workspace: DevicesWorkspace; device: ResolvedDevice; session?: RunitBleSession }) {
  const [open, setOpen] = useState<string>()
  const [values, setValues] = useState<Readonly<Record<string, StepValues>>>({})
  const [results, setResults] = useState<Readonly<Record<string, Result>>>({})
  const [busy, setBusy] = useState(false)

  const project = w.devices.find((entry) => entry.id === device.ref)
  const installProblems = w.diagnostics.some((entry) => entry.severity === 'error' && entry.subjectId === device.ref)
  const setupProblems = w.diagnostics.some((entry) => entry.severity === 'error' && entry.subjectId === `setup:${device.ref}`)
  const defaults = [...w.setup.filter((step) => step.device === device.ref && step.contract === SET_MODE), ...w.setup.filter((step) => step.device === device.ref && step.contract !== SET_MODE)]
  const all = contractsOf(w.catalog, device)
  const quick = QUICK.flatMap((name) => all.filter((contract) => contract.kind === 'device' && contract.command.name === `sys_device_${name}`))
  const deviceForms = all.filter((contract) => contract.kind === 'device' && !quick.includes(contract) && contract.command.name !== 'sys_device_uninstall')
  const groups = new Map<string, DeviceContract[]>()
  for (const contract of all.filter((entry) => entry.kind === 'contract')) groups.set(groupOf(contract), [...(groups.get(groupOf(contract)) ?? []), contract])

  const valuesOf = (contract: DeviceContract) => values[contract.id] ?? initialValues(contract)
  const note = (key: string, result: Result) => setResults((current) => ({ ...current, [key]: result }))
  const run = async (key: string, label: string, frames: readonly { frame: Uint8Array; contract?: DeviceContract; label: string }[]) => {
    if (!session) return
    setBusy(true)
    let result: Result = { ok: true, text: 'OK' }
    for (const entry of frames) {
      result = await send(session, `${device.name}: ${entry.label}`, entry.frame, entry.contract)
      if (!result.ok) {
        result = { ok: false, text: `${entry.label}: ${result.text}` }
        break
      }
    }
    note(key, frames.length > 1 && result.ok ? { ok: true, text: `${label}: ${frames.length} commands OK` } : result)
    setBusy(false)
  }
  const lifecycle = (name: string) => all.find((contract) => contract.kind === 'device' && contract.command.name === `sys_device_${name}`)
  const one = (contract: DeviceContract) => [{ frame: contractFrame(contract, device.deviceId, valuesOf(contract)), contract, label: contract.label }]

  const card = (contract: DeviceContract) => {
    const expanded = open === contract.id
    const result = results[contract.id]
    const current = valuesOf(contract)
    const warnings = contract.id === SET_PWM_FREQUENCY && typeof current.frequency_Hz === 'number'
      ? frequencyWarnings(w.catalog, w.devices, w.setup, w.actions, { device: device.ref, hz: current.frequency_Hz, ...(typeof current.pin === 'number' ? { pin: current.pin } : {}) })
      : []
    return (
      <div key={contract.id} className={`devices-contract ${expanded ? 'is-open' : ''}`}>
        <button type="button" className="devices-contract-head" onClick={() => setOpen(expanded ? undefined : contract.id)} aria-expanded={expanded}>
          {expanded ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />}
          <span>{contract.label}</span>
          {result && <i className={`devices-result-dot ${result.ok ? '' : 'is-error'}`} />}
        </button>
        {expanded && (
          <div className="devices-contract-body">
            {contract.description && <p className="devices-muted">{contract.description}</p>}
            {warnings.map((warning) => <p key={warning} className="program-diag is-warning"><AlertTriangle aria-hidden="true" />{warning}</p>)}
            <ContractFields contract={contract} deviceId={device.deviceId} values={valuesOf(contract)} actions={w.actions} onChange={(next) => setValues((current) => ({ ...current, [contract.id]: next }))} />
            {contract.returns && <p className="devices-hint">Returns {contract.returns.split(' @')[0]}</p>}
            <div className="program-actions">
              <button type="button" className="program-primary" disabled={!session || busy} onClick={() => void run(contract.id, contract.label, one(contract))}><Send aria-hidden="true" /><span>Send</span></button>
              <button type="button" onClick={() => w.compose(w.composing?.id, { device: device.ref, contract: contract.id, values: valuesOf(contract) })} title={w.composing ? `Add to ${w.composing.name}` : 'Start a new action with this command'}>
                <ListPlus aria-hidden="true" /><span>Add to action</span>
              </button>
            </div>
            {result && <p className={`program-diag ${result.ok ? '' : 'is-error'}`}>{result.ok ? null : <AlertCircle aria-hidden="true" />}{result.text}</p>}
          </div>
        )}
      </div>
    )
  }

  const deviceResult = results.device
  return (
    <div className="devices-commands">
      <section className="devices-command-group">
        <h3>Device</h3>
        <div className="devices-command-buttons">
          {project && device.type && (
            <>
              <button type="button" className="is-primary" disabled={!session || busy || installProblems} title={installProblems ? 'Fix the device configuration first' : 'Install with the configuration from the main view'}
                onClick={() => void run('device', 'Install', [{ frame: installFrame(device.type!, project), label: 'install' }])}><Download aria-hidden="true" />Install</button>
              {lifecycle('uninstall') && <button type="button" disabled={!session || busy} onClick={() => void run('device', 'Uninstall', one(lifecycle('uninstall')!))}><Trash2 aria-hidden="true" />Uninstall</button>}
            </>
          )}
          {defaults.length > 0 && (
            <button type="button" disabled={!session || busy || setupProblems} title="Send this device's default settings now (the stored code sets them at every boot)"
              onClick={() => void run('device', 'Default settings', defaults.flatMap((step) => {
                const contract = findContract(w.catalog, device, step.contract)
                return contract ? [{ frame: contractFrame(contract, device.deviceId, step.values), contract, label: describeStep(w.catalog, w.devices, step) }] : []
              }))}><SlidersHorizontal aria-hidden="true" />Apply defaults ({defaults.length})</button>
          )}
          {quick.map((contract) => (
            <button key={contract.id} type="button" disabled={!session || busy} title={contract.description} onClick={() => void run('device', contract.label, one(contract))}>{contract.label.replace(/^./, (c) => c.toUpperCase())}</button>
          ))}
        </div>
        {deviceResult && <p className={`program-diag ${deviceResult.ok ? '' : 'is-error'}`}>{deviceResult.ok ? null : <AlertCircle aria-hidden="true" />}{deviceResult.text}</p>}
        {deviceForms.map(card)}
      </section>
      {[...groups].map(([name, contracts]) => (
        <section key={name} className="devices-command-group">
          <h3>{name}</h3>
          {contracts.map(card)}
        </section>
      ))}
    </div>
  )
}
