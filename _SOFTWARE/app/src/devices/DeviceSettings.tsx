import { useState } from 'react'
import { Card } from '../components/Card'
import { SelectField, TextField } from '../components/FormField'
import { ArrowRight, Lock, Plus, Trash2 } from 'lucide-react'
import type { DeviceChoice } from '../domain/descriptors'
import { contractsOf, findContract, isSetupContract, pinDisplayLabel, pinKey, pinsOf, pinUsers, resolveDevice, SET_ERROR_HANDLING } from '../domain/devices'
import type { PinUser, ResolvedDevice } from '../domain/devices'
import { boardDeviceRef } from '../domain/project'
import { ContractFields, initialValues } from './ContractFields'
import { DeviceTile } from './DeviceTile'
import type { DevicesWorkspace } from './useDevicesWorkspace'

const SET_MODE = 'packet_sys_io_set_mode_t'
const SET_LEVEL = 'packet_sys_io_set_level_t'
const hex2 = (value: number): string => `0x${value.toString(16).padStart(2, '0').toUpperCase()}`

const modeName = (w: DevicesWorkspace, mode: number | undefined): string => (mode === undefined ? '' : w.catalog.ioModes.find((entry) => entry.value === mode)?.label ?? `mode ${mode}`)
const isOutput = (choice: DeviceChoice | undefined): boolean => !!choice?.symbol?.includes('OUTPUT')

/** The ref of the device with this device ID (board or user). */
const refOf = (w: DevicesWorkspace, deviceId: number): string | undefined =>
  w.catalog.board.some((entry) => entry.deviceId === deviceId) ? boardDeviceRef(deviceId) : w.devices.find((entry) => entry.deviceId === deviceId)?.id

/**
 * A reference to a device: its tile, its name and a detail (a pin, what it
 * uses the pin for); opens the device. Without a device it stands for the
 * board itself. Inside `.devices-ref-grid` the three parts line up in columns.
 */
function DevicePill({ w, deviceRef, name, detail }: { w: DevicesWorkspace; deviceRef?: string; name: string; detail: string }) {
  const device = deviceRef ? resolveDevice(w.catalog, w.devices, deviceRef) : undefined
  const appearance = device ? w.devices.find((entry) => entry.id === deviceRef)?.appearance : { icon: 'board' }
  return (
    <button type="button" className="devices-pill" disabled={!device} onClick={() => deviceRef && w.select({ kind: 'device', ref: deviceRef })} title={device ? `Open ${device.name}` : undefined}>
      <DeviceTile type={device?.type} appearance={appearance} size="small" />
      <span className="devices-pill-name">{name}</span>
      <span className="devices-pill-detail">{detail}</span>
    </button>
  )
}

/** How the board installs one of its own devices: bus, address, whether the bring-up switch is on. */
export function BoardInstallCard({ w, device }: { w: DevicesWorkspace; device: ResolvedDevice }) {
  const board = w.catalog.board.find((entry) => boardDeviceRef(entry.deviceId) === device.ref)
  return (
    <Card title="Installed by the board">
      {!board?.installed && <p className="devices-muted">Not installed on this board.</p>}
      <dl className="devices-facts">
        {board?.i2c && <div><dt>I2C</dt><dd>bus {board.i2c.bus}{board.i2c.bus === w.catalog.i2cBuses.internal ? ' (internal)' : ''} · {hex2(board.i2c.address)}</dd></div>}
        {device.type && <div><dt>Provides</dt><dd>{device.type.provider?.label ?? '—'}</dd></div>}
      </dl>
    </Card>
  )
}

/** The pins this device takes on other devices; each opens that device. */
export function PinsUsedCard({ w, device }: { w: DevicesWorkspace; device: ResolvedDevice }) {
  const links: { use: string; deviceId: number; pin: number; mode?: number }[] = []
  const board = w.catalog.board.find((entry) => boardDeviceRef(entry.deviceId) === device.ref)
  if (board) links.push(...board.pins.map((link) => ({ ...link, use: link.label })))
  else {
    for (const [key, users] of pinUsers(w.catalog, w.devices)) {
      for (const user of users) {
        if (user.owner !== device.ref) continue
        const [deviceId, pin] = key.split(':').map(Number) as [number, number]
        links.push({ use: user.use.replace(/ pin$/, ''), deviceId, pin, ...(user.mode === undefined ? {} : { mode: user.mode }) })
      }
    }
  }
  if (!links.length) return null
  return (
    <Card title="Pins it uses" className="devices-refs-card">
      <div className="devices-ref-grid">
        {links.map((link) => {
          const ref = refOf(w, link.deviceId)
          const target = ref ? resolveDevice(w.catalog, w.devices, ref) : undefined
          return (
            <div key={`${link.use}-${link.deviceId}-${link.pin}`} className="devices-ref-row">
              <span className="devices-ref-label">{link.use}</span>
              <ArrowRight className="devices-ref-arrow" aria-hidden="true" />
              <DevicePill w={w} deviceRef={ref} name={target?.name ?? `Device ${link.deviceId}`} detail={pinDisplayLabel(w.catalog, ref, link.pin)} />
              {link.mode !== undefined ? <span className="devices-mode-chip">{modeName(w, link.mode)}</span> : <span />}
            </div>
          )
        })}
      </div>
    </Card>
  )
}

function PinAliasField({ pin, label, alias, onSave, inTable = false }: { pin: number; label: string; alias: string; onSave: (value: string) => void; inTable?: boolean }) {
  const [draft, setDraft] = useState(alias)
  return <span className="devices-pin-name" role={inTable ? 'cell' : undefined}>
    <span>#{pin}</span>
    <TextField aria-label={`Alias for pin ${pin}`} title={`Name for pin ${pin}`} placeholder={label === String(pin) ? 'Add alias' : label} value={draft} onChange={(event) => setDraft(event.target.value)} onBlur={() => onSave(draft)} onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }} />
  </span>
}

/** Devices with pin choices but no mode contract still have project pin names. */
export function PinNamesCard({ w, device }: { w: DevicesWorkspace; device: ResolvedDevice }) {
  const pins = pinsOf(device.type)
  if (!pins.length || findContract(w.catalog, device, SET_MODE)) return null
  return <Card title="Pin names"><div className="devices-pin-names">
    {pins.map((pin) => <PinAliasField key={`${device.ref}:${pin.value}:${w.pinAliases?.[device.ref]?.[String(pin.value)] ?? ''}`} pin={pin.value} label={pin.label} alias={w.pinAliases?.[device.ref]?.[String(pin.value)] ?? ''} onSave={(value) => w.setPinAlias(device.ref, pin.value, value)} />)}
  </div></Card>
}

function TakenBy({ w, users }: { w: DevicesWorkspace; users: readonly PinUser[] }) {
  return (
    <span className="devices-taken">
      <Lock aria-hidden="true" />
      {users.map((user, index) => <DevicePill key={index} w={w} deviceRef={user.owner} name={user.ownerName} detail={user.use} />)}
    </span>
  )
}

/**
 * What the stored code sets on this device after installing it: a pin table
 * (mode and level per pin) when the device has pins, and any other setup
 * contract (PWM frequency, rail voltage …) with its values. Pins another
 * device takes are locked and link to it.
 */
export function DefaultSettingsCard({ w, device }: { w: DevicesWorkspace; device: ResolvedDevice }) {
  const [adding, setAdding] = useState('')
  const pins = pinsOf(device.type)
  const setMode = findContract(w.catalog, device, SET_MODE)
  const setLevel = findContract(w.catalog, device, SET_LEVEL)
  const modes = setMode?.parameters.find((parameter) => parameter.name === 'mode')?.choices ?? []
  const defaultMode = setMode?.parameters.find((parameter) => parameter.name === 'mode')?.defaultValue
  const defaultChoice = modes.find((choice) => choice.value === defaultMode)
  const table = pins.length > 0 && !!setMode
  const users = pinUsers(w.catalog, w.devices)
  const steps = w.setup.filter((step) => step.device === device.ref)
  const others = steps.filter((step) => step.contract !== SET_ERROR_HANDLING && (!table || (step.contract !== SET_MODE && step.contract !== SET_LEVEL)))
  const addable = contractsOf(w.catalog, device).filter((contract) => isSetupContract(contract) && contract.command.name !== 'sys_io_reset' && (!table || (contract.id !== SET_MODE && contract.id !== SET_LEVEL)))
  const problems = w.diagnostics.filter((entry) => entry.subjectId === `setup:${device.ref}`)

  if (!table && !addable.length) return null

  return (
    <Card title="Default settings">
      <p className="devices-muted">Device defaults apply at boot; project settings run after installation.</p>
      {problems.map((entry, index) => <p key={index} className={`program-diag ${entry.severity === 'error' ? 'is-error' : 'is-warning'}`}>{entry.message}</p>)}

      {table && (
        <div className="devices-pin-table" role="table" aria-label="Pin defaults">
          <div className="devices-pin-row is-head" role="row">
            <span role="columnheader">Pin</span>
            <span role="columnheader">Mode</span>
            <span role="columnheader">{setLevel ? 'Level' : ''}</span>
          </div>
          {pins.map((pin) => {
            const taken = (users.get(pinKey(device.deviceId, pin.value)) ?? []).filter((user) => user.owner !== device.ref)
            const modeStep = steps.find((step) => step.contract === SET_MODE && step.values.pin === pin.value)
            const levelStep = steps.find((step) => step.contract === SET_LEVEL && step.values.pin === pin.value)
            const mode = modes.find((choice) => choice.value === (modeStep?.values.mode ?? defaultMode))
            return (
              <div key={pin.value} className={`devices-pin-row ${taken.length ? 'is-taken' : ''} ${modeStep ? 'is-set' : ''}`} role="row">
                <PinAliasField key={`${device.ref}:${pin.value}:${w.pinAliases?.[device.ref]?.[String(pin.value)] ?? ''}`} pin={pin.value} label={pin.label} alias={w.pinAliases?.[device.ref]?.[String(pin.value)] ?? ''} onSave={(value) => w.setPinAlias(device.ref, pin.value, value)} inTable />
                {taken.length ? (
                  <span className="devices-pin-taken" role="cell"><TakenBy w={w} users={taken} />{taken[0]?.mode !== undefined && <em>{modeName(w, taken[0].mode)}</em>}</span>
                ) : (
                  <>
                    <span role="cell">
                      {defaultChoice && modes.length === 1 && !modeStep
                        ? <span className="devices-pin-default">{defaultChoice.label}<small>Device default</small></span>
                        : <SelectField aria-label={`Pin ${pin.value} mode`} value={typeof modeStep?.values.mode === 'number' ? modeStep.values.mode : ''} onChange={(event) => w.setPinMode(device.ref, pin.value, event.target.value === '' ? undefined : Number(event.target.value))}>
                          <option value="">{defaultChoice ? `${defaultChoice.label} (device default)` : '— not set —'}</option>
                          {modes.map((choice) => <option key={choice.value} value={choice.value} title={choice.description}>{choice.label}</option>)}
                        </SelectField>}
                    </span>
                    <span role="cell">
                      {setLevel && isOutput(mode) && (
                        <SelectField aria-label={`Pin ${pin.value} level`} value={levelStep === undefined ? '' : String(levelStep.values.level ?? 0)} onChange={(event) => w.setPinLevel(device.ref, pin.value, event.target.value === '' ? undefined : event.target.value === '1')}>
                          <option value="">—</option>
                          <option value="0">Low</option>
                          <option value="1">High</option>
                        </SelectField>
                      )}
                    </span>
                  </>
                )}
              </div>
            )
          })}
        </div>
      )}

      {others.map((step) => {
        const contract = findContract(w.catalog, device, step.contract)
        return (
          <div key={step.id} className="devices-default">
            <div className="devices-default-head">
              <strong>{contract?.label ?? step.contract}</strong>
              <button type="button" onClick={() => w.removeSetup(step.id)} title="Remove this default" aria-label="Remove default"><Trash2 aria-hidden="true" /></button>
            </div>
            {contract && <ContractFields contract={contract} values={step.values} actions={w.actions} pinLabel={(pin, label) => pinDisplayLabel(w.catalog, device.ref, pin, label)} onChange={(values) => w.updateSetup(step.id, { values })} />}
          </div>
        )
      })}

      {addable.length > 0 && (
        <div className="devices-default-add">
          <SelectField value={adding} onChange={(event) => setAdding(event.target.value)} aria-label="Default to add">
            <option value="">Add a default setting…</option>
            {addable.map((contract) => <option key={contract.id} value={contract.id}>{contract.label}</option>)}
          </SelectField>
          <button type="button" disabled={!adding} onClick={() => {
            const contract = addable.find((entry) => entry.id === adding)
            if (contract) w.addSetup(device.ref, contract.id, initialValues(contract))
            setAdding('')
          }}><Plus aria-hidden="true" />Add</button>
        </div>
      )}
    </Card>
  )
}
