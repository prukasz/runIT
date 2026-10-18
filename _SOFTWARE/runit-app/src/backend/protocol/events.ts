import { decodeFields, PayloadError } from '../catalog/codec'
import type { CatalogEntry, EventDef, ServiceCatalog } from '../catalog/services'
import type { LinkDefinitions } from '../definitions'

/*
 * An event the board sends on the events stream (SYS_EVENT.MD "Publishing"): `[event key][NOTIFY][sys_event_t]`, the command
 * shape. The layout of `sys_event_t` is the EVENT contract's (`contract_event.json`: the arguments of ON_EVENT, which take the
 * same struct), the event's name and meaning its contract's `events` table (the ID is `[contract u8][n u8]`, like an op).
 * Only NOTIFY events come here: the ones that matter to the user (a fault, a device suspended).
 */

/** The EVENT contract's command whose arguments are `sys_event_t` (`contract_event.json`); `events.test.ts` holds it. */
export const EVENT_LAYOUT = { contract: 'EVENT', command: 'ON_EVENT' } as const

export interface BoardEvent {
  /** The device that published it; `SYS_DEVICE_ID_NONE` for every device (a sweep). */
  readonly source: number
  /** The event ID, `[contract u8][n u8]`. */
  readonly code: number
  /** The contract it belongs to, and its row of that contract's `events` table; undefined when no descriptor has it. */
  readonly contract?: CatalogEntry
  readonly event?: EventDef
  readonly channel: number
  readonly flags: number
  readonly value: number
  /** The board's uptime when it was published. */
  readonly tMs: number
}

/** A frame of the events stream read; undefined when it is not a NOTIFY event or does not fit `sys_event_t`. */
export const decodeEvent = (defs: LinkDefinitions, catalog: ServiceCatalog, data: Uint8Array): BoardEvent | undefined => {
  if (data.length < 2 || data[0] !== defs.keys.event || data[1] !== defs.eventNotify) return undefined
  const layout = catalog.contracts.get(EVENT_LAYOUT.contract)?.commands.get(EVENT_LAYOUT.command)?.args
  if (!layout) return undefined
  let f: Readonly<Record<string, unknown>>
  try {
    f = decodeFields(layout, data.subarray(2))
  } catch (error) {
    if (error instanceof PayloadError) return undefined
    throw error
  }
  const code = Number(f.event)
  const contract = catalog.byContractId.get(code >> 8)
  const event = contract?.events.find((e) => e.code === code)
  return { source: Number(f.source), code, ...(contract ? { contract } : {}), ...(event ? { event } : {}), channel: Number(f.channel), flags: Number(f.flags), value: Number(f.value), tMs: Number(f.t_ms) }
}
