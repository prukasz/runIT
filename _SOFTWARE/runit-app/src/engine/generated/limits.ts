import { LIMIT_SYMBOLS } from '../wire/limits'
import type { VmLimits } from '../wire/limits'

/** The shape of generated/common/limits.json that this adapter reads. */
export interface GeneratedLimits {
  readonly limits: Readonly<Record<string, { readonly value: number }>>
}

/**
 * The board's limits from the published Kconfig values. An option the firmware no longer publishes stops the load with a
 * message that names it, instead of the engine checking against a number nobody confirmed.
 */
export const limitsFromGenerated = (published: GeneratedLimits): Partial<VmLimits> =>
  Object.fromEntries(
    Object.entries<string>(LIMIT_SYMBOLS).map(([key, symbol]) => {
      const limit = published.limits[symbol]
      if (!limit) throw new Error(`generated/common/limits.json has no limit '${symbol}' (mark its option in a Kconfig with #@limit).`)
      return [key, limit.value]
    }),
  ) as Partial<VmLimits>
