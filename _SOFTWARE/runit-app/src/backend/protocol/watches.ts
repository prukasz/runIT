import type { FieldValues } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'
import { serviceCall } from './services'
import type { ServiceCall } from './services'

/*
 * The watches of the board (SYS_WATCH.MD): a read the board runs by a trigger and sends by itself after one ADD. The head is
 * `WATCH ADD`'s arguments (`contract_watch.json`); the read follows it, as any request of the catalog: `[key][dir READ][...]`.
 * An entry comes on the watches channel, to the one who added the watch: `[id u8][status u8][data]`.
 */

/** `WATCH ADD`: the head by field id, then the read (`[key][dir READ][...]`, e.g. `requestFrame` of a read call), which is its last field. */
export const watchAddCall = (catalog: ServiceCatalog, head: FieldValues, read: Uint8Array): ServiceCall => serviceCall(catalog, 'WATCH', 'ADD', { ...head, read })

/** `WATCH REMOVE`: the sender's watch with this id. */
export const watchRemoveCall = (catalog: ServiceCatalog, id: number): ServiceCall => serviceCall(catalog, 'WATCH', 'REMOVE', { id })

/** One entry of a watch. */
export interface WatchEntry {
  /** The owner's id of the watch. */
  readonly id: number
  /** `true`: the read worked and `data` is its answer; `false`: `data` is the error record (`decodeErrorRecord`). */
  readonly ok: boolean
  readonly data: Uint8Array
}

/** An entry out of a frame of the watches channel (without its tag); undefined when it is too short. */
export const decodeWatchEntry = (defs: LinkDefinitions, data: Uint8Array): WatchEntry | undefined => {
  if (data.length < 2) return undefined
  return { id: data[0]!, ok: data[1] === defs.status.ok, data: data.subarray(2) }
}
