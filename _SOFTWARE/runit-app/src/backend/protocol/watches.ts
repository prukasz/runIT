import type { FieldValues } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'
import type { Request } from './frames'
import { serviceCall } from './services'
import type { ServiceCall } from './services'

/*
 * The watches of the board (SYS_WATCH.MD): a read the board runs by a trigger and sends by itself after one ADD. The head is
 * `WATCH ADD`'s arguments (`contract_watch.json`); the read follows it, as any request of the catalog: `[key][dir READ][...]`.
 * An entry comes on the watches stream: `[owner u8][id u8][status u8][data]`, the stream frame's `seq` counting the watch's entries.
 */

/** `WATCH ADD`: the head by field id, then the read (`[key][dir READ][...]`, e.g. `requestFrame` of a read call). */
export const watchAddCall = (catalog: ServiceCatalog, head: FieldValues, read: Uint8Array): ServiceCall => {
  const call = serviceCall(catalog, 'WATCH', 'ADD', head)
  const payload = new Uint8Array(call.request.payload!.length + read.length)
  payload.set(call.request.payload!)
  payload.set(read, call.request.payload!.length)
  const request: Request = { ...call.request, payload }
  return { ...call, request }
}

/** `WATCH REMOVE`: the sender's watch with this id. */
export const watchRemoveCall = (catalog: ServiceCatalog, id: number): ServiceCall => serviceCall(catalog, 'WATCH', 'REMOVE', { id })

/** One entry of a watch. */
export interface WatchEntry {
  /** The watch's owner: this client's id. */
  readonly owner: number
  /** The owner's id of the watch. */
  readonly id: number
  /** `true`: the read worked and `data` is its answer; `false`: `data` is the error record (`decodeErrorRecord`). */
  readonly ok: boolean
  readonly data: Uint8Array
}

/** An entry out of a frame of the watches stream (without its stream byte); undefined when it is too short. */
export const decodeWatchEntry = (defs: LinkDefinitions, data: Uint8Array): WatchEntry | undefined => {
  if (data.length < 3) return undefined
  return { owner: data[0]!, id: data[1]!, ok: data[2] === defs.status.ok, data: data.subarray(3) }
}
