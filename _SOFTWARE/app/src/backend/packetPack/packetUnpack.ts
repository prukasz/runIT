import { PacketPackError, SCALARS } from './packetPack'
import type { PacketField, PacketScalarType, PacketScalarValue, PacketStructValues, PacketValue } from './types'

export interface UnpackResult {
  readonly values: PacketStructValues
  /** Bytes consumed by the layout. Trailing bytes are left for the caller to judge. */
  readonly consumed: number
}

class Reader {
  private offset = 0
  private readonly view: DataView
  private readonly data: Uint8Array

  constructor(data: Uint8Array) {
    this.data = data
    this.view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  }

  get position(): number {
    return this.offset
  }

  get remaining(): number {
    return this.data.byteLength - this.offset
  }

  /** Length up to and including the next NUL, or the rest when there is none. */
  untilNul(): number {
    const end = this.data.indexOf(0, this.offset)
    return end < 0 ? this.remaining : end - this.offset + 1
  }

  take(length: number, fieldName: string): Uint8Array {
    if (length > this.remaining) throw new PacketPackError(`Field '${fieldName}' needs ${length} bytes, ${this.remaining} left.`)
    const bytes = this.data.slice(this.offset, this.offset + length)
    this.offset += length
    return bytes
  }

  scalar(type: PacketScalarType, fieldName: string): PacketScalarValue {
    const info = SCALARS[type]
    if (info.size > this.remaining) throw new PacketPackError(`Field '${fieldName}' needs ${info.size} bytes, ${this.remaining} left.`)
    const at = this.offset
    this.offset += info.size
    if (info.boolean) return this.view.getUint8(at) !== 0
    if (info.float) return info.size === 4 ? this.view.getFloat32(at, true) : this.view.getFloat64(at, true)
    if (info.size === 1) return info.signed ? this.view.getInt8(at) : this.view.getUint8(at)
    if (info.size === 2) return info.signed ? this.view.getInt16(at, true) : this.view.getUint16(at, true)
    if (info.size === 4) return info.signed ? this.view.getInt32(at, true) : this.view.getUint32(at, true)
    return info.signed ? this.view.getBigInt64(at, true) : this.view.getBigUint64(at, true)
  }
}

/**
 * Reverse of packStruct(): read ordered little-endian fields with no padding.
 * A variable bytes field takes the rest of the data; variable text runs to and
 * including the next NUL (or the end).
 */
export const unpackStruct = (fields: readonly PacketField[], data: Uint8Array): UnpackResult => {
  const reader = new Reader(data)
  const values = readFields(reader, fields)
  return { values, consumed: reader.position }
}

const readFields = (reader: Reader, fields: readonly PacketField[]): PacketStructValues => {
  const values: Record<string, PacketValue> = {}
  for (const field of fields) values[field.name] = readField(reader, field)
  return values
}

const readField = (reader: Reader, field: PacketField): PacketValue => {
  if (field.kind === 'scalar') {
    if (field.length === undefined) return reader.scalar(field.type, field.name)
    return Array.from({ length: field.length }, () => reader.scalar(field.type, field.name))
  }
  if (field.kind === 'bytes') return reader.take(field.length ?? reader.remaining, field.name)
  if (field.kind === 'text') {
    const raw = reader.take(field.length ?? reader.untilNul(), field.name)
    const end = raw.indexOf(0)
    return new TextDecoder().decode(end < 0 ? raw : raw.subarray(0, end))
  }
  return readFields(reader, field.fields)
}
