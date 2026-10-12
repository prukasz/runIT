import { concat } from '../protocol/frames'
import { batch, DATA_CHUNK, dataRecord, EN_MODE, encodeValue, FLAG, FRAME_BUDGET, MEMORY_WIDTH, NO_ID, ON_ERROR, OP, T, WIDTH } from './wire'
import type { ObjType, Value } from './wire'

/** An accessor id, as `Program.acc` returns it. */
export interface AccessorRef {
  readonly accessor: number
}

/** One step of an accessor: a fixed element, an element named by another accessor's value, or a named member. */
export type AccessorStep =
  | { readonly kind: 'index'; readonly index: number }
  | { readonly kind: 'ref'; readonly via: AccessorRef | number }
  | { readonly kind: 'name'; readonly name: string }

/** A block input or enable: a whole object, an accessor, or nothing (an unwired pin). */
export type Pin = number | AccessorRef | null

export interface BlockDef {
  /** The block type id (`VM_BLK_*`, see blocks.ts). */
  readonly type: number
  readonly inputs?: readonly Pin[]
  /** Object ids the block writes. */
  readonly outputs?: readonly number[]
  readonly enables?: readonly Pin[]
  /** The block's state bytes (blocks.ts `encodeState`). */
  readonly state?: Uint8Array
  /** The object that gets the block's ENO (a boolean the block sets when it acted). */
  readonly eno?: number
  readonly enableMode?: number
  readonly onError?: number
}

export interface ObjectOptions {
  readonly flags?: number
  readonly name?: string
  readonly init?: Value
}

export interface LoadFrame {
  readonly op: number
  readonly payload: Uint8Array
}

interface ObjectEntry {
  readonly id: number
  readonly type: ObjType
  readonly count: number
  readonly flags: number
  readonly name: Uint8Array
}

const u16le = (n: number): Uint8Array => Uint8Array.of(n & 0xff, (n >> 8) & 0xff)

/**
 * A program in the making: objects, accessors and blocks (ids = creation order). `frames()` is what goes to the board, in order:
 * OPEN, objects, initial values, accessors, blocks. Values uploaded this way are not fresh: a block that waits for a fresh input
 * runs only after the app writes one at run time (VM_CORE.MD).
 */
export class Program {
  readonly objects: ObjectEntry[] = []
  private readonly inits: { id: number; start: number; data: Uint8Array }[] = []
  private readonly accessors: { id: number; root: number; steps: readonly AccessorStep[] }[] = []
  private readonly blocks: Uint8Array[] = []
  private readonly wholeObject = new Map<number, number>()

  readonly size: number

  constructor(size = 32768) {
    this.size = size
  }

  /** A new object of `count` elements. Defaults: mutable and `upd_resetable`, so blocks that write it mark it fresh. */
  obj(type: ObjType, count = 1, options: ObjectOptions = {}): number {
    const id = this.objects.length
    const name = new TextEncoder().encode(options.name ?? '')
    if (name.length > 15) throw new RangeError(`Object name '${options.name}' is over 15 bytes.`)
    this.objects.push({ id, type, count, flags: options.flags ?? FLAG.mutable | FLAG.updResetable, name })
    if (options.init !== undefined) this.val(id, options.init)
    return id
  }

  f32(init?: Value, options: ObjectOptions = {}): number { return this.obj(T.f32, 1, { ...options, init }) }
  u32(init?: Value, options: ObjectOptions = {}): number { return this.obj(T.u32, 1, { ...options, init }) }
  i32(init?: Value, options: ObjectOptions = {}): number { return this.obj(T.i32, 1, { ...options, init }) }
  u8(init?: Value, options: ObjectOptions = {}): number { return this.obj(T.u8, 1, { ...options, init }) }
  bool(init?: Value, options: ObjectOptions = {}): number { return this.obj(T.bool, 1, { ...options, init }) }

  /** A folder: `count` pointers; `children` are object ids. */
  ptr(count = 1, children?: readonly number[], options: ObjectOptions = {}): number {
    return this.obj(T.ptr, count, { ...options, init: children ? [...children] : undefined })
  }

  /** The initial value of an object (from element `start`). */
  val(id: number, value: Value, start = 0): void {
    this.inits.push({ id, start, data: encodeValue(this.entry(id).type, value) })
  }

  /** The initial values as records (id, first element, bytes), in the order they were set. */
  initialRecords(): readonly { readonly id: number; readonly start: number; readonly data: Uint8Array }[] {
    return this.inits
  }

  type(id: number): ObjType {
    return this.entry(id).type
  }

  count(id: number): number {
    return this.entry(id).count
  }

  acc(root: number, steps: readonly AccessorStep[] = []): AccessorRef {
    const id = this.accessors.length
    this.accessors.push({ id, root, steps })
    return { accessor: id }
  }

  /** Adds a block; returns its id (= its position in the execution order). */
  block(def: BlockDef): number {
    const id = this.blocks.length
    const inputs = (def.inputs ?? []).map((pin) => this.pin(pin))
    const enables = (def.enables ?? []).map((pin) => this.pin(pin))
    const outputs = def.outputs ?? []
    const state = def.state ?? new Uint8Array()
    const head = new Uint8Array(14)
    const view = new DataView(head.buffer)
    view.setUint16(0, id, true)
    view.setUint16(2, id, true)
    head[4] = def.type
    head[5] = inputs.length
    head[6] = outputs.length
    head[7] = enables.length
    head[8] = def.enableMode ?? EN_MODE.any
    head[9] = def.onError ?? ON_ERROR.continue
    view.setUint16(10, state.length, true)
    view.setUint16(12, def.eno ?? NO_ID, true)
    this.blocks.push(concat(head, ...[...inputs, ...outputs, ...enables].map(u16le), state))
    return id
  }

  /** The accessor id of a pin: a whole-object accessor is made once per object. */
  private pin(pin: Pin): number {
    if (pin === null) return NO_ID
    if (typeof pin === 'object') return pin.accessor
    let id = this.wholeObject.get(pin)
    if (id === undefined) {
      id = this.acc(pin).accessor
      this.wholeObject.set(pin, id)
    }
    return id
  }

  private entry(id: number): ObjectEntry {
    const entry = this.objects[id]
    if (!entry) throw new RangeError(`No object ${id}.`)
    return entry
  }

  /** Every load frame, OPEN first. */
  frames(): LoadFrame[] {
    const open = new Uint8Array(10)
    const view = new DataView(open.buffer)
    view.setUint16(0, this.objects.length, true)
    view.setUint16(2, this.accessors.length, true)
    view.setUint16(4, this.blocks.length, true)
    view.setUint32(6, this.size, true)
    const out: LoadFrame[] = [{ op: OP.open, payload: open }]

    const pack = (op: number, records: readonly Uint8Array[]) => {
      let current: Uint8Array[] = []
      let size = 0
      const flush = () => {
        if (current.length) out.push({ op, payload: batch(current) })
        current = []
        size = 0
      }
      for (const record of records) {
        if (current.length && (size + record.length > FRAME_BUDGET || current.length === 255)) flush()
        current.push(record)
        size += record.length
      }
      flush()
    }

    pack(OP.addObjects, this.objects.map((o) => {
      const head = new Uint8Array(6)
      const hv = new DataView(head.buffer)
      hv.setUint16(0, o.id, true)
      hv.setUint16(2, o.count * MEMORY_WIDTH[o.type], true)
      head[4] = o.type | (o.name.length << 4)
      head[5] = o.flags
      return concat(head, o.name)
    }))

    const data: Uint8Array[] = []
    for (const init of this.inits) {
      const width = WIDTH[this.entry(init.id).type]
      const step = Math.floor(DATA_CHUNK / width) * width
      for (let at = 0; at < Math.max(init.data.length, 1); at += step) {
        data.push(dataRecord(init.id, init.data.subarray(at, at + step), init.start + at / width))
      }
    }
    pack(OP.setData, data)

    pack(OP.addAccessors, this.accessors.map((a) => {
      const steps = a.steps.map((step) => {
        if (step.kind === 'index') return concat(Uint8Array.of(0), new Uint8Array(Uint32Array.of(step.index).buffer))
        if (step.kind === 'ref') return concat(Uint8Array.of(1), u16le(typeof step.via === 'number' ? step.via : step.via.accessor))
        const name = new TextEncoder().encode(step.name)
        return concat(Uint8Array.of(2, name.length), name)
      })
      const body = concat(...steps)
      return concat(u16le(a.id), u16le(a.root), Uint8Array.of(a.steps.length, body.length), body)
    }))

    for (const block of this.blocks) out.push({ op: OP.addBlock, payload: block })
    return out
  }
}
