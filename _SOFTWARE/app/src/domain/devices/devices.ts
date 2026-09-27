import { packCommand, pinUseLabel } from '../descriptors'
import type { CommandCatalog, DeviceCatalog, DeviceChoice, DeviceContract, DeviceParameter, DeviceType } from '../descriptors'
import { boardDeviceRef } from '../project'
import type { ActionStep, DeviceRef, ProjectAction, ProjectDevice, StepValues } from '../project'
import type { UploadDiagnostic, UploadStep } from '../upload'

/*
 * Devices and actions → frames. A user device is installed by its type's
 * install packet (class 0x01, 0x40–0x4F) with the project's values and its
 * device ID; the stored code carries these installs, so the board has the
 * devices after every boot. A contract call takes its device ID from the
 * device it runs on. An action is a list of contract calls, recorded on the
 * board as a dynamic action (record start, the frames, record stop).
 */

/** What a device reference points at: a board device or a user device, with its type (if published). */
export interface ResolvedDevice {
  readonly ref: DeviceRef
  readonly deviceId: number
  readonly name: string
  readonly type?: DeviceType
  readonly system: boolean
}

export const resolveDevice = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], ref: DeviceRef): ResolvedDevice | undefined => {
  if (ref.startsWith('board:')) {
    const deviceId = Number(ref.slice('board:'.length))
    const board = catalog.board.find((entry) => entry.deviceId === deviceId)
    return board && { ref, deviceId, name: board.name, ...(board.type ? { type: board.type } : {}), system: true }
  }
  const device = devices.find((entry) => entry.id === ref)
  const type = device && catalog.type(device.type)
  return device && { ref, deviceId: device.deviceId, name: device.name, ...(type ? { type } : {}), system: false }
}

/** Every device a contract can run on: the board's, then the user's. */
export const allDevices = (catalog: DeviceCatalog, devices: readonly ProjectDevice[]): ResolvedDevice[] => [
  ...catalog.board.map((board) => resolveDevice(catalog, devices, boardDeviceRef(board.deviceId))!),
  ...devices.map((device) => resolveDevice(catalog, devices, device.id)!),
]

/** Contracts a device takes: its type's, then the lifecycle commands every device takes. */
export const contractsOf = (catalog: DeviceCatalog, device: ResolvedDevice | undefined): readonly DeviceContract[] => [...(device?.type?.contracts ?? []), ...catalog.lifecycle]

export const findContract = (catalog: DeviceCatalog, device: ResolvedDevice | undefined, id: string): DeviceContract | undefined => contractsOf(catalog, device).find((contract) => contract.id === id)

// ---------------------------------------------------------------------------
// Pins: which device takes which pin
// ---------------------------------------------------------------------------

export const pinKey = (deviceId: number, pin: number): string => `${deviceId}:${pin}`

/** Who takes a pin: a device (its install config) or the board's own setup. */
export interface PinUser {
  /** The device taking the pin, or undefined for the board's own setup (status LEDs …). */
  readonly owner?: DeviceRef
  readonly ownerName: string
  /** What for: `Interrupt pin`, `in pins[2]`, a board label. */
  readonly use: string
  /** sys_io_mode_e value the owner sets (none for pins used outside sys_io, e.g. I2C). */
  readonly mode?: number
  readonly level?: boolean
}

/** Every pin taken on the board: by the installed board devices, the board's own setup and the user's devices. */
export const pinUsers = (catalog: DeviceCatalog, devices: readonly ProjectDevice[]): Map<string, PinUser[]> => {
  const users = new Map<string, PinUser[]>()
  const add = (deviceId: number, pin: number, user: PinUser) => users.set(pinKey(deviceId, pin), [...(users.get(pinKey(deviceId, pin)) ?? []), user])
  for (const board of catalog.board) {
    if (!board.installed) continue
    for (const link of board.pins) add(link.deviceId, link.pin, { owner: boardDeviceRef(board.deviceId), ownerName: board.name, use: pinUseLabel(link.use), mode: link.mode })
  }
  for (const reserved of catalog.reservedPins) add(reserved.deviceId, reserved.pin, { ownerName: 'Board', use: reserved.label })
  for (const setup of catalog.boardPinSetup) add(setup.deviceId, setup.pin, { ownerName: 'Board', use: setup.label ?? 'Board pin', mode: setup.mode, ...(setup.level === undefined ? {} : { level: setup.level }) })
  for (const device of devices) {
    const type = catalog.type(device.type)
    for (const group of type?.pinGroups ?? []) {
      if (group.sentinelField && device.install[group.sentinelField] === group.sentinel) continue
      if (!group.deviceField || !group.pinField) continue
      add(device.install[group.deviceField] ?? 0, device.install[group.pinField] ?? 0, { owner: device.id, ownerName: device.name, use: group.label.replace(/ pin$/, ''), mode: group.modeField ? device.install[group.modeField] ?? 0 : 0 })
    }
  }
  return users
}

/** The pins a device type has, from its pin-mode contract (else any contract with a pin choice list). */
export const pinsOf = (type: DeviceType | undefined): readonly DeviceChoice[] => {
  const contract = type?.contracts.find((entry) => entry.command.name === 'sys_io_set_mode') ?? type?.contracts.find((entry) => entry.parameters.some((parameter) => parameter.name === 'pin' && parameter.choices))
  return contract?.parameters.find((parameter) => parameter.name === 'pin')?.choices ?? []
}

/** Contracts that set something up (default settings): not reads, not the lifecycle commands. */
export const isSetupContract = (contract: DeviceContract): boolean =>
  contract.kind === 'contract' && !contract.returns && !contract.command.response && !/_(get|list)(_|$)/.test(contract.command.name)

/** First free device ID after the board's own, up to the firmware's limit. */
export const nextDeviceId = (catalog: DeviceCatalog, devices: readonly ProjectDevice[]): number | undefined => {
  const taken = new Set([...catalog.board.map((entry) => entry.deviceId), ...devices.map((entry) => entry.deviceId)])
  for (let id = Math.max(...catalog.board.map((entry) => entry.deviceId)) + 1; id <= catalog.maxDeviceId; id++) if (!taken.has(id)) return id
  return undefined
}

/** Install values a new device starts with: the user I2C bus, pin groups set to "none", the rest at their first choice or minimum. */
export const defaultInstall = (type: DeviceType, i2cBus?: number): Record<string, number> => {
  const values: Record<string, number> = {}
  for (const field of type.install.request.fields) {
    if (field.name === 'device_id') continue
    values[field.name] = field.name === 'i2c_bus' && i2cBus !== undefined ? i2cBus : type.installChoices.get(field.name)?.[0]?.value ?? field.min ?? field.fallback
  }
  for (const group of type.pinGroups) if (group.sentinelField) values[group.sentinelField] = group.sentinel
  return values
}

export const installFrame = (type: DeviceType, device: Pick<ProjectDevice, 'deviceId' | 'install'>): Uint8Array =>
  packCommand(type.install, { ...device.install, device_id: device.deviceId })

/** The value a parameter starts with: its default, first choice, minimum, or the field's fallback; a list of those for an array field. */
export const initialParameterValue = (parameter: DeviceParameter): number | number[] => {
  const single = parameter.defaultValue ?? parameter.choices?.[0]?.value ?? parameter.min ?? parameter.field.fallback
  return parameter.field.kind === 'array' ? new Array<number>(parameter.field.arrayLength ?? 0).fill(single) : single
}

/** A parameter's value from a step: an array field padded (or cut) to its length, a scalar as is. */
export const parameterValue = (parameter: DeviceParameter, values: StepValues): number | number[] => {
  const value = values[parameter.name]
  const initial = initialParameterValue(parameter)
  if (!Array.isArray(initial)) return typeof value === 'number' ? value : initial
  const given = Array.isArray(value) ? value : typeof value === 'number' ? [value] : []
  return initial.map((fallback, index) => given[index] ?? fallback)
}

/** A contract call: the `instance` parameter (device ID) from the device, the rest from `values` (missing = default or 0). */
export const contractFrame = (contract: DeviceContract, deviceId: number, values: StepValues): Uint8Array => {
  const filled: Record<string, number | number[]> = {}
  for (const parameter of contract.parameters) filled[parameter.name] = parameter.instance ? deviceId : parameterValue(parameter, values)
  return packCommand(contract.command, filled)
}

/**
 * Problems with the user's devices: unknown type, ID clashes, values out of
 * range, not on the user I2C bus, two devices on one I2C address, a pin
 * another device already takes, a pin on a device with a higher ID.
 */
export const checkDevices = (catalog: DeviceCatalog, devices: readonly ProjectDevice[]): UploadDiagnostic[] => {
  const diagnostics: UploadDiagnostic[] = []
  const board = new Map(catalog.board.map((entry) => [entry.deviceId, entry.name]))
  const addresses = new Map<string, string>()
  const users = pinUsers(catalog, devices)
  for (const device of devices) {
    const error = (message: string) => diagnostics.push({ severity: 'error', message: `Device '${device.name}': ${message}`, subjectId: device.id })
    const type = catalog.type(device.type)
    if (!type) {
      error(`the firmware has no device type '${device.type}'.`)
      continue
    }
    if (board.has(device.deviceId)) error(`device ID ${device.deviceId} is the board's ${board.get(device.deviceId)}.`)
    if (device.deviceId > catalog.maxDeviceId) error(`device IDs go up to ${catalog.maxDeviceId}.`)
    if (devices.some((other) => other !== device && other.deviceId === device.deviceId)) error(`device ID ${device.deviceId} is used twice.`)
    for (const field of type.install.request.fields) {
      if (field.name === 'device_id') continue
      const value = device.install[field.name]
      const sentinel = type.pinGroups.some((group) => group.sentinelField === field.name && group.sentinel === value)
      if (value === undefined) error(`${field.label} is not set.`)
      else if (!sentinel && ((field.min !== undefined && value < field.min) || (field.max !== undefined && value > field.max))) error(`${field.label} ${value} is outside ${field.min ?? ''}..${field.max ?? ''}.`)
    }
    const bus = device.install.i2c_bus
    if (bus !== undefined && bus !== catalog.i2cBuses.user) error(`user devices go on the user I2C bus ${catalog.i2cBuses.user}; bus ${bus} is the board's own.`)
    for (const group of type.pinGroups) {
      if (!group.deviceField || !group.pinField || (group.sentinelField && device.install[group.sentinelField] === group.sentinel)) continue
      const target = device.install[group.deviceField] ?? 0
      const pin = device.install[group.pinField] ?? 0
      const owner = resolveDevice(catalog, devices, board.has(target) ? boardDeviceRef(target) : devices.find((entry) => entry.deviceId === target)?.id ?? '')
      if (!owner) error(`${group.label}: device ${target} doesn't exist.`)
      else if (owner.system && !catalog.board.find((entry) => entry.deviceId === target)?.installed) error(`${group.label}: ${owner.name} isn't installed on this board.`)
      // SYS_DEVICE.MD: the board installs and resumes devices from the lowest ID and suspends / removes them from the highest.
      else if (target >= device.deviceId) error(`${group.label} is on ${owner.name} (ID ${target}): a device's pins must be on a device with a lower ID, so give ${owner.name} an ID below ${device.deviceId}.`)
      const others = (users.get(pinKey(target, pin)) ?? []).filter((user) => user.owner !== device.id)
      if (others.length) error(`${group.label}: ${owner?.name ?? `device ${target}`} pin ${pin} is taken by ${others.map((user) => `${user.ownerName} (${user.use})`).join(', ')}.`)
    }
    const address = device.install.i2c_addr
    if (bus !== undefined && address !== undefined) {
      const key = `${bus}:${address}`
      if (addresses.has(key)) error(`I2C bus ${bus} address 0x${address.toString(16)} is also '${addresses.get(key)}'.`)
      addresses.set(key, device.name)
    }
  }
  return diagnostics
}

// ---------------------------------------------------------------------------
// Default settings (setup): contract calls run after the installs
// ---------------------------------------------------------------------------

/** Problems with the default settings (subject `setup:<device ref>`): gone devices or contracts, a pin another device takes. */
export const checkSetup = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], setup: readonly ActionStep[]): UploadDiagnostic[] => {
  const diagnostics: UploadDiagnostic[] = []
  const users = pinUsers(catalog, devices)
  for (const step of setup) {
    const device = resolveDevice(catalog, devices, step.device)
    const contract = findContract(catalog, device, step.contract)
    if (!device || !contract) {
      diagnostics.push({ severity: 'error', message: `Default setting ${step.contract}: ${device ? `${device.name} has no such contract` : `device ${step.device} is gone`}.`, subjectId: `setup:${step.device}` })
      continue
    }
    const pin = typeof step.values.pin === 'number' ? step.values.pin : undefined
    const taken = pin === undefined ? [] : (users.get(pinKey(device.deviceId, pin)) ?? []).filter((user) => user.owner !== step.device)
    if (taken.length) diagnostics.push({ severity: 'error', message: `${device.name} pin ${pin}: ${contract.label} — the pin is taken by ${taken.map((user) => `${user.ownerName} (${user.use})`).join(', ')}.`, subjectId: `setup:${step.device}` })
  }
  return diagnostics
}

/** Setup frames for the stored code: pin modes first, then the rest in project order. */
export const deviceSetupSteps = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], setup: readonly ActionStep[]): { steps: UploadStep[]; diagnostics: UploadDiagnostic[] } => {
  const diagnostics = checkSetup(catalog, devices, setup)
  if (diagnostics.some((entry) => entry.severity === 'error')) return { steps: [], diagnostics }
  const ordered = [...setup.filter((step) => step.contract === 'packet_sys_io_set_mode_t'), ...setup.filter((step) => step.contract !== 'packet_sys_io_set_mode_t')]
  const steps = ordered.map((step) => {
    const device = resolveDevice(catalog, devices, step.device)!
    return { label: `setup ${describeStep(catalog, devices, step)}`, frame: contractFrame(findContract(catalog, device, step.contract)!, device.deviceId, step.values) }
  })
  return { steps, diagnostics }
}

/**
 * Install frames for the stored code, by device ID: the order devices depend
 * on each other in (checkDevices: a device's pins are on a lower-ID device).
 * Empty when a device has errors.
 */
export const deviceInstallSteps = (catalog: DeviceCatalog, devices: readonly ProjectDevice[]): { steps: UploadStep[]; diagnostics: UploadDiagnostic[] } => {
  const diagnostics = checkDevices(catalog, devices)
  if (diagnostics.some((entry) => entry.severity === 'error')) return { steps: [], diagnostics }
  const steps = [...devices].sort((a, b) => a.deviceId - b.deviceId).map((device) => ({ label: `device install ${device.name} (${device.deviceId})`, frame: installFrame(catalog.type(device.type)!, device) }))
  return { steps, diagnostics }
}

// ---------------------------------------------------------------------------
// Error handling (sys_device_set_error_handling)
// ---------------------------------------------------------------------------

/** The contract that sets a device's importance and its action per error level. */
export const SET_ERROR_HANDLING = 'packet_sys_device_set_error_handling_t'

/** What runs when the device reports an error of one level: nothing, a static (built-in) or a dynamic (recorded) action. */
export interface ErrorAction {
  readonly scope: 'static' | 'dynamic'
  /** 0 = no action. */
  readonly id: number
}

/** Levels the actions are set for, in wire order (se_level_e 1..4). */
export const ERROR_ACTION_LEVELS = [1, 2, 3, 4] as const

/**
 * The wire array (`uint8_t actions[5]`, sys_device.c): `[0]` holds scope
 * bits, bit n-1 set = level n runs a dynamic action; `[1..4]` are the action
 * IDs for Low, Medium, High, Critical.
 */
export const decodeErrorActions = (array: readonly number[]): ErrorAction[] =>
  ERROR_ACTION_LEVELS.map((level) => ({ scope: ((array[0] ?? 0) >> (level - 1)) & 1 ? 'dynamic' : 'static', id: array[level] ?? 0 }))

/**
 * The levels whose action can run under an importance (sys_device.c): an
 * error above the importance runs the importance level's action, critical
 * errors always run the Critical one, and Disabled (0) ignores the rest.
 * Disabled → Critical; Low → Low, Critical; Medium → Low, Medium, Critical; …
 */
export const reachableErrorLevels = (importance: number): number[] =>
  ERROR_ACTION_LEVELS.filter((level) => level <= importance || level === ERROR_ACTION_LEVELS.at(-1))

/** Actions for levels the importance can't reach cleared (they would never run). */
export const clampErrorActions = (array: readonly number[], importance: number): number[] => {
  const reachable = reachableErrorLevels(importance)
  return encodeErrorActions(decodeErrorActions(array).map((action, index) => (reachable.includes(ERROR_ACTION_LEVELS[index]!) ? action : { scope: 'static', id: 0 })))
}

export const encodeErrorActions = (actions: readonly ErrorAction[]): number[] => [
  actions.reduce((mask, action, index) => (action.scope === 'dynamic' && action.id ? mask | (1 << index) : mask), 0),
  ...ERROR_ACTION_LEVELS.map((_, index) => actions[index]?.id ?? 0),
]

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

/** Bytes one recorded frame takes in an action blob: `[u16 len][frame]`. */
const recordedSize = (frame: Uint8Array): number => 2 + frame.byteLength

export interface ActionBuild {
  readonly ok: boolean
  /** The contract calls, in order. */
  readonly frames: readonly UploadStep[]
  /** Recorded bytes against `limit` (CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE). */
  readonly bytes: number
  readonly limit: number
  readonly diagnostics: readonly UploadDiagnostic[]
}

/** The blob limit per dynamic action (sys_actions, 2 KB). */
export const ACTION_MAX_BYTES = 2048

export const describeStep = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], step: ActionStep): string => {
  const device = resolveDevice(catalog, devices, step.device)
  const contract = findContract(catalog, device, step.contract)
  const values = contract?.parameters
    .filter((parameter) => !parameter.instance && !parameter.deviceWide)
    .map((parameter) => {
      const value = parameterValue(parameter, step.values)
      if (Array.isArray(value)) return `${parameter.label} [${value.join(', ')}]`
      const choice = parameter.choices?.find((entry) => entry.value === value)
      return `${parameter.label} ${parameter.boolean ? (value ? 'on' : 'off') : choice && choice.label !== String(value) ? choice.label : `${value}${parameter.unit ? ` ${parameter.unit}` : ''}`}`
    })
    .join(', ')
  return `${device?.name ?? step.device}: ${contract?.label ?? step.contract}${values ? ` (${values})` : ''}`
}

export const buildAction = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], action: ProjectAction): ActionBuild => {
  const diagnostics: UploadDiagnostic[] = []
  const frames: UploadStep[] = []
  if (action.actionId < 1 || action.actionId > 255) diagnostics.push({ severity: 'error', message: `Action ID ${action.actionId} is outside 1..255.`, subjectId: action.id })
  for (const [index, step] of action.steps.entries()) {
    const device = resolveDevice(catalog, devices, step.device)
    const contract = findContract(catalog, device, step.contract)
    if (!device || !contract) {
      diagnostics.push({ severity: 'error', message: `Step ${index + 1}: ${device ? `${device.name} has no contract ${step.contract}` : `device ${step.device} is gone`}.`, subjectId: step.id })
      continue
    }
    try {
      frames.push({ label: describeStep(catalog, devices, step), frame: contractFrame(contract, device.deviceId, step.values) })
    } catch (error) {
      diagnostics.push({ severity: 'error', message: `Step ${index + 1}: ${error instanceof Error ? error.message : String(error)}`, subjectId: step.id })
    }
  }
  const bytes = frames.reduce((sum, step) => sum + recordedSize(step.frame), 0)
  if (bytes > ACTION_MAX_BYTES) diagnostics.push({ severity: 'error', message: `The action records ${bytes} bytes; the board keeps ${ACTION_MAX_BYTES} per action.`, subjectId: action.id })
  if (!action.steps.length) diagnostics.push({ severity: 'warning', message: 'The action has no steps.', subjectId: action.id })
  return { ok: !diagnostics.some((entry) => entry.severity === 'error'), frames, bytes, limit: ACTION_MAX_BYTES, diagnostics }
}

const actionCommand = (commands: CommandCatalog, name: string) => {
  const command = commands.get(`packet_sys_action_${name}_t`)
  if (!command) throw new Error(`The firmware descriptors have no action packet '${name}'.`)
  return command
}

/**
 * Frames that store the action on the board: record start, the calls, record
 * stop. The board runs the calls while recording them (F-GAP-4).
 */
export const actionRecordSteps = (commands: CommandCatalog, action: ProjectAction, build: ActionBuild): UploadStep[] => [
  { label: `action ${action.actionId} record start`, frame: packCommand(actionCommand(commands, 'record_start'), { id: action.actionId }) },
  ...build.frames,
  { label: `action ${action.actionId} record stop`, frame: packCommand(actionCommand(commands, 'record_stop'), {}) },
]

export const actionRunStep = (commands: CommandCatalog, actionId: number): UploadStep => ({ label: `action ${actionId} run`, frame: packCommand(actionCommand(commands, 'dynamic'), { id: actionId }) })

export const actionRemoveStep = (commands: CommandCatalog, actionId: number): UploadStep => ({ label: `action ${actionId} remove`, frame: packCommand(actionCommand(commands, 'remove'), { id: actionId }) })

export const nextActionId = (actions: readonly ProjectAction[]): number | undefined => {
  const taken = new Set(actions.map((action) => action.actionId))
  for (let id = 1; id <= 255; id++) if (!taken.has(id)) return id
  return undefined
}
