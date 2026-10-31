import type { LinkDefinitions } from '../definitions'
import type { RunitLink } from './link'
import { sendInOrder } from './send'
import type { SendOptions } from './send'

/*
 * The project on the board (SYS_PROJECT.MD, SYS_ACTIONS.MD): stored records and the whole project as one stream.
 *
 *   records   a list of router frames kept in NVS under a u32 id: the user's actions (class 0), the settings of the system
 *             modules (0x01…), a device's install and default commands (0x02000000 | device id). Uploaded in a session
 *             (BEGIN, WRITE, COMMIT with the CRC), read back frame by frame.
 *   project   settings, devices and the program saved as one stream in sections; the board stores each section as its record,
 *             cuts the program into chunk records and writes the project record that runs them in order. The program is the
 *             compiled VM frames (`[vm key][dir][op][payload]`, writes all); the start section (the EXEC that starts it) is
 *             kept apart: LOAD leaves it out, RUN runs it. At a boot and right after a save the board does what the boot
 *             option says (`defs.projectBoot`: nothing, load, run).
 *
 * A frame here is what the router takes: keys and payload, no `seq` / `flags`.
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

/** Frames back to back, each `[u16 len][frame]`: the image of a record and the body of a project stream. */
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

const u32 = (value: number): Uint8Array => Uint8Array.of(value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff)
const readU32 = (bytes: Uint8Array, at: number): number => (bytes[at]! | (bytes[at + 1]! << 8) | (bytes[at + 2]! << 16) | (bytes[at + 3]! << 24)) >>> 0

const concat = (...parts: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((sum, p) => sum + p.length, 0))
  let at = 0
  for (const part of parts) {
    out.set(part, at)
    at += part.length
  }
  return out
}

/* ---- ids ---- */

/** The id of a device's install and default commands. */
export const deviceRecordId = (device: number): number => (0x02000000 | device) >>> 0
/** The project record; the section id of the program is 0, of the start 0xFFFFFFFF. */
export const PROJECT_RECORD_ID = 0x03000000
export const SECTION_PROGRAM = 0
export const SECTION_START = 0xffffffff
/** A record of a project bank (`SYS_PROJECT_ID`): class PROJECT, `[bank + 1 u8][n u16]`; n 0xFFFF is the start section. */
export const projectBankRecordId = (bank: number, n: number): number => (PROJECT_RECORD_ID | ((bank + 1) << 16) | n) >>> 0
const START_N = 0xffff

/* ---- frames ---- */

/** A frame for the VM router (a program frame, a write): `[vm key][dir][op][payload]`. */
export const vmFrame = (defs: LinkDefinitions, op: number, payload: Uint8Array = new Uint8Array()): Uint8Array => concat(Uint8Array.of(defs.keys.vm!, defs.dir.write, op), payload)

/** A write for the device router: `[device key][dir][id u16][op u16][args]` (op is `(contract id << 8) | n`). */
export const deviceFrame = (defs: LinkDefinitions, device: number, op: number, args: Uint8Array = new Uint8Array()): Uint8Array =>
  concat(Uint8Array.of(defs.keys.device!, defs.dir.write, device & 0xff, device >> 8, op & 0xff, op >> 8), args)

/** A program frame back into `{ op, payload }` (what the engine reads); undefined when it is not a write for the VM router. */
export const programFrameOf = (defs: LinkDefinitions, frame: Uint8Array): { readonly op: number; readonly payload: Uint8Array } | undefined =>
  frame.length >= 3 && frame[0] === defs.keys.vm && frame[1] === defs.dir.write ? { op: frame[2]!, payload: frame.slice(3) } : undefined

/* ---- the stream ---- */

export interface Section {
  /** The record id: a setting (0x01…), a device (`deviceRecordId`) or a user action. Not 0: that is the program. */
  readonly id: number
  readonly frames: readonly Uint8Array[]
}

export interface ProjectContent {
  /** In the order they run: settings, then devices, then the user's actions. */
  readonly sections?: readonly Section[]
  /** The compiled program (`vmFrame`s), after the sections. */
  readonly program?: readonly Uint8Array[]
  /** What starts it (the VM's EXEC), last in the stream: RUN runs it, LOAD leaves it out. */
  readonly start?: readonly Uint8Array[]
}

const sectionFrame = (defs: LinkDefinitions, id: number): Uint8Array => concat(Uint8Array.of(defs.keys.project!, defs.dir.write, defs.ops.project.SECTION!), u32(id))

/** The frames of the stream, section frames included. */
export const projectFrames = (defs: LinkDefinitions, content: ProjectContent): Uint8Array[] => {
  const frames: Uint8Array[] = []
  for (const section of content.sections ?? []) {
    if (section.id === SECTION_PROGRAM || section.id === SECTION_START) throw new RangeError('Sections 0 and 0xFFFFFFFF are the program and the start: give them as `program` and `start`.')
    frames.push(sectionFrame(defs, section.id), ...section.frames)
  }
  if (content.program && content.program.length > 0) frames.push(sectionFrame(defs, SECTION_PROGRAM), ...content.program)
  if (content.start && content.start.length > 0) frames.push(sectionFrame(defs, SECTION_START), ...content.start)
  return frames
}

/** The stream the board is sent. */
export const projectStream = (defs: LinkDefinitions, content: ProjectContent): Uint8Array => frameImage(projectFrames(defs, content))

/* ---- the project router ---- */

export interface ProjectInfo {
  readonly stored: boolean
  /** The boot option: a value of `defs.projectBoot`. */
  readonly boot: number
  readonly bank: number
  /** CRC and length of the stream the stored project was saved from: compare them with a project's `projectStream`. */
  readonly crc: number
  readonly bytes: number
}

export interface SaveOptions extends SendOptions {
  /** The boot option (`defs.projectBoot`, default none): kept, and done right after the save as at a boot. */
  readonly boot?: number
}

const project = (link: RunitLink, op: string, payload?: Uint8Array) =>
  link.request({ keys: [link.defs.keys.project!, link.defs.opDirs.project[op]!, link.defs.ops.project[op]!], ...(payload ? { payload } : {}) }, { timeoutMs: 10_000, label: `project ${op}` })

/** Saves a project: BEGIN, the stream in WRITEs, COMMIT with its CRC. Resolves with the stream's length and CRC once the board stored it. */
export const saveProject = async (link: RunitLink, content: ProjectContent, options: SaveOptions = {}): Promise<{ readonly bytes: number; readonly crc: number }> => {
  const defs = link.defs
  const stream = projectStream(defs, content)
  const crc = crc32(stream)
  await project(link, 'BEGIN', concat(u32(stream.length), Uint8Array.of(options.boot ?? defs.projectBoot.none)))
  const chunk = defs.frameMax - defs.frameHead - 3 - 4 // the frame's header, [key][dir][op] and the offset
  const writes = []
  for (let at = 0; at < stream.length; at += chunk) {
    writes.push({ keys: [defs.keys.project!, defs.dir.write, defs.ops.project.WRITE!], payload: concat(u32(at), stream.subarray(at, at + chunk)) })
  }
  await sendInOrder(link, writes, { window: options.window, timeoutMs: options.timeoutMs ?? 10_000, onProgress: options.onProgress })
  await project(link, 'COMMIT', u32(crc))
  return { bytes: stream.length, crc }
}

export const projectInfo = async (link: RunitLink): Promise<ProjectInfo> => {
  const data = await project(link, 'INFO')
  return { stored: data[0] === 1, boot: data[1]!, bank: data[2]!, crc: readU32(data, 3), bytes: readU32(data, 7) }
}

/** Loads the stored project and runs its start section. */
export const runProject = async (link: RunitLink): Promise<void> => void (await project(link, 'RUN'))
/** Loads the stored project without its start section: the program stays stopped. */
export const loadProject = async (link: RunitLink): Promise<void> => void (await project(link, 'LOAD'))
export const eraseProject = async (link: RunitLink): Promise<void> => void (await project(link, 'ERASE'))
/** Sets the boot option (`defs.projectBoot`). */
export const setProjectBoot = async (link: RunitLink, boot: number): Promise<void> => void (await project(link, 'OPTIONS', Uint8Array.of(boot)))

/* ---- records ---- */

const action = (link: RunitLink, op: string, payload?: Uint8Array, timeoutMs = 5000) =>
  link.request({ keys: [link.defs.keys.actions!, link.defs.opDirs.actions[op]!, link.defs.ops.actions[op]!], ...(payload ? { payload } : {}) }, { timeoutMs, label: `actions ${op}` })

export interface RecordInfo {
  readonly bytes: number
  /** Frames in the record. */
  readonly count: number
  readonly crc: number
}

/** Uploads a record (BEGIN, WRITE, COMMIT with the CRC), replacing the one with this id. */
export const storeRecord = async (link: RunitLink, id: number, frames: readonly Uint8Array[], options: SendOptions = {}): Promise<void> => {
  const defs = link.defs
  const image = frameImage(frames)
  await action(link, 'BEGIN', concat(u32(id), Uint8Array.of(image.length & 0xff, image.length >> 8)))
  const chunk = defs.frameMax - defs.frameHead - 3 - 2 // the frame's header, [key][dir][op] and the offset
  const writes = []
  for (let at = 0; at < image.length; at += chunk) writes.push({ keys: [defs.keys.actions!, defs.dir.write, defs.ops.actions.WRITE!], payload: concat(Uint8Array.of(at & 0xff, at >> 8), image.subarray(at, at + chunk)) })
  await sendInOrder(link, writes, options)
  await action(link, 'COMMIT', u32(crc32(image)))
}

export const recordInfo = async (link: RunitLink, id: number): Promise<RecordInfo> => {
  const data = await action(link, 'INFO', u32(id))
  return { bytes: data[0]! | (data[1]! << 8), count: data[2]! | (data[3]! << 8), crc: readU32(data, 4) }
}

/**
 * The frames of a record, as stored: READ answers the image from an offset, as many bytes as fit, so the image is read from 0
 * up to INFO's `bytes`, checked against its CRC and split into its frames.
 */
export const readRecord = async (link: RunitLink, id: number): Promise<Uint8Array[]> => {
  const info = await recordInfo(link, id)
  const image = new Uint8Array(info.bytes)
  for (let at = 0; at < info.bytes; ) {
    const data = await action(link, 'READ', concat(u32(id), Uint8Array.of(at & 0xff, at >> 8)), 10_000)
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
export const listRecords = async (link: RunitLink): Promise<number[]> => {
  const ids: number[] = []
  for (let start = 0; ; ) {
    const data = await action(link, 'LIST', u32(start))
    const total = data[0]! | (data[1]! << 8)
    const got = Array.from({ length: (data.length - 2) >> 2 }, (_, i) => readU32(data, 2 + 4 * i))
    ids.push(...got)
    if (got.length === 0 || ids.length >= total) return ids
    start = got[got.length - 1]! + 1
  }
}

export const runRecord = async (link: RunitLink, id: number, keepGoing = false): Promise<void> => void (await action(link, keepGoing ? 'RUN_ALL' : 'RUN', u32(id)))
export const removeRecord = async (link: RunitLink, id: number): Promise<void> => void (await action(link, 'REMOVE', u32(id)))

/* ---- reading the project back ---- */

export interface StoredProject {
  readonly info: ProjectInfo
  /** The records the project runs before its program, in order, as stored. */
  readonly sections: readonly Section[]
  /** The program: its frames in order (`programFrameOf` reads them for the engine). */
  readonly program: readonly Uint8Array[]
  /** The start section's frames; empty when it has none. */
  readonly start: readonly Uint8Array[]
  /** The CRC of the stream rebuilt from what was read equals the one the board saved: nothing was lost on the way. */
  readonly verified: boolean
}

/**
 * Reads the stored project back through the actions router: the project record names the sections and the list of program
 * chunks; each is a record of its own. Undefined when no project is stored.
 */
export const readProject = async (link: RunitLink): Promise<StoredProject | undefined> => {
  const defs = link.defs
  const info = await projectInfo(link)
  if (!info.stored) return undefined
  const sections: Section[] = []
  const program: Uint8Array[] = []
  for (const frame of await readRecord(link, PROJECT_RECORD_ID)) {
    if (frame[0] !== defs.keys.actions || frame[1] !== defs.dir.write || frame.length < 7) continue // the MARK frame: nothing to run
    const id = readU32(frame, 3)
    if (frame[2] === defs.ops.actions.RUN_ALL) sections.push({ id, frames: await readRecord(link, id) })
    else if (frame[2] === defs.ops.actions.RUN) {
      for (const run of await readRecord(link, id)) if (run[0] === defs.keys.actions && run[1] === defs.dir.write && run[2] === defs.ops.actions.RUN) program.push(...(await readRecord(link, readU32(run, 3))))
    }
  }
  const start = await readRecord(link, projectBankRecordId(info.bank, START_N)).catch(() => [] as Uint8Array[]) // none: not found
  const verified = crc32(projectStream(defs, { sections, program, start })) === info.crc
  return { info, sections, program, start, verified }
}
