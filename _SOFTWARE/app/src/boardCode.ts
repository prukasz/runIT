import { decodeResponseData, packCommand, runitCommandCatalog } from './domain/descriptors'
import type { CommandDescriptor } from './domain/descriptors'
import type { PacketStructValues } from './backend/packetPack'
import { crc32, decodeFrameList, FrameListError } from './domain/storedCode'
import type { RunitBleSession } from './backend/runitBleSession'
import { sendSteps } from './sendSteps'

/*
 * The board's stored code over the command link (class 0x0A,
 * dec_settings_project.h, app/docs/03_records_as_storage.md): info, store
 * (begin, write chunks, commit), read back, options, erase, load (restart).
 */

/** `sys_project_replay_e`. */
export const REPLAY_STATES = ['Not run', 'Replayed', 'Skipped (other firmware)', 'Corrupt'] as const

export interface BoardCodeInfo {
  readonly stored: boolean
  readonly autostart: boolean
  readonly length: number
  readonly frameCount: number
  readonly crc32: number
  /** Schema ID the stored code was built for. */
  readonly schemaId: number
  readonly firmwareSchemaId: number
  readonly capacity: number
  readonly replay: {
    /** Index into REPLAY_STATES. */
    readonly state: number
    readonly applied: number
    readonly failed: number
    /** Frame index of the first refusal, undefined when none. */
    readonly firstFailed?: number
    readonly firstTag: number
    readonly firstOwner: number
    /** CRC of the code this boot replayed (0: none). */
    readonly crc32: number
  }
}

const commands = runitCommandCatalog()
const command = (name: string): CommandDescriptor => {
  const found = commands.get(`packet_settings_project_${name}_t`)
  if (!found) throw new Error(`The firmware descriptors have no stored code packet '${name}'.`)
  return found
}

const call = async (session: RunitBleSession, name: string, values: PacketStructValues = {}, timeoutMs?: number) =>
  session.commands.call({ body: packCommand(command(name), values), label: `code ${name}`, timeoutMs })

/** Most bytes one read answer carries (CONFIG_SYS_INTERFACE_RESPONSE_MAX). */
export const READ_CHUNK_MAX = command('read').request.fields.find((field) => field.name === 'length')?.max ?? 128
/** Bytes of a write command before its data: class, packet, u32 offset. */
export const WRITE_HEADER_BYTES = 6

export const readCodeInfo = async (session: RunitBleSession): Promise<BoardCodeInfo> => {
  const info = command('info')
  const response = await call(session, 'info')
  const decoded = decodeResponseData(info, response.data)
  if (!decoded) throw new Error('The descriptors give no info response layout.')
  const v = (name: string): number => Number(decoded.values[name] ?? 0)
  const firstFailed = v('replay_first_failed')
  return {
    stored: v('stored') !== 0,
    autostart: v('autostart') !== 0,
    length: v('length'),
    frameCount: v('frame_count'),
    crc32: v('crc32') >>> 0,
    schemaId: v('schema_id') >>> 0,
    firmwareSchemaId: v('firmware_schema_id') >>> 0,
    capacity: v('capacity'),
    replay: {
      state: v('replay_state'),
      applied: v('replay_applied'),
      failed: v('replay_failed'),
      ...(firstFailed === 0xffff ? {} : { firstFailed }),
      firstTag: v('replay_first_tag'),
      firstOwner: v('replay_first_owner'),
      crc32: v('replay_crc32') >>> 0,
    },
  }
}

export type Progress = (done: number, total: number) => void

/**
 * Store a frame list: begin, write chunks in order, commit (CRC, schema ID),
 * then info to confirm. `frameBytes` is the longest command the link takes.
 * A refused or cut store leaves the board's old code in place.
 */
export const storeCode = async (session: RunitBleSession, bytes: Uint8Array, schemaId: number, frameBytes: number, onProgress?: Progress): Promise<BoardCodeInfo> => {
  const chunk = frameBytes - WRITE_HEADER_BYTES
  if (chunk < 1) throw new Error(`A ${frameBytes}-byte frame has no room for data.`)
  const write = command('write')
  const steps = [{ label: 'code begin', frame: packCommand(command('begin'), { length: bytes.byteLength }) }]
  for (let offset = 0; offset < bytes.byteLength; offset += chunk) {
    steps.push({ label: `code write @${offset}`, frame: packCommand(write, { offset, data: bytes.subarray(offset, offset + chunk) }) })
  }
  steps.push({ label: 'code commit', frame: packCommand(command('commit'), { crc32: crc32(bytes), schema_id: schemaId }) })
  try {
    await sendSteps(session, steps, onProgress)
  } catch (error) {
    await call(session, 'abort').catch(() => undefined)
    throw error
  }
  const info = await readCodeInfo(session)
  if (!info.stored || info.crc32 !== crc32(bytes) || info.length !== bytes.byteLength) throw new Error('The board reports other code than was stored.')
  return info
}

/**
 * Read the stored frame list back. `until` stops early once a decoded frame
 * matches (the rest is not read; no CRC check then). A full read is checked
 * against the board's CRC.
 */
export const readCode = async (session: RunitBleSession, info: BoardCodeInfo, onProgress?: Progress, until?: (frame: Uint8Array) => boolean): Promise<{ bytes: Uint8Array; complete: boolean }> => {
  if (!info.stored) throw new Error('The board has no stored code.')
  const bytes = new Uint8Array(info.length)
  let offset = 0
  let parsed = 0
  while (offset < info.length) {
    onProgress?.(offset, info.length)
    const length = Math.min(READ_CHUNK_MAX, info.length - offset)
    const response = await call(session, 'read', { offset, length })
    if (response.data.byteLength !== length) throw new Error(`The board sent ${response.data.byteLength} bytes at ${offset}, ${length} asked.`)
    bytes.set(response.data, offset)
    offset += length
    if (until) {
      // Look at the frames that are complete so far.
      const view = new DataView(bytes.buffer)
      while (parsed + 2 <= offset) {
        const frameLength = view.getUint16(parsed, true)
        if (parsed + 2 + frameLength > offset) break
        if (until(bytes.subarray(parsed + 2, parsed + 2 + frameLength))) return { bytes: bytes.slice(0, parsed), complete: false }
        parsed += 2 + frameLength
      }
    }
  }
  onProgress?.(info.length, info.length)
  if (crc32(bytes) !== info.crc32) throw new FrameListError('The code read back fails its CRC; read it again.')
  decodeFrameList(bytes) // framing check
  return { bytes, complete: true }
}

export const setCodeAutostart = async (session: RunitBleSession, autostart: boolean): Promise<void> => {
  await call(session, 'options', { autostart: autostart ? 1 : 0 })
}

export const eraseCode = async (session: RunitBleSession): Promise<void> => {
  await call(session, 'erase')
}

/** Restart the board, which replays the stored code; the link drops right after the answer. */
export const loadCode = async (session: RunitBleSession): Promise<void> => {
  await call(session, 'load')
}
