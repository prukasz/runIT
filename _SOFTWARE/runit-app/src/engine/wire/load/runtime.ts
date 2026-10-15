import { decodeValue } from '../format/codec'
import type { WireDefinitions } from '../format/definitions'
import type { DataDef } from '../format/program'
import { dataRecord } from '../format/records'
import { OBJ_WIDTH } from '../format/types'
import type { ObjType } from '../format/types'
import { pack } from './frames'
import type { LoadFrame } from './frames'
import { DEFAULT_LIMITS, dataChunk } from './limits'
import type { VmLimits } from './limits'

/*
 * The VM at run time. A value is written the way it is loaded: SET_DATA, which on a running program is a runtime write applied
 * at the start of the next pass (VM_CORE.MD "Remote"). A value is read the way it is written: the board sends the changed
 * values of the subscribed objects as the same command without `seq` / `flags`,
 *
 *   values        [ROUTER_KEY_VM][SET_DATA][u8 count] count x (id u16, start u16, byte length u16, data)
 *   heap objects  [ROUTER_KEY_VM][ADD_OBJECTS][u8 count] count x (id u16, payload u16, type | name length << 4, flags, name)
 *
 * so one decoder reads both directions. A heap object is described before any value that refers to it.
 */

/**
 * The frames that write these values (element `start` of each object; `data` in wire bytes), split to fit the board's frame.
 * `types` (object id -> type) gives the width of an element, so a long value is cut between elements.
 */
export const writeFrames = (defs: WireDefinitions, writes: readonly DataDef[], types: ReadonlyMap<number, ObjType>, limits: VmLimits = DEFAULT_LIMITS): LoadFrame[] => {
  const records: Uint8Array[] = []
  for (const write of writes) {
    const type = types.get(write.id)
    if (type === undefined) throw new RangeError(`Object ${write.id} is not in the program.`)
    const width = OBJ_WIDTH[type].wire
    const step = dataChunk(limits, width)
    for (let at = 0; at < Math.max(write.data.length, 1); at += step) records.push(dataRecord({ id: write.id, start: write.start + at / width, data: write.data.subarray(at, at + step) }))
  }
  return pack(defs.ops.setData, records, limits)
}

/** One object's value in a telemetry frame. */
export interface TelemetryValue {
  readonly id: number
  /** The first element the data covers. */
  readonly start: number
  readonly bytes: Uint8Array
  /** The object's type, when known (it was in the program, or announced by an ADD_OBJECTS record before). */
  readonly type?: ObjType
  /** `bytes` decoded by `type`: one element as a number (a boolean for `bool`), several as an array, text as bytes. Raw bytes without a type. */
  readonly value: number | boolean | number[] | boolean[] | Uint8Array
  /** The user's variable, when the reader was given the names. */
  readonly variable?: string
}

/** An object the board created at run time (a heap object), announced before its values. */
export interface TelemetryObject {
  readonly id: number
  readonly type: ObjType | undefined
  readonly name: string
  readonly payload: number
  readonly flags: number
}

export interface TelemetryUpdate {
  readonly values: readonly TelemetryValue[]
  readonly objects: readonly TelemetryObject[]
}

/**
 * Reads telemetry frames. It knows the type of an object from the program (`types`, object id -> type) and from the objects
 * the board announces; `names` (object id -> variable id) adds the user's variable to a value.
 */
export class TelemetryReader {
  private readonly types: Map<number, ObjType>
  private readonly typeOf: ReadonlyMap<number, ObjType>

  constructor(
    private readonly defs: WireDefinitions,
    types: ReadonlyMap<number, ObjType> = new Map(),
    private readonly names: ReadonlyMap<number, string> = new Map(),
  ) {
    this.types = new Map(types)
    this.typeOf = new Map(Object.entries(defs.objTypes).map(([name, number]) => [number, name as ObjType]))
  }

  /**
   * One frame of the telemetry stream, without its stream byte. Undefined when it is not a telemetry frame (another router key
   * or op) or it is cut short.
   */
  read(frame: Uint8Array): TelemetryUpdate | undefined {
    if (frame.length < 3 || frame[0] !== this.defs.routerKey) return undefined
    const op = frame[1]
    const count = frame[2]!
    const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength)
    const values: TelemetryValue[] = []
    const objects: TelemetryObject[] = []
    let at = 3
    if (op === this.defs.ops.setData) {
      for (let i = 0; i < count; i++) {
        if (at + 6 > frame.length) return undefined
        const id = view.getUint16(at, true)
        const start = view.getUint16(at + 2, true)
        const length = view.getUint16(at + 4, true)
        at += 6
        if (at + length > frame.length) return undefined
        const bytes = frame.slice(at, at + length)
        at += length
        const type = this.types.get(id)
        const variable = this.names.get(id)
        values.push({ id, start, bytes, ...(type ? { type } : {}), value: type ? decodeValue(type, bytes) : bytes, ...(variable ? { variable } : {}) })
      }
    } else if (op === this.defs.ops.addObjects) {
      for (let i = 0; i < count; i++) {
        if (at + 6 > frame.length) return undefined
        const id = view.getUint16(at, true)
        const payload = view.getUint16(at + 2, true)
        const typeAndName = frame[at + 4]!
        const flags = frame[at + 5]!
        const nameLength = typeAndName >> 4
        at += 6
        if (at + nameLength > frame.length) return undefined
        const type = this.typeOf.get(typeAndName & 0x0f)
        if (type) this.types.set(id, type)
        objects.push({ id, type, name: new TextDecoder().decode(frame.subarray(at, at + nameLength)), payload, flags })
        at += nameLength
      }
    } else return undefined
    return { values, objects }
  }
}
