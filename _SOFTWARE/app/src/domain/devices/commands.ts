import { routerKey } from '../../backend/catalog'
import { deviceContractOp, deviceOpConfig, encodeConfig } from '../settings'
import type { ConfigField, ConfigOp, ConfigValues, RawDeviceOp } from '../settings'
import type { DeviceField, DeviceParam, DeviceType } from './catalog'
import { defaultInstall } from './project'
import type { ProjectDevice } from './project'

/*
 * What a device type can be told to do, read from the descriptors: the ops of its contracts (their arguments are in the contracts file,
 * narrowed by the type's own parameters: the pins it has, its limits) and its own ops. A command is the same thing in a device's default
 * settings, in an action and in a live command: a device ID, an op and the values of the op's arguments, sent as a `ROUTER_KEY_DEVICE`
 * frame `[key][device id u16][op u16][arguments]`.
 */

export interface DeviceCommand {
  /** The op's C name (`IO_OP_SET_LEVEL`). */
  readonly symbol: string
  /** The contract's name, or the driver's own commands. */
  readonly group: string
  readonly alias: string
  readonly description?: string
  /** The arguments as a form, with what the device type narrows. */
  readonly op: ConfigOp
  /** The command answers with data. */
  readonly returns: boolean
  /** Whether a default setting or an action can hold it: it takes only plain numbers and reads nothing back. */
  readonly stored: boolean
  readonly rawReturns?: unknown
}

const OWN_GROUP = 'Device-specific'

/** The argument as the device type narrows it: its name, limits, the values a pin may take. */
const narrowed = (field: ConfigField, param: DeviceParam | undefined): ConfigField => {
  if (!param) return field
  const alias = param.alias ?? field.alias
  return {
    ...field,
    alias,
    ...(param.note ? { description: param.note } : {}),
    ...(param.min !== undefined ? { min: param.min } : {}),
    ...(param.max !== undefined ? { max: param.max } : {}),
    ...(param.default !== undefined ? { default: param.default } : {}),
    ...(param.one_of ? { choices: param.one_of.map((value) => ({ value, alias: `${alias} ${value}` })) } : {}),
  }
}

const command = (raw: RawDeviceOp, group: string, params: readonly DeviceParam[] | undefined): DeviceCommand => {
  const config = deviceOpConfig(raw)
  const op: ConfigOp = { ...config, fields: config.fields.map((field) => narrowed(field, params?.find((param) => param.field === field.name))) }
  const returns = raw.returns !== undefined
  return { symbol: raw.symbol, group, alias: raw.alias, ...(raw.description ? { description: raw.description } : {}), op, returns, stored: !returns && op.skipped.length === 0, rawReturns: raw.returns }
}

const cache = new Map<string, readonly DeviceCommand[]>()

/** The commands of a device type, its contracts' first, then its own. */
export const deviceCommands = (type: DeviceType | undefined): readonly DeviceCommand[] => {
  if (!type) return []
  const known = cache.get(type.id)
  if (known) return known
  const list: DeviceCommand[] = []
  for (const contract of type.contracts) {
    for (const op of contract.ops) {
      const raw = deviceContractOp(contract.contract, op.symbol)
      if (raw) list.push(command(raw, contract.alias, op.params))
    }
  }
  for (const op of type.own_ops as unknown as readonly RawDeviceOp[]) list.push(command(op, OWN_GROUP, undefined))
  cache.set(type.id, list)
  return list
}

let sweeps: readonly DeviceCommand[] | undefined

/** SUSPEND and RESUME sent to the device ID "none" reach every device (SYS_DEVICE.MD): the commands an action may send to all of them. */
export const sweepCommands = (): readonly DeviceCommand[] => {
  sweeps ??= ['SYS_DEVICE_OP_SUSPEND', 'SYS_DEVICE_OP_RESUME'].flatMap((symbol) => {
    const raw = deviceContractOp('DEVICE', symbol)
    return raw ? [command(raw, 'All devices', undefined)] : []
  })
  return sweeps
}

const u16 = (value: number): number[] => [value & 0xff, (value >> 8) & 0xff]

/** The frame of one command: the device router's key, the device, the op, the arguments. */
export const commandFrame = (deviceId: number, command_: DeviceCommand, values: ConfigValues): Uint8Array =>
  Uint8Array.of(routerKey('ROUTER_KEY_DEVICE'), ...u16(deviceId), ...u16(command_.op.opId), ...encodeConfig(command_.op, values))

const create = () => {
  const raw = deviceContractOp('DEVICE', 'SYS_DEVICE_OP_CREATE')
  if (!raw) throw new Error('The firmware publishes no SYS_DEVICE_OP_CREATE (data-structures/contracts/contracts.generated.json).')
  return raw
}

/** Calls `visit` for every number of a create struct: its key (see `ProjectDevice.install`), where it is and its field. */
const walk = (fields: readonly DeviceField[], base: number, prefix: string, visit: (key: string, at: number, field: DeviceField) => void) => {
  for (const field of fields) {
    const count = field.array_len ?? 0
    const elements = count || 1
    const size = field.size / elements
    for (let index = 0; index < elements; index++) {
      const key = `${prefix}${field.name}${count ? `[${index}]` : ''}`
      const at = base + field.offset + index * size
      if (field.fields?.length) walk(field.fields, at, `${key}.`, visit)
      else visit(key, at, field)
    }
  }
}

/** The create struct of a device, packed. */
export const encodeInstall = (type: DeviceType, install: Readonly<Record<string, number>>): Uint8Array => {
  const bytes = new Uint8Array(type.create?.size ?? 0)
  const values = { ...defaultInstall(type), ...install }
  walk(type.create?.fields ?? [], 0, '', (key, at, field) => {
    const value = values[key] ?? 0
    const width = Math.min(field.size, 4)
    for (let index = 0; index < width; index++) bytes[at + index] = Math.floor(value / 2 ** (8 * index)) & 0xff
  })
  return bytes
}

/** The numbers of a packed create struct, by key. */
export const decodeInstall = (type: DeviceType, bytes: Uint8Array): Record<string, number> => {
  const out: Record<string, number> = {}
  walk(type.create?.fields ?? [], 0, '', (key, at, field) => {
    const width = Math.min(field.size, 4)
    if (at + width > bytes.length) return
    let value = 0
    for (let index = 0; index < width; index++) value += bytes[at + index]! * 2 ** (8 * index)
    if (field.type.startsWith('i') && value >= 2 ** (width * 8 - 1)) value -= 2 ** (width * 8)
    out[key] = value
  })
  return out
}

/** The CREATE frame of a user device: `[key][device id][CREATE][type u16][create struct]`. */
export const createFrame = (device: ProjectDevice, type: DeviceType): Uint8Array =>
  Uint8Array.of(routerKey('ROUTER_KEY_DEVICE'), ...u16(device.deviceId), ...u16(create().id), ...u16(type.type.id), ...encodeInstall(type, device.install))

/** A CREATE frame back into the device it installs (its type, ID and values); the project's name and tags are not on the board. */
export const decodeCreateFrame = (frame: Uint8Array, types: readonly DeviceType[]): { readonly deviceId: number; readonly type: DeviceType; readonly install: Record<string, number> } | undefined => {
  if (frame[0] !== routerKey('ROUTER_KEY_DEVICE') || frame.length < 7) return undefined
  if ((frame[3]! | (frame[4]! << 8)) !== create().id) return undefined
  const typeId = frame[5]! | (frame[6]! << 8)
  const type = types.find((entry) => entry.type.id === typeId)
  if (!type) return undefined
  return { deviceId: frame[1]! | (frame[2]! << 8), type, install: decodeInstall(type, frame.subarray(7)) }
}
