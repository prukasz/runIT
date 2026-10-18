import { useState } from 'react'
import { ChevronDown, ChevronRight, ListPlus, Pause, Play, PlugZap, RotateCcw, Send, Unplug } from 'lucide-react'
import type { CatalogEntry, CommandDef, FieldValues } from '@backend'
import { Button, Card, CardStack, ChoiceField, FieldError, FormGrid, FormRow, NumberField, PanelHeader, Switch, TreeChildren, TreeDisclosure, TreeSlab } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import type { Choice } from '@ui/components'
import type { Step } from '../project/projectFile'
import { FieldsForm } from './FieldsForm'
import { LiveBadge } from './DevicesExplorer'
import { bundledEnums, deviceOp } from '@backend'
import { changesPin, commandGroups, DEVICE_COMMANDS, GPIO_MODE_MEMBER, hasGpioModes, kindOfModeValue, PIN_MODE_MEMBER, PIN_START_COMMANDS, pinNumber, pinReady, pinStartOf, pinStateOf, showAnswer, usedBy, withPinStart } from './devices'
import type { DeviceInfo, PinInterrupt, PinStart } from './devices'
import type { DevicesState } from './useDevices'

type Result = { readonly tone: 'info' | 'error'; readonly text: string }

/** One command: its arguments, Send (live), Add to start, Add to action; what the board answered. */
function CommandTester({ device, entry, command, state }: { device: DeviceInfo; entry: CatalogEntry; command: CommandDef; state: DevicesState }) {
  const [open, setOpen] = useState(false)
  const [args, setArgs] = useState<FieldValues>({})
  const [result, setResult] = useState<Result | undefined>()
  const [sending, setSending] = useState(false)
  const step: Step = { kind: 'device', device: device.id, contract: entry.id, command: command.id, ...(command.args ? { args } : {}) }
  // The pin states: now (what this session did live) and at start (what the project and the boot do): a command takes only a pin in the
  // state its descriptor wants (`pin_state`), so none is refused. A device without SET_MODE has its pins fixed.
  const io = state.catalog.contracts.get('IO')
  const setModeDef = io?.commands.get('SET_MODE')
  const hasSetMode = !setModeDef || !state.ops || state.ops.has(deviceOp(state.catalog, io!, setModeDef))
  const pinField = command.args?.fields.find((f) => f.type === 'pin' && f.pinState)
  const chosen = pinField ? args[pinField.id] : undefined
  const stateNow = (pin: number) => pinStateOf(device, state.board, pin, hasSetMode, state.pinKinds)
  const stateAtStart = (pin: number) => pinStateOf(device, state.board, pin, hasSetMode)
  const readyNow = !pinField || typeof chosen !== 'number' || pinReady(stateNow(chosen), pinField.pinState)
  const readyAtStart = !pinField || typeof chosen !== 'number' || pinReady(stateAtStart(chosen), pinField.pinState)
  const notAtStart = "The pin has no such mode when the project starts: add its Set mode to the start commands first (the pin's panel)."
  const send = async () => {
    setSending(true)
    setResult(undefined)
    try {
      const { value } = await state.services.device(device.id, entry.id, command.id, command.args ? args : undefined)
      // What the board did to the pin is what the next command's pin list is made from.
      if (entry.id === 'IO' && typeof args.pin === 'number') {
        if (command.id === 'SET_MODE') state.setPinKind(device.id, args.pin, kindOfModeValue(args.mode) ?? null)
        else if (command.id === 'RESET') state.setPinKind(device.id, args.pin, null)
      }
      setResult({ tone: 'info', text: showAnswer(command, value, state.nameOf, state.pinName, device.id) })
      if (entry === state.lifecycle) void state.probe()
    } catch (failure) {
      setResult({ tone: 'error', text: state.explain(failure) })
    } finally {
      setSending(false)
    }
  }
  const actions: Choice<number>[] = [...state.project.actions.map((a) => ({ value: a.id, label: a.name })), { value: -1, label: 'New action' }]
  const addToAction = (id: number | undefined) => {
    if (id === undefined) return
    if (id < 0) state.select({ kind: 'action', id: state.addAction([step]) })
    else {
      state.updateAction(id, (a) => ({ ...a, steps: [...a.steps, step] }))
      setResult({ tone: 'info', text: `Added to ${state.project.actions.find((a) => a.id === id)?.name}.` })
    }
  }
  return (
    <>
      <TreeSlab label={command.label} title={command.description ?? command.label} disclosure={<TreeDisclosure open={open} onToggle={() => setOpen(!open)} />} onClick={() => setOpen(!open)} />
      <TreeChildren open={open}>
        <div className="devices-command">
          {command.description && <p className="devices-hint">{command.description}</p>}
          {command.args && <FieldsForm grid={false} layout={command.args} values={args} onChange={setArgs} context={{ devices: state.devices, own: device, board: state.board, changesPin: changesPin(command), pinStates: stateNow }} />}
          <div className="devices-actions">
            <Button size="sm" variant="primary" disabled={!state.connected || sending || !readyNow} title={!state.connected ? 'Connect the board first' : !readyNow ? 'The pin is not in a state this command needs: choose another pin' : 'Send it to the board now'} onClick={() => void send()}>
              <Send aria-hidden="true" />Send
            </Button>
            <Button size="sm" disabled={!readyAtStart} title={readyAtStart ? 'Send it every time the stored project starts' : notAtStart} onClick={() => { state.setDefaults(device, (steps) => [...steps, step]); setResult({ tone: 'info', text: 'Added to the start commands.' }) }}>
              <ListPlus aria-hidden="true" />At start
            </Button>
            <ChoiceField<number> aria-label="Add to action" disabled={!readyAtStart} title={readyAtStart ? undefined : notAtStart} value={undefined} choices={actions} placeholder="Add to action…" onChange={addToAction} />
          </div>
          {result && (result.tone === 'error' ? <FieldError>{result.text}</FieldError> : <p className="devices-answer">{result.text}</p>)}
        </div>
      </TreeChildren>
    </>
  )
}

/** Install and Remove (a project device) and Reset, Suspend, Resume (any device), as icons, live. */
function LifecycleBar({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  const [result, setResult] = useState<Result | undefined>()
  const own = state.project.devices.find((d) => d.id === device.id)
  const known = state.live.get(device.id)
  const present = known?.state === 'present'
  const suspended = present && known.suspended
  const send = (command: string) => state.services.device(device.id, state.lifecycle.id, command)
  const go = async (work: () => Promise<unknown>, done: string) => {
    setResult(undefined)
    try {
      await work()
      setResult({ tone: 'info', text: done })
    } catch (failure) {
      setResult({ tone: 'error', text: state.explain(failure) })
    }
    void state.probe()
  }
  return (
    <div className="devices-command">
      <div className="devices-actions">
        {own && (
          <>
            <Button size="icon-sm" variant="primary" aria-label="Install" disabled={!state.connected || present} title={present ? 'It is on the board: remove it first to install it again' : 'Install: create it on the board now, with its settings'}
              onClick={() => void go(() => state.services.create(own.id, own.type, own.config), `${own.name} installed.`)}>
              <PlugZap aria-hidden="true" />
            </Button>
            <Button size="icon-sm" variant="danger" aria-label="Remove" disabled={!state.connected || !present} title="Remove it from the board (refused while another device uses it)"
              onClick={() => void go(() => send(DEVICE_COMMANDS.remove), `${own.name} removed.`)}>
              <Unplug aria-hidden="true" />
            </Button>
          </>
        )}
        <Button size="icon-sm" aria-label="Reset" disabled={!state.connected || !present} title="Reset: outputs off, settings back to defaults"
          onClick={() => { state.resetPinKinds(device.id); void go(() => send(DEVICE_COMMANDS.reset), `${device.name} reset.`) }}>
          <RotateCcw aria-hidden="true" />
        </Button>
        <Button size="icon-sm" aria-label="Suspend" disabled={!state.connected || !present || suspended} title="Suspend: outputs safe, it refuses writes until resumed"
          onClick={() => void go(() => send(DEVICE_COMMANDS.suspend), `${device.name} suspended.`)}>
          <Pause aria-hidden="true" />
        </Button>
        <Button size="icon-sm" aria-label="Resume" disabled={!state.connected || !suspended} title="Resume: let it work again"
          onClick={() => void go(() => send(DEVICE_COMMANDS.resume), `${device.name} resumed.`)}>
          <Play aria-hidden="true" />
        </Button>
      </div>
      {result && (result.tone === 'error' ? <FieldError>{result.text}</FieldError> : <p className="devices-answer">{result.text}</p>)}
    </div>
  )
}

/** The right panel of the Devices view: every operation of the selected device, tried on the board live. */
export function DeviceTestPanel({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  // Only what the device answers (its OPS) once the board said; the descriptor's list before that.
  const groups = commandGroups(state.catalog, device.type, { ops: state.ops })
  return (
    <div className="devices-test">
      <PanelHeader title={`Test ${device.name}`}>
        <LiveBadge device={device} live={state.live.get(device.id)} connected={state.connected} />
      </PanelHeader>
      {!state.connected && <p className="devices-hint">Connect the board to send commands. At start and Add to action work without it.</p>}
      <LifecycleBar device={device} state={state} />
      {groups.map(({ entry, commands }) => (
        <div key={entry.id} className="devices-group">
          <h4>{entry === device.type ? `${entry.label}: own operations` : entry.label}</h4>
          {commands.map((command) => (
            <CommandTester key={command.id} device={device} entry={entry} command={command} state={state} />
          ))}
        </div>
      ))}
    </div>
  )
}

/** The sys_io_intr_mode_e values of an ADC window (outside, inside): an analogue pin's interrupt takes them, with its thresholds. */
const WINDOW_MODES = new Set([4, 5])

/** A pin's settings in the right panel: its mode (GPIO: a choice; analogue: fixed) and its interrupt (edges, or an ADC window). */
export function PinPanel({ device, pin, state }: { device: DeviceInfo; pin: number; state: DevicesState }) {
  const [advanced, setAdvanced] = useState(false)
  const modes = device.type?.pins?.modes ?? []
  const io = device.type?.contracts?.includes('IO') ? state.catalog.contracts.get('IO') : undefined
  // Offered where the device answers the command (before the board answers, its descriptor says so).
  const offers = (command: string): boolean => {
    const def = io?.commands.get(command)
    return def !== undefined && (!state.ops || state.ops.has(deviceOp(state.catalog, io!, def)))
  }
  const members = bundledEnums()
  const kept = state.board.reservedPins.find((r) => r.device === device.id && r.pin === pin)
  const user = usedBy(state.devices, device.id).find((u) => u.use.pin.pin === pin)
  const free = !kept && !user
  const gpio = hasGpioModes(modes)
  const start = pinStartOf(device.defaults, device.id, pin)
  // An interrupt watches a configured input: with no mode set (or a push-pull output) the board refuses it, so none is kept then.
  const pushPullMode = members.sys_io_mode_e?.members.find((m) => m.name === 'SYS_IO_MODE_OUTPUT_PUSH_PULL')?.value
  const needsMode = gpio && (start.mode === undefined || start.mode === pushPullMode)
  const set = (next: PinStart) => {
    const blocked = gpio && (next.mode === undefined || next.mode === pushPullMode)
    const { interrupt: _, ...rest } = next
    state.setDefaults(device, (steps) => withPinStart(steps, device.id, pin, blocked ? rest : next))
  }
  const setInterrupt = (change: Partial<PinInterrupt>) => {
    const current: PinInterrupt = start.interrupt ?? { mode: 0, debounce: false, upMv: 0, downMv: 0, hysteresisMv: 0, count: 0 }
    set({ ...start, interrupt: { ...current, ...change } })
  }
  const modeName = (mode: string) => members.sys_io_mode_e?.members.find((m) => m.name === PIN_MODE_MEMBER[mode])
  const modeChoices: Choice<number>[] = modes.flatMap((m) => {
    const member = modeName(m)
    return member && m in GPIO_MODE_MEMBER ? [{ value: member.value, label: member.alias ?? member.name }] : []
  })
  const intrChoices: Choice<number>[] = (members.sys_io_intr_mode_e?.members ?? [])
    .filter((m) => (gpio ? m.value <= 3 : m.value === 0 || WINDOW_MODES.has(m.value)))
    .map((m) => ({ value: m.value, label: m.alias ?? m.name }))
  const canMode = gpio && offers(PIN_START_COMMANDS.mode)
  // An interrupt watches the pin's input: a push-pull output only sees its own drive, so it is not offered for one.
  const pushPull = members.sys_io_mode_e?.members.find((m) => m.name === 'SYS_IO_MODE_OUTPUT_PUSH_PULL')?.value
  const canInterrupt = offers(PIN_START_COMMANDS.interrupt) && !(gpio && start.mode !== undefined && start.mode === pushPull)
  const windowMode = !gpio && WINDOW_MODES.has(start.interrupt?.mode ?? 0)
  return (
    <div className="devices-test">
      <PanelHeader title={`Pin ${pinNumber(pin)}`}>
        <Button size="sm" onClick={() => state.select({ kind: 'device', id: device.id })}>{device.name}</Button>
      </PanelHeader>
      {!free && (
        <p className="devices-hint">{kept ? `${kept.label}: the board keeps this pin.` : `${user!.by.name} (${user!.use.use}) manages this pin.`}</p>
      )}
      {free && !canMode && !canInterrupt && <p className="devices-hint">Nothing to set on this pin.</p>}
      <ErrorsCard about={`device:${device.id}/pin:${pin}`} />
      {free && (
        <CardStack>
          {canMode && (
            <Card title="Mode">
              <ChoiceField<number> aria-label="Mode" value={start.mode} choices={modeChoices} emptyLabel="Default" placeholder="Default" onChange={(mode) => set({ ...start, mode })} />
            </Card>
          )}
          {canInterrupt && (
            <Card title="Interrupt">
              {needsMode ? <p className="devices-hint">Set the Mode to an input first: the interrupt needs a configured input pin.</p> : (
              <>
              <FormGrid>
                <FormRow className="is-wide" label="Interrupt">
                  <ChoiceField<number> aria-label="Interrupt" value={start.interrupt?.mode ?? 0} choices={intrChoices} onChange={(mode) => setInterrupt({ mode: mode ?? 0 })} />
                </FormRow>
                {gpio && (
                  <FormRow label="Debounce">
                    <Switch label={start.interrupt?.debounce ? 'On' : 'Off'} checked={start.interrupt?.debounce ?? false} onChange={(debounce) => setInterrupt({ debounce })} />
                  </FormRow>
                )}
                {windowMode && (
                  <>
                    <FormRow label="Upper threshold (mV)"><NumberField aria-label="Upper threshold" value={start.interrupt?.upMv ?? 0} min={0} max={65535} integer onCommit={(upMv) => setInterrupt({ upMv })} /></FormRow>
                    <FormRow label="Lower threshold (mV)"><NumberField aria-label="Lower threshold" value={start.interrupt?.downMv ?? 0} min={0} max={65535} integer onCommit={(downMv) => setInterrupt({ downMv })} /></FormRow>
                  </>
                )}
              </FormGrid>
              {windowMode && (
                <>
                  <Button size="sm" aria-expanded={advanced} onClick={() => setAdvanced(!advanced)}>
                    {advanced ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />}Advanced
                  </Button>
                  {advanced && (
                    <FormGrid>
                      <FormRow label="Hysteresis (mV)"><NumberField aria-label="Hysteresis" value={start.interrupt?.hysteresisMv ?? 0} min={0} max={65535} integer onCommit={(hysteresisMv) => setInterrupt({ hysteresisMv })} /></FormRow>
                      <FormRow label="Samples past the threshold"><NumberField aria-label="Event counter threshold" value={start.interrupt?.count ?? 0} min={0} max={65535} integer onCommit={(count) => setInterrupt({ count })} /></FormRow>
                    </FormGrid>
                  )}
                </>
              )}
              </>
              )}
            </Card>
          )}
        </CardStack>
      )}
    </div>
  )
}
