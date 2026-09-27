import { packPacket } from '../../backend/packetPack'
import type { VmCatalog } from '../descriptors'
import type { ObjectSection, ProjectDocument } from '../project'
import { compileObjects } from './objects'
import type { Diagnostic, ObjectLayout } from './objects'

/*
 * Project → the VM load sequence: 0x41 open, 0x42 objects, 0x43 values.
 * Accessors (0x44) and blocks (0x45) come with the canvas. Every frame is
 * [class][packet][body] without the seq byte (CommandClient adds it), at most
 * `maxFrameBytes` long; batched packets are split to fit.
 */

export interface CompileOptions {
  /** Longest command the link carries, seq byte excluded (BLE: MTU - 3 - 1). */
  readonly maxFrameBytes: number
  /** Object sections after the user's tree (block outputs, ENO), numbered in order. */
  readonly sections?: readonly ObjectSection[]
}

export interface CompiledProgram {
  /** No errors: the frames can be sent in order. */
  readonly ok: boolean
  readonly diagnostics: readonly Diagnostic[]
  readonly frames: readonly Uint8Array[]
  readonly objects: ObjectLayout
  readonly counts: { readonly objects: number; readonly accessors: number; readonly blocks: number }
  readonly arenaBytes: number
  readonly retainBytes: number
}

type BatchedPacket = 'addObjects' | 'setData' | 'addAccessors' | 'subscribe'

/** Class, packet and record count before the records of a batched packet (count width from vm-program). */
export const batchOverhead = (catalog: VmCatalog, packet: BatchedPacket): number => 2 + (catalog.wire[packet].batch?.countBytes ?? 1)

/** Records back to back after the count, as many per frame as fit and the packet's batch max allows. */
export const batchFrames = (catalog: VmCatalog, packet: BatchedPacket, records: readonly Uint8Array[], maxFrameBytes: number): Uint8Array[] => {
  const { header, batch } = catalog.wire[packet]
  if (!batch) throw new Error(`Packet 0x${header.toString(16)} is not batched.`)
  const overhead = batchOverhead(catalog, packet)
  const frames: Uint8Array[] = []
  let pending: Uint8Array[] = []
  let size = overhead
  const flush = () => {
    if (!pending.length) return
    const frame = new Uint8Array(size)
    const view = new DataView(frame.buffer)
    frame.set([catalog.classHeader, header])
    if (batch.countBytes === 2) view.setUint16(2, pending.length, true)
    else frame[2] = pending.length
    let offset = overhead
    for (const record of pending) {
      frame.set(record, offset)
      offset += record.byteLength
    }
    frames.push(frame)
    pending = []
    size = overhead
  }
  for (const record of records) {
    if (overhead + record.byteLength > maxFrameBytes) throw new RangeError(`A ${record.byteLength}-byte record of packet 0x${header.toString(16)} doesn't fit a ${maxFrameBytes}-byte frame.`)
    if (size + record.byteLength > maxFrameBytes || pending.length === batch.max) flush()
    pending.push(record)
    size += record.byteLength
  }
  flush()
  return frames
}

/** 0x43 records, a value split by whole elements where it doesn't fit one frame. */
export const dataRecords = (catalog: VmCatalog, data: ReturnType<typeof compileObjects>['data'], maxFrameBytes: number): Uint8Array[] => {
  const head = catalog.wire.setData.recordSize // u16 id, u16 start_idx, u16 byte_len
  const records: Uint8Array[] = []
  for (const { wireId, bytes, wireWidth } of data) {
    const room = Math.floor((maxFrameBytes - batchOverhead(catalog, 'setData') - head) / wireWidth) * wireWidth
    if (room < wireWidth) throw new RangeError(`A ${maxFrameBytes}-byte frame has no room for one element of object ${wireId}.`)
    for (let offset = 0; offset < bytes.byteLength; offset += room) {
      const chunk = bytes.subarray(offset, offset + room)
      const record = new Uint8Array(head + chunk.byteLength)
      const view = new DataView(record.buffer)
      view.setUint16(0, wireId, true)
      view.setUint16(2, offset / wireWidth, true)
      view.setUint16(4, chunk.byteLength, true)
      record.set(chunk, head)
      records.push(record)
    }
  }
  return records
}

export const compileProgram = (project: ProjectDocument, catalog: VmCatalog, options: CompileOptions): CompiledProgram => {
  const objects = compileObjects(project, catalog, options.sections)
  const diagnostics = [...objects.diagnostics]
  const counts = { objects: objects.layout.objects.length, accessors: 0, blocks: 0 }
  const arenaBytes = objects.arenaBytes
  if (arenaBytes > catalog.arena.maxBytes) diagnostics.push({ severity: 'error', message: `The program needs ${arenaBytes} bytes of arena, the device has ${catalog.arena.maxBytes}.`, firmwareError: 'ERR_VM_LOAD_TOO_BIG' })

  let frames: Uint8Array[] = []
  if (!diagnostics.some((entry) => entry.severity === 'error')) {
    try {
      const open = packPacket(
        {
          classHeader: catalog.classHeader,
          functionHeader: catalog.packets.open,
          fields: [
            { kind: 'scalar', name: 'obj_cnt', type: 'uint16_t' },
            { kind: 'scalar', name: 'acc_cnt', type: 'uint16_t' },
            { kind: 'scalar', name: 'blk_cnt', type: 'uint16_t' },
            { kind: 'scalar', name: 'total_size', type: 'uint32_t' },
          ],
        },
        { obj_cnt: counts.objects, acc_cnt: counts.accessors, blk_cnt: counts.blocks, total_size: arenaBytes },
      )
      frames = [
        open,
        ...batchFrames(catalog, 'addObjects', objects.createRecords, options.maxFrameBytes),
        ...batchFrames(catalog, 'setData', dataRecords(catalog, objects.data, options.maxFrameBytes), options.maxFrameBytes),
      ]
    } catch (error) {
      diagnostics.push({ severity: 'error', message: error instanceof Error ? error.message : String(error) })
    }
  }

  return {
    ok: !diagnostics.some((entry) => entry.severity === 'error'),
    diagnostics,
    frames,
    objects: objects.layout,
    counts,
    arenaBytes,
    retainBytes: objects.retainBytes,
  }
}
