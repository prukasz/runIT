import type { RunitLink } from './link'
import { crc32 } from './project'

/*
 * The project's source on the board (SYS_PROJECT.MD "The source"): the project as the app edits it, kept next to the program
 * so the board gives it back whole, names and all. The firmware stores the bytes and never reads them:
 *
 *   [u8 version][u32 stream CRC LE][u8 name length][name, UTF-8][gzip of the project file]
 *
 * The name can be read from the first chunk alone. The stream CRC is INFO's of the stored project it was written with
 * (0: none stored): it tells when the program on the board is not the one this copy describes.
 */

const VERSION = 1
const HEAD_BYTES = 6 // version, CRC, name length
const NAME_MAX = 255
/** A chunk: fits one frame both ways, and the head and any name are in the first. */
const CHUNK = 400
const TIMEOUT_MS = 10_000

export interface ProjectSource {
  readonly name: string
  /** The project file, as text. */
  readonly text: string
  readonly streamCrc: number
}

interface FileInfo {
  readonly bytes: number
  readonly crc: number
  readonly chunks: number
}

const request = (link: RunitLink, op: string, payload?: Uint8Array) =>
  link.request({ keys: [link.defs.keys.project!, link.defs.opDirs.project[op]!, link.defs.ops.project[op]!], ...(payload ? { payload } : {}) }, { timeoutMs: TIMEOUT_MS, label: `project ${op}` })

const fileInfo = async (link: RunitLink): Promise<FileInfo> => {
  const data = await request(link, 'SOURCE_INFO')
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  return { bytes: view.getUint32(0, true), crc: view.getUint32(4, true), chunks: view.getUint16(8, true) }
}

const readChunk = (link: RunitLink, chunk: number) => request(link, 'SOURCE_READ', Uint8Array.of(chunk & 0xff, chunk >> 8))

const through = async (bytes: Uint8Array, stream: CompressionStream | DecompressionStream): Promise<Uint8Array> =>
  new Uint8Array(await new Response(new Blob([bytes as BlobPart]).stream().pipeThrough(stream)).arrayBuffer())

/** The file's bytes for a project. A name too long is cut (whole characters are the reader's matter: it reads what is valid). */
export const encodeProjectSource = async (source: ProjectSource): Promise<Uint8Array> => {
  const name = new TextEncoder().encode(source.name).subarray(0, NAME_MAX)
  const body = await through(new TextEncoder().encode(source.text), new CompressionStream('gzip'))
  const out = new Uint8Array(HEAD_BYTES + name.length + body.length)
  const view = new DataView(out.buffer)
  out[0] = VERSION
  view.setUint32(1, source.streamCrc, true)
  out[5] = name.length
  out.set(name, HEAD_BYTES)
  out.set(body, HEAD_BYTES + name.length)
  return out
}

const headOf = (bytes: Uint8Array): { readonly name: string; readonly streamCrc: number; readonly bodyAt: number } => {
  if (bytes.length < HEAD_BYTES || bytes[0] !== VERSION) throw new Error(`The board's copy of the project has a format this app does not know (${bytes[0]}).`)
  const nameBytes = bytes[5]!
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  return { name: new TextDecoder().decode(bytes.subarray(HEAD_BYTES, HEAD_BYTES + nameBytes)), streamCrc: view.getUint32(1, true), bodyAt: HEAD_BYTES + nameBytes }
}

export const decodeProjectSource = async (bytes: Uint8Array): Promise<ProjectSource> => {
  const head = headOf(bytes)
  const text = new TextDecoder().decode(await through(bytes.subarray(head.bodyAt), new DecompressionStream('gzip')))
  return { name: head.name, text, streamCrc: head.streamCrc }
}

/** Writes the project's source, in place of the one there, and checks it read back whole by the board's length and CRC. */
export const saveProjectSource = async (link: RunitLink, source: ProjectSource, onProgress?: (done: number, total: number) => void): Promise<void> => {
  const bytes = await encodeProjectSource(source)
  await request(link, 'SOURCE_ERASE')
  const total = Math.ceil(bytes.length / CHUNK)
  for (let offset = 0, done = 0; offset < bytes.length; offset += CHUNK, done++) {
    onProgress?.(done, total)
    const head = Uint8Array.of(offset & 0xff, (offset >>> 8) & 0xff, (offset >>> 16) & 0xff, offset >>> 24)
    const data = bytes.subarray(offset, offset + CHUNK)
    const payload = new Uint8Array(head.length + data.length)
    payload.set(head)
    payload.set(data, head.length)
    await request(link, 'SOURCE_APPEND', payload)
  }
  const info = await fileInfo(link)
  if (info.bytes !== bytes.length || info.crc !== crc32(bytes)) throw new Error('The copy of the project did not reach the board whole: write the project again.')
}

/** The project's source on the board; undefined when it keeps none. */
export const readProjectSource = async (link: RunitLink, onProgress?: (done: number, total: number) => void): Promise<ProjectSource | undefined> => {
  const info = await fileInfo(link)
  if (info.bytes === 0) return undefined
  const bytes = new Uint8Array(info.bytes)
  let at = 0
  for (let chunk = 0; chunk < info.chunks; chunk++) {
    onProgress?.(chunk, info.chunks)
    const data = await readChunk(link, chunk)
    if (at + data.length > bytes.length) throw new Error('The board\'s copy of the project did not read back whole: read it again.')
    bytes.set(data, at)
    at += data.length
  }
  if (at !== info.bytes || crc32(bytes) !== info.crc) throw new Error('The board\'s copy of the project did not read back whole: read it again.')
  return decodeProjectSource(bytes)
}

/** The name of the project the board keeps a copy of, from the first chunk alone; undefined when it keeps none. */
export const projectSourceName = async (link: RunitLink): Promise<string | undefined> => {
  const info = await fileInfo(link)
  return info.bytes === 0 ? undefined : headOf(await readChunk(link, 0)).name
}
