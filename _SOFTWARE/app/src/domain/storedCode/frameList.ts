import { fromHex, toHex } from '../upload'

/*
 * The stored code as bytes: the frame list the board keeps in its `project`
 * NVS partition and replays at boot (app/docs/03_records_as_storage.md).
 * Same record format as sys_actions: `[u16 len LE][class][packet][payload]`,
 * no seq byte. The CRC is CRC-32 (IEEE 802.3, zlib), what the firmware gets
 * from esp_rom_crc32_le(0, data, len).
 */

export class FrameListError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'FrameListError'
  }
}

export const encodeFrameList = (frames: readonly Uint8Array[]): Uint8Array => {
  const size = frames.reduce((sum, frame) => sum + 2 + frame.byteLength, 0)
  const bytes = new Uint8Array(size)
  const view = new DataView(bytes.buffer)
  let offset = 0
  for (const [index, frame] of frames.entries()) {
    if (frame.byteLength < 2) throw new FrameListError(`Frame ${index} has ${frame.byteLength} bytes; every frame starts with class and packet.`)
    if (frame.byteLength > 0xffff) throw new FrameListError(`Frame ${index} is ${frame.byteLength} bytes, a length prefix holds 65535.`)
    view.setUint16(offset, frame.byteLength, true)
    bytes.set(frame, offset + 2)
    offset += 2 + frame.byteLength
  }
  return bytes
}

export const decodeFrameList = (bytes: Uint8Array): Uint8Array[] => {
  const frames: Uint8Array[] = []
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  let offset = 0
  while (offset < bytes.byteLength) {
    if (offset + 2 > bytes.byteLength) throw new FrameListError(`Length prefix of frame ${frames.length} is cut off at byte ${offset}.`)
    const length = view.getUint16(offset, true)
    if (length < 2 || offset + 2 + length > bytes.byteLength) throw new FrameListError(`Frame ${frames.length} at byte ${offset} claims ${length} bytes, ${bytes.byteLength - offset - 2} left.`)
    frames.push(bytes.slice(offset + 2, offset + 2 + length))
    offset += 2 + length
  }
  return frames
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    table[n] = c >>> 0
  }
  return table
})()

export const crc32 = (bytes: Uint8Array): number => {
  let crc = 0xffffffff
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff]! ^ (crc >>> 8)
  return (crc ^ 0xffffffff) >>> 0
}

// ---------------------------------------------------------------------------
// Export file (inspection only; the project JSON stays the source)
// ---------------------------------------------------------------------------

export const CODE_FORMAT = 'runit-code'
export const CODE_FORMAT_VERSION = 1

export interface StoredCodeStep {
  readonly label: string
  readonly frame: Uint8Array
}

export interface StoredCodeFile {
  /** Error schema ID of the firmware the frames were built for (commit / boot replay check). */
  readonly schemaId: number
  readonly steps: readonly StoredCodeStep[]
}

export const serializeStoredCode = (code: StoredCodeFile, created = new Date()): string => {
  const bytes = encodeFrameList(code.steps.map((step) => step.frame))
  return `${JSON.stringify(
    {
      format: CODE_FORMAT,
      format_version: CODE_FORMAT_VERSION,
      created: created.toISOString(),
      schema_id: `0x${code.schemaId.toString(16).padStart(8, '0').toUpperCase()}`,
      bytes: bytes.byteLength,
      crc32: `0x${crc32(bytes).toString(16).padStart(8, '0').toUpperCase()}`,
      frames: code.steps.map((step) => ({ label: step.label, frame: toHex(step.frame) })),
    },
    null,
    2,
  )}\n`
}

export const parseStoredCode = (text: string): StoredCodeFile => {
  let root: unknown
  try {
    root = JSON.parse(text)
  } catch (error) {
    throw new FrameListError(`not JSON (${error instanceof Error ? error.message : String(error)})`)
  }
  const doc = root as Record<string, unknown> | null
  if (!doc || typeof doc !== 'object' || doc.format !== CODE_FORMAT) throw new FrameListError(`expected format '${CODE_FORMAT}'`)
  if (doc.format_version !== CODE_FORMAT_VERSION) throw new FrameListError(`format_version ${String(doc.format_version)} is not supported`)
  if (typeof doc.schema_id !== 'string' || !/^0x[0-9a-f]{1,8}$/i.test(doc.schema_id)) throw new FrameListError('schema_id: expected a hex number')
  if (!Array.isArray(doc.frames)) throw new FrameListError('frames: expected an array')
  const steps = doc.frames.map((entry: unknown, index): StoredCodeStep => {
    const item = entry as Record<string, unknown> | null
    if (!item || typeof item.frame !== 'string') throw new FrameListError(`frames[${index}]: expected { label, frame }`)
    return { label: typeof item.label === 'string' ? item.label : '', frame: fromHex(item.frame) }
  })
  if (typeof doc.crc32 === 'string') {
    const actual = crc32(encodeFrameList(steps.map((step) => step.frame)))
    if (actual !== Number.parseInt(doc.crc32, 16)) throw new FrameListError(`crc32 ${doc.crc32} doesn't match the frames (0x${actual.toString(16).toUpperCase()}).`)
  }
  return { schemaId: Number.parseInt(doc.schema_id, 16), steps }
}
