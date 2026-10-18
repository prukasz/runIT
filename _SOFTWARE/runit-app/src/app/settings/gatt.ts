import { decodeFields, requestFrame, serviceCall } from '@backend'
import type { ServiceCall, ServiceCatalog } from '@backend'
import type { GattCharacteristic, GattService } from '../project/projectFile'

/*
 * The GATT table (SYS_BLE.MD "Runtime changes"): the services and characteristics the project adds next to the board's own
 * (locked) service. The commands and their fields are the BLE descriptor's (`contract_ble.json`); this file only knows which
 * command does what, and the layout of LIST's answer, which the descriptor gives as bytes.
 *
 *   record   the frames of the settings record: every user service and its characteristics created, then APPLY (a start
 *            builds the table again from it)
 *   apply    what changes the board now: every user service it has removed, the project's created, then APPLY (the link
 *            drops and comes back when a client is connected)
 */

/** The BLE descriptor's commands and fields this file builds frames of. */
export const GATT = {
  service: 'BLE', createService: 'SVC_CREATE', removeService: 'SVC_REMOVE', createChar: 'CHAR_CREATE', apply: 'APPLY', list: 'LIST',
} as const

/** A service of the board's table as LIST gives it: `locked` is the board's own (runIT), never changed. */
export interface LiveGattService extends GattService {
  readonly locked: boolean
}

const u16 = (d: Uint8Array, at: number): number => d[at]! | (d[at + 1]! << 8)
const u32 = (d: Uint8Array, at: number): number => (d[at]! | (d[at + 1]! << 8) | (d[at + 2]! << 16)) + d[at + 3]! * 0x1000000
const nameAt = (d: Uint8Array, at: number): [string, number] => {
  let end = at
  while (end < d.length && d[end] !== 0) end++
  return [new TextDecoder().decode(d.subarray(at, end)), end + 1]
}

/** LIST's answer: `u8 count`, per service `u16 uuid, u8 flags (primary, locked), u8 n`, n × `u16 uuid, u8 flags, u32 tx, u32 rx, name\0`. */
export const decodeGattList = (data: Uint8Array): LiveGattService[] => {
  const services: LiveGattService[] = []
  let at = 1
  for (let s = 0; s < (data[0] ?? 0) && at + 4 <= data.length; s++) {
    const [uuid, flags, n] = [u16(data, at), data[at + 2]!, data[at + 3]!]
    at += 4
    const characteristics: GattCharacteristic[] = []
    for (let c = 0; c < n && at + 11 <= data.length; c++) {
      const charFlags = data[at + 2]!
      const [name, next] = nameAt(data, at + 11)
      characteristics.push({
        uuid: u16(data, at), name, write: (charFlags & 1) !== 0, indicate: (charFlags & 2) !== 0, notify: (charFlags & 4) !== 0, framed: (charFlags & 8) !== 0,
        tx: u32(data, at + 3), rx: u32(data, at + 7),
      })
      at = next
    }
    services.push({ uuid, primary: (flags & 1) !== 0, locked: (flags & 2) !== 0, characteristics })
  }
  return services
}

const createService = (catalog: ServiceCatalog, s: GattService): ServiceCall =>
  serviceCall(catalog, GATT.service, GATT.createService, { uuid: s.uuid, is_primary: s.primary })

const createChar = (catalog: ServiceCatalog, s: GattService, c: GattCharacteristic): ServiceCall =>
  serviceCall(catalog, GATT.service, GATT.createChar, {
    svc_uuid: s.uuid, uuid: c.uuid, is_write: c.write, is_indicate: c.indicate, is_notify: c.notify, is_framed: c.framed,
    tx_buffer_size: c.tx, rx_buffer_size: c.rx, desc: c.name,
  })

const build = (catalog: ServiceCatalog, services: readonly GattService[]): ServiceCall[] =>
  services.flatMap((s) => [createService(catalog, s), ...s.characteristics.map((c) => createChar(catalog, s, c))])

/** The frames of the GATT record: the user services built, then APPLY. None when the project adds no service. */
export const gattRecord = (catalog: ServiceCatalog, services: readonly GattService[]): Uint8Array[] =>
  services.length === 0 ? [] : [...build(catalog, services), serviceCall(catalog, GATT.service, GATT.apply)].map((c) => requestFrame(c.request))

/** The commands that bring the board's table to the project's: the board's user services removed, the project's built, APPLY. */
export const gattApply = (catalog: ServiceCatalog, live: readonly LiveGattService[], services: readonly GattService[]): ServiceCall[] => [
  ...live.filter((s) => !s.locked).map((s) => serviceCall(catalog, GATT.service, GATT.removeService, { uuid: s.uuid })),
  ...build(catalog, services),
  serviceCall(catalog, GATT.service, GATT.apply),
]

/** The user services a stored GATT record builds (its SVC_CREATE and CHAR_CREATE frames, by the descriptor's layouts). */
export const gattFromRecord = (catalog: ServiceCatalog, frames: readonly Uint8Array[]): GattService[] => {
  const entry = catalog.service(GATT.service)
  const svc = catalog.command(entry, GATT.createService)
  const chr = catalog.command(entry, GATT.createChar)
  const services: GattService[] = []
  for (const frame of frames) {
    if (frame[0] !== entry.key) continue
    const args = (command: typeof svc) => decodeFields(command.args!, frame.subarray(2)) as Readonly<Record<string, unknown>>
    if (frame[1] === svc.op) services.push({ uuid: Number(args(svc).uuid), primary: Boolean(args(svc).is_primary), characteristics: [] })
    else if (frame[1] === chr.op) {
      const a = args(chr)
      const at = services.findIndex((s) => s.uuid === Number(a.svc_uuid))
      if (at < 0) continue
      const c: GattCharacteristic = {
        uuid: Number(a.uuid), name: String(a.desc ?? ''), write: Boolean(a.is_write), indicate: Boolean(a.is_indicate), notify: Boolean(a.is_notify),
        framed: Boolean(a.is_framed), tx: Number(a.tx_buffer_size), rx: Number(a.rx_buffer_size),
      }
      services[at] = { ...services[at]!, characteristics: [...services[at]!.characteristics, c] }
    }
  }
  return services
}

/** Two GATT tables equal (the user services, in order). */
export const sameGatt = (a: readonly GattService[], b: readonly GattService[]): boolean =>
  JSON.stringify(a.map(({ uuid, primary, characteristics }) => ({ uuid, primary, characteristics }))) ===
  JSON.stringify(b.map(({ uuid, primary, characteristics }) => ({ uuid, primary, characteristics })))
