import type { ServiceCatalog } from '../catalog/services'
import { decodeReturns, serviceCall } from '../protocol/services'
import type { RunitLink } from './link'

/*
 * Flashing the board on the link of the board the app is on (SYS_FLASHER.MD): START, then STATUS until it ends. That board
 * writes its own firmware into the other one through its ROM bootloader; the app sends nothing but these. The new board says
 * hello with id 0 on the link and is a slave of the link's board from then on: PEERS lists it, commands reach it with
 * `board: 0` (CommandOptions), and its id is set like any board's (SESSION ID, then a restart).
 */

const SERVICE = 'FLASHER'

/** `sys_flasher_state_e`. */
export const FLASHER_STATE = { idle: 0, running: 1, done: 2, failed: 3 } as const

/** Where a flashing is (`sys_flasher_status_reply_t`). */
export interface FlasherStatus {
  /** `sys_flasher_state_e`: FLASHER_STATE. */
  readonly state: number
  /** `sys_flasher_step_e`: 0 connect, 1 bootloader, 2 erase, 3 firmware, 4 restart. */
  readonly step: number
  readonly done: number
  readonly total: number
  /** When it failed: the tag of its error (its record is on the errors stream); else 0. */
  readonly error: number
}

/** A flashing that ended FAILED. */
export class FlasherFailed extends Error {
  constructor(readonly status: FlasherStatus) {
    super(`Flashing the attached board failed (error 0x${status.error.toString(16).padStart(8, '0')}, step ${status.step}).`)
    this.name = 'FlasherFailed'
  }
}

export const flasherStatus = async (link: RunitLink, catalog: ServiceCatalog): Promise<FlasherStatus> => {
  const call = serviceCall(catalog, SERVICE, 'STATUS')
  const data = await link.request(call.request, { label: 'FLASHER STATUS' })
  const f = decodeReturns(call.command.read!.returns!, data) as unknown as Record<keyof FlasherStatus, number>
  return { state: Number(f.state), step: Number(f.step), done: f.done, total: f.total, error: f.error }
}

export interface FlashOptions {
  /** Each status read while it runs. */
  readonly onProgress?: (status: FlasherStatus) => void
  /** Between two reads (default 500 ms). */
  readonly pollMs?: number
  /** It gives up after this (default 3 min: a 2 MB firmware takes well under a minute at 921600). */
  readonly timeoutMs?: number
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms))

/** Flashes the board on the link and waits for the end: the last status (DONE), or FlasherFailed. */
export const flashAttached = async (link: RunitLink, catalog: ServiceCatalog, options: FlashOptions = {}): Promise<FlasherStatus> => {
  const pollMs = options.pollMs ?? 500
  const timeoutMs = options.timeoutMs ?? 180_000
  await link.request(serviceCall(catalog, SERVICE, 'START').request, { label: 'FLASHER START' })
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    await sleep(pollMs)
    const status = await flasherStatus(link, catalog)
    options.onProgress?.(status)
    if (status.state === FLASHER_STATE.done) return status
    if (status.state === FLASHER_STATE.failed) throw new FlasherFailed(status)
  }
  throw new Error(`Flashing the attached board did not end in ${timeoutMs} ms.`)
}
