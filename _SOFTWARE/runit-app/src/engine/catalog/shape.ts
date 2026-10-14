import type { BlockType, Field, Pin, PinGroup, PinSpec, Variant } from './types'

/*
 * What the board checks of a block's wiring at load (`vm_block_verify`, block/vm_blocks.c; the numbers of each type are in its
 * VM_BLOCK_TYPE_DEFINE): the least and most inputs and outputs, the enables it needs, which inputs must be wired. They follow
 * from the descriptor: a pin is a slot, a required pin raises the least count, a group adds its own range.
 */

export interface BlockShape {
  /** The block's pins, concrete: shared ones first, then the variant's own, then one per field that has an input. */
  readonly inputs: readonly PinSpec[]
  readonly outputs: readonly PinSpec[]
  /** Shared fields, then the variant's. */
  readonly fields: readonly Field[]
  readonly minIn: number
  readonly maxIn: number
  readonly minQ: number
  readonly maxQ: number
  /** 1 when the block does nothing without an enable source (edge, latch). */
  readonly minEn: number
  /** Bit n: input n must be wired (`required_in`). */
  readonly requiredIn: number
}

const isGroup = (p: PinSpec): p is PinGroup => p.kind === 'group'

/** [least, most] slots a list of pins takes: pins count once each (a required one raises the least), a group its own range. */
const slots = (pins: readonly PinSpec[]): { readonly min: number; readonly max: number; readonly required: number } => {
  let min = 0
  let max = 0
  let required = 0
  for (const p of pins) {
    if (isGroup(p)) {
      max += p.max
      min = max - p.max + p.min
      continue
    }
    if (p.required) {
      min = max + 1
      required |= 1 << max
    }
    max += 1
  }
  return { min, max, required }
}

const pinOfField = (f: Field): Pin => ({
  kind: 'pin',
  id: f.id,
  alias: f.alias,
  ...(f.description !== undefined ? { description: f.description } : {}),
  value: f.type === 'accessor' ? 'object' : f.type === 'boolean' ? 'bool' : f.rawType === 'float' || (f.scale !== undefined && f.scale !== 1) ? 'float' : 'int',
  required: f.input === 'required',
  overrides: f.id,
  writes: false,
  hints: f.hints,
  field: f.id,
})

/** The variant of a block by id, or the only one; undefined for a block without variants. */
export const variantOf = (block: BlockType, variantId?: string): Variant | undefined => {
  if (block.variants.length === 0) return undefined
  return block.variants.find((v) => v.id === variantId) ?? block.variants[0]
}

/**
 * The shape of a block (and variant) as the board will check it. Pins that come from a variant's fields (a device command's
 * arguments) follow the pins the block lists itself, in the field order: that is the slot order of the block's inputs.
 */
export const shapeOf = (block: BlockType, variantId?: string): BlockShape => {
  const variant = variantOf(block, variantId)
  const fields = [...block.fields, ...(variant?.fields ?? [])]
  const own = [...block.inputs, ...(variant?.inputs ?? [])]
  // Block-level fields with an input are already pins of the block (fromGenerated.ts); only the variant's are added here.
  const derived = (variant?.fields ?? []).filter((f) => f.input !== 'none').map(pinOfField)
  const inputs = [...own, ...derived]
  const outputs = [...block.outputs, ...(variant?.outputs ?? [])]
  const i = slots(inputs)
  const q = slots(outputs)
  return {
    inputs,
    outputs,
    fields,
    minIn: i.min,
    maxIn: i.max,
    minQ: q.min,
    maxQ: q.max,
    minEn: block.enable?.required ? 1 : 0,
    requiredIn: i.required,
  }
}
