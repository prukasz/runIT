import { bundledEnums, deviceOp, requestFrame, serviceCall } from '@backend'
import type { ServiceCall, ServiceCatalog } from '@backend'
import type { GattCharacteristic, GattService } from '../project/projectFile'

/*
 * What the project adds to the board's Bluetooth (SYS_BLE.MD, PROVIDER_BLE.MD). The board's GATT table is made by the providers: a
 * provider of type BLE (`PROVIDER CREATE`) makes its service when that is new, a notify characteristic frames go out on (`tx`) and
 * a writable one they come in from (`rx`), and takes them with it when it is removed. So the project keeps providers, grouped by
 * the service they are in; the service is only the UUID they share.
 *
 *   record   the frames of the settings record: one `PROVIDER CREATE` per provider (the table is built again from it)
 *   apply    what changes the board now: the providers of the project it has removed, then every one created; the board drops the
 *            Bluetooth link for a moment when a client is connected
 *   table    `BLE LIST`: the table as the board has it, to show it and to compare it with the project
 */

/** The commands this file builds frames of, and reads. */
export const GATT = { service: 'BLE', list: 'LIST', providers: 'PROVIDER', create: 'CREATE', remove: 'REMOVE', providerList: 'LIST' } as const

/** `SYS_DATA_PROVIDER_TYPE_BLE`: the provider type CREATE takes (`sys_data_provider_type_e` of enums.json). */
const bleType = (): number => {
  const member = bundledEnums().sys_data_provider_type_e?.members.find((m) => m.name === 'SYS_DATA_PROVIDER_TYPE_BLE')
  if (!member) throw new Error("generated/common/enums.json: group 'sys_data_provider_type_e' has no SYS_DATA_PROVIDER_TYPE_BLE.")
  return member.value
}

/** `PROVIDER_BLE_FRAMED` (provider_ble.h): both characteristics carry frames. */
const FRAMED = 0x01
/** `provider_ble_cfg_t`: `{u16 svc, u16 tx, u16 rx, u8 flags, char name[16]}`, 23 B. */
const CFG_BYTES = 23
const NAME_BYTES = 16

/** A characteristic of the board's table as `BLE LIST` gives it; `locked` marks the board's own service (the app's link), never changed. */
export interface LiveCharacteristic {
  readonly uuid: number
  readonly name: string
  readonly write: boolean
  readonly indicate: boolean
  readonly notify: boolean
  readonly framed: boolean
}

export interface LiveGattService {
  readonly uuid: number
  readonly primary: boolean
  readonly locked: boolean
  readonly characteristics: readonly LiveCharacteristic[]
}

const u16 = (d: Uint8Array, at: number): number => d[at]! | (d[at + 1]! << 8)
const nameAt = (d: Uint8Array, at: number): [string, number] => {
  let end = at
  while (end < d.length && d[end] !== 0) end++
  return [new TextDecoder().decode(d.subarray(at, end)), end + 1]
}

/** LIST's answer: `u8 count`, per service `u16 uuid, u8 flags (primary, locked), u8 n`, n × `u16 uuid, u8 flags, u16 tx, name\0`. */
export const decodeGattList = (data: Uint8Array): LiveGattService[] => {
  const services: LiveGattService[] = []
  let at = 1
  for (let s = 0; s < (data[0] ?? 0) && at + 4 <= data.length; s++) {
    const [uuid, flags, n] = [u16(data, at), data[at + 2]!, data[at + 3]!]
    at += 4
    const characteristics: LiveCharacteristic[] = []
    for (let c = 0; c < n && at + 5 <= data.length; c++) {
      const charFlags = data[at + 2]!
      const [name, next] = nameAt(data, at + 5)
      characteristics.push({ uuid: u16(data, at), name, write: (charFlags & 1) !== 0, indicate: (charFlags & 2) !== 0, notify: (charFlags & 4) !== 0, framed: (charFlags & 8) !== 0 })
      at = next
    }
    services.push({ uuid, primary: (flags & 1) !== 0, locked: (flags & 2) !== 0, characteristics })
  }
  return services
}

/** `PROVIDER LIST`'s answer: `u8 count`, then `{provider u8, type u8, state u8}` per provider: the ids of the BLE ones. */
export const decodeBleProviders = (data: Uint8Array): number[] => {
  const ids: number[] = []
  for (let i = 0; i < (data[0] ?? 0) && 1 + 3 * i + 3 <= data.length; i++) if (data[2 + 3 * i] === bleType()) ids.push(data[1 + 3 * i]!)
  return ids
}

/** The create config of a provider (`provider_ble_cfg_t`) of the characteristics `c` in service `service`. */
const cfgOf = (service: number, c: GattCharacteristic): Uint8Array => {
  const cfg = new Uint8Array(CFG_BYTES)
  const view = new DataView(cfg.buffer)
  view.setUint16(0, service, true)
  view.setUint16(2, c.tx, true)
  view.setUint16(4, c.rx, true)
  cfg[6] = c.framed ? FRAMED : 0
  cfg.set(new TextEncoder().encode(c.name).subarray(0, NAME_BYTES - 1), 7)
  return cfg
}

const createCall = (catalog: ServiceCatalog, service: number, c: GattCharacteristic): ServiceCall =>
  serviceCall(catalog, GATT.providers, GATT.create, { type: bleType(), flags: 0, cfg: cfgOf(service, c) }, 'write', c.provider)

const removeCall = (catalog: ServiceCatalog, provider: number): ServiceCall => serviceCall(catalog, GATT.providers, GATT.remove, undefined, 'write', provider)

const build = (catalog: ServiceCatalog, services: readonly GattService[]): ServiceCall[] =>
  services.flatMap((s) => s.characteristics.map((c) => createCall(catalog, s.uuid, c)))

/** The frames of the BLE record: the project's providers created. None when the project adds none. */
export const gattRecord = (catalog: ServiceCatalog, services: readonly GattService[]): Uint8Array[] =>
  build(catalog, services).map((c) => requestFrame(c.request))

/** The project's providers the board has (`have`: the BLE providers it lists) removed: a CREATE of one that exists is refused, so they go before the record runs. */
export const gattRemoves = (catalog: ServiceCatalog, have: readonly number[], services: readonly GattService[]): ServiceCall[] =>
  services.flatMap((s) => s.characteristics.map((c) => c.provider)).filter((id) => have.includes(id)).map((id) => removeCall(catalog, id))

/** The commands that bring the board to the project's providers: the ones it has removed, then every one created. */
export const gattApply = (catalog: ServiceCatalog, have: readonly number[], services: readonly GattService[]): ServiceCall[] => [
  ...gattRemoves(catalog, have, services),
  ...build(catalog, services),
]

/** The services a stored BLE record builds (its `PROVIDER CREATE` frames of type BLE): `[key][dir][provider][op u16][type][flags][cfg]`. */
export const gattFromRecord = (catalog: ServiceCatalog, frames: readonly Uint8Array[]): GattService[] => {
  const entry = catalog.service(GATT.providers)
  const create = catalog.command(entry, GATT.create)
  const services: GattService[] = []
  for (const frame of frames) {
    const op = frame[3]! | (frame[4]! << 8)
    if (frame[0] !== entry.key || frame[1] !== catalog.dirs.write || op !== deviceOp(catalog, entry, create) || frame[5] !== bleType() || frame.length < 7 + CFG_BYTES) continue
    const cfg = frame.subarray(7)
    const [svc, tx, rx] = [u16(cfg, 0), u16(cfg, 2), u16(cfg, 4)]
    const [name] = nameAt(cfg, 7)
    const c: GattCharacteristic = { provider: frame[2]!, tx, rx, framed: (cfg[6]! & FRAMED) !== 0, name }
    const at = services.findIndex((s) => s.uuid === svc)
    if (at < 0) services.push({ uuid: svc, characteristics: [c] })
    else services[at] = { ...services[at]!, characteristics: [...services[at]!.characteristics, c] }
  }
  return services
}

/** The characteristic UUIDs of each service, as a comparable text: what the project's providers make and what the board's table has. */
const uuids = (services: readonly { readonly uuid: number; readonly chars: readonly number[] }[]): string =>
  JSON.stringify(services.map((s) => [s.uuid, [...s.chars].sort((a, b) => a - b)]).sort((a, b) => (a[0] as number) - (b[0] as number)))

/** Whether the board's table has what the project's providers make (services and their characteristics by UUID). */
export const sameGatt = (project: readonly GattService[], live: readonly LiveGattService[]): boolean =>
  uuids(project.map((s) => ({ uuid: s.uuid, chars: s.characteristics.flatMap((c) => [c.tx, c.rx].filter((u) => u !== 0)) })))
  === uuids(live.filter((s) => !s.locked).map((s) => ({ uuid: s.uuid, chars: s.characteristics.map((c) => c.uuid) })))
