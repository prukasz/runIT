import type { BlockType } from '../catalog/types'
import type { EnumTables } from '../generated/wire'
import { type DecompileResult, decompileState } from './decompile'
import { compileFormula, type Formula, type FormulaContext, type FormulaResult, formulaContext, formulaState } from './formula'
import { type Token, tokenize } from './parse'
import { type Flavor, OP_INFO, SYNTAX } from './syntax'

/*
 * The language of one expression block. The block's descriptor names the published enum that holds its opcodes (`opcodes.enum`:
 * `vm_expr_op_e` for floats, `vm_bit_op_e` for u32) and the type of its constants; the opcode numbers, aliases and descriptions
 * are read from that enum: a block's language is what the firmware's machine for that block can do.
 */

/** The machine a constant type belongs to: floats (`f32`) or whole numbers with bit operations (`u32`). */
const FLAVOR: Readonly<Record<'f32' | 'u32', Flavor>> = { f32: 'float', u32: 'bit' }

/** An operator written between (or before) its operands. */
export interface OperatorInfo {
  readonly symbol: string
  readonly op: string
  readonly arity: 1 | 2
  /** Higher binds tighter. */
  readonly precedence: number
}

/** An operation written as `name(a, b)`. */
export interface FunctionInfo {
  readonly name: string
  readonly op: string
  readonly arity: number
  /** What the firmware says it does with the stack (`a b -> a+b`). */
  readonly description: string
}

export interface ExpressionLanguage {
  /** The block type this language belongs to. */
  readonly block: string
  readonly editor: string
  readonly flavor: Flavor
  /** The opcodes of the block's machine. */
  readonly context: FormulaContext
  readonly operators: readonly OperatorInfo[]
  /** Every callable operation, by its first name (its alias, else its opcode name). */
  readonly functions: readonly FunctionInfo[]
  /** Names that stand for a number: `pi`, `true`. */
  readonly constants: Readonly<Record<string, number>>
  /** The text split for colouring; stops at a character that belongs to nothing. */
  tokenize(text: string): ReturnType<typeof tokenize>
  /** The text as the formula the board runs. `inputCount`: inputs the block has; `wired(i)`: whether input i has a wire. */
  compile(text: string, inputCount: number, wired?: (pin: number) => boolean): FormulaResult
  /** The state bytes of a compiled formula (the block's `state`). */
  state(formula: Formula): Uint8Array
  /** The formula in a block's state bytes, as text. */
  decompile(state: Uint8Array): DecompileResult
}

export type { Token }

/** The language of a block type, from the generated enums; undefined when the block has no `opcodes`. */
export const expressionLanguage = (enums: EnumTables, type: Pick<BlockType, 'id' | 'editor' | 'opcodes'>): ExpressionLanguage | undefined => {
  const { editor, opcodes } = type
  if (editor === undefined || opcodes === undefined) return undefined
  const flavor = FLAVOR[opcodes.constant]
  const context = formulaContext(flavor, enums, opcodes.enum)
  const syntax = SYNTAX[flavor]
  const table = OP_INFO[flavor]

  const operators: OperatorInfo[] = []
  syntax.binary.forEach((entries, level) => entries.forEach(([symbol, op]) => context.opcodes.has(op) && operators.push({ symbol, op, arity: 2, precedence: level })))
  if (syntax.power && context.opcodes.has(syntax.power[1])) operators.push({ symbol: syntax.power[0], op: syntax.power[1], arity: 2, precedence: syntax.binary.length + 1 })
  for (const [symbol, op] of Object.entries(syntax.unary)) if (context.opcodes.has(op)) operators.push({ symbol, op, arity: 1, precedence: syntax.binary.length })

  const first = new Map<string, string>() // opcode name -> the name a palette lists it by
  for (const [name, entry] of context.functions) {
    const have = first.get(entry.op)
    const preferred = context.aliases.get(entry.op)
    if (!have || name === preferred) first.set(entry.op, name)
  }
  const functions = [...first]
    .filter(([op]) => context.opcodes.has(op) && table[op] !== undefined)
    .map(([op, name]): FunctionInfo => ({ name, op, arity: table[op]!.pops, description: context.descriptions.get(op) ?? '' }))
    .sort((a, b) => a.name.localeCompare(b.name))

  return {
    block: type.id,
    editor,
    flavor: flavor,
    context,
    operators,
    functions,
    constants: syntax.constants,
    tokenize: (text) => tokenize(text, flavor),
    compile: (text, inputCount, wired = () => true) => compileFormula(context, text, inputCount, wired),
    state: (formula) => formulaState(flavor, formula),
    decompile: (state) => decompileState(context, state),
  }
}
