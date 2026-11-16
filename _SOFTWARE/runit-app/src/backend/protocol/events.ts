import { decodeFields, PayloadError } from '../catalog/codec'
import type { FieldRecord } from '../catalog/codec'
import type { CatalogEntry, EventDef, ServiceCatalog } from '../catalog/services'

/*
 * An event the board sends on the events stream (SYS_EVENT_STREAM.MD): the `sys_event_t` as it is, `[tag u32][source u16][channel u16]
 * [t_ms u64][flags u16][len u16][payload]`. Its layout is the EVENT contract's (`contract_event.json`: the arguments of the ON_EVENT
 * write, which take the same struct); the event's name and payload its contract's `events` table. The tag is `EVT_TAG(module, code)`: the
 * module's number in the upper 16 bits (the contract's id) and the event's code in the lower. Only the events that matter to the user come
 * here (a fault, a device suspended).
 */

/** The EVENT contract's command whose arguments are `sys_event_t` (`contract_event.json`); `events.test.ts` holds it. */
export const EVENT_LAYOUT = { contract: 'EVENT', command: 'ON_EVENT' } as const

export interface BoardEvent {
  /** What published it, as its module defines it: a device's id for the device contracts; `SYS_DEVICE_ID_NONE` for every device (a sweep). */
  readonly source: number
  /** The event's tag, `EVT_TAG(module, code)`: what a contract's `events` table calls `code`. */
  readonly code: number
  /** The contract it belongs to, and its row of that contract's `events` table; undefined when no descriptor has it. */
  readonly contract?: CatalogEntry
  readonly event?: EventDef
  /** The pin, channel or object on the source; 0 when it has none. */
  readonly channel: number
  readonly flags: number
  /** The event's payload by field id, when its row of the table describes one. */
  readonly payload?: FieldRecord
  /** The board's clock when it was published: UTC ms once a client set it (`TIME`), else the time since the start (before 2020). */
  readonly tMs: number
}

/** A frame of the events stream read; undefined when it does not fit `sys_event_t`. */
export const decodeEvent = (catalog: ServiceCatalog, data: Uint8Array): BoardEvent | undefined => {
  const layout = catalog.contracts.get(EVENT_LAYOUT.contract)?.commands.get(EVENT_LAYOUT.command)?.write?.args
  if (!layout) return undefined
  let f: Readonly<Record<string, unknown>>
  try {
    f = decodeFields(layout, data)
  } catch (error) {
    if (error instanceof PayloadError) return undefined
    throw error
  }
  const code = Number(f.tag)
  const contract = catalog.byContractId.get(code >>> 16)
  const event = contract?.events.find((e) => e.code === code)
  let payload: FieldRecord | undefined
  if (event?.payload && f.data instanceof Uint8Array) {
    try {
      payload = decodeFields(event.payload, f.data)
    } catch (error) {
      if (!(error instanceof PayloadError)) throw error
    }
  }
  return { source: Number(f.source), code, ...(contract ? { contract } : {}), ...(event ? { event } : {}), channel: Number(f.channel), flags: Number(f.flags), ...(payload ? { payload } : {}), tMs: Number(f.t_ms) }
}
