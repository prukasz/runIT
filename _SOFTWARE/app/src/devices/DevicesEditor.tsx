import { Fragment, useEffect, useMemo, useState } from 'react'
import { AlertCircle, ArrowRight, BookOpen, FileText, Lock, Play, Plus, Trash2 } from 'lucide-react'
import { Button, ToggleChip, buttonClass } from '../components/Button'
import { Card, CardStack } from '../components/Card'
import { ChoiceField } from '../components/ChoiceField'
import { EditableField } from '../components/EditableField'
import { FormGrid, FormRow, NumberField, TextField } from '../components/FormField'
import {
  BOARD_DEVICES,
  DEVICE_TYPES,
  devicePinModes,
  deviceProvidesAnyMode,
  deviceType,
  entryRequiredModes,
  fieldChoices,
  getDevicePins,
  installEntries,
  isOutputMode,
  MAX_DEVICE_ID,
  NO_DEVICE,
  pinDisplayLabel,
  pinKey,
  pinUsers,
} from '../domain/devices'
import type { BoardDevice, DeviceType, InstallEntry, ProjectDevice } from '../domain/devices'
import { ActionSteps } from './ActionSteps'
import { DeviceTile } from './DeviceTile'
import { Markdown } from './Markdown'
import { boardRef } from './useDevicesWorkspace'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import './Devices.css'

/*
 * The middle of the Devices tab, as the legacy app had it: every device as a card, the device types to add (filtered by their tags),
 * and a device's page: its name, ID and description, its Configuration (the create struct as a form, built from the descriptor:
 * aliases, units, limits, choices, pins of other devices) and its User guide (the driver's Markdown) with its commands.
 */

/** User guides with pictures (app/docs/devices/<descriptor id>.md); a type without one shows its driver's own guide. */
const USER_GUIDES = Object.fromEntries(
  Object.entries(import.meta.glob<string>('../../docs/devices/*.md', { query: '?raw', import: 'default', eager: true })).map(([path, text]) => [path.replace(/^.*\/(.*)\.md$/, '$1'), text]),
)

export function DevicesEditor({ workspace: w }: { workspace: DevicesWorkspace }) {
  const selection = w.selection
  if (selection.kind === 'add') return <DeviceTypeCatalog workspace={w} />
  if (selection.kind === 'device') return <DevicePage key={selection.ref} workspace={w} deviceRef={selection.ref} />
  if (selection.kind === 'action') return <ActionPage key={selection.id} workspace={w} actionRef={selection.id} />
  return <DevicesOverview workspace={w} />
}

function DevicesOverview({ workspace: w }: { workspace: DevicesWorkspace }) {
  const cards = [
    ...BOARD_DEVICES.map((device) => ({ key: boardRef(device.id), name: w.boardName(device.id), sub: device.type?.alias ?? device.alias, type: device.type })),
    ...w.devices.map((device) => ({ key: device.id, name: device.name, sub: deviceType(device.type)?.alias ?? device.name, type: deviceType(device.type) })),
  ]
  return (
    <div className="devices-editor">
      <div className="devices-page-header">
        <DeviceTile size="large" />
        <div>
          <h1>Board devices</h1>
          <p className="devices-muted">{BOARD_DEVICES.length} devices on the board, {w.devices.length} added.</p>
        </div>
      </div>
      <div className="devices-card-grid">
        {cards.map((entry) => (
          <button key={entry.key} type="button" className="devices-card" onClick={() => w.select({ kind: 'device', ref: entry.key })}>
            <DeviceTile type={entry.type} />
            <strong>{entry.name}</strong>
            <span>{entry.sub}</span>
          </button>
        ))}
        {w.actions.map((action) => (
          <button key={action.id} type="button" className="devices-card" onClick={() => w.select({ kind: 'action', id: action.id })}>
            <span className="device-tile is-medium"><Play aria-hidden="true" /></span>
            <strong>{action.name}</strong>
            <span>Action {action.actionId} · {action.steps.length} {action.steps.length === 1 ? 'step' : 'steps'}</span>
          </button>
        ))}
        <button type="button" className="devices-card is-add" onClick={() => w.select({ kind: 'add' })}>
          <span className="device-tile is-medium"><Plus aria-hidden="true" /></span>
          <strong>Add a device</strong>
          <span>From the firmware's device types</span>
        </button>
      </div>
    </div>
  )
}

/** The device types a user can add: the ones whose create struct the firmware publishes. */
function DeviceTypeCatalog({ workspace: w }: { workspace: DevicesWorkspace }) {
  const [tags, setTags] = useState<ReadonlySet<string>>(new Set())
  const addable = DEVICE_TYPES.filter((type) => type.create)
  const allTags = useMemo(() => [...new Set(addable.flatMap((type) => type.tags))].sort(), [addable])
  const types = addable.filter((type) => [...tags].every((tag) => type.tags.includes(tag)))
  const toggle = (tag: string) => setTags((current) => {
    const next = new Set(current)
    if (!next.delete(tag)) next.add(tag)
    return next
  })
  return (
    <div className="devices-editor">
      <div className="devices-page-header">
        <span className="device-tile is-large"><Plus aria-hidden="true" /></span>
        <div>
          <h1>Add a device</h1>
          <p className="devices-muted">Device types the firmware can install. The new device gets the next free device ID.</p>
        </div>
      </div>
      <div className="devices-tags" role="group" aria-label="Filter by tag">
        {allTags.map((tag) => <ToggleChip key={tag} selected={tags.has(tag)} onClick={() => toggle(tag)}>{tag}</ToggleChip>)}
      </div>
      <div className="devices-card-grid">
        {types.map((type) => (
          <button key={type.id} type="button" className="devices-card is-type" onClick={() => w.addDevice(type.id)} title={`Add ${type.alias}`}>
            <DeviceTile type={type} />
            <strong>{type.alias}</strong>
            <span>{type.description}</span>
            <span className="devices-card-meta">{type.protocols.join(' · ')}</span>
          </button>
        ))}
        {!types.length && <p className="devices-muted">No device type has all of these tags.</p>}
      </div>
    </div>
  )
}

function DevicePage({ workspace: w, deviceRef }: { workspace: DevicesWorkspace; deviceRef: string }) {
  const [page, setPage] = useState<'configuration' | 'guide'>('configuration')
  const board = deviceRef.startsWith('board:') ? BOARD_DEVICES.find((device) => boardRef(device.id) === deviceRef) : undefined
  const device = board ? undefined : w.devices.find((entry) => entry.id === deviceRef)
  if (!board && !device) return <div className="devices-editor"><p className="devices-muted">This device is gone.</p></div>
  const type = board ? board.type : deviceType(device!.type)
  const problems = device ? w.problems.filter((problem) => problem.deviceId === device.id) : []

  return (
    <div className="devices-editor">
      <div className="devices-page-header">
        <DeviceTile type={type} size="large" />
        <div className="devices-page-title">
          <span className="object-editor-kicker">{board ? 'System device' : 'User device'}{type ? ` · ${type.alias}` : ''}</span>
          <div className="devices-header-name" role="heading" aria-level={1}>
            {device
              ? <EditableField aria-label="Device name" value={device.name} onChange={(name) => w.updateDevice(device.id, { name })} iconTitle="Edit device name" />
              : <EditableField aria-label="Device name" value={w.boardName(board!.id)} onChange={(name) => w.renameBoardDevice(board!.id, name)} iconTitle="Name it in this project (empty: the firmware's name)" />}
          </div>
          <label className="devices-header-id">
            <span>Device ID</span>
            {device
              ? <NumberField aria-label="Device ID" integer min={0} max={MAX_DEVICE_ID} value={device.deviceId} onCommit={(id) => w.updateDevice(device.id, { deviceId: id })} />
              : <span className="devices-header-id-fixed">{board!.id}</span>}
          </label>
          {board?.description && <p className="devices-muted">{board.description}</p>}
          {type && <p className="devices-muted">{type.description}</p>}
          {device && <EditableField className="devices-header-description" aria-label="Device description" value={device.description ?? ''} placeholder="Describe this device in your project" onChange={(description) => w.updateDevice(device.id, { description: description || undefined })} iconTitle="Edit device description" />}
        </div>
        <div className="devices-page-actions">
          {type?.datasheet && <a className={buttonClass()} href={type.datasheet} target="_blank" rel="noreferrer" title="Manufacturer datasheet"><FileText aria-hidden="true" /><span>Datasheet</span></a>}
          {device && <Button variant="danger" onClick={() => w.removeDevice(device.id)} title="Delete this device"><Trash2 aria-hidden="true" /><span>Delete</span></Button>}
        </div>
      </div>

      <div className="devices-subtabs" role="tablist" aria-label="Device pages">
        <button role="tab" aria-selected={page === 'configuration'} className={page === 'configuration' ? 'selected' : ''} onClick={() => setPage('configuration')}>Configuration</button>
        <button role="tab" aria-selected={page === 'guide'} className={page === 'guide' ? 'selected' : ''} onClick={() => setPage('guide')}><BookOpen aria-hidden="true" />User guide</button>
      </div>

      {page === 'guide' ? <DeviceGuide type={type} /> : (
        <CardStack>
          {problems.map((problem, index) => <p key={index} className="program-diag is-error"><AlertCircle aria-hidden="true" />{problem.message}</p>)}
          {device && type && <InstallCard workspace={w} device={device} type={type} />}
          {board && <BoardInstallCard board={board} />}
          <PinsUsedCard workspace={w} deviceRef={deviceRef} />
          <DefaultSettingsCard workspace={w} deviceRef={deviceRef} deviceId={board ? board.id : device!.deviceId} type={type} />
          <PinNamesCard workspace={w} deviceRef={deviceRef} type={type} />
          {device && <TagsCard workspace={w} device={device} type={type} />}
          {!type && <Card><p className="devices-muted">The firmware publishes no descriptor for this device type.</p></Card>}
        </CardStack>
      )}
    </div>
  )
}

const refOf = (w: DevicesWorkspace, deviceId: number): string | undefined =>
  BOARD_DEVICES.some((entry) => entry.id === deviceId) ? boardRef(deviceId) : w.devices.find((entry) => entry.deviceId === deviceId)?.id

function DevicePill({ workspace: w, deviceRef, name, detail }: { workspace: DevicesWorkspace; deviceRef?: string; name: string; detail: string }) {
  const board = deviceRef?.startsWith('board:') ? BOARD_DEVICES.find((device) => boardRef(device.id) === deviceRef) : undefined
  const dev = !board && deviceRef ? w.devices.find((entry) => entry.id === deviceRef) : undefined
  const type = board ? board.type : dev ? deviceType(dev.type) : undefined
  return (
    <button
      type="button"
      className="devices-pill"
      disabled={!board && !dev}
      onClick={() => deviceRef && w.select({ kind: 'device', ref: deviceRef })}
      title={dev ? `Open ${dev.name}` : board ? `Open ${w.boardName(board.id)}` : undefined}
    >
      <DeviceTile type={type} size="small" />
      <span className="devices-pill-name">{name}</span>
      <span className="devices-pill-detail">{detail}</span>
    </button>
  )
}

function BoardInstallCard({ board }: { board: BoardDevice }) {
  return (
    <Card title="Installed on the board">
      <p className="devices-muted">{board.description ?? 'System device created by the firmware at every boot.'}</p>
      <dl className="devices-facts">
        <div><dt>Device</dt><dd>{board.alias}</dd></div>
        {board.type?.alias && <div><dt>Driver</dt><dd>{board.type.alias}</dd></div>}
      </dl>
    </Card>
  )
}

const MODE_NAMES: Record<number, string> = {
  0: 'Input',
  1: 'Input + Pullup',
  2: 'Input + Pulldown',
  3: 'Output',
  4: 'Open-Drain',
  5: 'Open-Drain + Pullup',
  6: 'PWM',
  7: 'ADC',
  8: 'DAC',
}

const modeName = (type: DeviceType | undefined, mode: number): string => {
  const fromType = devicePinModes(type).find((m) => m.value === mode)?.alias
  if (fromType) return fromType
  return MODE_NAMES[mode] ?? `Mode ${mode}`
}

function PinsUsedCard({ workspace: w, deviceRef }: { workspace: DevicesWorkspace; deviceRef: string }) {
  const users = pinUsers(w.devices)
  const links: { use: string; targetDevice: number; targetPin: number; mode?: number }[] = []
  for (const [, userList] of users) {
    for (const u of userList) {
      if (u.ownerRef === deviceRef) {
        links.push({ use: u.use, targetDevice: u.targetDevice, targetPin: u.targetPin, mode: u.mode })
      }
    }
  }
  if (!links.length) return null
  return (
    <Card title="Pins it uses" className="devices-refs-card">
      <div className="devices-ref-grid">
        {links.map((link, idx) => {
          const targetRef = refOf(w, link.targetDevice)
          const targetBoard = targetRef?.startsWith('board:') ? BOARD_DEVICES.find((d) => boardRef(d.id) === targetRef) : undefined
          const targetUser = !targetBoard && targetRef ? w.devices.find((d) => d.id === targetRef) : undefined
          const targetName = targetBoard ? w.boardName(targetBoard.id) : targetUser ? targetUser.name : `Device ${link.targetDevice}`
          const targetType = targetBoard ? targetBoard.type : targetUser ? deviceType(targetUser.type) : undefined
          const targetPinLabel = getDevicePins(targetType).find((p) => p.pin === link.targetPin)?.label
          const detail = pinDisplayLabel(w.pinAliases, targetRef ?? '', link.targetPin, targetPinLabel)
          return (
            <div key={idx} className="devices-ref-row">
              <span className="devices-ref-label">{link.use}</span>
              <ArrowRight className="devices-ref-arrow" aria-hidden="true" />
              <DevicePill workspace={w} deviceRef={targetRef} name={targetName} detail={detail} />
              {link.mode !== undefined && <span className="devices-mode-chip">{modeName(targetType, link.mode)}</span>}
            </div>
          )
        })}
      </div>
    </Card>
  )
}

function PinAliasField({ pin, label, alias, onSave, inTable = false }: { pin: number; label: string; alias: string; onSave: (value: string) => void; inTable?: boolean }) {
  const [draft, setDraft] = useState(alias)
  useEffect(() => {
    setDraft(alias)
  }, [alias])
  return (
    <span className="devices-pin-name" role={inTable ? 'cell' : undefined}>
      <span>#{pin}</span>
      <TextField
        aria-label={`Alias for pin ${pin}`}
        title={`Name for pin ${pin}`}
        placeholder={label === String(pin) ? 'Add alias' : label}
        value={draft}
        onChange={(event) => setDraft(event.target.value)}
        onBlur={() => onSave(draft)}
        onKeyDown={(event) => {
          if (event.key === 'Enter') event.currentTarget.blur()
        }}
      />
    </span>
  )
}

function PinNamesCard({ workspace: w, deviceRef, type }: { workspace: DevicesWorkspace; deviceRef: string; type?: DeviceType }) {
  const pins = getDevicePins(type)
  const hasModeOp = type?.contracts.flatMap((c) => c.ops).some((op) => op.symbol === 'IO_OP_SET_MODE')
  if (!pins.length || hasModeOp) return null
  return (
    <Card title="Pin names">
      <div className="devices-pin-names">
        {pins.map((pin) => (
          <PinAliasField
            key={`${deviceRef}:${pin.pin}:${w.pinAliases?.[deviceRef]?.[String(pin.pin)] ?? ''}`}
            pin={pin.pin}
            label={pin.label}
            alias={w.pinAliases?.[deviceRef]?.[String(pin.pin)] ?? ''}
            onSave={(value) => w.setPinAlias(deviceRef, pin.pin, value)}
          />
        ))}
      </div>
    </Card>
  )
}

/** The commands the board runs on this device after it is installed: stored as the device's record, together with its CREATE. */
function DefaultSettingsCard({ workspace: w, deviceRef, deviceId, type }: { workspace: DevicesWorkspace; deviceRef: string; deviceId: number; type?: DeviceType }) {
  const pins = getDevicePins(type)
  const modes = devicePinModes(type)
  const hasSetMode = type?.contracts.flatMap((c) => c.ops).some((op) => op.symbol === 'IO_OP_SET_MODE')
  const hasSetLevel = type?.contracts.flatMap((c) => c.ops).some((op) => op.symbol === 'IO_OP_SET_LEVEL')
  const table = pins.length > 0 && !!hasSetMode

  const users = pinUsers(w.devices)
  const steps = w.setup.filter((step) => step.device === deviceId)
  const pinSteps = steps.filter((step) => step.op === 'IO_OP_SET_MODE' || step.op === 'IO_OP_SET_LEVEL')
  const otherSteps = steps.filter((step) => step.op !== 'IO_OP_SET_MODE' && step.op !== 'IO_OP_SET_LEVEL')
  const problems = w.stepProblems.filter((problem) => problem.subject === `setup:${deviceId}`)

  return (
    <Card title="Default settings">
      <p className="devices-muted">
        {table
          ? 'Device defaults apply at boot; project settings run after installation.'
          : 'Commands that run on this device when the project starts, after it is installed. Saving stores them on the board as this device\'s record.'}
      </p>
      {problems.map((problem, index) => (
        <p key={index} className="program-diag is-error">
          <AlertCircle aria-hidden="true" />
          {problem.message}
        </p>
      ))}

      {table && (
        <div className="devices-pin-table" role="table" aria-label="Pin defaults">
          <div className="devices-pin-row is-head" role="row">
            <span role="columnheader">Pin</span>
            <span role="columnheader">Mode</span>
            <span role="columnheader">{hasSetLevel ? 'Level' : ''}</span>
          </div>
          {pins.map((pin) => {
            const taken = (users.get(pinKey(deviceId, pin.pin)) ?? []).filter((user) => user.ownerRef !== deviceRef)
            const modeStep = steps.find((step) => step.op === 'IO_OP_SET_MODE' && step.values.pin === pin.pin)
            const levelStep = steps.find((step) => step.op === 'IO_OP_SET_LEVEL' && step.values.pin === pin.pin)
            const currentMode = modeStep?.values.mode
            return (
              <div key={pin.pin} className={`devices-pin-row ${taken.length ? 'is-taken' : ''} ${modeStep ? 'is-set' : ''}`} role="row">
                <PinAliasField
                  key={`${deviceRef}:${pin.pin}:${w.pinAliases?.[deviceRef]?.[String(pin.pin)] ?? ''}`}
                  pin={pin.pin}
                  label={pin.label}
                  alias={w.pinAliases?.[deviceRef]?.[String(pin.pin)] ?? ''}
                  onSave={(value) => w.setPinAlias(deviceRef, pin.pin, value)}
                  inTable
                />
                {taken.length ? (
                  <span className="devices-pin-taken" role="cell">
                    <span className="devices-taken">
                      <Lock aria-hidden="true" />
                      {taken.map((user, idx) => (
                        <Fragment key={idx}>
                          <DevicePill workspace={w} deviceRef={user.ownerRef} name={user.ownerName} detail={user.use} />
                          {user.mode !== undefined && <em>{MODE_NAMES[user.mode] ?? `Mode ${user.mode}`}</em>}
                        </Fragment>
                      ))}
                    </span>
                  </span>
                ) : (
                  <>
                    <span role="cell">
                      <ChoiceField<number>
                        aria-label={`Pin ${pin.pin} mode`}
                        value={currentMode}
                        emptyLabel="— not set —"
                        choices={modes.map((choice) => ({ value: choice.value, label: choice.alias }))}
                        onChange={(next) => w.setPinMode(deviceRef, pin.pin, next)}
                      />
                    </span>
                    <span role="cell">
                      {hasSetLevel && isOutputMode(currentMode) ? (
                        <ChoiceField<number>
                          aria-label={`Pin ${pin.pin} level`}
                          value={levelStep?.values.level}
                          emptyLabel="—"
                          choices={[
                            { value: 0, label: 'Low' },
                            { value: 1, label: 'High' },
                          ]}
                          onChange={(next) => w.setPinLevel(deviceRef, pin.pin, next)}
                        />
                      ) : null}
                    </span>
                  </>
                )}
              </div>
            )
          })}
        </div>
      )}

      <ActionSteps
        workspace={w}
        steps={table ? otherSteps : steps}
        fixedDevice={deviceId}
        onChange={(next) => w.setSetup(deviceId, table ? [...pinSteps, ...next] : next)}
      />
    </Card>
  )
}

/** An action: commands of any devices, in order, stored on the board under its action ID. */
function ActionPage({ workspace: w, actionRef }: { workspace: DevicesWorkspace; actionRef: string }) {
  const action = w.actions.find((entry) => entry.id === actionRef)
  if (!action) return <div className="devices-editor"><p className="devices-muted">This action is gone.</p></div>
  const problems = w.stepProblems.filter((problem) => problem.subject === action.id)
  return (
    <div className="devices-editor">
      <div className="devices-page-header">
        <span className="device-tile is-large"><Play aria-hidden="true" /></span>
        <div className="devices-page-title">
          <span className="object-editor-kicker">Action</span>
          <div className="devices-header-name" role="heading" aria-level={1}>
            <EditableField aria-label="Action name" value={action.name} onChange={(name) => w.updateAction(action.id, { name })} iconTitle="Edit action name" />
          </div>
          <label className="devices-header-id">
            <span>Action ID</span>
            <NumberField aria-label="Action ID" integer min={1} max={0xffffff} value={action.actionId} onCommit={(actionId) => w.updateAction(action.id, { actionId })} />
          </label>
          <p className="devices-muted">The board keeps the action under this ID; the project, a block or an event runs it. The name stays in the project.</p>
        </div>
        <div className="devices-page-actions">
          <Button variant="danger" onClick={() => w.removeAction(action.id)} title="Delete this action"><Trash2 aria-hidden="true" /><span>Delete</span></Button>
        </div>
      </div>
      <CardStack>
        <Card title="Steps">
          {problems.map((problem, index) => <p key={index} className="program-diag is-error"><AlertCircle aria-hidden="true" />{problem.message}</p>)}
          <ActionSteps workspace={w} steps={action.steps} onChange={(steps) => w.updateAction(action.id, { steps })} />
        </Card>
      </CardStack>
    </div>
  )
}

/** The devices a pin can be on: every device whose type has the IO contract and satisfies the required modes. */
const pinHosts = (w: DevicesWorkspace, except: string, requiredModes?: readonly number[]) => {
  const matches = (type?: DeviceType) => {
    if (!type?.contracts.some((contract) => contract.contract === 'IO')) return false
    return deviceProvidesAnyMode(type, requiredModes ?? [])
  }
  return [
    ...BOARD_DEVICES.filter((device) => matches(device.type)).map((device) => ({ value: device.id, label: `${w.boardName(device.id)} (#${device.id})` })),
    ...w.devices.filter((device) => device.id !== except && matches(deviceType(device.type))).map((device) => ({ value: device.deviceId, label: `${device.name} (#${device.deviceId})` })),
  ]
}

/** The create struct as a form: every number by its alias (choices as a list), every pin as the device it is on and its number. */
function InstallCard({ workspace: w, device, type }: { workspace: DevicesWorkspace; device: ProjectDevice; type: DeviceType }) {
  const entries = installEntries(type)
  const set = (patch: Record<string, number>) => w.updateDevice(device.id, { install: { ...device.install, ...patch } })
  const users = pinUsers(w.devices)
  if (!entries.length) return <Card title="Install"><p className="devices-muted">This device type takes no settings.</p></Card>
  const row = (entry: InstallEntry) => {
    const note = [entry.field.description, entry.field.unit ? `(${entry.field.unit})` : ''].filter(Boolean).join(' ')
    if (entry.kind === 'pin') {
      const requiredModes = entryRequiredModes(entry.field)
      const hosts = pinHosts(w, device.id, requiredModes)
      const on = device.install[`${entry.key}.device`] ?? NO_DEVICE
      const modes = entry.field.modes?.map((mode) => (typeof mode === 'object' && mode && 'alias' in mode ? String((mode as { alias?: string }).alias) : String(mode))).filter(Boolean).join(', ')
        || (entry.field.default_mode !== undefined ? (MODE_NAMES[entry.field.default_mode] ?? `Mode ${entry.field.default_mode}`) : undefined)
      const targetRef = refOf(w, on)
      const targetBoard = targetRef?.startsWith('board:') ? BOARD_DEVICES.find((d) => boardRef(d.id) === targetRef) : undefined
      const targetDev = !targetBoard && targetRef ? w.devices.find((d) => d.id === targetRef) : undefined
      const targetType = targetBoard ? targetBoard.type : targetDev ? deviceType(targetDev.type) : undefined
      const targetPins = getDevicePins(targetType, requiredModes)
      const currentPin = device.install[`${entry.key}.pin`] ?? 0

      return (
        <FormRow key={entry.key} label={<>{entry.label}{entry.field.required ? ' *' : ''}</>} note={[note, modes ? `Modes: ${modes}.` : ''].filter(Boolean).join(' ')}>
          <div className="devices-pin-pick">
            <ChoiceField<number>
              selectClassName="form-field-panel"
              value={on}
              unavailableLabel={(id) => `Device #${id}`}
              choices={[{ value: NO_DEVICE, label: 'Not connected' }, ...hosts]}
              onChange={(next) => {
                if (next === undefined) return
                if (next === NO_DEVICE) {
                  set({ [`${entry.key}.device`]: NO_DEVICE, [`${entry.key}.pin`]: 0 })
                  return
                }
                const nextRef = refOf(w, next)
                const nextBoard = nextRef?.startsWith('board:') ? BOARD_DEVICES.find((d) => boardRef(d.id) === nextRef) : undefined
                const nextDev = !nextBoard && nextRef ? w.devices.find((d) => d.id === nextRef) : undefined
                const nextType = nextBoard ? nextBoard.type : nextDev ? deviceType(nextDev.type) : undefined
                const nextPins = getDevicePins(nextType, requiredModes)
                const firstFree = nextPins.find((p) => {
                  const taken = (users.get(pinKey(next, p.pin)) ?? []).filter((u) => u.ownerRef !== device.id)
                  return taken.length === 0
                })?.pin ?? nextPins[0]?.pin ?? 0
                set({ [`${entry.key}.device`]: next, [`${entry.key}.pin`]: firstFree })
              }}
            />
            {on !== NO_DEVICE && (
              targetPins.length > 0 ? (
                <ChoiceField<number>
                  selectClassName="form-field-panel"
                  value={currentPin}
                  unavailableLabel={(pin) => `Pin ${pin}`}
                  choices={targetPins.map((p) => {
                    const taken = (users.get(pinKey(on, p.pin)) ?? []).filter((u) => u.ownerRef !== device.id || u.use !== entry.label)
                    const label = pinDisplayLabel(w.pinAliases, targetRef ?? '', p.pin, p.label)
                    return {
                      value: p.pin,
                      label: taken.length > 0 ? `${label} — ${taken.map((u) => u.ownerName).join(', ')}` : label,
                      disabled: taken.length > 0,
                    }
                  })}
                  onChange={(pin) => pin !== undefined && set({ [`${entry.key}.pin`]: pin })}
                />
              ) : (
                <NumberField aria-label={`${entry.label} pin`} integer min={0} max={254} value={currentPin} onCommit={(pin) => set({ [`${entry.key}.pin`]: pin })} />
              )
            )}
          </div>
        </FormRow>
      )
    }
    const choices = fieldChoices(entry.field)
    const value = device.install[entry.key] ?? 0
    return (
      <FormRow key={entry.key} label={<>{entry.label}{entry.field.unit ? ` (${entry.field.unit})` : ''}</>} note={entry.field.description}>
        {choices
          ? <ChoiceField<number> selectClassName="form-field-panel" value={value} unavailableLabel={(entry_) => `Invalid: ${entry_}`} choices={choices.map((choice) => ({ value: choice.value, label: choice.alias }))} onChange={(next) => next !== undefined && set({ [entry.key]: next })} />
          : <NumberField className="form-field-panel" integer min={entry.field.min} max={entry.field.max} value={value} onCommit={(next) => set({ [entry.key]: next })} />}
      </FormRow>
    )
  }
  return (
    <Card title="Install">
      <FormGrid>{entries.map(row)}</FormGrid>
    </Card>
  )
}

function TagsCard({ workspace: w, device, type }: { workspace: DevicesWorkspace; device: ProjectDevice; type?: DeviceType }) {
  const [newTag, setNewTag] = useState('')
  const choices = [...new Set([...(type?.tags ?? []), ...device.tags])]
  const toggle = (tag: string) => w.updateDevice(device.id, { tags: device.tags.includes(tag) ? device.tags.filter((entry) => entry !== tag) : [...device.tags, tag] })
  return (
    <Card title="Tags">
      <div className="devices-tags">
        {choices.map((tag) => <ToggleChip key={tag} selected={device.tags.includes(tag)} onClick={() => toggle(tag)}>{tag}</ToggleChip>)}
        <form className="devices-tag-add" onSubmit={(event) => { event.preventDefault(); const tag = newTag.trim().toLowerCase(); if (tag && !device.tags.includes(tag)) w.updateDevice(device.id, { tags: [...device.tags, tag] }); setNewTag('') }}>
          <TextField value={newTag} placeholder="new tag" aria-label="New tag" onChange={(event) => setNewTag(event.target.value)} />
        </form>
      </div>
    </Card>
  )
}


function DeviceGuide({ type }: { type?: DeviceType }) {
  const guide = type ? USER_GUIDES[type.id] ?? type.guide : undefined
  return (
    <CardStack>
      <Card>{guide ? <Markdown source={guide} /> : <p className="devices-muted">No guide for this device yet.</p>}</Card>
      {type?.datasheet && <Card><p><a href={type.datasheet} target="_blank" rel="noreferrer">Datasheet</a></p></Card>}
    </CardStack>
  )
}
