import type { FieldValues } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'
import { serviceCall } from './calls'
import type { ServiceCall } from './calls'

/*
 * The watches of a board (SYS_WATCH.MD): a read the board runs by a trigger and sends by itself after one ADD. The head is
 * `WATCH ADD`'s arguments (`contract_watch.json`); the read follows it as a stored frame, `[key][dir READ][...]`. An entry is a
 * PUSH of the watches' stream, `[owner u8][id u8][status u8][data]`, sent to every port linked from the board's WATCH: the raw
 * telemetry characteristic carries every owner's, so a client keeps the ones whose owner is its own id.
 */

/** The watch service's commands (`contract_watch.json`). */
export const WATCH_COMMANDS = { service: 'WATCH', add: 'ADD', remove: 'REMOVE', read: 'read', id: 'id' } as const

/** `WATCH ADD`: the head by field id, then the read (`requestFrame` of a read call), its last field. */
export const watchAddCall = (catalog: ServiceCatalog, head: FieldValues, read: Uint8Array): ServiceCall =>
  serviceCall(catalog, WATCH_COMMANDS.service, WATCH_COMMANDS.add, { ...head, [WATCH_COMMANDS.read]: read })

/** `WATCH REMOVE`: the sender's watch with this id. */
export const watchRemoveCall = (catalog: ServiceCatalog, id: number): ServiceCall => serviceCall(catalog, WATCH_COMMANDS.service, WATCH_COMMANDS.remove, { [WATCH_COMMANDS.id]: id })

/** One entry of a watch. */
export interface WatchEntry {
  /** The peer that added the watch. */
  readonly owner: number
  /** The owner's id of the watch. */
  readonly id: number
  /** `true`: the read worked and `data` is its answer; `false`: `data` is the error record (`decodeErrorRecord`). */
  readonly ok: boolean
  readonly data: Uint8Array
}

/** An entry out of a PUSH frame's data of the watches' stream; undefined when it is too short. */
export const decodeWatchEntry = (defs: LinkDefinitions, data: Uint8Array): WatchEntry | undefined => {
  if (data.length < 3) return undefined
  return { owner: data[0]!, id: data[1]!, ok: data[2] === defs.status.ok, data: data.subarray(3) }
}
