import type { EnumTables } from '../generated/wire'
import { Failure, type FormulaError, type Node, parse } from './parse'
import { type Flavor, OP_INFO, STACK_MAX, functionTable } from './syntax'
import type { FunctionEntry } from './syntax'

/*
 * Formulas: the text a user writes in an expression block, as the bytecode the board runs. The state is
 *
 *   u8 const_cnt, u8 rt (0), u16 code_len, const_cnt x 4 bytes (a float, or a u32 for the bit block), code_len bytes of code
 *
 * and the board checks it once at load: every opcode known, every operand in range and an input wired, the stack never short
 * or over 16, ending with exactly one value. The syntax and the stack effects are in `syntax.ts`.
 */

export interface Formula {
  /** The constants of the program, in the order the code reads them. */
  readonly constants: readonly number[]
  readonly code: Uint8Array
  /** The most the stack holds at once. */
  readonly depth: number
  /** The inputs the formula reads. */
  readonly inputs: readonly number[]
}

export type FormulaResult = { readonly ok: true; readonly formula: Formula } | { readonly ok: false; readonly error: FormulaError }

/** The opcodes of one block, from the generated enum of that block's expression machine. */
export interface FormulaContext {
  readonly flavor: Flavor
  /** Opcode name (without prefix) -> number, from the generated enum. */
  readonly opcodes: ReadonlyMap<string, number>
  /** Opcode name -> alias, from the generated enum (`+`, `mod`, `ln`): the words a formula may call a function by. */
  readonly aliases: ReadonlyMap<string, string>
  /** Opcode name -> what the firmware says it does (`a b -> a+b`). */
  readonly descriptions: ReadonlyMap<string, string>
  /** The names a formula may call as functions. */
  readonly functions: ReadonlyMap<string, FunctionEntry>
}

/** The prefix every member of the group shares (`VM_EXPR_`), up to its last underscore. */
const prefixOf = (names: readonly string[]): string => {
  let prefix = names[0] ?? ''
  for (const name of names) while (!name.startsWith(prefix)) prefix = prefix.slice(0, -1)
  return prefix.slice(0, prefix.lastIndexOf('_') + 1)
}

/** What a formula needs from the generated enum `enumName` (`vm_expr_op_e`, `vm_bit_op_e`). */
export const formulaContext = (flavor: Flavor, enums: EnumTables, enumName: string): FormulaContext => {
  const table = enums[enumName]
  if (!table) throw new Error(`generated/common/enums.json has no enum '${enumName}'.`)
  const opcodes = new Map<string, number>()
  const aliases = new Map<string, string>()
  const descriptions = new Map<string, string>()
  const prefix = prefixOf(table.members.map((m) => m.name))
  for (const member of table.members) {
    if (member.name.endsWith('_OP_CNT')) continue
    const name = member.name.slice(prefix.length)
    opcodes.set(name, member.value)
    if (member.alias) aliases.set(name, member.alias.toLowerCase())
    if (member.description) descriptions.set(name, member.description)
  }
  return { flavor, opcodes, aliases, descriptions, functions: functionTable(flavor, aliases) }
}

/** Compiles a formula. `inputCount` is how many inputs the block has; `wired(i)` says whether input i has a wire. */
export const compileFormula = (context: FormulaContext, text: string, inputCount: number, wired: (pin: number) => boolean): FormulaResult => {
  try {
    if (text.trim() === '') throw new Failure({ message: 'The formula is empty.', from: 0, to: 0 })
    const tree = parse(text, context.flavor, context.functions)
    const table = OP_INFO[context.flavor]
    const constants: number[] = []
    const code: number[] = []
    const inputs = new Set<number>()
    let depth = 0
    let max = 0
    const opcode = (name: string, node: Node): number => {
      const value = context.opcodes.get(name)
      if (value === undefined) throw new Failure({ message: `The board's firmware has no '${name}' operation.`, from: node.from, to: node.to })
      return value
    }
    const emit = (node: Node): void => {
      if (node.kind === 'num') {
        let index = constants.findIndex((c) => Object.is(c, node.value))
        if (index < 0) {
          index = constants.push(node.value) - 1
          if (index > 0xfe) throw new Failure({ message: 'Too many different numbers in one formula (255 at most).', from: node.from, to: node.to })
        }
        code.push(opcode('K', node), index)
        depth += 1
      } else if (node.kind === 'in') {
        if (node.pin >= inputCount) throw new Failure({ message: `in${node.pin} does not exist: the block has ${inputCount} input${inputCount === 1 ? '' : 's'}. Add an input.`, from: node.from, to: node.to })
        if (!wired(node.pin)) throw new Failure({ message: `in${node.pin} is not wired: connect something to it.`, from: node.from, to: node.to })
        inputs.add(node.pin)
        code.push(opcode('IN', node), node.pin)
        depth += 1
      } else {
        for (const arg of node.args) emit(arg)
        const effect = table[node.op]!
        code.push(opcode(node.op, node))
        depth += effect.pushes - effect.pops
      }
      max = Math.max(max, depth)
      if (max > STACK_MAX) throw new Failure({ message: `The formula needs a stack of ${max}; the board has ${STACK_MAX}. Split it over two blocks.`, from: node.from, to: node.to })
    }
    emit(tree)
    if (code.length > 0xffff) throw new Failure({ message: 'The formula is too long.', from: 0, to: text.length })
    return { ok: true, formula: { constants, code: Uint8Array.from(code), depth: max, inputs: [...inputs].sort((a, b) => a - b) } }
  } catch (error) {
    if (error instanceof Failure) return { ok: false, error: error.error }
    throw error
  }
}

/** The block's state for a formula: header, constants (floats, or u32 for the bit block), code. */
export const formulaState = (flavor: Flavor, formula: Formula): Uint8Array => {
  const bytes = new Uint8Array(4 + formula.constants.length * 4 + formula.code.length)
  const view = new DataView(bytes.buffer)
  view.setUint8(0, formula.constants.length)
  view.setUint8(1, 0) // rt: the board's own flag
  view.setUint16(2, formula.code.length, true)
  formula.constants.forEach((value, i) => (flavor === 'float' ? view.setFloat32(4 + i * 4, value, true) : view.setUint32(4 + i * 4, value >>> 0, true)))
  bytes.set(formula.code, 4 + formula.constants.length * 4)
  return bytes
}

/**
 * The inverse of `formulaState`: the constants and the code in a block's state (as the board stores it). Undefined when the
 * bytes are not a formula state (shorter than the header says).
 */
export const parseFormulaState = (flavor: Flavor, state: Uint8Array): { readonly constants: readonly number[]; readonly code: Uint8Array } | undefined => {
  if (state.length < 4) return undefined
  const view = new DataView(state.buffer, state.byteOffset, state.byteLength)
  const count = view.getUint8(0)
  const length = view.getUint16(2, true)
  if (state.length < 4 + count * 4 + length) return undefined
  const constants = Array.from({ length: count }, (_, i) => (flavor === 'float' ? view.getFloat32(4 + i * 4, true) : view.getUint32(4 + i * 4, true)))
  return { constants, code: state.slice(4 + count * 4, 4 + count * 4 + length) }
}
