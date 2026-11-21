import { decodeFields, encodeFields, PayloadError, readValue, rawWidth } from '../catalog/codec'
import type { FieldRecord, FieldValue, FieldValues, Layout } from '../catalog/codec'
import { sideOf } from '../catalog/services'
import type { CatalogEntry, CommandDef, Returns, ServiceCatalog, Side } from '../catalog/services'
import type { Request } from './frame'

/*
 * The request of any command of the catalog, and its answer decoded as its descriptor says. Pure: no link, so the same request
 * is sent (`session/services.ts`), stored in a record or added as a watch's read (`requestFrame`). The key is the header's; the
 * body is what the router's module takes after it:
 *
 *   service                 key = its router key     body [dir][op][args]                     (ROUTER.MD)
 *   service with an id      key = its router key     body [dir][id u8][(contract << 8) | op u16][args]   (PROVIDER)
 *   device command          key = the device router  body [dir][device u8][(contract << 8) | op u16][args]   (SYS_DEVICE.MD)
 *
 * `dir` is the side (`cmd_dir_e`): a command with one side runs it, one with both (the mirror) is told which. A device command
 * names its contract (`IO`, `DEVICE`, ...) or the device type whose own ops it is (`servo`). Every number is the catalog's.
 */

export interface ServiceCall {
  readonly request: Request
  readonly entry: CatalogEntry
  readonly command: CommandDef
  /** The side it runs. */
  readonly side: Side
  /** For the observer events and messages: `SYS PING`, `device 3 IO LEVEL write`. */
  readonly label: string
}

/** The arguments of a call: a struct by field id, or the rows of a side whose arguments are rows (`array`). */
export type CallArgs = FieldValues | readonly FieldValues[]

const concat = (...parts: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((sum, p) => sum + p.length, 0))
  let at = 0
  for (const part of parts) {
    out.set(part, at)
    at += part.length
  }
  return out
}

const u8 = (n: number, what: string): number => {
  if (!Number.isInteger(n) || n < 0 || n > 0xff) throw new PayloadError(`${what} ${n} is not a u8.`)
  return n
}

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

/** The u16 op of an addressed command: the contract in the high byte (a contract's id, or the own-ops id for a device type's own command). */
export const deviceOp = (catalog: ServiceCatalog, entry: CatalogEntry, command: CommandDef): number => {
  const contract = entry.kind === 'device' ? catalog.ownContractId : entry.contractId
  if (contract === undefined) throw new Error(`${entry.id} has no contract id to address its ops with.`)
  return ((contract << 8) | command.op) >>> 0
}

/** `[dir][target u8][op u16 LE][args]`: the body of an addressed command (a device's, a provider's). */
const addressed = (dir: number, target: number, op: number, args: Uint8Array): Uint8Array => concat(Uint8Array.of(dir, target, op & 0xff, op >> 8), args)

/**
 * A command of a system service: `serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: 500 }, 'write')`. `side` may be left out
 * for a command with one side. A service addressed by an id (one with a contract id: PROVIDER) takes `id`, else the id of none
 * (`SYS_DATA_PROVIDER_ID_NONE`: LIST).
 */
export const serviceCall = (catalog: ServiceCatalog, service: string, command: string, args?: CallArgs, side?: Side, id?: number): ServiceCall => {
  const entry = catalog.service(service)
  const def = catalog.command(entry, command)
  const s = sideOf(def, side)
  const urgent = def.urgent ? { urgent: true } : {}
  const label = `${entry.id} ${named(def, s)}`
  if (entry.contractId === undefined) return { request: { key: entry.key!, body: concat(Uint8Array.of(catalog.dirs[s], def.op), argsOf(def, s, args)), ...urgent }, entry, command: def, side: s, label }
  const target = id ?? catalog.noProvider
  if (target === undefined) throw new Error(`${entry.id} is addressed by an id, and the id of none is not published (provider_id).`)
  return { request: { key: entry.key!, body: addressed(catalog.dirs[s], u8(target, `${entry.id}: id`), deviceOp(catalog, entry, def), argsOf(def, s, args)), ...urgent }, entry, command: def, side: s, label: `${label}${id === undefined ? '' : ` ${id}`}` }
}

const deviceKey = (catalog: ServiceCatalog): number => {
  const lifecycle = lifecycleOf(catalog)
  return lifecycle.key!
}

/** The device contract every device answers (CREATE, INFO, OPS, ...): the one that is also the device router's key. */
const lifecycleOf = (catalog: ServiceCatalog): CatalogEntry => {
  const entry = catalog.lifecycleContractId === undefined ? undefined : catalog.byContractId.get(catalog.lifecycleContractId)
  if (!entry || entry.key === undefined) throw new Error('No descriptor of the device contract (a device contract with a router key): no device can be addressed.')
  return entry
}

/**
 * A command to device `device`: `deviceCall(catalog, 3, 'IO', 'LEVEL', { pin: 22, level: 'High' }, 'write')`, or of a device
 * type's own ops: `deviceCall(catalog, 9, 'servo', 'target', { value_x10: 900 })`. `side` may be left out for a command with
 * one side.
 */
export const deviceCall = (catalog: ServiceCatalog, device: number, contract: string, command: string, args?: CallArgs, side?: Side): ServiceCall => {
  const entry = catalog.contracts.get(contract) ?? catalog.devices.get(contract)
  if (!entry) throw new Error(`No device contract or device type ${contract}.`)
  const def = catalog.command(entry, command)
  const s = sideOf(def, side)
  const body = addressed(catalog.dirs[s], u8(device, 'Device ID'), deviceOp(catalog, entry, def), argsOf(def, s, args))
  return { request: { key: deviceKey(catalog), body, ...(def.urgent ? { urgent: true } : {}) }, entry, command: def, side: s, label: `device ${device} ${entry.id} ${named(def, s)}` }
}

/** The device contract's command that creates a device (SYS_DEVICE.MD "Ops"). */
const CREATE = 'CREATE'
/** The device contract's command that makes a device of another board here (SYS_REMOTE.MD). */
const CREATE_REMOTE = 'CREATE_REMOTE'

/**
 * CREATE of a device of type `type` (a device descriptor's id or type id) under `device`: the device contract's write, whose
 * fields are the type, the flags and the type's config struct (SYS_DEVICE.MD: `[type u16][flags u8][config]`). `system`: the
 * device is the board's own (`SYS_DEVICE_CREATE_SYSTEM`).
 */
export const createDeviceCall = (catalog: ServiceCatalog, device: number, type: string | number, config?: FieldValues, system = false): ServiceCall => {
  const deviceType = catalog.device(type)
  const lifecycle = lifecycleOf(catalog)
  const create = catalog.command(lifecycle, CREATE)
  const [typeField, flagsField, configField] = create.write?.args?.fields ?? []
  if (!typeField || !flagsField || !configField) throw new Error(`${lifecycle.id} ${CREATE} has no write with a type, flags and config fields.`)
  if (system && catalog.createSystem === undefined) throw new Error('The flag of the board\'s own device is not published (device_create_flag).')
  if (!deviceType.config && config && Object.keys(config).length > 0) throw new PayloadError(`${deviceType.id} has no config.`)
  const body = deviceType.config ? encodeFields(deviceType.config, config) : new Uint8Array(0)
  const args = { [typeField.id]: deviceType.typeId!, [flagsField.id]: system ? catalog.createSystem! : 0, [configField.id]: body }
  const call = deviceCall(catalog, device, lifecycle.id, CREATE, args, 'write')
  return { ...call, label: `device ${device} ${lifecycle.id} ${CREATE} ${deviceType.id}` }
}

/**
 * CREATE_REMOTE under `device`: an alias of device `remoteId` of type `type` on board `board`, reached through the session
 * (SYS_REMOTE.MD). Its fields by their place in the descriptor: the type, the board, the remote id, the flag of how its calls go
 * (`device_remote_flag`): `wired` waits for every answer, `wireless` (the default) does not.
 */
export const createRemoteDeviceCall = (catalog: ServiceCatalog, device: number, type: string | number, board: number, remoteId: number, link: 'wired' | 'wireless' = 'wireless'): ServiceCall => {
  const deviceType = catalog.device(type)
  const lifecycle = lifecycleOf(catalog)
  const create = catalog.command(lifecycle, CREATE_REMOTE)
  const [typeField, boardField, idField, flagsField] = create.write?.args?.fields ?? []
  if (!typeField || !boardField || !idField || !flagsField) throw new Error(`${lifecycle.id} ${CREATE_REMOTE} has no write with a type, a board, a remote id and flags.`)
  const args = { [typeField.id]: deviceType.typeId!, [boardField.id]: board, [idField.id]: remoteId, [flagsField.id]: catalog.remoteFlags[link] }
  const call = deviceCall(catalog, device, lifecycle.id, CREATE_REMOTE, args, 'write')
  return { ...call, label: `device ${device} ${lifecycle.id} ${CREATE_REMOTE} ${deviceType.id} on board ${board}` }
}

/** The provider service's commands (`contract_provider.json`): the one that makes a provider and its fields by place. */
export const PROVIDER_COMMANDS = { service: 'PROVIDER', create: 'CREATE', remove: 'REMOVE', list: 'LIST' } as const

/**
 * PROVIDER CREATE of a provider of type `type` (a provider descriptor's id, `ble`) under `id`, its config by the descriptor's
 * fields: the provider service's write, whose fields are the type, the flags and the type's config struct.
 */
export const createProviderCall = (catalog: ServiceCatalog, id: number, type: string | number, config?: FieldValues): ServiceCall => {
  const providerType = catalog.provider(type)
  const entry = catalog.service(PROVIDER_COMMANDS.service)
  const create = catalog.command(entry, PROVIDER_COMMANDS.create)
  const [typeField, flagsField, configField] = create.write?.args?.fields ?? []
  if (!typeField || !flagsField || !configField) throw new Error(`${entry.id} ${PROVIDER_COMMANDS.create} has no write with a type, flags and config fields.`)
  const body = providerType.config ? encodeFields(providerType.config, config) : new Uint8Array(0)
  const call = serviceCall(catalog, entry.id, PROVIDER_COMMANDS.create, { [typeField.id]: providerType.providerType!, [flagsField.id]: 0, [configField.id]: body }, 'write', id)
  return { ...call, label: `${entry.id} ${id} ${PROVIDER_COMMANDS.create} ${providerType.id}` }
}

/** A provider's CREATE frame read back: its id, its type and its config. */
export interface ProviderFrame {
  readonly id: number
  readonly type: CatalogEntry
  readonly config: FieldRecord
}

/** A stored frame `[key][dir][id][op u16][type][flags][cfg]` read as a PROVIDER CREATE; undefined for any other frame. */
export const readProviderFrame = (catalog: ServiceCatalog, frame: Uint8Array): ProviderFrame | undefined => {
  const entry = catalog.services.get(PROVIDER_COMMANDS.service)
  const create = entry?.commands.get(PROVIDER_COMMANDS.create)
  const args = create?.write?.args
  if (!entry || !create || !args || frame.length < 5 || frame[0] !== entry.key || frame[1] !== catalog.dirs.write) return undefined
  if ((frame[3]! | (frame[4]! << 8)) !== deviceOp(catalog, entry, create)) return undefined
  try {
    const head = decodeFields(args, frame.subarray(5))
    const type = catalog.byProviderType.get(Number(head[args.fields[0]!.id]))
    if (!type?.config) return undefined
    return { id: frame[2]!, type, config: decodeFields(type.config, head[args.fields[2]!.id] as Uint8Array) }
  } catch (error) {
    if (error instanceof PayloadError) return undefined
    throw error
  }
}

/** A device frame read back: the device, the command, its side and its arguments; a CREATE also gives the type, whether it is the board's own and its config. */
export interface DeviceFrame {
  readonly device: number
  readonly entry: CatalogEntry
  readonly command: CommandDef
  readonly side: Side
  readonly args?: FieldRecord
  readonly create?: { readonly type: CatalogEntry; readonly system: boolean; readonly config?: FieldRecord }
}

/**
 * A stored frame (`[key][dir][device u8][op u16][args]`) back into the device command it is: the mirror of `deviceCall` /
 * `createDeviceCall`. A device type's own op is read with the type `typeOf` gives for the device (the op alone does not say whose
 * it is). Undefined for a frame that is not a device command the catalog describes, or whose arguments do not fit its layout.
 */
export const readDeviceFrame = (catalog: ServiceCatalog, frame: Uint8Array, typeOf: (device: number) => CatalogEntry | undefined = () => undefined): DeviceFrame | undefined => {
  const lifecycle = catalog.lifecycleContractId === undefined ? undefined : catalog.byContractId.get(catalog.lifecycleContractId)
  if (!lifecycle || frame.length < 5 || frame[0] !== lifecycle.key) return undefined
  const side = catalog.sideOfDir(frame[1]!)
  const device = frame[2]!
  const op = frame[3]! | (frame[4]! << 8)
  const contract = op >> 8
  const payload = frame.subarray(5)
  const entry = contract === catalog.ownContractId ? typeOf(device) : catalog.byContractId.get(contract)
  const command = entry && [...entry.commands.values()].find((c) => c.op === (op & 0xff))
  if (!side || !entry || !command || !command[side]) return undefined
  const args = command[side]!.args
  try {
    if (entry === lifecycle && command.id === CREATE && args) {
      const head = decodeFields(args, payload)
      const type = catalog.byTypeId.get(Number(head[args.fields[0]!.id]))
      if (!type) return undefined
      const body = head[args.fields[2]!.id] as Uint8Array
      const system = catalog.createSystem !== undefined && (Number(head[args.fields[1]!.id]) & catalog.createSystem) !== 0
      return { device, entry, command, side, create: { type, system, ...(type.config ? { config: decodeFields(type.config, body) } : {}) } }
    }
    return { device, entry, command, side, ...(args ? { args: decodeFields(args, payload) } : {}) }
  } catch (error) {
    if (error instanceof PayloadError) return undefined
    throw error
  }
}

/* ---- answers ---- */

/** An answer whose descriptor has a `lead`: the number in front (a count, a total), then the rows. */
export interface LeadAnswer {
  readonly lead: number
  readonly rows: readonly FieldValue[] | readonly FieldRecord[]
}

export type Decoded = FieldValue | FieldRecord | readonly FieldValue[] | readonly FieldRecord[] | LeadAnswer

const chunks = (data: Uint8Array, size: number, what: string): Uint8Array[] => {
  if (size <= 0) throw new PayloadError(`${what}: an element of no size.`)
  if (data.length % size !== 0) throw new PayloadError(`${what}: ${data.length} bytes are not whole elements of ${size}.`)
  return Array.from({ length: data.length / size }, (_, i) => data.subarray(i * size, (i + 1) * size))
}

const rowsOf = (returns: Returns, data: Uint8Array, what: string): Decoded => {
  if (returns.kind === 'struct') {
    if (!returns.array) return decodeFields(returns.layout, data)
    if (returns.layout.variable) throw new PayloadError(`${what}: a list of structs that end in a field of any length.`)
    return chunks(data, returns.layout.fixed, what).map((c) => decodeFields(returns.layout, c))
  }
  const width = rawWidth(returns.rawType)
  if (!returns.array || width === undefined) return readValue(data, 0, returns.rawType, width, returns.boolean)
  return chunks(data, width, what).map((c) => readValue(c, 0, returns.rawType, width, returns.boolean))
}

/**
 * The answer as its descriptor describes it: a struct by field id, a value, or a list of either; with a `lead`, the number in
 * front and the rows after it.
 */
export const decodeReturns = (returns: Returns, data: Uint8Array, what = 'answer'): Decoded => {
  if (!returns.lead) return rowsOf(returns, data, what)
  const width = rawWidth(returns.lead.rawType)!
  const lead = Number(readValue(data, 0, returns.lead.rawType, width))
  return { lead, rows: rowsOf(returns, data.subarray(width), what) as LeadAnswer['rows'] }
}

/** The decoded answer of a call; undefined when its side describes none. */
export const decodeAnswer = (call: Pick<ServiceCall, 'command' | 'side' | 'label'>, data: Uint8Array): Decoded | undefined => {
  const returns = call.command[call.side]?.returns
  return returns ? decodeReturns(returns, data, call.label) : undefined
}
