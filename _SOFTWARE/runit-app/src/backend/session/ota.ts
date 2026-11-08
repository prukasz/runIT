import type { ServiceCatalog } from '../catalog/services'
import { decodeReturns, serviceCall } from '../protocol/services'
import type { RunitLink } from './link'
import { sendInOrder } from './send'

/*
 * The firmware update (SYS_OTA.MD): the image into the board's other slot, then a restart. The new firmware stays once a client
 * confirms it after its session's hello; a board that restarts before that goes back to the old one.
 */

const SERVICE = 'OTA'
const TIMEOUT_MS = 15_000

export interface OtaInfo {
  readonly slot: number
  /** `sys_ota_state_e`: 0 flashed by cable, 1 confirmed, 2 not confirmed yet. */
  readonly state: number
  readonly open: boolean
  readonly written: number
  readonly slotBytes: number
}

const PENDING = 2

/** The board a call goes to: a slave of the link's board (through it), else the link's own. */
export interface OtaOptions {
  readonly board?: number
}

const to = (options: OtaOptions) => (options.board !== undefined ? { board: options.board } : {})

export const otaInfo = async (link: RunitLink, catalog: ServiceCatalog, options: OtaOptions = {}): Promise<OtaInfo> => {
  const call = serviceCall(catalog, SERVICE, 'INFO')
  const data = await link.request(call.request, { label: 'OTA INFO', ...to(options) })
  const fields = decodeReturns(call.command.read!.returns!, data) as unknown as { slot: number; state: number; open: boolean; written: number; slot_bytes: number }
  return { slot: fields.slot, state: Number(fields.state), open: Boolean(fields.open), written: fields.written, slotBytes: fields.slot_bytes }
}

/** Keeps the firmware that runs, when it still is to be confirmed. Once the session is up: that is what proves it works. */
export const confirmFirmware = async (link: RunitLink, catalog: ServiceCatalog, options: OtaOptions = {}): Promise<boolean> => {
  if ((await otaInfo(link, catalog, options)).state !== PENDING) return false
  await link.request(serviceCall(catalog, SERVICE, 'CONFIRM').request, { label: 'OTA CONFIRM', ...to(options) })
  return true
}

/**
 * Writes `image` into the other slot and makes it what the next boot starts. The program is unloaded first; the caller restarts
 * the board (`SYS RESTART`), and the first session after it confirms the firmware. `board`: a slave of the link's board, whose
 * frames the link's board passes on (the same firmware runs on every board).
 */
export const writeFirmware = async (link: RunitLink, catalog: ServiceCatalog, image: Uint8Array, options: OtaOptions & { readonly onProgress?: (done: number, total: number) => void } = {}): Promise<void> => {
  const chunk = link.defs.frameMax - link.defs.frameHead - 3 - 4 // the frame's header, [key][dir][op] and the offset
  await link.request(serviceCall(catalog, 'VM', 'RESET').request, { timeoutMs: TIMEOUT_MS, label: 'VM RESET', ...to(options) })
  await link.request(serviceCall(catalog, SERVICE, 'BEGIN', { size: image.length }).request, { timeoutMs: TIMEOUT_MS, label: 'OTA BEGIN', ...to(options) })
  const writes = []
  for (let offset = 0; offset < image.length; offset += chunk) {
    writes.push(serviceCall(catalog, SERVICE, 'WRITE', { offset, data: image.subarray(offset, offset + chunk) }).request)
  }
  await sendInOrder(link, writes, { timeoutMs: TIMEOUT_MS, ...(options.onProgress ? { onProgress: options.onProgress } : {}), ...to(options) })
  await link.request(serviceCall(catalog, SERVICE, 'COMMIT').request, { timeoutMs: TIMEOUT_MS, label: 'OTA COMMIT', ...to(options) })
}
