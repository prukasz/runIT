import { useEffect, useState } from 'react'
import { BookOpen, ListChecks, Lock, Play, Plus, RotateCcw } from 'lucide-react'
import { bundledEnums, deviceCall, onboardActions, sendInOrder } from '@backend'
import type { Request } from '@backend'
import { Badge, Button, Card, CardStack, ChoiceField, DeviceTile, EditableField, FieldError, FormGrid, FormRow, PanelHeader, RefPill, TreeSlab, deviceIcon } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import { useErrors } from '../console/useConsole'
import type { Choice } from '@ui/components'
import type { CatalogEntry, FieldInput, FieldValues } from '@backend'
import type { ProjectAction, Step } from '../project/projectFile'
import { DevicePage } from './DevicePage'
import { FieldsForm } from './FieldsForm'
import { LiveBadge } from './DevicesExplorer'
import { StepList } from './StepList'
import { isCanvasStep } from '../hardware/apply'
import { admittedSeverities, baseConfig, DEVICE_COMMANDS, devicePins, isBusField, isLiveSettingsStep, isPinStart, liveSettingsArgs, liveSettingsOf, PIN_START_COMMANDS, pinHint, pinNotes, pinNumber, SINGLE_TYPES, startSettings, usedBy } from './devices'
import type { DeviceInfo } from './devices'
import type { DevicesState } from './useDevices'
import './Devices.css'


/*
 * A device's settings: its config (what CREATE takes). The fields its live settings command writes (`liveSettingsOf`: the DRV8962's
 * topology, current limit, braking) change on the board at once while it is there; on a board device they are kept as that write in
 * its start commands (its config is the firmware's), on a project device in its config too. The board's answer to the read shows
 * while connected.
 */
function SettingsCard({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  const type = device.type
  const context = { devices: state.devices, own: device, board: state.board }
  const live = liveSettingsOf(type)
  const liveIds = new Set((live?.write?.args?.fields ?? []).map((f) => f.id))
  const present = state.connected && state.live.get(device.id)?.state === 'present'
  const [read, setRead] = useState<FieldValues | undefined>()
  const [error, setError] = useState<string | undefined>()
  useEffect(() => {
    setRead(undefined)
    setError(undefined)
    if (!live || !type || !present) return
    let stopped = false
    state.services.device(device.id, type.id, live.id, undefined, { side: 'read' })
      .then(({ value }) => { if (!stopped && value !== null && typeof value === 'object' && !Array.isArray(value)) setRead(value as FieldValues) })
      .catch(() => undefined)
    return () => { stopped = true }
  }, [live, type, present, device.id, state.services])
  const sendLive = async (args: FieldValues) => {
    if (!live || !type || !present) return
    setError(undefined)
    try {
      await state.services.device(device.id, type.id, live.id, args, { side: 'write' })
      setRead(args)
    } catch (failure) {
      setError(state.explain(failure))
    }
  }
  if (!type?.config || type.config.fields.length === 0) return <Card title="Settings" subhead="This type has nothing to set when it is created." />
  const liveNote = live && <p className="devices-hint">{present ? 'Changed on the board at once.' : 'Sent to the board at once while it is connected.'}{device.system ? ' Kept in the start commands.' : ''}</p>
  if (device.system) {
    // The bus and the address the board's firmware gave the device (board.json `i2c`) are its values, as the fields show them.
    const boardValues: FieldValues = Object.fromEntries(type.config.fields.flatMap((f): [string, FieldInput][] => {
      const pins = device.uses.filter((u) => u.field === f.id).map((u) => u.pin)
      const fromI2c = f.id === 'i2c_bus' ? device.i2c?.bus : f.id === 'i2c_addr' ? device.i2c?.address : undefined
      const value = fromI2c ?? device.config[f.id] ?? (f.pinRef ? ((f.size ?? 3) / 3 === 1 ? pins[0] : pins) : undefined)
      return value === undefined || value === null || (Array.isArray(value) && value.length === 0) ? [] : [[f.id, value as FieldInput]]
    }))
    const values: FieldValues = { ...boardValues, ...(live ? startSettings(device, live) : {}), ...read }
    const change = (next: FieldValues) => {
      if (!live) return
      const args = liveSettingsArgs(live, { ...values, ...next })
      state.setDefaults(device, (steps) => [...steps.filter((s) => !isLiveSettingsStep(s, device, live)), { kind: 'device', device: device.id, contract: type.id, command: live.id, args }])
      void sendLive(args)
    }
    return (
      <Card title="Settings">
        {live && <FieldsForm layout={type.config} values={values} context={context} hide={(f) => !liveIds.has(f.id)} onChange={change} />}
        {liveNote}
        {/* The rest only as board.json gives it, locked: the type's default need not be what the board's firmware sets. */}
        <FieldsForm layout={type.config} values={boardValues} context={context} disabled hide={(f) => liveIds.has(f.id) || boardValues[f.id] === undefined} onChange={() => undefined} />
        {error && <FieldError>{error}</FieldError>}
      </Card>
    )
  }
  const presets = (type.presets ?? []).map((p): Choice<string> => ({ value: p.id, label: p.label, ...(p.description ? { description: p.description } : {}) }))
  return (
    <Card title="Settings" actions={<Button size="sm" onClick={() => state.updateDevice(device.id, (d) => ({ ...d, config: baseConfig(type, state.board) }))}><RotateCcw aria-hidden="true" />Defaults</Button>}>
      {presets.length > 0 && (
        <div className="devices-actions">
          <ChoiceField<string> aria-label="Preset" value={undefined} choices={presets} placeholder="Apply a preset…"
            onChange={(id) => { const preset = type.presets?.find((p) => p.id === id); if (preset) state.updateDevice(device.id, (d) => ({ ...d, config: { ...d.config, ...preset.values } })) }} />
        </div>
      )}
      <FieldsForm layout={type.config} values={device.config} context={context} hide={isBusField} onChange={(config) => {
        state.updateDevice(device.id, (d) => ({ ...d, config }), `device-config:${device.id}`)
        if (live && [...liveIds].some((id) => config[id] !== device.config[id])) void sendLive(liveSettingsArgs(live, config))
      }} />
      {liveNote}
      {error && <FieldError>{error}</FieldError>}
    </Card>
  )
}

/** The error policy a device starts with: its ERROR_POLICY write in the start commands (`sys_device` starts at LOW, no actions). */
interface Policy { readonly importance: number; readonly actions: readonly number[] }

const isPolicyStep = (step: Step, state: DevicesState): boolean =>
  step.kind === 'device' && step.contract === state.lifecycle.id && step.command === DEVICE_COMMANDS.policy

function policyOf(device: DeviceInfo, state: DevicesState): Policy {
  const step = device.defaults.find((s) => isPolicyStep(s, state))
  const args = step?.kind === 'device' ? (step.args ?? {}) : {}
  const actions = Array.isArray(args.actions) ? (args.actions as readonly number[]) : []
  return { importance: typeof args.importance === 'number' ? args.importance : 1, actions: Array.from({ length: 4 }, (_, i) => actions[i] ?? 0) }
}

/** The policy as the device's start command: the one policy step, replaced by a change. */
const setPolicy = (device: DeviceInfo, state: DevicesState, policy: Policy) =>
  state.setDefaults(device, (steps) => [
    ...steps.filter((s) => !isPolicyStep(s, state)),
    { kind: 'device', device: device.id, contract: state.lifecycle.id, command: DEVICE_COMMANDS.policy, args: { importance: policy.importance, actions: [...policy.actions] } },
  ])

function PolicyCard({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  const policy = policyOf(device, state)
  const enums = bundledEnums()
  const importance = (enums.sys_device_importance_e?.members ?? []).map((m): Choice<number> => ({ value: m.value, label: m.alias ?? m.name, ...(m.description ? { description: m.description } : {}) }))
  // The four levels a policy answers: LOW to CRITICAL (NONE marks a context node, not a failure).
  const severities = (enums.err_sev_e?.members ?? []).filter((m) => /^ERR_SEV_(LOW|MEDIUM|HIGH|CRITICAL)$/.test(m.name))
  const admitted = admittedSeverities(policy.importance, severities)
  // What a severity can run: the policy's own responses on this device, the firmware's own actions (suspend all, reset all, ...) and
  // the project's, each under its origin.
  const actions: Choice<number>[] = [
    ...(enums.device_response?.members ?? []).map((m): Choice<number> => ({ value: m.value, label: m.alias ?? m.name, group: 'This device', ...(m.description ? { description: m.description } : {}) })),
    ...onboardActions().map((a): Choice<number> => ({ value: a.id, label: a.alias.replace(/^Onboard action: /, '').replace(/^./, (c) => c.toUpperCase()), group: 'Board' })),
    ...state.project.actions.map((a): Choice<number> => ({ value: a.id, label: a.name, group: 'Project' })),
  ]
  const set = (next: Policy) => setPolicy(device, state, next)
  return (
    <Card title="Error policy">
      <FormRow label="Importance" note="Which errors of this device get a response">
        <ChoiceField<number> aria-label="Importance" value={policy.importance} choices={importance} onChange={(value) => value !== undefined && set({ ...policy, importance: value })} />
      </FormRow>
      <FormGrid>
        {admitted.map((sev) => (
          <FormRow key={sev.value} label={`${sev.alias ?? sev.name} errors run`} note={sev.description}>
            <ChoiceField<number> aria-label={`${sev.alias ?? sev.name} errors run`} value={policy.actions[sev.value - 1] || undefined} choices={actions} emptyLabel="None" placeholder="None"
              onChange={(id) => set({ ...policy, actions: policy.actions.map((a, i) => (i === sev.value - 1 ? (id ?? 0) : a)) })} />
          </FormRow>
        ))}
      </FormGrid>
      {state.project.actions.length === 0 && <p className="devices-hint">Add an action of your own in the Actions tab; the board's own are listed above.</p>}
    </Card>
  )
}

function StartCard({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  // A pin's mode and interrupt are set in the pin's panel: the list names the pin, a click opens it.
  const pinSteps = device.defaults.filter((s) => isPinStart(s, device.id))
  const kept = (s: Step) => !isPolicyStep(s, state) && !isPinStart(s, device.id)
  return (
    <Card title="At start">
      {pinSteps.map((s, i) => {
        const pin = Number(s.kind === 'device' ? s.args?.pin : 0)
        return (
          <div key={i} className="devices-pin-start">
            <TreeSlab icon={<DeviceTile size="small" tags={device.type?.tags} />} label={`${state.pinName(device.id, pin)} · ${s.kind === 'device' && s.command === PIN_START_COMMANDS.interrupt ? 'Interrupt' : 'Mode'}`}
              onClick={() => state.selectPin(device.id, pin)} />
          </div>
        )
      })}
      <StepList steps={device.defaults.filter(kept)} state={state} showDevice={false}
        locked={(s) => (isCanvasStep(state.project, device.id, s) ? 'Set by the Devices canvas for what is wired to it: change it there' : undefined)}
        onChange={(steps) => state.setDefaults(device, (all) => [...all.filter((s) => !kept(s)), ...steps])} empty="Add in side panel." />
    </Card>
  )
}

function PinsCard({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  const pins = devicePins(device)
  if (!pins) return null
  const notes = pinNotes(state.board, device.id)
  const users = usedBy(state.devices, device.id)
  const errors = useErrors()
  return (
    <Card title="Pins">
      <div className="devices-pins">
        {Array.from({ length: pins.count }, (_, pin) => {
          const user = users.filter((u) => u.use.pin.pin === pin)
          const note = notes.get(pin)
          const kept = state.board.reservedPins.find((r) => r.device === device.id && r.pin === pin)
          return (
            // A click on the row opens the pin's settings (mode, interrupt) in the right panel; the references and buttons in it keep their own action.
            <div key={pin} className={`devices-pin ${errors && errors.console.unread(`device:${device.id}/pin:${pin}`) > 0 ? 'has-error' : ''}`.trim()} data-key={`device:${device.id}/pin:${pin}`}
              onClick={(event) => { if (!(event.target as HTMLElement).closest('button, .ref-pill')) state.selectPin(device.id, pin) }}>
              <span className="devices-pin-name" title={pinHint(device, pin)}>{pinNumber(pin)}</span>
              {/* A pin another device uses is named by that use: the reference stands in place of the user's name. */}
              {user.length > 0 || kept
                ? (
                  <span className="devices-pin-uses">
                    {user.map((u) => (
                      <RefPill key={`${u.by.id}:${u.use.use}`} compact name={u.by.name} detail={u.use.use} icon={deviceIcon(u.by.type?.tags)} onJump={() => state.select({ kind: 'device', id: u.by.id })} />
                    ))}
                    {kept && user.length === 0 && <RefPill compact name={kept.label} detail="Board" icon={Lock} title={kept.reason} />}
                  </span>
                )
                : <EditableField className="devices-pin-alias" value={device.pinAliases.get(pin) ?? ''} placeholder="Name…" maxLength={32} aria-label={`Name of pin ${pinNumber(pin)}`}
                    onChange={(name) => state.setPinAlias(device.id, pin, name)} />}
              <span className="devices-pin-note">{note && user.length === 0 && !kept && <Badge tone="info" size="compact">{note}</Badge>}</span>
            </div>
          )
        })}
      </div>
    </Card>
  )
}

function LinksCard({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  const inActions = state.project.actions.filter((a) => a.steps.some((s) => s.kind === 'device' && s.device === device.id))
  return (
    <Card title="Links">
      <FormRow label="Uses">
        <div className="devices-refs">
          {device.uses.length === 0 && <span className="devices-hint">No pins of other devices.</span>}
          {device.uses.map((u) => {
            const target = state.devices.find((d) => d.id === u.pin.device)
            return <RefPill key={u.use} name={target?.name ?? `Device ${u.pin.device}`} broken={!target} detail={`${state.pinName(u.pin.device, u.pin.pin)} · ${u.use}`} icon={deviceIcon(target?.type?.tags)}
              refKey={`device:${u.pin.device}/pin:${u.pin.pin}`} title={u.description} onJump={target ? () => state.select({ kind: 'device', id: target.id }) : undefined} />
          })}
        </div>
      </FormRow>
      <FormRow label="In actions">
        <div className="devices-refs">
          {inActions.length === 0 && <span className="devices-hint">No action sends it a command.</span>}
          {inActions.map((a) => <RefPill key={a.id} name={a.name} icon={ListChecks} onJump={() => state.select({ kind: 'action', id: a.id })} />)}
        </div>
      </FormRow>
    </Card>
  )
}

function DeviceScreen({ device, state, onPage }: { device: DeviceInfo; state: DevicesState; onPage: () => void }) {
  return (
    <>
      <PanelHeader icon={<DeviceTile tags={device.type?.tags} />}
        title={device.system
          ? device.name
          : <EditableField className="devices-title" value={device.name} maxLength={48} aria-label="Device name" onChange={(name) => state.updateDevice(device.id, (d) => ({ ...d, name }), `device-name:${device.id}`)} />}>
        <Badge tone="system" title={device.type?.label}>{device.type?.label ?? 'No descriptor'}</Badge>
        <LiveBadge device={device} live={state.live.get(device.id)} connected={state.connected} />
      </PanelHeader>
      {device.description && <p className="devices-hint">{device.description}</p>}
      <div className="devices-actions"><Button size="sm" onClick={onPage}><BookOpen aria-hidden="true" />Device page</Button></div>
      <CardStack>
        <ErrorsCard about={`device:${device.id}`} />
        <SettingsCard device={device} state={state} />
        <PolicyCard device={device} state={state} />
        <PinsCard device={device} state={state} />
        <StartCard device={device} state={state} />
        <LinksCard device={device} state={state} />
      </CardStack>
    </>
  )
}

/** The request of a step (a write), to send it live. */
const stepRequest = (state: DevicesState, step: Step): Request =>
  step.kind === 'frame' ? { keys: [step.bytes[0]!], payload: Uint8Array.from(step.bytes.slice(1)) } : deviceCall(state.services.link.defs, state.catalog, step.device, step.contract, step.command, step.args, 'write').request

function ActionScreen({ action, state }: { action: ProjectAction; state: DevicesState }) {
  const [result, setResult] = useState<{ tone: 'info' | 'error'; text: string } | undefined>()
  const runNow = async () => {
    setResult(undefined)
    try {
      await sendInOrder(state.services.link, action.steps.map((s) => stepRequest(state, s)))
      setResult({ tone: 'info', text: `Sent ${action.steps.length} command(s).` })
    } catch (failure) {
      setResult({ tone: 'error', text: state.explain(failure) })
    }
  }
  return (
    <>
      <PanelHeader
        title={<EditableField className="devices-title" value={action.name} maxLength={48} aria-label="Action name" onChange={(name) => state.updateAction(action.id, (a) => ({ ...a, name }), `action-name:${action.id}`)} />} />
      <CardStack>
        <Card title="Commands">
          <StepList steps={action.steps} state={state} onChange={(steps) => state.updateAction(action.id, (a) => ({ ...a, steps }))} empty="None" />
          <div className="devices-actions">
            <Button variant="primary" disabled={!state.connected || action.steps.length === 0} title={state.connected ? 'Send these commands to the board now, in order' : 'Connect the board first'} onClick={() => void runNow()}>
              <Play aria-hidden="true" />Run now
            </Button>
          </div>
          {result && (result.tone === 'error' ? <FieldError>{result.text}</FieldError> : <p className="devices-hint">{result.text}</p>)}
        </Card>
      </CardStack>
    </>
  )
}

/** Every type that can be installed, as a tile, by category; a tile adds the device and opens it. */
function AddScreen({ state }: { state: DevicesState }) {
  const groups = new Map<string, CatalogEntry[]>()
  for (const t of [...state.catalog.devices.values()].filter((t) => !SINGLE_TYPES.has(t.id)).sort((a, b) => a.label.localeCompare(b.label))) {
    const group = t.category ?? (t.tags[0] ?? 'Other')
    groups.set(group, [...(groups.get(group) ?? []), t])
  }
  return (
    <>
      <PanelHeader icon={<DeviceTile icon={Plus} />} title="Add a device" />
      <CardStack>
        {[...groups].sort(([a], [b]) => a.localeCompare(b)).map(([group, types]) => (
          <Card key={group} title={group.toUpperCase()}>
            <div className="devices-types">
              {types.map((t) => (
                <Button key={t.id} className="devices-type" title={t.description} data-key={`type:${t.id}`} onClick={() => state.addDevice(t)}>
                  <DeviceTile tags={t.tags} />
                  <span className="devices-type-label">{t.label}</span>
                  {t.description && <span className="devices-type-note">{t.description}</span>}
                </Button>
              ))}
            </div>
          </Card>
        ))}
      </CardStack>
    </>
  )
}

/** The Devices view's details screen: the selected device or action. */
export function DevicesScreen({ state }: { state: DevicesState }) {
  // The device whose page (manual, code) is open in place of its settings.
  const [pageOf, setPageOf] = useState<number | undefined>()
  const selection = state.selection
  const device = selection?.kind === 'device' ? state.devices.find((d) => d.id === selection.id) : undefined
  const action = selection?.kind === 'action' ? state.project.actions.find((a) => a.id === selection.id) : undefined
  return (
    // A click in the centre outside a pin (its row, or its At start line) leaves the pin: the right panel is the device's again.
    <div className="devices-screen" onClick={(event) => { if (state.pinSelection && !(event.target as HTMLElement).closest('.devices-pin, .devices-pin-start')) state.clearPin() }}>
      {selection?.kind === 'add'
        ? <AddScreen state={state} />
        : device && pageOf === device.id
        ? <DevicePage device={device} onBack={() => setPageOf(undefined)} />
        : device
        ? <DeviceScreen device={device} state={state} onPage={() => setPageOf(device.id)} />
        : action
          ? <ActionScreen action={action} state={state} />
          : (
            <div className="view-placeholder">
              <h2>Devices</h2>
              <p>Choose a device or an action on the left, or add one. The toolbar writes the devices, their start commands and the actions to the board, and reads them back.</p>
            </div>
          )}
    </div>
  )
}
