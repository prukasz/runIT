import { routerKey, routerOp } from '../../backend/catalog'
import { hex, transportId, uartEndpointText } from './boardSetup'
import { configOp, decodeConfig, opRecord, settingsOps } from './contractConfig'
import type { ConfigValues } from './contractConfig'
import { boardConnectors, defaultBleProfile } from './projectSettings'
import type { BleCharacteristicSettings, BleProfile, BleServiceSettings, ConnectorSettings } from './types'

/*
 * The other way of saving: the records the board keeps (`@record` in C) read back into editor form. A record is a list of frames, each
 * `[router key][op][arguments]` (a name after the arguments for CHAR_CREATE / CREATE), so they are decoded with the same argument
 * structs the save builds them from. A record the board does not hold means the board is in its default state for that part.
 */

/** The frames of each record the board stored, by record id; a missing id is a record the board does not hold. */
export type StoredRecords = ReadonlyMap<number, readonly Uint8Array[]>

const BLE = 'ROUTER_KEY_BLE'
const CONN = 'ROUTER_KEY_CONN'

/** The ids of every record the app keeps settings in. */
export const settingsRecordIds = (routerSymbols: readonly string[]): number[] => {
  const ids = new Set<number>()
  for (const symbol of routerSymbols) for (const op of settingsOps(symbol)) {
    const id = opRecord(symbol, op.symbol)
    if (id !== undefined) ids.add(id)
  }
  for (const [router, op] of [[BLE, 'OP_SVC_CREATE'], [CONN, 'OP_CREATE']] as const) {
    const id = opRecord(router, op)
    if (id !== undefined) ids.add(id)
  }
  return [...ids]
}

const textAfter = (frame: Uint8Array, from: number): string => {
  const rest = frame.subarray(from)
  const end = rest.indexOf(0)
  return new TextDecoder().decode(end < 0 ? rest : rest.subarray(0, end))
}

/** The values of the settings ops found in the records, by op symbol; an op with no frame is left out (it keeps the default). */
export const decodeSettingsOps = (routerSymbol: string, records: StoredRecords): Record<string, ConfigValues> => {
  const key = routerKey(routerSymbol)
  const found: Record<string, ConfigValues> = {}
  for (const op of settingsOps(routerSymbol)) {
    const frames = records.get(opRecord(routerSymbol, op.symbol) ?? -1) ?? []
    // the last frame of the op wins, like a replay
    for (const frame of frames) if (frame[0] === key && frame[1] === op.opId) found[op.symbol] = decodeConfig(op, frame.subarray(2))
  }
  return found
}

/** The user's services and characteristics the BLE record builds; the board's own service stays as the board defines it. */
export const decodeBleRecord = (records: StoredRecords, known: BleProfile): BleProfile => {
  const defaults = defaultBleProfile()
  const frames = records.get(opRecord(BLE, 'OP_SVC_CREATE') ?? -1) ?? []
  const key = routerKey(BLE)
  const createService = configOp(BLE, 'OP_SVC_CREATE')
  const createChar = configOp(BLE, 'OP_CHAR_CREATE')
  const services: BleServiceSettings[] = []
  for (const frame of frames) {
    if (frame[0] !== key) continue
    if (frame[1] === createService.opId) {
      const values = decodeConfig(createService, frame.subarray(2))
      const uuid = hex(values.uuid!, 4)
      const old = known.services.find((service) => !service.system && service.uuid.toUpperCase() === uuid)
      services.push({ id: old?.id ?? `svc_${uuid}`, name: old?.name ?? `Service ${uuid}`, uuid, isPrimary: values.is_primary === 1, characteristics: [] })
    } else if (frame[1] === createChar.opId) {
      const values = decodeConfig(createChar, frame.subarray(2))
      const service = services.find((entry) => entry.uuid === hex(values.svc_uuid!, 4))
      if (!service) continue
      const uuid = hex(values.uuid!, 4)
      const old = known.services.flatMap((entry) => entry.characteristics).find((char) => !char.system && char.uuid.toUpperCase() === uuid)
      const char: BleCharacteristicSettings = {
        id: old?.id ?? `chr_${uuid}`, name: textAfter(frame, 2 + createChar.size), uuid, write: values.is_write === 1, notify: values.is_notify === 1,
        indicate: values.is_indicate === 1, framed: values.is_framed === 1, txBufferSize: values.tx_buffer_size!, rxBufferSize: values.rx_buffer_size!,
      }
      service.characteristics.push(char)
    }
  }
  return { ...known, services: [defaults.services[0]!, ...services] }
}

const providerName = (value: number): 'BLE' | 'UART' | undefined => (['BLE', 'UART'] as const).find((name) => transportId(name) === value)

/** The streams the CONN record leaves on a board that has just started: its own four with the changed bindings, plus the user's. */
export const decodeConnRecord = (records: StoredRecords, known: readonly ConnectorSettings[]): ConnectorSettings[] => {
  const frames = records.get(opRecord(CONN, 'OP_CREATE') ?? -1) ?? []
  const key = routerKey(CONN)
  const op = (symbol: string) => configOp(CONN, symbol)
  const [create, bind, unbind] = [op('OP_CREATE'), op('OP_BIND'), op('OP_UNBIND')]
  const suspend = routerOp(CONN, 'OP_SUSPEND')
  const connectors = boardConnectors()
  const byId = (id: number) => connectors.find((connector) => connector.id === id)
  const bindingId = (connector: ConnectorSettings, provider: string, endpoint: string) => `b_${connector.key}_${provider.toLowerCase()}_${endpoint}`
  for (const frame of frames) {
    if (frame[0] !== key) continue
    if (frame[1] === create.opId) {
      const values = decodeConfig(create, frame.subarray(2))
      const name = textAfter(frame, 2 + create.size)
      const old = known.find((connector) => !connector.system && connector.id === values.id)
      connectors.push({ id: values.id!, key: `connector_${values.id}`, name, alias: old?.alias ?? name, stream: hex(values.stream!, 2), system: false, description: old?.description ?? '', isSuspended: false, bindings: [] })
    } else if (frame[1] === bind.opId) {
      const values = decodeConfig(bind, frame.subarray(2))
      const connector = byId(values.id!)
      const provider = providerName(values.provider!)
      if (!connector || !provider) continue
      const endpoint = provider === 'UART' ? uartEndpointText(values.endpoint!) : hex(values.endpoint!, 4)
      connector.bindings = [...connector.bindings.filter((entry) => entry.provider !== provider), { id: bindingId(connector, provider, endpoint), provider, endpoint }]
    } else if (frame[1] === unbind.opId) {
      const values = decodeConfig(unbind, frame.subarray(2))
      const connector = byId(values.id!)
      const provider = providerName(values.provider!)
      if (connector && provider) connector.bindings = connector.bindings.filter((entry) => entry.provider !== provider)
    } else if (frame[1] === suspend) {
      const connector = byId(frame[2]!)
      if (connector) connector.isSuspended = true
    }
  }
  return connectors
}

/** Everything the app edits, from the records: what a "sync" puts in the forms. */
export interface BoardSettings {
  readonly routers: Readonly<Record<string, Readonly<Record<string, ConfigValues>>>>
  readonly ble: BleProfile
  readonly connectors: ConnectorSettings[]
}

export const decodeBoardSettings = (records: StoredRecords, routerSymbols: readonly string[], known: { ble: BleProfile; connectors: readonly ConnectorSettings[] }): BoardSettings => ({
  routers: Object.fromEntries(routerSymbols.map((symbol) => [symbol, decodeSettingsOps(symbol, records)])),
  ble: decodeBleRecord(records, known.ble),
  connectors: decodeConnRecord(records, known.connectors),
})

/*
 * What the board runs now (not what it keeps): the `ble` router's LIST (the GATT table after the next apply) and the `conn` router's
 * INFO (every stream with its bindings). A save diffs against these, so it never removes what is not there.
 */

const u16 = (data: Uint8Array, at: number): number => data[at]! | (data[at + 1]! << 8)
const u32 = (data: Uint8Array, at: number): number => (data[at]! | (data[at + 1]! << 8) | (data[at + 2]! << 16)) + data[at + 3]! * 0x1000000

/** Reads a NUL-ended name at `at`: [text, the index after the NUL]. */
const nameAt = (data: Uint8Array, at: number): [string, number] => {
  let end = at
  while (end < data.length && data[end] !== 0) end++
  return [new TextDecoder().decode(data.subarray(at, end)), end + 1]
}

/** The user's services of the live table in editor form (the board's locked service stays as the board setup defines it). */
export const decodeBleList = (data: Uint8Array, known: BleProfile): BleProfile => {
  const defaults = defaultBleProfile()
  const services: BleServiceSettings[] = []
  let at = 1
  for (let s = 0; s < (data[0] ?? 0); s++) {
    const uuid = hex(u16(data, at), 4)
    const flags = data[at + 2]!
    const count = data[at + 3]!
    at += 4
    const old = known.services.find((service) => !service.system && service.uuid.toUpperCase() === uuid)
    const service: BleServiceSettings = { id: old?.id ?? `svc_${uuid}`, name: old?.name ?? `Service ${uuid}`, uuid, isPrimary: (flags & 1) !== 0, characteristics: [] }
    for (let c = 0; c < count; c++) {
      const charUuid = hex(u16(data, at), 4)
      const charFlags = data[at + 2]!
      const tx = u32(data, at + 3)
      const rx = u32(data, at + 7)
      const [name, next] = nameAt(data, at + 11)
      at = next
      const oldChar = known.services.flatMap((entry) => entry.characteristics).find((char) => !char.system && char.uuid.toUpperCase() === charUuid)
      service.characteristics.push({
        id: oldChar?.id ?? `chr_${charUuid}`, name, uuid: charUuid, write: (charFlags & 1) !== 0, indicate: (charFlags & 2) !== 0, notify: (charFlags & 4) !== 0,
        framed: (charFlags & 8) !== 0, txBufferSize: tx, rxBufferSize: rx,
      })
    }
    // the locked service is the board's own (runIT): the editor keeps it from the board setup
    if ((flags & 2) === 0 && uuid !== defaults.services[0]!.uuid) services.push(service)
  }
  return { ...known, services: [defaults.services[0]!, ...services] }
}

/** The streams the board runs now, in editor form: its own with their live bindings and pause, then the user's. */
export const decodeConnInfo = (data: Uint8Array, known: readonly ConnectorSettings[]): ConnectorSettings[] => {
  const connectors = boardConnectors()
  let at = 1
  for (let n = 0; n < (data[0] ?? 0); n++) {
    const [id, stream, flags, count] = [data[at]!, data[at + 1]!, data[at + 2]!, data[at + 3]!]
    at += 4
    const bindings: { provider: 'BLE' | 'UART'; endpoint: number }[] = []
    for (let b = 0; b < count; b++) {
      const provider = providerName(data[at]!)
      const endpoint = u32(data, at + 1)
      at += 5
      if (provider) bindings.push({ provider, endpoint })
    }
    const [name, next] = nameAt(data, at)
    at = next
    let connector = connectors.find((entry) => entry.id === id)
    if (!connector) {
      const old = known.find((entry) => !entry.system && entry.id === id)
      connector = { id, key: `connector_${id}`, name, alias: old?.alias ?? name, stream: hex(stream, 2), system: false, description: old?.description ?? '', isSuspended: false, bindings: [] }
      connectors.push(connector)
    }
    const target = connector
    target.isSuspended = (flags & 2) !== 0
    target.bindings = bindings.map(({ provider, endpoint }) => {
      const text = provider === 'UART' ? uartEndpointText(endpoint) : hex(endpoint, 4)
      return target.bindings.find((entry) => entry.provider === provider && entry.endpoint === text) ?? { id: `b_${target.key}_${provider.toLowerCase()}_${text}`, provider, endpoint: text }
    })
  }
  return connectors
}
