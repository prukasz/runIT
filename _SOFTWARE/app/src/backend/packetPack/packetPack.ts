import type { PacketField, PacketScalarType, PacketScalarValue, PacketSchema, PacketStructValues, PacketValue } from './types'

export class PacketPackError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'PacketPackError'
  }
}

export type ScalarInfo = { readonly size: number; readonly signed: boolean; readonly float: boolean; readonly boolean: boolean }

export const SCALARS: Readonly<Record<PacketScalarType, ScalarInfo>> = {
  u8: { size: 1, signed: false, float: false, boolean: false }, uint8_t: { size: 1, signed: false, float: false, boolean: false }, uint8: { size: 1, signed: false, float: false, boolean: false },
  i8: { size: 1, signed: true, float: false, boolean: false }, int8_t: { size: 1, signed: true, float: false, boolean: false }, int8: { size: 1, signed: true, float: false, boolean: false },
  u16: { size: 2, signed: false, float: false, boolean: false }, uint16_t: { size: 2, signed: false, float: false, boolean: false }, uint16: { size: 2, signed: false, float: false, boolean: false },
  i16: { size: 2, signed: true, float: false, boolean: false }, int16_t: { size: 2, signed: true, float: false, boolean: false }, int16: { size: 2, signed: true, float: false, boolean: false },
  u32: { size: 4, signed: false, float: false, boolean: false }, uint32_t: { size: 4, signed: false, float: false, boolean: false }, uint32: { size: 4, signed: false, float: false, boolean: false },
  i32: { size: 4, signed: true, float: false, boolean: false }, int32_t: { size: 4, signed: true, float: false, boolean: false }, int32: { size: 4, signed: true, float: false, boolean: false },
  u64: { size: 8, signed: false, float: false, boolean: false }, uint64_t: { size: 8, signed: false, float: false, boolean: false }, uint64: { size: 8, signed: false, float: false, boolean: false },
  i64: { size: 8, signed: true, float: false, boolean: false }, int64_t: { size: 8, signed: true, float: false, boolean: false }, int64: { size: 8, signed: true, float: false, boolean: false },
  f32: { size: 4, signed: true, float: true, boolean: false }, float: { size: 4, signed: true, float: true, boolean: false },
  f64: { size: 8, signed: true, float: true, boolean: false }, double: { size: 8, signed: true, float: true, boolean: false },
  bool: { size: 1, signed: false, float: false, boolean: true }, _Bool: { size: 1, signed: false, float: false, boolean: true },
  bitmask8: { size: 1, signed: false, float: false, boolean: false }, bitmask16: { size: 2, signed: false, float: false, boolean: false }, bitmask32: { size: 4, signed: false, float: false, boolean: false }, bitmask64: { size: 8, signed: false, float: false, boolean: false },
}

class Writer {
  private readonly bytes: number[] = []

  pushByte(value: number): void {
    this.bytes.push(value)
  }

  pushBytes(data: Uint8Array): void {
    this.bytes.push(...data)
  }

  pushScalar(type: PacketScalarType, value: PacketScalarValue, fieldName: string): void {
    const info = SCALARS[type]
    const buffer = new ArrayBuffer(info.size)
    const view = new DataView(buffer)
    if (info.boolean) {
      if (value !== true && value !== false && value !== 0 && value !== 1) throw new PacketPackError(`Field '${fieldName}' requires boolean or 0/1.`)
      view.setUint8(0, value === true || value === 1 ? 1 : 0)
    } else if (info.float) {
      if (typeof value !== 'number' || !Number.isFinite(value)) throw new PacketPackError(`Field '${fieldName}' requires a finite number.`)
      if (info.size === 4) view.setFloat32(0, value, true)
      else view.setFloat64(0, value, true)
    } else {
      const integer = toInteger(value, fieldName)
      assertRange(integer, info.size * 8, info.signed, fieldName)
      this.setInteger(view, info, integer)
    }
    this.pushBytes(new Uint8Array(buffer))
  }

  finish(): Uint8Array {
    return Uint8Array.from(this.bytes)
  }

  private setInteger(view: DataView, info: ScalarInfo, value: bigint): void {
    if (info.size === 1) {
      if (info.signed) view.setInt8(0, Number(value))
      else view.setUint8(0, Number(value))
    } else if (info.size === 2) {
      if (info.signed) view.setInt16(0, Number(value), true)
      else view.setUint16(0, Number(value), true)
    } else if (info.size === 4) {
      if (info.signed) view.setInt32(0, Number(value), true)
      else view.setUint32(0, Number(value), true)
    } else if (info.signed) {
      view.setBigInt64(0, value, true)
    } else {
      view.setBigUint64(0, value, true)
    }
  }
}

const toInteger = (value: PacketScalarValue, fieldName: string): bigint => {
  if (typeof value === 'bigint') return value
  if (typeof value !== 'number' || !Number.isSafeInteger(value)) throw new PacketPackError(`Field '${fieldName}' requires a safe integer or bigint.`)
  return BigInt(value)
}

const assertRange = (value: bigint, bits: number, signed: boolean, fieldName: string): void => {
  const limit = 1n << BigInt(bits)
  const min = signed ? -(limit >> 1n) : 0n
  const max = signed ? (limit >> 1n) - 1n : limit - 1n
  if (value < min || value > max) throw new PacketPackError(`Field '${fieldName}' is outside ${signed ? 'signed' : 'unsigned'} ${bits}-bit range.`)
}

const requireRecord = (value: PacketValue | undefined, fieldName: string): PacketStructValues => {
  if (!value || typeof value !== 'object' || Array.isArray(value) || value instanceof Uint8Array) {
    throw new PacketPackError(`Field '${fieldName}' requires an object value.`)
  }
  return value as PacketStructValues
}

const requireBytes = (value: PacketValue | undefined, fieldName: string): Uint8Array => {
  if (!(value instanceof Uint8Array)) throw new PacketPackError(`Field '${fieldName}' requires Uint8Array data.`)
  return value
}

const requireArray = (value: PacketValue | undefined, fieldName: string, length: number): readonly PacketScalarValue[] => {
  if (!Array.isArray(value) || value.length !== length) throw new PacketPackError(`Field '${fieldName}' requires exactly ${length} values.`)
  return value
}

const fixedLength = (field: PacketField): number | undefined => {
  if (field.kind === 'scalar') return SCALARS[field.type].size * (field.length ?? 1)
  if (field.kind === 'bytes' || field.kind === 'text') return field.length
  return packedSize(field.fields)
}

/** Returns undefined when a schema contains variable bytes/text and has no fixed packed size. */
export const packedSize = (fields: readonly PacketField[]): number | undefined => {
  let total = 0
  for (const field of fields) {
    const size = fixedLength(field)
    if (size === undefined) return undefined
    total += size
  }
  return total
}

/** Pack ordered fields without class/function headers. No alignment padding is inserted. */
export const packStruct = (fields: readonly PacketField[], values: PacketStructValues): Uint8Array => {
  const writer = new Writer()
  writeFields(writer, fields, values)
  return writer.finish()
}

/** Pack class byte, function byte, then an exact little-endian packed payload. */
export const packPacket = (schema: PacketSchema, values: PacketStructValues): Uint8Array => {
  assertRange(BigInt(schema.classHeader), 8, false, 'classHeader')
  assertRange(BigInt(schema.functionHeader), 8, false, 'functionHeader')
  const writer = new Writer()
  writer.pushByte(schema.classHeader)
  writer.pushByte(schema.functionHeader)
  writeFields(writer, schema.fields, values)
  return writer.finish()
}

const writeFields = (writer: Writer, fields: readonly PacketField[], values: PacketStructValues): void => {
  for (const field of fields) {
    const value = values[field.name]
    if (field.kind === 'scalar') {
      if (field.length === undefined) writer.pushScalar(field.type, value as PacketScalarValue, field.name)
      else requireArray(value, field.name, field.length).forEach((entry) => writer.pushScalar(field.type, entry, field.name))
      continue
    }
    if (field.kind === 'bytes') {
      const bytes = requireBytes(value, field.name)
      if (field.length !== undefined && bytes.byteLength !== field.length) throw new PacketPackError(`Field '${field.name}' requires exactly ${field.length} bytes.`)
      writer.pushBytes(bytes)
      continue
    }
    if (field.kind === 'text') {
      if (typeof value !== 'string') throw new PacketPackError(`Field '${field.name}' requires a string.`)
      const encoded = new TextEncoder().encode(`${value}${field.nulTerminate ? '\0' : ''}`)
      if (field.length !== undefined) {
        if (encoded.byteLength > field.length) throw new PacketPackError(`Field '${field.name}' exceeds its ${field.length}-byte capacity.`)
        writer.pushBytes(encoded)
        writer.pushBytes(new Uint8Array(field.length - encoded.byteLength))
      } else {
        writer.pushBytes(encoded)
      }
      continue
    }
    writer.pushBytes(packStruct(field.fields, requireRecord(value, field.name)))
  }
}
