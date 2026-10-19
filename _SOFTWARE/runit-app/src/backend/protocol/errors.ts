import type { ErrorField, ErrorInfo, LinkDefinitions } from '../definitions'

/*
 * The error record of an ERROR answer, and of every record on the errors stream (ERRORS.MD "Error record"):
 *
 *   [src u8]   0 internal (no request), 1 command
 *   [time u64 LE]  the board's clock (UTC ms) when it was reported; before 2020 the clock was never set: a time since the start
 *   src 1: [link u8][seq u8][flags u8][req_len u8: bits 0-6 length, bit 7 cut][request]
 *   [chain]    [count u8: bits 0-6, bit 7 cut] then per node [tag u32][len u8][payload]; a node's severity is its tag's (errors.json)
 *
 * The chain starts with the route (one `ERR_ROUTER_KEY` per level) and ends with the root cause.
 */

export interface ErrorNode {
  readonly tag: number
  readonly severity: number
  readonly payload: Uint8Array
  /** From errors.json, when the firmware publishes the tag. */
  readonly info?: ErrorInfo
  /** The payload's fields by name, when the tag is published. */
  readonly fields?: Readonly<Record<string, number | string>>
}

/** A board clock reading from this is a date (a client set it); before it, a time since the start. The same bound as the firmware's `synced`. */
export const CLOCK_SET_AFTER_MS = 1_577_836_800_000

export interface ErrorRecord {
  readonly source: 'internal' | 'command'
  /** The board's clock when it reported the error (ms): UTC when it is at least CLOCK_SET_AFTER_MS, else since the start. */
  readonly time: number
  readonly command?: { readonly link: number; readonly seq: number; readonly flags: number; readonly request: Uint8Array; readonly requestCut: boolean }
  readonly chain: readonly ErrorNode[]
  readonly chainCut: boolean
  /** The root cause: the last node. */
  readonly cause?: ErrorNode
}

const readField = (view: DataView, field: ErrorField): number | string | undefined => {
  if (field.offset + field.size > view.byteLength) return undefined
  switch (field.type) {
    case 'u8': return view.getUint8(field.offset)
    case 'i8': return view.getInt8(field.offset)
    case 'u16': return view.getUint16(field.offset, true)
    case 'i16': return view.getInt16(field.offset, true)
    case 'u32': return view.getUint32(field.offset, true)
    case 'i32': return view.getInt32(field.offset, true)
    case 'f32': return view.getFloat32(field.offset, true)
    case 'chars': return new TextDecoder().decode(new Uint8Array(view.buffer, view.byteOffset + field.offset, field.size)).replace(/\0[\s\S]*$/, '')
    default: return undefined
  }
}

const fieldsOf = (info: ErrorInfo, payload: Uint8Array): Record<string, number | string> => {
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const out: Record<string, number | string> = {}
  for (const field of info.fields) {
    const value = readField(view, field)
    if (value !== undefined) out[field.name] = value
  }
  return out
}

/** The record, or undefined when the bytes are not one (cut short). A tag the catalog does not know keeps its raw payload. */
export const decodeErrorRecord = (defs: LinkDefinitions, data: Uint8Array, catalog?: ReadonlyMap<number, ErrorInfo>): ErrorRecord | undefined => {
  if (data.length < 1) return undefined
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  let at = 0
  const source = data[at++] === defs.errorRecord.srcCommand ? 'command' : 'internal'
  if (data.length < at + defs.errorRecord.stampBytes) return undefined
  const stamp = view.getBigUint64(at, true)
  at += defs.errorRecord.stampBytes
  const time = Number(stamp)
  let command: ErrorRecord['command']
  if (source === 'command') {
    if (data.length < at + 4) return undefined
    const lengthByte = data[at + 3]!
    const length = lengthByte & 0x7f
    const header = { link: data[at]!, seq: data[at + 1]!, flags: data[at + 2]! }
    at += 4
    if (data.length < at + length) return undefined
    command = { ...header, request: data.slice(at, at + length), requestCut: (lengthByte & defs.errorRecord.requestCut) !== 0 }
    at += length
  }
  if (data.length < at + 1) return undefined
  const countByte = data[at++]!
  const chain: ErrorNode[] = []
  for (let i = 0; i < (countByte & 0x7f); i++) {
    if (data.length < at + 5) return undefined
    const tag = view.getUint32(at, true)
    const length = data[at + 4]!
    at += 5
    if (data.length < at + length) return undefined
    const payload = data.slice(at, at + length)
    at += length
    const info = catalog?.get(tag)
    const severity = info?.severity ?? 0
    chain.push({ tag, severity, payload, ...(info ? { info, fields: fieldsOf(info, payload) } : {}) })
  }
  const cause = chain.at(-1)
  return { source, time, ...(command ? { command } : {}), chain, chainCut: (countByte & 0x80) !== 0, ...(cause ? { cause } : {}) }
}
