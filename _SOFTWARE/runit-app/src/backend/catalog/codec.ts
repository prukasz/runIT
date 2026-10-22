/*
 * The payload of a command and of its answer, laid out as a descriptor says (`templates/contract.schema.json`, `field`): every field
 * has a `raw_type`, an `offset` (or follows the one before) and a `size` (or the width of its raw type). A `bytes` or `string`
 * field without a size runs to the end of the payload and is the last one; a `string` is NUL-terminated with nothing after the
 * NUL (`cmd_name_arg`, CMD.MD). A number field whose size is a multiple of its raw type's width is an array (`u32` of 16 bytes:
 * `u32[4]`). Integers are little endian, as the firmware's packed structs. A `pin_ref` field is a `sys_io_pin_t` (or a list of
 * them): `{ device, pin }`, and `null` for an unused pin (`SYS_IO_PIN_NONE`, whose values the catalog takes from `enums.json`).
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

/** A pin of a device (`sys_io_pin_t`, SYS_IO.MD); `null` where a pin is unused. */
export interface PinRef {
  readonly device: number
  readonly pin: number
}

/** `sizeof(sys_io_pin_t)`: u16 device, u8 pin (packed). */
export const PIN_REF_SIZE = 3

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
  /** The wire value is the shown value times `scale` (`scale` of the descriptor): min, max, step and the values are in the shown unit. */
  readonly scale?: number
  /** What the app may draw instead of the field's own control (`control`): a slider for a number with a range, a switch for a choice of two. */
  readonly control?: 'slider' | 'switch'
  readonly default?: unknown
  readonly optional: boolean
  /** The values of a `choice` field, or of an `enum` field from its group in `enums.json`. */
  readonly choices?: readonly Choice[]
  /** The named bits of a `bitmask` field: the only ones the form offers. */
  readonly bits?: readonly { readonly bit: number; readonly label: string }[]
  readonly enum?: string
  /** An array field whose element i stands for the member of value i of this enum group (`index_enum`): its column names. */
  readonly indexEnum?: string
  readonly requiresContract?: string
  readonly requires?: string
  /** A `pin` field of a device command: the states the pin must be in for the command (`pin_state`: unconfigured, configured, input, output, pwm, adc, dac; any of them). */
  readonly pinState?: readonly string[]
  readonly pinRef: boolean
  /** The values of an unused pin, for a `pinRef` field. */
  readonly pinNone?: PinRef
}

/** One entry of a range that follows the device's config: when every field of `when` holds, min / max (a number, or a config field's id, times `scale`). */
export interface RangeByConfig {
  readonly when: Readonly<Record<string, unknown>>
  readonly min: number | string
  readonly max: number | string
  readonly scale?: number
  readonly step?: number
}

/**
 * The range a device gives a field of one of its commands (`ranges` of its descriptor or of its board.json entry), by `COMMAND.field`.
 * `byConfig` (a descriptor's `by_config`) is resolved against one device's config by `rangesForConfig`.
 */
export type FieldRanges = Readonly<Record<string, { readonly min: number; readonly max: number; readonly step?: number; readonly byConfig?: readonly RangeByConfig[] }>>

/** The `ranges` of a descriptor as `FieldRanges`: an entry without a numeric min and max below it is left out. */
export const parseRanges = (json: unknown): FieldRanges => {
  const out: Record<string, FieldRanges[string]> = {}
  if (json && typeof json === 'object') {
    for (const [key, value] of Object.entries(json)) {
      const r = value as { min?: unknown; max?: unknown; step?: unknown; by_config?: unknown } | null
      if (!(r && typeof r.min === 'number' && typeof r.max === 'number' && r.min < r.max)) continue
      const byConfig = (Array.isArray(r.by_config) ? r.by_config : []).flatMap((e): RangeByConfig[] => {
        const entry = e as { when?: unknown; min?: unknown; max?: unknown; scale?: unknown; step?: unknown } | null
        const end = (v: unknown) => typeof v === 'number' || typeof v === 'string'
        if (!entry || !entry.when || typeof entry.when !== 'object' || !end(entry.min) || !end(entry.max)) return []
        return [{ when: entry.when as Record<string, unknown>, min: entry.min as number | string, max: entry.max as number | string,
          ...(typeof entry.scale === 'number' ? { scale: entry.scale } : {}), ...(typeof entry.step === 'number' && entry.step > 0 ? { step: entry.step } : {}) }]
      })
      out[key] = { min: r.min, max: r.max, ...(typeof r.step === 'number' && r.step > 0 ? { step: r.step } : {}), ...(byConfig.length > 0 ? { byConfig } : {}) }
    }
  }
  return out
}

/**
 * The ranges for one device: each entry with `byConfig` takes the first one whose `when` holds in `config` (a config field's id as an
 * end reads its value, times `scale`), else its own min and max. A value the config does not have (yet) leaves the entry's own.
 */
export const rangesForConfig = (ranges: FieldRanges, config: Readonly<Record<string, unknown>>): FieldRanges => {
  const out: Record<string, FieldRanges[string]> = {}
  for (const [key, r] of Object.entries(ranges)) {
    const hit = r.byConfig?.find((e) => Object.entries(e.when).every(([field, value]) => config[field] === value))
    // A number is the end itself; a config field's id is its value, times scale.
    const end = (v: number | string): number | undefined => (typeof v === 'number' ? v : typeof config[v] === 'number' ? (config[v] as number) * (hit?.scale ?? 1) : undefined)
    const min = hit ? end(hit.min) : r.min
    const max = hit ? end(hit.max) : r.max
    const step = hit?.step ?? r.step
    out[key] = min !== undefined && max !== undefined && min !== max
      ? { min: Math.min(min, max), max: Math.max(min, max), ...(step !== undefined ? { step } : {}) }
      : { min: r.min, max: r.max, ...(r.step !== undefined ? { step: r.step } : {}) }
  }
  return out
}

/** `layout` with the ranges a device gives the fields of command `command` (min, max and step replace the contract's). */
export const withRanges = (layout: Layout | undefined, command: string, ranges: FieldRanges | undefined): Layout | undefined => {
  if (!layout || !ranges) return layout
  const fields = layout.fields.map((f) => {
    const r = ranges[`${command}.${f.id}`]
    return r ? { ...f, min: r.min, max: r.max, ...(r.step !== undefined ? { step: r.step } : {}) } : f
  })
  return { ...layout, fields }
}

export interface Layout {
  /** The C struct it is (`struct` in the descriptor). */
  readonly struct?: string
  readonly fields: readonly WireField[]
  /** The bytes of the fixed part; the whole payload unless the last field runs to the end. */
  readonly fixed: number
  /** The last field has no size. */
  readonly variable: boolean
  /** A run of such structs, one per row (`array` in the descriptor: the rows of a matrix). */
  readonly array?: boolean
}

export type FieldValue = number | bigint | boolean | string | Uint8Array | PinRef | null | readonly (number | bigint | boolean)[] | readonly (PinRef | null)[]
/**
 * What a caller gives for a field: a number, a boolean, a choice's label or an enum member's name, bytes, a string, a list for an
 * array field, a `{ device, pin }` (or `null`, no pin) for a pin reference.
 */
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
  const shown = numberOf(field, input)
  // A scaled field: its range is in the shown unit, the wire takes the shown value times scale (rounded for an integer).
  if (field.scale !== undefined && field.scale !== 1) {
    const n = Number(shown)
    if (field.min !== undefined && n < field.min) throw new PayloadError(`${field.id} = ${n} is below ${field.min}.`)
    if (field.max !== undefined && n > field.max) throw new PayloadError(`${field.id} = ${n} is above ${field.max}.`)
    const wire = n * field.scale
    return writeNumber(view, at, { ...field, scale: undefined, min: undefined, max: undefined }, isFloat(field.rawType) ? wire : Math.round(wire))
  }
  const value = shown
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

const isPinRef = (input: unknown): input is PinRef =>
  typeof input === 'object' && input !== null && !(input instanceof Uint8Array) && !Array.isArray(input) && 'device' in input && 'pin' in input

/** The bytes of a pin reference field: one `sys_io_pin_t`, or a list of them when its size holds several. */
const pinBytes = (field: WireField, input: FieldInput): Uint8Array => {
  const count = field.size! / PIN_REF_SIZE
  const items: unknown = count === 1 && !Array.isArray(input) ? [input] : input
  if (!Array.isArray(items) || items.length !== count) throw new PayloadError(`${field.id} takes ${count} pins.`)
  const out = new Uint8Array(field.size!)
  const view = new DataView(out.buffer)
  items.forEach((item: unknown, i: number) => {
    let pin: PinRef
    if (item === null) {
      if (!field.pinNone) throw new PayloadError(`${field.id}: the unused pin is not published (io_pin_none in enums.json).`)
      pin = field.pinNone
    } else if (isPinRef(item)) pin = item
    else throw new PayloadError(`${field.id} takes { device, pin }, or null for no pin.`)
    if (!Number.isInteger(pin.device) || pin.device < 0 || pin.device > 0xffff) throw new PayloadError(`${field.id}: device ${pin.device} is not a u16.`)
    if (!Number.isInteger(pin.pin) || pin.pin < 0 || pin.pin > 0xff) throw new PayloadError(`${field.id}: pin ${pin.pin} is not a u8.`)
    view.setUint16(i * PIN_REF_SIZE, pin.device, true)
    view.setUint8(i * PIN_REF_SIZE + 2, pin.pin)
  })
  return out
}

/** The value a field gets when the caller leaves it out: its default, zero for an optional field (no pin for a pin reference); else an error. */
const missing = (field: WireField): FieldInput => {
  if (field.default !== undefined && field.default !== null) return field.default as FieldInput
  if (field.optional && field.pinRef) {
    const count = (field.size ?? PIN_REF_SIZE) / PIN_REF_SIZE
    return count === 1 ? null : new Array<null>(count).fill(null)
  }
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
    const input = values[field.id] === undefined ? missing(field) : values[field.id]!
    if (field.pinRef) {
      fixed.set(pinBytes(field, input), field.offset)
      continue
    }
    if (input === null) throw new PayloadError(`${field.id} takes a value, not null.`)
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

/** A pin reference field: `{ device, pin }`, `null` for an unused pin, a list when its size holds several. */
const readPins = (data: Uint8Array, field: WireField): FieldValue => {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  const count = field.size! / PIN_REF_SIZE
  const pins = Array.from({ length: count }, (_, i): PinRef | null => {
    const at = field.offset + i * PIN_REF_SIZE
    const device = view.getUint16(at, true)
    return field.pinNone && device === field.pinNone.device ? null : { device, pin: view.getUint8(at + 2) }
  })
  return count === 1 ? pins[0]! : pins
}

/** The fields of a payload by id. */
export const decodeFields = (layout: Layout, data: Uint8Array): FieldRecord => {
  if (data.length < layout.fixed) throw new PayloadError(`The answer is ${data.length} bytes; its layout needs ${layout.fixed}.`)
  const shown = (f: WireField, v: FieldValue): FieldValue =>
    f.scale === undefined || f.scale === 1 ? v : typeof v === 'number' ? v / f.scale : Array.isArray(v) ? (v as unknown[]).map((x) => (typeof x === 'number' ? x / f.scale! : x)) as FieldValue : v
  return Object.fromEntries(layout.fields.map((f) => [f.id, f.pinRef ? readPins(data, f) : shown(f, readValue(data, f.offset, f.rawType, f.size, f.type === 'boolean'))]))
}
