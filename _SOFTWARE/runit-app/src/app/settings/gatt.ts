import { bleWayCall, bundledEnums, decodeAnswer, PROVIDER_COMMANDS, readBleWay, requestFrame, serviceCall } from '@backend'
import type { FieldRecord, LeadAnswer, LinkDefinitions, LiveGattService, ServiceCall, ServiceCatalog } from '@backend'
import type { GattCharacteristic, GattService } from '../project/projectFile'

/*
 * What the project adds to the board's Bluetooth: its ways, each a provider of the BLE type (the backend's `protocol/gatt.ts`:
 * `bleWayCall` from `provider_ble.json`, `readBleWay`, `decodeBleTable`), grouped by the service they are in; the service is
 * only the UUID they share.
 *
 *   record   the frames of the settings record: one PROVIDER CREATE per way (the table is built again from it)
 *   apply    what changes the board now: the project's ways it has removed, then every one created; the board drops the
 *            Bluetooth link for a moment when a client is connected
 *   table    BLE LIST: the table as the board has it, to show it and to compare it with the project
 */

/** The first provider id a way may have: below it are the board's own providers (the console, the runIT Bluetooth pair), which its CREATE would find. */
export const FIRST_WAY_PROVIDER = 0x10

/** The last provider id a way may have. */
export const MAX_WAY_PROVIDER = 254

/** What is wrong with the ways before they go to the board: a provider id of the board's own, one twice, one a link to another board has. */
export const gattProblems = (services: readonly GattService[], links: readonly number[] = []): string[] => {
  const ways = services.flatMap((s) => s.characteristics.map((c) => ({ ...c, service: s.uuid })))
  const hex = (n: number) => `0x${n.toString(16)}`
  return ways.flatMap((w, i) => {
    const label = `Bluetooth way ${w.name || hex(w.chr)}`
    if (w.provider < FIRST_WAY_PROVIDER) return [`${label}: provider ${w.provider} is one of the board's own (below ${hex(FIRST_WAY_PROVIDER)}): give it a higher one.`]
    if (ways.findIndex((o) => o.provider === w.provider) !== i) return [`${label}: provider ${w.provider} is the provider of another way.`]
    if (links.includes(w.provider)) return [`${label}: provider ${w.provider} is the link to a board.`]
    return []
  })
}

const wayOf =(service: number, c: GattCharacteristic) => ({ provider: c.provider, service, chr: c.chr, framed: c.framed, name: c.name })

const removeCall = (catalog: ServiceCatalog, provider: number): ServiceCall => serviceCall(catalog, PROVIDER_COMMANDS.service, PROVIDER_COMMANDS.remove, undefined, 'write', provider)

const build = (defs: LinkDefinitions, catalog: ServiceCatalog, services: readonly GattService[]): ServiceCall[] =>
  services.flatMap((s) => s.characteristics.map((c) => bleWayCall(defs, catalog, wayOf(s.uuid, c))))

/** The ids of the board's providers of the BLE type, from PROVIDER LIST's answer. */
export const bleProviders = (catalog: ServiceCatalog, data: Uint8Array): number[] => {
  const call = serviceCall(catalog, PROVIDER_COMMANDS.service, PROVIDER_COMMANDS.list)
  const ble = catalog.provider('ble').providerType
  return ((decodeAnswer(call, data) as LeadAnswer).rows as readonly FieldRecord[]).filter((r) => r.type === ble).map((r) => Number(r.provider))
}

/** The frames of the BLE record: the project's ways created. None when the project adds none. */
export const gattRecord = (defs: LinkDefinitions, catalog: ServiceCatalog, services: readonly GattService[]): Uint8Array[] =>
  build(defs, catalog, services).map((c) => requestFrame(c.request))

/** The project's ways the board has (`have`: its BLE providers) removed: a CREATE of one that exists is refused, so they go before the record runs. */
export const gattRemoves = (catalog: ServiceCatalog, have: readonly number[], services: readonly GattService[]): ServiceCall[] =>
  services.flatMap((s) => s.characteristics.map((c) => c.provider)).filter((id) => have.includes(id)).map((id) => removeCall(catalog, id))

/** The commands that bring the board to the project's ways: the ones it has removed, then every one created. */
export const gattApply = (defs: LinkDefinitions, catalog: ServiceCatalog, have: readonly number[], services: readonly GattService[]): ServiceCall[] => [
  ...gattRemoves(catalog, have, services),
  ...build(defs, catalog, services),
]

/** The services a stored BLE record builds (its PROVIDER CREATE frames of the BLE type). */
export const gattFromRecord = (defs: LinkDefinitions, catalog: ServiceCatalog, frames: readonly Uint8Array[]): GattService[] => {
  const services: GattService[] = []
  for (const frame of frames) {
    const way = readBleWay(defs, catalog, frame)
    if (!way) continue
    const c: GattCharacteristic = { provider: way.provider, chr: way.chr, framed: way.framed, name: way.name }
    const at = services.findIndex((s) => s.uuid === way.service)
    if (at < 0) services.push({ uuid: way.service, characteristics: [c] })
    else services[at] = { ...services[at]!, characteristics: [...services[at]!.characteristics, c] }
  }
  return services
}

/**
 * The board's own service as the firmware makes it (the group `ble_runit_uuid`: the service first, then its characteristics),
 * shown while the board's table is not read: it is always there, and locked.
 */
export const ownGattService = (): LiveGattService | undefined => {
  const [service, ...characteristics] = bundledEnums().ble_runit_uuid?.members ?? []
  if (!service) return undefined
  return {
    uuid: service.value, primary: true, locked: true,
    characteristics: characteristics.map((c) => ({ uuid: c.value, name: c.alias ?? c.name, write: false, indicate: false, notify: false, framed: false })),
  }
}

/** The characteristic UUIDs of each service, as a comparable text: what the project's ways make and what the board's table has. */
const uuids = (services: readonly { readonly uuid: number; readonly chars: readonly number[] }[]): string =>
  JSON.stringify(services.map((s) => [s.uuid, [...s.chars].sort((a, b) => a - b)]).sort((a, b) => (a[0] as number) - (b[0] as number)))

/** Whether the board's table has what the project's ways make (services and their characteristics by UUID). */
export const sameGatt = (project: readonly GattService[], live: readonly LiveGattService[]): boolean =>
  uuids(project.map((s) => ({ uuid: s.uuid, chars: s.characteristics.map((c) => c.chr) })))
  === uuids(live.filter((s) => !s.locked).map((s) => ({ uuid: s.uuid, chars: s.characteristics.map((c) => c.uuid) })))
