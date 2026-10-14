import type { BlockDescriptor } from '../../backend/vm/blocks'

/*
 * The bytecode of an expression block (EXPR, EXPR_BIT) as its generated descriptor publishes it (`encoding` in
 * data-structures/vm/blocks/<block>.block.json): the opcodes with their aliases, how many values each pops and pushes, the
 * constant type and the stack limit. The formula language (expression.ts) is built from it, so an opcode added in C is usable.
 */

export interface VmOpcode {
  readonly symbol: string
  readonly value: number
  readonly alias: string
  readonly description?: string
  readonly pops: number
  readonly pushes: number
  /** none, input (an input pin index) or const (a constant index): one byte after the opcode. */
  readonly operand: string
}

export interface VmBlockEncoding {
  /** Type of the 4-byte constants: f32 (EXPR) or u32 (EXPR_BIT). */
  readonly constantType: string
  readonly stackMax: number
  readonly opcodes: readonly VmOpcode[]
  /** By alias (`+`, `in`), symbol (`VM_EXPR_ADD`) or symbol without its prefix (`ADD`). */
  readonly opcode: (token: string) => VmOpcode | undefined
}

interface RawEncoding {
  readonly kind: string
  readonly constant_type: string
  readonly stack_max: number
  readonly opcodes: readonly VmOpcode[]
}

/** The block's bytecode, or undefined for a block that has none. */
export const blockEncoding = (descriptor: BlockDescriptor | undefined): VmBlockEncoding | undefined => {
  const raw = (descriptor as { encoding?: RawEncoding } | undefined)?.encoding
  if (!raw || raw.kind !== 'bytecode') return undefined
  const byToken = new Map<string, VmOpcode>()
  for (const entry of raw.opcodes) {
    byToken.set(entry.alias, entry)
    byToken.set(entry.symbol, entry)
    byToken.set(entry.symbol.replace(/^VM_[A-Z]+_/, ''), entry)
  }
  return {
    constantType: raw.constant_type,
    stackMax: raw.stack_max,
    opcodes: raw.opcodes,
    opcode: (token: string) => byToken.get(token) ?? byToken.get(token.toUpperCase()),
  }
}
