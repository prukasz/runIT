import { shapeOf } from '../../catalog/shape'
import type { BlockShape } from '../../catalog/shape'
import type { BlockType, Pin, PinGroup, PinSpec } from '../../catalog/types'
import type { BlockInstance } from './program'

/*
 * A block instance's concrete pins: the type's (and variant's) pin list with each group expanded to the number of pins the
 * user gave it (`groups.inputs` / `groups.outputs`, the group's least when unset). These positions are the ones
 * `BlockInstance.inputs` / `.outputs` and the compiler use.
 */

export interface ConcretePin {
  /** Position among the block's inputs (or outputs). */
  readonly index: number
  readonly pin: Pin
  /** The group the pin belongs to, and its place in the group. */
  readonly group?: { readonly spec: PinGroup; readonly n: number }
}

export interface ConcretePins {
  readonly shape: BlockShape
  readonly inputs: readonly ConcretePin[]
  readonly outputs: readonly ConcretePin[]
}

const slug = (text: string): string => text.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') || 'pin'

const expand = (specs: readonly PinSpec[], count: number | undefined): ConcretePin[] => {
  const out: ConcretePin[] = []
  for (const spec of specs) {
    if (spec.kind === 'pin') {
      out.push({ index: out.length, pin: spec })
      continue
    }
    const n = Math.max(spec.min, Math.min(spec.max, count ?? spec.min))
    for (let i = 0; i < n; i++) {
      const pin: Pin = {
        kind: 'pin',
        id: `${slug(spec.alias)}${i}`,
        alias: `${spec.alias} ${i}`,
        ...(spec.description !== undefined ? { description: spec.description } : {}),
        value: spec.value,
        required: spec.required,
        ...(spec.flow ? { flow: spec.flow } : {}),
        writes: false,
        hints: [],
      }
      out.push({ index: out.length, pin, group: { spec, n: i } })
    }
  }
  return out
}

export const concretePins = (type: BlockType, instance: Pick<BlockInstance, 'variant' | 'groups'>): ConcretePins => {
  const shape = shapeOf(type, instance.variant)
  return { shape, inputs: expand(shape.inputs, instance.groups?.inputs), outputs: expand(shape.outputs, instance.groups?.outputs) }
}
