import { decodeFields, encodeFields, PayloadError, readValue, rawWidth } from '../catalog/codec'
import type { FieldRecord, FieldValue, FieldValues } from '../catalog/codec'
import type { CatalogEntry, CommandDef, Returns, ServiceCatalog } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'
import type { Request } from './frames'

/*
 * A request for any command of the catalog, and its answer decoded as the descriptor says. Pure: no link, so the same request
 * can be sent (`session/services.ts`) or stored in a record (`requestFrame`).
 *
 *   service command   [router key][op][args]                                 (ROUTER.MD)
 *   device command    [device key][device id u16][(contract << 8) | op u16][args]   (SYS_DEVICE.MD "Ops")
 *
 * A device command names its contract (`IO`, `DEVICE`, ...) or the device type whose own ops it is (`servo`).
 */

export interface ServiceCall {
  readonly request: Request
  readonly entry: CatalogEntry
  readonly command: CommandDef
  /** For the observer events: `SYS PING`, `device 3 IO SET_LEVEL`. */
  readonly label: string
}

const argsOf = (command: CommandDef, args: FieldValues | undefined): Uint8Array => {
  if (command.args) return encodeFields(command.args, args)
  if (args && Object.keys(args).length > 0) throw new PayloadError(`${command.id} takes no arguments.`)
  return new Uint8Array(0)
}

const concat = (...parts: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((sum, p) => sum + p.length, 0))
  let at = 0
  for (const part of parts) {
    out.set(part, at)
    at += part.length
  }
  return out
}

/** A command of a system service: `serviceCall(catalog, 'SYS', 'DEADMAN_SET', { timeout_ms: 500, on_link_lost: true })`. */
export const serviceCall = (catalog: ServiceCatalog, service: string, command: string, args?: FieldValues): ServiceCall => {
  const entry = catalog.service(service)
  const def = catalog.command(entry, command)
  return { request: { keys: [entry.key!, def.op], payload: argsOf(def, args), ...(def.urgent ? { urgent: true } : {}) }, entry, command: def, label: `${entry.id} ${def.id}` }
}

/** The u16 op of a device command: the contract in the high byte (a contract's id, or the own-ops id for a device type's own command). */
export const deviceOp = (catalog: ServiceCatalog, entry: CatalogEntry, command: CommandDef): number => {
  const contract = entry.kind === 'device' ? catalog.ownContractId : entry.contractId
  if (contract === undefined) throw new Error(`${entry.id} has no contract id to address its ops with.`)
  return ((contract << 8) | command.op) >>> 0
}

const deviceRequest = (defs: LinkDefinitions, device: number, op: number, payload: Uint8Array, urgent: boolean): Request => {
  if (!Number.isInteger(device) || device < 0 || device > 0xffff) throw new PayloadError(`Device ID ${device} is not a u16.`)
  return { keys: [defs.keys.device!], payload: concat(Uint8Array.of(device & 0xff, device >> 8, op & 0xff, op >> 8), payload), ...(urgent ? { urgent: true } : {}) }
}

/**
 * A command to device `device`: `deviceCall(defs, catalog, 3, 'IO', 'SET_LEVEL', { pin: 22, level: 'High' })`, or of a device type's
 * own ops: `deviceCall(defs, catalog, 9, 'servo', 'set', { value_x10: 900 })`.
 */
export const deviceCall = (defs: LinkDefinitions, catalog: ServiceCatalog, device: number, contract: string, command: string, args?: FieldValues): ServiceCall => {
  const entry = catalog.contracts.get(contract) ?? catalog.devices.get(contract)
  if (!entry) throw new Error(`No device contract or device type ${contract}.`)
  const def = catalog.command(entry, command)
  return { request: deviceRequest(defs, device, deviceOp(catalog, entry, def), argsOf(def, args), def.urgent), entry, command: def, label: `device ${device} ${entry.id} ${def.id}` }
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
  const typeField = create.args?.fields[0]
  if (!create.args || !typeField) throw new Error(`${lifecycle.id} ${CREATE} has no type field.`)
  const head = encodeFields(create.args, { [typeField.id]: deviceType.typeId! })
  if (!deviceType.config && config && Object.keys(config).length > 0) throw new PayloadError(`${deviceType.id} has no config.`)
  const body = deviceType.config ? encodeFields(deviceType.config, config) : new Uint8Array(0)
  return { request: deviceRequest(defs, device, deviceOp(catalog, lifecycle, create), concat(head, body), false), entry: lifecycle, command: create, label: `device ${device} ${lifecycle.id} ${CREATE} ${deviceType.id}` }
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

/** The decoded answer of a call; undefined when the command describes none. */
export const decodeAnswer = (call: Pick<ServiceCall, 'command' | 'label'>, data: Uint8Array): Decoded | undefined =>
  call.command.returns ? decodeReturns(call.command.returns, data, call.label) : undefined
