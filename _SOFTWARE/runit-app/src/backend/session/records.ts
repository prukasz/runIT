import type { ServiceCatalog } from '../catalog/services'
import { serviceCall } from '../protocol/services'
import type { RunitLink } from './link'

/*
 * The records of the board (SYS_ACTIONS.MD): a list of router frames kept in NVS under a u32 id, written whole (BEGIN, WRITE,
 * COMMIT with the image's CRC) and read back from an offset. The user's actions (class 0), the settings of the system modules
 * (0x01…), a device's install and default commands (0x02000000 | device id) and the board's profile (0x04000000) are records.
 *
 * A frame here is what the router takes: keys and payload, no `seq` / `flags` (`requestFrame`). The ops are the ACTIONS service's
 * (`contract_actions.json`): none is numbered here.
 */

const CRC_TABLE = (() => {
  const table = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    table[n] = c >>> 0
  }
  return table
})()

/** CRC-32 as zlib and the firmware (`esp_rom_crc32_le(0, …)`) compute it. */
export const crc32 = (bytes: Uint8Array): number => {
  let crc = 0xffffffff
  for (const b of bytes) crc = CRC_TABLE[(crc ^ b) & 0xff]! ^ (crc >>> 8)
  return (crc ^ 0xffffffff) >>> 0
}

/** Frames back to back, each `[u16 len][frame]`: the image of a record. */
export const frameImage = (frames: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(frames.reduce((sum, f) => sum + 2 + f.length, 0))
  let at = 0
  for (const frame of frames) {
    if (frame.length < 1 || frame.length > 0xffff) throw new RangeError(`A frame of ${frame.length} bytes cannot be stored.`)
    out[at] = frame.length & 0xff
    out[at + 1] = frame.length >> 8
    out.set(frame, at + 2)
    at += 2 + frame.length
  }
  return out
}

/** The frames of an image; undefined when it is cut inside a frame. */
export const parseImage = (image: Uint8Array): Uint8Array[] | undefined => {
  const frames: Uint8Array[] = []
  for (let at = 0; at < image.length; ) {
    if (at + 2 > image.length) return undefined
    const length = image[at]! | (image[at + 1]! << 8)
    if (length === 0 || at + 2 + length > image.length) return undefined
    frames.push(image.slice(at + 2, at + 2 + length))
    at += 2 + length
  }
  return frames
}

const readU32 = (bytes: Uint8Array, at: number): number => (bytes[at]! | (bytes[at + 1]! << 8) | (bytes[at + 2]! << 16) | (bytes[at + 3]! << 24)) >>> 0

const SERVICE = 'ACTIONS'
/** The bytes a WRITE of a record's image carries at most: a request holds `frameMax - frameHead`, less `[key][dir][op]` and the u16 offset. */
const writeChunk = (link: RunitLink): number => link.defs.frameMax - link.defs.frameHead - 3 - 2

type Args = Record<string, number | Uint8Array>

const call = (link: RunitLink, catalog: ServiceCatalog, command: string, args?: Args, timeoutMs = 5000): Promise<Uint8Array> =>
  link.request(serviceCall(catalog, SERVICE, command, args as never).request, { timeoutMs, label: `${SERVICE} ${command}` })

export interface RecordInfo {
  /** The image's length. */
  readonly bytes: number
  /** Frames in the record. */
  readonly count: number
  readonly crc: number
}

/** Writes a record (BEGIN, WRITE, COMMIT with the CRC), replacing the one with this id. A frame at a time, each answered before the next. */
export const storeRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number, frames: readonly Uint8Array[]): Promise<void> => {
  const image = frameImage(frames)
  await call(link, catalog, 'BEGIN', { id, len: image.length })
  const chunk = writeChunk(link)
  for (let offset = 0; offset < image.length; offset += chunk) await call(link, catalog, 'WRITE', { offset, data: image.subarray(offset, offset + chunk) })
  await call(link, catalog, 'COMMIT', { crc32: crc32(image) })
}

export const recordInfo = async (link: RunitLink, catalog: ServiceCatalog, id: number): Promise<RecordInfo> => {
  const data = await call(link, catalog, 'INFO', { id })
  return { bytes: data[0]! | (data[1]! << 8), count: data[2]! | (data[3]! << 8), crc: readU32(data, 4) }
}

/**
 * The frames of a record, as stored: READ answers the image from an offset, as many bytes as fit, so the image is read from 0
 * up to INFO's `bytes`, checked against its CRC and split into its frames.
 */
export const readRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number): Promise<Uint8Array[]> => {
  const info = await recordInfo(link, catalog, id)
  const image = new Uint8Array(info.bytes)
  for (let at = 0; at < info.bytes; ) {
    const data = await call(link, catalog, 'READ', { id, offset: at }, 10_000)
    if (data.length === 0 || at + data.length > info.bytes) throw new Error(`Record ${id.toString(16)}: READ at ${at} gave ${data.length} bytes of ${info.bytes}.`)
    image.set(data, at)
    at += data.length
  }
  if (crc32(image) !== info.crc) throw new Error(`Record ${id.toString(16)}: the image read back does not match its CRC.`)
  const frames = parseImage(image)
  if (!frames) throw new Error(`Record ${id.toString(16)}: the image read back is not a list of frames.`)
  return frames
}

/** Every stored record id, ascending. */
export const listRecords = async (link: RunitLink, catalog: ServiceCatalog): Promise<number[]> => {
  const ids: number[] = []
  for (let start = 0; ; ) {
    const data = await call(link, catalog, 'LIST', { start })
    const total = data[0]! | (data[1]! << 8)
    const got = Array.from({ length: (data.length - 2) >> 2 }, (_, i) => readU32(data, 2 + 4 * i))
    ids.push(...got)
    if (got.length === 0 || ids.length >= total) return ids
    start = got[got.length - 1]! + 1
  }
}

/** Runs a record on the board; `keepGoing`: RUN_ALL, every frame runs and the first failure is the answer. */
export const runRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number, keepGoing = false): Promise<void> => void (await call(link, catalog, keepGoing ? 'RUN_ALL' : 'RUN', { id }))
export const removeRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number): Promise<void> => void (await call(link, catalog, 'REMOVE', { id }))
