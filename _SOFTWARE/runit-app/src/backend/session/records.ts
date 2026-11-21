import type { FieldRecord } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import { serviceCall } from '../protocol/calls'
import type { CallArgs, LeadAnswer } from '../protocol/calls'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The records of the board (SYS_ACTIONS.MD): a list of stored frames kept in NVS under a u32 id, written whole (BEGIN, WRITE,
 * COMMIT with the image's CRC) and read back from an offset. The user's actions, the settings of the system modules, a device's
 * create and start commands and the board's profile are records.
 *
 * A stored frame is what the router takes, `[key][dir][...]` (`requestFrame`), no header. The commands and their fields are the
 * ACTIONS service's (`contract_actions.json`), named once in `ACTIONS_COMMANDS`.
 */

export const ACTIONS_COMMANDS = {
  service: 'ACTIONS',
  begin: 'BEGIN', write: 'WRITE', commit: 'COMMIT', info: 'INFO', read: 'READ', list: 'LIST', run: 'RUN', runAll: 'RUN_ALL', remove: 'REMOVE',
  field: { id: 'id', len: 'len', offset: 'offset', data: 'data', crc: 'crc32', start: 'start', bytes: 'bytes', count: 'count' },
} as const

const C = ACTIONS_COMMANDS
const F = C.field

const CRC_TABLE = (() => {
  const table = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    table[n] = c >>> 0
  }
  return table
})()

/** CRC-32 as zlib and the firmware (`crc32_ieee(0, …)`, CRC.MD) compute it. */
export const crc32 = (bytes: Uint8Array): number => {
  let crc = 0xffffffff
  for (const b of bytes) crc = CRC_TABLE[(crc ^ b) & 0xff]! ^ (crc >>> 8)
  return (crc ^ 0xffffffff) >>> 0
}

/** Frames back to back, each `[u16 len LE][frame]`: the image of a record (SYS_ACTIONS.MD "Image"). */
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

const call = async (link: RunitLink, catalog: ServiceCatalog, command: string, args?: CallArgs, timeoutMs = 5000) =>
  new Services(link, catalog).send(serviceCall(catalog, C.service, command, args), { timeoutMs })

/** The bytes one WRITE carries at most: what a request's body holds, less the body of a WRITE with no data. */
const writeChunk = (link: RunitLink, catalog: ServiceCatalog): number =>
  link.defs.frameMax - link.defs.frameHead - serviceCall(catalog, C.service, C.write, { [F.offset]: 0, [F.data]: new Uint8Array(0) }).request.body.length

export interface RecordInfo {
  /** The image's length. */
  readonly bytes: number
  /** Frames in the record. */
  readonly count: number
  readonly crc: number
}

/** Writes a record (BEGIN, WRITE, COMMIT with the CRC), replacing the one with this id; each step answered before the next. */
export const storeRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number, frames: readonly Uint8Array[]): Promise<void> => {
  const image = frameImage(frames)
  await call(link, catalog, C.begin, { [F.id]: id, [F.len]: image.length })
  const chunk = writeChunk(link, catalog)
  for (let offset = 0; offset < image.length; offset += chunk) await call(link, catalog, C.write, { [F.offset]: offset, [F.data]: image.subarray(offset, offset + chunk) })
  await call(link, catalog, C.commit, { [F.crc]: crc32(image) })
}

export const recordInfo = async (link: RunitLink, catalog: ServiceCatalog, id: number): Promise<RecordInfo> => {
  const info = (await call(link, catalog, C.info, { [F.id]: id })).value as FieldRecord
  return { bytes: Number(info[F.bytes]), count: Number(info[F.count]), crc: Number(info[F.crc]) }
}

/** The frames of a record as stored: the image read from 0 up to INFO's `bytes`, checked against its CRC and split into its frames. */
export const readRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number): Promise<Uint8Array[]> => {
  const info = await recordInfo(link, catalog, id)
  const image = new Uint8Array(info.bytes)
  for (let at = 0; at < info.bytes; ) {
    const { data } = await call(link, catalog, C.read, { [F.id]: id, [F.offset]: at }, 10_000)
    if (data.length === 0 || at + data.length > info.bytes) throw new Error(`Record ${id.toString(16)}: READ at ${at} gave ${data.length} bytes of ${info.bytes}.`)
    image.set(data, at)
    at += data.length
  }
  if (crc32(image) !== info.crc) throw new Error(`Record ${id.toString(16)}: the image read back does not match its CRC.`)
  const frames = parseImage(image)
  if (!frames) throw new Error(`Record ${id.toString(16)}: the image read back is not a list of frames.`)
  return frames
}

/** Every stored record id, ascending: LIST answers the total and the ids from `start` that fit, so it is asked until all came. */
export const listRecords = async (link: RunitLink, catalog: ServiceCatalog): Promise<number[]> => {
  const ids: number[] = []
  for (let start = 0; ; ) {
    const { lead: total, rows } = (await call(link, catalog, C.list, { [F.start]: start })).value as LeadAnswer
    const got = rows.map(Number)
    ids.push(...got)
    if (got.length === 0 || ids.length >= total) return ids
    start = got[got.length - 1]! + 1
  }
}

/** Runs a record on the board; `keepGoing`: RUN_ALL, every frame runs and the first failure is the answer. */
export const runRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number, keepGoing = false): Promise<void> => void (await call(link, catalog, keepGoing ? C.runAll : C.run, { [F.id]: id }))

export const removeRecord = async (link: RunitLink, catalog: ServiceCatalog, id: number): Promise<void> => void (await call(link, catalog, C.remove, { [F.id]: id }))
