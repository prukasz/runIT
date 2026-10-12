import type { VmCatalog } from '../descriptors'
import { walkObjects } from '../project'
import type { ObjectNode, ValueNode } from '../project'
import type { ObjectLayout } from './objects'
import { encodeObjectValue } from './objects'
import { batchFrames, batchOverhead, dataRecords } from './program'

/*
 * Packets for a loaded program: telemetry subscription (0x47), execution
 * control (0x48) and value writes (0x43; applied at the start of the next
 * pass while running). Frames are [class][packet][body], seq excluded.
 */

/**
 * Replace the telemetry list with these objects. One packet: the device
 * replaces the whole list per packet, so it can't be split. At most
 * CONFIG_VM_SUB_MAX_SUBSCRIBERS IDs (the subscribe packet's batch max).
 */
export const packSubscribe = (catalog: VmCatalog, wireIds: readonly number[], maxFrameBytes: number): Uint8Array => {
  const { header, recordSize, batch } = catalog.wire.subscribe
  const max = batch?.max ?? 0
  const overhead = batchOverhead(catalog, 'subscribe')
  const size = overhead + wireIds.length * recordSize
  if (wireIds.length > max) throw new RangeError(`${wireIds.length} objects, the board tracks at most ${max} subscriptions. Subscribe to fewer (a folder brings its children).`)
  if (size > maxFrameBytes) throw new RangeError(`Subscribing to ${wireIds.length} objects needs a ${size}-byte frame, the link carries ${maxFrameBytes}. Subscribe to fewer (a folder brings its children).`)
  const frame = new Uint8Array(size)
  const view = new DataView(frame.buffer)
  frame.set([catalog.classHeader, header])
  if (batch?.countBytes === 2) view.setUint16(2, wireIds.length, true)
  else frame[2] = wireIds.length
  wireIds.forEach((id, index) => view.setUint16(overhead + index * recordSize, id, true))
  return frame
}

/**
 * Wire IDs of the objects marked `subscribed`, in wire ID order. A subscribed
 * folder already brings everything under it, so marked objects below one are
 * left out. References count as their target.
 */
export const subscribedWireIds = (objects: readonly ObjectNode[], layout: ObjectLayout): number[] => {
  const ids = new Set<number>()
  walkObjects(objects, (node, ancestors) => {
    if (node.kind === 'reference' || !node.subscribed) return
    if (ancestors.some((folder) => folder.subscribed)) return
    const wireId = layout.wireIdOf.get(node.id)
    if (wireId !== undefined) ids.add(wireId)
  })
  return [...ids].sort((a, b) => a - b)
}

export const packExec = (catalog: VmCatalog, command: number): Uint8Array => Uint8Array.from([catalog.classHeader, catalog.packets.exec, command])

/**
 * Write `value` to an uploaded value object, from element 0. Zeros are sent
 * (unlike at load). A text is padded with zeros to the object's capacity, so
 * a shorter text doesn't leave the end of the old one behind. Throws when the
 * object isn't in the layout or the value doesn't fit it.
 */
export const packValueWrite = (catalog: VmCatalog, layout: ObjectLayout, node: ValueNode, value: ValueNode['value'], maxFrameBytes: number): Uint8Array[] => {
  const wireId = layout.wireIdOf.get(node.id)
  const placed = wireId === undefined ? undefined : layout.objects[wireId]
  if (!placed || placed.node.kind !== 'value') throw new Error(`'${node.name}' is not in the uploaded program; upload again.`)
  const { bytes, problem } = encodeObjectValue({ ...node, value }, placed.type, true)
  if (problem) throw new Error(`'${node.name}': ${problem}.`)
  if (!bytes) throw new Error(`'${node.name}': nothing to write.`)
  let wire = bytes
  if (placed.type.key === 'STR' && bytes.byteLength < placed.elements) {
    wire = new Uint8Array(placed.elements)
    wire.set(bytes)
  }
  return batchFrames(catalog, 'setData', dataRecords(catalog, [{ wireId: placed.wireId, bytes: wire, wireWidth: placed.type.wireWidth }], maxFrameBytes), maxFrameBytes)
}
