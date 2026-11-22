import type { FieldRecord } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'
import { createProviderCall, readProviderFrame, serviceCall } from './calls'
import type { ServiceCall } from './calls'

/*
 * The board's GATT table and the characteristics a client adds to it (SYS_BLE.MD, PROVIDER_BLE.MD). The table is made by the
 * providers: a provider of the BLE type (`provider_ble.json`) makes its service when that is new and one characteristic, written
 * and notified, its frames go out on and come in from (`chr`), and takes it with it when it is removed. So a way is a
 * provider, and a service is only the UUID its ways share. `BLE LIST` reads the table as the board has it.
 *
 * The config's fields are the descriptor's; the flags are the published ones (`provider_ble_flag`, `cmd_send_flag`,
 * `ble_svc_flag`, `ble_chr_flag`).
 */

/** The BLE provider type (`provider_ble.json`) and its config's fields, and BLE's table read (`contract_ble.json`). */
export const BLE_WAYS = {
  provider: 'ble',
  service: 'svc', chr: 'chr', flags: 'flags', rxFlags: 'rx_flags', name: 'name',
  table: { service: 'BLE', command: 'LIST' },
} as const

/** A way of the board's Bluetooth: a provider of the BLE type. */
export interface BleWay {
  /** Its provider id. */
  readonly provider: number
  /** The service it is in. */
  readonly service: number
  /** The characteristic: its frames go out on it as notifications and come in from it as writes. */
  readonly chr: number
  /** It carries frames (a session's way); else raw bytes (a stream's). */
  readonly framed: boolean
  readonly name: string
}

/** PROVIDER CREATE of a way. A write of the client goes out of the provider's port without waiting (`CMD_SEND_NOWAIT`): the BLE host never waits. */
export const bleWayCall = (defs: LinkDefinitions, catalog: ServiceCatalog, way: BleWay): ServiceCall =>
  createProviderCall(catalog, way.provider, BLE_WAYS.provider, {
    [BLE_WAYS.service]: way.service,
    [BLE_WAYS.chr]: way.chr,
    [BLE_WAYS.flags]: way.framed ? defs.ble.providerFramed : 0,
    [BLE_WAYS.rxFlags]: defs.ble.sendNoWait,
    [BLE_WAYS.name]: way.name,
  })

/** The way a stored PROVIDER CREATE frame makes; undefined for any other frame, a provider of another type included. */
export const readBleWay = (defs: LinkDefinitions, catalog: ServiceCatalog, frame: Uint8Array): BleWay | undefined => {
  const read = readProviderFrame(catalog, frame)
  if (!read || read.type.id !== BLE_WAYS.provider) return undefined
  const c: FieldRecord = read.config
  return {
    provider: read.id,
    service: Number(c[BLE_WAYS.service]),
    chr: Number(c[BLE_WAYS.chr]),
    framed: (Number(c[BLE_WAYS.flags]) & defs.ble.providerFramed) !== 0,
    name: String(c[BLE_WAYS.name] ?? ''),
  }
}

/** A characteristic of the board's table. */
export interface LiveCharacteristic {
  readonly uuid: number
  readonly name: string
  readonly write: boolean
  readonly indicate: boolean
  readonly notify: boolean
  readonly framed: boolean
}

/** A service of the board's table; `locked` marks the board's own (the app's link), never changed. */
export interface LiveGattService {
  readonly uuid: number
  readonly primary: boolean
  readonly locked: boolean
  readonly characteristics: readonly LiveCharacteristic[]
}

/** The call that reads the table. */
export const bleTableCall = (catalog: ServiceCatalog): ServiceCall => serviceCall(catalog, BLE_WAYS.table.service, BLE_WAYS.table.command)

const u16 = (d: Uint8Array, at: number): number => d[at]! | (d[at + 1]! << 8)

/**
 * `BLE LIST`'s answer, whose layout the descriptor gives in words (a list of lists, names NUL-ended): `u8 count`, per service
 * `u16 uuid, u8 flags, u8 n`, then n × `u16 uuid, u8 flags, u16 tx, name\0`.
 */
export const decodeBleTable = (defs: LinkDefinitions, data: Uint8Array): LiveGattService[] => {
  const { service: svc, characteristic: chr } = defs.ble
  const services: LiveGattService[] = []
  let at = 1
  for (let s = 0; s < (data[0] ?? 0) && at + 4 <= data.length; s++) {
    const [uuid, flags, n] = [u16(data, at), data[at + 2]!, data[at + 3]!]
    at += 4
    const characteristics: LiveCharacteristic[] = []
    for (let c = 0; c < n && at + 5 <= data.length; c++) {
      const f = data[at + 2]!
      let end = at + 5
      while (end < data.length && data[end] !== 0) end++
      const name = new TextDecoder().decode(data.subarray(at + 5, end))
      characteristics.push({ uuid: u16(data, at), name, write: (f & chr.write) !== 0, indicate: (f & chr.indicate) !== 0, notify: (f & chr.notify) !== 0, framed: (f & chr.framed) !== 0 })
      at = end + 1
    }
    services.push({ uuid, primary: (flags & svc.primary) !== 0, locked: (flags & svc.locked) !== 0, characteristics })
  }
  return services
}
