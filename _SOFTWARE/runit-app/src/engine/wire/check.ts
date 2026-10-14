import { arenaUsage } from './arena'
import { DEFAULT_LIMITS } from './limits'
import type { VmLimits } from './limits'
import type { ProgramIR } from './program'
import { ID_NONE, NAME_MAX, OBJ_WIDTH } from './types'

/**
 * A reason the board would refuse a program at load. `code` is the firmware's error tag where one exists (see
 * generated/common/errors.json), so a problem found here and the board's refusal read the same.
 */
export interface ProgramProblem {
  readonly code: string
  readonly where: { readonly kind: 'program' | 'object' | 'data' | 'accessor' | 'block'; readonly id?: number }
  readonly message: string
}

const utf8Length = (text: string): number => new TextEncoder().encode(text).length

/** The most a retained object adds to the stored record: head 4 + name + value (VM_CORE.MD, `CONFIG_VM_RETAIN_MAX_BYTES`). */
const RETAIN_RECORD_BASE = 2
const RETAIN_RECORD_PER_OBJECT = 4

/**
 * Every load rule the board enforces that can be known before the upload: slot ids (dense, unique, never 0xFFFF), object
 * shapes, initial values, accessor references (only to accessors loaded earlier, within the depth limit), block wiring
 * (existing objects and accessors, no output named twice, an ENO that is a mutable bool and not an output), pin and state
 * sizes, and the arena. Sources: loader/vm_loader.c, obj/vm_obj_build.c, block/vm_block_build.c, store/vm_store.c.
 * Block-type rules (pin shape against the palette, the type's own check) need the block catalog: see compiler/.
 */
export const checkProgram = (ir: ProgramIR, limits: VmLimits = DEFAULT_LIMITS): ProgramProblem[] => {
  const problems: ProgramProblem[] = []
  const add = (code: string, kind: ProgramProblem['where']['kind'], id: number | undefined, message: string) =>
    problems.push({ code, where: id === undefined ? { kind } : { kind, id }, message })

  /** Ids must fill 0..count-1 exactly once each (`vm_loader_verify_complete`, ERR_VM_REG_OOB / _DUP). */
  const slots = (kind: 'object' | 'accessor' | 'block', ids: readonly number[]): Set<number> => {
    const seen = new Set<number>()
    for (const id of ids) {
      if (id === ID_NONE || id >= ids.length) add('ERR_VM_REG_OOB', kind, id, `The ${kind} id ${id} is outside 0..${ids.length - 1}.`)
      else if (seen.has(id)) add('ERR_VM_REG_DUP', kind, id, `The ${kind} id ${id} is used twice.`)
      seen.add(id)
    }
    for (let id = 0; id < ids.length; id++) if (!seen.has(id)) add('ERR_VM_LOAD_INCOMPLETE', kind, id, `No ${kind} fills slot ${id}.`)
    return seen
  }

  const objects = new Map(ir.objects.map((o) => [o.id, o]))
  slots('object', ir.objects.map((o) => o.id))
  slots('accessor', ir.accessors.map((a) => a.id))
  slots('block', ir.blocks.map((b) => b.id))

  let retained = RETAIN_RECORD_BASE
  for (const object of ir.objects) {
    const name = utf8Length(object.name)
    if (name > NAME_MAX) add('ERR_VM_OBJ_NAME_TOO_LONG', 'object', object.id, `The name '${object.name}' is ${name} bytes; the limit is ${NAME_MAX}.`)
    if (!Number.isInteger(object.count) || object.count < 1) add('ERR_VM_OBJ_EMPTY', 'object', object.id, `The object has no elements.`)
    else if (object.count * OBJ_WIDTH[object.type].mem > 0xffff) add('ERR_VM_OBJ_BAD_SIZE', 'object', object.id, `${object.count} elements of ${object.type} do not fit a 16-bit payload size.`)
    if (object.flags.retentive) {
      if (object.type === 'ptr') add('ERR_VM_OBJ_RETENTIVE_PTR', 'object', object.id, 'A folder (pointer object) cannot be retained: its pointers would dangle after a restart.')
      if (name === 0) add('ERR_VM_RETAIN_UNNAMED', 'object', object.id, 'A retained object needs a name: values are kept by name.')
      retained += RETAIN_RECORD_PER_OBJECT + name + object.count * OBJ_WIDTH[object.type].mem
    }
  }
  if (retained > limits.retainMaxBytes) add('ERR_VM_RETAIN_TOO_BIG', 'program', undefined, `Retained values need ${retained} bytes; the board keeps ${limits.retainMaxBytes}.`)

  for (const init of ir.data) {
    const object = objects.get(init.id)
    if (!object) {
      add('ERR_VM_ACCESSOR_UNKNOWN_ID', 'data', init.id, `Initial value for object ${init.id}, which does not exist.`)
      continue
    }
    const width = OBJ_WIDTH[object.type].wire
    if (init.data.length % width !== 0) add('ERR_VM_LOAD_DATA_RANGE', 'data', init.id, `${init.data.length} bytes is not a whole number of ${object.type} elements (${width} bytes each).`)
    else if (init.start + init.data.length / width > object.count) add('ERR_VM_LOAD_DATA_RANGE', 'data', init.id, `The value runs past the object's ${object.count} elements.`)
    if (object.type === 'ptr') {
      const view = new DataView(init.data.buffer, init.data.byteOffset, init.data.byteLength)
      for (let at = 0; at + 2 <= init.data.length; at += 2) {
        const child = view.getUint16(at, true)
        if (!objects.has(child)) add('ERR_VM_ACCESSOR_UNKNOWN_ID', 'data', init.id, `The folder lists object ${child}, which does not exist.`)
      }
    }
  }

  // Accessors load in id order; a reference step may name only an accessor loaded before it.
  const depth = new Map<number, number>()
  for (const accessor of [...ir.accessors].sort((a, b) => a.id - b.id)) {
    if (!objects.has(accessor.root)) add('ERR_VM_ACCESSOR_UNKNOWN_ID', 'accessor', accessor.id, `The path starts at object ${accessor.root}, which does not exist.`)
    let chain = 1
    for (const step of accessor.steps) {
      if (step.kind === 'ref') {
        if (step.accessor >= accessor.id) add('ERR_VM_REG_OOB', 'accessor', accessor.id, `A step reads accessor ${step.accessor}, which is not loaded before accessor ${accessor.id}.`)
        else chain = Math.max(chain, 1 + (depth.get(step.accessor) ?? 1))
      } else if (step.kind === 'name' && utf8Length(step.name) > NAME_MAX) {
        add('ERR_VM_OBJ_NAME_TOO_LONG', 'accessor', accessor.id, `The step name '${step.name}' is over ${NAME_MAX} bytes.`)
      }
    }
    depth.set(accessor.id, chain)
    if (chain > limits.accessorMaxDepth) add('ERR_VM_ACCESSOR_DEPTH_EXCEEDED', 'accessor', accessor.id, `Accessors read accessors ${chain} deep; the board resolves ${limits.accessorMaxDepth}.`)
  }
  const accessors = new Set(ir.accessors.map((a) => a.id))

  for (const block of ir.blocks) {
    const here = (code: string, message: string) => add(code, 'block', block.id, message)
    if (block.inputs.length > limits.maxIn || block.outputs.length > limits.maxOut || block.enables.length > limits.maxEn) {
      here('ERR_VM_BLK_BAD_SHAPE', `${block.inputs.length} inputs, ${block.outputs.length} outputs and ${block.enables.length} enables; the board takes ${limits.maxIn}, ${limits.maxOut} and ${limits.maxEn}.`)
    }
    block.inputs.forEach((id, pin) => {
      if (id !== ID_NONE && !accessors.has(id)) here('ERR_VM_BLK_BAD_REF', `Input ${pin} reads accessor ${id}, which does not exist.`)
    })
    block.enables.forEach((id, slot) => {
      if (!accessors.has(id)) here('ERR_VM_BLK_BAD_REF', `Enable ${slot} reads accessor ${id}, which does not exist.`)
    })
    block.outputs.forEach((id, pin) => {
      if (!objects.has(id)) here('ERR_VM_BLK_BAD_REF', `Output ${pin} writes object ${id}, which does not exist.`)
      else if (block.outputs.indexOf(id) !== pin) here('ERR_VM_BLK_OUTPUT_TAKEN', `Output ${pin} writes object ${id}, which another output of the block already writes.`)
    })
    if (block.eno !== ID_NONE) {
      const eno = objects.get(block.eno)
      if (!eno || !eno.flags.mutable || eno.type !== 'bool') here('ERR_VM_BLK_BAD_REF', `The ENO object ${block.eno} must exist and be a writable boolean.`)
      else if (block.outputs.includes(block.eno)) here('ERR_VM_BLK_OUTPUT_TAKEN', `The ENO object ${block.eno} is also an output of the block.`)
    }
  }

  const arena = arenaUsage(ir).total
  if (arena > limits.storeMaxPool) add('ERR_VM_LOAD_TOO_BIG', 'program', undefined, `The program needs ${arena} bytes of arena; the board's limit is ${limits.storeMaxPool}.`)
  return problems
}
