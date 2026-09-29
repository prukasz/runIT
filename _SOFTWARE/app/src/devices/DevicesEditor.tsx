import { useId, useMemo, useState } from 'react'
import { Card, CardStack } from '../components/Card'
import { FormGrid, FormRow, SelectField, TextField } from '../components/FormField'
import { AlertCircle, AlertTriangle, ArrowDown, ArrowUp, BookOpen, FileText, ImagePlus, ListPlus, Play, Plus, Trash2, Upload, X } from 'lucide-react'
import type { DeviceType } from '../domain/descriptors'
import { runitCommandCatalog, runitDeviceCatalog } from '../domain/descriptors'
import { actionRecordSteps, actionRunStep, allDevices, buildAction, contractsOf, describeStep, deviceDisplayName, findContract, modesOf, pinKey, pinsOf, pinUsers, resolveDevice } from '../domain/devices'
import type { ResolvedDevice } from '../domain/devices'
import type { ActionStep, ProjectAction, ProjectDevice } from '../domain/project'
import { Button, ToggleChip, buttonClass } from '../components/Button'
import { EditableField } from '../components/EditableField'
import { FieldLabel } from '../components/FieldNote'
import { ContractFields, initialValues } from './ContractFields'
import { DEVICE_ICONS, DeviceTile } from './DeviceTile'
import { Markdown } from './Markdown'
import { BoardInstallCard, DefaultSettingsCard, PinsUsedCard } from './DeviceSettings'
import { ErrorHandlingCard } from './ErrorHandling'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import type { RunitBleSession } from '../backend/runitBleSession'
import { sendSteps } from '../sendSteps'

/** Device guides: app/docs/devices/<descriptor id>.md (optional, written by hand). */
const GUIDES = Object.fromEntries(
  Object.entries(import.meta.glob<string>('../../docs/devices/*.md', { query: '?raw', import: 'default', eager: true })).map(([path, text]) => [path.replace(/^.*\/(.*)\.md$/, '$1'), text]),
)

const IMAGE_MAX_BYTES = 256 * 1024
const hex2 = (value: number): string => `0x${value.toString(16).padStart(2, '0').toUpperCase()}`

export function DevicesEditor({ workspace: w, session }: { workspace: DevicesWorkspace; session?: RunitBleSession }) {
  const selection = w.selection
  return (
    <>
      {selection?.kind === 'add' ? <DeviceTypeCatalog workspace={w} />
        : selection?.kind === 'device' ? <DevicePage key={selection.ref} workspace={w} deviceRef={selection.ref} />
        : selection?.kind === 'action' ? <ActionSummary workspace={w} actionId={selection.id} />
        : <DevicesOverview workspace={w} />}
      {w.composing && <ActionComposer workspace={w} action={w.composing} session={session} />}
    </>
  )
}

function DevicesOverview({ workspace: w }: { workspace: DevicesWorkspace }) {
  return (
    <div className="object-editor devices-editor">
      <div className="devices-page-header">
        <DeviceTile size="large" />
        <div>
          <h1>Board devices</h1>
          <p className="devices-muted">{w.catalog.board.length} devices on the board, {w.devices.length} added, {w.actions.length} actions.</p>
        </div>
      </div>
      <div className="devices-card-grid">
        {[...w.catalog.board.map((device) => ({ key: `b${device.deviceId}`, ref: `board:${device.deviceId}`, name: device.name, sub: device.title, type: device.type, appearance: undefined })),
          ...w.devices.map((device) => ({ key: device.id, ref: device.id, name: deviceDisplayName(w.catalog, device), sub: w.catalog.type(device.type)?.title ?? device.type, type: w.catalog.type(device.type), appearance: device.appearance }))].map((entry) => (
          <button key={entry.key} type="button" className="devices-card" onClick={() => w.select({ kind: 'device', ref: entry.ref })}>
            <DeviceTile appearance={entry.appearance} type={entry.type} />
            <strong>{entry.name}</strong>
            <span>{entry.sub}</span>
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

// ---------------------------------------------------------------------------
// Add: the device types the firmware can install
// ---------------------------------------------------------------------------

function DeviceTypeCatalog({ workspace: w }: { workspace: DevicesWorkspace }) {
  const [tags, setTags] = useState<ReadonlySet<string>>(new Set())
  const allTags = useMemo(() => [...new Set(w.catalog.types.flatMap((type) => type.tags))].sort(), [w.catalog])
  const types = w.catalog.types.filter((type) => [...tags].every((tag) => type.tags.includes(tag)))
  const toggle = (tag: string) => setTags((current) => {
    const next = new Set(current)
    if (next.has(tag)) next.delete(tag)
    else next.add(tag)
    return next
  })
  return (
    <div className="object-editor devices-editor">
      <div className="devices-page-header">
        <span className="device-tile is-large"><Plus aria-hidden="true" /></span>
        <div>
          <h1>Add a device</h1>
          <p className="devices-muted">Device types the firmware can install. The new device gets the next free device ID; the stored code installs it at every boot.</p>
        </div>
      </div>
      <div className="devices-tags" role="group" aria-label="Filter by tag">
        {allTags.map((tag) => <ToggleChip key={tag} selected={tags.has(tag)} onClick={() => toggle(tag)}>{tag}</ToggleChip>)}
      </div>
      {w.error && <p className="program-diag is-error"><AlertCircle aria-hidden="true" />{w.error}</p>}
      <div className="devices-card-grid">
        {types.map((type) => (
          <button key={type.id} type="button" className="devices-card is-type" onClick={() => w.addDevice(type.id)} title={`Add ${type.title}`}>
            <DeviceTile type={type} />
            <strong>{type.title}</strong>
            <span>{type.description}</span>
            <span className="devices-card-meta">{[type.provider?.label, ...type.protocols].filter(Boolean).join(' · ')}</span>
          </button>
        ))}
        {!types.length && <p className="devices-muted">No device type has all of these tags.</p>}
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Device page
// ---------------------------------------------------------------------------

function DevicePage({ workspace: w, deviceRef }: { workspace: DevicesWorkspace; deviceRef: string }) {
  const [page, setPage] = useState<'configuration' | 'guide'>('configuration')
  const resolved = resolveDevice(w.catalog, w.devices, deviceRef)
  const device = w.devices.find((entry) => entry.id === deviceRef)
  if (!resolved) return <div className="object-editor devices-editor"><p className="devices-muted">This device is gone.</p></div>
  const type = resolved.type
  const problems = w.diagnostics.filter((entry) => entry.subjectId === deviceRef)
  const firstContract = contractsOf(w.catalog, resolved)[0]

  return (
    <div className="object-editor devices-editor">
      <div className="devices-page-header">
        <DeviceTile appearance={device?.appearance} type={type} size="large" />
        <div className="devices-page-title">
          <span className="object-editor-kicker">{resolved.system ? 'System device' : 'User device'}{type ? ` · ${type.title}` : ''}</span>
          <div className="devices-header-name" role="heading" aria-level={1} aria-label={resolved.name}>
            {device
              ? <EditableField aria-label="Device name" value={device.name} onChange={(name) => w.updateDevice(device.id, { name })} iconTitle="Edit device name" />
              : <BoardNameField key={w.deviceAliases?.[deviceRef] ?? ''} name={resolved.name} original={runitDeviceCatalog().board.find((entry) => entry.deviceId === resolved.deviceId)?.name ?? resolved.name} onSave={(name) => w.setDeviceAlias(deviceRef, name)} />}
          </div>
          <label className="devices-header-id">
            <span>Device ID</span>
            {device
              ? <TextField aria-label="Device ID" type="number" min={0} max={w.catalog.maxDeviceId} value={device.deviceId} onChange={(event) => w.updateDevice(device.id, { deviceId: Math.max(0, Math.floor(Number(event.target.value) || 0)) })} />
              : <span className="devices-header-id-fixed">{resolved.deviceId}</span>}
          </label>
          {type && <p className="devices-muted">{type.description}</p>}
          {device && <EditableField className="devices-header-description" aria-label="Device description" value={device.description ?? ''} placeholder="Describe this device in your project" onChange={(description) => w.updateDevice(device.id, { description: description || undefined })} iconTitle="Edit device description" />}
        </div>
        <div className="devices-page-actions">
          <Button variant="primary" disabled={!firstContract} onClick={() => firstContract && w.compose(w.composing?.id, { device: deviceRef, contract: firstContract.id, values: initialValues(firstContract) })} title="Open the action composer with a step on this device">
            <ListPlus aria-hidden="true" /><span>Add to action</span>
          </Button>
          {type?.datasheet && <a className={buttonClass()} href={type.datasheet} target="_blank" rel="noreferrer" title="Manufacturer datasheet (PDF)"><FileText aria-hidden="true" /><span>Datasheet</span></a>}
        </div>
      </div>

      <div className="devices-subtabs" role="tablist" aria-label="Device pages">
        <button role="tab" aria-selected={page === 'configuration'} className={page === 'configuration' ? 'selected' : ''} onClick={() => setPage('configuration')}>Configuration</button>
        <button role="tab" aria-selected={page === 'guide'} className={page === 'guide' ? 'selected' : ''} onClick={() => setPage('guide')}><BookOpen aria-hidden="true" />User guide</button>
      </div>

      {page === 'guide' ? <DeviceGuide type={type} /> : (
        <CardStack>
          {problems.map((entry, index) => <p key={index} className="program-diag is-error"><AlertCircle aria-hidden="true" />{entry.message}</p>)}
          {device ? <UserDeviceConfig workspace={w} device={device} resolved={resolved} type={type} /> : <SystemDeviceInfo workspace={w} device={resolved} />}
        </CardStack>
      )}
    </div>
  )
}

function BoardNameField({ name, original, onSave }: { name: string; original: string; onSave: (name: string) => void }) {
  const [draft, setDraft] = useState(name)
  return <EditableField aria-label="Device name" value={draft} onChange={setDraft} onBlur={() => { onSave(draft); if (!draft.trim()) setDraft(original) }} onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }} iconTitle="Edit device name" />
}

function SystemDeviceInfo({ workspace: w, device }: { workspace: DevicesWorkspace; device: ResolvedDevice }) {
  const type = device.type
  return (
    <>
      <BoardInstallCard w={w} device={device} />
      <PinsUsedCard w={w} device={device} />
      <DefaultSettingsCard w={w} device={device} />
      <ErrorHandlingCard w={w} device={device} />
      {!type && <Card><p className="devices-muted">The firmware publishes no descriptor for this device yet: only the device commands (reset, suspend …) are available.</p></Card>}
    </>
  )
}

function UserDeviceConfig({ workspace: w, device, resolved, type }: { workspace: DevicesWorkspace; device: ProjectDevice; resolved: ResolvedDevice; type?: DeviceType }) {
  const [newTag, setNewTag] = useState('')
  const [imageError, setImageError] = useState('')
  const update = (patch: Partial<Omit<ProjectDevice, 'id' | 'type'>>) => w.updateDevice(device.id, patch)
  const tagChoices = [...new Set([...(type?.tags ?? []), ...device.tags])]
  const toggleTag = (tag: string) => update({ tags: device.tags.includes(tag) ? device.tags.filter((entry) => entry !== tag) : [...device.tags, tag] })

  const pickImage = async (file: File | undefined) => {
    if (!file) return
    if (file.size > IMAGE_MAX_BYTES) {
      setImageError(`The image is ${Math.round(file.size / 1024)} KB; up to ${IMAGE_MAX_BYTES / 1024} KB (it is saved in the project file).`)
      return
    }
    const reader = new FileReader()
    reader.onload = () => {
      setImageError('')
      update({ appearance: { image: String(reader.result) } })
    }
    reader.readAsDataURL(file)
  }

  return (
    <>
      {type && <InstallCard workspace={w} device={device} type={type} />}
      <PinsUsedCard w={w} device={resolved} />
      <DefaultSettingsCard w={w} device={resolved} />
      <ErrorHandlingCard w={w} device={resolved} />

      <Card title="Tags">
        <div className="devices-tags">
          {tagChoices.map((tag) => <ToggleChip key={tag} selected={device.tags.includes(tag)} onClick={() => toggleTag(tag)}>{tag}</ToggleChip>)}
          <form className="devices-tag-add" onSubmit={(event) => { event.preventDefault(); const tag = newTag.trim().toLowerCase(); if (tag && !device.tags.includes(tag)) update({ tags: [...device.tags, tag] }); setNewTag('') }}>
            <TextField value={newTag} placeholder="new tag" aria-label="New tag" onChange={(event) => setNewTag(event.target.value)} />
          </form>
        </div>
      </Card>

      <Card title="Tile">
        <div className="devices-tile-picker">
          <button type="button" className={`devices-tile-choice ${!device.appearance ? 'selected' : ''}`} title="Type default" onClick={() => update({ appearance: undefined })}><DeviceTile type={type} /></button>
          {Object.entries(DEVICE_ICONS).map(([name, Icon]) => (
            <button key={name} type="button" className={`devices-tile-choice ${device.appearance?.icon === name ? 'selected' : ''}`} title={name} aria-label={`Icon ${name}`} onClick={() => update({ appearance: { icon: name } })}>
              <span className="device-tile is-medium"><Icon aria-hidden="true" /></span>
            </button>
          ))}
          {device.appearance?.image && <span className="devices-tile-choice selected" title="Image"><DeviceTile appearance={device.appearance} /></span>}
          <label className="devices-tile-choice is-upload" title="Image or SVG (up to 256 KB)">
            <span className="device-tile is-medium"><ImagePlus aria-hidden="true" /></span>
            <TextField type="file" accept="image/png,image/jpeg,image/webp,image/svg+xml" hidden onChange={(event) => { void pickImage(event.target.files?.[0]); event.target.value = '' }} />
          </label>
        </div>
        {imageError && <p className="program-diag is-error"><AlertCircle aria-hidden="true" />{imageError}</p>}
      </Card>
    </>
  )
}

function InstallCard({ workspace: w, device, type }: { workspace: DevicesWorkspace; device: ProjectDevice; type: DeviceType }) {
  const set = (name: string, value: number) => w.updateDevice(device.id, { install: { ...device.install, [name]: value } })
  const grouped = new Set(type.pinGroups.flatMap((group) => [group.deviceField, group.pinField, group.modeField].filter(Boolean) as string[]))
  const plain = type.install.request.fields.filter((field) => field.name !== 'device_id' && field.name !== 'i2c_bus' && !grouped.has(field.name))
  // IO devices on this board: installed board devices and the other user devices.
  const targets = allDevices(w.catalog, w.devices).filter((entry) => entry.ref !== device.id && entry.type?.provider?.label.toLowerCase().includes('io') && (!entry.system || w.catalog.board.find((board) => board.deviceId === entry.deviceId)?.installed))
  const users = pinUsers(w.catalog, w.devices)
  const idBase = useId()

  return (
    <Card title="Install">
      <FormGrid>
        {plain.map((field) => {
          const choices = type.installChoices.get(field.name)
          const value = device.install[field.name] ?? 0
          return (
            <FormRow key={field.name} htmlFor={`${idBase}-${field.name}`} note={field.note} label={<>{field.label === field.name ? field.name.replaceAll('_', ' ') : field.label}{field.unit ? ` (${field.unit})` : ''}</>}>
              {choices ? (
                <SelectField id={`${idBase}-${field.name}`} className="form-field-panel" value={value} onChange={(event) => set(field.name, Number(event.target.value))}>
                  {!choices.some((choice) => choice.value === value) && <option value={value}>Invalid: {field.name === 'i2c_addr' ? hex2(value) : value}</option>}
                  {choices.map((choice) => <option key={choice.value} value={choice.value}>{field.name === 'i2c_addr' ? hex2(choice.value) : choice.label}</option>)}
                </SelectField>
              ) : (
                <TextField id={`${idBase}-${field.name}`} className="form-field-panel" type="number" min={field.min} max={field.max} value={value} onChange={(event) => set(field.name, Number(event.target.value))} />
              )}
            </FormRow>
          )
        })}
      </FormGrid>
      {type.pinGroups.map((group) => {
        const none = group.sentinelField !== undefined && device.install[group.sentinelField] === group.sentinel
        const modes = type.installChoices.get(group.modeField ?? '') ?? []
        const availableModes = (target: ResolvedDevice | undefined) => modes.filter((mode) => modesOf(target?.type).some((supported) => supported.value === mode.value))
        const groupTargets = group.modeField ? targets.filter((target) => availableModes(target).length > 0) : targets
        const targetId = group.deviceField ? device.install[group.deviceField] ?? 0 : 0
        const currentTarget = groupTargets.find((entry) => entry.deviceId === targetId)
        const choices = availableModes(currentTarget)
        const currentMode = group.modeField ? device.install[group.modeField] ?? 0 : 0
        return (
          <div key={group.key} className="devices-pin-group">
            <div className="devices-pin-group-title">
              <FieldLabel note={group.note} className="devices-pin-group-name">{group.label}</FieldLabel>
              <div className="devices-segmented" role="radiogroup" aria-label={`${group.label} connection`}>
                <button type="button" role="radio" aria-checked={none} className={none ? 'selected' : ''} onClick={() => group.sentinelField && set(group.sentinelField, group.sentinel)}>Not connected</button>
                <button type="button" role="radio" aria-checked={!none} className={!none ? 'selected' : ''} onClick={() => none && group.sentinelField && set(group.sentinelField, 0)}>Connected</button>
              </div>
            </div>
            {!none && (
              <FormGrid>
                {group.deviceField && (
                  <FormRow label="On device">
                    <SelectField className="form-field-panel" value={targetId} onChange={(event) => {
                      const selected = Number(event.target.value)
                      const supported = availableModes(groupTargets.find((target) => target.deviceId === selected))
                      const preferred = type.install.request.fields.find((field) => field.name === group.modeField)?.defaultValue
                      const mode = supported.some((choice) => choice.value === currentMode) ? currentMode : supported.find((choice) => choice.value === preferred)?.value ?? supported[0]?.value
                      w.updateDevice(device.id, { install: { ...device.install, [group.deviceField!]: selected, ...(group.modeField && mode !== undefined ? { [group.modeField]: mode } : {}) } })
                    }}>
                      {!currentTarget && <option value={targetId}>Unavailable device #{targetId}</option>}
                      {groupTargets.map((target) => <option key={target.ref} value={target.deviceId}>{target.name} (#{target.deviceId})</option>)}
                    </SelectField>
                  </FormRow>
                )}
                {group.pinField && (() => {
                  const pins = pinsOf(currentTarget?.type)
                  const value = device.install[group.pinField] ?? 0
                  return (
                    <FormRow label="Pin">
                      {pins.length ? (
                        <SelectField className="form-field-panel" value={value} onChange={(event) => set(group.pinField!, Number(event.target.value))}>
                          {!pins.some((pin) => pin.value === value) && <option value={value}>Pin {value}</option>}
                          {pins.map((pin) => {
                            const taken = (users.get(pinKey(targetId, pin.value)) ?? []).filter((user) => user.owner !== device.id)
                            return <option key={pin.value} value={pin.value} disabled={taken.length > 0}>{`Pin ${pin.value}${taken.length ? ` — ${taken.map((user) => user.ownerName).join(', ')}` : ''}`}</option>
                          })}
                        </SelectField>
                      ) : (
                        <TextField className="form-field-panel" type="number" min={0} max={254} value={value} onChange={(event) => set(group.pinField!, Number(event.target.value))} />
                      )}
                    </FormRow>
                  )
                })()}
                {group.modeField && (
                  <FormRow label="Mode">
                    <SelectField className="form-field-panel" value={currentMode} onChange={(event) => set(group.modeField!, Number(event.target.value))}>
                      {!choices.some((choice) => choice.value === currentMode) && <option value={currentMode}>Unavailable mode {currentMode}</option>}
                      {choices.map((choice) => <option key={choice.value} value={choice.value} title={choice.description}>{choice.label}</option>)}
                    </SelectField>
                  </FormRow>
                )}
              </FormGrid>
            )}
          </div>
        )
      })}
    </Card>
  )
}

function DeviceGuide({ type }: { type?: DeviceType }) {
  const guide = type ? GUIDES[type.id] : undefined
  return (
    <CardStack>
      <Card>
        {guide ? <Markdown source={guide} /> : <p className="devices-muted">No user guide for this device yet (app/docs/devices/{type?.id ?? '…'}.md).</p>}
      </Card>
      {type && (
        <Card title="Commands">
          <ul className="devices-guide-contracts">
            {type.contracts.map((contract) => <li key={contract.id}><strong>{contract.label}</strong>{contract.description ? ` — ${contract.description}` : ''}{contract.returns ? <em> Returns {contract.returns.split(' @')[0]}.</em> : null}</li>)}
          </ul>
          {type.datasheet && <p><a href={type.datasheet} target="_blank" rel="noreferrer">Datasheet (PDF)</a></p>}
        </Card>
      )}
    </CardStack>
  )
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

function ActionSummary({ workspace: w, actionId }: { workspace: DevicesWorkspace; actionId: string }) {
  const action = w.actions.find((entry) => entry.id === actionId)
  if (!action) return <div className="object-editor devices-editor"><p className="devices-muted">This action is gone.</p></div>
  const built = buildAction(w.catalog, w.devices, action)
  const build = { ...built, diagnostics: [...built.diagnostics, ...w.diagnostics.filter((entry) => entry.subjectId === action.id)] }
  return (
    <div className="object-editor devices-editor">
      <div className="devices-page-header">
        <span className="device-tile is-large"><ListPlus aria-hidden="true" /></span>
        <div className="devices-page-title">
          <span className="object-editor-kicker">Action · ID {action.actionId} · {build.bytes} / {build.limit} B</span>
          <h1>{action.name}</h1>
        </div>
        <div className="devices-page-actions">
          <Button variant="primary" onClick={() => w.compose(action.id)}><ListPlus aria-hidden="true" /><span>Edit steps</span></Button>
        </div>
      </div>
      <Card>
        <ol className="devices-step-summary">{action.steps.map((step) => <li key={step.id}>{describeStep(w.catalog, w.devices, step)}</li>)}</ol>
        {!action.steps.length && <p className="devices-muted">No steps yet.</p>}
        {build.diagnostics.map((entry, index) => <p key={index} className={`program-diag ${entry.severity === 'error' ? 'is-error' : 'is-warning'}`}>{entry.severity === 'error' ? <AlertCircle aria-hidden="true" /> : <AlertTriangle aria-hidden="true" />}{entry.message}</p>)}
      </Card>
    </div>
  )
}

function StepEditor({ workspace: w, action, step, index }: { workspace: DevicesWorkspace; action: ProjectAction; step: ActionStep; index: number }) {
  const devices = allDevices(w.catalog, w.devices)
  const device = resolveDevice(w.catalog, w.devices, step.device)
  const contracts = contractsOf(w.catalog, device)
  const contract = findContract(w.catalog, device, step.contract)
  const replace = (next: Partial<ActionStep>) => w.updateAction(action.id, { steps: action.steps.map((entry) => (entry.id === step.id ? { ...entry, ...next } : entry)) })
  const move = (offset: -1 | 1) => {
    const steps = [...action.steps]
    const [moved] = steps.splice(index, 1)
    steps.splice(index + offset, 0, moved!)
    w.updateAction(action.id, { steps })
  }
  const chooseDevice = (ref: string) => {
    const next = resolveDevice(w.catalog, w.devices, ref)
    const same = findContract(w.catalog, next, step.contract)
    const fallback = same ?? contractsOf(w.catalog, next)[0]
    replace({ device: ref, contract: fallback?.id ?? step.contract, values: same ? step.values : fallback ? initialValues(fallback) : {} })
  }
  const chooseContract = (id: string) => {
    const next = findContract(w.catalog, device, id)
    replace({ contract: id, values: next ? initialValues(next) : {} })
  }

  return (
    <li className="devices-step">
      <div className="devices-step-head">
        <span className="devices-step-index">{index + 1}</span>
        <SelectField value={step.device} onChange={(event) => chooseDevice(event.target.value)} aria-label="Device">
          {!device && <option value={step.device}>{step.device} (gone)</option>}
          {devices.map((entry) => <option key={entry.ref} value={entry.ref}>{entry.name} (#{entry.deviceId})</option>)}
        </SelectField>
        <SelectField value={step.contract} onChange={(event) => chooseContract(event.target.value)} aria-label="Contract">
          {!contract && <option value={step.contract}>{step.contract}</option>}
          {contracts.map((entry) => <option key={entry.id} value={entry.id}>{entry.kind === 'device' ? `Device: ${entry.label}` : entry.label}</option>)}
        </SelectField>
        <div className="devices-step-buttons">
          <button type="button" disabled={index === 0} onClick={() => move(-1)} title="Move up" aria-label="Move step up"><ArrowUp aria-hidden="true" /></button>
          <button type="button" disabled={index === action.steps.length - 1} onClick={() => move(1)} title="Move down" aria-label="Move step down"><ArrowDown aria-hidden="true" /></button>
          <button type="button" onClick={() => w.updateAction(action.id, { steps: action.steps.filter((entry) => entry.id !== step.id) })} title="Remove step" aria-label="Remove step"><Trash2 aria-hidden="true" /></button>
        </div>
      </div>
      {contract && device && <ContractFields contract={contract} values={step.values} actions={w.actions} onChange={(values) => replace({ values })} />}
    </li>
  )
}

/** The composer: over the dimmed app, a list of contract calls recorded as one action. */
function ActionComposer({ workspace: w, action, session }: { workspace: DevicesWorkspace; action: ProjectAction; session?: RunitBleSession }) {
  const built = buildAction(w.catalog, w.devices, action)
  const build = { ...built, diagnostics: [...built.diagnostics, ...w.diagnostics.filter((entry) => entry.subjectId === action.id)] }
  const [status, setStatus] = useState<{ ok: boolean; text: string }>()
  const [busy, setBusy] = useState(false)
  const onBoard = async (label: string, steps: Parameters<typeof sendSteps>[1]) => {
    if (!session) return
    setBusy(true)
    try {
      await sendSteps(session, steps)
      setStatus({ ok: true, text: label })
    } catch (error) {
      setStatus({ ok: false, text: error instanceof Error ? error.message : String(error) })
    } finally {
      setBusy(false)
    }
  }
  const record = () => {
    if (!window.confirm('Record this action on the board? The board runs every step while recording it (the devices move now).')) return
    void onBoard(`Recorded as action ${action.actionId} (${build.bytes} B).`, actionRecordSteps(runitCommandCatalog(), action, build))
  }
  const anchor = w.selection?.kind === 'device' ? w.selection.ref : action.steps.at(-1)?.device ?? 'board:0'
  const addStep = () => {
    const device = resolveDevice(w.catalog, w.devices, anchor)
    const contract = contractsOf(w.catalog, device)[0]
    if (contract) w.addStep(action.id, { device: anchor, contract: contract.id, values: initialValues(contract) })
  }
  const percent = Math.min(100, Math.round((100 * build.bytes) / build.limit))

  return (
    <>
      <div className="devices-composer-backdrop" onClick={w.closeComposer} />
      <section className="devices-composer" role="dialog" aria-label="Action composer" aria-modal="true">
        <header className="devices-composer-header">
          <ListPlus aria-hidden="true" />
          <SelectField value={action.id} onChange={(event) => w.compose(event.target.value)} aria-label="Action">
            {w.actions.map((entry) => <option key={entry.id} value={entry.id}>{entry.name} (ID {entry.actionId})</option>)}
          </SelectField>
          <Button onClick={() => w.compose()} title="New action"><Plus aria-hidden="true" />New</Button>
          <Button size="icon" onClick={w.closeComposer} aria-label="Close composer"><X aria-hidden="true" /></Button>
        </header>

        <div className="devices-composer-fields">
          <label>Name<TextField value={action.name} onChange={(event) => w.updateAction(action.id, { name: event.target.value })} /></label>
          <label>Action ID<TextField type="number" min={1} max={255} value={action.actionId} onChange={(event) => w.updateAction(action.id, { actionId: Math.max(1, Math.min(255, Math.floor(Number(event.target.value) || 1))) })} /></label>
          <div className="devices-meter" title={`${build.bytes} of ${build.limit} bytes the board keeps per action`}>
            <span>Size {build.bytes} / {build.limit} B</span>
            <div><i style={{ width: `${percent}%` }} className={build.bytes > build.limit ? 'is-over' : ''} /></div>
          </div>
        </div>
        {w.error && <p className="program-diag is-error"><AlertCircle aria-hidden="true" />{w.error}</p>}

        <ol className="devices-steps">
          {action.steps.map((step, index) => <StepEditor key={step.id} workspace={w} action={action} step={step} index={index} />)}
        </ol>
        {!action.steps.length && <p className="devices-muted">No steps yet. Add one here, or use "Add to action" on a command in the right panel.</p>}
        <Button variant="dashed" className="devices-add-step" onClick={addStep}><Plus aria-hidden="true" />Add step</Button>

        {build.diagnostics.map((entry, index) => <p key={index} className={`program-diag ${entry.severity === 'error' ? 'is-error' : 'is-warning'}`}>{entry.severity === 'error' ? <AlertCircle aria-hidden="true" /> : <AlertTriangle aria-hidden="true" />}{entry.message}</p>)}

        {status && <p className={`program-diag ${status.ok ? '' : 'is-error'}`}>{status.ok ? null : <AlertCircle aria-hidden="true" />}{status.text}</p>}
        <footer className="devices-composer-footer">
          <Button variant="danger" onClick={() => w.removeAction(action.id)}><Trash2 aria-hidden="true" />Delete</Button>
          <span className="devices-muted">{session ? 'Saved in the project.' : 'Saved in the project. Connect a board to record or run it.'}</span>
          <Button disabled={!session || busy || !build.ok || !action.steps.length} onClick={record} title="Store the steps on the board as this dynamic action (runs them once)"><Upload aria-hidden="true" />Record on board</Button>
          <Button disabled={!session || busy} onClick={() => void onBoard(`Action ${action.actionId} ran.`, [actionRunStep(runitCommandCatalog(), action.actionId)])} title="Run the action stored on the board under this ID"><Play aria-hidden="true" />Run</Button>
          <Button variant="primary" onClick={w.closeComposer}>Done</Button>
        </footer>
      </section>
    </>
  )
}
