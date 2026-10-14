import { enumValue } from '../../backend/catalog'
import { configValueOk, defaultConfig, decodeConfig } from '../settings'
import { BOARD_DEVICES, DEVICE_TYPES, deviceType } from './catalog'
import type { DeviceType } from './catalog'
import { commandFrame, createFrame, decodeCreateFrame, deviceCommands, sweepCommands } from './commands'
import type { DeviceCommand } from './commands'
import { defaultInstall, NO_DEVICE } from './project'
import type { ProjectDevice } from './project'

/*
 * Default settings and actions are the same thing: a list of device commands the board keeps as a record (SYS_ACTIONS.MD).
 *   - a device's record (`DEVICE` class, its device ID): the CREATE of a user device, then its default settings (the board's own devices
 *     are created by the firmware, so their record holds only the default settings);
 *   - an action (`USER` class, the action ID the project chooses): any commands of any devices, in order.
 * Nothing replays by itself; a record changes the board when it is run (by the project, or by hand).
 */

/** One command in a list: the device ID, the op (its C name) and the values of its arguments. */
export interface ActionStep {
  readonly id: string
  /** The device ID the command is sent to. */
  readonly device: number
  readonly op: string
  readonly values: Readonly<Record<string, number>>
}

export interface ProjectAction {
  readonly id: string
  readonly name: string
  /** The board's record ID (class USER, this index). */
  readonly actionId: number
  readonly steps: readonly ActionStep[]
}

/** The frames a record holds, to be stored under `id`; no frames = the record is removed. */
export interface RecordImage {
  readonly label: string
  readonly id: number
  readonly frames: readonly Uint8Array[]
}

/** What a record holds, from the project's side. */
export interface StoredDeviceState {
  readonly devices: readonly ProjectDevice[]
  /** The default settings of every device (board's and user's), by device ID in each step. */
  readonly setup: readonly ActionStep[]
  readonly actions: readonly ProjectAction[]
  /** The action IDs this project has put on the board (or read from it): the only user records a save may remove. */
  readonly onBoard: readonly number[]
}

const recordClass = (name: string): number => enumValue('action_class', name)
const recordId = (cls: number, index: number): number => ((cls << 24) | (index & 0xffffff)) >>> 0
const classOf = (id: number): number => id >>> 24

/** The record of a device: its install and default settings. */
export const deviceRecordId = (deviceId: number): number => recordId(recordClass('SYS_ACTIONS_CLASS_DEVICE'), deviceId & 0xffff)
/** The record of an action. */
export const userActionId = (actionId: number): number => recordId(recordClass('SYS_ACTIONS_CLASS_USER'), actionId)

/** The type of the device with this device ID, the board's or the project's. */
export const resolveType = (deviceId: number, devices: readonly ProjectDevice[]): DeviceType | undefined =>
  BOARD_DEVICES.find((device) => device.id === deviceId)?.type ?? deviceType(devices.find((device) => device.deviceId === deviceId)?.type)

/** What a device ID can be sent: the commands of its type, or for "no device" (every device) the sweeps. */
export const commandsOf = (deviceId: number, devices: readonly ProjectDevice[]): readonly DeviceCommand[] =>
  deviceId === NO_DEVICE ? sweepCommands() : deviceCommands(resolveType(deviceId, devices))

export const stepCommand = (step: ActionStep, devices: readonly ProjectDevice[]): DeviceCommand | undefined =>
  commandsOf(step.device, devices).find((command) => command.symbol === step.op)

const uid = (): string => `st-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

export const newStep = (device: number, command: DeviceCommand): ActionStep => ({ id: uid(), device, op: command.symbol, values: defaultConfig(command.op) })

export const nextActionId = (actions: readonly ProjectAction[]): number => Math.max(0, ...actions.map((action) => action.actionId)) + 1

export const newAction = (actions: readonly ProjectAction[]): ProjectAction => {
  const actionId = nextActionId(actions)
  return { id: `act-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`, name: `Action ${actionId}`, actionId, steps: [] }
}

/** The frame of a step; none when the device or the command is not known. */
export const stepFrame = (step: ActionStep, devices: readonly ProjectDevice[]): Uint8Array | undefined => {
  const command = stepCommand(step, devices)
  return command ? commandFrame(step.device, command, { ...defaultConfig(command.op), ...step.values }) : undefined
}

/** What is wrong with a step: a device the project lacks, a command the device does not have, a value the field refuses. */
export const stepProblems = (step: ActionStep, devices: readonly ProjectDevice[]): string[] => {
  const type = resolveType(step.device, devices)
  if (!type && step.device !== NO_DEVICE) return [`Device ${step.device} is not in the project.`]
  const command = commandsOf(step.device, devices).find((entry) => entry.symbol === step.op)
  if (!command) return [`${step.op} is not a command of ${type?.alias ?? 'the devices'}.`]
  return command.op.fields
    .filter((field) => !field.hidden && step.values[field.name] !== undefined && !configValueOk(field, step.values[field.name]))
    .map((field) => `${command.alias}: ${field.alias} ${step.values[field.name]} is not a value it takes.`)
}

export interface ActionProblem {
  /** The action's project id, or the user device's, whose list it is. */
  readonly subject: string
  readonly message: string
}

/** What is wrong with the actions and the default settings: a shared or zero action ID, a step that cannot be sent. */
export const actionProblems = (state: Pick<StoredDeviceState, 'devices' | 'setup' | 'actions'>): ActionProblem[] => {
  const problems: ActionProblem[] = []
  for (const action of state.actions) {
    if (action.actionId < 1) problems.push({ subject: action.id, message: 'The action ID must be 1 or more.' })
    const twin = state.actions.find((other) => other !== action && other.actionId === action.actionId)
    if (twin) problems.push({ subject: action.id, message: `Action ID ${action.actionId} is also ${twin.name}'s.` })
    for (const step of action.steps) for (const message of stepProblems(step, state.devices)) problems.push({ subject: action.id, message })
  }
  for (const step of state.setup) for (const message of stepProblems(step, state.devices)) problems.push({ subject: `setup:${step.device}`, message })
  return problems
}

const framesOf = (steps: readonly ActionStep[], devices: readonly ProjectDevice[]): Uint8Array[] =>
  steps.flatMap((step) => {
    const frame = stepFrame(step, devices)
    return frame ? [frame] : []
  })

/**
 * The records the project stands for: each device's (CREATE of a user device, then its default settings) and each action's.
 * With `stored` (the ids the board lists) the records the project no longer has are listed empty, so the save removes them: the
 * device records all, and an action only when this project put it on the board (the firmware keeps some user actions of its own).
 */
export const recordImages = (state: StoredDeviceState, stored?: ReadonlySet<number>): RecordImage[] => {
  const images: RecordImage[] = []
  const wanted = new Set<number>()
  const add = (label: string, id: number, frames: Uint8Array[]) => {
    if (frames.length) wanted.add(id)
    images.push({ label, id, frames })
  }
  for (const device of BOARD_DEVICES) {
    const frames = framesOf(state.setup.filter((step) => step.device === device.id), state.devices)
    if (frames.length) add(`${device.alias} defaults`, deviceRecordId(device.id), frames)
  }
  for (const device of state.devices) {
    const type = deviceType(device.type)
    if (!type?.create) continue
    add(device.name, deviceRecordId(device.deviceId), [createFrame(device, type), ...framesOf(state.setup.filter((step) => step.device === device.deviceId), state.devices)])
  }
  for (const action of state.actions) add(action.name, userActionId(action.actionId), framesOf(action.steps, state.devices))
  for (const id of stored ?? []) {
    if (wanted.has(id) || images.some((image) => image.id === id)) continue
    const mine = classOf(id) === recordClass('SYS_ACTIONS_CLASS_DEVICE') || (classOf(id) === recordClass('SYS_ACTIONS_CLASS_USER') && state.onBoard.includes(id))
    if (mine) images.push({ label: `record ${id.toString(16)}`, id, frames: [] })
  }
  return images
}

/** The action IDs of the project that the board holds and this project did not put there: a save would replace them. */
export const foreignActions = (state: StoredDeviceState, stored: ReadonlySet<number>): number[] =>
  state.actions.map((action) => userActionId(action.actionId)).filter((id) => stored.has(id) && !state.onBoard.includes(id))

/** The records read from the board put in the project's form; names, tags and descriptions the board does not keep come from `known`. */
export const decodeRecords = (records: ReadonlyMap<number, readonly Uint8Array[]>, known: Pick<StoredDeviceState, 'devices' | 'actions'>): Pick<StoredDeviceState, 'devices' | 'setup' | 'actions' | 'onBoard'> => {
  const deviceClass = recordClass('SYS_ACTIONS_CLASS_DEVICE')
  const userClass = recordClass('SYS_ACTIONS_CLASS_USER')
  const devices = [...known.devices]
  const setup: ActionStep[] = []
  const actions: ProjectAction[] = []
  const step = (frame: Uint8Array, deviceId: number, using: readonly ProjectDevice[]): ActionStep | undefined => {
    const op = frame[3]! | (frame[4]! << 8)
    const command = commandsOf(deviceId, using).find((entry) => entry.op.opId === op)
    if (!command || frame.length < 5) return undefined
    return { id: uid(), device: deviceId, op: command.symbol, values: { ...defaultConfig(command.op), ...decodeConfig(command.op, frame.subarray(5)) } }
  }
  const frameDevice = (frame: Uint8Array): number => frame[1]! | (frame[2]! << 8)
  const isCommand = (frame: Uint8Array): boolean => frame.length >= 5 && frame[0] === enumValue('router_key', 'ROUTER_KEY_DEVICE')

  // devices first, so the commands of every record find their types
  for (const [id, frames] of records) {
    if (classOf(id) !== deviceClass) continue
    for (const frame of frames) {
      const made = decodeCreateFrame(frame, DEVICE_TYPES)
      if (!made) continue
      const at = devices.findIndex((device) => device.deviceId === made.deviceId)
      const old = at >= 0 ? devices[at] : undefined
      const device: ProjectDevice = {
        id: old?.id ?? `dev-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
        type: made.type.id,
        deviceId: made.deviceId,
        name: old?.name ?? made.type.alias,
        ...(old?.description ? { description: old.description } : {}),
        tags: old?.tags ?? [],
        install: { ...defaultInstall(made.type), ...made.install },
      }
      if (at >= 0) devices[at] = device
      else devices.push(device)
    }
  }
  for (const [id, frames] of records) {
    if (classOf(id) === deviceClass) {
      for (const frame of frames) {
        if (!isCommand(frame) || decodeCreateFrame(frame, DEVICE_TYPES)) continue
        const made = step(frame, frameDevice(frame), devices)
        if (made) setup.push(made)
      }
    } else if (classOf(id) === userClass) {
      const actionId = id & 0xffffff
      const old = known.actions.find((action) => action.actionId === actionId)
      actions.push({
        id: old?.id ?? `act-${actionId}`,
        name: old?.name ?? `Action ${actionId}`,
        actionId,
        steps: frames.filter(isCommand).flatMap((frame) => {
          const made = step(frame, frameDevice(frame), devices)
          return made ? [made] : []
        }),
      })
    }
  }
  actions.sort((a, b) => a.actionId - b.actionId)
  return { devices, setup, actions, onBoard: actions.map((action) => userActionId(action.actionId)) }
}

const isRecord = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value)

/** Saved steps: what can be read is kept (a step with a missing value takes the field's default when it is sent). */
export const stepsFromJson = (value: unknown): ActionStep[] => {
  if (!Array.isArray(value)) return []
  return value.flatMap((raw): ActionStep[] => {
    if (!isRecord(raw) || typeof raw.device !== 'number' || typeof raw.op !== 'string') return []
    const values = isRecord(raw.values) ? Object.fromEntries(Object.entries(raw.values).filter((entry): entry is [string, number] => typeof entry[1] === 'number')) : {}
    return [{ id: typeof raw.id === 'string' ? raw.id : uid(), device: raw.device, op: raw.op, values }]
  })
}

export const actionsFromJson = (value: unknown): ProjectAction[] => {
  if (!Array.isArray(value)) return []
  return value.flatMap((raw): ProjectAction[] => {
    if (!isRecord(raw) || typeof raw.id !== 'string' || typeof raw.actionId !== 'number') return []
    return [{ id: raw.id, name: typeof raw.name === 'string' ? raw.name : `Action ${raw.actionId}`, actionId: raw.actionId, steps: stepsFromJson(raw.steps) }]
  })
}
