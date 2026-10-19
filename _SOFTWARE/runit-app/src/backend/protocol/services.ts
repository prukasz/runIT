import { decodeFields, encodeFields, PayloadError, readValue, rawWidth } from '../catalog/codec'
import type { FieldRecord, FieldValue, FieldValues, Layout } from '../catalog/codec'
import { sideOf } from '../catalog/services'
import type { CatalogEntry, CommandDef, Returns, ServiceCatalog, Side } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'
import type { Request } from './frames'

/*
 * A request for any command of the catalog, and its answer decoded as the descriptor says. Pure: no link, so the same request
 * can be sent (`session/services.ts`) or stored in a record (`requestFrame`).
 *
 *   service command   [router key][dir][op][args]                                 (ROUTER.MD)
 *   device command    [device key][dir][device id u16][(contract << 8) | op u16][args]   (SYS_DEVICE.MD "Ops")
 *
 * `dir` is the side of the command (`cmd_dir_e`): its read or its write. A command with one side runs it; one with both (the
 * mirror: the read answers what the write takes) is told which. A device command names its contract (`IO`, `DEVICE`, ...) or the
 * device type whose own ops it is (`servo`).
 */

export interface ServiceCall {
  readonly request: Request
  readonly entry: CatalogEntry
  readonly command: CommandDef
  /** The side it runs. */
  readonly side: Side
  /** For the observer events: `SYS PING`, `device 3 IO LEVEL write`. */
  readonly label: string
}

/** The arguments of a call: a struct by field id, or the rows of a side whose arguments are rows (`array`). */
export type CallArgs = FieldValues | readonly FieldValues[]

const argsOf = (command: CommandDef, side: Side, args: CallArgs | undefined): Uint8Array => {
  const layout: Layout | undefined = command[side]?.args
  if (layout?.array) {
    if (args !== undefined && !Array.isArray(args)) throw new PayloadError(`${command.id} ${side} takes rows.`)
    return concat(...((args as readonly FieldValues[] | undefined) ?? []).map((row) => encodeFields(layout, row)))
  }
  if (Array.isArray(args)) throw new PayloadError(`${command.id} ${side} takes one struct, not rows.`)
  const values = args as FieldValues | undefined
  if (layout) return encodeFields(layout, values)
  if (values && Object.keys(values).length > 0) throw new PayloadError(`${command.id} ${side} takes no arguments.`)
  return new Uint8Array(0)
}

/** `LEVEL write` for a command with both sides, its bare id otherwise. */
const named = (command: CommandDef, side: Side): string => (command.read && command.write ? `${command.id} ${side}` : command.id)

const concat = (...parts: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((sum, p) => sum + p.length, 0))
  let at = 0
  for (const part of parts) {
    out.set(part, at)
    at += part.length
  }
  return out
}

/**
 * A command of a system service: `serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: 500, on_link_lost: true }, 'write')`. `side`
 * may be left out for a command with one side.
 */
export const serviceCall = (catalog: ServiceCatalog, service: string, command: string, args?: CallArgs, side?: Side): ServiceCall => {
  const entry = catalog.service(service)
  const def = catalog.command(entry, command)
  const s = sideOf(def, side)
  return { request: { keys: [entry.key!, catalog.dirs[s], def.op], payload: argsOf(def, s, args), ...(def.urgent ? { urgent: true } : {}) }, entry, command: def, side: s, label: `${entry.id} ${named(def, s)}` }
}

/** The u16 op of a device command: the contract in the high byte (a contract's id, or the own-ops id for a device type's own command). */
export const deviceOp = (catalog: ServiceCatalog, entry: CatalogEntry, command: CommandDef): number => {
  const contract = entry.kind === 'device' ? catalog.ownContractId : entry.contractId
  if (contract === undefined) throw new Error(`${entry.id} has no contract id to address its ops with.`)
  return ((contract << 8) | command.op) >>> 0
}

const deviceRequest = (defs: LinkDefinitions, dir: number, device: number, op: number, payload: Uint8Array, urgent: boolean): Request => {
  if (!Number.isInteger(device) || device < 0 || device > 0xffff) throw new PayloadError(`Device ID ${device} is not a u16.`)
  return { keys: [defs.keys.device!, dir], payload: concat(Uint8Array.of(device & 0xff, device >> 8, op & 0xff, op >> 8), payload), ...(urgent ? { urgent: true } : {}) }
}

/**
 * A command to device `device`: `deviceCall(defs, catalog, 3, 'IO', 'LEVEL', { pin: 22, level: 'High' }, 'write')`, or of a device
 * type's own ops: `deviceCall(defs, catalog, 9, 'servo', 'target', { value_x10: 900 })`. `side` may be left out for a command with
 * one side.
 */
export const deviceCall = (defs: LinkDefinitions, catalog: ServiceCatalog, device: number, contract: string, command: string, args?: CallArgs, side?: Side): ServiceCall => {
  const entry = catalog.contracts.get(contract) ?? catalog.devices.get(contract)
  if (!entry) throw new Error(`No device contract or device type ${contract}.`)
  const def = catalog.command(entry, command)
  const s = sideOf(def, side)
  return { request: deviceRequest(defs, catalog.dirs[s], device, deviceOp(catalog, entry, def), argsOf(def, s, args), def.urgent), entry, command: def, side: s, label: `device ${device} ${entry.id} ${named(def, s)}` }
}

/** The command of the device contract that creates a device (SYS_DEVICE.MD "Ops"). */
const CREATE = 'CREATE'

/**
 * CREATE of a device of type `type` (a device descriptor's id or type id) under `device`: the generic command of the device
 * contract with the type, then the type's config struct (SYS_DEVICE.MD: `[type u16][create struct]`).
 */
export const createDeviceCall = (defs: LinkDefinitions, catalog: ServiceCatalog, device: number, type: string | number, config?: FieldValues): ServiceCall => {
  const deviceType = catalog.device(type)
  const lifecycle = catalog.lifecycleContractId === undefined ? undefined : catalog.byContractId.get(catalog.lifecycleContractId)
  if (!lifecycle) throw new Error('No descriptor of the device contract (SYS_DEVICE_CONTRACT_DEVICE): devices cannot be created.')
  const create = catalog.command(lifecycle, CREATE)
  const createArgs = create.write?.args
  const typeField = createArgs?.fields[0]
  if (!createArgs || !typeField) throw new Error(`${lifecycle.id} ${CREATE} has no write with a type field.`)
  const head = encodeFields(createArgs, { [typeField.id]: deviceType.typeId! })
  if (!deviceType.config && config && Object.keys(config).length > 0) throw new PayloadError(`${deviceType.id} has no config.`)
  const body = deviceType.config ? encodeFields(deviceType.config, config) : new Uint8Array(0)
  return { request: deviceRequest(defs, catalog.dirs.write, device, deviceOp(catalog, lifecycle, create), concat(head, body), false), entry: lifecycle, command: create, side: 'write', label: `device ${device} ${lifecycle.id} ${CREATE} ${deviceType.id}` }
}

/** A device frame read back: the device, the command, its side and its arguments; a CREATE also gives the type and its config. */
export interface DeviceFrame {
  readonly device: number
  readonly entry: CatalogEntry
  readonly command: CommandDef
  readonly side: Side
  readonly args?: FieldRecord
  readonly create?: { readonly type: CatalogEntry; readonly config?: FieldRecord }
}

/**
 * A frame of a record (`[device key][dir][id u16][op u16][args]`, no seq or flags) back into the command it is, by the catalog: the
 * mirror of `deviceCall` / `createDeviceCall`. A device type's own op is read with the type `typeOf` gives for the device (the
 * op alone does not say whose it is). Undefined for a frame that is not a device command the catalog describes, or whose
 * arguments do not fit its layout.
 */
export const readDeviceFrame = (defs: LinkDefinitions, catalog: ServiceCatalog, frame: Uint8Array, typeOf: (device: number) => CatalogEntry | undefined = () => undefined): DeviceFrame | undefined => {
  if (frame.length < 6 || frame[0] !== defs.keys.device) return undefined
  const side = catalog.sideOfDir(frame[1]!)
  const device = frame[2]! | (frame[3]! << 8)
  const op = frame[4]! | (frame[5]! << 8)
  const contract = op >> 8
  const payload = frame.subarray(6)
  const entry = contract === catalog.ownContractId ? typeOf(device) : catalog.byContractId.get(contract)
  const command = entry && [...entry.commands.values()].find((c) => c.op === (op & 0xff))
  if (!side || !entry || !command || !command[side]) return undefined
  const args = command[side]!.args
  try {
    if (contract === catalog.lifecycleContractId && command.id === CREATE && args) {
      const head = decodeFields(args, payload)
      const type = catalog.byTypeId.get(Number(head[args.fields[0]!.id]))
      if (!type) return undefined
      const body = payload.subarray(args.fixed)
      return { device, entry, command, side, create: { type, ...(type.config ? { config: decodeFields(type.config, body) } : {}) } }
    }
    return { device, entry, command, side, ...(args ? { args: decodeFields(args, payload) } : {}) }
  } catch (error) {
    if (error instanceof PayloadError) return undefined
    throw error
  }
}

/** A request as the router takes it from a record: `[keys…][payload]`, no seq or flags (SYS_ACTIONS.MD). */
export const requestFrame = (request: Request): Uint8Array => concat(Uint8Array.from(request.keys), request.payload ?? new Uint8Array(0))

/* ---- answers ---- */

export type Decoded = FieldValue | FieldRecord | readonly FieldValue[] | readonly FieldRecord[]

const chunks = (data: Uint8Array, size: number, what: string): Uint8Array[] => {
  if (size <= 0) throw new PayloadError(`${what}: an element of no size.`)
  if (data.length % size !== 0) throw new PayloadError(`${what}: ${data.length} bytes are not whole elements of ${size}.`)
  return Array.from({ length: data.length / size }, (_, i) => data.subarray(i * size, (i + 1) * size))
}

/**
 * The answer as its descriptor describes it: a struct by field id, a value, or a list of either. A value that only leads the
 * answer (the rest described in words, such as a count followed by records) is read from the front; the bytes stay in `data`.
 */
export const decodeReturns = (returns: Returns, data: Uint8Array, what = 'answer'): Decoded => {
  if (returns.kind === 'struct') {
    if (!returns.array) return decodeFields(returns.layout, data)
    if (returns.layout.variable) throw new PayloadError(`${what}: a list of structs that end in a field of any length.`)
    return chunks(data, returns.layout.fixed, what).map((c) => decodeFields(returns.layout, c))
  }
  const width = rawWidth(returns.rawType)
  if (!returns.array || width === undefined) return readValue(data, 0, returns.rawType, width, returns.boolean)
  return chunks(data, width, what).map((c) => readValue(c, 0, returns.rawType, width, returns.boolean))
}

/** The decoded answer of a call; undefined when its side describes none. */
export const decodeAnswer = (call: Pick<ServiceCall, 'command' | 'side' | 'label'>, data: Uint8Array): Decoded | undefined => {
  const returns = call.command[call.side]?.returns
  return returns ? decodeReturns(returns, data, call.label) : undefined
}
