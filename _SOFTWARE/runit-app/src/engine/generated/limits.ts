import { FRAME_MAX_SYMBOL, LIMIT_SYMBOLS } from '../wire/load/limits'
import type { VmLimits } from '../wire/load/limits'
import type { EnumTables } from './wire'

/** The shape of generated/common/limits.json that this adapter reads. */
export interface GeneratedLimits {
  readonly limits: Readonly<Record<string, { readonly value: number }>>
}

/**
 * The board's limits from the published Kconfig values. An option the firmware no longer publishes stops the load with a
 * message that names it, instead of the engine checking against a number nobody confirmed; one that publishes none (no VM) leaves
 * the engine's defaults.
 */
export const limitsFromGenerated = (published: GeneratedLimits): Partial<VmLimits> =>
  // A firmware without a VM publishes none of them: the engine keeps its DEFAULT_LIMITS.
  !Object.values<string>(LIMIT_SYMBOLS).some((symbol) => published.limits[symbol]) ? {} : Object.fromEntries(
    Object.entries<string>(LIMIT_SYMBOLS).map(([key, symbol]) => {
      const limit = published.limits[symbol]
      if (!limit) throw new Error(`generated/common/limits.json has no limit '${symbol}' (mark its option in a Kconfig with #@limit).`)
      return [key, limit.value]
    }),
  ) as Partial<VmLimits>

/** The largest frame: cmd.h's `CMD_FRAME_MAX`, published in generated/common/enums.json (group `cmd_frame`). */
export const frameMaxFromGenerated = (enums: EnumTables): number => {
  const member = enums[FRAME_MAX_SYMBOL.group]?.members.find((m) => m.name === FRAME_MAX_SYMBOL.name)
  if (!member) throw new Error(`generated/common/enums.json has no '${FRAME_MAX_SYMBOL.name}' in group '${FRAME_MAX_SYMBOL.group}' (cmd.h).`)
  return member.value
}
