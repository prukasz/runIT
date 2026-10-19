import { bundledEnums, createDeviceCall, deviceCall, deviceOp, isMirror, onboardActionIds, readDeviceFrame, requestFrame, sidesOf } from '@backend'
import type { BoardDescriptor, CatalogEntry, CommandDef, FieldRecord, FieldValues, LinkDefinitions, PinRef, Section, ServiceCatalog, Side, WireField } from '@backend'
import type { BoardDefaults, ProjectAction, ProjectDevice, ProjectFile, Step } from '../project/projectFile'

/*
 * The devices of the Devices view, pure (no link, no UI): the board's own (board.json, created by the firmware at boot) and the
 * project's (created from their config), the pins each uses and who uses its pins, the records that carry them to the board,
 * and those records read back.
 *
 *   device record   0x02000000 | id (SYS_ACTIONS.MD): a project device's CREATE, then its start commands; a board device's
 *                   start commands only (the firmware created it). Each is a section of the stored project, so it runs at
 *                   every start, in the order of the stream: the board's devices first, then the project's in creation order
 *                   (a device may only use devices created before it, SYS_DEVICE.MD).
 *   action          a record of the user class: the user's commands, run on a call (an app, the VM, an error policy). Never a
 *                   section (every section runs at start, SYS_PROJECT.MD).
 */

/** The record class of an ID (`[class u8][index u24]`, SYS_ACTIONS.MD) and the classes by name (`action_class` in enums.json). */
export const recordClass = (id: number): number => id >>> 24
const actionClass = (name: string): number => {
  const member = bundledEnums().action_class?.members.find((m) => m.name === name)
  if (!member) throw new Error(`generated/common/enums.json: group 'action_class' has no ${name}.`)
  return member.value
}
export const USER_CLASS = actionClass('SYS_ACTIONS_CLASS_USER')
export const DEVICE_CLASS = actionClass('SYS_ACTIONS_CLASS_DEVICE')
/** The class of the settings records (log, power, Bluetooth, data streams). */
export const SETTINGS_CLASS = actionClass('SYS_ACTIONS_CLASS_SYSTEM')
/** A device's record ID (`SYS_ACTIONS_ID_DEVICE`); the device ID is a u16. */
export const deviceRecordOf = (device: number): number => ((DEVICE_CLASS << 24) | (device & 0xffff)) >>> 0
export const actionRecordOf = (action: number): number => ((USER_CLASS << 24) | (action & 0xffffff)) >>> 0

/** A pin of another device a device uses, and what for (the config field's label: `Interrupt pin`, `In pin 2`). */
export interface PinUse {
  /** The config field it is (`intr_pin`). */
  readonly field: string
  readonly use: string
  readonly pin: PinRef
  readonly description?: string
}

/** A device as the Devices view shows it. */
export interface DeviceInfo {
  readonly id: number
  readonly name: string
  /** The board's own: created by the firmware at boot, not by the project. */
  readonly system: boolean
  /** Its type in the catalog; undefined when no descriptor describes it. */
  readonly type: CatalogEntry | undefined
  readonly description?: string
  /** A project device's config; the values board.json gives for a board device. */
  readonly config: FieldValues
  /** A board device's place on an I2C bus (board.json). */
  readonly i2c?: { readonly bus: number; readonly address: number }
  readonly uses: readonly PinUse[]
  /** The user's names of its pins, by pin. */
  readonly pinAliases: ReadonlyMap<number, string>
  /** The commands it gets at start, after it is created. */
  readonly defaults: readonly Step[]
}

/** The device type of a board device: the descriptor board.json names. */
const boardType = (catalog: ServiceCatalog, descriptor: string): CatalogEntry | undefined =>
  [...catalog.devices.values()].find((entry) => entry.source.endsWith(`/${descriptor}.json`))

/** What a use is called: the config field's label, and the place in a list of pins (`in_pin[0]` -> `In pin 1`). */
const useLabel = (type: CatalogEntry | undefined, use: string): string => {
  const [, id = use, index] = /^(\w+)(?:\[(\d+)\])?$/.exec(use) ?? []
  // A use the descriptor has no field for is named from its id (`in_pin` -> `In pin`).
  const label = type?.config?.fields.find((f) => f.id === id)?.label ?? `${id.charAt(0).toUpperCase()}${id.slice(1).replace(/_/g, ' ')}`
  return index === undefined ? label : `${label} ${Number(index) + 1}`
}

const isPin = (value: unknown): value is PinRef => typeof value === 'object' && value !== null && 'device' in value && 'pin' in value

/** The pins a config uses: every pin reference field, set or defaulted, unused ones left out. */
export const configPins = (type: CatalogEntry | undefined, config: FieldValues = {}): PinUse[] =>
  (type?.config?.fields ?? []).filter((f) => f.pinRef).flatMap((field) => {
    const value = config[field.id] ?? field.default
    const list = Array.isArray(value) ? value : [value]
    return list.flatMap((pin, i) => (isPin(pin) ? [{ field: field.id, use: list.length > 1 ? `${field.label} ${i + 1}` : field.label, pin, ...(field.description ? { description: field.description } : {}) }] : []))
  })

/** The user's names of a device's pins, by pin. */
const aliasesOf = (project: ProjectFile, device: number): ReadonlyMap<number, string> =>
  new Map(project.pinAliases.filter((a) => a.device === device && a.name.trim()).map((a) => [a.pin, a.name.trim()]))

/** Every device of the view: the board's (in board.json's order), then the project's (in creation order, see `creationOrder`). A pin another device
 * uses has no alias: it is named by that use. */
export const deviceList = (project: ProjectFile, catalog: ServiceCatalog, board: BoardDescriptor): DeviceInfo[] => {
  const devices = listDevices(project, catalog, board)
  return devices.map((d) => {
    const taken = new Set(devices.flatMap((o) => o.uses.filter((u) => u.pin.device === d.id).map((u) => u.pin.pin)))
    return [...d.pinAliases.keys()].some((pin) => taken.has(pin)) ? { ...d, pinAliases: new Map([...d.pinAliases].filter(([pin]) => !taken.has(pin))) } : d
  })
}

/**
 * The project's devices in the order they are created: each after the devices whose pins it uses, the others as listed. Nothing
 * orders them by hand. A cycle of uses keeps the listed order between its devices (`deviceProblems` names it).
 */
export const creationOrder = (devices: readonly DeviceInfo[]): DeviceInfo[] => {
  const byId = new Map(devices.map((d) => [d.id, d]))
  const placed = new Set<number>()
  const out: DeviceInfo[] = []
  const place = (d: DeviceInfo, below: ReadonlySet<number>) => {
    if (placed.has(d.id) || below.has(d.id)) return
    const path = new Set(below).add(d.id)
    for (const use of d.uses) {
      const used = byId.get(use.pin.device)
      if (used) place(used, path)
    }
    if (!placed.has(d.id)) {
      placed.add(d.id)
      out.push(d)
    }
  }
  devices.forEach((d) => place(d, new Set()))
  return out
}

const listDevices = (project: ProjectFile, catalog: ServiceCatalog, board: BoardDescriptor): DeviceInfo[] => [
  ...board.devices.map((d): DeviceInfo => {
    const type = boardType(catalog, d.descriptor)
    return {
      id: d.id,
      name: d.name,
      system: true,
      type,
      ...(d.description ? { description: d.description } : {}),
      config: d.config as FieldValues,
      ...(d.i2c ? { i2c: d.i2c } : {}),
      uses: d.pins.map((p) => ({ field: /^\w+/.exec(p.use)?.[0] ?? p.use, use: useLabel(type, p.use), pin: { device: p.device, pin: p.pin }, ...(p.description ? { description: p.description } : {}) })),
      pinAliases: aliasesOf(project, d.id),
      defaults: project.boardDefaults.find((b) => b.device === d.id)?.steps ?? [],
    }
  }),
  ...creationOrder(project.devices.map((d): DeviceInfo => {
    const type = catalog.devices.get(d.type)
    return { id: d.id, name: d.name, system: false, type, ...(type?.description ? { description: type.description } : {}), config: d.config ?? {}, uses: configPins(type, d.config), pinAliases: aliasesOf(project, d.id), defaults: d.defaults ?? [] }
  })),
]

/**
 * The bus of the user's devices (board.json `i2c_buses[].name`): bus 0 belongs to the board's own chips, so a project device gets
 * the user bus and its form does not offer the choice. (Unlocking the internal bus is a setting of the app, later.)
 */
export const USER_I2C_BUS = 'user'

/** The field of a config that names an I2C bus. */
export const isBusField = (field: WireField): boolean => field.enum === 'sys_i2c_bus_e'

/** The config a new project device starts from: its I2C bus is the user bus. */
export const baseConfig = (type: CatalogEntry | undefined, board: BoardDescriptor): FieldValues => {
  const bus = board.i2cBuses.find((b) => b.name === USER_I2C_BUS)
  const field = type?.config?.fields.find(isBusField)
  return bus && field ? { [field.id]: bus.bus } : {}
}

/**
 * The severities an error policy of this importance answers (`sys_device.h`: NONE answers none, LOW the critical ones only, each
 * step down one more severity): a severity at or above the threshold `top + 1 - importance`, `top` the highest severity.
 */
export const admittedSeverities = <T extends { readonly value: number }>(importance: number, severities: readonly T[]): T[] => {
  if (importance <= 0) return []
  const top = Math.max(...severities.map((s) => s.value))
  return severities.filter((s) => s.value > top - importance)
}

/** The IO contract's commands a pin's start settings are (their writes): its mode (MODE) and its interrupt (INTR). */
export const PIN_START_COMMANDS = { mode: 'MODE', interrupt: 'INTR' } as const

/** The GPIO modes a device's `pins.modes` names, as the `sys_io_mode_e` member a MODE write takes. PWM, ADC and interrupt are not here: a command sets those. */
export const GPIO_MODE_MEMBER: Readonly<Record<string, string>> = {
  input: 'SYS_IO_MODE_INPUT',
  input_pullup: 'SYS_IO_MODE_INPUT_PULLUP',
  input_pulldown: 'SYS_IO_MODE_INPUT_PULLDOWN',
  output: 'SYS_IO_MODE_OUTPUT_PUSH_PULL',
  output_od: 'SYS_IO_MODE_OUTPUT_OPEN_DRAIN',
}

/** A pin's interrupt: its `sys_io_intr_mode_e` (0 = off, no step), debounce, and the window fields an ADC window takes (mV, a count). */
export interface PinInterrupt {
  readonly mode: number
  readonly debounce: boolean
  readonly upMv: number
  readonly downMv: number
  readonly hysteresisMv: number
  readonly count: number
}

/** A user pin's start settings (the commands the board runs at every start): its mode, and its interrupt when it has one. */
export interface PinStart {
  /** The `sys_io_mode_e` value; undefined keeps the boot default. */
  readonly mode?: number
  readonly interrupt?: PinInterrupt
}

/** The modes a pin of this device can be set to: the GPIO ones (`pins.modes`), and the fixed one of an analogue device (ADC). */
export const PIN_MODE_MEMBER: Readonly<Record<string, string>> = { ...GPIO_MODE_MEMBER, adc: 'SYS_IO_MODE_ADC', pwm: 'SYS_IO_MODE_PWM' }

/** Whether a device's pins are GPIO (a mode each, set at start) or only analogue (their mode is fixed: an ADC channel). */
export const hasGpioModes = (modes: readonly string[]): boolean => modes.some((m) => m in GPIO_MODE_MEMBER)

/** A start command that sets a pin's mode or interrupt (`pin` any when omitted): read in the pin's panel, shown as a pin in the list. */
export const isPinStart = (step: Step, device: number, pin?: number): step is Extract<Step, { kind: 'device' }> =>
  step.kind === 'device' && step.device === device && step.contract === 'IO' && (step.command === PIN_START_COMMANDS.mode || step.command === PIN_START_COMMANDS.interrupt) && (pin === undefined || step.args?.pin === pin)

/** The pin's start settings, read from the device's start commands. */
export const pinStartOf = (steps: readonly Step[], device: number, pin: number): PinStart => {
  let start: PinStart = {}
  for (const step of steps) {
    if (!isPinStart(step, device, pin)) continue
    if (step.command === PIN_START_COMMANDS.mode) start = { ...start, mode: Number(step.args?.mode) }
    else {
      const args = step.args ?? {}
      start = {
        ...start,
        interrupt: {
          mode: Number(args.mode), debounce: args.debounce === true, upMv: Number(args.threshold_up_mV ?? 0), downMv: Number(args.threshold_down_mV ?? 0),
          hysteresisMv: Number(args.hysteresis_mV ?? 0), count: Number(args.event_counter_threshold ?? 0),
        },
      }
    }
  }
  return start
}

/** The start commands with the pin's settings replaced by `next`: an unset mode, or an interrupt of mode 0, has no step.
 *  An interrupt needs a configured pin (the board refuses INTR on one at its boot default), so an interrupt without a
 *  mode gets the one it needs: an input for an edge, ADC for a window. */
export const withPinStart = (steps: readonly Step[], device: number, pin: number, next: PinStart): Step[] => {
  const made: Step[] = []
  const i = next.interrupt
  const mode = next.mode ?? (i && i.mode !== 0 ? (i.mode >= 4 ? 7 : 0) : undefined)
  if (mode !== undefined) made.push({ kind: 'device', device, contract: 'IO', command: PIN_START_COMMANDS.mode, args: { pin, mode } })
  if (i && i.mode !== 0) {
    made.push({
      kind: 'device', device, contract: 'IO', command: PIN_START_COMMANDS.interrupt,
      // Every argument of INTR is given (the window fields are 0 for an edge).
      args: { pin, mode: i.mode, debounce: i.debounce, threshold_up_mV: i.upMv, threshold_down_mV: i.downMv, hysteresis_mV: i.hysteresisMv, event_counter_threshold: i.count },
    })
  }
  return [...steps.filter((step) => !isPinStart(step, device, pin)), ...made]
}

/* ---- the state a pin is in: which commands it can take ---- */

/** The kind of state a `sys_io_mode_e` member puts a pin in (the `pin_state` words of the IO descriptor): input, output, pwm, adc, dac. */
const modeKind = (member: string): string | undefined =>
  /^SYS_IO_MODE_INPUT/.test(member) ? 'input' : /^SYS_IO_MODE_OUTPUT/.test(member) ? 'output' : /^SYS_IO_MODE_(PWM|ADC|DAC)$/.exec(member)?.[1]?.toLowerCase()

/** The state kind of a `sys_io_mode_e` value, or undefined for one that is none. */
const kindOfMode = (mode: number): string | undefined => {
  const member = bundledEnums().sys_io_mode_e?.members.find((m) => m.value === mode)
  return member && modeKind(member.name)
}

/**
 * Whether a device's command offers a member of an enum field: the modes of a MODE write only the ones its pins can be (`pins.modes`), the
 * interrupt modes of INTR the edges for a device with digital pins and the ADC windows for one with only analogue pins.
 */
export const offersMember = (device: DeviceInfo | undefined, group: string | undefined, value: number): boolean => {
  const modes = device?.type?.pins?.modes
  if (!modes) return true
  if (group === 'sys_io_mode_e') return modes.some((m) => PIN_MODE_MEMBER[m] !== undefined && bundledEnums().sys_io_mode_e?.members.find((x) => x.name === PIN_MODE_MEMBER[m])?.value === value)
  if (group === 'sys_io_intr_mode_e') return value === 0 || (hasGpioModes(modes) ? value <= 3 : value >= 4)
  return true
}

/** The state kind of a MODE argument as a form keeps it: the member's value, or its alias or name. */
export const kindOfModeValue = (mode: unknown): string | undefined => {
  const member = bundledEnums().sys_io_mode_e?.members.find((m) => m.value === Number(mode) || m.alias === mode || m.name === mode)
  return member && modeKind(member.name)
}

/** The state kinds a device's `pins.modes` word stands for (a GPIO's `input_pullup` is an input, `interrupt` is no state). */
const kindOfCapability = (word: string): string | undefined => (word.startsWith('input') ? 'input' : word === 'output' || word === 'output_od' ? 'output' : ['pwm', 'adc', 'dac'].includes(word) ? word : undefined)

/**
 * What the app knows of the state of a pin of `device` (the words of `pin_state`): `unconfigured`, or the kinds it is in. A device
 * without the MODE write (`setMode` false: PCA9685's channels, the DAC) has its pins fixed: every kind it offers. Otherwise it is what
 * this session set live (`live`: a kind, or null after a reset), else what the start commands do (the last MODE write, a RESET
 * clears it), else what the boot sets up (board.json `pin_setup`): the app cannot ask the board, so a pin set up by an earlier
 * session and not in the project reads as unconfigured.
 */
export const pinStateOf = (device: DeviceInfo, board: BoardDescriptor, pin: number, setMode: boolean, live?: ReadonlyMap<string, string | null>): readonly string[] => {
  const reset = device.type?.pins?.defaultMode
  if (!setMode) return [...new Set((device.type?.pins?.modes ?? []).flatMap((m) => kindOfCapability(m) ?? []))]
  const liveKind = live?.get(`${device.id}/${pin}`)
  if (liveKind !== undefined) return liveKind === null ? (reset ? [kindOfCapability(reset) ?? reset, 'unconfigured'] : ['unconfigured']) : reset ? [liveKind, 'unconfigured'] : [liveKind]
  let kind: string | undefined
  const boot = board.pinSetup.find((p) => p.device === device.id && p.pin === pin)
  if (boot) kind = modeKind(boot.mode)
  for (const step of device.defaults) {
    if (step.kind !== 'device' || step.contract !== 'IO' || step.args?.pin !== pin) continue
    if (step.command === PIN_START_COMMANDS.mode) kind = kindOfMode(Number(step.args.mode))
    else if (step.command === 'RESET') kind = undefined
  }
  // A device with a reset mode (default_mode: the expander's inputs, the ADC's channels) is never `unconfigured`: its pins have that mode
  // until a MODE write changes it, and one is accepted at any time.
  if (reset) return [...new Set([kind ?? kindOfCapability(reset) ?? reset, 'unconfigured'])]
  return kind ? [kind] : ['unconfigured']
}

/** The pin is in a state the command wants (`pin_state` of its pin field): any of the words; `configured` is any kind. */
export const pinReady = (states: readonly string[], wants: readonly string[] | undefined): boolean =>
  !wants || wants.some((w) => (w === 'configured' ? states.some((s) => s !== 'unconfigured') : states.includes(w)))

/** What to say when no pin is ready for a command that wants `wants`. */
export const noPinReady = (wants: readonly string[]): string =>
  wants.includes('unconfigured') ? 'Every pin is set up already' : wants.includes('configured') ? 'No pin is set up yet: set its mode first' : `No pin is set up as ${wants.join(' or ')}: set its mode first`

/** The fields of INTR that only an ADC window takes. */
const WINDOW_FIELDS = ['threshold_up_mV', 'threshold_down_mV', 'hysteresis_mV', 'event_counter_threshold']

/** A pin's number as the app shows it: always `#n` (what commands and errors carry). */
export const pinNumber = (pin: number): string => `#${pin}`

/** The descriptor's own name of a pin (`P26`, `CH8`): a hint beside the number, never in its place. */
export const pinHint = (type: CatalogEntry | undefined, pin: number): string | undefined => type?.pins?.labels?.[pin]

/** Names a pin of a device: `#n`, then the user's name of it (`#22 LED blue`). */
export type PinNamer = (device: number, pin: number) => string

/** The pin names of these devices: always the number, then the user's alias when it has one. */
export const pinNamer = (devices: readonly DeviceInfo[]): PinNamer => (device, pin) => {
  const alias = devices.find((x) => x.id === device)?.pinAliases.get(pin)
  return alias ? `${pinNumber(pin)} ${alias}` : pinNumber(pin)
}

/** A pin in a list of pins: `#n`, then the use that names it when another device has it (`#8 Motor driver 2 · Nsleep pin`), else its alias. */
export const pinChoiceLabel = (devices: readonly DeviceInfo[], device: number, pin: number): string => {
  const user = usedBy(devices, device).find((u) => u.use.pin.pin === pin)
  return user ? `${pinNumber(pin)} ${user.by.name} · ${user.use.use}` : pinNamer(devices)(device, pin)
}

/**
 * A side of a command changes the pin it is given: its write (level, toggle, PWM, mode, reset, interrupt, lock; not the reads). A pin
 * another device uses is managed by that device, so the view offers it to reads only.
 */
export const changesPin = (side: Side): boolean => side === 'write'

/** The devices that use a pin of `device`, and which pin. */
export const usedBy = (devices: readonly DeviceInfo[], device: number): { readonly by: DeviceInfo; readonly use: PinUse }[] =>
  devices.flatMap((by) => by.uses.filter((use) => use.pin.device === device).map((use) => ({ by, use })))

/** A pin the board keeps for itself (board.json `reserved_pins`: the I2C buses, the strapping pins, the expander's lines): no command or reference takes it. */
export const isReserved = (board: BoardDescriptor, device: number, pin: number): boolean => board.reservedPins.some((p) => p.device === device && p.pin === pin)

/** What board.json says of a device's pins: the reserved ones (why) and the ones the boot sets up (as what). */
export const pinNotes = (board: BoardDescriptor, device: number): ReadonlyMap<number, string> => {
  const notes = new Map<number, string>()
  for (const p of board.reservedPins) if (p.device === device) notes.set(p.pin, `Reserved: ${p.label}`)
  for (const p of board.pinSetup) if (p.device === device) notes.set(p.pin, p.label)
  return notes
}

/* ---- commands ---- */

/** The ops a device answers (its OPS), each with the bits of its sides. */
export type DeviceOps = ReadonlyMap<number, number>

/** The bit of each side in an OPS row's `sides` (`contract_device.json`, `SIDE_READ` / `SIDE_WRITE` in sys_device.c). */
export const OPS_SIDE_BIT: Readonly<Record<Side, number>> = { read: 0x01, write: 0x02 }

/** The OPS answer as the ops and their sides. */
export const decodeOps = (value: unknown): DeviceOps =>
  new Map((Array.isArray(value) ? value : []).map((row: Readonly<Record<string, unknown>>) => [Number(row.op), Number(row.sides)]))

/** The device answers this side of the command; true until its OPS says (`ops` undefined). */
export const answers = (catalog: ServiceCatalog, ops: DeviceOps | undefined, entry: CatalogEntry, command: CommandDef, side: Side): boolean =>
  command[side] !== undefined && (!ops || ((ops.get(deviceOp(catalog, entry, command)) ?? 0) & OPS_SIDE_BIT[side]) !== 0)

/** One side of one command: what the test panel tries. */
export interface CommandSideOf {
  readonly command: CommandDef
  readonly side: Side
}

/**
 * The commands a device can be sent, by contract: the device contract, each contract its type implements, its own ops; each side of
 * a command on its own (a mirror is its read and its write). CREATE is the install, never listed; a board device (created by the
 * firmware) has no REMOVE either. With the device's OPS answer only the sides it answers are listed (a contract may describe ops a
 * chip does not have: an expander has no PWM).
 */
export const commandGroups = (catalog: ServiceCatalog, type: CatalogEntry | undefined, { ops }: { readonly ops?: DeviceOps } = {}): { readonly entry: CatalogEntry; readonly commands: readonly CommandSideOf[] }[] => {
  const lifecycle = catalog.lifecycleContractId === undefined ? undefined : catalog.byContractId.get(catalog.lifecycleContractId)
  const contracts = [lifecycle, ...(type?.contracts ?? []).map((c) => catalog.contracts.get(c)).filter((c) => c !== lifecycle), type]
  // The test panel has these elsewhere: Install / Remove and the lifecycle icons on top, the error policy under them; INFO and OPS the
  // view asks by itself, and the policy is read back with the start commands.
  const hidden = (entry: CatalogEntry, command: CommandDef) => entry === lifecycle && (command.id === CREATE || (Object.values(DEVICE_COMMANDS) as string[]).includes(command.id))
  return contracts
    .filter((entry, i, all): entry is CatalogEntry => entry !== undefined && all.indexOf(entry) === i)
    .map((entry) => ({
      entry,
      commands: [...entry.commands.values()].filter((c) => !hidden(entry, c)).flatMap((command) => sidesOf(command).filter((side) => answers(catalog, ops, entry, command, side)).map((side) => ({ command, side }))),
    }))
    .filter((group) => group.commands.length > 0)
}

/** A side's name in a list: the command's label, and the side when the command has both (`Level: read`). */
export const sideLabel = ({ command, side }: CommandSideOf): string => (isMirror(command) ? `${command.label}: ${side}` : command.label)

/** The command of the device contract that creates a device (SYS_DEVICE.MD "Ops"). */
const CREATE = 'CREATE'

/** The device contract (`SYS_DEVICE_CONTRACT_DEVICE`): RESET, SUSPEND, RESUME, REMOVE, INFO, OPS... */
export const lifecycleOf = (catalog: ServiceCatalog): CatalogEntry => {
  const entry = catalog.lifecycleContractId === undefined ? undefined : catalog.byContractId.get(catalog.lifecycleContractId)
  if (!entry) throw new Error('No descriptor of the device contract (SYS_DEVICE_CONTRACT_DEVICE).')
  return entry
}

/* ---- frames and records ---- */

/** The frame of a step, as a record holds it: a step is a write (a record holds writes only, SYS_ACTIONS.MD). */
export const stepFrame = (defs: LinkDefinitions, catalog: ServiceCatalog, step: Step): Uint8Array =>
  step.kind === 'frame' ? Uint8Array.from(step.bytes) : requestFrame(deviceCall(defs, catalog, step.device, step.contract, step.command, step.args, 'write').request)

/** The install packet of a project device: CREATE with its type and config. */
export const installFrame = (defs: LinkDefinitions, catalog: ServiceCatalog, device: ProjectDevice): Uint8Array =>
  requestFrame(createDeviceCall(defs, catalog, device.id, device.type, device.config).request)

/** The frames of a device's record: CREATE for a project device, then its start commands. */
export const deviceFrames = (defs: LinkDefinitions, catalog: ServiceCatalog, project: ProjectFile, device: DeviceInfo): Uint8Array[] => {
  const own = project.devices.find((d) => d.id === device.id)
  return [...(device.system || !own ? [] : [installFrame(defs, catalog, own)]), ...device.defaults.map((s) => stepFrame(defs, catalog, s))]
}

/** The sections of the stored project the devices are: the board's devices with start commands, then every project device. */
export const deviceSections = (defs: LinkDefinitions, catalog: ServiceCatalog, project: ProjectFile, board: BoardDescriptor): Section[] =>
  deviceList(project, catalog, board)
    .filter((d) => !d.system || d.defaults.length > 0)
    .map((d) => ({ id: deviceRecordOf(d.id), frames: deviceFrames(defs, catalog, project, d) }))

/** What is wrong with the devices before they go to the board: an ID twice, a cycle of uses (the order is `creationOrder`'s). */
export const deviceProblems = (project: ProjectFile, catalog: ServiceCatalog, board: BoardDescriptor): string[] => {
  const problems: string[] = []
  const devices = deviceList(project, catalog, board)
  devices.forEach((d, i) => {
    const twin = devices.findIndex((o) => o.id === d.id)
    if (twin !== i) problems.push(`${d.name} has the ID of ${devices[twin]!.name} (${d.id}).`)
    if (!d.type) problems.push(`${d.name}: no descriptor describes its type.`)
    for (const use of d.uses) {
      const at = devices.findIndex((o) => o.id === use.pin.device)
      if (at < 0) problems.push(`${d.name}: ${use.use} is on a device the board does not have.`)
      else if (at > i) problems.push(`${d.name} uses ${devices[at]!.name}, which uses ${d.name} back: they cannot be created in turn.`)
    }
  })
  return problems
}

/** The lowest device ID above the board's own that nothing uses yet. */
export const nextDeviceId = (project: ProjectFile, board: BoardDescriptor, onBoard: ReadonlySet<number> = new Set()): number => {
  const taken = new Set([...board.devices.map((d) => d.id), ...project.devices.map((d) => d.id), ...onBoard])
  let id = Math.max(-1, ...board.devices.map((d) => d.id)) + 1
  while (taken.has(id)) id++
  return id
}

/**
 * The project with a device moved from ID `from` to ID `to`: the device itself and every reference to it (a pin of its config,
 * a command of a start list or an action, a pin alias, a block's device) name the new ID.
 */
export const renumberDevice = (project: ProjectFile, from: number, to: number): ProjectFile => {
  const walk = (value: unknown): unknown => {
    if (Array.isArray(value)) return value.map(walk)
    if (value === null || typeof value !== 'object') return value
    return Object.fromEntries(Object.entries(value).map(([key, v]) => [key, key === 'device' && v === from ? to : walk(v)]))
  }
  const moved = walk(project) as ProjectFile
  return { ...moved, devices: moved.devices.map((d) => (d.id === from ? { ...d, id: to } : d)) }
}

/** The lowest action ID (1 and up) the project and the firmware's onboard actions leave free. */
export const nextActionId = (project: ProjectFile): number => {
  const taken = new Set([...onboardActionIds(), ...project.actions.map((a) => a.id)])
  let id = 1
  while (taken.has(id)) id++
  return id
}

/* ---- read back ---- */

/** A decoded field value as a project keeps it (JSON): bytes as a list, a u64 as a number. */
const keepable = (record: FieldRecord | undefined): FieldValues | undefined =>
  record && Object.fromEntries(Object.entries(record).map(([id, value]) => [id, value instanceof Uint8Array ? Array.from(value) : typeof value === 'bigint' ? Number(value) : value]))

/** What the board keeps: the device records (the stored project's sections, else the device records alone) and the user actions. */
export interface BoardRecords {
  readonly devices: readonly Section[]
  readonly actions: readonly Section[]
}

/**
 * The project with the devices and actions the board keeps in place of its own: a device record that starts with CREATE is a
 * project device (its type and config), one without is a board device's start commands; a frame the catalog does not describe
 * stays as its bytes. Names the project already gives an ID are kept. The program is left as it is.
 */
export const fromBoard = (defs: LinkDefinitions, catalog: ServiceCatalog, board: BoardDescriptor, project: ProjectFile, records: BoardRecords): { readonly project: ProjectFile; readonly notes: string[] } => {
  const notes: string[] = []
  const types = new Map<number, CatalogEntry>()
  for (const d of board.devices) {
    const type = boardType(catalog, d.descriptor)
    if (type) types.set(d.id, type)
  }
  const typeOf = (device: number) => types.get(device)
  const step = (frame: Uint8Array): Step => {
    const read = readDeviceFrame(defs, catalog, frame, typeOf)
    if (!read || read.create || read.side !== 'write') return { kind: 'frame', bytes: Array.from(frame) }
    const args = keepable(read.args)
    return { kind: 'device', device: read.device, contract: read.entry.id, command: read.command.id, ...(args ? { args } : {}) }
  }

  const devices: ProjectDevice[] = []
  const boardDefaults: BoardDefaults[] = []
  for (const section of records.devices) {
    if (recordClass(section.id) !== DEVICE_CLASS) continue
    const id = section.id & 0xffff
    const [first, ...rest] = section.frames
    const create = first && readDeviceFrame(defs, catalog, first, typeOf)?.create
    if (create) {
      types.set(id, create.type)
      const config = keepable(create.config)
      const known = project.devices.find((d) => d.id === id)
      devices.push({ id, type: create.type.id, name: known?.name ?? `${create.type.label} ${id}`, ...(config ? { config } : {}), defaults: rest.map(step) })
    } else if (board.devices.some((d) => d.id === id)) boardDefaults.push({ device: id, steps: section.frames.map(step) })
    else notes.push(`The record of device ${id} has no install command and the board has no device ${id}: left out.`)
  }
  const onboard = onboardActionIds()
  const actions: ProjectAction[] = records.actions
    .filter((a) => recordClass(a.id) === USER_CLASS && !onboard.has(a.id & 0xffffff))
    .map((a) => {
      const id = a.id & 0xffffff
      return { id, name: project.actions.find((p) => p.id === id)?.name ?? `Action ${id}`, steps: a.frames.map(step) }
    })
  const gone = project.devices.filter((d) => !devices.some((n) => n.id === d.id)).length
  if (gone > 0) notes.push(`${gone} device(s) of the project are not on the board and were left out (Undo brings them back).`)
  return { project: { ...project, devices, boardDefaults, actions }, notes }
}

/* ---- showing values ---- */

/** A value of a field as a user reads it: a choice by its label, a pin by its device and name, bytes in hex. */
export const showValue = (field: WireField, value: unknown, nameOf: (device: number) => string | undefined, pinName: PinNamer, own?: number): string => {
  if (value === undefined) return '—'
  if (value === null) return 'none'
  if (isPin(value)) return `${nameOf(value.device) ?? `Device ${value.device}`} · ${pinName(value.device, value.pin)}`
  if (Array.isArray(value)) {
    if (field.rawType === 'bytes' && !field.pinRef) return value.map((b: number) => b.toString(16).padStart(2, '0')).join(' ')
    return value.map((v) => showValue(field, v, nameOf, pinName, own)).join(', ')
  }
  if (value instanceof Uint8Array) return Array.from(value, (b) => b.toString(16).padStart(2, '0')).join(' ')
  const choice = field.choices?.find((c) => c.value === value || (typeof c.value === 'boolean' && Number(c.value) === value))
  if (choice) return choice.label
  if (field.type === 'pin' && typeof value === 'number' && own !== undefined) return pinName(own, value)
  if (typeof value === 'boolean') return value ? 'Yes' : 'No'
  return `${String(value)}${field.unit ? ` ${field.unit}` : ''}`
}

/** A step in one line: the device, the command and its arguments. */
export const describeStep = (catalog: ServiceCatalog, step: Step, nameOf: (device: number) => string | undefined, pinName: PinNamer): { readonly device?: number; readonly command: string; readonly args: string } => {
  if (step.kind === 'frame') return { command: 'Command', args: step.bytes.map((b) => b.toString(16).padStart(2, '0')).join(' ') }
  const entry = catalog.contracts.get(step.contract) ?? catalog.devices.get(step.contract)
  const command = entry?.commands.get(step.command)
  // An edge interrupt has no window: its threshold fields are not shown (INTR takes them, ignored for an edge).
  const window = step.args?.mode === 4 || step.args?.mode === 5
  const fields = (command?.write?.args?.fields ?? []).filter((f) => window || !WINDOW_FIELDS.includes(f.id))
  const args = fields.map((f) => `${f.label} ${showValue(f, step.args?.[f.id] ?? f.default, nameOf, pinName, step.device)}`).join(' · ')
  return { device: step.device, command: command ? `${entry!.label}: ${command.label}` : `${step.contract} ${step.command}`, args }
}

/** An answer of a side as a user reads it: a struct field by field, a value with its unit or choice, a list joined; a list of u16 in hex. */
export const showAnswer = (command: CommandDef, side: Side, value: unknown, nameOf: (device: number) => string | undefined, pinName: PinNamer, own?: number): string => {
  const returns = command[side]?.returns
  if (!returns || value === undefined) return 'Done.'
  if (returns.kind === 'struct') {
    const one = (record: unknown) => returns.layout.fields.map((f) => `${f.label}: ${showValue(f, (record as Record<string, unknown>)[f.id], nameOf, pinName, own)}`).join(' · ')
    return Array.isArray(value) ? value.map(one).join('\n') : one(value)
  }
  const field = { id: '', label: returns.label ?? '', type: returns.boolean ? 'boolean' : 'number', rawType: returns.rawType, offset: 0, optional: false, pinRef: false, ...(returns.unit ? { unit: returns.unit } : {}), ...(returns.choices ? { choices: returns.choices } : {}) } satisfies WireField
  const hex = returns.array && returns.rawType === 'u16' && !returns.unit
  const show = (v: unknown) => (hex && typeof v === 'number' ? `0x${v.toString(16).padStart(4, '0')}` : showValue(field, v, nameOf, pinName, own))
  return `${returns.label ? `${returns.label}: ` : ''}${Array.isArray(value) ? value.map(show).join(', ') : show(value)}`
}

/** The device contract's commands the view sends by itself (`contract_device.json`); `devices.test.ts` holds them against the catalog. */
/** The types a project cannot add more of: the chip's own GPIO, which the board already has (board.json). */
export const SINGLE_TYPES: ReadonlySet<string> = new Set(['gpio_esp'])

export const DEVICE_COMMANDS = { info: 'INFO', ops: 'OPS', remove: 'REMOVE', reset: 'RESET', suspend: 'SUSPEND', resume: 'RESUME', policy: 'ERROR_POLICY' } as const

/** The device contract's events the view follows (`contract_device.json`, SYS_DEVICE_EVENTS): a device's `suspended` flag changed. */
export const DEVICE_EVENTS = { suspended: 'SUSPENDED', resumed: 'RESUMED' } as const
