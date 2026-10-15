/*
 * The payload of a command and of its answer, laid out as a descriptor says (`templates/contract.schema.json`, `field`): every field
 * has a `raw_type`, an `offset` (or follows the one before) and a `size` (or the width of its raw type). A `bytes` or `string`
 * field without a size runs to the end of the payload and is the last one; a `string` is NUL-terminated with nothing after the
 * NUL (`cmd_name_arg`, CMD.MD). A number field whose size is a multiple of its raw type's width is an array (`u32` of 16 bytes:
 * `u32[4]`). Integers are little endian, as the firmware's packed structs.
 *
 * Nothing here knows a command: the layouts come from the catalog (`services.ts`).
 */

export const RAW_TYPES = ['u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'u64', 'i64', 'float', 'double', 'bytes', 'string'] as const
export type RawType = (typeof RAW_TYPES)[number]

/** The width of the C type a raw type names; undefined for `bytes` and `string`, whose length is the field's. */
export const rawWidth = (raw: RawType): number | undefined =>
  ({ u8: 1, i8: 1, u16: 2, i16: 2, u32: 4, i32: 4, u64: 8, i64: 8, float: 4, double: 8 })[raw as Exclude<RawType, 'bytes' | 'string'>]

export interface Choice {
  readonly label: string
  readonly value: number | string | boolean
  /** The enum member's name, for an `enum` field. */
  readonly name?: string
  readonly description?: string
}

export interface WireField {
  readonly id: string
  readonly label: string
  readonly description?: string
  /** The field's type for a form (`integer`, `number`, `boolean`, `choice`, `enum`, `pin`, `device`, `bitmask`, `string`). */
  readonly type: string
  readonly rawType: RawType
  readonly offset: number
  /** Undefined: the field runs to the end of the payload. */
  readonly size?: number
  readonly unit?: string
  readonly min?: number
  readonly max?: number
  readonly step?: number
  readonly default?: unknown
  readonly optional: boolean
  /** The values of a `choice` field, or of an `enum` field from its group in `enums.json`. */
  readonly choices?: readonly Choice[]
  readonly enum?: string
  readonly requiresContract?: string
  readonly requires?: string
  readonly pinRef: boolean
}

export interface Layout {
  /** The C struct it is (`struct` in the descriptor). */
  readonly struct?: string
  readonly fields: readonly WireField[]
  /** The bytes of the fixed part; the whole payload unless the last field runs to the end. */
  readonly fixed: number
  /** The last field has no size. */
  readonly variable: boolean
}

export type FieldValue = number | bigint | boolean | string | Uint8Array | readonly (number | bigint | boolean)[]
/** What a caller gives for a field: a number, a boolean, a choice's label or an enum member's name, bytes, a string, a list for an array field. */
export type FieldInput = FieldValue | readonly number[]
export type FieldValues = Readonly<Record<string, FieldInput>>
export type FieldRecord = Readonly<Record<string, FieldValue>>

export class PayloadError extends RangeError {
  constructor(message: string) {
    super(message)
    this.name = 'PayloadError'
  }
}

const RANGE: Partial<Record<RawType, readonly [bigint, bigint]>> = {
  u8: [0n, 0xffn],
  i8: [-0x80n, 0x7fn],
  u16: [0n, 0xffffn],
  i16: [-0x8000n, 0x7fffn],
  u32: [0n, 0xffffffffn],
  i32: [-0x80000000n, 0x7fffffffn],
  u64: [0n, 0xffffffffffffffffn],
  i64: [-0x8000000000000000n, 0x7fffffffffffffffn],
}

const isFloat = (raw: RawType) => raw === 'float' || raw === 'double'

/** A choice's label or an enum member's name (or alias) as its value. */
const choiceValue = (field: WireField, text: string): number | undefined => {
  const choice = field.choices?.find((c) => c.label === text || c.name === text)
  return typeof choice?.value === 'number' ? choice.value : typeof choice?.value === 'boolean' ? Number(choice.value) : undefined
}

const numberOf = (field: WireField, input: FieldInput): number | bigint => {
  if (typeof input === 'boolean') return input ? 1 : 0
  if (typeof input === 'string') {
    const value = choiceValue(field, input)
    if (value === undefined) throw new PayloadError(`'${input}' is not a value of ${field.id}${field.choices ? ` (${field.choices.map((c) => c.name ?? c.label).join(', ')})` : ''}.`)
    return value
  }
  if (typeof input === 'number' || typeof input === 'bigint') return input
  throw new PayloadError(`${field.id} takes a number.`)
}

const writeNumber = (view: DataView, at: number, field: WireField, input: FieldInput): void => {
  const value = numberOf(field, input)
  const raw = field.rawType
  if (isFloat(raw)) {
    const n = Number(value)
    if (field.min !== undefined && n < field.min) throw new PayloadError(`${field.id} = ${n} is below ${field.min}.`)
    if (field.max !== undefined && n > field.max) throw new PayloadError(`${field.id} = ${n} is above ${field.max}.`)
    if (raw === 'float') view.setFloat32(at, n, true)
    else view.setFloat64(at, n, true)
    return
  }
  if (typeof value === 'number' && !Number.isInteger(value)) throw new PayloadError(`${field.id} = ${value} is not an integer (${raw}).`)
  const big = BigInt(value)
  const [lo, hi] = RANGE[raw]!
  if (big < lo || big > hi) throw new PayloadError(`${field.id} = ${big} does not fit ${raw}.`)
  if (field.min !== undefined && big < BigInt(Math.ceil(field.min))) throw new PayloadError(`${field.id} = ${big} is below ${field.min}.`)
  if (field.max !== undefined && big > BigInt(Math.floor(field.max))) throw new PayloadError(`${field.id} = ${big} is above ${field.max}.`)
  const n = Number(big)
  switch (raw) {
    case 'u8': return view.setUint8(at, n)
    case 'i8': return view.setInt8(at, n)
    case 'u16': return view.setUint16(at, n, true)
    case 'i16': return view.setInt16(at, n, true)
    case 'u32': return view.setUint32(at, n, true)
    case 'i32': return view.setInt32(at, n, true)
    case 'u64': return view.setBigUint64(at, big, true)
    case 'i64': return view.setBigInt64(at, big, true)
  }
}

const bytesOf = (field: WireField, input: FieldInput): Uint8Array => {
  if (input instanceof Uint8Array) return input
  if (Array.isArray(input) && input.every((b) => Number.isInteger(b) && b >= 0 && b <= 0xff)) return Uint8Array.from(input)
  throw new PayloadError(`${field.id} takes bytes.`)
}

const textOf = (field: WireField, input: FieldInput): Uint8Array => {
  if (typeof input !== 'string') throw new PayloadError(`${field.id} takes text.`)
  if (input.includes('\0')) throw new PayloadError(`${field.id} cannot hold a NUL.`)
  const text = new TextEncoder().encode(input)
  const out = new Uint8Array(text.length + 1)
  out.set(text)
  return out
}

/** The value a field gets when the caller leaves it out: its default, zero for an optional field; else an error. */
const missing = (field: WireField): FieldInput => {
  if (field.default !== undefined && field.default !== null) return field.default as FieldInput
  if (field.optional) return field.rawType === 'string' ? '' : field.rawType === 'bytes' ? new Uint8Array(field.size ?? 0) : 0
  throw new PayloadError(`${field.id} is missing.`)
}

/** The payload of a layout from the values by field id. A field that is not in the layout is an error (a misspelt id). */
export const encodeFields = (layout: Layout, values: FieldValues = {}): Uint8Array => {
  const known = new Set(layout.fields.map((f) => f.id))
  const unknown = Object.keys(values).filter((id) => !known.has(id))
  if (unknown.length > 0) throw new PayloadError(`No field ${unknown.join(', ')} here (${[...known].join(', ') || 'none'}).`)
  const parts: { at: number; bytes: Uint8Array }[] = []
  const fixed = new Uint8Array(layout.fixed)
  const view = new DataView(fixed.buffer)
  for (const field of layout.fields) {
    const input = values[field.id] ?? missing(field)
    if (field.rawType === 'bytes' || field.rawType === 'string') {
      const bytes = field.rawType === 'bytes' ? bytesOf(field, input) : textOf(field, input)
      if (field.size === undefined) {
        parts.push({ at: field.offset, bytes })
        continue
      }
      if (bytes.length > field.size || (field.rawType === 'bytes' && bytes.length !== field.size)) throw new PayloadError(`${field.id} is ${field.size} bytes, not ${bytes.length}.`)
      fixed.set(bytes, field.offset)
      continue
    }
    const width = rawWidth(field.rawType)!
    const count = (field.size ?? width) / width
    if (count === 1) {
      writeNumber(view, field.offset, field, input)
      continue
    }
    if (!Array.isArray(input) || input.length !== count) throw new PayloadError(`${field.id} takes ${count} values.`)
    input.forEach((item: FieldInput, i: number) => writeNumber(view, field.offset + i * width, field, item))
  }
  if (parts.length === 0) return fixed
  const tail = parts[0]!
  const out = new Uint8Array(tail.at + tail.bytes.length)
  out.set(fixed)
  out.set(tail.bytes, tail.at)
  return out
}

const readNumber = (view: DataView, at: number, raw: RawType): number | bigint => {
  switch (raw) {
    case 'u8': return view.getUint8(at)
    case 'i8': return view.getInt8(at)
    case 'u16': return view.getUint16(at, true)
    case 'i16': return view.getInt16(at, true)
    case 'u32': return view.getUint32(at, true)
    case 'i32': return view.getInt32(at, true)
    case 'float': return view.getFloat32(at, true)
    case 'double': return view.getFloat64(at, true)
    case 'u64': case 'i64': {
      const big = raw === 'u64' ? view.getBigUint64(at, true) : view.getBigInt64(at, true)
      return big >= BigInt(Number.MIN_SAFE_INTEGER) && big <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(big) : big
    }
    default: throw new PayloadError(`${raw} is not a number.`)
  }
}

const readText = (bytes: Uint8Array): string => {
  const end = bytes.indexOf(0)
  return new TextDecoder().decode(end < 0 ? bytes : bytes.subarray(0, end))
}

/** One value of a raw type at `at` (a list when `size` holds several); `boolean` turns a number into true / false. */
export const readValue = (data: Uint8Array, at: number, raw: RawType, size: number | undefined, boolean = false): FieldValue => {
  const end = size === undefined ? data.length : at + size
  if (end > data.length) throw new PayloadError(`The answer is ${data.length} bytes; a ${raw} at ${at} needs ${end}.`)
  if (raw === 'bytes') return data.slice(at, end)
  if (raw === 'string') return readText(data.subarray(at, end))
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  const width = rawWidth(raw)!
  const one = (offset: number) => {
    const value = readNumber(view, offset, raw)
    return boolean ? value !== 0 && value !== 0n : value
  }
  const count = (end - at) / width
  return count === 1 ? one(at) : Array.from({ length: count }, (_, i) => one(at + i * width))
}

/** The fields of a payload by id. */
export const decodeFields = (layout: Layout, data: Uint8Array): FieldRecord => {
  if (data.length < layout.fixed) throw new PayloadError(`The answer is ${data.length} bytes; its layout needs ${layout.fixed}.`)
  return Object.fromEntries(layout.fields.map((f) => [f.id, readValue(data, f.offset, f.rawType, f.size, f.type === 'boolean')]))
}
