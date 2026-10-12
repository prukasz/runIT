import { decodeValue, DYNAMIC_ID_BIT, KEY_VM, MEMORY_WIDTH, OP, WIDTH } from './wire'
import type { ObjType } from './wire'

/** A heap object's describe record: sent before its first value (the program does not know it). */
export interface DescribeRecord {
  readonly kind: 'describe'
  readonly id: number
  /** `payload_size` in the board's memory. */
  readonly size: number
  readonly type: ObjType
  readonly flags: number
  readonly name: string
}

/** Elements `start`.. of an object (a big object arrives in several records). */
export interface ValueRecord {
  readonly kind: 'value'
  readonly id: number
  readonly start: number
  readonly data: Uint8Array
}

export type TelemetryRecord = DescribeRecord | ValueRecord

/**
 * A telemetry frame (the payload after the stream byte): the same commands as the uploads, `[0x0B][op][count]` and records
 * (ADD_OBJECTS = describe, SET_DATA = values). Undefined when it is not a well-formed VM frame.
 */
export const parseTelemetryFrame = (frame: Uint8Array): TelemetryRecord[] | undefined => {
  if (frame.length < 3 || frame[0] !== KEY_VM || (frame[1] !== OP.addObjects && frame[1] !== OP.setData)) return undefined
  const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength)
  const records: TelemetryRecord[] = []
  let at = 3
  for (let i = 0; i < frame[2]; i++) {
    if (at + 6 > frame.length) return undefined
    if (frame[1] === OP.addObjects) {
      const id = view.getUint16(at, true)
      const size = view.getUint16(at + 2, true)
      const typeName = frame[at + 4]
      const nameLength = typeName >> 4
      at += 6
      if (at + nameLength > frame.length) return undefined
      records.push({ kind: 'describe', id, size, type: (typeName & 0x0f) as ObjType, flags: frame[at - 1], name: new TextDecoder().decode(frame.subarray(at, at + nameLength)) })
      at += nameLength
    } else {
      const id = view.getUint16(at, true)
      const start = view.getUint16(at + 2, true)
      const length = view.getUint16(at + 4, true)
      at += 6
      if (at + length > frame.length) return undefined
      records.push({ kind: 'value', id, start, data: frame.subarray(at, at + length) })
      at += length
    }
  }
  return at === frame.length ? records : undefined
}

export interface ObjectKind {
  readonly type: ObjType
  readonly count: number
}

/**
 * The board's values as the app last heard them: every subscribed object's bytes, big objects reassembled from their records.
 * Only changes are sent after the first snapshot, so a value stays here until the board sends a new one.
 */
export class TelemetryMirror {
  private readonly bytes = new Map<number, Uint8Array>()
  private readonly heap = new Map<number, ObjectKind>()
  private readonly waiters = new Set<() => void>()
  private readonly kindOf: (id: number) => ObjectKind | undefined
  /** Frames that could not be read. */
  malformed = 0

  /** `kindOf` tells the type of a program object (the heap objects describe themselves). */
  constructor(kindOf: (id: number) => ObjectKind | undefined) {
    this.kindOf = kindOf
  }

  /** Folds one telemetry frame in. */
  apply(frame: Uint8Array): void {
    const records = parseTelemetryFrame(frame)
    if (!records) {
      this.malformed++
      return
    }
    for (const record of records) {
      if (record.kind === 'describe') {
        this.heap.set(record.id, { type: record.type, count: Math.floor(record.size / MEMORY_WIDTH[record.type]) })
        continue
      }
      const kind = this.kind(record.id)
      const width = kind ? WIDTH[kind.type] : 1 // an object nobody described: raw bytes
      let buffer = this.bytes.get(record.id) ?? new Uint8Array(0)
      const end = record.start * width + record.data.length
      if (buffer.length < end) {
        const grown = new Uint8Array(end)
        grown.set(buffer)
        buffer = grown
      }
      buffer.set(record.data, record.start * width)
      this.bytes.set(record.id, buffer)
    }
    this.waiters.forEach((wake) => wake())
  }

  kind(id: number): ObjectKind | undefined {
    return id & DYNAMIC_ID_BIT ? this.heap.get(id) : this.kindOf(id)
  }

  has(id: number): boolean {
    return this.bytes.has(id)
  }

  raw(id: number): Uint8Array | undefined {
    return this.bytes.get(id)
  }

  /** The decoded value: a number (a boolean for a bool object) for one element, an array for several, undefined until it arrived. */
  value(id: number): number | boolean | number[] | Uint8Array | undefined {
    const bytes = this.bytes.get(id)
    const kind = this.kind(id)
    return bytes && kind ? decodeValue(kind.type, bytes) : undefined
  }

  /** Forget everything (a new program, a new subscription list). */
  clear(): void {
    this.bytes.clear()
    this.heap.clear()
  }

  /** Resolves when `ready()` holds after a telemetry frame (or now), rejects after `timeoutMs`. */
  waitFor(ready: () => boolean, timeoutMs: number): Promise<void> {
    if (ready()) return Promise.resolve()
    return new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiters.delete(check)
        reject(new Error(`No telemetry for the awaited values in ${timeoutMs} ms.`))
      }, timeoutMs)
      const check = () => {
        if (!ready()) return
        clearTimeout(timer)
        this.waiters.delete(check)
        resolve()
      }
      this.waiters.add(check)
    })
  }
}
