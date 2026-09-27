import type { VmCatalog, VmObjectType } from '../descriptors'

/*
 * VM telemetry (vm-program.generated.json "telemetry"): after a 0x47 subscribe
 * the device sends every subscribed object (and its PTR children) once, then
 * whatever changed, as 0x43 value records. Heap objects (ID with the DYN bit)
 * are described first with 0x42 records.
 */

export interface VmValueRecord {
  readonly id: number
  /** First element written (elements, not bytes). */
  readonly startIdx: number
  /** Elements in wire form. */
  readonly data: Uint8Array
}

export interface VmDescribedObject {
  readonly id: number
  readonly head: Uint8Array
  readonly name: string
}

export type VmTelemetryFrame =
  | { readonly kind: 'values'; readonly records: readonly VmValueRecord[] }
  | { readonly kind: 'describe'; readonly objects: readonly VmDescribedObject[] }

class Reader {
  readonly data: Uint8Array
  offset: number
  constructor(data: Uint8Array, offset: number) {
    this.data = data
    this.offset = offset
  }

  take(length: number, what: string): Uint8Array {
    if (this.offset + length > this.data.byteLength) throw new RangeError(`VM telemetry: ${what} runs past the frame (${this.data.byteLength} bytes).`)
    const bytes = this.data.subarray(this.offset, this.offset + length)
    this.offset += length
    return bytes
  }

  u16(what: string): number {
    const bytes = this.take(2, what)
    return bytes[0]! | (bytes[1]! << 8)
  }
}

/** Decode one frame `[stream][class][packet][u8 count][records]`; undefined when it isn't VM telemetry. */
export const decodeVmTelemetry = (frame: Uint8Array, catalog: VmCatalog): VmTelemetryFrame | undefined => {
  const { telemetry } = catalog
  if (frame.byteLength < 4 || frame[0] !== telemetry.stream || frame[1] !== telemetry.classHeader) return undefined
  const packet = frame[2]
  const count = frame[3]!
  const reader = new Reader(frame, 4)
  if (packet === telemetry.values) {
    const records: VmValueRecord[] = []
    for (let index = 0; index < count; index++) {
      const id = reader.u16(`record ${index} id`)
      const startIdx = reader.u16(`record ${index} start_idx`)
      const length = reader.u16(`record ${index} byte_len`)
      records.push({ id, startIdx, data: reader.take(length, `record ${index} data`).slice() })
    }
    return { kind: 'values', records }
  }
  if (packet === telemetry.describe) {
    const nameSize = catalog.head.fields.get('d.name_size')!
    const objects: VmDescribedObject[] = []
    for (let index = 0; index < count; index++) {
      const id = reader.u16(`object ${index} id`)
      const head = reader.take(catalog.head.size, `object ${index} header`).slice()
      const nameLength = (head[nameSize.offset]! >> (nameSize.bit ?? 0)) & ((1 << (nameSize.width ?? 8)) - 1)
      const name = String.fromCharCode(...reader.take(nameLength, `object ${index} name`))
      objects.push({ id, head, name })
    }
    return { kind: 'describe', objects }
  }
  return undefined
}

/**
 * Merge value records into per-object buffers (wire form, whole object).
 * `sizeOf` gives an object's full wire size; records for unknown objects are
 * skipped. Returns a new map when anything changed, else the same one.
 */
export const applyVmValues = (values: ReadonlyMap<number, Uint8Array>, records: readonly VmValueRecord[], sizeOf: (id: number) => { readonly bytes: number; readonly wireWidth: number } | undefined): ReadonlyMap<number, Uint8Array> => {
  let next: Map<number, Uint8Array> | undefined
  for (const record of records) {
    const size = sizeOf(record.id)
    if (!size) continue
    next ??= new Map(values)
    const buffer = (next.get(record.id) ?? new Uint8Array(size.bytes)).slice()
    const offset = record.startIdx * size.wireWidth
    buffer.set(record.data.subarray(0, Math.max(0, size.bytes - offset)), offset)
    next.set(record.id, buffer)
  }
  return next ?? values
}

/** Elements in wire form → numbers, booleans (B), text (STR, up to the first NUL) or child IDs (PTR). */
export const decodeVmElements = (type: VmObjectType, data: Uint8Array): readonly number[] | readonly boolean[] | string => {
  if (type.key === 'STR') {
    const end = data.indexOf(0)
    return String.fromCharCode(...(end < 0 ? data : data.subarray(0, end)))
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  const count = Math.floor(data.byteLength / type.wireWidth)
  const read = (index: number): number => {
    const at = index * type.wireWidth
    switch (type.wireType) {
      case 'float': return view.getFloat32(at, true)
      case 'int32_t': return view.getInt32(at, true)
      case 'uint32_t': return view.getUint32(at, true)
      case 'uint16_t': return view.getUint16(at, true)
      default: return view.getUint8(at)
    }
  }
  const numbers = Array.from({ length: count }, (_, index) => read(index))
  return type.key === 'B' ? numbers.map((number) => number !== 0) : numbers
}
