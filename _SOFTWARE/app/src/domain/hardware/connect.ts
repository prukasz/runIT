import { flowOf, isSupply } from './hardware'
import type { PinSpec } from './hardware'

/*
 * Which pins may be wired together: kind (ground to ground, supply to supply, a bus signal to the same signal) and flow
 * (a producer feeds a consumer; a pin that can be either, like a GPIO, takes the role the other end needs). The IO mode
 * does not decide: a GPIO's digital output may feed an analog input, which reads the level. This is what the "available
 * only" highlight and the refusal of a wire dropped on a wrong pin use.
 */

/** A UART's transmit meets the other side's receive; every other bus signal meets itself. */
const CROSSED: Readonly<Record<string, string>> = { 'uart-tx': 'uart-rx', 'uart-rx': 'uart-tx' }

export const signalsMatch = (a: string | undefined, b: string | undefined): boolean => !!a && !!b && (CROSSED[a] ?? a) === b

/** Why `a` and `b` must not be wired together, or undefined when they may. */
export function connectionProblem(a: PinSpec, b: PinSpec): string | undefined {
  if (a.kind === 'gnd' || b.kind === 'gnd') return a.kind === b.kind ? undefined : 'Ground connects only to ground.'

  if (isSupply(a.kind) || isSupply(b.kind)) {
    if (!isSupply(a.kind) || !isSupply(b.kind)) return 'A supply connects only to a supply pin.'
    if (a.kind === 'supply_out' && b.kind === 'supply_out' && (!a.net || a.net !== b.net)) return 'Two supply outputs would short.'
    return undefined
  }

  if (a.kind === 'bus' || b.kind === 'bus') {
    if (a.kind !== b.kind) return 'A bus pin connects only to the same bus signal.'
    return signalsMatch(a.signal, b.signal) ? undefined : `${a.label} and ${b.label} are not the same bus signal.`
  }

  if (flowOf(a) === 'producer' && flowOf(b) === 'producer') return `${a.label} and ${b.label} are both producers: one of them has to consume.`
  return undefined
}
