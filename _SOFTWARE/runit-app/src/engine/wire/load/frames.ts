import { arenaUsage } from './arena'
import { ByteWriter } from '../format/bytes'
import type { WireDefinitions } from '../format/definitions'
import { DEFAULT_LIMITS, dataChunk, frameFill } from './limits'
import type { VmLimits } from './limits'
import type { ProgramIR } from '../format/program'
import { accessorRecord, blockRecord, dataRecord, objectRecord, openRecord, subscribeRecord } from '../format/records'
import { OBJ_WIDTH } from '../format/types'
import type { ExecCommand } from '../format/types'

/** One command of the VM router: the op and its payload (the router adds `[seq][flags][ROUTER_KEY_VM][op]`). */
export interface LoadFrame {
  readonly op: number
  readonly payload: Uint8Array
}

/** A batch frame body: `u8 count` and the records. */
export const batchBody = (records: readonly Uint8Array[]): Uint8Array => {
  const out = new ByteWriter().u8(records.length)
  for (const record of records) out.bytes(record)
  return out.finish()
}

/** Records into batch frames: a frame is closed when the next record would pass what a frame is filled to (`frameFill`), or at 255 records (`u8 count`). */
const pack = (op: number, records: readonly Uint8Array[], limits: VmLimits): LoadFrame[] => {
  const frames: LoadFrame[] = []
  const fill = frameFill(limits)
  let current: Uint8Array[] = []
  let size = 0
  const flush = () => {
    if (current.length) frames.push({ op, payload: batchBody(current) })
    current = []
    size = 0
  }
  for (const record of records) {
    if (current.length && (size + record.length > fill || current.length === 255)) flush()
    current.push(record)
    size += record.length
  }
  flush()
  return frames
}

/**
 * The frames that load a program, in the order the board needs them: OPEN (slot counts and arena size), the objects, their
 * initial values, the accessors, then the blocks, one per frame, in execution order. Values uploaded this way are not fresh
 * (VM_CORE.MD, "Still open" 5): a block waiting for a fresh input runs only after a run-time write.
 *
 * `arenaBytes` defaults to what the program needs (arena.ts); pass more to leave room.
 */
export const loadFrames = (defs: WireDefinitions, ir: ProgramIR, limits: VmLimits = DEFAULT_LIMITS, arenaBytes?: number): LoadFrame[] => {
  const types = new Map(ir.objects.map((o) => [o.id, o.type]))
  const open = openRecord({ objects: ir.objects.length, accessors: ir.accessors.length, blocks: ir.blocks.length }, arenaBytes ?? arenaUsage(ir).total)
  const frames: LoadFrame[] = [{ op: defs.ops.open, payload: open }]

  frames.push(...pack(defs.ops.addObjects, ir.objects.map((o) => objectRecord(defs, o)), limits))

  const values: Uint8Array[] = []
  for (const init of ir.data) {
    const type = types.get(init.id)
    if (type === undefined) throw new RangeError(`Initial value for object ${init.id}, which the program does not define.`)
    const width = OBJ_WIDTH[type].wire
    const step = dataChunk(limits, width)
    for (let at = 0; at < Math.max(init.data.length, 1); at += step) {
      values.push(dataRecord({ id: init.id, start: init.start + at / width, data: init.data.subarray(at, at + step) }))
    }
  }
  frames.push(...pack(defs.ops.setData, values, limits))

  frames.push(...pack(defs.ops.addAccessors, ir.accessors.map((a) => accessorRecord(defs, a)), limits))
  for (const block of ir.blocks) frames.push({ op: defs.ops.addBlock, payload: blockRecord(defs, block) })
  return frames
}

/** RESET: unload the program and leave the VM stopped. */
export const resetFrame = (defs: WireDefinitions): LoadFrame => ({ op: defs.ops.reset, payload: new Uint8Array() })

/** EXEC: run control (`vm_exec_command_e`). */
export const execFrame = (defs: WireDefinitions, command: ExecCommand): LoadFrame => ({ op: defs.ops.exec, payload: Uint8Array.of(defs.exec[command]) })

/** SPEED: slow motion; 1 is normal speed. */
export const speedFrame = (defs: WireDefinitions, factor: number): LoadFrame => ({ op: defs.ops.speed, payload: new ByteWriter().u16(factor).finish() })

/** SUBSCRIBE: telemetry for these objects; none clears the subscription. The board refuses more than `subscribeMax` (`CONFIG_VM_SUB_MAX_SUBSCRIBERS`). */
export const subscribeFrame = (defs: WireDefinitions, ids: readonly number[], limits: VmLimits = DEFAULT_LIMITS): LoadFrame => {
  if (ids.length > limits.subscribeMax) throw new RangeError(`${ids.length} objects to subscribe to; the board takes ${limits.subscribeMax}.`)
  return { op: defs.ops.subscribe, payload: batchBody(ids.map(subscribeRecord)) }
}
