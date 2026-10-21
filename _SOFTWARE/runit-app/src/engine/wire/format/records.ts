import { ByteWriter } from './bytes'
import type { WireDefinitions } from './definitions'
import type { AccessorDef, BlockDef, DataDef, ObjectDef, ObjectFlags } from './program'
import { NAME_MAX, OBJ_WIDTH } from './types'

/*
 * One function per record of loader/vm_wire.h, packed little-endian. A record is the fixed struct followed by its
 * variable parts in order; a batch frame body is `u8 count` and the records (frames.ts).
 */

const utf8 = (text: string): Uint8Array => new TextEncoder().encode(text)

/** The object head's flag byte from the flags a program sets. */
export const flagBits = (defs: WireDefinitions, flags: ObjectFlags): number =>
  (flags.mutable ? defs.headFlags.mutable : 0) |
  (flags.retentive ? defs.headFlags.retentive : 0) |
  (flags.usrProtected ? defs.headFlags.usrProtected : 0)

/** `vm_wire_open_t`: object, accessor and block slots, and the arena size (10 bytes). */
export const openRecord = (counts: { readonly objects: number; readonly accessors: number; readonly blocks: number }, arenaBytes: number): Uint8Array =>
  new ByteWriter().u16(counts.objects).u16(counts.accessors).u16(counts.blocks).u32(arenaBytes).finish()

/** `vm_wire_obj_t` + tag: id, the object head (`payload_size` in memory bytes, type | name length << 4, flags), the name. */
export const objectRecord = (defs: WireDefinitions, object: ObjectDef): Uint8Array => {
  const name = utf8(object.name)
  if (name.length > NAME_MAX) throw new RangeError(`Object name '${object.name}' is ${name.length} bytes; the limit is ${NAME_MAX}.`)
  const payload = object.count * OBJ_WIDTH[object.type].mem
  if (!Number.isInteger(object.count) || object.count < 1 || payload > 0xffff) {
    throw new RangeError(`Object ${object.id} has ${object.count} elements of ${object.type} (${payload} bytes): the payload must be 1..65535 bytes.`)
  }
  return new ByteWriter()
    .u16(object.id)
    .u16(payload)
    .u8(defs.objTypes[object.type] | (name.length << 4))
    .u8(flagBits(defs, object.flags))
    .bytes(name)
    .finish()
}

/** `vm_wire_data_t` + data: id, first element, byte length. A PTR object's data is its children's u16 ids. */
export const dataRecord = (data: DataDef): Uint8Array =>
  new ByteWriter().u16(data.id).u16(data.start).u16(data.data.length).bytes(data.data).finish()

/** `vm_wire_acc_t` + steps: accessor id, root object, step count and byte length, then the steps back to back. */
export const accessorRecord = (defs: WireDefinitions, accessor: AccessorDef): Uint8Array => {
  const steps = new ByteWriter()
  for (const step of accessor.steps) {
    if (step.kind === 'literal') steps.u8(defs.idxKinds.literal).u32(step.value)
    else if (step.kind === 'ref') steps.u8(defs.idxKinds.ref).u16(step.accessor)
    else {
      const name = utf8(step.name)
      if (name.length > NAME_MAX) throw new RangeError(`Accessor step name '${step.name}' is ${name.length} bytes; the limit is ${NAME_MAX}.`)
      steps.u8(defs.idxKinds.name).u8(name.length).bytes(name)
    }
  }
  const body = steps.finish()
  if (accessor.steps.length > 0xff || body.length > 0xff) {
    throw new RangeError(`Accessor ${accessor.id} has ${accessor.steps.length} steps in ${body.length} bytes; the record holds at most 255 of each.`)
  }
  return new ByteWriter().u16(accessor.id).u16(accessor.root).u8(accessor.steps.length).u8(body.length).bytes(body).finish()
}

/** `vm_wire_block_t` + input accessor ids, output object ids, enable accessor ids (u16 each), then the state. */
export const blockRecord = (defs: WireDefinitions, block: BlockDef): Uint8Array => {
  if (block.inputs.length > 0xff || block.outputs.length > 0xff || block.enables.length > 0xff || block.state.length > 0xffff) {
    throw new RangeError(`Block ${block.id} is too large for its record (${block.inputs.length} in, ${block.outputs.length} out, ${block.enables.length} en, ${block.state.length} bytes of state).`)
  }
  const out = new ByteWriter()
    .u16(block.id)
    .u16(block.index)
    .u8(block.type)
    .u8(block.inputs.length)
    .u8(block.outputs.length)
    .u8(block.enables.length)
    .u8(defs.enMode[block.enableMode])
    .u8(defs.onError[block.onError])
    .u16(block.state.length)
    .u16(block.eno)
  for (const id of [...block.inputs, ...block.outputs, ...block.enables]) out.u16(id)
  return out.bytes(block.state).finish()
}

/** `vm_wire_sub_t`: an object id. The frame body is `u8 count` and these. */
export const subscribeRecord = (id: number): Uint8Array => new ByteWriter().u16(id).finish()
